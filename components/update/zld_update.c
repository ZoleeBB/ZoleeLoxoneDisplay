#include "zld_update.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "board.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_ota_ops.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "mbedtls/sha256.h"

#ifndef ZLD_CARD_DIR
#define ZLD_CARD_DIR "/sdcard"
#endif
#define ROOT ZLD_CARD_DIR "/zld"
#define BLOBS ROOT "/blobs"
#define STAGE ROOT "/stage.idx"
#define PART ROOT "/transfer.tmp"
#define MAX_ASSETS 2048
#define MAX_FILE_SIZE (32U * 1024U * 1024U)
#define MAX_INDEX_SIZE (512U * 1024U)
#define PATH_CAP 160
#define CHUNK 2048
#define STR_IMPL(x) #x
#define STR(x) STR_IMPL(x)

typedef struct {
    const char *name;
    const char *sha;
    uint32_t size;
} asset_entry_t;

#include "zld_asset_index.inc"

#define ACTIVE_COUNT (sizeof(active_entries) / sizeof(active_entries[0]))
#define MARKER_PREFIX "ZLD-BUNDLE-1/" STR(BOARD_SKU) "/" BOARD_SILICON "/"
static const char running_marker[] = MARKER_PREFIX ZLD_GENERATION "/END";
static atomic_bool ready, card_online;
static uint8_t card_nonce[16];
/* The HTTP task and optional download worker hold one reference each. A negative
 * value reserves the imminent reboot and prevents a racing write from starting. */
static atomic_int update_users;
static bool mkdir_if_missing(const char *path);

bool zld_card_online(void)
{
    return atomic_load(&card_online) && atomic_load(&update_users) >= 0;
}

static bool read_session(void)
{
    /* Read one full sector through the POSIX fd. FatFs' full-sector path goes
     * directly to disk even when the directory entry is still in its cache. */
    uint8_t sector[512];
    int fd = open(ROOT "/session.bin", O_RDONLY);
    if (fd < 0) return false;
    ssize_t n = read(fd, sector, sizeof(sector));
    int err = close(fd);
    return !err && n == sizeof(sector) && !memcmp(sector, card_nonce, sizeof(card_nonce));
}

bool zld_card_session_start(void)
{
    /* Called exactly once after mount, before profile loading or starting tasks. */
    atomic_store(&card_online, false);
    uint8_t sector[512] = {0};
    esp_fill_random(card_nonce, sizeof(card_nonce));
    memcpy(sector, card_nonce, sizeof(card_nonce));
    if (!mkdir_if_missing(ROOT)) return false;
    int fd = open(ROOT "/session.bin", O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return false;
    bool ok = write(fd, sector, sizeof(sector)) == sizeof(sector) && fsync(fd) == 0;
    if (close(fd)) ok = false;
    ok = ok && read_session();
    atomic_store(&card_online, ok);
    return ok;
}

bool zld_card_verify(void)
{
    if (!zld_card_online()) return false;
    if (read_session()) return zld_card_online();
    atomic_store(&card_online, false);
    return false;
}

bool zld_card_needs_restart(void)
{
    /* app_main is the only polling caller. A short I/O failure temporarily
     * suspends controls, then the same card can pass the following sample. */
    static unsigned failures;
    if (atomic_load(&update_users) < 0) return true;
    if (read_session()) {
        failures = 0;
        atomic_store(&card_online, true);
        return false;
    }
    atomic_store(&card_online, false);
    if (failures < 2) ++failures;
    if (failures < 2) return false;
    int idle = 0;
    return atomic_compare_exchange_strong(&update_users, &idle, -1);
}

static bool hold_update(void)
{
    int users = atomic_load(&update_users);
    while (users >= 0) {
        if (atomic_compare_exchange_weak(&update_users, &users, users + 1)) return true;
    }
    return false;
}

enum { CLOUD_IDLE, CLOUD_RUNNING, CLOUD_OK, CLOUD_ERROR };
static atomic_int cloud_state;
static atomic_uint cloud_position;
static zld_update_authorize_t allow;
static char staged[65];
static bool reboot_pending;
static esp_timer_handle_t restart_timer;
extern const char update_html[] asm("_binary_update_html_start");

static bool blob_path(const asset_entry_t *entry, char *out, size_t cap)
{
    const char *ext = strrchr(entry->name, '.');
    if (!ext || !out || !cap) return false;
    int n = snprintf(out, cap, BLOBS "/%.2s/%s%s", entry->sha, entry->sha, ext);
    return n >= 0 && (size_t)n < cap;
}

bool zld_asset_path(const char *logical, char *out, size_t capacity)
{
    if (!logical || !out || !capacity) return false;
    if (!zld_card_online()) { out[0] = 0; return false; }
    out[0] = '\0';
    size_t lo = 0, hi = ACTIVE_COUNT;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int cmp = strcmp(logical, active_entries[mid].name);
        if (cmp == 0) return blob_path(&active_entries[mid], out, capacity);
        if (cmp < 0) hi = mid;
        else lo = mid + 1;
    }
    return false;
}

