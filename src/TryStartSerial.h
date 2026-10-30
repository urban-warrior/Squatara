#ifndef TRYSTARTSERIAL_H_
#define TRYSTARTSERIAL_H_

#ifndef LED_BUILTIN
#define LED_BUILTIN -1
#endif  // !LED_BUILTIN

bool inline try_start_serial(const char *start_msg, int wait_secs=2)
{
    constexpr int FLASHES=3;

    if constexpr (LED_BUILTIN >= 0)pinMode(LED_BUILTIN, OUTPUT);
    for (int cnt=0; cnt < FLASHES; cnt++)
    {
        if constexpr (LED_BUILTIN >= 0)digitalWrite(LED_BUILTIN, HIGH);
        delay(100);
        if constexpr (LED_BUILTIN >= 0)digitalWrite(LED_BUILTIN, LOW);
        delay(100);
    }
    int count=0;

    Serial.begin(115200);
    while (!Serial && count < wait_secs)
    {
        ++count;
        delay(1000);
    }
    delay(250); // wait just a bit to make sure everything is settled
    Serial.println(start_msg);

    return true;
}

#endif  // TRYSTARTSERIAL_H_
