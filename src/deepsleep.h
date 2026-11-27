#ifndef DEEPSLEEP_H_
#define DEEPSLEEP_H_

#include "driver/rtc_io.h"
#include <Fonts/FreeMonoBold18pt7b.h>

constexpr gpio_num_t POWER_SWITCH = GPIO_NUM_5;

static void powerswitch_init()
{
    rtc_gpio_init(POWER_SWITCH);
    rtc_gpio_set_direction(POWER_SWITCH, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pullup_dis(POWER_SWITCH);
    rtc_gpio_pulldown_dis(POWER_SWITCH);
}

static void initiate_deep_sleep(unsigned long delay_ms, const char *msg, uint16_t color=ST77XX_GREEN)
{
    if (delay_ms != 0)
    {
        canvas.fillScreen(color);
        if (msg != nullptr)
        {
            canvas.setFont(&FreeMonoBold18pt7b);
            canvas.setTextColor(color != ST77XX_RED ? ST77XX_BLUE : ST77XX_WHITE); // default is BLUE
            canvas_println_centered(msg, 75);
        }
        display.drawRGBBitmap(0, 0, canvas.getBuffer(), DISPLAY_WIDTH, DISPLAY_HEIGHT);
        delay(delay_ms);
    }
    //digitalWrite(TFT_BACKLITE, LOW); // make sure backlight is off
    // 1. Shut down I2C and isolate SDA/SCL so they don't back-power STEMMA QT sensors
    Wire.end();
    pinMode(SDA, INPUT);
    pinMode(SCL, INPUT);
    // 2. Shut down TFT, STEMMA QT power (GPIO 7), and NeoPixel (GPIO 21)
    pinMode(TFT_BACKLITE, OUTPUT);
    digitalWrite(TFT_BACKLITE, LOW); // make sure backlight is off

    pinMode(TFT_I2C_POWER, OUTPUT);
    digitalWrite(TFT_I2C_POWER, LOW);
    gpio_hold_en(static_cast<gpio_num_t>(TFT_I2C_POWER));

    pinMode(NEOPIXEL_POWER, OUTPUT);
    digitalWrite(NEOPIXEL_POWER, LOW);
    gpio_hold_en(static_cast<gpio_num_t>(NEOPIXEL_POWER));
    gpio_deep_sleep_hold_en();
    // normally watching for "power switch" to go on, so notice when it goes HIGH
    // if "power switch" is on, need to wait for it to be off, so notice when it goes LOW
    if (esp_sleep_enable_ext0_wakeup(POWER_SWITCH, !digitalRead(POWER_SWITCH) ? HIGH : LOW))
    {
        Logger.error("%s(): ERROR\n", __func__);
    }
    else
    {
        Logger.info("%s(): Entering deepsleep! [%u]\n", __func__, digitalRead(POWER_SWITCH));
    }
    Serial.flush();
    esp_deep_sleep_start();
}

static void release_deep_sleep()
{
    // 1. Release deep sleep pin holds and restore normal GPIO control
    gpio_deep_sleep_hold_dis();
    gpio_hold_dis(static_cast<gpio_num_t>(SDA));
    gpio_hold_dis(static_cast<gpio_num_t>(SCL));
    gpio_hold_dis(static_cast<gpio_num_t>(TFT_I2C_POWER));
    gpio_hold_dis(static_cast<gpio_num_t>(NEOPIXEL_POWER));
    rtc_gpio_deinit(POWER_SWITCH);
    pinMode(POWER_SWITCH, INPUT);
    // 2. Restore power to STEMMA QT (VL53L4CD) and TFT
    pinMode(TFT_I2C_POWER, OUTPUT);
    digitalWrite(TFT_I2C_POWER, HIGH);
    pinMode(NEOPIXEL_POWER, OUTPUT);
    digitalWrite(NEOPIXEL_POWER, HIGH);
    delay(500); // Give the VL53L4CD ~10ms to power up before I2C init
    // 3. Re-initialize I2C and the VL53L4CD sensor
    Wire.begin(SDA, SCL);
}

static void show_wakeup_cause(int16_t x=0, int16_t y=75)
{
    char buf[40];

    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    const char *cause_str = "UNKNOWN";
    switch (cause)
    {
        case ESP_SLEEP_WAKEUP_UNDEFINED:    // reset was not caused by exit from deep sleep
            cause_str = "RESET"; break;
        case ESP_SLEEP_WAKEUP_EXT0:
            cause_str = "POWER SWITCH"; break;
        case ESP_SLEEP_WAKEUP_EXT1:
            cause_str = "EXT1"; break;
        case ESP_SLEEP_WAKEUP_TIMER:
            cause_str = "TIMER"; break;
        case ESP_SLEEP_WAKEUP_VBAT_UNDER_VOLT:   // Wakeup caused by VDD_BAT under voltage.
            cause_str = "VBATT UNDERVOLT"; break;
        default:
            break;
    }
    snprintf(buf, sizeof(buf)-1, "%s  [%u]", cause_str, cause);
    Logger.info("Wakeup: %s\n", buf);
    canvas.setFont(&FreeMono9pt7b);
    canvas.setTextColor(ST77XX_GREEN);
    canvas_println_centered(buf, y);
    display.drawRGBBitmap(0, 0, canvas.getBuffer(), DISPLAY_WIDTH, DISPLAY_HEIGHT);
}

#endif  // DEEPSLEEP_H_
