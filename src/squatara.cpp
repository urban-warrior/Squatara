#include <Arduino.h>
#include "configuration.h"
#include <TryStartSerial.h>
#include <Preferences.h>  // Non-volatile storage (NVS)
#include <Adafruit_MAX1704X.h>
#include <Adafruit_ST7789.h>
#include <Fonts/FreeMono9pt7b.h>
#include <Fonts/FreeMono12pt7b.h>
#include <Fonts/FreeMono18pt7b.h>
#include <HackRegular26pt7b.h>
#include <DejaVu_Sans_Mono_14.h>
#include "tof-vl53l4cd.h"
#include "buzzer.h"
#include "deepsleep.h"
#include <DebugLogger.h>
#include "persistent.h"

static Adafruit_MAX17048 lipo;
static Adafruit_ST7789 display = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
static ToF_Sensor tof_sensor(&Wire, 11, 0x30); // don't use default first! NOLINT(*-interfaces-global-init)
static ToF_Sensor floor_sensor(&Wire, 12 /*, address -- defaults to 0x29 */); // NOLINT(*-interfaces-global-init)
static bool tof_configured = false;
static Buzzer buzzer(A0, A5); // DEBUG LED on A5!

// ST77XX_BLACK ST77XX_WHITE ST77XX_RED ST77XX_GREEN ST77XX_BLUE ST77XX_CYAN ST77XX_MAGENTA ST77XX_YELLOW ST77XX_ORANGE
constexpr int16_t DISPLAY_WIDTH=240;
constexpr int16_t DISPLAY_HEIGHT=135;
static GFXcanvas16 canvas(DISPLAY_WIDTH, DISPLAY_HEIGHT);

static void esp32_reset()
{
    canvas.fillScreen(ST77XX_BLACK);
    canvas.setFont(&FreeMono18pt7b);
    canvas.setCursor(0, 0);
    canvas.println(""); // get to start of first line
    canvas.setTextColor(ST77XX_YELLOW);
    canvas.println("\n>> RESET <<");
    display.drawRGBBitmap(0, 0, canvas.getBuffer(), DISPLAY_WIDTH, DISPLAY_HEIGHT);
    delay(1000);
    ESP.restart();
}

// #include "buttons.h"
#include "cli_commands.h"

static void show_product_name(const char *name = "Squatara", int16_t y = 13)
{
    int16_t top_left_x, top_left_y;
    uint16_t text_w, text_h;

    canvas.setFont(&FreeMono12pt7b);
    canvas.fillScreen(ST77XX_BLACK);
    canvas.getTextBounds(name, 0, y, &top_left_x, &top_left_y, &text_w, &text_h);
    Logger.debug("<%s> %d  %d  %u  %u\n", __func__, top_left_x, top_left_y, text_w, text_h);
    canvas.setCursor(static_cast<int16_t>((canvas.width() - text_w) / 2), y);
    canvas.setTextColor(ST77XX_WHITE);
    canvas.println(name);
    display.drawRGBBitmap(0, 0, canvas.getBuffer(), DISPLAY_WIDTH, DISPLAY_HEIGHT);
    Logger.debug("<%s> X: %u  Y:%u\n", __func__, canvas.getCursorX(), canvas.getCursorY());
}

static void show_build(int16_t y = 37)
{
    canvas.setCursor(0, y);
    canvas.setTextColor(ST77XX_RED);
    canvas.setFont(&FreeMono9pt7b);
    canvas.println(__DATE__ "@" __TIME__);
    display.drawRGBBitmap(0, 0, canvas.getBuffer(), DISPLAY_WIDTH, DISPLAY_HEIGHT);
    Logger.debug("<%s> X: %u  Y:%u\n", __func__, canvas.getCursorX(), canvas.getCursorY());
}

static void show_errors(bool tof, bool floor, int16_t y = 55)
{
    canvas.setCursor(0, y);
    canvas.setTextColor(ST77XX_YELLOW);
    canvas.setFont(&FreeMono9pt7b);
    if ( !tof) canvas.println("No Presence Sensor!");
    if ( !floor) canvas.println("No Floor Sensor!");
    display.drawRGBBitmap(0, 0, canvas.getBuffer(), DISPLAY_WIDTH, DISPLAY_HEIGHT);
    Logger.debug("<%s> X: %u  Y:%u\n", __func__, canvas.getCursorX(), canvas.getCursorY());
}

