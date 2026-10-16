// main/main.c —— PC 状态监控应用入口。
//
// 该固件是一个派生应用:启动后直接进入自绘的科技感仪表界面,不再提供基线
// 硬件测试菜单。设备经 USB 数据线(USB Serial/JTAG 控制台 VFS)连接电脑,
// 周期性发送状态请求帧,电脑端伴侣程序回传状态数据并显示在屏幕上。
#include "app_pcmon.h"
#include "bsp_battery.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"
#include "esp_log.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "PC status monitor starting");

    bsp_i2c_init();
    bsp_i2c_scan();

    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "display/LVGL init failed; check SPI wiring (MOSI=%d SCLK=%d CS=%d DC=%d BL=%d)",
                 BSP_LCD_MOSI, BSP_LCD_SCLK, BSP_LCD_CS, BSP_LCD_DC, BSP_LCD_BL);
        return;
    }
    bsp_display_backlight(100);

    if (bsp_battery_init() != ESP_OK) {
        ESP_LOGW(TAG, "battery gauge unavailable; level shown as --");
    }

    if (!app_pcmon_start()) {
        ESP_LOGE(TAG, "PC monitor app failed to start");
    }
}
