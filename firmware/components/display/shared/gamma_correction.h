/**
 * @file gamma_correction.h
 * @brief Shared gamma correction table for LED/PWM drivers.
 *
 * @details
 * This header contains gamma correction lookup tables used by multiple
 * output drivers (PWM dimmer, MOSFET driver, addressable LEDs).
 * 
 * Previously each driver had its own copy (~200 bytes each), wasting flash.
 * Now all drivers share this single definition.
 *
 * @par Why Gamma Correction?
 * Human eyes perceive brightness logarithmically, not linearly.
 * Without gamma correction:
 *   - 50% duty cycle looks like ~75% brightness
 *   - Low brightness levels are barely distinguishable
 *   - Fades look uneven (fast at bottom, slow at top)
 * 
 * With gamma correction (γ = 2.2):
 *   - Perceived brightness matches the input value
 *   - Smooth, natural-looking fades
 *   - Fine control at low brightness levels
 *
 * @par Usage
 * @code
 * #include "gamma_correction.h"
 * 
 * // For 0-100% input, 10-bit PWM output:
 * uint8_t brightness = 50;  // 50%
 * uint32_t duty = GAMMA_TABLE_100[brightness] * maxDuty / 1000;
 * 
 * // For 0-255 input, get 0-255 corrected output:
 * uint8_t linear = 128;
 * uint8_t corrected = GAMMA_TABLE_256[linear];
 * @endcode
 */

/*
 * =============================================================================
 * BEGINNER'S GUIDE: GAMMA CORRECTION
 * =============================================================================
 * 
 * WHY DO WE NEED THIS?
 * ~~~~~~~~~~~~~~~~~~~~
 * 
 * LEDs emit light proportional to current (which is proportional to duty cycle).
 * But human eyes don't perceive light linearly!
 * 
 *     What the LED does:          What your eye sees:
 *     
 *     Duty│                       Perceived│
 *     100%│          ╱            Brightness│      ╱───
 *      75%│        ╱              100%│    ╱
 *      50%│      ╱                 75%│  ╱
 *      25%│    ╱                   50%│╱
 *       0%│__╱                     25%│
 *         └────────────              └────────────
 *           0%  50%  100%              0%  50%  100%
 *           Input                      Input
 *     
 *     Linear duty cycle           Eye perceives it as
 *     (what you set)              "already bright" at low values
 * 
 * THE SOLUTION: Gamma Correction
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * 
 * We apply the INVERSE of the eye's response curve:
 * 
 *     output = input^gamma    (where gamma ≈ 2.2)
 * 
 *     Input │ Linear │ Gamma (2.2) │ Perceived
 *     ──────┼────────┼─────────────┼──────────
 *       10% │   10%  │     1%      │    10%  ← Now matches!
 *       25% │   25%  │     5%      │    25%
 *       50% │   50%  │    22%      │    50%
 *       75% │   75%  │    53%      │    75%
 *      100% │  100%  │   100%      │   100%
 * 
 * The low duty cycles (1%, 5%, 22%) LOOK like 10%, 25%, 50% to your eye!
 * 
 * =============================================================================
 * THE TWO TABLES
 * =============================================================================
 * 
 * GAMMA_TABLE_100[101]:
 *     - Input: 0-100 (percentage)
 *     - Output: 0-1000 (scaled for easy math)
 *     - Use case: User-facing "brightness %" controls
 *     - To get actual duty: output * maxDuty / 1000
 * 
 * GAMMA_TABLE_256[256]:
 *     - Input: 0-255 (8-bit value)
 *     - Output: 0-255 (8-bit corrected)
 *     - Use case: RGB color values, 8-bit protocols
 *     - Direct replacement for linear values
 * 
 * =============================================================================
 * WHY PRE-COMPUTED TABLES?
 * =============================================================================
 * 
 * Option 1: Calculate at runtime
 *     duty = pow(input/100.0, 2.2) * maxDuty;
 *     
 *     Problems:
 *     - pow() is SLOW (hundreds of cycles)
 *     - Floating point on ESP32 is okay but still slower than integer
 *     - Called potentially thousands of times per second for smooth fades
 * 
 * Option 2: Pre-computed lookup table (what we do)
 *     duty = GAMMA_TABLE_100[input] * maxDuty / 1000;
 *     
 *     Benefits:
 *     - One array lookup + one multiply + one divide
 *     - ~10x faster than pow()
 *     - Consistent timing (no floating point surprises)
 *     - Table is in flash, not RAM
 * 
 * =============================================================================
 */

#pragma once

#include <stdint.h>

