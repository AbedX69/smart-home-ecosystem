/**
 * @file buzzer.h
 * @brief Passive piezo buzzer driver for ESP32 (ESP-IDF).
 *
 * @details
 * This component drives passive piezo buzzers using LEDC (PWM).
 * Supports tones, frequency sweeps, preset sounds, and melodies.
 * All sound playback is non-blocking (plays in background).
 *
 * @note
 * Electrical assumptions:
 * - Passive piezo buzzer (NOT active buzzers with built-in oscillators)
 * - Connected between GPIO and GND
 * - Optional 100-330 ohm series resistor (not strictly required)
 *
 * @par Supported hardware
 * - Any passive piezo buzzer element
 * - Passive buzzer modules (the ones WITHOUT a built-in oscillator)
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
 * BEGINNER'S GUIDE: PIEZO BUZZERS
 * =============================================================================
 * 
 * A piezo buzzer makes sound by vibrating a crystal when you apply voltage.
 * There are TWO types - this driver only works with PASSIVE ones:
 * 
 * PASSIVE PIEZO (what we use):
 *     - No internal oscillator
 *     - YOU control the frequency with PWM
 *     - Can play different pitches and melodies
 *     - Looks the same as active but has no + mark on top (usually)
 * 
 * ACTIVE PIEZO (NOT supported):
 *     - Has a built-in oscillator circuit
 *     - Makes one fixed tone when you apply voltage
 *     - Just needs GPIO HIGH/LOW, no PWM
 *     - Usually has a + mark and is sealed
 * 
 * HOW TO TELL THEM APART:
 *     - Apply 3.3V directly: Active makes sound, Passive is silent
 *     - Look at the bottom: Active has a black blob (IC), Passive has a bare PCB
 * 
 * =============================================================================
 * HOW PASSIVE PIEZOS MAKE SOUND
 * =============================================================================
 * 
 * The crystal inside bends when voltage is applied:
 * 
 *     Voltage ON:          Voltage OFF:
 *     
 *       ┌────────┐           ┌────────┐
 *       │ ~~~~~~ │  ← bent   │ ────── │  ← flat
 *       └────────┘           └────────┘
 * 
 * By switching voltage ON/OFF rapidly (PWM), the crystal vibrates = sound!
 * 
 *     440 times/second = A4 note (concert pitch)
 *     262 times/second = Middle C (C4)
 *     Higher frequency  = Higher pitch
 *     Lower frequency   = Lower pitch
 * 
 * =============================================================================
 * WHY PWM (LEDC) FOR SOUND?
 * =============================================================================
 * 
 * PWM = Pulse Width Modulation = switching ON/OFF very fast
 * 
 *     PWM signal at 1000Hz:
 *     
 *          ┌──┐  ┌──┐  ┌──┐  ┌──┐  ┌──┐
 *          │  │  │  │  │  │  │  │  │  │
 *     ─────┘  └──┘  └──┘  └──┘  └──┘  └─────
 *     
 *     ←──── 1ms ────→   (1000Hz = 1ms period)
 * 
 * The ESP32's LEDC peripheral generates this signal in hardware.
 * "LEDC" stands for "LED Controller" but it's just a PWM generator -
 * works great for buzzers too.
 * 
 * =============================================================================
 * VOLUME CONTROL
 * =============================================================================
 * 
 * Volume is controlled by the PWM DUTY CYCLE (how long the signal is ON
 * vs OFF within each period):
 * 
 *     10% duty (quiet):        50% duty (LOUDEST):
 *     
 *     ┌┐    ┌┐    ┌┐          ┌────┐  ┌────┐  ┌────┐
 *     ││    ││    ││          │    │  │    │  │    │
 *     ┘└────┘└────┘└──        ┘    └──┘    └──┘    └──
 * 
 * 50% duty = maximum voltage swing = loudest sound.
 * Going ABOVE 50% actually gets quieter (same swing, inverted).
 * This driver maps 0-100% volume to 0-50% PWM duty automatically.
 * 
 * =============================================================================
 * NON-BLOCKING PLAYBACK
 * =============================================================================
 * 
 * All sound functions return IMMEDIATELY. The sound plays in a background
 * FreeRTOS task while your code keeps running:
 * 
 *     buzzer.beep();          // Starts beep, returns RIGHT AWAY
 *     doOtherStuff();         // Runs while beep is still playing
 *     
 * vs BLOCKING (which this driver does NOT do):
 *     
 *     buzzer.beep();          // Would wait until beep finishes
 *     doOtherStuff();         // Only runs AFTER beep is done
 * 
 * If you start a new sound while one is playing, the old sound is
 * stopped and the new one starts immediately.
 * 
 * =============================================================================
 * WIRING
 * =============================================================================
 * 
 *     ESP32             Passive Buzzer
 *     ┌──────┐         ┌──────────┐
 *     │      │         │          │
 *     │ GPIOx├────────►│ +  (in)  │
 *     │      │         │          │
 *     │  GND ├────────►│ -  (gnd) │
 *     │      │         │          │
 *     └──────┘         └──────────┘
 * 
 * Optional: Add a 220 ohm resistor between GPIO and buzzer + pin
 * to limit current. Not required but protects the GPIO.
 * 
 * =============================================================================
 * LEDC RESOURCE USAGE
 * =============================================================================
 * 
 * This driver uses:
 *     - LEDC Timer 0, LOW_SPEED_MODE
 *     - LEDC Channel 0, LOW_SPEED_MODE
 * 
 * If you're also using LEDC for LEDs (like the LED component), make
 * sure to use DIFFERENT timers and channels to avoid conflicts:
 *     - Buzzer: Timer 0, Channel 0
 *     - LEDs:   Timer 1+, Channel 1+
 * 
 * LOW_SPEED_MODE is used because it works on ALL ESP32 variants.
 * HIGH_SPEED_MODE only exists on the original ESP32, not S3/C6.
 * 
 * =============================================================================
 * USAGE EXAMPLE
 * =============================================================================
 * 
 *     #include "buzzer.h"
 *     
 *     extern "C" void app_main(void) {
 *         Buzzer buzzer(GPIO_NUM_4);
 *         buzzer.init();
 *         
 *         // Play a tone
 *         buzzer.tone(1000, 500, 50);  // 1kHz, 500ms, 50% volume
 *         vTaskDelay(pdMS_TO_TICKS(1000));
 *         
 *         // Use presets
 *         buzzer.beep();       // Quick UI feedback
 *         vTaskDelay(pdMS_TO_TICKS(500));
 *         
 *         buzzer.success();    // Happy ascending sound
 *         vTaskDelay(pdMS_TO_TICKS(1000));
 *         
 *         buzzer.error();      // Sad descending sound
 *         vTaskDelay(pdMS_TO_TICKS(1000));
 *         
 *         // Frequency sweep (sounds like a theremin)
 *         buzzer.sweepLog(300, 3000, 1000, 60, 10);
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
 * @brief Musical note structure for melody playback.
 *
 * @details
 * Represents one note (or rest) in a melody.
 * Set frequencyHz to 0 for a rest (silence).
 */
