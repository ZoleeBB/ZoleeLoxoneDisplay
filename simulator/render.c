#include "panel_ui.h"
#include "fixture.h"
#include "lvgl.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SIDE 720
static uint32_t framebuffer[SIDE*SIDE], draw_buffer[SIDE*64];
static lv_indev_data_t pointer;
static const char *output;

static void draw(lv_display_t *display, const lv_area_t *area, uint8_t *pixels)
{
    size_t width=area->x2-area->x1+1;
    for (int y=area->y1;y<=area->y2;y++) {
        memcpy(framebuffer+y*SIDE+area->x1,pixels,sizeof(uint32_t)*width);
        pixels+=width*sizeof(uint32_t);
    }
    lv_display_flush_ready(display);
}

static void read_pointer(lv_indev_t *dev,lv_indev_data_t *out)
{
    (void)dev;
    out->point=pointer.point;
    out->state=pointer.state;
}

static void step(unsigned ms)
{
    for (unsigned t=0;t<ms;t+=10) {lv_tick_inc(10);lv_timer_handler();}
}

static void tap(int x,int y)
{
    pointer.point=(lv_point_t){x,y};
    pointer.state=LV_INDEV_STATE_PRESSED;step(75);
    pointer.state=LV_INDEV_STATE_RELEASED;step(350);
}

static void snapshot(const char *name)
{
    char path[512];
    snprintf(path,sizeof(path),"%s/%s.ppm",output,name);
    FILE *f=fopen(path,"wb");
    assert(f);
    fprintf(f,"P6\n%d %d\n255\n",SIDE,SIDE);
    for (size_t i=0;i<SIDE*SIDE;i++) {
        uint32_t p=framebuffer[i];
        uint8_t rgb[3]={(uint8_t)(p>>16),(uint8_t)(p>>8),(uint8_t)p};
        assert(fwrite(rgb,1,3,f)==3);
    }
    assert(fclose(f)==0);
}

int main(int argc,char **argv)
{
    if (argc!=4) {fprintf(stderr,"panel-preview SD_KÖNYVTÁR FIXTURE_JSON KIMENET\n");return 2;}
    fixture_sd=argv[1];fixture_profile=argv[2];output=argv[3];
    setenv("TZ","UTC0",1);tzset();
    lv_init();
    lv_display_t *d=lv_display_create(SIDE,SIDE);
    lv_display_set_color_format(d,LV_COLOR_FORMAT_XRGB8888);
    lv_display_set_buffers(d,draw_buffer,NULL,sizeof(draw_buffer),LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(d,draw);
    lv_indev_t *input=lv_indev_create();
    lv_indev_set_type(input,LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(input,read_pointer);
    fixture_complete=false;fixture_online=false;fixture_assets=false;
    assert(panel_ui_start()==ESP_OK);
    step(1000);
    snapshot("wifi-networks");
    tap(200,224);
    step(650);
    snapshot("wifi-password");
    fixture_online=true;step(600);
    snapshot("setup-ip");
    fixture_complete=true;fixture_assets=true;fixture_generation++;step(800);
    assert(fixture_page==0 && fixture_brightness==80);
    snapshot("thermostat");
    tap(450,610);
    assert(fixture_calls==1);
    tap(270,40);assert(fixture_page==1);
    snapshot("lighting");
    tap(100,210);assert(fixture_calls==2);
    tap(615,115);step(300);
    tap(180,361);step(300);
    snapshot("lighting-outputs");
    tap(669,59);
    tap(450,42);assert(fixture_page==2);
    snapshot("controls");
    tap(642,41);assert(fixture_page==3);
    step(1100);
    snapshot("energy");
    step(9000);
    assert(fixture_brightness==10 && fixture_page==-1);
    snapshot("standby");
    unsigned before=fixture_calls;
    tap(270,42);
    assert(fixture_brightness==80 && fixture_page==3 && fixture_calls==before);
    puts("Rendben: Wi-Fi varázsló, webes átadás, lapváltás, IRC és jelenetparancs, kimenetek, energia, képernyőkímélő és ébresztés.");
    return 0;
}
