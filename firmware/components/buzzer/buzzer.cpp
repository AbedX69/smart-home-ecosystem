/**
 * @file buzzer.cpp
 * @brief Passive piezo buzzer driver implementation (ESP-IDF).
 *
 * @details
 * Implements PWM-based buzzer control using LEDC peripheral.
 * All timed sounds use background FreeRTOS tasks for non-blocking playback.
 */

/*
 * =============================================================================
 * BEGINNER'S GUIDE: HOW THIS IMPLEMENTATION WORKS
 * =============================================================================
 * 
 * The buzzer driver has two layers:
 * 
 * LOW LEVEL (talks to hardware):
 *     setOutput(frequency, duty) → Configures LEDC PWM output
 *     
 *     This is what actually makes the buzzer vibrate.
 *     frequency = pitch (how many times per second)
 *     duty = volume (how wide the ON pulse is)
 * 
 * HIGH LEVEL (user-facing API):
 *     tone()      → Starts a sound for a duration
 *     sweepLog()  → Smoothly changes frequency over time
 *     beep()      → Pre-configured short beep
 *     etc.
 * 
 * =============================================================================
 * NON-BLOCKING DESIGN: HOW BACKGROUND TASKS WORK
 * =============================================================================
 * 
 * When you call buzzer.tone(1000, 500, 50), here's what happens:
 * 
 *     YOUR CODE                    BACKGROUND TASK
 *     ─────────                    ───────────────
 *     tone(1000, 500, 50)
 *       │ Set LEDC to 1kHz
 *       │ Create background task ──────► Task starts
 *       │ Return immediately              │
 *       ▼                                 │ vTaskDelay(500ms)
 *     doOtherStuff()                      │
 *     moreWork()                          │ ... waiting ...
 *     evenMoreWork()                      │
 *       │                                 ▼
 *       │                           Stop LEDC (silence)
 *       │                           Delete self
 *       ▼
 *     keepGoing()
 * 
 * The background task just sleeps for the duration, then stops the sound
 * and deletes itself. Your code never has to wait.
 * 
 * =============================================================================
 * WHAT HAPPENS WHEN SOUNDS OVERLAP?
 * =============================================================================
 * 
 * If you start a new sound while one is already playing:
 * 
 *     buzzer.tone(1000, 5000, 50);   // Start 5-second tone
 *     vTaskDelay(1000);               // Wait 1 second
 *     buzzer.beep();                  // Start beep (kills the tone!)
 * 
 * The new sound ALWAYS wins:
 *     1. Old background task is killed (vTaskDelete)
 *     2. Buzzer is silenced briefly
 *     3. New sound starts
 * 
 * =============================================================================
 * THREAD SAFETY
 * =============================================================================
 * 
 * Multiple FreeRTOS tasks might call buzzer functions at the same time.
 * A mutex prevents them from corrupting the internal state:
 * 
 *     Task A: buzzer.beep()    Task B: buzzer.alarm()
 *         │                        │
 *         ├─ Take mutex            ├─ Take mutex (BLOCKED - waits)
 *         ├─ Kill old task         │
 *         ├─ Start beep            │
 *         ├─ Give mutex            │
 *         │                        ├─ Take mutex (got it!)
 *         │                        ├─ Kill beep task
 *         │                        ├─ Start alarm
 *         │                        ├─ Give mutex
 * 
 * =============================================================================
 * PARAMETER PASSING TO TASKS
 * =============================================================================
 * 
 * FreeRTOS tasks receive ONE void* parameter. For simple values (like a
 * duration), we cast it directly:
 * 
 *     xTaskCreate(toneTask, ..., (void*)(uintptr_t)durationMs, ...)
 *     // In task: uint32_t duration = (uint32_t)(uintptr_t)pvParameters;
 * 
 * For complex parameters (sweep, melody), we malloc a struct and the task
 * frees it when done:
 * 
 *     SweepParams *params = new SweepParams{...};
 *     xTaskCreate(sweepTask, ..., params, ...)
 *     // In task: ... use params ... delete params;
 * 
 * =============================================================================
 */

#include "buzzer.h"

#include <math.h>       /* powf() for logarithmic sweep calculations */
#include <string.h>     /* memcpy() for copying melody note arrays */
#include <esp_log.h>


/*
 * Logging tag for ESP_LOGI, ESP_LOGE, etc.
 */
static const char *TAG = "Buzzer";


