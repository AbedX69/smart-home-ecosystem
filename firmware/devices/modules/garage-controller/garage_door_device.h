/*
 * =============================================================================
 * FILE:        garage_door_device.h
 * AUTHOR:      AbedX69
 * CREATED:     2026-05-05
 * VERSION:     1.0.0
 * LICENSE:     MIT
 * PLATFORM:    All ESP32 variants (ESP-IDF v5.x)
 * =============================================================================
 *
 * Wired Garage Door Device — 2 relays (UP / DOWN) + 2 buttons.
 *
 * This is the DEVICE side: it owns the physical relays that drive the motor
 * and (for now) the local manual buttons. Eventually buttons will move to a
 * separate ESP32 (see garage_door_remote) and reach this device over a
 * wireless transport.
 *
 * NO position sensors. State is inferred from a travel-time timeout.
 *
 * STATE MACHINE
 * =============
 *
 *                       ┌──────────────┐
 *                       │   STOPPED    │ ← boot state (safest)
 *                       │     MID      │
 *                       └──┬────────┬──┘
 *                  UP press│        │DOWN press
 *                          ▼        ▼
 *                  ┌─────────────┐  ┌─────────────┐
 *                  │  MOVING_UP  │  │ MOVING_DOWN │
 *                  │ (UP relay)  │  │(DOWN relay) │
 *                  └──┬───────┬──┘  └──┬───────┬──┘
 *           any press │       │timeout │       │ any press
 *                     │       ▼        ▼       │
 *                     │  ┌─────────┐ ┌─────────┐
 *                     │  │  IDLE   │ │  IDLE   │
 *                     │  │  OPEN   │ │ CLOSED  │
 *                     │  └────┬────┘ └────┬────┘
 *                     │  DOWN │           │ UP
 *                     │       │           │
 *                     ▼       ▼           ▼
 *                  STOPPED_MID  (resumes movement)
 *
 * RULES
 * =====
 *   - In IDLE_OPEN: UP press is ignored.
 *   - In IDLE_CLOSED: DOWN press is ignored.
 *   - In MOVING_*: ANY button press stops and goes to STOPPED_MID.
 *   - In STOPPED_MID: UP starts opening, DOWN starts closing.
 *   - Hard interlock: UP and DOWN relays can NEVER be on simultaneously.
 *     Direction change inserts a brief dead-time before energizing the
 *     opposite relay.
 *   - Every move gets a fresh travel-time budget.
 *
 * USAGE
 * =====
 *
 *     GarageDoorDevice door(GPIO_NUM_4,    // up relay pin
 *                           GPIO_NUM_5,    // down relay pin
 *                           GPIO_NUM_18,   // up button pin
 *                           GPIO_NUM_19);  // down button pin
 *     door.init();
 *
 *     while (true) {
 *         door.update();
 *         vTaskDelay(pdMS_TO_TICKS(20));
 *     }
 *
 * =============================================================================
 */

#pragma once

#include <driver/gpio.h>
#include <stdint.h>

#include "relay.h"
#include "button.h"


/* ─── Tunables ───────────────────────────────────────────────────────────── */

#ifndef GARAGE_TRAVEL_MS
#define GARAGE_TRAVEL_MS              60000
#endif

#ifndef GARAGE_DIRECTION_DEADTIME_MS
#define GARAGE_DIRECTION_DEADTIME_MS  500
#endif


/* ─── States ─────────────────────────────────────────────────────────────── */

enum class GarageState : uint8_t {
    STOPPED_MID  = 0,
    MOVING_UP    = 1,
    MOVING_DOWN  = 2,
    IDLE_OPEN    = 3,
    IDLE_CLOSED  = 4,
};


/* ─── Class ──────────────────────────────────────────────────────────────── */

class GarageDoorDevice {
public:

    GarageDoorDevice(gpio_num_t up_relay_pin,
                     gpio_num_t down_relay_pin,
                     gpio_num_t up_btn_pin,
                     gpio_num_t down_btn_pin,
                     bool relay_active_low = true);

    ~GarageDoorDevice();

    /** Initialize all GPIO. Boot state is STOPPED_MID. */
    bool init();

    /** Poll buttons + run state machine. Call every 10–50 ms. */
    void update();

    /* ─── Programmatic commands (same effect as button press) ──────── */

    void cmdUp();
    void cmdDown();
    void stop();

    /* ─── Query ────────────────────────────────────────────────────── */

    GarageState state() const { return _state; }
    const char* stateStr() const;
    uint32_t    elapsedMs() const;

    bool isMoving() const {
        return _state == GarageState::MOVING_UP ||
               _state == GarageState::MOVING_DOWN;
    }

private:

    Relay   _up_relay;
    Relay   _down_relay;
    Button  _up_btn;
    Button  _down_btn;

    GarageState _state;
    uint64_t    _move_start_us;

    void enterStoppedMid();
    void enterMovingUp();
    void enterMovingDown();
    void enterIdleOpen();
    void enterIdleClosed();

    void engage(bool up);
    void killBothRelays();
};
