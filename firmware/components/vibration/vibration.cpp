/**
 * @file vibration.cpp
 * @brief Vibration motor driver implementation (ESP-IDF).
 *
 * @details
 * Implements PWM-based vibration control using LEDC peripheral.
 * All timed vibrations use background FreeRTOS tasks for non-blocking playback.
 */

/*
 * =============================================================================
 * BEGINNER'S GUIDE: HOW THIS IMPLEMENTATION WORKS
 * =============================================================================
 * 
 * This driver is structurally almost identical to the buzzer driver,
 * but SIMPLER because vibration motors don't care about frequency:
 * 
 *     BUZZER:     frequency (pitch) + duty (volume)
 *     VIBRATION:  fixed frequency   + duty (intensity)
 * 
 * The PWM frequency is locked at 5kHz. We only change the duty cycle
 * to control how strong the vibration feels.
 * 
 * =============================================================================
 * WHY 5kHz PWM FREQUENCY?
 * =============================================================================
 * 
 * The motor doesn't care what frequency we use. But WE care because:
 * 
 *     TOO LOW (< 1kHz):
 *         Motor makes audible whining/buzzing noise
 *         The ON/OFF switching is slow enough for you to hear it
 *     
 *     JUST RIGHT (5kHz):
 *         Above human hearing threshold for most people
 *         Motor runs smoothly, no audible whine
 *         Low enough that the MOSFET/transistor switches efficiently
 *     
 *     TOO HIGH (> 20kHz):
 *         Works fine but wastes switching energy
 *         No benefit since motor inertia smooths everything anyway
 * 
 * =============================================================================
 * INTENSITY vs BUZZER VOLUME
 * =============================================================================
 * 
 * Unlike the buzzer (where 50% duty = max loudness), vibration motors
 * are simple DC motors. More duty = more average voltage = faster spin
 * = stronger vibration. So we map linearly:
 * 
 *     0%   intensity → 0%   duty → motor off
 *     50%  intensity → 50%  duty → medium vibration
 *     100% intensity → 100% duty → maximum vibration
 * 
 * This is different from the buzzer where 100% volume mapped to 50% duty!
 * 
 * =============================================================================
 * NON-BLOCKING DESIGN
 * =============================================================================
 * 
 * Same pattern as the buzzer driver:
 * 
 *     motor.vibrate(500, 80);     // Start vibrating, returns immediately
 *     doOtherStuff();             // Runs while motor is buzzing
 *     
 *     // After 500ms, background task stops the motor automatically
 * 
 * If you start a new vibration while one is running, the old one is
 * killed and the new one starts.
 * 
 * =============================================================================
 * HAPTIC PATTERN DESIGN
 * =============================================================================
 * 
 * Good haptic patterns follow these principles:
 * 
 * 1. CONTRAST: Vary intensity between steps. A tap at 80% feels
 *    different from a buzz at 40%.
 * 
 * 2. TIMING: Short gaps (50-100ms) between pulses feel "connected".
 *    Longer gaps (200ms+) feel like separate events.
 * 
 * 3. RAMP: Gradual intensity changes feel smooth and premium.
 *    Sudden on/off feels cheap and jarring.
 * 
 * 4. BREVITY: Shorter patterns feel more responsive.
 *    Long patterns feel annoying quickly.
 * 
 *     GOOD (phone notification):
 *     ──┌─┐──────   Quick, crisp, done.
 *       │ │
 *       └─┘
 *     
 *     BAD (cheap alarm clock):
 *     ──┌────────────────────────   BZZZZZZZZZZZ (please stop)
 *       │
 *       └
 * 
 * =============================================================================
 */

#include "vibration.h"

#include <string.h>     /* memcpy() for copying pattern arrays */
#include <esp_log.h>


/*
 * Logging tag for ESP_LOGI, ESP_LOGE, etc.
 */
static const char *TAG = "Vibration";


/*
 * =============================================================================
 * LEDC CONFIGURATION CONSTANTS
 * =============================================================================
 * 
 * IMPORTANT: These use Timer 1 and Channel 1, different from the buzzer
 * (Timer 0, Channel 0) so both can work simultaneously.
 * 
 * PWM_FREQ: 5kHz - above audible whine, efficient switching.
 * RESOLUTION: 10-bit = 1024 duty steps (0-1023) for smooth intensity control.
 */