static void hex_digest(const uint8_t raw[32], char hex[65])
{
    static const char alphabet[] = "0123456789abcdef";
    for (int i = 0; i < 32; ++i) {
        hex[i * 2] = alphabet[raw[i] >> 4];
        hex[i * 2 + 1] = alphabet[raw[i] & 15];
    }
    hex[64] = 0;
}

static bool valid_sha(const char *value)
{
    return value && strlen(value) == 64 && strspn(value, "0123456789abcdef") == 64;
}

static bool blob_ok(const asset_entry_t *entry)
{
    char path[PATH_CAP], hex[65];
    struct stat st;
    if (!blob_path(entry, path, sizeof(path)) || stat(path, &st) ||
        st.st_size != entry->size) return false;
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    uint8_t buf[CHUNK], raw[32];
    mbedtls_sha256_context hash;
    mbedtls_sha256_init(&hash);
    int err = mbedtls_sha256_starts(&hash, 0);
    size_t count;
    while (!err && (count = fread(buf, 1, sizeof(buf), file)) != 0)
        err = mbedtls_sha256_update(&hash, buf, count);
    bool ok = !ferror(file) && !err && !mbedtls_sha256_finish(&hash, raw);
    mbedtls_sha256_free(&hash);
    fclose(file);
    if (!ok) return false;
    hex_digest(raw, hex);
    return !strcmp(hex, entry->sha);
}

bool zld_assets_check(void)
{
    if (!zld_card_verify()) return false;
    for (size_t i = 0; i < ACTIVE_COUNT; ++i) {
        if (!blob_ok(&active_entries[i])) {
            atomic_store(&ready, false);
            return false;
        }
    }
    bool same_card = zld_card_verify();
    atomic_store(&ready, same_card);
    return same_card;
}

bool zld_assets_ready(void)
{
    return atomic_load(&ready) && zld_card_online();
}

/* One sorted, whitespace-free logical name per line keeps staging bounded even
 * with hundreds of resources. No caller-supplied string is used as a VFS path. */
static bool parse_line(char *line, asset_entry_t *entry)
{
    char *end = strchr(line, '\n');
    if (!end || end - line < 68 || end[1] || line[64] != ' ') return false;
    *end = 0;
    line[64] = 0;
    if (!valid_sha(line)) return false;
    char *number = line + 65;
    if (*number < '1' || *number > '9') return false;
    errno = 0;
    unsigned long size = strtoul(number, &end, 10);
    if (errno || !size || size > MAX_FILE_SIZE || *end != ' ') return false;
    char *name = end + 1;
    size_t len = strlen(name);
    const char *ext = strrchr(name, '.');
    if (!len || len >= PATH_CAP || !ext || name[0] == '.' || name[0] == '/' ||
        strstr(name, "//") || strstr(name, "/.") || strlen(ext + 1) > 8 || !ext[1] ||
        strspn(ext + 1, "abcdefghijklmnopqrstuvwxyz0123456789") != strlen(ext + 1))
        return false;
    for (char *p = name; *p; ++p) {
        if (!isalnum((unsigned char)*p) && !strchr("._/-", *p)) return false;
    }
    *entry = (asset_entry_t){.name = name, .size = size, .sha = line};
    return true;
}

typedef bool (*index_visitor_t)(const asset_entry_t *, unsigned, void *);

static bool walk_stage(index_visitor_t visit, void *arg, const char *expected_sha)
{
    FILE *file = fopen(STAGE, "rb");
    if (!file) return false;
    unsigned i = 0;
    char previous[PATH_CAP] = "", line[256];
    mbedtls_sha256_context hash;
    mbedtls_sha256_init(&hash);
    bool ok = !mbedtls_sha256_starts(&hash, 0);
    while (ok && fgets(line, sizeof(line), file)) {
        asset_entry_t entry;
        if (i == MAX_ASSETS || mbedtls_sha256_update(&hash, (const uint8_t *)line, strlen(line)) ||
            !parse_line(line, &entry) ||
            strcmp(previous, entry.name) >= 0 || (visit && !visit(&entry, i, arg))) {
            ok = false;
            break;
        }
        strcpy(previous, entry.name);
        ++i;
    }
    uint8_t raw[32];
    char hex[65];
    ok = ok && !ferror(file) && i > 0 && !mbedtls_sha256_finish(&hash, raw);
    mbedtls_sha256_free(&hash);
    if (ok) { hex_digest(raw, hex); ok = !strcmp(hex, expected_sha); }
    fclose(file);
    return ok;
}