/**
 * @brief Gamma correction table for 0-100% input.
 * 
 * @details
 * Pre-calculated with gamma = 2.2.
 * Output scaled to 0-1000 for integer math.
 * 
 * Formula: output = round((input/100)^2.2 * 1000)
 * 
 * Usage:
 * @code
 * uint8_t percent = 50;  // User wants 50% brightness
 * uint32_t duty = GAMMA_TABLE_100[percent] * maxDuty / 1000;
 * @endcode
 */
inline const uint16_t GAMMA_TABLE_100[101] = {
    //  0%   1%   2%   3%   4%   5%   6%   7%   8%   9%
        0,   0,   0,   0,   0,   1,   1,   2,   2,   3,   //   0-9
        4,   5,   6,   8,   9,  11,  13,  15,  18,  20,   //  10-19
       23,  26,  30,  33,  37,  41,  46,  50,  55,  60,   //  20-29
       66,  71,  77,  84,  90,  97, 104, 111, 119, 127,   //  30-39
      135, 144, 153, 162, 171, 181, 191, 202, 212, 224,   //  40-49
      235, 247, 259, 271, 284, 297, 311, 325, 339, 354,   //  50-59
      369, 384, 400, 416, 433, 450, 467, 485, 503, 521,   //  60-69
      540, 560, 579, 600, 620, 641, 663, 684, 707, 729,   //  70-79
      752, 776, 800, 824, 849, 874, 900, 926, 952, 979,   //  80-89
     1006,1034,1062,1091,1120,1150,1180,1210,1241,1272,   //  90-99
     1304                                                  // 100
};

/**
 * @brief Gamma correction table for 8-bit (0-255) input/output.
 * 
 * @details
 * Pre-calculated with gamma = 2.2.
 * Input and output both 0-255.
 * 
 * Formula: output = round((input/255)^2.2 * 255)
 * 
 * Usage:
 * @code
 * uint8_t linearRed = 128;
 * uint8_t correctedRed = GAMMA_TABLE_256[linearRed];
 * @endcode
 */
inline const uint8_t GAMMA_TABLE_256[256] = {
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   1,   1,   1,   1,
      1,   1,   1,   1,   1,   1,   1,   1,   1,   2,   2,   2,   2,   2,   2,   2,
      2,   3,   3,   3,   3,   3,   3,   3,   4,   4,   4,   4,   4,   5,   5,   5,
      5,   6,   6,   6,   6,   7,   7,   7,   7,   8,   8,   8,   9,   9,   9,  10,
     10,  10,  11,  11,  11,  12,  12,  13,  13,  13,  14,  14,  15,  15,  16,  16,
     17,  17,  18,  18,  19,  19,  20,  20,  21,  21,  22,  22,  23,  24,  24,  25,
     25,  26,  27,  27,  28,  29,  29,  30,  31,  32,  32,  33,  34,  35,  35,  36,
     37,  38,  39,  39,  40,  41,  42,  43,  44,  45,  46,  47,  48,  49,  50,  50,
     51,  52,  54,  55,  56,  57,  58,  59,  60,  61,  62,  63,  64,  66,  67,  68,
     69,  70,  72,  73,  74,  75,  77,  78,  79,  81,  82,  83,  85,  86,  87,  89,
     90,  92,  93,  95,  96,  98,  99, 101, 102, 104, 105, 107, 109, 110, 112, 114,
    115, 117, 119, 120, 122, 124, 126, 127, 129, 131, 133, 135, 137, 138, 140, 142,
    144, 146, 148, 150, 152, 154, 156, 158, 160, 162, 164, 167, 169, 171, 173, 175,
    177, 180, 182, 184, 186, 189, 191, 193, 196, 198, 200, 203, 205, 208, 210, 213,
    215, 218, 220, 223, 225, 228, 231, 233, 236, 239, 241, 244, 247, 249, 252, 255
};

/**
 * @brief Apply gamma correction to a 0-100 value.
 * 
 * @param percent Input brightness 0-100.
 * @param maxDuty Maximum PWM duty value (e.g., 1023 for 10-bit).
 * @return Gamma-corrected duty cycle value.
 */
inline uint32_t gammaCorrect100(uint8_t percent, uint32_t maxDuty) {
    if (percent > 100) percent = 100;
    return (uint32_t)GAMMA_TABLE_100[percent] * maxDuty / 1000;
}

/**
 * @brief Apply gamma correction to an 8-bit value.
 * 
 * @param value Input brightness 0-255.
 * @return Gamma-corrected value 0-255.
 */
inline uint8_t gammaCorrect256(uint8_t value) {
    return GAMMA_TABLE_256[value];
}
