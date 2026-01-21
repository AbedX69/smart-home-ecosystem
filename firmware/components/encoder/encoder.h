#pragma once
#include "driver/gpio.h"

class RotaryEncoder {
private:
    gpio_num_t pinCLK;     // Encoder A
    gpio_num_t pinDT;      // Encoder B
    gpio_num_t pinSW;      // Button
    int position;
    int lastEncoded;
    bool lastButtonState;
    
public:
    RotaryEncoder(gpio_num_t clk, gpio_num_t dt, gpio_num_t sw);
    void init();
    void update();
    
    // Rotation
    int getPosition();
    void resetPosition();
    
    // Button
    bool isButtonPressed();
    bool wasButtonPressed();
};