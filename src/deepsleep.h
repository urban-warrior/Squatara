#ifndef DEEPSLEEP_H_
#define DEEPSLEEP_H_

#include "driver/rtc_io.h"
static void initiate_deep_sleep(unsigned long delay_ms, char *msg=nullptr)
{
    if (delay_ms != 0)
    {
        canvas.fillScreen(ST77XX_RED);
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
    if (esp_sleep_enable_ext0_wakeup(GPIO_NUM_5, /* LOW */ HIGH))
    {
        Serial.printf("%s(): ERROR\n", __func__);
    }
    else
    {
        rtc_gpio_isolate(GPIO_NUM_5);
        Serial.printf("%s(): Entering deepsleep! [%u]\n", __func__, digitalRead(GPIO_NUM_5));
    }
    Serial.flush();
    delay(1000); // TODO necessary?
    esp_deep_sleep_start();
}

#endif  // DEEPSLEEP_H_
