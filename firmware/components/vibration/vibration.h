/**
 * @file vibration.h
 * @brief Vibration motor driver for ESP32 (ESP-IDF).
 *
 * @details
 * This component drives vibration motors using LEDC (PWM).
 * Supports intensity control, timed pulses, and preset haptic patterns.
 * All vibration playback is non-blocking (plays in background).
 *
 * @note
 * Electrical assumptions:
 * - Vibration motor module with built-in driver (3 pins: IN, VCC, GND)
 * - IN pin accepts PWM signal from ESP32 GPIO
 * - VCC powered from 3.3V or 5V (check your module's specs)
 *
 * @par Supported hardware
 * - Coin/pancake vibration motor modules (3-pin with built-in driver)
 * - Any vibration motor module with a PWM-controlled IN pin
 * - Bare motors through a transistor/MOSFET (you wire the driver yourself)
 *
 * @par Tested boards
 * - ESP32D (original ESP32)
 * - ESP32-S3 WROOM
 * - ESP32-S3 Seeed XIAO
 * - ESP32-C6 WROOM
 * - ESP32-C6 Seeed XIAO
 */

/*
 * =============================================================================
 * BEGINNER'S GUIDE: VIBRATION MOTORS
 * =============================================================================
 * 
 * Vibration motors are tiny DC motors with an off-center weight attached
 * to the shaft. When the motor spins, the unbalanced weight wobbles and
 * creates the vibration you feel in phones, game controllers, etc.
 * 
 * =============================================================================
 * TYPES OF VIBRATION MOTORS
 * =============================================================================
 * 
 * COIN / PANCAKE (what you have):
 *     - Flat disc shape, ~10mm diameter
 *     - The weight is built into the flat rotor
 *     - Often found in phones and wearables
 *     
 *           ┌─────────────┐
 *           │  ○ ○ ○ ○ ○  │  ← Flat disc
 *           │  ○ ◉ ○ ○ ○  │     ◉ = off-center weight
 *           │  ○ ○ ○ ○ ○  │
 *           └──┬──┬──┬────┘
 *              │  │  │
 *             IN VCC GND    ← Your 3-pin module
 * 
 * CYLINDER / BARREL:
 *     - Small tube shape, ~5mm x 12mm
 *     - Weight is on the end of the shaft
 *     - Often found in game controllers
 *     
 *           ┌──────────┐
 *           │  ╔══╗    │  ← Cylinder body
 *           │  ║██║──◗ │     ◗ = off-center weight on shaft
 *           │  ╚══╝    │
 *           └──┬───┬───┘
 *              +   -        ← Bare motor (2 wires, needs transistor)
 * 
 * =============================================================================
 * YOUR MODULE: 3-PIN WITH BUILT-IN DRIVER
 * =============================================================================
 * 
 * Your module (V919) has a tiny transistor/driver circuit on the PCB.
 * This is great because:
 * 
 *     WITHOUT driver module (bare motor):
 *         
 *         ESP32 GPIO ──► Transistor ──► Motor ──► GND
 *              │              │
 *              └── Can't drive motor directly!
 *                  GPIO max ~40mA, motor needs ~80-100mA
 *     
 *     WITH driver module (what you have):
 *         
 *         ESP32 GPIO ──► IN pin ──► [Built-in driver] ──► Motor
 *                                        │
 *                               VCC ─────┘  (powers the motor)
 *                               GND ─────── (shared ground)
 *     
 *         The IN pin just needs a small signal. The module's
 *         built-in transistor handles the motor's current.
 * 
 * =============================================================================
 * HOW INTENSITY CONTROL WORKS
 * =============================================================================
 * 
 * Unlike the buzzer (where PWM frequency = pitch), vibration motors
 * don't care about frequency. They only care about DUTY CYCLE:
 * 
 *     Low duty (30%) = weak vibration:
 *     
 *     ┌─┐      ┌─┐      ┌─┐
 *     │ │      │ │      │ │         Motor barely spins
 *     ┘ └──────┘ └──────┘ └──────
 *     
 *     High duty (100%) = strong vibration:
 *     
 *     ┌────────┌────────┌────────
 *     │        │        │           Motor at full speed
 *     ┘        ┘        ┘
 * 
 * We use a fixed PWM frequency (~5kHz) so the motor doesn't
 * make audible whining, and vary the duty cycle for intensity.
 * 
 * =============================================================================
 * HAPTIC PATTERNS
 * =============================================================================
 * 
 * Haptic feedback = vibration patterns that communicate information
 * through touch. Your phone does this all the time:
 * 
 *     NOTIFICATION TAP:
 *     ─────┌──────┐──────────────────  Single pulse, 150ms
 *          │      │
 *          └──────┘
 *     
 *     DOUBLE TAP:
 *     ─────┌─────┐     ┌─────┐──────  Two pulses, 150ms gap between
 *          │     │     │     │
 *          └─────┘     └─────┘
 *     
 *     HEARTBEAT:
 *     ─────┌────┐     ┌───────┐──────  "ba-BUM" (second beat longer)
 *          │    │     │       │
 *          └────┘     └───────┘
 *     
 *     ALARM:
 *     ─────┌──────┐   ┌──────┐   ┌──  Long pulses, urgent
 *          │      │   │      │   │
 *          └──────┘   └──────┘   └──
 * 
 * =============================================================================
 * LEDC RESOURCE USAGE
 * =============================================================================
 * 
 * This driver uses:
 *     - LEDC Timer 1, LOW_SPEED_MODE
 *     - LEDC Channel 1, LOW_SPEED_MODE
 * 
 * This is DIFFERENT from the buzzer driver (Timer 0, Channel 0)
 * so they can coexist without conflicts:
 *     - Buzzer:    Timer 0, Channel 0
 *     - Vibration: Timer 1, Channel 1
 *     - LEDs:      Timer 2+, Channel 2+
 * 
 * =============================================================================
 * WIRING
 * =============================================================================
 * 
 *     ESP32             Vibration Module (V919)
 *     ┌──────┐         ┌──────────────┐
 *     │      │         │              │
 *     │ GPIOx├────────►│ IN  (signal) │
 *     │      │         │              │
 *     │ 3.3V ├────────►│ VCC (power)  │
 *     │      │         │              │
 *     │  GND ├────────►│ GND (ground) │
 *     │      │         │              │
 *     └──────┘         └──────────────┘
 * 
 * Some modules work on 3.3V, some need 5V for full intensity.
 * Check your module - if vibration feels weak on 3.3V, try 5V on VCC.
 * The IN pin always works with 3.3V logic from the ESP32.
 * 
 * =============================================================================
 * USAGE EXAMPLE
 * =============================================================================
 * 
 *     #include "vibration.h"
 *     
 *     extern "C" void app_main(void) {
 *         Vibration motor(GPIO_NUM_4);
 *         motor.init();
 *         
 *         // Vibrate at 70% intensity for 500ms
 *         motor.vibrate(500, 70);
 *         vTaskDelay(pdMS_TO_TICKS(1000));
 *         
 *         // Use presets
 *         motor.tap();           // Quick notification tap
 *         vTaskDelay(pdMS_TO_TICKS(1000));
 *         
 *         motor.doubleTap();     // Two quick taps
 *         vTaskDelay(pdMS_TO_TICKS(1000));
 *         
 *         motor.heartbeat();     // ba-BUM pattern
 *         vTaskDelay(pdMS_TO_TICKS(1000));
 *         
 *         motor.alarm();         // Urgent repeated buzzing
 *     }
 * 
 * =============================================================================
 */