static void timeout_start_screen(unsigned long ms)
{
    unsigned long start = millis();
    canvas.setFont(&FreeMono9pt7b);
    canvas.setTextColor(ST77XX_YELLOW);
    canvas.setCursor(0, 111);
    while (millis() - start < ms)
    {
        if ((millis() - start) % (ms / 10) == 0) // 10 stars fit display line
        {
            canvas.print("* ");
            display.drawRGBBitmap(0, 0, canvas.getBuffer(), DISPLAY_WIDTH, DISPLAY_HEIGHT);
        }
    }
    canvas.fillScreen(ST77XX_BLACK);
    show_product_name();
}

void setup()
{
    preferences.begin("configuration", false /* read-write */);
    Logger.addOutput(&Serial);
    setup_log_level();
    try_start_serial("\033[2J\033[1;1H" BUILD_STAMP);
    Logger.info("C++ Standard Value: %lu\n", __cplusplus);
    setup_persistent();
#ifdef SQUATARA_BUTTONS_H
    setup_buttons();
#endif // SQUATARA_BUTTONS_H
    pinMode(TFT_BACKLITE, OUTPUT);
    digitalWrite(TFT_BACKLITE, LOW);
    display.init(DISPLAY_HEIGHT, DISPLAY_WIDTH); // Init ST7789 240x135
    display.setRotation(3);
    Logger.info("Display w: %u  h: %u\n", display.width(), display.height());
    if (!lipo.begin())
    {
        Serial.println("LIPO not found");
    }
    tof_configured = tof_sensor.configure();
    if (!tof_configured)
    {
        Serial.println("Distance sensor error!");
    }
    else
    {
        Logger.info("max distance: %lu mm\n", tof_sensor.max_mm);
    }
    bool floor_sensor_configured = floor_sensor.configure();
    if (!floor_sensor_configured)
    {
        Serial.println("Floor sensor error!");
    }
    else
    {
        Logger.info("Floor sensor found");
    }

#ifdef SQUATARA_BUTTONS_H
    check_buttons();
#endif // SQUATARA_BUTTONS_H
    show_product_name();
    show_build();
    show_errors(tof_configured, floor_sensor_configured);
    digitalWrite(TFT_BACKLITE, HIGH);
    timeout_start_screen(5000);
#ifdef SQUATARA_CLI_COMMANDS_H
    cli_enable_detect();
    if (cli_begun)
    {
        Serial.println("\nCLI ready -- type 'help' for commands");
        cli.refreshPrompt();
    }
#endif  // SQUATARA_CLI_COMMANDS_H
    buzzer.on();
    delay(250 /* ms */);
    buzzer.off();
} // end setup()

static void battery_status_display(int16_t x, int16_t y)
{
    float cell_soc = lipo.cellPercent();
    [[maybe_unused]] float cell_roc = lipo.chargeRate();
    [[maybe_unused]] float cell_vdc = lipo.cellVoltage();
    constexpr float minimum_allowed_soc = 1.0 /* percent */;
    constexpr float warning_soc = 2.0 /* percent */;
    char buf[40];
    constexpr uint16_t bar_graph_height = 15;
    constexpr int16_t bar_graph_margin = 8;
    constexpr uint16_t bar_graph_width = 200;
    constexpr uint16_t BAR_GRAPH_COLOR = ST77XX_BLUE;
    constexpr int16_t y_offset = 20;

    y = static_cast<int16_t>(y - y_offset);

    if (cell_soc > 100.0) cell_soc = 100.0;
    snprintf(buf, sizeof(buf) - 1, "%3.0f%%", cell_soc);
    if (cell_soc <= minimum_allowed_soc) initiate_deep_sleep(10000);
    uint16_t battery_status_color = (cell_soc >= warning_soc) ? BAR_GRAPH_COLOR : ST77XX_RED;
    canvas.setTextColor(battery_status_color);
    canvas.setFont(&DejaVu_Sans_Mono_14);
    canvas.setCursor(x, y);
    canvas.fillRect(x, static_cast<int16_t>(y + bar_graph_margin), DISPLAY_WIDTH, bar_graph_height, ST77XX_BLACK /* ST77XX_RED */);
    canvas.setCursor(x, y);
    Logger.debug("<%s> X: %i  Y:%d\n", __func__,  x, y);
    canvas.drawRect(x, static_cast<int16_t>(y + bar_graph_margin), bar_graph_width, bar_graph_height, battery_status_color);
    canvas.fillRect(x, static_cast<int16_t>(y + bar_graph_margin), static_cast<int16_t>((bar_graph_width * cell_soc) / 100), bar_graph_height,
                    battery_status_color);
    canvas.setCursor(static_cast<int16_t>(x + bar_graph_width + 4), static_cast<int16_t>(y + 20)); // move to value field
    canvas.print(buf);
    canvas.setCursor(x, y); //restore cursor position
    display.drawRGBBitmap(0, 0, canvas.getBuffer(), DISPLAY_WIDTH, DISPLAY_HEIGHT);
}