/*
 * =============================================================================
 * LEDC CONFIGURATION CONSTANTS
 * =============================================================================
 * 
 * These define which LEDC resources the buzzer uses.
 * 
 * RESOLUTION: 10-bit = 1024 duty steps (0-1023).
 *     More bits = finer volume control but lower max frequency.
 *     10-bit with 80MHz clock: max ~78kHz (way above audible 20kHz).
 * 
 * LOW_SPEED_MODE: Works on ALL ESP32 variants.
 *     HIGH_SPEED_MODE only exists on the original ESP32 (not S3/C6).
 */
#define BUZZER_LEDC_TIMER       LEDC_TIMER_0
#define BUZZER_LEDC_CHANNEL     LEDC_CHANNEL_0
#define BUZZER_LEDC_MODE        LEDC_LOW_SPEED_MODE
#define BUZZER_DUTY_RESOLUTION  LEDC_TIMER_10_BIT
#define BUZZER_MAX_DUTY         ((1 << 10) - 1)     /* 1023 */
#define BUZZER_DEFAULT_FREQ_HZ  1000

/*
 * FreeRTOS task parameters for background sound playback.
 */
#define BUZZER_TASK_STACK       2048
#define BUZZER_TASK_PRIORITY    2


/*
 * =============================================================================
 * INTERNAL STRUCTS: PARAMETERS FOR BACKGROUND TASKS
 * =============================================================================
 * 
 * These are heap-allocated, passed to tasks, and freed by the tasks.
 * They include a pointer back to the Buzzer instance so the static
 * task functions can access instance methods.
 */

struct SweepParams {
    Buzzer  *self;          // Pointer back to the Buzzer instance
    uint32_t startHz;
    uint32_t endHz;
    uint32_t durationMs;
    uint8_t  volume;
    uint32_t stepMs;
};

struct MelodyParams {
    Buzzer     *self;       // Pointer back to the Buzzer instance
    BuzzerNote *notes;      // Heap-allocated copy of the melody
    int         count;
    uint16_t    gapMs;
};

/*
 * Simple struct to pass both 'this' pointer and duration to tone task.
 */
struct ToneParams {
    Buzzer  *self;
    uint32_t durationMs;
};


/* ============================= Constructor / Destructor ============================= */

/**
 * @brief Construct a Buzzer instance.
 *
 * @param pin GPIO pin number.
 */

/*
 * =============================================================================
 * CONSTRUCTOR
 * =============================================================================
 * 
 * Just stores the pin number. Hardware setup happens in init().
 * Same pattern as the Button and Encoder components.
 */
Buzzer::Buzzer(gpio_num_t pin)
    : pin(pin),
      initialized(false),
      taskHandle(NULL),
      mutex(NULL)
{
    // Nothing else - init() sets up hardware
}


/**
 * @brief Destructor.
 */

/*
 * =============================================================================
 * DESTRUCTOR
 * =============================================================================
 * 
 * Stops any playing sound and cleans up the mutex.
 * The LEDC channel is left as-is (GPIO goes to default state).
 */
