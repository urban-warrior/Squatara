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

static void canvas_println_centered(const char *text, int16_t y)
{
    int16_t top_left_x, top_left_y;
    uint16_t text_w, text_h;

    // uses current Font and color
    canvas.getTextBounds(text, 0, y, &top_left_x, &top_left_y, &text_w, &text_h);
    canvas.setCursor(static_cast<int16_t>((canvas.width() - text_w) / 2), y);
    canvas.println(text);
    Logger.debug("<%s> %d  %d  %u  %u\n", __func__, top_left_x, top_left_y, text_w, text_h);
}

#include "deepsleep.h"

static void esp32_reset()
{
    canvas.fillScreen(ST77XX_BLACK);
    canvas.setFont(&FreeMono18pt7b);
    canvas.setTextColor(ST77XX_RED);
    canvas_println_centered(">> RESET <<", 75);
    display.drawRGBBitmap(0, 0, canvas.getBuffer(), DISPLAY_WIDTH, DISPLAY_HEIGHT);
    delay(1000);
    ESP.restart();
}

constexpr float minimum_allowed_vdc = 3.3 /* volts */;

static float estimate_soc()
{
    constexpr float max_lipo_vdc = 4.2;
    float estimated_soc = (lipo.cellVoltage() - minimum_allowed_vdc)/(max_lipo_vdc - minimum_allowed_vdc); // raw percentage SoC (linear)
    estimated_soc = estimated_soc * 100.0f; // convert to percent
    if (estimated_soc < 0.0) estimated_soc = 0.0;  // clamp negative to zero
    if (estimated_soc > 100.0) estimated_soc = 100.0; // clamp to max
    return estimated_soc;
}

// #include "buttons.h"
#include "cli_commands.h"

static void show_product_name(const char *name = "Squatara", int16_t y = 13)
{
    canvas.setFont(&FreeMono12pt7b);
    canvas.fillScreen(ST77XX_BLACK);
    canvas.setTextColor(ST77XX_WHITE);
    canvas_println_centered(name, y);
    display.drawRGBBitmap(0, 0, canvas.getBuffer(), DISPLAY_WIDTH, DISPLAY_HEIGHT);
    Logger.debug("<%s> X: %u  Y:%u\n", __func__, canvas.getCursorX(), canvas.getCursorY());
}

static void show_build(int16_t y = 37)
{
    canvas.setCursor(0, y);
    canvas.setTextColor(ST77XX_RED);
    canvas.setFont(&FreeMono9pt7b);
    canvas_println_centered(__DATE__ "@" __TIME__, canvas.getCursorY());
    display.drawRGBBitmap(0, 0, canvas.getBuffer(), DISPLAY_WIDTH, DISPLAY_HEIGHT);
    Logger.debug("<%s> X: %u  Y:%u\n", __func__, canvas.getCursorX(), canvas.getCursorY());
}

static void show_errors(bool tof, bool floor, int16_t y = 75)
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
    release_deep_sleep();
    powerswitch_init();
    bool lipo_started = lipo.begin();
    if (lipo_started) delay(500); // settle LIPO
    {
        if (!digitalRead(POWER_SWITCH) || (lipo_started && (lipo.cellVoltage() <= minimum_allowed_vdc)))
            initiate_deep_sleep(0 /* don't do anything except sleep */, nullptr /* no text -- no color */);
    }
    //release_deep_sleep();
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
    if (!lipo_started)
    {
        Logger.error("LIPO not found\n");
    }
    else
    {
        lipo.wake(); // disable hibernation
        Logger.debug("HibThr<0x0A>: %1.1f  ActThr<0x0B>: %1.1f\n",
            lipo.getHibernationThreshold(), lipo.getActivityThreshold());
        Logger.debug("Reset V: %f\n", lipo.getResetVoltage());
        float min_av;
        float max_av;
        lipo.getAlertVoltages(min_av, max_av);
        Logger.debug("Min Alert V: %f  Max Alert V: %f\n", min_av, max_av);
    }
    tof_configured = tof_sensor.configure();
    if (!tof_configured)
    {
        Logger.error("Distance sensor error!\n");
    }
    else
    {
        Logger.debug("max distance: %lu mm\n", tof_sensor.max_mm);
    }
    bool floor_sensor_configured = floor_sensor.configure();
    if (!floor_sensor_configured)
    {
        Logger.error("Floor sensor error!\n");
    }
    else
    {
        Logger.debug("Floor sensor found\n");
    }

#ifdef SQUATARA_BUTTONS_H
    check_buttons();
#endif // SQUATARA_BUTTONS_H
    show_product_name();
    show_build();
    show_errors(tof_configured, floor_sensor_configured);
    digitalWrite(TFT_BACKLITE, HIGH);
    Logger.debug("POWER_SWITCH GPIO valid wakeup source: %u\n", esp_sleep_is_valid_wakeup_gpio(POWER_SWITCH)); // GPIO 5 is OK!
