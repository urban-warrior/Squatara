#ifndef BUZZER_H_
#define BUZZER_H_

class Buzzer
{
    uint8_t pin = 255;
    uint8_t led = 255;

public:
    Buzzer(uint8_t pin_, uint8_t led_ = 255)
    {
        pin = pin_;
        led = led_;

        pinMode(pin, OUTPUT);
        digitalWrite(pin, LOW);
        if (led != 255) pinMode(led, OUTPUT);
        if (led != 255) digitalWrite(led, LOW);
    }

    virtual ~Buzzer()
    {
    }

    virtual void on()
    {
        digitalWrite(pin, HIGH);
    }

    virtual void off()
    {
        digitalWrite(pin, LOW);
    }

    virtual void led_on()
    {
        if (led != 255) digitalWrite(led, HIGH);
    }

    virtual void led_off()
    {
        if (led != 255) digitalWrite(led, LOW);
    }
};

#endif // BUZZER_H_
