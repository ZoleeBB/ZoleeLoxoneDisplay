#include "board_touch.h"
#include "board_click.h"
#include "esp_lv_adapter_input.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static esp_lv_adapter_touch_callbacks_t cb;
static esp_lcd_touch_point_data_t hw[5], shown;
static uint8_t fingers, report;
static unsigned presses, fresh_reads;
static bool failed;

esp_err_t esp_lv_adapter_set_touch_callbacks(lv_indev_t *dev,const esp_lv_adapter_touch_callbacks_t *callbacks)
{
    assert(dev!=NULL);
    cb=*callbacks;
    return ESP_OK;
}
esp_err_t esp_lcd_panel_io_rx_param(void *io,int reg,void *data,size_t len)
{
    assert(io==(void *)42 && reg==0x814e && len==1);
    *(uint8_t *)data=report;
    return ESP_OK;
}
esp_err_t esp_lcd_touch_read_data(esp_lcd_touch_handle_t tp)
{
    assert(tp && (report & 0x80));
    ++fresh_reads;
    return failed ? ESP_FAIL : ESP_OK;
}
esp_err_t esp_lcd_touch_get_data(esp_lcd_touch_handle_t tp, esp_lcd_touch_point_data_t *points,
                                 uint8_t *count,uint8_t max_count)
{
    assert(tp && max_count==5);
    memcpy(points,hw,fingers*sizeof(*points));
    *count=fingers;
    return ESP_OK;
}
void board_sound_touch(void) { ++presses; }

static void poll(uint8_t status,uint8_t visible)
{
    typeof(*((esp_lcd_touch_handle_t)0)) model={.io=(void *)42};
    uint8_t count=123;
    report=status;
    assert(cb.custom_touch_read(&model,&shown,&count,1,NULL)==(failed?ESP_FAIL:ESP_OK));
    assert(count==visible);
}
static void sensor(void)
{
    lv_indev_t device={0};
    assert(board_touch_feedback(&device)==ESP_OK);
    poll(0,0);
    hw[0]=(esp_lcd_touch_point_data_t){30,50,1};
    fingers=1; poll(0x81,1); assert(presses==1 && shown.x==30);
    for(int i=0;i<24;i++) poll(0,1); /* poll interval shorter than GT911 scan */
    assert(presses==1 && fresh_reads==1);
    hw[0].x=620; poll(0x81,1); assert(presses==1 && shown.x==620);
    failed=true; poll(0x81,0);
    failed=false; poll(0x81,1); assert(presses==1);
    hw[1]=(esp_lcd_touch_point_data_t){20,60,3}; fingers=2;
    poll(0x82,1); assert(presses==2 && shown.track_id==1);
    hw[0]=hw[1]; fingers=1; poll(0x81,1); assert(presses==2 && shown.track_id==3);
    fingers=0; poll(0x80,0); poll(0,0);
    fingers=1; poll(0x81,1); assert(presses==3);
}
static void waveform(void)
{
    board_click_mixer_t m={0};
    int16_t sample[2*200]={0};
    atomic_store(&m.pending,1);
    assert(board_click_mix(&m,sample,31));
    assert(board_click_mix(&m,sample+2*31,169));
    assert(!board_click_pending(&m) && !sample[0]);
    int peak=0;
    int64_t energy=0;
    for(int i=0;i<200;i++) {
        int v=sample[2*i], mag=v<0?-v:v;
        assert(v==sample[2*i+1]);
        if(i>=144) assert(v==0); /* 6 ms at 24 kHz */
        if(mag>peak) peak=mag;
        energy+=(int64_t)v*v;
    }
    assert(peak==1320 && energy>100000 && energy<144*450*450);
    int16_t rails[2*200];
    for(int i=0;i<200;i++) { rails[2*i]=INT16_MAX; rails[2*i+1]=INT16_MIN; }
    atomic_store(&m.pending,1);
    assert(board_click_mix(&m,rails,200));
    for(int i=0;i<200;i++) {
        int p=sample[2*i];
        assert(rails[i*2] == (p>0?INT16_MAX:INT16_MAX+p));
        assert(rails[i*2+1] == (p<0?INT16_MIN:INT16_MIN+p));
    }
}
int main(void)
{
    sensor(); waveform();
    puts("GT911: idle, new/stale reports, drag, second finger, release, I2C recovery. PCM: duration, peak, RMS, split blocks and saturation OK.");
}