#pragma once

#include <driver/ledc.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <stdint.h>


/**
 * @brief Single step in a vibration pattern.
 *
 * @details
 * Represents one vibration pulse or rest in a pattern sequence.
 * Set intensity to 0 for a rest (pause between pulses).
 */
struct VibrationStep {
    uint16_t durationMs;    /**< Duration in milliseconds */
    uint8_t  intensity;     /**< Intensity 0-100% (0 = rest/pause) */
};


/**
 * @class Vibration
 * @brief Vibration motor driver using LEDC PWM.
 *
 * @details
 * Provides intensity-controlled vibration, timed pulses,
 * and preset haptic patterns. All playback is non-blocking.
 */
class Vibration {

public:

    /**
     * @brief Construct a new Vibration instance.
     *
     * @param pin GPIO pin connected to the motor module's IN pin.
     *
     * @note
     * Does not configure hardware. Call init() first.
     */
    Vibration(gpio_num_t pin);


    /**
     * @brief Destroy the Vibration instance.
     *
     * @details
     * Stops any active vibration and releases resources.
     */
    ~Vibration();


    /**
     * @brief Initialize LEDC peripheral for vibration output.
     *
     * @details
     * - Configures LEDC Timer 1 (LOW_SPEED_MODE, 10-bit, 5kHz)
     * - Configures LEDC Channel 1 on the specified pin
     * - Creates a mutex for thread safety
     *
     * @note Must be called before any other Vibration methods.
     */
    void init();


