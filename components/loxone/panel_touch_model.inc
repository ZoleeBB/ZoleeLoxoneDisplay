/* Touch targets are control identities in the authenticated structure, independent
 * of the visible cards. Only documented commands may leave this file. */
bool lx_touch_attach(cJSON *entry, const cJSON *control, bool secured)
{
    const char *type = light_string(control, "type");
    bool button = !strcmp(type, "Pushbutton"), sw = !strcmp(type, "Switch");
    bool light = !strcmp(type, "LightControllerV2");
    bool shade = !strcmp(type, "Jalousie") || !strcmp(type, "CentralJalousie");
    bool audio = !strcmp(type, "AudioZoneV2");
    if (!(button || sw || light || shade || audio)) return true;
    if (light_blocked(control, secured) || !lx_uuid_valid(control_action_uuid(control))) return true;
    if ((light && !cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(entry, "lighting_blocked"))) ||
        (shade && !cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(entry, "shading_blocked"))) ||
        (audio && !cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(entry, "controls_blocked")))) return true;

    const cJSON *states = cJSON_GetObjectItemCaseSensitive(control, "states");
    if (states && !cJSON_IsObject(states)) return true;
    static const char *const keys[] = {"jLocked", "locked", "isLocked", "deviceState", "safetyActive",
        "lockedOn", "active", "moodList", "serverState", "clientState", "playState"};
    cJSON *meta = cJSON_CreateObject();
    if (!meta) return false;
    for (unsigned i=0; i < sizeof(keys)/sizeof(keys[0]); ++i) {
        const cJSON *id = cJSON_GetObjectItemCaseSensitive(states, keys[i]);
        if (!id) continue;
        if (!cJSON_IsString(id) || !lx_uuid_valid(id->valuestring)) { cJSON_Delete(meta); return true; }
        if (!cJSON_AddStringToObject(meta, keys[i], id->valuestring)) { cJSON_Delete(meta); return false; }
    }
    if (!cJSON_AddItemToObject(entry, "touch_states", meta)) { cJSON_Delete(meta); return false; }
    const cJSON *detail = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(control,"details"),"type");
    if (sw) return cJSON_AddTrueToObject(entry, "touch_switch") != NULL;
    if (button) {
        /* Scene (511) has the same protocol type, but is not a wired VI (71). */
        return cJSON_AddTrueToObject(entry, "touch_button") &&
               cJSON_AddBoolToObject(entry, "touch_input", cJSON_IsNumber(detail) && detail->valuedouble == 71);
    }
    return cJSON_AddTrueToObject(entry, "touch_target") != NULL;
}

static const cJSON *touch_row(const cJSON *catalog, const char *uuid)
{
    if (!lx_uuid_valid(uuid)) return NULL;
    const cJSON *row;
    cJSON_ArrayForEach(row, catalog) {
        if (!strcmp(light_string(row, "control_uuid"), uuid) &&
            cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(row, "touch_states"))) return row;
    }
    return NULL;
}

static bool touch_number(const cJSON *states, const char *key, lx_touch_lookup_t get, double *number)
{
    lx_touch_value_t v;
    const char *id = light_string(states, key);
    if (!*id || !get(id, &v) || !v.numeric || !isfinite(v.number)) return false;
    *number = v.number;
    return true;
}

static bool touch_unlocked(const cJSON *row, lx_touch_lookup_t get)
{
    if (!row || !cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(row,"secured"))) return false;
    const cJSON *states = cJSON_GetObjectItemCaseSensitive(row, "touch_states");
    const char *lock = light_string(states, "jLocked");
    if (*lock) {
        bool known = false;
        lx_touch_value_t v;
        if (!get(lock, &v) || v.numeric || light_lock(v.text, v.length, &known) || !known) return false;
    }
    static const char *const binary[] = {"locked", "isLocked", "safetyActive"};
    for (unsigned i = 0; i < sizeof(binary)/sizeof(binary[0]); ++i) {
        double number;
        if (*light_string(states, binary[i]) &&
            (!touch_number(states, binary[i], get, &number) || number != 0)) return false;
    }
    double value;
    if (*light_string(states, "deviceState") &&
        (!touch_number(states,"deviceState",get,&value) || (value != 0 && value != 2))) return false;
    static const char *const online[] = {"serverState", "clientState"};
    for (unsigned i = 0; i < sizeof(online)/sizeof(online[0]); ++i)
        if (*light_string(states, online[i]) &&
            (!touch_number(states, online[i], get, &value) || value != 2)) return false;
    return true;
}

cJSON *lx_touch_mood_list(const cJSON *catalog, const char *uuid, lx_touch_lookup_t get, const char **status)
{
    const cJSON *row = touch_row(catalog, uuid);
    const cJSON *states = cJSON_GetObjectItemCaseSensitive(row, "touch_states");
    const char *id = light_string(states, "moodList");
    *status = "unsupported";
    if (!*id || strcmp(light_string(row,"type"),"LightControllerV2")) return NULL;
    lx_touch_value_t v;
    if (!get(id, &v)) { *status = "loading"; return NULL; }
    if (v.numeric) return NULL;
    cJSON *data = light_json(v.text, v.length);
    if (!cJSON_IsArray(data) || cJSON_GetArraySize(data) > LX_LIGHT_MAX_MOODS) { cJSON_Delete(data); return NULL; }
    cJSON *list = cJSON_CreateArray();
    const cJSON *item;
    cJSON_ArrayForEach(item, data) {
        const cJSON *mood = cJSON_GetObjectItemCaseSensitive(item,"id");
        const cJSON *name = cJSON_GetObjectItemCaseSensitive(item,"name");
        cJSON *choice = cJSON_CreateObject();
        if (!choice || !cJSON_IsNumber(mood) || mood->valuedouble < 0 || mood->valuedouble > INT_MAX ||
            mood->valuedouble != floor(mood->valuedouble) || !cJSON_IsString(name) ||
            !cJSON_AddNumberToObject(choice,"id",mood->valuedouble) ||
            !cJSON_AddStringToObject(choice,"name",name->valuestring) || !cJSON_AddItemToArray(list, choice)) {
            cJSON_Delete(choice); cJSON_Delete(list); list = NULL; break;
        }
    }
    cJSON_Delete(data);
    if (list) *status = "ready";
    return list;
}

