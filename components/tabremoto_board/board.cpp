#include "board.hpp"
#include "bsp/m5stack_tab5.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include <atomic>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
namespace tabremoto {
static std::atomic<unsigned> downs{0},ups{0},errors{0};
static QueueHandle_t key_queue=nullptr;
bool keyboard_event(uint8_t& event){return key_queue&&xQueueReceive(key_queue,&event,0)==pdTRUE;}
static void reset_input(){if(key_queue){xQueueReset(key_queue);uint8_t reset=255;xQueueSend(key_queue,&reset,0);}}
KeyStats keyboard_stats(){return {downs.load(),ups.load(),errors.load()};}
static void keyboard_worker(void* arg){
 auto dev=(i2c_master_dev_handle_t)arg;
 uint8_t mode[]={0x10,0};
 if(i2c_master_transmit(dev,mode,2,50)!=ESP_OK){++errors;vTaskDelete(nullptr);return;}
 bool recovering=false;
 for(;;){uint8_t reg=2,count=0;
 if(recovering){
  if(i2c_master_transmit(dev,mode,2,50)!=ESP_OK){vTaskDelay(pdMS_TO_TICKS(500));continue;}
  // Drop stale packets from before the outage, then restart with no held keys.
  uint8_t clear[]={2,0};
  if(i2c_master_transmit(dev,clear,2,50)!=ESP_OK){vTaskDelay(pdMS_TO_TICKS(500));continue;}
  recovering=false;
 }
 if(i2c_master_transmit_receive(dev,&reg,1,&count,1,30)!=ESP_OK){++errors;reset_input();recovering=true;vTaskDelay(pdMS_TO_TICKS(500));continue;}
 for(unsigned i=0;i<count && i<32;i++){uint8_t event=255;reg=0x20;
 if(i2c_master_transmit_receive(dev,&reg,1,&event,1,30)!=ESP_OK){++errors;reset_input();recovering=true;break;}
 if(event==255)break;
 unsigned row=(event>>4)&7,col=event&15;
 if(row>=5||col>=14){++errors;continue;}
 if(event&128)++downs;else ++ups;
 if(key_queue&&xQueueSend(key_queue,&event,0)!=pdTRUE){++errors;xQueueReset(key_queue);uint8_t reset=255;xQueueSend(key_queue,&reset,0);}
 }vTaskDelay(pdMS_TO_TICKS(10));}
}

BoardStatus board_start() {
    BoardStatus state;
    esp_chip_info_t chip{}; esp_chip_info(&chip); state.chip_revision=chip.revision;
    esp_flash_get_size(nullptr,&state.flash_bytes);
    // The panel keeps scanning while the P4 reboots. Keep initialization dark
    // and release any backlight hold left by the previous application.
    gpio_hold_dis(BSP_LCD_BACKLIGHT);
    gpio_set_direction(BSP_LCD_BACKLIGHT,GPIO_MODE_OUTPUT);
    gpio_set_level(BSP_LCD_BACKLIGHT,0);
    gpio_hold_en(BSP_LCD_BACKLIGHT);
    bsp_display_cfg_t cfg{}; cfg.lvgl_port_cfg=ESP_LVGL_PORT_INIT_CONFIG();
    cfg.lvgl_port_cfg.task_stack=12288; cfg.lvgl_port_cfg.task_max_sleep_ms=20;
    cfg.buffer_size=BSP_LCD_H_RES*100; cfg.double_buffer=true;
    cfg.flags.buff_spiram=true; cfg.flags.sw_rotate=true;
    auto *display=bsp_display_start_with_config(&cfg);
    state.display=display!=nullptr;
    if(display && lvgl_port_lock(2000)) {
        bsp_display_rotate(display,LV_DISPLAY_ROTATION_90);
        state.touch=bsp_display_get_input_dev()!=nullptr;
        lvgl_port_unlock();
    }
    // Probe identity first; the retained bus then belongs to the raw-event worker.
    i2c_master_bus_config_t bus_cfg{}; bus_cfg.i2c_port=I2C_NUM_0;
    bus_cfg.sda_io_num=GPIO_NUM_0; bus_cfg.scl_io_num=GPIO_NUM_1;
    bus_cfg.clk_source=I2C_CLK_SRC_DEFAULT; bus_cfg.glitch_ignore_cnt=7;
    bus_cfg.flags.enable_internal_pullup=true;
    i2c_master_bus_handle_t bus=nullptr;
    if(i2c_new_master_bus(&bus_cfg,&bus)==ESP_OK) {
        i2c_master_dev_handle_t dev=nullptr; i2c_device_config_t cfg{};
        cfg.dev_addr_length=I2C_ADDR_BIT_LEN_7; cfg.device_address=0x6d; cfg.scl_speed_hz=100000;
        if(i2c_master_bus_add_device(bus,&cfg,&dev)==ESP_OK) {
            uint8_t reg=0xff,address=0;
            if(i2c_master_transmit_receive(dev,&reg,1,&address,1,50)==ESP_OK && address==0x6d) {
                reg=0xfe; state.keyboard=i2c_master_transmit_receive(dev,&reg,1,&state.keyboard_version,1,50)==ESP_OK;
            }
            key_queue=xQueueCreate(64,sizeof(uint8_t));
            if(state.keyboard && key_queue && xTaskCreate(keyboard_worker,"a164_raw",4096,dev,2,nullptr)==pdPASS)return state;
            i2c_master_bus_rm_device(dev);
        }
        i2c_del_master_bus(bus);
    }
    return state;
}
void board_prepare_reboot() {
    if(lvgl_port_lock(2000)) {lv_timer_enable(false);lvgl_port_unlock();}
    bsp_display_brightness_set(0);
    ledc_stop(LEDC_LOW_SPEED_MODE,static_cast<ledc_channel_t>(CONFIG_BSP_DISPLAY_BRIGHTNESS_LEDC_CH),0);
    gpio_hold_en(BSP_LCD_BACKLIGHT);
    vTaskDelay(pdMS_TO_TICKS(50));
}
}
