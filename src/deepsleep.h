#ifndef DEEPSLEEP_H_
#define DEEPSLEEP_H_

static void initiate_deep_sleep(unsigned long delay_ms)
{
    canvas.fillScreen(ST77XX_RED);
    display.drawRGBBitmap(0, 0, canvas.getBuffer(), DISPLAY_WIDTH, DISPLAY_HEIGHT);
    delay(delay_ms);
    digitalWrite(TFT_BACKLITE, LOW); // make sure backlight is off
    Serial.flush();
    esp_deep_sleep_start();
}

#endif  // DEEPSLEEP_H_