Buzzer::~Buzzer() {
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
 * Sets up two things in the LEDC peripheral:
 * 
 * 1. TIMER: Generates the base clock that determines PWM frequency.
 *    We set it to 1kHz initially - tone() changes it as needed.
 *    
 *        Timer 0
 *        ┌─────────────┐
 *        │ 80MHz clock  │
 *        │ ÷ divider    │──► frequency
 *        │ 10-bit count │
 *        └─────────────┘
 * 
 * 2. CHANNEL: Connects a GPIO pin to the timer and sets duty cycle.
 *    
 *        Channel 0
 *        ┌──────────────┐
 *        │ Timer 0 ──►  │
 *        │ Duty cycle   │──► GPIO pin ──► Buzzer
 *        │ Compare      │
 *        └──────────────┘
 */
void Buzzer::init() {
    ESP_LOGI(TAG, "Initializing buzzer on GPIO %d", pin);

    /*
     * Create mutex for thread-safe access.
     */
    mutex = xSemaphoreCreateMutex();
    if (mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return;
    }

    /*
     * Configure LEDC timer.
     * 
     * clk_cfg = LEDC_AUTO_CLK: Let ESP-IDF pick the best clock source
     * automatically. On ESP32 it'll use APB_CLK (80MHz), on others
     * it picks whatever's available.
     */
    ledc_timer_config_t timerCfg = {};
    timerCfg.speed_mode      = BUZZER_LEDC_MODE;
    timerCfg.timer_num       = BUZZER_LEDC_TIMER;
    timerCfg.duty_resolution = BUZZER_DUTY_RESOLUTION;
    timerCfg.freq_hz         = BUZZER_DEFAULT_FREQ_HZ;
    timerCfg.clk_cfg         = LEDC_AUTO_CLK;

    esp_err_t ret = ledc_timer_config(&timerCfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LEDC timer config failed: %s", esp_err_to_name(ret));
        return;
    }

    /*
     * Configure LEDC channel.
     * duty = 0 means no output initially (buzzer silent until tone() is called).
     */
    ledc_channel_config_t channelCfg = {};
    channelCfg.speed_mode = BUZZER_LEDC_MODE;
    channelCfg.channel    = BUZZER_LEDC_CHANNEL;
    channelCfg.timer_sel  = BUZZER_LEDC_TIMER;
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
    ESP_LOGI(TAG, "Buzzer initialized on GPIO %d", pin);
}


/* ============================= Internal Helpers ============================= */

/**
 * @brief Convert volume percentage to LEDC duty cycle.
 *
 * @param volume 0-100%.
 * @return LEDC duty value.
 */

/*
 * =============================================================================
 * VOLUME → DUTY CONVERSION
 * =============================================================================
 * 
 * Piezo buzzers are loudest at 50% duty cycle because that gives
 * the maximum AC voltage swing across the element:
 * 
 *     50% duty:  ┌────┐    ┌────┐       ← Max swing = loudest
 *                │    │    │    │
 *                ┘    └────┘    └────
 * 
 *     10% duty:  ┌┐      ┌┐              ← Small swing = quiet
 *                ││      ││
 *                ┘└──────┘└──────
 * 
 *     90% duty:  ┌──────┐┌──────┐        ← Also small swing = quiet!
 *                │      ││      │
 *                ┘      └┘      └
 * 
 * So "100% volume" maps to 50% duty (not 100% duty).
 * Formula: duty = (volume / 100) * (maxDuty / 2)
 */
uint32_t Buzzer::volumeToDuty(uint8_t volume) {
    if (volume == 0) return 0;
    if (volume > 100) volume = 100;
    return (uint32_t)volume * (BUZZER_MAX_DUTY / 2) / 100;
}


/**
 * @brief Set LEDC output (frequency and duty).
 *
 * @param frequencyHz Frequency (0 = silence).
 * @param duty LEDC duty value (0 = silence).
 */

/*
 * =============================================================================
 * SET OUTPUT - THE FUNCTION THAT ACTUALLY MAKES SOUND
 * =============================================================================
 * 
 * Everything in this driver eventually calls this function.
 * It talks directly to the LEDC hardware:
 * 
 *     1. Set timer frequency (pitch)
 *     2. Set channel duty (volume)
 *     3. Update (commit changes to hardware)
 */
void Buzzer::setOutput(uint32_t frequencyHz, uint32_t duty) {
    if (frequencyHz == 0 || duty == 0) {
        /* Silence: zero duty = no PWM output */
        ledc_set_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL, 0);
        ledc_update_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL);
        return;
    }

    ledc_set_freq(BUZZER_LEDC_MODE, BUZZER_LEDC_TIMER, frequencyHz);
    ledc_set_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL, duty);
    ledc_update_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL);
}


/**
 * @brief Kill current background task and silence.
 */

/*
 * =============================================================================
 * KILL CURRENT TASK
 * =============================================================================
 * 
 * Called before starting any new sound. Ensures only one sound
 * plays at a time.
 * 
 * vTaskDelete() immediately kills the task. Any heap memory the
 * task allocated (sweep params, melody notes) will leak, but this
 * is a rare edge case and the amounts are tiny (< 100 bytes).
 */
void Buzzer::killCurrentTask() {
    if (taskHandle != NULL) {
        vTaskDelete(taskHandle);
        taskHandle = NULL;
    }
    setOutput(0, 0);
}


/* ============================= Background Tasks ============================= */

/**
 * @brief Tone task: waits for duration then silences.
 */

/*
 * =============================================================================
 * TONE TASK
 * =============================================================================
 * 
 * The simplest background task. Just sleeps for the duration,
 * then stops the sound and deletes itself.
 */