#define VIB_LEDC_TIMER          LEDC_TIMER_1
#define VIB_LEDC_CHANNEL        LEDC_CHANNEL_1
#define VIB_LEDC_MODE           LEDC_LOW_SPEED_MODE
#define VIB_DUTY_RESOLUTION     LEDC_TIMER_10_BIT
#define VIB_MAX_DUTY            ((1 << 10) - 1)         /* 1023 */
#define VIB_PWM_FREQ_HZ         5000                     /* 5kHz - no audible whine */

/*
 * FreeRTOS task parameters for background vibration playback.
 */
#define VIB_TASK_STACK          2048
#define VIB_TASK_PRIORITY       2


/*
 * =============================================================================
 * INTERNAL STRUCTS: PARAMETERS FOR BACKGROUND TASKS
 * =============================================================================
 */

struct VibrateParams {
    Vibration *self;
    uint32_t   durationMs;
};

struct PatternParams {
    Vibration     *self;
    VibrationStep *steps;       // Heap-allocated copy of the pattern
    int            count;
};


/* ============================= Constructor / Destructor ============================= */

/**
 * @brief Construct a Vibration instance.
 *
 * @param pin GPIO pin number.
 */

/*
 * =============================================================================
 * CONSTRUCTOR
 * =============================================================================
 * 
 * Same pattern as Buzzer, Button, Encoder.
 * Just stores config. Hardware setup happens in init().
 */
Vibration::Vibration(gpio_num_t pin)
    : pin(pin),
      initialized(false),
      taskHandle(NULL),
      mutex(NULL)
{
}


/**
 * @brief Destructor.
 */

/*
 * =============================================================================
 * DESTRUCTOR
 * =============================================================================
 * 
 * Stops any active vibration and cleans up the mutex.
 */
Vibration::~Vibration() {
    if (initialized) {
        stop();
    }
    if (mutex != NULL) {
        vSemaphoreDelete(mutex);
        mutex = NULL;
    }
}


/* ============================= Initialization ============================= */

/**
 * @brief Initialize LEDC hardware.
 */

/*
 * =============================================================================
 * INITIALIZATION
 * =============================================================================
 * 
 * Same idea as the buzzer, but simpler:
 * 
 *     Timer 1                    Channel 1
 *     ┌─────────────┐           ┌──────────────┐
 *     │ 5kHz fixed   │           │ Timer 1 ──►  │
 *     │ 10-bit res   │──────────►│ Duty cycle   │──► GPIO ──► Motor IN
 *     └─────────────┘           └──────────────┘
 * 
 * Frequency stays at 5kHz forever. We only change the duty cycle.
 */
void Vibration::init() {
    ESP_LOGI(TAG, "Initializing vibration motor on GPIO %d", pin);

    mutex = xSemaphoreCreateMutex();
    if (mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return;
    }

    /*
     * Configure LEDC timer.
     * Fixed 5kHz frequency - never changes (unlike buzzer which changes per-tone).
     */
    ledc_timer_config_t timerCfg = {};
    timerCfg.speed_mode      = VIB_LEDC_MODE;
    timerCfg.timer_num       = VIB_LEDC_TIMER;
    timerCfg.duty_resolution = VIB_DUTY_RESOLUTION;
    timerCfg.freq_hz         = VIB_PWM_FREQ_HZ;
    timerCfg.clk_cfg         = LEDC_AUTO_CLK;

    esp_err_t ret = ledc_timer_config(&timerCfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LEDC timer config failed: %s", esp_err_to_name(ret));
        return;
    }

    /*
     * Configure LEDC channel.
     * duty = 0 means motor is off initially.
     */
    ledc_channel_config_t channelCfg = {};
    channelCfg.speed_mode = VIB_LEDC_MODE;
    channelCfg.channel    = VIB_LEDC_CHANNEL;
    channelCfg.timer_sel  = VIB_LEDC_TIMER;
    channelCfg.intr_type  = LEDC_INTR_DISABLE;
    channelCfg.gpio_num   = pin;
    channelCfg.duty       = 0;
    channelCfg.hpoint     = 0;

    ret = ledc_channel_config(&channelCfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LEDC channel config failed: %s", esp_err_to_name(ret));
        return;
    }

    initialized = true;
    ESP_LOGI(TAG, "Vibration motor initialized on GPIO %d", pin);
}


