/* The web editor owns persistence. This converts its tab document to the
 * bounded models consumed by the original Loxone transport and block services. */
#include "panel_profile.h"
#include <ctype.h>
#include <math.h>
#include <string.h>

static const char *text(const cJSON *o, const char *name)
{
    const char *s = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(o, name));
    return s ? s : "";
}

static bool copy_text(char *dst, size_t capacity, const char *s)
{
    if (!s) s = "";
    size_t n = strlen(s);
    if (n >= capacity) return false;
    memcpy(dst, s, n + 1);
    return true;
}

static bool uuid(char out[LX_UUID_LEN], const char *s)
{
    if (!*s) { out[0] = 0; return true; }
    if (!lx_uuid_valid(s)) return false;
    for (size_t i = 0; i <= strlen(s); ++i) out[i] = tolower((unsigned char)s[i]);
    return true;
}

static double number(const cJSON *o, const char *name, double fallback)
{
    const cJSON *n = cJSON_GetObjectItemCaseSensitive(o, name);
    return cJSON_IsNumber(n) && isfinite(n->valuedouble) ? n->valuedouble : fallback;
}

static bool read_card(lx_home_card_t *card, const cJSON *block)
{
    static const char *const names[] = {
        "value", "pulse", "switch", "lighting", "irc", "shading", "dimmer",
        "central-lighting", "slider", "timed-switch", "window-monitor", "gate", "window", "audio",
    };
    unsigned type = 0;
    while (type < sizeof(names)/sizeof(names[0]) && strcmp(names[type], text(block, "type"))) ++type;
    if (type == sizeof(names)/sizeof(names[0])) return false;
    card->type = (lx_home_card_type_t)type;
    const char *source = text(block, "stateUuid");
    if (!*source && type == LX_HOME_CARD_VALUE) source = text(block, "uuid");
    card->source.scale = number(block, "scale", 1);
    card->source.offset = number(block, "offset", 0);
    card->decimals = (unsigned)fmax(0, fmin(3, number(block, "decimals", 1)));
    card->allow_permanent = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(block, "allowPermanent"));
    return copy_text(card->id, sizeof(card->id), text(block, "id")) && card->id[0] &&
        strcmp(card->id, LX_INTERCOM_GATE_ID) && copy_text(card->name, sizeof(card->name), text(block, "name")) &&
        copy_text(card->unit, sizeof(card->unit), text(block, "unit")) &&
        uuid(card->control_uuid, text(block, "uuid")) && uuid(card->source.uuid, source);
}

