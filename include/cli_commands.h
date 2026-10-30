#ifndef SQUATARA_CLI_COMMANDS_H
#define SQUATARA_CLI_COMMANDS_H

#include <cli/cli.hpp>
#include "configuration.h"
#include "i2c-scan.h"
#include <charconv>

static bool cli_begun=false;
Cli inline cli;

static void cli_enable_detect()
{
    constexpr const char *NVS_PARTITION_NAME = "nvs";
    // Find the partition struct pointer by its label
    const esp_partition_t *target_partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA,
        ESP_PARTITION_SUBTYPE_DATA_NVS,
        NVS_PARTITION_NAME
    );

    if (target_partition != nullptr)
    {
        Logger.debug("Found partition: %s\n", target_partition->label);
        uint8_t buffer[16];
        // Read 64 bytes starting from the beginning (offset 0) of that partition
        esp_err_t err = esp_partition_read(target_partition, 0x4ff0, buffer, sizeof(buffer));
        if (err == ESP_OK)
        {
            Logger.debug("%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
                buffer[0], buffer[1], buffer[2], buffer[3], buffer[4], buffer[5], buffer[6], buffer[7],
                buffer[8], buffer[9], buffer[10], buffer[11], buffer[12], buffer[13], buffer[14], buffer[15]);
            uint32_t sentinel = buffer[15] << 24 | buffer[14] << 16 | buffer[13] << 8 | buffer[12];
            // Data is now populated in buffer as raw binary
            Logger.debug("NVS: %p\n", (void *)(target_partition->address));
            if (sentinel == 0xdeadbeef)
            {
                cli.begin();
                cli_begun = true;
            }
        }
        else
        {
            Logger.debug("esp_partition_read() error: %u\n", err);
        }
    }

}

CLI_COMMAND(help)
{
    ioStream.println("Available commands:");

    cliCmd_t* cmds = CliCommand::getTable();
    size_t count = CliCommand::getCmdCnt();

    for (size_t i = 0; i < count; i++) {
        ioStream.printf("  %s\n", cmds[i].name);
    }
    return 0;
}

CLI_COMMAND(ver)
{
    ioStream.printf("Build: %s", BUILD_STAMP);  // BUILD_STAMP has \n
    return 0;
}

CLI_COMMAND(version)
{
    return CliCommand::exec(ioStream, "ver", argv, argc);
}

CLI_COMMAND(log)
{
    if (argc != 0)
    {
        Logger.info("log argv[0]: '%s'\n", argv[0]);
        // create some aliases
        if (strcasecmp(argv[0], "none") == 0) argv[0] = "n";
        if (strcasecmp(argv[0], "error") == 0) argv[0] = "e";
        if (strcasecmp(argv[0], "warn") == 0) argv[0] = "w";
        if (strcasecmp(argv[0], "info") == 0) argv[0] = "i";
        if (strcasecmp(argv[0], "debug") == 0) argv[0] = "d";
        auto first_c = std::tolower(argv[0][0]); // allow for uppercase alias
        if ((strlen(argv[0]) == 1) && ((first_c == 'n') || (first_c == 'e') || (first_c == 'w')
            || (first_c == 'i') || (first_c == 'd')))
        {
            LogLevel level=Logger.getLevel();
            switch (first_c)
            {
                case 'n':
                    level = LogLevel::None;
                    break;
                case 'e':
                    level = LogLevel::Error;
                    break;
                case 'w':
                    level = LogLevel::Warn;
                    break;
                case 'i':
                    level = LogLevel::Info;
                    break;
                case 'd':
                    level = LogLevel::Debug;
                    break;
                default:
                    break;
            }
            preferences.putUChar(LOG_LEVEL_KEY, static_cast<uint8_t>(level));
            Logger.setLevel(level);
        }
    }
    char const * level_name = "Unknown";
    switch (Logger.getLevel())
    {
        case LogLevel::None:
            level_name = "None";
            break;
        case LogLevel::Error:
            level_name = "Error";
            break;
        case LogLevel::Warn:
            level_name = "Warn";
            break;
        case LogLevel::Info:
            level_name = "Info";
            break;
        case LogLevel::Debug:
            level_name = "Debug";
            break;
        default:
            break;
    }
    ioStream.printf("LogLevel: %s  [%u]\n", level_name, (uint8_t)Logger.getLevel());
    return 0;
}

CLI_COMMAND(reset)
{
    esp32_reset();
    return 0;  // will not be reached!
}

CLI_COMMAND(i2c)
{
    scan_i2c(ioStream);
    return 0;
}

CLI_COMMAND(soc)
{
    float cell_soc = lipo.cellPercent();
    float cell_roc = lipo.chargeRate();
    float cell_vdc = lipo.cellVoltage();
    uint32_t uptime = millis()/1000;

    ioStream.printf("{\"timestamp\":\"%lu\", \"SoC\":\"%0.0f\", \"Vdc\":\"%.2f\", \"RoC\":\"%.2f\"}\r\n",
                    uptime, cell_soc, cell_vdc, cell_roc);
    return 0;
}