void Buzzer::toneTask(void *pvParameters) {
    ToneParams *params = (ToneParams *)pvParameters;
    Buzzer *self = params->self;
    uint32_t durationMs = params->durationMs;
    delete params;

    vTaskDelay(pdMS_TO_TICKS(durationMs));

    xSemaphoreTake(self->mutex, portMAX_DELAY);
    self->setOutput(0, 0);
    self->taskHandle = NULL;
    xSemaphoreGive(self->mutex);

    vTaskDelete(NULL);
}


/**
 * @brief Sweep task: steps through frequencies using log interpolation.
 */

/*
 * =============================================================================
 * SWEEP TASK
 * =============================================================================
 * 
 * Logarithmic sweep: f(t) = start * (end/start)^(t/T)
 * 
 * Why logarithmic instead of linear?
 * 
 *     LINEAR SWEEP (sounds wrong):
 *         100Hz → 200Hz  = +100Hz (doubles, one octave up)
 *         200Hz → 300Hz  = +100Hz (only 50% increase, half octave)
 *         Same Hz change but DIFFERENT perceived pitch change!
 *     
 *     LOG SWEEP (sounds musical):
 *         100Hz → 200Hz  = x2 (one octave)
 *         200Hz → 400Hz  = x2 (one octave)
 *         Same ratio = same perceived pitch change!
 * 
 * powf(ratio, t) does the exponential interpolation.
 */
void Buzzer::sweepTask(void *pvParameters) {
    SweepParams *params = (SweepParams *)pvParameters;
    Buzzer *self = params->self;

    uint32_t duty = self->volumeToDuty(params->volume);
    uint32_t steps = params->durationMs / params->stepMs;
    float ratio = (float)params->endHz / (float)params->startHz;

    for (uint32_t i = 0; i <= steps; i++) {
        float t = (float)i / (float)steps;
        float freq = (float)params->startHz * powf(ratio, t);
        self->setOutput((uint32_t)freq, duty);
        vTaskDelay(pdMS_TO_TICKS(params->stepMs));
    }

    delete params;

    xSemaphoreTake(self->mutex, portMAX_DELAY);
    self->setOutput(0, 0);
    self->taskHandle = NULL;
    xSemaphoreGive(self->mutex);

    vTaskDelete(NULL);
}


/**
 * @brief Melody task: plays notes in sequence.
 */

/*
 * =============================================================================
 * MELODY TASK
 * =============================================================================
 * 
 * Iterates through the notes array:
 * 
 *     For each note:
 *         1. If frequency > 0: play note at its volume
 *            If frequency == 0: silence (rest)
 *         2. Wait for the note's duration
 *         3. Brief silence gap between notes (articulation)
 * 
 * The gap between notes is what makes it sound like separate
 * notes vs one continuous tone. Even 10-20ms makes a difference.
 */
void Buzzer::melodyTask(void *pvParameters) {
    MelodyParams *params = (MelodyParams *)pvParameters;
    Buzzer *self = params->self;

    for (int i = 0; i < params->count; i++) {
        BuzzerNote *note = &params->notes[i];

        if (note->frequencyHz == 0 || note->volume == 0) {
            self->setOutput(0, 0);
        } else {
            uint32_t duty = self->volumeToDuty(note->volume);
            self->setOutput(note->frequencyHz, duty);
        }

        vTaskDelay(pdMS_TO_TICKS(note->durationMs));

        /* Gap between notes (silence for articulation) */
        if (params->gapMs > 0 && i < params->count - 1) {
            self->setOutput(0, 0);
            vTaskDelay(pdMS_TO_TICKS(params->gapMs));
        }
    }

    delete[] params->notes;
    delete params;

    xSemaphoreTake(self->mutex, portMAX_DELAY);
    self->setOutput(0, 0);
    self->taskHandle = NULL;
    xSemaphoreGive(self->mutex);

    vTaskDelete(NULL);
}


/* ============================= Public API: Core ============================= */

/**
 * @brief Play a tone.
 */

/*
 * =============================================================================
 * tone() - PLAY A FREQUENCY FOR A DURATION
 * =============================================================================
 * 
 * Steps:
 *     1. Kill any currently playing sound
 *     2. Set the LEDC output to the requested frequency and volume
 *     3. If duration > 0, start a background task to stop it later
 *     4. If duration == 0, play forever until stop() is called
 */
