#include "panel_energy_stats.h"
#include "panel_config.h"
#include "zld_loxone.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static int64_t ticks;
static time_t wall=1795763580;
static unsigned gen=1,energy_index;
static bool online=true,ready=true,mapped=true;
static const char *grid="grid-a";
const lx_field_t lx_fields[LX_FIELD_COUNT]={{"PowerGrid",0,0},{"PowerBat",0,0},{"PowerPV",0,0},{"PowerFull",0,0}};

int64_t esp_timer_get_time(void) {return ticks;}
time_t __wrap_time(time_t *out) {if (out) *out=wall;return wall;}
bool zld_assets_ready(void) {return ready;}
uint32_t panel_config_generation(void) {return gen;}
cJSON *panel_config_snapshot(void)
{
    cJSON *p=cJSON_CreateObject();
    cJSON_AddNumberToObject(p,"revision",gen);
    cJSON *lx=cJSON_AddObjectToObject(p,"loxone");
    cJSON_AddStringToObject(lx,"host","miniserver.local");
    cJSON_AddNumberToObject(lx,"port",80);
    cJSON *tabs=cJSON_AddArrayToObject(p,"tabs");
    for (unsigned i=0;i<2;i++) {
        cJSON *tab=cJSON_CreateObject(); cJSON_AddItemToArray(tabs,tab);
        cJSON_AddStringToObject(tab,"kind",i==energy_index?"energy":"controls");
        if (i==energy_index) {
            cJSON *energy=cJSON_AddObjectToObject(tab,"energy");
            cJSON *field=cJSON_AddObjectToObject(energy,"PowerGrid");
            cJSON_AddStringToObject(field,"uuid",grid);
            cJSON_AddNumberToObject(field,"scale",1);
            cJSON_AddNumberToObject(field,"offset",0);
        }
    }
    return p;
}
bool zld_loxone_energy_at(unsigned tab,unsigned generation,lx_values_t *out)
{
    *out=(lx_values_t){.seen={true,true,true,true},.value={-1.,-2.,4.,1.}};
    return tab==energy_index && gen==generation && online && mapped;
}

static double today(void)
{
    panel_daily_t day={0}; bool live;
    panel_energy_stats_read(energy_index,gen,&day,&live);
    return day.production;
}

static void tick(unsigned seconds)
{
    ticks+=seconds*1000000LL;
    wall+=seconds;
    panel_energy_stats_tick();
}

static void near(double a,double b)
{
    assert(fabs(a-b)<.000001);
}

int main(int argc,char **argv)
{
    assert(argc==2);
    if (!strcmp(argv[1],"restore")) {
        wall+=86400+400;
        grid="grid-b";
        energy_index=1;
        tick(1);
        assert(today()>.2 && today()<.34);
        puts("A napi összesítő visszatöltődött a megmaradt ellenőrzőpontból.");
        return 0;
    }
    tick(1);
    for (int i=0;i<45;i++)tick(1);
    near(today(),4.*45/3600);
    double previous=today();
    online=false; tick(1); online=true; tick(1); near(today(),previous);
    ready=false; tick(1); ready=true; tick(1); near(today(),previous);
    mapped=false; tick(1); mapped=true; tick(1); near(today(),previous);
    tick(12); near(today(),previous);
    wall+=100; tick(1); near(today(),previous);
    gen++;energy_index=1;tick(1);near(today(),previous);
    gen++;grid="grid-b";tick(1);near(today(),0);
    wall+=86400; tick(1);near(today(),0);
    for (int i=0;i<650;i++)tick(1);
    bool live;panel_daily_t d;
    assert(panel_energy_stats_read(1,gen,&d,&live) && live && d.seen==31);
    near(d.production,4.*650/3600);
    near(d.exported,650./3600);
    near(d.pv_home,650./3600);
    near(d.pv_stored,2.*650/3600);
    assert(!panel_energy_stats_read(1,gen-1,&d,&live));
    puts("Napi integrálás, előjel, generáció, rendezés, új párosítás, kimaradás és napváltás rendben.");
    return 0;
}