CLI_COMMAND(uptime)
{
    unsigned long uptime = millis();
    unsigned long secs = uptime / 1000;
    unsigned long mins = secs / 60;
    unsigned long hours = mins / 60;
    unsigned long days = hours / 24;

    Logger.debug("millis(): %lu  days: %lu  hours: %lu  mins: %lu  secs: %lu\n",
        uptime, days, hours, mins, secs);
    ioStream.printf("uptime: %lu-%02lu:%02lu:%02lu\n", days, hours % 24, mins % 60, secs % 60);
    return 0;
}

CLI_COMMAND(threshold)
{
    if (argc != 0)
    {
        u_char new_value;
        const char *next_chr = &argv[0][strlen(argv[0])];
        Logger.debug("'%s'  [%u]  %p  %p\n", argv[0], strlen(argv[0]), argv[0], next_chr);
        auto [ptr, ec] = std::from_chars(argv[0], next_chr, new_value);
        if (ec == std::errc())
        {
            Logger.debug("ptr: %p\n", ptr);
            if (ptr != next_chr)
            {
                ioStream.printf("%d extraneous character(s) '%s'\n", next_chr-ptr, argv[0]);
            }
            else
            {
                constexpr u_char low_limit = 25;
                constexpr u_char high_limit = 99;
                Logger.debug("raw new_value: %u\n", new_value);
                if ((new_value >= low_limit) && (new_value <= high_limit))
                {
                    preferences.putUChar(THRESHOLD_KEY, new_value);
                }
                else
                {
                    ioStream.printf("%u not in range [%u, %u]\n", new_value, low_limit, high_limit);
                }
            }
        }
        else
        {
            ioStream.printf("Conversion error: %u: '%s'\n", (unsigned int)ec, argv[0]);
        }
    }
    ioStream.printf("threshold: %u\n", preferences.getUChar(THRESHOLD_KEY));
    return 0;
}

CLI_COMMAND(hysteresis)
{
    if (argc != 0)
    {
        u_char new_value;
        const char *next_chr = &argv[0][strlen(argv[0])];
        Logger.debug("'%s'  [%u]  %p  %p\n", argv[0], strlen(argv[0]), argv[0], next_chr);
        auto [ptr, ec] = std::from_chars(argv[0], next_chr, new_value);
        if (ec == std::errc())
        {
            Logger.debug("ptr: %p\n", ptr);
            if (ptr != next_chr)
            {
                ioStream.printf("%d extraneous character(s) '%s'\n", next_chr-ptr, argv[0]);
            }
            else
            {
                constexpr u_char low_limit = 0;
                constexpr u_char high_limit = 25;
                Logger.debug("raw new_value: %u\n", new_value);
                if ((new_value >= low_limit) && (new_value <= high_limit))
                {
                    preferences.putUChar(HYSTERESIS_KEY, new_value);
                }
                else
                {
                    ioStream.printf("%u not in range [%u, %u]\n", new_value, low_limit, high_limit);
                }
            }
        }
        else
        {
            ioStream.printf("Conversion error: %u: '%s'\n", (unsigned int)ec, argv[0]);
        }
    }
    ioStream.printf("hysteresis: %u%%\n", preferences.getUChar(HYSTERESIS_KEY));
    return 0;
}

CLI_COMMAND(buzztime)
{
    if (argc != 0)
    {
        float new_value;
        const char *next_chr = &argv[0][strlen(argv[0])];
        Logger.debug("'%s'  [%u]  %p  %p\n", argv[0], strlen(argv[0]), argv[0], next_chr);
        auto [ptr, ec] = std::from_chars(argv[0], next_chr, new_value);
        if (ec == std::errc())
        {
            Logger.debug("ptr: %p\n", ptr);
            if (ptr != next_chr)
            {
                ioStream.printf("%d extraneous character(s) '%s'\n", next_chr-ptr, argv[0]);
            }
            else
            {
                constexpr float low_limit = 0.1;
                constexpr float high_limit = 10.0;
                constexpr int precision = 1;
                Logger.debug("raw new_value: %f\n", new_value);
                if ((new_value >= low_limit) && (new_value <= high_limit))
                {
                    auto scale = static_cast<float>(std::pow(10.0, precision));
                    float clipped_value=std::round(new_value * scale)/scale;

                    preferences.putFloat(BUZZTIME_KEY, clipped_value);
                }
                else
                {
                    ioStream.printf("%f not in range [%0.*f, %0.*f]\n", new_value, precision,
                                    low_limit, precision, high_limit);
                }
            }
        }
        else
        {
            ioStream.printf("Conversion error: %u: '%s'\n", (unsigned int)ec, argv[0]);
        }
    }
    ioStream.printf("buzztime:  %0.1f s\n", preferences.getFloat(BUZZTIME_KEY, 2.0));
    return 0;
}

#endif //SQUATARA_CLI_COMMANDS_H