static uint16_t bar_graph(uint32_t max_value, uint8_t threshold, uint16_t value, int16_t x, int16_t y)
{
    constexpr uint16_t bar_graph_height = 15;
    constexpr int16_t bar_graph_margin = 8;
    constexpr uint16_t bar_graph_width = 200;
    int16_t x_save = canvas.getCursorX(); // save cursor position to restore
    int16_t y_save = canvas.getCursorY(); // save cursor position to restore
    auto threshold_value = static_cast<uint16_t>((threshold * max_value) / 100);

    uint8_t percent = (100 * value) / max_value;
    canvas.fillRect(x, static_cast<int16_t>(y + bar_graph_margin), DISPLAY_WIDTH, bar_graph_height, ST77XX_BLACK);
    canvas.fillRect(x, static_cast<int16_t>(y + bar_graph_margin),
                    static_cast<int16_t>((bar_graph_width * percent) / 100), bar_graph_height,
                    ((value > threshold_value) ? ST77XX_GREEN : ST77XX_RED));
    canvas.setTextColor((value > threshold_value) ? ST77XX_GREEN : ST77XX_RED);
    canvas.setCursor(static_cast<int16_t>(x + bar_graph_width + 8), static_cast<int16_t>(y + 20)); // move to value field
    canvas.setFont(&FreeMono9pt7b); // set font for value field
    Logger.debug("<%s> X: %u Y: %u  %u\n", __func__, x + bar_graph_width + 9, y+20, value);
    if (value <= 999)
    {
        canvas.printf("%3u", value);
    }
    else
    {
        canvas.print("###");
    }
    canvas.setCursor(x_save, y_save); // restore (text) cursor position
    display.drawRGBBitmap(0, 0, canvas.getBuffer(), DISPLAY_WIDTH, DISPLAY_HEIGHT);
    return threshold_value;
}