void Buzzer::tone(uint32_t frequencyHz, uint32_t durationMs, uint8_t volume) {
    if (!initialized) {
        ESP_LOGE(TAG, "Not initialized - call init() first");
        return;
    }

    xSemaphoreTake(mutex, portMAX_DELAY);
    killCurrentTask();

    if (frequencyHz == 0 || volume == 0) {
        xSemaphoreGive(mutex);
        return;
    }

    uint32_t duty = volumeToDuty(volume);
    setOutput(frequencyHz, duty);

    if (durationMs > 0) {
        ToneParams *params = new ToneParams{this, durationMs};
        BaseType_t ret = xTaskCreate(
            toneTask, "buz_tone", BUZZER_TASK_STACK,
            params, BUZZER_TASK_PRIORITY, &taskHandle
        );
        if (ret != pdPASS) {
            ESP_LOGE(TAG, "Failed to create tone task");
            setOutput(0, 0);
            delete params;
        }
    }

    xSemaphoreGive(mutex);
}


/**
 * @brief Stop sound.
 */

/*
 * =============================================================================
 * stop() - SILENCE IMMEDIATELY
 * =============================================================================
 * 
 * Kills the background task and sets duty to 0.
 * Safe to call even if nothing is playing.
 */
void Buzzer::stop() {
    if (!initialized) return;

    xSemaphoreTake(mutex, portMAX_DELAY);
    killCurrentTask();
    xSemaphoreGive(mutex);
}


/**
 * @brief Logarithmic frequency sweep.
 */

/*
 * =============================================================================
 * sweepLog() - SMOOTH FREQUENCY TRANSITION
 * =============================================================================
 * 
 * Allocates sweep parameters on the heap, creates a background task
 * that steps through frequencies. The task frees the params when done.
 * 
 * Extra stack (+1024) allocated for the powf() floating-point math.
 */
void Buzzer::sweepLog(uint32_t startHz, uint32_t endHz, uint32_t durationMs,
                      uint8_t volume, uint32_t stepMs) {
    if (!initialized) {
        ESP_LOGE(TAG, "Not initialized - call init() first");
        return;
    }
    if (startHz == 0 || endHz == 0 || durationMs == 0 || stepMs == 0) {
        ESP_LOGE(TAG, "Sweep parameters cannot be zero");
        return;
    }

    SweepParams *params = new SweepParams{this, startHz, endHz, durationMs, volume, stepMs};

    xSemaphoreTake(mutex, portMAX_DELAY);
    killCurrentTask();

    BaseType_t ret = xTaskCreate(
        sweepTask, "buz_sweep", BUZZER_TASK_STACK + 1024,
        params, BUZZER_TASK_PRIORITY, &taskHandle
    );
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create sweep task");
        delete params;
    }

    xSemaphoreGive(mutex);
}


/* ============================= Public API: Presets ============================= */

/**
 * @brief Quick beep.
 */

/*
 * =============================================================================
 * PRESET: BEEP
 * =============================================================================
 * 
 * Short, sharp 2kHz tone for 80ms at moderate volume.
 * Good for: button press feedback, menu selection, notification tick.
 */
void Buzzer::beep() {
    tone(2000, 80, 50);
}


/**
 * @brief R2D2 chirp.
 */

/*
 * =============================================================================
 * PRESET: CHIRP
 * =============================================================================
 * 
 * Rising then falling frequency sweep, like R2D2 beeping.
 * Built from discrete frequency steps to simulate a smooth chirp.
 * 
 *     Frequency:
 *     5000 │        ╱╲
 *     4000 │      ╱    ╲
 *     2500 │    ╱        ╲
 *     1500 │  ╱            ╲
 *      800 │╱                ╲
 *      400 ├──────────────────╲──
 *          0   100  200  300  400 ms
 */
void Buzzer::chirp() {
    static const BuzzerNote notes[] = {
        { 400,  30, 55 },
        { 800,  30, 55 },
        { 1500, 30, 60 },
        { 2500, 30, 65 },
        { 4000, 40, 65 },
        { 5000, 50, 60 },   /* Peak */
        { 4000, 40, 60 },
        { 2500, 30, 55 },
        { 1500, 30, 55 },
        { 800,  30, 50 },
        { 400,  30, 45 },
    };
    playMelody(notes, sizeof(notes) / sizeof(notes[0]), 0);
}


/**
 * @brief Two-tone alarm.
 */

