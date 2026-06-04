/*
 * garage_door_device.h — single-button garage state machine (logic only).
 *
 * 1 button. Each press:
 *   moving      -> STOP
 *   stopped mid -> move the OPPOSITE way to last time
 *   fully open  -> close ;  fully closed -> open
 * First press after boot opens (UP). No position sensor -> travel-time timeout.
 *
 * This class only tracks STATE. The output (buzzers now, relays later) is done
 * by the caller based on state() — see main.cpp.
 */
#pragma once

#include <driver/gpio.h>
#include <stdint.h>
#include "button.h"

#ifndef GARAGE_TRAVEL_MS
#define GARAGE_TRAVEL_MS 60000
#endif

// Minimum time the door must sit STOPPED/idle before a press can start a new
// move. Protects a real motor from rapid move->stop->reverse hammering.
// Stopping is never gated -- only starting a new move.
#ifndef GARAGE_MIN_DWELL_MS
#define GARAGE_MIN_DWELL_MS 700
#endif

enum class GarageState : uint8_t {
    STOPPED_MID = 0,
    MOVING_UP   = 1,
    MOVING_DOWN = 2,
    IDLE_OPEN   = 3,
    IDLE_CLOSED = 4,
};

class GarageDoorDevice {
public:
    GarageDoorDevice(gpio_num_t btn_pin);

    bool init();
    void update();              // poll button + run timeout. call every 10-50 ms

    void cmdToggle();           // the single-button action
    void cmdUp();               // explicit (for the future remote)
    void cmdDown();
    void stop();

    GarageState state() const { return _state; }
    const char* stateStr() const;
    uint32_t    elapsedMs() const;
    bool isMoving() const {
        return _state == GarageState::MOVING_UP ||
               _state == GarageState::MOVING_DOWN;
    }

private:
    Button      _btn;
    GarageState _state;
    uint64_t    _move_start_us;
    uint64_t    _rest_since_us;   // when we last entered a STOPPED/idle state
    bool        _last_dir_up;

    bool dwellElapsed() const;    // true once GARAGE_MIN_DWELL_MS has passed

    void enterStoppedMid();
    void enterMovingUp();
    void enterMovingDown();
    void enterIdleOpen();
    void enterIdleClosed();
};