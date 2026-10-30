#ifndef TOF_VL53L4CD_H_
#define TOF_VL53L4CD_H_

#include <vl53l4cd_class.h>
#include <DebugLogger.h>

class ToF_Sensor : public VL53L4CD
{
    constexpr static uint8_t default_addr = 0x29;
    uint8_t addr;
    int xshut_pin;

public:
    const uint32_t max_mm = 1000;

    ToF_Sensor(TwoWire *i2c, int xshut_pin = -1, uint8_t addr = default_addr) : VL53L4CD(
        i2c, -1 /* do *not* configure XSHUT here! */)
    {
        this->addr = addr;
        this->xshut_pin = xshut_pin;
        if (this->xshut_pin != -1)
        {
            pinMode(xshut_pin, OUTPUT);
            digitalWrite(xshut_pin, LOW /* turn sensor off */);
            delay(10);
        }
    }

    ~ToF_Sensor() override = default;

    virtual bool configure()
    {
        VL53L4CD_ERROR ret = VL53L4CD_ERROR_NONE; // uint8_t

        if (this->xshut_pin != -1)
        {
            digitalWrite(xshut_pin, HIGH /* turn sensor on */);
            delay(10);
        }
        ret = this->InitSensor();
        if (ret == VL53L4CD_ERROR_NONE)
        {
            if (this->addr != default_addr)
            {
                this->VL53L4CD_SetI2CAddress(this->addr * 2); // alert! needs doubling!
            }
            // Program the highest possible TimingBudget, without enabling the
            // low power mode. This should give the best accuracy
            this->VL53L4CD_SetRangeTiming(200, 0);

            // Start Measurements
            this->VL53L4CD_StartRanging();
        }
        else
        {
            Logger.error("%s() VL53L4CD error: %u\n", __func__, ret);
            return false;
        }
        return true;
    }

    virtual int32_t getRange() // return range in mm (uint16_t) or bogus value (signed for errors?)
    {
        VL53L4CD_Result_t range_result;
        VL53L4CD_ERROR ret = VL53L4CD_ERROR_NONE;
        uint8_t status;
        uint8_t ready = 0;

        do
        {
            status = this->VL53L4CD_CheckForDataReady(&ready);
        } while (!ready);

        if (!status)
        {
            constexpr uint8_t range_valid = 0;
            [[maybe_unused]] constexpr uint8_t sigma_failed = 1;
            constexpr uint8_t signal_fail = 2;
            [[maybe_unused]] constexpr uint8_t min_range_fail = 3;
            constexpr uint8_t phase_fail = 4;
            constexpr uint8_t beyond_ambiguous_max = 7;
            this->VL53L4CD_ClearInterrupt(); // (Mandatory) Clear HW interrupt to restart measurements
            ret = this->VL53L4CD_GetResult(&range_result); // Read measured distance. range_status = 0 means valid data
            if (ret == VL53L4CD_ERROR_NONE)
            {
                char buf[100];

                snprintf(buf, sizeof(buf) - 1, "[0x%02x] Status = %3u, Distance = %5u mm, Signal = %6u kbps/spad\r\n",
                         this->addr,
                         range_result.range_status,
                         range_result.distance_mm,
                         range_result.signal_per_spad_kcps);
                if (range_result.range_status == range_valid)
                {
                    // apparently good range!
                    return range_result.distance_mm;
                }
                else
                {
                    if (range_result.range_status == signal_fail || range_result.range_status == phase_fail ||
                        range_result.range_status == beyond_ambiguous_max)
                    {
                        return static_cast<int16_t>(max_mm);
                    }
                    else
                    {
                        Logger.debug("[0x%02x] %s() VL53L4CD range_status: %u\n", this->addr, __func__,
                                     range_result.range_status);
                    }
                }
            }
            else
            {
                // ????? if (ret != signal_fail && ret != phase_fail && ret != beyond_ambiguous_max)
                {
                    Logger.error("[0x%02x] %s() VL53L4CD error: %u\n", this->addr, __func__, ret);
                }
            }
        }
        return -1; // TODO something else?
    }

    void printSWversion()
    {
        VL53L4CD_ERROR ret = VL53L4CD_ERROR_NONE;
        VL53L4CD_Version_t version;

        ret = this->VL53L4CD_GetSWVersion(&version);
        if (ret == VL53L4CD_ERROR_NONE)
        {
            Logger.debug("[0x%02x] VL53L4CD: %u.%u.%u  [%lu]\n", this->addr, version.major, version.minor,
                         version.build, version.revision);
        }
        else
        {
            Logger.error("[0x%02x] %s() VL53L4CD error: %u\n", this->addr, __func__, ret);
        }
    }
};

#endif // TOF_VL53L4CD_H_