struct BuzzerNote {
    uint16_t frequencyHz;   /**< Frequency in Hz (0 = rest/silence) */
    uint16_t durationMs;    /**< Duration in milliseconds */
    uint8_t  volume;        /**< Volume 0-100% */
};


/**
 * @class Buzzer
 * @brief Passive piezo buzzer driver using LEDC PWM.
 *
 * @details
 * Provides tone generation, frequency sweeps, preset sounds,
 * and melody playback. All playback is non-blocking.
 */
class Buzzer {

public:

    /**
     * @brief Construct a new Buzzer instance.
     *
     * @param pin GPIO pin the buzzer is connected to.
     *
     * @note
     * Does not configure hardware. Call init() first.
     */
    Buzzer(gpio_num_t pin);


    /**
     * @brief Destroy the Buzzer instance.
     *
     * @details
     * Stops any playing sound and releases LEDC resources.
     */
    ~Buzzer();


    /**
     * @brief Initialize LEDC peripheral for buzzer output.
     *
     * @details
     * - Configures LEDC Timer 0 (LOW_SPEED_MODE, 10-bit resolution)
     * - Configures LEDC Channel 0 on the specified pin
     * - Creates a mutex for thread safety
     *
     * @note Must be called before any other Buzzer methods.
     */
    void init();