static const char *touch_action_target(const cJSON *row)
{
    return cJSON_GetObjectItemCaseSensitive(row,"action_uuid") ?
        light_string(row,"action_uuid") : light_string(row,"control_uuid");
}

bool lx_touch_command(const cJSON *catalog, const lx_touch_mapping_t *target, lx_touch_lookup_t get, char out[96])
{
    const cJSON *row = touch_row(catalog, target->uuid);
    if (!touch_unlocked(row, get)) return false;
    const cJSON *states = cJSON_GetObjectItemCaseSensitive(row,"touch_states");
    const char *action = target->action, *address = touch_action_target(row);
    const char *type = light_string(row, "type"), *verb = NULL;
    if (!strcmp(target->type,"lighting") && !strcmp(type,"LightControllerV2")) {
        if (!strcmp(action,"next")) verb = "plus";
        else if (!strcmp(action,"previous")) verb = "minus";
        else if (!strcmp(action,"scene")) {
            const char *status;
            cJSON *list = lx_touch_mood_list(catalog, target->uuid, get, &status);
            bool found = false;
            const cJSON *item;
            cJSON_ArrayForEach(item, list)
                if (cJSON_GetObjectItemCaseSensitive(item,"id")->valuedouble == target->scene_id) found = true;
            cJSON_Delete(list);
            return found && !target->input_uuid[0] && lx_uuid_valid(address) &&
                snprintf(out,96,"jdev/sps/io/%s/changeTo/%u",address,target->scene_id) < 96;
        }
    } else if (!strcmp(target->type,"pulse") && !strcmp(action,"trigger") &&
               cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(row,"touch_button"))) verb = "pulse";
    else if (!strcmp(target->type,"switch") && !strcmp(action,"trigger") &&
             cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(row,"touch_switch"))) {
        double locked, active;
        if (*light_string(states, "lockedOn") &&
            (!touch_number(states,"lockedOn",get,&locked) || locked != 0)) return false;
        if (target->input_uuid[0]) verb = "pulse";
        else if (touch_number(states, "active", get, &active) && (active == 1 || active == 0))
            verb = active == 1 ? "off" : "on";
    } else if (!strcmp(target->type,"shading") && (!strcmp(type,"Jalousie") || !strcmp(type,"CentralJalousie"))) {
        if (!strcmp(action,"up")) verb = "FullUp";
        if (!strcmp(action,"down")) verb = "FullDown";
    } else if (!strcmp(target->type,"audio") && !strcmp(type,"AudioZoneV2")) {
        lx_audio_action_t op;
        if (!strcmp(action,"volumeUp")) op = LX_AUDIO_VOLUME_UP;
        else if (!strcmp(action,"volumeDown")) op = LX_AUDIO_VOLUME_DOWN;
        else if (!strcmp(action,"next")) op = LX_AUDIO_NEXT;
        else if (!strcmp(action,"previous")) op = LX_AUDIO_PREVIOUS;
        else if (!strcmp(action,"playPause")) {
            double play;
            if (!touch_number(states,"playState",get,&play) || (play != 0 && play != 1 && play != 2)) return false;
            op = play == 2 ? LX_AUDIO_PAUSE : LX_AUDIO_PLAY;
        } else return false;
        return !target->input_uuid[0] && lx_audio_command_build(row, op, 0, out);
    }
    if (!verb) return false;
    if (target->input_uuid[0]) {
        if (strcmp(target->type,"switch") && (strcmp(target->type,"lighting") || strcmp(action,"next"))) return false;
        const cJSON *input = touch_row(catalog,target->input_uuid);
        if (!cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(input,"touch_input")) || !touch_unlocked(input,get))
            return false;
        address = touch_action_target(input); verb = "pulse";
    }
    return lx_uuid_valid(address) && snprintf(out,96,"jdev/sps/io/%s/%s",address,verb) < 96;
}

/* JSON replies use dev/ for jdev/. Some Miniserver IO handlers return only
 * the control's address (as in Loxone's webservices Pushbutton example).
 * A timed-out previous request forces a reconnect, including same-target IO. */
bool lx_touch_ack_matches(const char *command, const char *control)
{
    if (!command || !control || strncmp(command, "jdev/sps/io/", 12)) return false;
    if (*control == '/') ++control;
    if (!strncmp(control, "jdev/", 5)) ++control;
    const char *address = command + 1;
    if (!strcmp(address, control)) return true;
    const char *verb = strchr(address + strlen("dev/sps/io/"), '/');
    return verb && (size_t)(verb - address) == strlen(control) && !strncmp(address, control, strlen(control));
}