static esp_err_t fail(httpd_req_t *req, const char *status, const char *message)
{
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    return httpd_resp_sendstr(req, message);
}

static bool permitted(httpd_req_t *req, bool write)
{
    if (!allow(req, write)) return false;
    if (write && atomic_load(&cloud_state) == CLOUD_RUNNING) {
        fail(req, "409 Conflict", "A képek letöltése már folyamatban van.");
        return false;
    }
    if (write && reboot_pending) {
        fail(req, "409 Conflict", "A kijelző újraindul.");
        return false;
    }
    return true;
}

static bool header_equals(httpd_req_t *req, const char *key, const char *expected)
{
    char value[96];
    return httpd_req_get_hdr_value_str(req, key, value, sizeof(value)) == ESP_OK &&
           strcmp(value, expected) == 0;
}

static bool staged_request(httpd_req_t *req)
{
    if (!permitted(req, true)) return false;
    if (*staged && header_equals(req, "X-Pack-Manifest", staged)) return true;
    fail(req, "409 Conflict", "A feltöltés megváltozott. Válaszd ki újra a csomagot.");
    return false;
}

static bool mkdir_if_missing(const char *path)
{
    return mkdir(path, 0775) == 0 || errno == EEXIST;
}

/* Stream to flash or card and hash exactly the bytes written. */
static bool receive(httpd_req_t *req, FILE *file, esp_ota_handle_t ota, char hex[65])
{
    uint8_t buf[CHUNK], raw[32];
    mbedtls_sha256_context hash;
    mbedtls_sha256_init(&hash);
    int err = mbedtls_sha256_starts(&hash, 0);
    size_t left = req->content_len;
    while (!err && left) {
        size_t want = left < sizeof(buf) ? left : sizeof(buf);
        int n = httpd_req_recv(req, (char *)buf, want);
        if (n <= 0) { err = -1; break; }
        if (file) err = fwrite(buf, 1, n, file) != (size_t)n;
        else err = esp_ota_write(ota, buf, n) != ESP_OK;
        if (!err) err = mbedtls_sha256_update(&hash, buf, n);
        left -= n;
    }
    if (!err) err = mbedtls_sha256_finish(&hash, raw);
    mbedtls_sha256_free(&hash);
    if (err) return false;
    hex_digest(raw, hex);
    return true;
}

static bool receive_file(httpd_req_t *req, const char *path, char sha[65])
{
    FILE *file = fopen(path, "wb");
    if (!file) return false;
    bool ok = receive(req, file, 0, sha);
    if (fclose(file)) ok = false;
    if (!ok) unlink(path);
    return ok;
}

static esp_err_t page(httpd_req_t *req)
{
    if (!permitted(req, false)) return ESP_OK;
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_sendstr(req, update_html);
}

static esp_err_t info(httpd_req_t *req)
{
    if (!permitted(req, false)) return ESP_OK;
    char result[320];
    snprintf(result, sizeof(result),
        "{\"sku\":\"" STR(BOARD_SKU) "\",\"silicon\":\"" BOARD_SILICON
        "\",\"generation\":\"" ZLD_GENERATION "\",\"ready\":%s,\"remote\":%s,"
        "\"cloudState\":%d,\"position\":%u,\"count\":%u}",
        zld_assets_ready() ? "true" : "false", sizeof(ZLD_SOURCE_REV) == 41 ? "true" : "false",
        atomic_load(&cloud_state), atomic_load(&cloud_position), (unsigned)ACTIVE_COUNT);
    httpd_resp_set_type(req, "application/json");
    /* Also ensures the compatibility record is part of every linked image. */
    httpd_resp_set_hdr(req, "X-Running-Bundle", running_marker);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, result);
}

static bool report_missing(const asset_entry_t *entry, unsigned i, void *arg)
{
    if (blob_ok(entry)) return true;
    char text[16];
    int len = snprintf(text, sizeof(text), "%u\n", i);
    return httpd_resp_send_chunk(arg, text, len) == ESP_OK;
}