/*
 * =============================================================================
 * PRESET: ALARM
 * =============================================================================
 * 
 * European-style two-tone siren: alternates 900Hz and 1200Hz.
 * Six tones, ~1200ms total. Think ambulance but less annoying.
 * 
 *     Frequency:
 *     1200 │  ┌──┐  ┌──┐  ┌──┐
 *          │  │  │  │  │  │  │
 *      900 │──┘  └──┘  └──┘  └──
 *          0  200 400 600 800 1200 ms
 */
void Buzzer::alarm() {
    static const BuzzerNote notes[] = {
        { 900,  200, 70 },
        { 1200, 200, 70 },
        { 900,  200, 70 },
        { 1200, 200, 70 },
        { 900,  200, 70 },
        { 1200, 200, 70 },
    };
    playMelody(notes, sizeof(notes) / sizeof(notes[0]), 0);
}


/**
 * @brief Success sound.
 */

/*
 * =============================================================================
 * PRESET: SUCCESS
 * =============================================================================
 * 
 * Ascending major triad: C5 → E5 → G5
 * 
 *     Music theory:
 *         C5 (523Hz) = root
 *         E5 (659Hz) = major third (happy interval)
 *         G5 (784Hz) = perfect fifth (satisfying resolution)
 * 
 * Sounds like: "da-da-DA!" (level up, achievement unlocked)
 */
void Buzzer::success() {
    static const BuzzerNote notes[] = {
        { 523, 120, 55 },   /* C5 */
        { 659, 120, 55 },   /* E5 */
        { 784, 160, 60 },   /* G5 (slightly longer for emphasis) */
    };
    playMelody(notes, sizeof(notes) / sizeof(notes[0]), 15);
}


/**
 * @brief Error sound.
 */

/*
 * =============================================================================
 * PRESET: ERROR
 * =============================================================================
 * 
 * Descending minor pattern: G4 → Eb4 → C4
 * 
 *     Music theory:
 *         G4  (392Hz) = start high
 *         Eb4 (311Hz) = minor third (sad interval)
 *         C4  (262Hz) = resolve low
 * 
 * Sounds like: "bah-bah-buhh" (game over, connection lost)
 */
void Buzzer::error() {
    static const BuzzerNote notes[] = {
        { 392, 150, 55 },   /* G4  */
        { 311, 150, 55 },   /* Eb4 */
        { 262, 200, 50 },   /* C4  */
    };
    playMelody(notes, sizeof(notes) / sizeof(notes[0]), 15);
}


/**
 * @brief UI click.
 */

/*
 * =============================================================================
 * PRESET: CLICK
 * =============================================================================
 * 
 * Ultra-short high-frequency tick (6kHz, 15ms).
 * At 15ms it's too short to perceive as a pitch - just a "tick".
 * Good for: rotary encoder detents, list scrolling, subtle feedback.
 */
void Buzzer::click() {
    tone(6000, 15, 35);
}


/* ============================= Public API: Advanced ============================= */

/**
 * @brief Play a melody.
 */

/*
 * =============================================================================
 * playMelody() - SEQUENCE OF NOTES
 * =============================================================================
 * 
 * Copies the notes array to the heap (so caller can free/reuse theirs),
 * then creates a background task to play through them.
 * 
 * The task plays each note for its duration, with optional silence
 * gaps between notes for articulation.
 */
void Buzzer::playMelody(const BuzzerNote *notes, int count, uint16_t gapMs) {
    if (!initialized) {
        ESP_LOGE(TAG, "Not initialized - call init() first");
        return;
    }
    if (notes == NULL || count <= 0) {
        ESP_LOGE(TAG, "Invalid melody (null or empty)");
        return;
    }

    /*
     * Copy notes to heap. The background task will free this when done.
     * We copy because the caller's array might be on the stack or
     * could be reused before the task finishes playing.
     */
    BuzzerNote *notesCopy = new BuzzerNote[count];
    memcpy(notesCopy, notes, sizeof(BuzzerNote) * count);

    MelodyParams *params = new MelodyParams{this, notesCopy, count, gapMs};

    xSemaphoreTake(mutex, portMAX_DELAY);
    killCurrentTask();

    BaseType_t ret = xTaskCreate(
        melodyTask, "buz_melody", BUZZER_TASK_STACK,
        params, BUZZER_TASK_PRIORITY, &taskHandle
    );
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create melody task");
        delete[] notesCopy;
        delete params;
    }

    xSemaphoreGive(mutex);
}