/* ============================= Internal Helpers ============================= */

/**
 * @brief Convert intensity percentage to LEDC duty value.
 *
 * @param intensity 0-100%.
 * @return LEDC duty value.
 */

/*
 * =============================================================================
 * INTENSITY → DUTY CONVERSION
 * =============================================================================
 * 
 * Unlike the buzzer (where 100% volume = 50% duty), vibration motors
 * are linear: more duty = more power = stronger vibration.
 * 
 *     0%   intensity → 0    duty → motor off
 *     50%  intensity → 512  duty → half speed
 *     100% intensity → 1023 duty → full blast
 */
uint32_t Vibration::intensityToDuty(uint8_t intensity) {
    if (intensity == 0) return 0;
    if (intensity > 100) intensity = 100;
    return (uint32_t)intensity * VIB_MAX_DUTY / 100;
}


/**
 * @brief Set LEDC duty.
 *
 * @param duty LEDC duty value (0 = off).
 */

/*
 * =============================================================================
 * SET OUTPUT
 * =============================================================================
 * 
 * Even simpler than the buzzer version - no frequency change needed.
 * Just set duty and commit.
 */
void Vibration::setOutput(uint32_t duty) {
    ledc_set_duty(VIB_LEDC_MODE, VIB_LEDC_CHANNEL, duty);
    ledc_update_duty(VIB_LEDC_MODE, VIB_LEDC_CHANNEL);
}


/**
 * @brief Kill current background task and stop motor.
 */

/*
 * =============================================================================
 * KILL CURRENT TASK
 * =============================================================================
 * 
 * Same pattern as buzzer. Kill old task, silence motor, ready for new command.
 */
void Vibration::killCurrentTask() {
    if (taskHandle != NULL) {
        vTaskDelete(taskHandle);
        taskHandle = NULL;
    }
    setOutput(0);
}


/* ============================= Background Tasks ============================= */

/**
 * @brief Vibrate task: runs motor for duration then stops.
 */

/*
 * =============================================================================
 * VIBRATE TASK
 * =============================================================================
 * 
 * Same as buzzer's tone task. Sleep for duration, then stop.
 */
void Vibration::vibrateTask(void *pvParameters) {
    VibrateParams *params = (VibrateParams *)pvParameters;
    Vibration *self = params->self;
    uint32_t durationMs = params->durationMs;
    delete params;

    vTaskDelay(pdMS_TO_TICKS(durationMs));

    xSemaphoreTake(self->mutex, portMAX_DELAY);
    self->setOutput(0);
    self->taskHandle = NULL;
    xSemaphoreGive(self->mutex);

    vTaskDelete(NULL);
}


/**
 * @brief Pattern task: plays a sequence of vibration steps.
 */

/*
 * =============================================================================
 * PATTERN TASK
 * =============================================================================
 * 
 * Same idea as buzzer's melody task, but simpler (no frequency, just intensity).
 * 
 * For each step:
 *     1. Set motor intensity (or 0 for rest)
 *     2. Wait for the step's duration
 *     3. Move to next step
 */
void Vibration::patternTask(void *pvParameters) {
    PatternParams *params = (PatternParams *)pvParameters;
    Vibration *self = params->self;

    for (int i = 0; i < params->count; i++) {
        VibrationStep *step = &params->steps[i];

        if (step->intensity == 0) {
            self->setOutput(0);
        } else {
            uint32_t duty = self->intensityToDuty(step->intensity);
            self->setOutput(duty);
        }

        vTaskDelay(pdMS_TO_TICKS(step->durationMs));
    }

    delete[] params->steps;
    delete params;

    xSemaphoreTake(self->mutex, portMAX_DELAY);
    self->setOutput(0);
    self->taskHandle = NULL;
    xSemaphoreGive(self->mutex);

    vTaskDelete(NULL);
}


/* ============================= Public API: Core ============================= */

/**
 * @brief Vibrate for a duration.
 */