bool panel_profile_read(panel_profile_t *out, const cJSON *panel, const char *password, const char *camera_password)
{
    if (!out || !cJSON_IsObject(panel)) return false;
    memset(out, 0, sizeof(*out));
    lx_config_t *conn = &out->connection;
    lx_config_defaults(conn);
    const cJSON *loxone = cJSON_GetObjectItemCaseSensitive(panel, "loxone");
    conn->tls = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(loxone, "tls"));
    double port = number(loxone, "port", conn->tls ? 443 : 80);
    if (port < 1 || port > 65535 || floor(port) != port) return false;
    conn->port = (uint16_t)port;
    if (!copy_text(conn->host, sizeof(conn->host), text(loxone, "host")) ||
        !copy_text(conn->username, sizeof(conn->username), text(loxone, "user")) ||
        !copy_text(conn->ca_pem, sizeof(conn->ca_pem), text(loxone, "ca_pem")) ||
        !copy_text(conn->password, sizeof(conn->password), password)) return false;
    for (const char *p = conn->host; *p; ++p)
        if (!isalnum((unsigned char)*p) && *p != '.' && *p != '-') return false;
    if (conn->tls && conn->host[0] && !strstr(conn->ca_pem, "-----BEGIN CERTIFICATE-----")) return false;
    const cJSON *display = cJSON_GetObjectItemCaseSensitive(panel, "display");
    if (!uuid(conn->map[6].uuid, text(display, "nightUuid"))) return false;
    const cJSON *tabs = cJSON_GetObjectItemCaseSensitive(panel, "tabs");
    const cJSON *tab;
    cJSON_ArrayForEach(tab, tabs) {
        if (out->home.page_count == LX_HOME_MAX_PAGES) return false;
        unsigned index = out->home.page_count++;
        lx_home_page_t *page = &out->home.pages[index];
        page->first_card = out->home.card_count;
        panel_energy_mapping_t *energy = &out->energy[index];
        energy->enabled = !strcmp(text(tab, "kind"), "energy");
        if (energy->enabled) {
            const cJSON *map = cJSON_GetObjectItemCaseSensitive(tab, "energy");
            for (int i = 0; i < LX_FIELD_COUNT; ++i) {
                const cJSON *field = cJSON_GetObjectItemCaseSensitive(map, lx_fields[i].name);
                const char *s = cJSON_IsString(field) ? field->valuestring : text(field, "uuid");
                if (!uuid(energy->fields[i].uuid, s)) return false;
                energy->fields[i].scale = number(field, "scale", 1);
                energy->fields[i].offset = number(field, "offset", 0);
            }
        }
        const cJSON *block;
        cJSON_ArrayForEach(block, cJSON_GetObjectItemCaseSensitive(tab, "blocks")) {
            if (page->card_count == LX_HOME_MAX_CARDS) return false;
            lx_home_card_t *card = &out->home.cards[out->home.card_count];
            if (!read_card(card, block)) return false;
            for (unsigned n = 0; n < out->home.card_count; ++n)
                if (!strcmp(out->home.cards[n].id, card->id)) return false;
            ++page->card_count;
            ++out->home.card_count;
        }
    }
    const cJSON *door = cJSON_GetObjectItemCaseSensitive(panel, "intercom");
    lx_intercom_config_t *ic = &out->home.intercom;
    ic->enabled = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(door, "enabled"));
    ic->speaker_volume = (uint8_t)fmin(100,fmax(0,number(door, "speakerVolume", 55)));
    ic->microphone_gain = (uint8_t)fmin(100,fmax(0,number(door, "microphoneGain", 80)));
    ic->ringtone_repeat = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(door, "ringtoneRepeat"));
    ic->gate.type = LX_HOME_CARD_PULSE;
    strcpy(ic->gate.id, LX_INTERCOM_GATE_ID);
    return uuid(ic->bell_uuid, text(door, "uuid")) && uuid(ic->gate.control_uuid, text(door, "gateUuid")) &&
        copy_text(ic->stream_url, sizeof(ic->stream_url), text(door, "cameraUrl")) &&
        copy_text(ic->video_username, sizeof(ic->video_username), text(door, "cameraUser")) &&
        copy_text(ic->video_password, sizeof(ic->video_password), camera_password) &&
        copy_text(ic->ringtone, sizeof(ic->ringtone), text(door, "ringtone"));
}

cJSON *lx_home_document_parse(const char *body, size_t size)
{
    if (!body || size < 2 || size > LX_HOME_JSON_MAX || memchr(body, 0, size))
        return NULL;
    unsigned depth = 0;
    bool quoted = false, escaped = false;
    for (size_t i = 0; i < size; i++)
    {
        char ch = body[i];
        if (quoted)
        {
            if (escaped)
                escaped = false;
            else if (ch == '\\')
                escaped = true;
            else if (ch == '"')
                quoted = false;
        }
        else if (ch == '"')
            quoted = true;
        else if (ch == '{' || ch == '[')
        {
            if (++depth > 8)
                return NULL;
        }
        else if (ch == '}' || ch == ']')
        {
            if (!depth)
                return NULL;
            depth--;
        }
    }
    if (depth || quoted)
        return NULL;
    return cJSON_ParseWithLengthOpts(body, size + 1, NULL, true);
}