    // =========================== Core Functions ===========================

    /**
     * @brief Play a tone at the specified frequency.
     *
     * @param frequencyHz Frequency in Hz (20-20000 recommended).
     * @param durationMs  Duration in ms (0 = play until stop() is called).
     * @param volume      Volume 0-100% (0 = silent, 100 = max).
     *
     * @note Non-blocking. Returns immediately; sound plays in background.
     * @note Playing a new sound stops any currently playing sound.
     */
    void tone(uint32_t frequencyHz, uint32_t durationMs, uint8_t volume);


    /**
     * @brief Stop any currently playing sound immediately.
     *
     * @note Safe to call at any time, even if nothing is playing.
     */
    void stop();


    /**
     * @brief Logarithmic frequency sweep.
     *
     * @param startHz    Starting frequency in Hz.
     * @param endHz      Ending frequency in Hz (can be lower for descending).
     * @param durationMs Total sweep time in milliseconds.
     * @param volume     Volume 0-100%.
     * @param stepMs     Time between frequency steps (10-20ms recommended).
     *
     * @note Non-blocking. Log sweeps sound more musical than linear sweeps
     *       because human hearing perceives pitch logarithmically.
     */
    void sweepLog(uint32_t startHz, uint32_t endHz, uint32_t durationMs,
                  uint8_t volume, uint32_t stepMs);


    // =========================== Preset Sounds ===========================

    /**
     * @brief Quick UI feedback beep.
     *
     * @details Short 2kHz beep (80ms). For button presses, confirmations.
     */
    void beep();


    /**
     * @brief R2D2-style chirp sound.
     *
     * @details Rising then falling frequency sweep (~370ms).
     *          Sounds robotic and playful.
     */
    void chirp();


    /**
     * @brief Two-tone alarm sound.
     *
     * @details Alternates 900Hz and 1200Hz (~1200ms).
     *          Attention-grabbing but not painful.
     */
    void alarm();


    /**
     * @brief Success/completion sound.
     *
     * @details Ascending C5-E5-G5 major triad (~400ms).
     *          Cheerful "task complete" feedback.
     */
    void success();


    /**
     * @brief Error/failure sound.
     *
     * @details Descending G4-Eb4-C4 minor pattern (~500ms).
     *          Sounds disappointed but not harsh.
     */
    void error();


    /**
     * @brief Short UI click sound.
     *
     * @details Very brief 6kHz tick (15ms).
     *          Like a mechanical keyboard click.
     */
    void click();


    // =========================== Advanced ===========================

    /**
     * @brief Play a melody (sequence of notes).
     *
     * @param notes Array of BuzzerNote structures.
     * @param count Number of notes in the array.
     * @param gapMs Silence between notes in ms (0 = legato).
     *
     * @note Non-blocking. The notes array is copied internally.
     */
    void playMelody(const BuzzerNote *notes, int count, uint16_t gapMs);


private:

    gpio_num_t pin;                 // GPIO pin number
    bool initialized;               // True after init()
    TaskHandle_t taskHandle;        // Current background sound task
    SemaphoreHandle_t mutex;        // Thread safety mutex

    // --- Internal helpers ---

    /**
     * @brief Convert volume percentage (0-100) to LEDC duty value.
     */
    uint32_t volumeToDuty(uint8_t volume);

    /**
     * @brief Set LEDC frequency and duty (low-level hardware control).
     */
    void setOutput(uint32_t frequencyHz, uint32_t duty);

    /**
     * @brief Kill any running background task and silence output.
     *
     * @note Must be called while holding the mutex.
     */
    void killCurrentTask();

    // --- Background task functions (static, called via FreeRTOS) ---

    static void toneTask(void *pvParameters);
    static void sweepTask(void *pvParameters);
    static void melodyTask(void *pvParameters);
};
