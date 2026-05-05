/*
 * =============================================================================
 * FILE:        garage_door_remote.h
 * AUTHOR:      AbedX69
 * CREATED:     2026-05-05
 * VERSION:     0.0.1 (STUB)
 * =============================================================================
 *
 * GarageDoorRemote — handheld remote / wall-button ESP32 that sends UP/DOWN
 * commands to a paired GarageDoorDevice over a wireless transport.
 *
 * Conceptually:
 *   - Owns 2 buttons (UP, DOWN).
 *   - Owns a transport sender (LoRa / ESP-NOW / mesh — TBD).
 *   - On press, sends a command frame.
 *   - Optionally receives state updates back to drive an LED indicator.
 *
 * TODO — NOT YET IMPLEMENTED
 * ==========================
 *   - Pick transport. Match what GarageDoorDevice uses on its receive side.
 *   - Define the wire-format message struct (probably reuse message_protocol).
 *   - Pairing / address book (which device(s) does this remote talk to).
 *   - Optional acknowledgement + retry on send.
 *   - Battery / sleep management for handheld units.
 *
 * =============================================================================
 */

#pragma once

#include <driver/gpio.h>
#include <stdint.h>

class GarageDoorRemote {
public:
    GarageDoorRemote(gpio_num_t up_btn_pin, gpio_num_t down_btn_pin);
    ~GarageDoorRemote();

    bool init();        // TODO
    void update();      // TODO

    // void pairWith(const uint8_t mac[6]);   // TODO
    // void sendUp();                          // TODO
    // void sendDown();                        // TODO

private:
    // gpio_num_t _up_btn_pin;
    // gpio_num_t _down_btn_pin;
    // TODO: Button instances + transport handle
};