#ifdef SQUATARA_CLI_COMMANDS_H
    cli_enable_detect();
    if (cli_begun)
    {
        show_wakeup_cause();
        Serial.println("\nCLI ready -- type 'help' for commands");
        cli.refreshPrompt();
    }
#endif  // SQUATARA_CLI_COMMANDS_H
    timeout_start_screen(3000);
    buzzer.on();
    delay(250 /* ms */);
    buzzer.off();
} // end setup()

static void draw_battery_element(int16_t x, int16_t y, uint16_t central_color, uint16_t bg_color, int16_t w, int16_t h)
{
    constexpr int16_t x_border_width = 2;
    constexpr int16_t y_border_width = 6;
    auto central_x = static_cast<int16_t>(x + x_border_width);
    auto central_y = static_cast<int16_t>(y + y_border_width);
    auto central_w = static_cast<int16_t>(w - 2*x_border_width);
    auto central_h = static_cast<int16_t>(h - 2*y_border_width);
    canvas.fillRect(x, y, w, h, bg_color);
    canvas.fillRect(central_x, central_y, central_w, central_h, ST77XX_BLACK);
    canvas.fillRect(central_x, central_y, central_w, central_h, central_color);
}

static void show_battery_soc(float cell_soc, int16_t x, int16_t y, uint16_t roc_color)
{
    float cell_roc = lipo.chargeRate();
    char buf[40];
    uint16_t roc_display_color = (cell_roc <= 0.0) ? roc_color : ST77XX_GREEN;
    canvas.setTextColor(roc_display_color);
    if (show_battery_soc_percent)
    {
        snprintf(buf, sizeof(buf) - 1, "%3.0f%%", cell_soc);
        canvas.setFont(&DejaVu_Sans_Mono_14);
        canvas.setCursor(x, y); // move to value field
        canvas.print(buf);
    }
    else
    {
        constexpr int16_t spot_size = 6;
        constexpr int16_t x_spot_offset = 18;
        constexpr int16_t y_spot_offset = -7;
        uint16_t roc_spot_color = (cell_roc <= 0.0) ? ST77XX_BLACK : ST77XX_GREEN;
        canvas.fillRect(static_cast<int16_t>(x + x_spot_offset), static_cast<int16_t>(y + y_spot_offset),
            spot_size, spot_size, roc_spot_color);
    }

}

static void battery_status_display(int16_t x, int16_t y)
{
    float cell_soc = estimate_soc();
    static float last_cell_soc = 0.0;
    if (cell_soc >= 99.0) cell_soc = 100.0; // clamp to max for display
    constexpr float warning_soc = 2.0 /* percent */;
    constexpr uint16_t bar_graph_height = 16;
    constexpr int16_t bar_graph_margin = 8;
    constexpr uint16_t bar_graph_width = 200;
    constexpr uint16_t BAR_GRAPH_COLOR = ST77XX_BLUE;
    constexpr int16_t y_offset = 20;

    if (fabs(cell_soc - last_cell_soc) < 2.0) // 2% hysteresis band
    {
        cell_soc = last_cell_soc;
    }
    else
    {
        last_cell_soc = cell_soc;
    }
    y = static_cast<int16_t>(y - y_offset);
    auto y_rect = static_cast<int16_t>(y + bar_graph_margin);
    uint16_t battery_status_color = (cell_soc >= warning_soc) ? BAR_GRAPH_COLOR : ST77XX_RED;
    canvas.fillRect(x, y_rect, DISPLAY_WIDTH, bar_graph_height, ST77XX_BLACK /* ST77XX_RED */);
    canvas.drawRect(x, y_rect, bar_graph_width, bar_graph_height, battery_status_color);
    int soc_percent = static_cast<int>(cell_soc);
    int16_t element_x = x;
    while (soc_percent > 0)
    {
        draw_battery_element(element_x, y_rect, ST77XX_GREEN, battery_status_color, bar_graph_width/10, bar_graph_height);
        element_x += bar_graph_width/10;
        soc_percent -= 10;
    }
    show_battery_soc(cell_soc, static_cast<int16_t>(x + bar_graph_width + 4), static_cast<int16_t>(y + 20), BAR_GRAPH_COLOR);
    canvas.setCursor(x, y); //restore cursor position
    display.drawRGBBitmap(0, 0, canvas.getBuffer(), DISPLAY_WIDTH, DISPLAY_HEIGHT);
}

