#ifndef DEEPSLEEP_H_
#define DEEPSLEEP_H_

#include "driver/rtc_io.h"
static void initiate_deep_sleep(unsigned long delay_ms, const char *msg, uint16_t color=ST77XX_GREEN)
{
    if (delay_ms != 0)
    {
        canvas.fillScreen(color);
        if (msg != nullptr)
        {
            canvas.setFont(&FreeMono18pt7b);
            canvas.setTextColor(ST77XX_WHITE);
            canvas_println_centered(msg, 75);
        }
        display.drawRGBBitmap(0, 0, canvas.getBuffer(), DISPLAY_WIDTH, DISPLAY_HEIGHT);
        delay(delay_ms);
    }
    digitalWrite(TFT_BACKLITE, LOW); // make sure backlight is off
    // normally watching for "power switch" to go on, so notice when it goes HIGH
    // if "power switch" is on, need to wait for it to be off, so notice when it goes LOW
    if (esp_sleep_enable_ext0_wakeup(GPIO_NUM_5, !digitalRead(GPIO_NUM_5) ? HIGH : LOW))
    {
        Serial.printf("%s(): ERROR\n", __func__);
    }
    else
    {
        rtc_gpio_isolate(GPIO_NUM_5);
        Serial.printf("%s(): Entering deepsleep! [%u]\n", __func__, digitalRead(GPIO_NUM_5));
    }
    Serial.flush();
    esp_deep_sleep_start();
}

#endif  // DEEPSLEEP_H_
