/* Ported from the source project's per-device authenticated SD credential store. */
#include "panel_secret.h"
#include "esp_random.h"
#include "nvs.h"
#include "mbedtls/gcm.h"
#include "mbedtls/platform_util.h"
#include <string.h>

static void hex(const unsigned char *data, size_t n, char *out)
{
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++)
    {
        out[2 * i] = digits[data[i] >> 4];
        out[2 * i + 1] = digits[data[i] & 15];
    }
    out[2 * n] = 0;
}
static bool unhex(const char *s, unsigned char *out, size_t n)
{
    if (!s || strlen(s) != n * 2)
        return false;
    for (size_t i = 0; i < n; i++)
    {
        unsigned a, b;
        char x = s[i * 2], y = s[i * 2 + 1];
        if (x >= '0' && x <= '9')
            a = x - '0';
        else if (x >= 'a' && x <= 'f')
            a = x - 'a' + 10;
        else
            return false;
        if (y >= '0' && y <= '9')
            b = y - '0';
        else if (y >= 'a' && y <= 'f')
            b = y - 'a' + 10;
        else
            return false;
        out[i] = (a << 4) | b;
    }
    return true;
}
static esp_err_t device_key(unsigned char key[32], bool create)
{
    nvs_handle_t h;
    esp_err_t e = nvs_open("panel_secret", create ? NVS_READWRITE : NVS_READONLY, &h);
    if (e != ESP_OK)
        return e;
    size_t n = 32;
    e = nvs_get_blob(h, "key", key, &n);
    if (e == ESP_ERR_NVS_NOT_FOUND && create)
    {
        esp_fill_random(key, 32);
        e = nvs_set_blob(h, "key", key, 32);
        if (e == ESP_OK)
            e = nvs_commit(h);
    }
    else if (e == ESP_OK && n != 32)
        e = ESP_ERR_INVALID_SIZE;
    nvs_close(h);
    return e;
}
static const char *text(const cJSON *o, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsString(v) ? v->valuestring : NULL;
}
bool panel_secret_crypt(char password[PANEL_SECRET_CAP], const char *associated,
                             cJSON *root, bool encrypt)
{
    unsigned char key[32] = {0}, nonce[12] = {0}, tag[16] = {0}, cipher[PANEL_SECRET_CAP] = {0};
    char encoded[PANEL_SECRET_CAP * 2 + 1], nonce_hex[25], tag_hex[33];
    bool ok = false;
    mbedtls_gcm_context ctx;
    mbedtls_gcm_init(&ctx);
    if (device_key(key, encrypt) != ESP_OK)
        goto done;
    if (mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, key, 256) != 0)
        goto done;
    size_t a = strlen(associated);
    if (a > 512)
        goto done;
    if (encrypt)
    {
        size_t n = strlen(password);
        if (!n || n >= PANEL_SECRET_CAP)
            goto done;
        esp_fill_random(nonce, sizeof(nonce));
        if (mbedtls_gcm_crypt_and_tag(&ctx,
                                      MBEDTLS_GCM_ENCRYPT,
                                      n,
                                      nonce,
                                      sizeof(nonce),
                                      (unsigned char *)associated,
                                      a,
                                      (unsigned char *)password,
                                      cipher,
                                      sizeof(tag),
                                      tag) != 0)
            goto done;
        hex(cipher, n, encoded);
        hex(nonce, sizeof(nonce), nonce_hex);
        hex(tag, sizeof(tag), tag_hex);
        cJSON *secret = cJSON_CreateObject();
        if (!secret)
            goto done;
        if (!cJSON_AddStringToObject(secret, "cipher", encoded) ||
            !cJSON_AddStringToObject(secret, "nonce", nonce_hex) ||
            !cJSON_AddStringToObject(secret, "tag", tag_hex) ||
            !cJSON_AddItemToObject(root, "secret", secret))
        {
            cJSON_Delete(secret);
            goto done;
        }
        ok = true;
    }
    else
    {
        const cJSON *secret = cJSON_GetObjectItemCaseSensitive(root, "secret");
        const char *c = text(secret, "cipher");
        if (!c || !strlen(c) || strlen(c) % 2 || strlen(c) >= PANEL_SECRET_CAP * 2)
            goto done;
        size_t n = strlen(c) / 2;
        if (!unhex(c, cipher, n) || !unhex(text(secret, "nonce"), nonce, sizeof(nonce)) ||
            !unhex(text(secret, "tag"), tag, sizeof(tag)))
            goto done;
        if (mbedtls_gcm_auth_decrypt(&ctx,
                                     n,
                                     nonce,
                                     sizeof(nonce),
                                     (unsigned char *)associated,
                                     a,
                                     tag,
                                     sizeof(tag),
                                     cipher,
                                     (unsigned char *)password) != 0)
            goto done;
        password[n] = 0;
        ok = true;
    }
done:
    if (!ok)
        mbedtls_platform_zeroize(password, PANEL_SECRET_CAP);
    mbedtls_gcm_free(&ctx);
    mbedtls_platform_zeroize(key, sizeof(key));
    mbedtls_platform_zeroize(cipher, sizeof(cipher));
    mbedtls_platform_zeroize(encoded, sizeof(encoded));
    return ok;
}
