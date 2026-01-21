#include "encoder.h"

RotaryEncoder::RotaryEncoder(gpio_num_t clk, gpio_num_t dt, gpio_num_t sw) 
    : pinCLK(clk), pinDT(dt), pinSW(sw), position(0), lastEncoded(0), lastButtonState(false) {}

void RotaryEncoder::init() {
    gpio_set_direction(pinCLK, GPIO_MODE_INPUT);
    gpio_set_direction(pinDT, GPIO_MODE_INPUT);
    gpio_set_pull_mode(pinCLK, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(pinDT, GPIO_PULLUP_ONLY);
    
    gpio_set_direction(pinSW, GPIO_MODE_INPUT);
    gpio_set_pull_mode(pinSW, GPIO_PULLUP_ONLY);
}

void RotaryEncoder::update() {
    int MSB = gpio_get_level(pinCLK);
    int LSB = gpio_get_level(pinDT);
    int encoded = (MSB << 1) | LSB;
    int sum = (lastEncoded << 2) | encoded;
    
    if(sum == 0b1101 || sum == 0b0100 || sum == 0b0010 || sum == 0b1011) position++;
    if(sum == 0b1110 || sum == 0b0111 || sum == 0b0001 || sum == 0b1000) position--;
    
    lastEncoded = encoded;
}

int RotaryEncoder::getPosition() {
    return position;
}

void RotaryEncoder::resetPosition() {
    position = 0;
}

bool RotaryEncoder::isButtonPressed() {
    return gpio_get_level(pinSW) == 0;
}

bool RotaryEncoder::wasButtonPressed() {
    bool current = isButtonPressed();
    bool pressed = (current && !lastButtonState);
    lastButtonState = current;
    return pressed;
}