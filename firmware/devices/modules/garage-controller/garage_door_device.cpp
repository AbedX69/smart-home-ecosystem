/*
 * =============================================================================
 * FILE:        garage_door_device.cpp
 * AUTHOR:      AbedX69
 * CREATED:     2026-05-05
 * VERSION:     1.0.0
 * =============================================================================
 */

#include "garage_door_device.h"

#include <esp_log.h>
#include <esp_timer.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "GarageDoor";


GarageDoorDevice::GarageDoorDevice(gpio_num_t up_relay_pin,
                                   gpio_num_t down_relay_pin,
                                   gpio_num_t up_btn_pin,
                                   gpio_num_t down_btn_pin,
                                   bool relay_active_low)
    : _up_relay(up_relay_pin,   relay_active_low),
      _down_relay(down_relay_pin, relay_active_low),
      _up_btn(up_btn_pin),
      _down_btn(down_btn_pin),
      _state(GarageState::STOPPED_MID),
      _move_start_us(0)
{
}

GarageDoorDevice::~GarageDoorDevice() {
    killBothRelays();
}

bool GarageDoorDevice::init() {
    ESP_LOGI(TAG, "Initializing garage door device");

    _up_relay.init();
    _down_relay.init();

    _up_btn.init();
    _down_btn.init();

    killBothRelays();

    _state = GarageState::STOPPED_MID;
    _move_start_us = 0;

    ESP_LOGI(TAG, "Boot state: STOPPED_MID");
    return true;
}


void GarageDoorDevice::update() {
    _up_btn.update();
    _down_btn.update();

    bool up_pressed   = _up_btn.wasPressed();
    bool down_pressed = _down_btn.wasPressed();

    if (up_pressed)   cmdUp();
    if (down_pressed) cmdDown();

    if (isMoving()) {
        uint64_t now = esp_timer_get_time();
        uint32_t elapsed_ms = (uint32_t)((now - _move_start_us) / 1000ULL);

        if (elapsed_ms >= GARAGE_TRAVEL_MS) {
            if (_state == GarageState::MOVING_UP) enterIdleOpen();
            else                                  enterIdleClosed();
        }
    }
}


void GarageDoorDevice::cmdUp() {
    switch (_state) {
        case GarageState::STOPPED_MID:
        case GarageState::IDLE_CLOSED:
            ESP_LOGI(TAG, "UP cmd: starting open");
            enterMovingUp();
            break;
        case GarageState::MOVING_UP:
        case GarageState::MOVING_DOWN:
            ESP_LOGI(TAG, "UP cmd while moving: STOP");
            enterStoppedMid();
            break;
        case GarageState::IDLE_OPEN:
            ESP_LOGI(TAG, "UP cmd ignored: already open");
            break;
    }
}

void GarageDoorDevice::cmdDown() {
    switch (_state) {
        case GarageState::STOPPED_MID:
        case GarageState::IDLE_OPEN:
            ESP_LOGI(TAG, "DOWN cmd: starting close");
            enterMovingDown();
            break;
        case GarageState::MOVING_UP:
        case GarageState::MOVING_DOWN:
            ESP_LOGI(TAG, "DOWN cmd while moving: STOP");
            enterStoppedMid();
            break;
        case GarageState::IDLE_CLOSED:
            ESP_LOGI(TAG, "DOWN cmd ignored: already closed");
            break;
    }
}

void GarageDoorDevice::stop() {
    if (isMoving()) {
        ESP_LOGI(TAG, "stop() called");
        enterStoppedMid();
    }
}


void GarageDoorDevice::enterStoppedMid() {
    killBothRelays();
    _state = GarageState::STOPPED_MID;
    _move_start_us = 0;
    ESP_LOGI(TAG, "→ STOPPED_MID");
}

void GarageDoorDevice::enterMovingUp() {
    engage(true);
    _state = GarageState::MOVING_UP;
    _move_start_us = esp_timer_get_time();
    ESP_LOGI(TAG, "→ MOVING_UP (timer %u ms)", (unsigned)GARAGE_TRAVEL_MS);
}

void GarageDoorDevice::enterMovingDown() {
    engage(false);
    _state = GarageState::MOVING_DOWN;
    _move_start_us = esp_timer_get_time();
    ESP_LOGI(TAG, "→ MOVING_DOWN (timer %u ms)", (unsigned)GARAGE_TRAVEL_MS);
}

void GarageDoorDevice::enterIdleOpen() {
    killBothRelays();
    _state = GarageState::IDLE_OPEN;
    _move_start_us = 0;
    ESP_LOGI(TAG, "→ IDLE_OPEN (travel timeout)");
}

void GarageDoorDevice::enterIdleClosed() {
    killBothRelays();
    _state = GarageState::IDLE_CLOSED;
    _move_start_us = 0;
    ESP_LOGI(TAG, "→ IDLE_CLOSED (travel timeout)");
}


void GarageDoorDevice::killBothRelays() {
    _up_relay.off();
    _down_relay.off();
}

void GarageDoorDevice::engage(bool up) {
    /*
     * Hard interlock:
     *   1. Both relays OFF.
     *   2. Block for GARAGE_DIRECTION_DEADTIME_MS so the motor never sees
     *      opposite phases at the same instant.
     *   3. Energize only the requested relay.
     */
    killBothRelays();

    if (GARAGE_DIRECTION_DEADTIME_MS > 0) {
        vTaskDelay(pdMS_TO_TICKS(GARAGE_DIRECTION_DEADTIME_MS));
    }

    if (up) _up_relay.on();
    else    _down_relay.on();
}


const char* GarageDoorDevice::stateStr() const {
    switch (_state) {
        case GarageState::STOPPED_MID: return "STOPPED_MID";
        case GarageState::MOVING_UP:   return "MOVING_UP";
        case GarageState::MOVING_DOWN: return "MOVING_DOWN";
        case GarageState::IDLE_OPEN:   return "IDLE_OPEN";
        case GarageState::IDLE_CLOSED: return "IDLE_CLOSED";
    }
    return "?";
}

uint32_t GarageDoorDevice::elapsedMs() const {
    if (!isMoving()) return 0;
    uint64_t now = esp_timer_get_time();
    return (uint32_t)((now - _move_start_us) / 1000ULL);
}