    // =========================== Core Functions ===========================

    /**
     * @brief Vibrate for a specified duration.
     *
     * @param durationMs Duration in ms (0 = vibrate until stop() is called).
     * @param intensity  Intensity 0-100% (default: 100).
     *
     * @note Non-blocking. Returns immediately; vibration runs in background.
     * @note Starting a new vibration stops any currently active one.
     */
    void vibrate(uint32_t durationMs, uint8_t intensity = 100);


    /**
     * @brief Stop vibration immediately.
     *
     * @note Safe to call at any time, even if nothing is active.
     */
    void stop();


    // =========================== Preset Patterns ===========================

    /**
     * @brief Quick notification tap.
     *
     * @details Single strong pulse (150ms).
     *          Like a phone notification buzz.
     */
    void tap();


    /**
     * @brief Double tap pattern.
     *
     * @details Two distinct pulses with clear gap (~450ms total).
     *          Like a "message received" haptic.
     */
    void doubleTap();


    /**
     * @brief Triple tap pattern.
     *
     * @details Three distinct pulses (~750ms total).
     *          Good for "attention needed" feedback.
     */
    void tripleTap();


    /**
     * @brief Heartbeat pattern.
     *
     * @details Two pulses: short-medium then longer-strong (~520ms total).
     *          Feels like "ba-BUM".
     */
    void heartbeat();


    /**
     * @brief Alarm / urgent buzz.
     *
     * @details Four long strong pulses (~2.6 seconds total).
     *          Impossible to ignore.
     */
    void alarm();


    /**
     * @brief Gentle ramp-up pulse.
     *
     * @details Gradually increases intensity then drops off (~750ms total).
     *          Smooth, non-jarring feedback.
     */
    void pulse();


    // =========================== Advanced ===========================

    /**
     * @brief Play a custom vibration pattern.
     *
     * @param steps Array of VibrationStep structures.
     * @param count Number of steps in the array.
     *
     * @note Non-blocking. The steps array is copied internally.
     */
    void playPattern(const VibrationStep *steps, int count);


private:

    gpio_num_t pin;                 // GPIO pin number
    bool initialized;               // True after init()
    TaskHandle_t taskHandle;        // Current background vibration task
    SemaphoreHandle_t mutex;        // Thread safety mutex

    // --- Internal helpers ---

    /**
     * @brief Convert intensity percentage (0-100) to LEDC duty value.
     */
    uint32_t intensityToDuty(uint8_t intensity);

    /**
     * @brief Set LEDC duty (low-level hardware control).
     */
    void setOutput(uint32_t duty);

    /**
     * @brief Kill any running background task and stop motor.
     *
     * @note Must be called while holding the mutex.
     */
    void killCurrentTask();

    // --- Background task functions (static, called via FreeRTOS) ---

    static void vibrateTask(void *pvParameters);
    static void patternTask(void *pvParameters);
};