static void show_floor_range(int32_t range, int16_t y)
{
    static bool once = false;
    int32_t display_range = range;
    int16_t top_left_x, top_left_y;
    uint16_t text_w, text_h;
    char buf[10] = "!***!";

    if (range < 15) return; // should never see valid reading < 15!

    constexpr int32_t depth = 4; // history depth
    static int32_t history[depth] = {0};
    static size_t index = 0;
    history[index] = range;
    static int32_t last_range = 0;
    Logger.debug("<%s> [%zu] %lu  (%lu) %lu\n", __func__, index, range, (index+(depth-1))%depth, last_range);
    constexpr int16_t moving_threshold = 10;
    if (abs(last_range - range) >= moving_threshold)
    {
        Logger.info("====================== Moving... %lu ========================\n", abs(last_range - range));
        // change is larger than noise, so definitely moving
        for (int32_t & i : history)
        {
            i = range;
        }
    }
    static int32_t last_sum = range * depth;
    int32_t sum = 0;
    for (size_t i = 0; i < depth; i++)
    {
        size_t history_i = (index + i) % depth;
        Logger.debug("<%s> %zu  H: %u\n", __func__, i, history_i);
        Logger.debug("<%s> [%zu(%zu)] %lu  ", __func__, i, history_i, history[history_i]);
        sum += history[history_i];
    }
    Logger.debug("\n");
    index = (index + 1) % depth; // move history along
    if (abs(last_sum - sum) <= depth / 2) sum = last_sum; // less than 1 digit
    auto avg_range = static_cast<int16_t>(static_cast<float>(sum) / depth /*+0.5*/);
    /* if (abs(avg_range - range) < 3) */
    display_range = avg_range;
    Logger.debug("<%s> [%zu]: R: %lu  A: %u  D: %lu  S: %lu  L: %lu\n",
        __func__, index, range, avg_range, display_range, sum, last_sum);
    last_range = range;
    last_sum = sum;
    if (display_range < 888) snprintf(buf, sizeof(buf) - 1, "%ld", display_range);
    canvas.setFont(&HackRegular26pt7b);
    canvas.getTextBounds(buf, 0, y, &top_left_x, &top_left_y, &text_w, &text_h);
    if (!once)
    {
        Logger.debug("<%s> %d  %d  %u  %u\n", __func__, top_left_x, top_left_y, text_w, text_h);
    }
    canvas.setCursor(static_cast<int16_t>((canvas.width() - text_w) / 2), y);
    canvas.setTextColor(ST77XX_GREEN);
    canvas.fillRect(0, static_cast<int16_t>(y - text_h), DISPLAY_WIDTH, static_cast<int16_t>(text_h + 1 + 1 + 1), ST77XX_BLACK /* ST77XX_RED */);
    canvas.println(buf);
    display.drawRGBBitmap(0, 0, canvas.getBuffer(), DISPLAY_WIDTH, DISPLAY_HEIGHT);
    if (!once)
    {
        Logger.debug("<%s> X: %u  Y:%u\n", __func__, canvas.getCursorX(), canvas.getCursorY());
    }
    once = true;
}


void loop()
{
#ifdef SQUATARA_CLI_COMMANDS_H
    if (cli_begun) cli.loop();
#endif  // SQUATARA_CLI_COMMANDS_H

    static unsigned long next_time_millis = 0;
    static unsigned long interval_ms = 100;
    static unsigned long next_battery_millis = 0;
    unsigned long now_millis = millis();

    if (preferences.getBool(BATTERY_KEY))
    {
        if (now_millis >= next_battery_millis)
        {
            constexpr unsigned long battery_interval_ms = 10000;

            next_battery_millis = now_millis + battery_interval_ms;
            battery_status_display(0 /* X position */, 42 /* Y position */);
        }
    }
    if (now_millis >= next_time_millis)
    {
        next_time_millis = now_millis + interval_ms;
        if (tof_configured)
        {
            // these should only be recalculated when preferences change (to avoid wasted cycles)
            auto buzzer_millis = static_cast<unsigned long>(preferences.getFloat(BUZZTIME_KEY) * 1000 /* ms in sec */);
            uint8_t hysteresis = preferences.getUChar(HYSTERESIS_KEY); // % hysteresis
            auto threshold_delta = static_cast<uint16_t>((hysteresis * tof_sensor.max_mm) / 100);

            unsigned long start_millis = millis();
            static int print_cnt = 10;
            int32_t range = tof_sensor.getRange(); // get sensor reading
            unsigned long measurement_ms = millis() - start_millis;
            static unsigned long buzzer_stop_millis = 0;
            static bool triggered = false;
            static bool buzzer_on = false;
            int32_t floor_range = floor_sensor.getRange();

            if (print_cnt-- > 0)
            {
                Logger.info("<%s> range: %lu mm -- measurement time: %lu ms\n", __func__, range, measurement_ms);
            }

            if (range >= 0)
            {
                uint16_t th = bar_graph(tof_sensor.max_mm, preferences.getUChar(THRESHOLD_KEY), range,
                                        0 /* X position */, 111 /* Y position */);
                if (range <= th && range > th / 10)
                {
                    buzzer.led_on();
                    if (!triggered)
                    {
                        triggered = true;
                        buzzer_stop_millis = millis() + buzzer_millis;
                        buzzer.on();
                        buzzer_on = true;
                    }
                }
                else
                {
                    buzzer.led_off();
                }
                if (triggered && (range > (th + threshold_delta)))
                {
                    triggered = false;
                }
            }
            if (buzzer_on && millis() >= buzzer_stop_millis)
            {
                buzzer.off();
                buzzer_on = false;
            }
            if (floor_range >= 0)
            {
                show_floor_range(floor_range, 100 /* Y position */);
            }
        }
    }
    delay(5);
}