/*
 * =============================================================================
 * vibrate() - BUZZ FOR A DURATION AT AN INTENSITY
 * =============================================================================
 * 
 * The main function. Start motor, return immediately.
 * If duration > 0, a background task stops it later.
 * If duration == 0, it runs until stop() is called.
 */
void Vibration::vibrate(uint32_t durationMs, uint8_t intensity) {
    if (!initialized) {
        ESP_LOGE(TAG, "Not initialized - call init() first");
        return;
    }

    xSemaphoreTake(mutex, portMAX_DELAY);
    killCurrentTask();

    if (intensity == 0) {
        xSemaphoreGive(mutex);
        return;
    }

    uint32_t duty = intensityToDuty(intensity);
    setOutput(duty);

    if (durationMs > 0) {
        VibrateParams *params = new VibrateParams{this, durationMs};
        BaseType_t ret = xTaskCreate(
            vibrateTask, "vib_pulse", VIB_TASK_STACK,
            params, VIB_TASK_PRIORITY, &taskHandle
        );
        if (ret != pdPASS) {
            ESP_LOGE(TAG, "Failed to create vibrate task");
            setOutput(0);
            delete params;
        }
    }

    xSemaphoreGive(mutex);
}


/**
 * @brief Stop vibration.
 */

/*
 * =============================================================================
 * stop() - KILL MOTOR IMMEDIATELY
 * =============================================================================
 */
void Vibration::stop() {
    if (!initialized) return;

    xSemaphoreTake(mutex, portMAX_DELAY);
    killCurrentTask();
    xSemaphoreGive(mutex);
}


/* ============================= Public API: Presets ============================= */

/**
 * @brief Quick notification tap.
 */

/*
 * =============================================================================
 * PRESET: TAP
 * =============================================================================
 * 
 * Single short pulse at high intensity.
 * Feels crisp and immediate. Classic phone notification.
 * 
 *     Intensity:
 *     100% │  ┌──┐
 *          │  │  │
 *       0% │──┘  └──────
 *          0  50ms
 */
void Vibration::tap() {
    vibrate(150, 100);
}


/**
 * @brief Double tap.
 */

/*
 * =============================================================================
 * PRESET: DOUBLE TAP
 * =============================================================================
 * 
 * Two distinct pulses separated by a clear gap.
 * Motor needs ~100ms+ to spin up and feel like a real vibration,
 * and the gap needs to be long enough for the motor to fully stop
 * (capacitors on the module keep it spinning briefly after power off).
 * 
 *     Intensity:
 *     100% │  ┌─────┐     ┌─────┐
 *          │  │     │     │     │
 *       0% │──┘     └─────┘     └──
 *          0  150   300   450   600 ms
 */
void Vibration::doubleTap() {
    static const VibrationStep steps[] = {
        { 150, 100 },   // First tap
        { 150,   0 },   // Gap (motor needs time to actually stop)
        { 150, 100 },   // Second tap
    };
    playPattern(steps, sizeof(steps) / sizeof(steps[0]));
}


/**
 * @brief Triple tap.
 */

/*
 * =============================================================================
 * PRESET: TRIPLE TAP
 * =============================================================================
 * 
 * Three distinct pulses. More urgent than double tap.
 * Same timing principle: long enough for motor spin-up and stop.
 * 
 *     Intensity:
 *     100% │  ┌─────┐     ┌─────┐     ┌─────┐
 *          │  │     │     │     │     │     │
 *       0% │──┘     └─────┘     └─────┘     └──
 *          0  150   300   450   600   750   900 ms
 */
void Vibration::tripleTap() {
    static const VibrationStep steps[] = {
        { 150, 100 },   // First tap
        { 150,   0 },   // Gap
        { 150, 100 },   // Second tap
        { 150,   0 },   // Gap
        { 150, 100 },   // Third tap
    };
    playPattern(steps, sizeof(steps) / sizeof(steps[0]));
}


/**
 * @brief Heartbeat pattern.
 */

/*
 * =============================================================================
 * PRESET: HEARTBEAT
 * =============================================================================
 * 
 * Two pulses mimicking a heartbeat: short "ba" then longer "BUM".
 * The second pulse is longer AND stronger, like a real heartbeat.
 * 
 * Motor modules with capacitors need longer pulses to feel distinct.
 * The gap must also be long enough for the motor to fully stop,
 * otherwise both beats blur into one continuous vibration.
 * 
 *     Intensity:
 *     100% │              ┌────────┐
 *      80% │  ┌─────┐     │        │
 *          │  │     │     │        │
 *       0% │──┘     └─────┘        └──────
 *          0  120   270   420      670 ms
 *             ba          BUM
 */