static esp_err_t prepare(httpd_req_t *req)
{
    if (!permitted(req, true)) return ESP_OK;
    char expected[65], actual[65];
    if (!req->content_len || req->content_len > MAX_INDEX_SIZE ||
        !header_equals(req, "X-Pack-Target", STR(BOARD_SKU) "-" BOARD_SILICON) ||
        httpd_req_get_hdr_value_str(req, "X-Pack-Manifest", expected, sizeof(expected)) != ESP_OK ||
        !valid_sha(expected))
        return fail(req, "400 Bad Request", "Eltérő hardver vagy érvénytelen képjegyzék.");
    staged[0] = 0;
    if (!mkdir_if_missing(ROOT) || !mkdir_if_missing(BLOBS) || !receive_file(req, STAGE, actual))
        return fail(req, "503 Service Unavailable", "A kártya nem írható vagy a feltöltés megszakadt.");
    if (strcmp(expected, actual) || !walk_stage(NULL, NULL, expected))
        return fail(req, "400 Bad Request", "Sérült vagy üres képjegyzék.");
    strcpy(staged, actual);
    httpd_resp_set_type(req, "text/plain");
    if (!walk_stage(report_missing, req, staged)) return ESP_FAIL;
    return httpd_resp_send_chunk(req, NULL, 0);
}

/* A power loss leaves either verified data or a retryable missing file. */
static bool install_temp(const asset_entry_t *entry)
{
    char path[PATH_CAP], shard[sizeof(BLOBS) + 4];
    snprintf(shard, sizeof(shard), BLOBS "/%.2s", entry->sha);
    if (!zld_card_verify() || !mkdir_if_missing(shard) || !blob_path(entry, path, sizeof(path))) return false;
    if (blob_ok(entry)) return unlink(PART) == 0;
    if (unlink(path) && errno != ENOENT) return false;
    return rename(PART, path) == 0;
}

typedef struct {
    FILE *file;
    size_t remaining;
    bool error;
    mbedtls_sha256_context hash;
} download_t;

static esp_err_t remote_event(esp_http_client_event_t *event)
{
    if (event->event_id != HTTP_EVENT_ON_DATA) return ESP_OK;
    download_t *transfer = event->user_data;
    if (!transfer->file || transfer->error) return ESP_FAIL;
    size_t n = event->data_len;
    if (n > transfer->remaining || fwrite(event->data, 1, n, transfer->file) != n ||
        mbedtls_sha256_update(&transfer->hash, event->data, n)) {
        transfer->error = true;
        return ESP_FAIL;
    }
    transfer->remaining -= n;
    return ESP_OK;
}

static bool download_entry(esp_http_client_handle_t client, download_t *transfer, const asset_entry_t *entry)
{
    char url[320], hex[65];
    uint8_t raw[32];
    int n = snprintf(url, sizeof(url),
        "https://raw.githubusercontent.com/ZoleeBB/ZoleeLoxoneDisplay/" ZLD_SOURCE_REV "/sdcard/%s", entry->name);
    if (n < 0 || (size_t)n >= sizeof(url) || esp_http_client_set_url(client, url) != ESP_OK) return false;
    transfer->file = fopen(PART, "wb");
    if (!transfer->file) return false;
    transfer->remaining = entry->size;
    transfer->error = false;
    bool ok = !mbedtls_sha256_starts(&transfer->hash, 0) && esp_http_client_perform(client) == ESP_OK &&
        esp_http_client_get_status_code(client) == 200 && !transfer->error && !transfer->remaining &&
        !mbedtls_sha256_finish(&transfer->hash, raw);
    if (fclose(transfer->file)) ok = false;
    transfer->file = NULL;
    if (ok) {
        hex_digest(raw, hex);
        ok = !strcmp(hex, entry->sha) && install_temp(entry);
    }
    if (!ok) unlink(PART);
    return ok;
}

