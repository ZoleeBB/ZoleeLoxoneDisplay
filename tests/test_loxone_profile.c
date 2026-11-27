/* Host regression for the panel-to-Miniserver bridge. Each tab subscribes to
 * its own mapping, irrespective of the order or visibility of control pages. */
#include "panel_profile.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define A "00000001-0000-0000-0000000000000000"
#define B "00000002-0000-0000-0000000000000000"
#define C "00000003-0000-0000-0000000000000000"

static void event(uint8_t *out, uint8_t uuid, double value)
{
    memset(out, 0, 24);
    out[0] = uuid;
    uint64_t raw;
    memcpy(&raw, &value, sizeof(raw));
    for (unsigned n=0; n<8; ++n) out[16+n] = (uint8_t)(raw >> (8*n));
}

int main(void)
{
    cJSON *document = cJSON_Parse("{\"revision\":13,"
        "\"loxone\":{\"host\":\"mini.local\",\"user\":\"panel\",\"port\":80},"
        "\"display\":{\"nightUuid\":\"" C "\"},"
        "\"intercom\":{\"enabled\":true,\"uuid\":\"" B "\",\"gateUuid\":\"" C "\"},"
        "\"tabs\":[{\"kind\":\"energy\",\"energy\":{\"PowerPV\":{\"uuid\":\"" A "\",\"scale\":0.001,\"offset\":1}}},"
        "{\"kind\":\"controls\",\"blocks\":[{\"id\":\"szoba\",\"type\":\"irc\",\"name\":\"Szoba\",\"uuid\":\"" B "\"}]},"
        "{\"kind\":\"energy\",\"energy\":{\"PowerPV\":{\"uuid\":\"" B "\"}}}]}");
    panel_profile_t *model = malloc(sizeof(*model));
    assert(document && model && panel_profile_read(model, document, "integration-secret", "video-secret"));
    assert(model->generation == 13);
    assert(!strcmp(model->connection.password,"integration-secret"));
    assert(model->home.intercom.enabled && !strcmp(model->home.intercom.video_password,"video-secret"));
    assert(!strcmp(model->home.intercom.gate.name, "Kapunyitás"));
    assert(model->home.page_count == 3 && model->home.card_count == 1);
    assert(model->home.pages[1].first_card==0 && model->home.cards[0].type==LX_HOME_CARD_IRC);
    uint8_t delta[72];
    event(delta,1,1500);
    event(delta+24,2,0);
    event(delta+48,3,1);
    lx_values_t first={0},second={0},night={0};
    assert(lx_values_update_fields(&first,model->energy[0].fields,false,2,delta,sizeof(delta)));
    assert(lx_values_update_fields(&second,model->energy[2].fields,false,2,delta,sizeof(delta)));
    assert(lx_values_update(&night,&model->connection,2,delta,sizeof(delta)));
    assert(first.seen[2] && first.value[2]==2.5 && !first.seen[6]);
    assert(second.seen[2] && second.value[2]==0 && !second.seen[6]);
    assert(night.seen[6] && night.value[6]==1 && !night.seen[2]);
    assert(!lx_values_update_fields(&first,model->energy[0].fields,false,2,delta,sizeof(delta)-1));
    assert(first.seen[2] && first.value[2]==2.5);
    event(delta,1,18000);
    assert(lx_values_update_fields(&first,model->energy[0].fields,false,2,delta,24));
    assert(!first.seen[2] && first.value[2]==0);
    const cJSON *tabs=cJSON_GetObjectItemCaseSensitive(document,"tabs");
    cJSON *cards=cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(tabs,1),"blocks");
    assert(cJSON_AddItemToArray(cards,cJSON_Duplicate(cJSON_GetArrayItem(cards,0),true)));
    assert(!panel_profile_read(model,document,"",""));
    cJSON_Delete(document);
    free(model);
    puts("OK: two independent energy tabs, night state, intercom, IRC, malformed delta and duplicate card ID");
}
