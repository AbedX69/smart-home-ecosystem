#pragma once
#include <driver/gpio.h>
#include <esp_timer.h>  // Add angle brackets instead of quotes

class RotaryEncoder {
private:
    gpio_num_t pinCLK;           // Encoder Channel A
    gpio_num_t pinDT;            // Encoder Channel B
    gpio_num_t pinSW;            // Push button

    volatile int32_t position;   // Current position counter (volatile for ISR access)
    volatile uint8_t lastEncoded; // Last state of CLK+DT
    // Button state tracking
    bool lastButtonState;
    uint64_t lastButtonChangeTime;

    // Debouncing for rotation
    uint64_t lastRotationTime;
    bool halfStepMode;           // Half-step mode enabled

    static const uint32_t ROTATION_DEBOUNCE_US = 1000; // 1ms debounce
    static const uint32_t BUTTON_DEBOUNCE_US = 50000;  // 50ms debounce

    // ISR handler - must be static for C-style ISR
    static void isrHandler(void* arg);
    // Instance pointer for ISR (since static can't access members directly)
    static RotaryEncoder* instance;

public:
    RotaryEncoder(gpio_num_t clk, gpio_num_t dt, gpio_num_t sw, bool halfStep = false);
    ~RotaryEncoder();

    // Initialize GPIOs and attach interrupts
    void init();

    // Rotation methods
    int32_t getPosition() const;
    void resetPosition();
    void setPosition(int32_t pos);

    // Button methods
    bool isButtonPressed() const;
    bool wasButtonPressed();  // Edge detection with debouncing
};