static void download_card(void *unused)
{
    (void)unused;
    download_t transfer = {0};
    mbedtls_sha256_init(&transfer.hash);
    /* Reuse one TLS connection while fetching missing files at the same commit. */
    const esp_http_client_config_t cfg = {
        .url = "https://raw.githubusercontent.com/",
        .crt_bundle_attach = esp_crt_bundle_attach,
        .disable_auto_redirect = true,
        .timeout_ms = 12000,
        .buffer_size = 4096,
        .event_handler = remote_event,
        .user_data = &transfer,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    bool ok = client && mkdir_if_missing(ROOT) && mkdir_if_missing(BLOBS);
    for (size_t i = 0; ok && i < ACTIVE_COUNT; ++i) {
        atomic_store(&cloud_position, i + 1);
        if (!blob_ok(&active_entries[i])) {
            atomic_store(&ready, false);
            ok = download_entry(client, &transfer, &active_entries[i]);
            if (!ok) {
                vTaskDelay(pdMS_TO_TICKS(1000));
                ok = download_entry(client, &transfer, &active_entries[i]);
            }
        }
    }
    if (client) esp_http_client_cleanup(client);
    mbedtls_sha256_free(&transfer.hash);
    if (ok) ok = zld_assets_check();
    atomic_store(&cloud_state, ok ? CLOUD_OK : CLOUD_ERROR);
    atomic_fetch_sub(&update_users, 1);
    vTaskDelete(NULL);
}

static esp_err_t fetch_card(httpd_req_t *req)
{
    if (!permitted(req, true)) return ESP_OK;
    if (sizeof(ZLD_SOURCE_REV) != 41)
        return fail(req, "409 Conflict", "Ehhez a helyi buildhez válaszd ki a .zld fájlt.");
    staged[0] = 0;
    atomic_store(&cloud_position, 0);
    if (!hold_update()) return fail(req, "503 Service Unavailable", "A kártya nem érhető el.");
    atomic_store(&cloud_state, CLOUD_RUNNING);
    if (xTaskCreate(download_card, "sd_download", 8192, NULL, 4, NULL) != pdPASS) {
        atomic_store(&cloud_state, CLOUD_ERROR);
        atomic_fetch_sub(&update_users, 1);
        return fail(req, "503 Service Unavailable", "Nincs elég memória a letöltés elindításához.");
    }
    httpd_resp_set_status(req, "202 Accepted");
    return httpd_resp_sendstr(req, "OK");
}

static esp_err_t asset(httpd_req_t *req)
{
    if (!staged_request(req)) return ESP_OK;
    char number[12];
    if (httpd_req_get_hdr_value_str(req, "X-Pack-Index", number, sizeof(number)) != ESP_OK ||
        !*number || strspn(number, "0123456789") != strlen(number))
        return fail(req, "400 Bad Request", "Hibás fájlsorszám.");
    unsigned long target = strtoul(number, NULL, 10);
    if (target >= MAX_ASSETS) return fail(req, "400 Bad Request", "Ismeretlen fájl.");
    FILE *idx = fopen(STAGE, "rb");
    if (!idx) return fail(req, "409 Conflict", "A telepítést újra kell kezdeni.");
    char line[256];
    bool found = false;
    for (unsigned i = 0; i <= target && fgets(line, sizeof(line), idx); ++i) found = i == target;
    fclose(idx);
    asset_entry_t entry;
    if (!found || !parse_line(line, &entry) || req->content_len != entry.size)
        return fail(req, "400 Bad Request", "Eltérő képméret vagy ismeretlen fájl.");
    char digest[65];
    if (!receive_file(req, PART, digest))
        return fail(req, "503 Service Unavailable", "A fájlt nem sikerült a kártyára írni.");
    if (strcmp(digest, entry.sha)) {
        unlink(PART);
        return fail(req, "400 Bad Request", "Hibás fájlellenőrző összeg.");
    }
    if (!install_temp(&entry))
        return fail(req, "503 Service Unavailable", "A fájl mentése sikertelen.");
    return httpd_resp_sendstr(req, "OK");
}

static bool verify_entry(const asset_entry_t *entry, unsigned i, void *ctx)
{
    (void)i; (void)ctx;
    return blob_ok(entry);
}

static esp_err_t seed(httpd_req_t *req)
{
    if (!staged_request(req)) return ESP_OK;
    if (strcmp(staged, ZLD_GENERATION))
        return fail(req, "409 Conflict", "Ehhez a csomaghoz firmware-frissítés is szükséges.");
    if (!zld_assets_check()) return fail(req, "409 Conflict", "A képcsomag hiányos.");
    return httpd_resp_sendstr(req, "OK");
}

/* Scan in small pieces. Markers that straddle a read boundary are also matched. */
static bool image_matches(const esp_partition_t *part, size_t size)
{
    char marker[sizeof(running_marker)];
    snprintf(marker, sizeof(marker), MARKER_PREFIX "%s/END", staged);
    uint8_t buf[CHUNK + sizeof(marker)];
    size_t offset = 0, previous = 0;
    while (offset < size) {
        size_t n = size - offset < CHUNK ? size - offset : CHUNK;
        if (esp_partition_read(part, offset, buf + previous, n) != ESP_OK) break;
        size_t length = previous + n;
        for (size_t i = 0; i + sizeof(marker) <= length; ++i) {
            if (buf[i] == 'Z' && !memcmp(buf + i, marker, sizeof(marker))) return true;
        }
        previous = length < sizeof(marker) - 1 ? length : sizeof(marker) - 1;
        memmove(buf, buf + length - previous, previous);
        offset += n;
    }
    return false;
}

static void restart(void *unused)
{
    (void)unused;
    esp_restart();
}

static esp_err_t firmware(httpd_req_t *req)
{
    if (!staged_request(req)) return ESP_OK;
    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    char expected[65], actual[65];
    if (!part || !req->content_len || req->content_len > part->size ||
        httpd_req_get_hdr_value_str(req, "X-Firmware-Sha256", expected, sizeof(expected)) != ESP_OK ||
        !valid_sha(expected)) return fail(req, "400 Bad Request", "Hibás firmware méret vagy azonosító.");
    esp_ota_handle_t ota;
    if (esp_ota_begin(part, req->content_len, &ota) != ESP_OK)
        return fail(req, "409 Conflict", "Az OTA partíció még nem írható.");
    if (!receive(req, NULL, ota, actual) || strcmp(actual, expected)) {
        esp_ota_abort(ota);
        return fail(req, "400 Bad Request", "Hiányos vagy sérült firmware.");
    }
    esp_app_desc_t desc;
    if (esp_ota_end(ota) != ESP_OK || esp_ota_get_partition_description(part, &desc) != ESP_OK ||
        memcmp(desc.project_name, esp_app_get_description()->project_name, sizeof(desc.project_name)) ||
        !image_matches(part, req->content_len))
        return fail(req, "400 Bad Request", "A firmware és a képcsomag nem tartozik ehhez a kijelzőhöz.");
    if (!walk_stage(verify_entry, NULL, staged) || !zld_card_verify())
        return fail(req, "409 Conflict", "A szükséges SD-fájlok ellenőrzése sikertelen.");
    if (esp_ota_set_boot_partition(part) != ESP_OK)
        return fail(req, "500 Internal Server Error", "Az új firmware nem aktiválható.");
    reboot_pending = true;
    esp_err_t sent = httpd_resp_sendstr(req, "OK");
    if (esp_timer_start_once(restart_timer, 1000000) != ESP_OK) esp_restart();
    return sent;
}

static esp_err_t guarded_write(httpd_req_t *req)
{
    const httpd_uri_t *route = req->user_ctx;
    if (!hold_update()) return fail(req, "503 Service Unavailable", "A kijelző újraindul.");
    esp_err_t rc = route->handler(req);
    atomic_fetch_sub(&update_users, 1);
    return rc;
}

esp_err_t zld_update_register(httpd_handle_t server, zld_update_authorize_t authorize)
{
    if (!server || !authorize) return ESP_ERR_INVALID_ARG;
    allow = authorize;
    const esp_timer_create_args_t timer = {.callback = restart, .name = "ota_restart"};
    esp_err_t err = esp_timer_create(&timer, &restart_timer);
    if (err != ESP_OK) return err;
    static const httpd_uri_t routes[] = {
        {.uri="/update", .method=HTTP_GET, .handler=page},
        {.uri="/update/info", .method=HTTP_GET, .handler=info},
        {.uri="/update/download", .method=HTTP_POST, .handler=fetch_card},
        {.uri="/update/prepare", .method=HTTP_POST, .handler=prepare},
        {.uri="/update/asset", .method=HTTP_POST, .handler=asset},
        {.uri="/update/seed", .method=HTTP_POST, .handler=seed},
        {.uri="/update/firmware", .method=HTTP_POST, .handler=firmware},
    };
    for (size_t i = 0; i < sizeof(routes)/sizeof(routes[0]); ++i) {
        httpd_uri_t route = routes[i];
        if (route.method == HTTP_POST) {
            route.handler = guarded_write;
            route.user_ctx = (void *)&routes[i];
        }
        err = httpd_register_uri_handler(server, &route);
        if (err != ESP_OK) {
            while (i > 0) {
                --i;
                httpd_unregister_uri_handler(server, routes[i].uri, routes[i].method);
            }
            esp_timer_delete(restart_timer);
            return err;
        }
    }
    return ESP_OK;
}
