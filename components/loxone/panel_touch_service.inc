/* The authenticated worker alone owns the encrypted WebSocket salt and replies.
 * Each gesture occupies one bounded FIFO slot, even a three-pulse train. */
typedef struct {
    unsigned generation, session, epoch, zone, taps;
    int64_t queued_us;
} touch_gesture_t;

#define TOUCH_FIFO_SIZE 8
#define TOUCH_PULSE_SPACING_US 140000LL
#define TOUCH_MAX_SPACING_US 250000LL
static touch_gesture_t s_touch_fifo[TOUCH_FIFO_SIZE];
static unsigned s_touch_front, s_touch_count, s_touch_epoch;

static void touch_cancel_locked(void)
{
    ++s_touch_epoch;
    s_touch_front = s_touch_count = 0;
}

void zld_touch_cancel(void)
{
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    touch_cancel_locked();
    xSemaphoreGive(s_lock);
}

static bool touch_live_locked(void)
{
    return s_profile && s_catalog && s_home_live && !s_dead && !s_home_busy &&
        atomic_load(&s_refresh) == CATALOG_IDLE && s_network_ready() &&
        esp_timer_get_time() - s_home_response < LX_KEEPALIVE_US * 3;
}

static unsigned touch_session_locked(unsigned generation)
{
    return touch_live_locked() && s_profile->generation == generation && s_config_generation() == generation &&
        s_profile->touch.enabled && !s_intercom_ringing && !intercom_held(s_intercom_generation) ?
        s_home_session : 0;
}

uint32_t zld_touch_session(unsigned generation)
{
    if (!s_lock) return 0;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    unsigned session = touch_session_locked(generation);
    xSemaphoreGive(s_lock);
    return session;
}

static bool touch_value_locked(const char *uuid, lx_touch_value_t *out)
{
    const light_cache_entry_t *entry = lighting_cache_find(uuid);
    if (!entry || !entry->seen) return false;
    *out = (lx_touch_value_t){entry->numeric,entry->number,entry->text,entry->length};
    return true;
}

esp_err_t zld_touch_press(unsigned generation, uint32_t session, unsigned zone, unsigned taps)
{
    if (zone > LX_TOUCH_CENTER || taps < 1 || taps > (zone == LX_TOUCH_CENTER ? 3U : 1U))
        return ESP_ERR_INVALID_ARG;
    if (!s_lock || !session) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t result = ESP_ERR_INVALID_STATE;
    char validated[96];
    if (touch_session_locked(generation) == session &&
        lx_touch_command(s_catalog, &s_profile->touch.zones[zone], touch_value_locked, validated)) {
        if (s_touch_count < TOUCH_FIFO_SIZE) {
            unsigned i = (s_touch_front + s_touch_count++) % TOUCH_FIFO_SIZE;
            s_touch_fifo[i] = (touch_gesture_t){generation,session,s_touch_epoch,zone,taps,esp_timer_get_time()};
            result = ESP_OK;
        } else result = ESP_ERR_TIMEOUT;
    }
    xSemaphoreGive(s_lock);
    return result;
}

cJSON *zld_touch_moods(const char *uuid)
{
    const char *status = "offline";
    cJSON *moods = NULL;
    if (s_lock) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (touch_live_locked()) moods = lx_touch_mood_list(s_catalog, uuid, touch_value_locked, &status);
        xSemaphoreGive(s_lock);
    }
    if (!moods) moods = cJSON_CreateArray();
    cJSON *result = cJSON_CreateObject();
    if (!result || !moods || !cJSON_AddStringToObject(result,"status",status) ||
        !cJSON_AddItemToObject(result,"moods",moods)) {
        cJSON_Delete(moods); cJSON_Delete(result); return NULL;
    }
    return result;
}

static bool touch_build_current(const touch_gesture_t *g, char command[96])
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool valid = g->epoch == s_touch_epoch && touch_session_locked(g->generation) == g->session &&
        lx_touch_command(s_catalog, &s_profile->touch.zones[g->zone], touch_value_locked, command);
    xSemaphoreGive(s_lock);
    return valid;
}

/* The websocket callback can receive contact/lock changes while an earlier
 * pulse is awaiting ACK or while the worker spaces two taps. Apply the already
 * queued state before validating the next command. Avoid indefinite draining. */
static bool touch_drain_status(void)
{
    message_t m;
    for (unsigned i = 0; i < LX_QUEUE_DEPTH; ++i) {
        if (xQueueReceive(s_messages, &m, 0) != pdTRUE) return !s_dead;
        mark_response();
        bool ok = update_values(m.type, m.data, m.length);
        free_message(&m);
        if (!ok) { s_dead = true; return false; }
    }
    return !s_dead && !uxQueueMessagesWaiting(s_messages);
}

static void touch_poll(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (!s_touch_count) { xSemaphoreGive(s_lock); return; }
    touch_gesture_t g = s_touch_fifo[s_touch_front];
    s_touch_front = (s_touch_front + 1) % TOUCH_FIFO_SIZE;
    --s_touch_count;
    xSemaphoreGive(s_lock);
    if (esp_timer_get_time() - g.queued_us > 1500000LL) return;

    int64_t previous = 0;
    for (unsigned i = 0; i < g.taps; ++i) {
        if (i) {
            int64_t delay = previous + TOUCH_PULSE_SPACING_US - esp_timer_get_time();
            if (delay > 0) vTaskDelay(pdMS_TO_TICKS((delay + 999) / 1000) + 1);
        }
        char command[96];
        if (!touch_drain_status() || !touch_build_current(&g, command)) break;
        int64_t now = esp_timer_get_time();
        /* A delayed ACK must never turn a double-click into two unrelated taps. */
        if (i && now - previous > TOUCH_MAX_SPACING_US) break;
        previous = now;
        cJSON *ack = receive_reply(command, true, true);
        if (!ack) {
            /* A late/ambiguous ACK must not satisfy another command's reply.
             * Never retry a pulse whose delivery cannot be established. */
            s_dead = true;
            break;
        }
        cJSON_Delete(ack);
    }
}