static void show_battery_v_and_roc(int16_t y)
{
    char buf[20];
    int16_t x_rect;
    int16_t y_rect;
    uint16_t w;
    uint16_t h;

    canvas.setTextColor(ST77XX_YELLOW);
    canvas.setFont(&FreeMono9pt7b);
    snprintf(buf, sizeof(buf) - 1, "%3.2fV", lipo.cellVoltage());
    canvas.setCursor(1, y);
    canvas.getTextBounds(buf, canvas.getCursorX(), canvas.getCursorY(), &x_rect, &y_rect, &w, &h);
    auto v_rect_tlx = static_cast<int16_t>(x_rect - 1);
    auto v_rect_tly = static_cast<int16_t>(y_rect - 1);
    auto v_rect_w = static_cast<int16_t>(w + 2);
    auto v_rect_h = static_cast<int16_t>(h + 2);
    canvas.fillRect(v_rect_tlx, v_rect_tly, v_rect_w, v_rect_h, ST77XX_BLACK);
    canvas.print(buf);
    float roc = lipo.chargeRate();
    if (roc != 0.0)  roc = static_cast<float>((roc >= 0.0) ? roc + 0.05 : roc - 0.05);
    snprintf(buf, sizeof(buf) - 1, "%4.1f", fabs(roc));
    canvas.getTextBounds("-99.9", 0, y, &x_rect, &y_rect, &w, &h);
    auto r_rect_w = static_cast<int16_t>(w - x_rect - 3);
    auto r_rect_tlx = static_cast<int16_t>(canvas.width() - r_rect_w);
    auto r_rect_tly = static_cast<int16_t>(y_rect - 1);
    auto r_rect_h = static_cast<int16_t>(h + 2);
    canvas.fillRect(r_rect_tlx, r_rect_tly, r_rect_w, r_rect_h, ST77XX_BLACK);
    canvas.setCursor(static_cast<int16_t>(r_rect_tlx + 5), y);
    //canvas.setTextColor(roc >= 0 ? ST77XX_CYAN : ST77XX_RED);
    canvas.setTextColor(roc >= 0 ? ST77XX_WHITE : ST77XX_RED);
    canvas.print(buf);
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
    //int32_t display_range;
    int16_t top_left_x;
    int16_t top_left_y;
    uint16_t text_w;
    uint16_t text_h;
    constexpr const char *max_field = "!***!";
    char buf[20];
    strncpy(buf, max_field, sizeof(buf));

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
        Logger.debug("====================== Moving... %lu ========================\n", abs(last_range - range));
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
        Logger.debug("<%s> [%zu(%zu)] %lu  ", __func__, i, history_i, history[history_i]);
        sum += history[history_i];
    }
    Logger.debug("\n");
    index = (index + 1) % depth; // move history along
    if (abs(last_sum - sum) <= depth / 2) sum = last_sum; // less than 1 digit
    auto avg_range = static_cast<int32_t>(static_cast<float>(sum) / depth /*+0.5*/);
    /* if (abs(avg_range - range) < 3) */
    auto display_range = avg_range;
    Logger.debug("<%s> [%zu]: R: %lu  A: %lu  D: %lu  S: %lu  L: %lu\n",
        __func__, index, range, avg_range, display_range, sum, last_sum);
    last_range = range;
    last_sum = sum;
    if (display_range < 888) snprintf(buf, sizeof(buf) - 1, "%ld", display_range);
    canvas.setFont(&HackRegular26pt7b);
    // not a duplicate of canvas_println_centered() because does not write entire line
    canvas.getTextBounds(max_field, 0, 0, &top_left_x, &top_left_y, &text_w, &text_h);
    if (!once) Logger.debug("<%s> %d  %d  %u  %u\n", __func__, top_left_x, top_left_y, text_w, text_h);
    canvas.setTextColor(ST77XX_GREEN);
    auto rect_tlx = static_cast<int16_t>((canvas.width() - text_w + top_left_x) / 2);
    auto rect_tly = static_cast<int16_t>(y - text_h + 1);
    auto rect_w = static_cast<int16_t>(text_w + 1);
    auto rect_h = static_cast<int16_t>(text_h + 3);
    if (!once) Logger.debug("<%s> rect_tlx: %u  rect_tly: %u  rect_w: %u  rect_h: %u\n",
        __func__, rect_tlx, rect_tly, rect_w, rect_h);
    canvas.fillRect(rect_tlx, rect_tly, rect_w, rect_h, ST77XX_BLACK);
    canvas.getTextBounds(buf, 0, 0, &top_left_x, &top_left_y, &text_w, &text_h);
    canvas.setCursor(static_cast<int16_t>((canvas.width() - text_w - top_left_x) / 2), y);
    canvas.println(buf);
    display.drawRGBBitmap(0, 0, canvas.getBuffer(), DISPLAY_WIDTH, DISPLAY_HEIGHT);
    if (!once) Logger.debug("<%s> X: %u  Y:%u\n", __func__, canvas.getCursorX(), canvas.getCursorY());
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

    if (!digitalRead(POWER_SWITCH)) // watch for power switch
    {
        delay(50); // wait some time for debounce
        if (!digitalRead(POWER_SWITCH)) initiate_deep_sleep(1000 /* ms */, "Bye!", ST77XX_GREEN); // won't return!
    }

    if (preferences.getBool(BATTERY_KEY))
    {
        if (now_millis >= next_battery_millis)
        {
            constexpr unsigned long battery_interval_ms = 2000;

            next_battery_millis = now_millis + battery_interval_ms;
            battery_status_display(0 /* X position */, 42 /* Y position */);
            if (show_battery) show_battery_v_and_roc(75 /* Y position */);
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
                Logger.debug("<%s> range: %lu mm -- measurement time: %lu ms\n", __func__, range, measurement_ms);
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
