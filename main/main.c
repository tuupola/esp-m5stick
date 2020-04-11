/*

SPDX-License-Identifier: MIT-0

MIT No Attribution

Copyright (c) 2020 Mika Tuupola

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <mipi_dcs.h>
#include <mipi_display.h>
#include <copepod.h>
#include <copepod_hal.h>
#include <fps.h>
#include <font8x8.h>
#include <rgb565.h>

#include "esp_i2c_hal.h"
#include "axp192.h"
#include "bm8563.h"
#include "sdkconfig.h"

static const char *TAG = "main";
static float fb_fps;

bm8563_datetime_t rtc;
spi_device_handle_t spi;

/*

Cap framebuffer to 45fps.
T = 1000 / 45 / (1000 / CONFIG_FREERTOS_HZ)

*/
void framebuffer_task(void *params)
{
    TickType_t last;
    const TickType_t period = 1000 / 45 / portTICK_RATE_MS;

    last = xTaskGetTickCount();

    while (1) {
        pod_flush();
        fb_fps = fps();
        vTaskDelayUntil(&last, period);
    }

    vTaskDelete(NULL);
}

/*

Update fps counter on top left corner every 1 second.

*/
void fps_task(void *params)
{
    uint16_t color = rgb565(0, 255, 0);
    char message[128];

    while (1) {
        sprintf(message, "%.*f fps", 1, fb_fps);
        pod_put_text(message, 6, 6, color, font8x8);

        vTaskDelay(1000 / portTICK_RATE_MS);
    }

    vTaskDelete(NULL);
}

void demo_task(void *params)
{
    float vacin, iacin, vvbus, ivbus, vts, temp, pbat, vbat, icharge, idischarge, vaps, cbat;
    uint8_t power, charge;

    while (1) {
        bm8563_read(&rtc);
        ESP_LOGI(TAG,
            "RTC: %04d-%02d-%02d %02d:%02d:%02d",
            rtc.year, rtc.month, rtc.day, rtc.hours, rtc.minutes, rtc.seconds
        );

        axp192_read(AXP192_ACIN_VOLTAGE, &vacin);
        axp192_read(AXP192_ACIN_CURRENT, &iacin);
        axp192_read(AXP192_VBUS_VOLTAGE, &vvbus);
        axp192_read(AXP192_VBUS_CURRENT, &ivbus);
        axp192_read(AXP192_TEMP, &temp);
        axp192_read(AXP192_TS_INPUT, &vts);
        axp192_read(AXP192_BATTERY_POWER, &pbat);
        axp192_read(AXP192_BATTERY_VOLTAGE, &vbat);
        axp192_read(AXP192_CHARGE_CURRENT, &icharge);
        axp192_read(AXP192_DISCHARGE_CURRENT, &idischarge);
        axp192_read(AXP192_APS_VOLTAGE, &vaps);
        axp192_read(AXP192_COULOMB_COUNTER, &cbat);

        ESP_LOGI(TAG,
            "vacin: %.2fV iacin: %.2fA vvbus: %.2fV ivbus: %.2fA vts: %.2fV temp: %.0fC "
            "pbat: %.2fmW vbat: %.2fV icharge: %.2fA idischarge: %.2fA, vaps: %.2fV "
            "cbat: %.2fmAh",
            vacin, iacin, vvbus, ivbus, vts, temp, pbat, vbat, icharge, idischarge, vaps, cbat
        );

        axp192_ioctl(AXP192_READ_POWER_STATUS, &power);
        axp192_ioctl(AXP192_READ_CHARGE_STATUS, &charge);
        ESP_LOGI(TAG,
            "power: 0x%02x charge: 0x%02x",
            power, charge
        );
        vTaskDelay(1000 / portTICK_RATE_MS);
    }
    vTaskDelete(NULL);
}

void app_main()
{
    ESP_LOGI(TAG, "SDK version: %s", esp_get_idf_version());
    ESP_LOGI(TAG, "Heap when starting: %d", esp_get_free_heap_size());

    rtc.year = 2020;
    rtc.month = 12;
    rtc.day = 31;
    rtc.hours = 23;
    rtc.minutes = 59;
    rtc.seconds = 45;

    i2c_hal_master_init();

    ESP_LOGD(TAG, "Initializing AXP192");
    axp192_init(i2c_hal_master_read, i2c_hal_master_write);
    axp192_ioctl(AXP192_COULOMB_COUNTER_ENABLE, NULL);
    axp192_ioctl(AXP192_COULOMB_COUNTER_CLEAR, NULL);

    ESP_LOGD(TAG, "Initializing BM8563");
    bm8563_init(i2c_hal_master_read, i2c_hal_master_write);
    bm8563_write(&rtc);

    ESP_LOGD(TAG, "Initializing display");
    pod_init();

    ESP_LOGI(TAG, "Heap after init: %d", esp_get_free_heap_size());

    xTaskCreatePinnedToCore(demo_task, "Demo", 4096, NULL, 1, NULL, 1);
    xTaskCreatePinnedToCore(fps_task, "FPS", 8192, NULL, 2, NULL, 1);
    xTaskCreatePinnedToCore(framebuffer_task, "Framebuffer", 8192, NULL, 1, NULL, 0);
}
