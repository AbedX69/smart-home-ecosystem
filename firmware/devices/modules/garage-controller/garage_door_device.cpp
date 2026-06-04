/*
 * garage_door_device.cpp — single-button garage state machine (logic only).
 */
#include "garage_door_device.h"

#include <esp_log.h>
#include <esp_timer.h>

static const char* TAG = "GarageDoor";

GarageDoorDevice::GarageDoorDevice(gpio_num_t btn_pin)
    : _btn(btn_pin),
      _state(GarageState::STOPPED_MID),
      _move_start_us(0),
      _rest_since_us(0),
      _last_dir_up(false)   /* false => first press moves UP (opens) */
{
}

bool GarageDoorDevice::init() {
    _btn.init();
    _state = GarageState::STOPPED_MID;
    _move_start_us = 0;
    _rest_since_us = 0;            /* 0 => first press allowed immediately */
    _last_dir_up = false;
    ESP_LOGI(TAG, "Boot: STOPPED_MID (first press opens)");
    return true;
}

void GarageDoorDevice::update() {
    _btn.update();
    if (_btn.wasPressed()) cmdToggle();

    if (isMoving()) {
        uint32_t e = (uint32_t)((esp_timer_get_time() - _move_start_us) / 1000ULL);
        if (e >= GARAGE_TRAVEL_MS) {
            if (_state == GarageState::MOVING_UP) enterIdleOpen();
            else                                  enterIdleClosed();
        }
    }
}

void GarageDoorDevice::cmdToggle() {
    switch (_state) {
        case GarageState::MOVING_UP:
        case GarageState::MOVING_DOWN:
            enterStoppedMid();             // stop is always immediate
            return;
        case GarageState::STOPPED_MID:
            if (!dwellElapsed()) { ESP_LOGI(TAG, "press ignored (dwell)"); return; }
            if (_last_dir_up) enterMovingDown();   // reverse
            else              enterMovingUp();
            return;
        case GarageState::IDLE_OPEN:
            if (!dwellElapsed()) { ESP_LOGI(TAG, "press ignored (dwell)"); return; }
            enterMovingDown();
            return;
        case GarageState::IDLE_CLOSED:
            if (!dwellElapsed()) { ESP_LOGI(TAG, "press ignored (dwell)"); return; }
            enterMovingUp();
            return;
    }
}

void GarageDoorDevice::cmdUp() {
    switch (_state) {
        case GarageState::STOPPED_MID:
        case GarageState::IDLE_CLOSED:
            if (!dwellElapsed()) { ESP_LOGI(TAG, "UP ignored (dwell)"); return; }
            enterMovingUp();   break;
        case GarageState::MOVING_UP:
        case GarageState::MOVING_DOWN:  enterStoppedMid(); break;
        case GarageState::IDLE_OPEN:    break;             // already open
    }
}

void GarageDoorDevice::cmdDown() {
    switch (_state) {
        case GarageState::STOPPED_MID:
        case GarageState::IDLE_OPEN:
            if (!dwellElapsed()) { ESP_LOGI(TAG, "DOWN ignored (dwell)"); return; }
            enterMovingDown(); break;
        case GarageState::MOVING_UP:
        case GarageState::MOVING_DOWN:  enterStoppedMid(); break;
        case GarageState::IDLE_CLOSED:  break;             // already closed
    }
}

void GarageDoorDevice::stop() {
    if (isMoving()) enterStoppedMid();
}

bool GarageDoorDevice::dwellElapsed() const {
    uint64_t since_ms = (esp_timer_get_time() - _rest_since_us) / 1000ULL;
    return since_ms >= GARAGE_MIN_DWELL_MS;
}

void GarageDoorDevice::enterStoppedMid() {
    _state = GarageState::STOPPED_MID; _move_start_us = 0;
    _rest_since_us = esp_timer_get_time();
    ESP_LOGI(TAG, "-> STOPPED_MID");
}
void GarageDoorDevice::enterMovingUp() {
    _state = GarageState::MOVING_UP; _move_start_us = esp_timer_get_time();
    _last_dir_up = true;  ESP_LOGI(TAG, "-> MOVING_UP");
}
void GarageDoorDevice::enterMovingDown() {
    _state = GarageState::MOVING_DOWN; _move_start_us = esp_timer_get_time();
    _last_dir_up = false; ESP_LOGI(TAG, "-> MOVING_DOWN");
}
void GarageDoorDevice::enterIdleOpen() {
    _state = GarageState::IDLE_OPEN; _move_start_us = 0;
    _rest_since_us = esp_timer_get_time();
    ESP_LOGI(TAG, "-> IDLE_OPEN");
}
void GarageDoorDevice::enterIdleClosed() {
    _state = GarageState::IDLE_CLOSED; _move_start_us = 0;
    _rest_since_us = esp_timer_get_time();
    ESP_LOGI(TAG, "-> IDLE_CLOSED");
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
    return (uint32_t)((esp_timer_get_time() - _move_start_us) / 1000ULL);
}