void Vibration::heartbeat() {
    static const VibrationStep steps[] = {
        { 120,  80 },   // "ba" - lighter first beat
        { 150,   0 },   // Pause (motor must fully stop here)
        { 250, 100 },   // "BUM" - stronger, longer second beat
    };
    playPattern(steps, sizeof(steps) / sizeof(steps[0]));
}


/**
 * @brief Alarm buzz.
 */

/*
 * =============================================================================
 * PRESET: ALARM
 * =============================================================================
 * 
 * Four long, strong pulses. Urgent and impossible to ignore.
 * Each pulse is long enough to reach full motor speed, and gaps
 * are long enough for the motor to fully stop between pulses.
 * 
 *     Intensity:
 *     100% │  ┌──────┐     ┌──────┐     ┌──────┐     ┌──────┐
 *          │  │      │     │      │     │      │     │      │
 *       0% │──┘      └─────┘      └─────┘      └─────┘      └──
 *          0  400    650  1050   1300  1700   1950  2350   2600 ms
 */
void Vibration::alarm() {
    static const VibrationStep steps[] = {
        { 400, 100 },   // Buzz
        { 250,   0 },   // Gap
        { 400, 100 },   // Buzz
        { 250,   0 },   // Gap
        { 400, 100 },   // Buzz
        { 250,   0 },   // Gap
        { 400, 100 },   // Buzz
    };
    playPattern(steps, sizeof(steps) / sizeof(steps[0]));
}


/**
 * @brief Gentle ramp-up pulse.
 */

/*
 * =============================================================================
 * PRESET: PULSE
 * =============================================================================
 * 
 * Gradually increases intensity then drops off.
 * Each step needs to be long enough for the motor to actually
 * change speed. With module capacitors, ~150ms per step minimum.
 * 
 * Starts above the motor's startup threshold (~40%) so it
 * actually begins spinning from the first step.
 * 
 *     Intensity:
 *     100% │              ┌─────┐
 *      80% │        ┌─────┘     │
 *      60% │  ┌─────┘           └─────┐
 *      40% │──┘                       └──
 *       0% │                             └──
 *          0  150   300   450   600   750  900 ms
 */
void Vibration::pulse() {
    static const VibrationStep steps[] = {
        { 150,  40 },   // Gentle start (above motor threshold)
        { 150,  60 },   // Building
        { 150,  80 },   // Getting there
        { 150, 100 },   // Peak
        { 150,  60 },   // Fade down
    };
    playPattern(steps, sizeof(steps) / sizeof(steps[0]));
}


/* ============================= Public API: Advanced ============================= */

/**
 * @brief Play a custom pattern.
 */

/*
 * =============================================================================
 * playPattern() - CUSTOM VIBRATION SEQUENCE
 * =============================================================================
 * 
 * Same approach as buzzer's playMelody():
 *     1. Copy the steps array to heap
 *     2. Create a background task to play through them
 *     3. Task frees the copy when done
 */
void Vibration::playPattern(const VibrationStep *steps, int count) {
    if (!initialized) {
        ESP_LOGE(TAG, "Not initialized - call init() first");
        return;
    }
    if (steps == NULL || count <= 0) {
        ESP_LOGE(TAG, "Invalid pattern (null or empty)");
        return;
    }

    VibrationStep *stepsCopy = new VibrationStep[count];
    memcpy(stepsCopy, steps, sizeof(VibrationStep) * count);

    PatternParams *params = new PatternParams{this, stepsCopy, count};

    xSemaphoreTake(mutex, portMAX_DELAY);
    killCurrentTask();

    BaseType_t ret = xTaskCreate(
        patternTask, "vib_pattern", VIB_TASK_STACK,
        params, VIB_TASK_PRIORITY, &taskHandle
    );
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create pattern task");
        delete[] stepsCopy;
        delete params;
    }

    xSemaphoreGive(mutex);
}
