/**
 * @file font_5x7.h
 * @brief Shared 5x7 pixel font for display drivers.
 *
 * @details
 * This header contains a single copy of the 5x7 ASCII font used by
 * multiple display drivers (SSD1306, GC9A01, ILI9341, ST7789, SSD1357, EPaper).
 * 
 * Previously each driver had its own copy (~475 bytes each), wasting ~2.8KB flash.
 * Now all drivers share this single definition.
 *
 * @note
 * Uses 'inline' to allow inclusion in multiple translation units without
 * linker "multiple definition" errors. The compiler/linker will merge them.
 *
 * @par Font Details
 * - Character size: 5 pixels wide × 7 pixels tall
 * - ASCII range: 32-126 (printable characters)
 * - Format: Column-major (each byte is one column, LSB at top)
 * - Total size: 95 characters × 5 bytes = 475 bytes
 *
 * @par Usage
 * @code
 * #include "font_5x7.h"
 * 
 * void drawChar(char c) {
 *     if (c < 32 || c > 126) return;
 *     const uint8_t* glyph = &FONT_5X7[(c - 32) * 5];
 *     for (int col = 0; col < 5; col++) {
 *         uint8_t columnData = glyph[col];
 *         for (int row = 0; row < 7; row++) {
 *             if (columnData & (1 << row)) {
 *                 drawPixel(x + col, y + row);
 *             }
 *         }
 *     }
 * }
 * @endcode
 */

/*
 * =============================================================================
 * BEGINNER'S GUIDE: HOW BITMAP FONTS WORK
 * =============================================================================
 * 
 * Unlike TrueType fonts (smooth curves), bitmap fonts store each character
 * as a grid of pixels. Simple but fast - perfect for microcontrollers.
 * 
 * =============================================================================
 * THE 5×7 FORMAT
 * =============================================================================
 * 
 * Each character is 5 columns wide and 7 rows tall:
 * 
 *     Example: Letter 'A'
 *     
 *         Col:  0   1   2   3   4
 *              ─────────────────────
 *     Row 0:    ·   ·   █   ·   ·      ← Top
 *     Row 1:    ·   █   ·   █   ·
 *     Row 2:    █   ·   ·   ·   █
 *     Row 3:    █   █   █   █   █      ← Middle bar
 *     Row 4:    █   ·   ·   ·   █
 *     Row 5:    █   ·   ·   ·   █
 *     Row 6:    ·   ·   ·   ·   ·      ← Bottom (usually empty for descenders)
 * 
 * =============================================================================
 * COLUMN-MAJOR ENCODING
 * =============================================================================
 * 
 * We store ONE BYTE per column. Each bit represents one row:
 * 
 *     Column 0 of 'A':
 *     
 *         Row 0: 0 (off)   ─┐
 *         Row 1: 0 (off)    │
 *         Row 2: 1 (ON)     │  These 7 bits become
 *         Row 3: 1 (ON)     ├─ one byte: 0b01111100
 *         Row 4: 1 (ON)     │  = 0x7C
 *         Row 5: 1 (ON)     │
 *         Row 6: 0 (off)   ─┘
 *     
 *     So column 0 = 0x7C
 * 
 * The full 'A' character (5 bytes):
 * 
 *     { 0x7C, 0x12, 0x11, 0x12, 0x7C }
 *       col0  col1  col2  col3  col4
 * 
 * =============================================================================
 * WHY COLUMN-MAJOR?
 * =============================================================================
 * 
 * Most displays write left-to-right, top-to-bottom. With column-major:
 * 
 *     for (col = 0; col < 5; col++) {
 *         byte = font[char][col];     // Get whole column at once
 *         for (row = 0; row < 7; row++) {
 *             if (byte & (1 << row))  // Check each bit
 *                 setPixel(x+col, y+row);
 *         }
 *     }
 * 
 * This is cache-friendly and branch-predictable.
 * 
 * =============================================================================
 * WHY 'inline' KEYWORD?
 * =============================================================================
 * 
 * Problem: If multiple .cpp files include this header, they each get a
 * copy of the array. Normally the linker would complain:
 * 
 *     "multiple definition of FONT_5X7"
 * 
 * Solution: 'inline' tells the compiler/linker "this might appear multiple
 * times, just pick one copy". Works in C++17 and later.
 * 
 * For C++11/14, you could use:
 *     static const uint8_t FONT_5X7[] = { ... };
 * But 'static' means each file gets its OWN copy (wastes memory).
 * 
 * =============================================================================
 */

#pragma once

#include <stdint.h>

/**
 * @brief 5x7 pixel ASCII font (characters 32-126).
 * 
 * @details
 * 95 characters × 5 bytes = 475 bytes total.
 * Access: FONT_5X7[(char - 32) * 5] gives pointer to character's 5 columns.
 */
inline const uint8_t FONT_5X7[] = {
    // ASCII 32: Space
    0x00, 0x00, 0x00, 0x00, 0x00,
    // ASCII 33: !
    0x00, 0x00, 0x5F, 0x00, 0x00,
    // ASCII 34: "
    0x00, 0x07, 0x00, 0x07, 0x00,
    // ASCII 35: #
    0x14, 0x7F, 0x14, 0x7F, 0x14,
    // ASCII 36: $
    0x24, 0x2A, 0x7F, 0x2A, 0x12,
    // ASCII 37: %
    0x23, 0x13, 0x08, 0x64, 0x62,
    // ASCII 38: &
    0x36, 0x49, 0x55, 0x22, 0x50,
    // ASCII 39: '
    0x00, 0x05, 0x03, 0x00, 0x00,
    // ASCII 40: (
    0x00, 0x1C, 0x22, 0x41, 0x00,
    // ASCII 41: )
    0x00, 0x41, 0x22, 0x1C, 0x00,
    // ASCII 42: *
    0x08, 0x2A, 0x1C, 0x2A, 0x08,
    // ASCII 43: +
    0x08, 0x08, 0x3E, 0x08, 0x08,
    // ASCII 44: ,
    0x00, 0x50, 0x30, 0x00, 0x00,
    // ASCII 45: -
    0x08, 0x08, 0x08, 0x08, 0x08,
    // ASCII 46: .
    0x00, 0x60, 0x60, 0x00, 0x00,
    // ASCII 47: /
    0x20, 0x10, 0x08, 0x04, 0x02,
    // ASCII 48: 0
    0x3E, 0x51, 0x49, 0x45, 0x3E,
    // ASCII 49: 1
    0x00, 0x42, 0x7F, 0x40, 0x00,
    // ASCII 50: 2
    0x42, 0x61, 0x51, 0x49, 0x46,
    // ASCII 51: 3
    0x21, 0x41, 0x45, 0x4B, 0x31,
    // ASCII 52: 4
    0x18, 0x14, 0x12, 0x7F, 0x10,
    // ASCII 53: 5
    0x27, 0x45, 0x45, 0x45, 0x39,
    // ASCII 54: 6
    0x3C, 0x4A, 0x49, 0x49, 0x30,
    // ASCII 55: 7
    0x01, 0x71, 0x09, 0x05, 0x03,
    // ASCII 56: 8
    0x36, 0x49, 0x49, 0x49, 0x36,
    // ASCII 57: 9
    0x06, 0x49, 0x49, 0x29, 0x1E,
    // ASCII 58: :
    0x00, 0x36, 0x36, 0x00, 0x00,
    // ASCII 59: ;
    0x00, 0x56, 0x36, 0x00, 0x00,
    // ASCII 60: <
    0x00, 0x08, 0x14, 0x22, 0x41,
    // ASCII 61: =
    0x14, 0x14, 0x14, 0x14, 0x14,
    // ASCII 62: >
    0x41, 0x22, 0x14, 0x08, 0x00,
    // ASCII 63: ?
    0x02, 0x01, 0x51, 0x09, 0x06,
    // ASCII 64: @
    0x32, 0x49, 0x79, 0x41, 0x3E,
    // ASCII 65: A
    0x7E, 0x11, 0x11, 0x11, 0x7E,
    // ASCII 66: B
    0x7F, 0x49, 0x49, 0x49, 0x36,
    // ASCII 67: C
    0x3E, 0x41, 0x41, 0x41, 0x22,
    // ASCII 68: D
    0x7F, 0x41, 0x41, 0x22, 0x1C,
    // ASCII 69: E
    0x7F, 0x49, 0x49, 0x49, 0x41,
    // ASCII 70: F
    0x7F, 0x09, 0x09, 0x01, 0x01,
    // ASCII 71: G
    0x3E, 0x41, 0x41, 0x51, 0x32,
    // ASCII 72: H
    0x7F, 0x08, 0x08, 0x08, 0x7F,
    // ASCII 73: I
    0x00, 0x41, 0x7F, 0x41, 0x00,
    // ASCII 74: J
    0x20, 0x40, 0x41, 0x3F, 0x01,
    // ASCII 75: K
    0x7F, 0x08, 0x14, 0x22, 0x41,
    // ASCII 76: L
    0x7F, 0x40, 0x40, 0x40, 0x40,
    // ASCII 77: M
    0x7F, 0x02, 0x04, 0x02, 0x7F,
    // ASCII 78: N
    0x7F, 0x04, 0x08, 0x10, 0x7F,
    // ASCII 79: O
    0x3E, 0x41, 0x41, 0x41, 0x3E,
    // ASCII 80: P
    0x7F, 0x09, 0x09, 0x09, 0x06,
    // ASCII 81: Q
    0x3E, 0x41, 0x51, 0x21, 0x5E,
    // ASCII 82: R
    0x7F, 0x09, 0x19, 0x29, 0x46,
    // ASCII 83: S
    0x46, 0x49, 0x49, 0x49, 0x31,
    // ASCII 84: T
    0x01, 0x01, 0x7F, 0x01, 0x01,
    // ASCII 85: U
    0x3F, 0x40, 0x40, 0x40, 0x3F,
    // ASCII 86: V
    0x1F, 0x20, 0x40, 0x20, 0x1F,
    // ASCII 87: W
    0x7F, 0x20, 0x18, 0x20, 0x7F,
    // ASCII 88: X
    0x63, 0x14, 0x08, 0x14, 0x63,
    // ASCII 89: Y
    0x03, 0x04, 0x78, 0x04, 0x03,
    // ASCII 90: Z
    0x61, 0x51, 0x49, 0x45, 0x43,
    // ASCII 91: [
    0x00, 0x00, 0x7F, 0x41, 0x41,
    // ASCII 92: backslash
    0x02, 0x04, 0x08, 0x10, 0x20,
    // ASCII 93: ]
    0x41, 0x41, 0x7F, 0x00, 0x00,
    // ASCII 94: ^
    0x04, 0x02, 0x01, 0x02, 0x04,
    // ASCII 95: _
    0x40, 0x40, 0x40, 0x40, 0x40,
    // ASCII 96: `
    0x00, 0x01, 0x02, 0x04, 0x00,
    // ASCII 97: a
    0x20, 0x54, 0x54, 0x54, 0x78,
    // ASCII 98: b
    0x7F, 0x48, 0x44, 0x44, 0x38,
    // ASCII 99: c
    0x38, 0x44, 0x44, 0x44, 0x20,
    // ASCII 100: d
    0x38, 0x44, 0x44, 0x48, 0x7F,
    // ASCII 101: e
    0x38, 0x54, 0x54, 0x54, 0x18,
    // ASCII 102: f
    0x08, 0x7E, 0x09, 0x01, 0x02,
    // ASCII 103: g
    0x08, 0x14, 0x54, 0x54, 0x3C,
    // ASCII 104: h
    0x7F, 0x08, 0x04, 0x04, 0x78,
    // ASCII 105: i
    0x00, 0x44, 0x7D, 0x40, 0x00,
    // ASCII 106: j
    0x20, 0x40, 0x44, 0x3D, 0x00,
    // ASCII 107: k
    0x00, 0x7F, 0x10, 0x28, 0x44,
    // ASCII 108: l
    0x00, 0x41, 0x7F, 0x40, 0x00,
    // ASCII 109: m
    0x7C, 0x04, 0x18, 0x04, 0x78,
    // ASCII 110: n
    0x7C, 0x08, 0x04, 0x04, 0x78,
    // ASCII 111: o
    0x38, 0x44, 0x44, 0x44, 0x38,
    // ASCII 112: p
    0x7C, 0x14, 0x14, 0x14, 0x08,
    // ASCII 113: q
    0x08, 0x14, 0x14, 0x18, 0x7C,
    // ASCII 114: r
    0x7C, 0x08, 0x04, 0x04, 0x08,
    // ASCII 115: s
    0x48, 0x54, 0x54, 0x54, 0x20,
    // ASCII 116: t
    0x04, 0x3F, 0x44, 0x40, 0x20,
    // ASCII 117: u
    0x3C, 0x40, 0x40, 0x20, 0x7C,
    // ASCII 118: v
    0x1C, 0x20, 0x40, 0x20, 0x1C,
    // ASCII 119: w
    0x3C, 0x40, 0x30, 0x40, 0x3C,
    // ASCII 120: x
    0x44, 0x28, 0x10, 0x28, 0x44,
    // ASCII 121: y
    0x0C, 0x50, 0x50, 0x50, 0x3C,
    // ASCII 122: z
    0x44, 0x64, 0x54, 0x4C, 0x44,
    // ASCII 123: {
    0x00, 0x08, 0x36, 0x41, 0x00,
    // ASCII 124: |
    0x00, 0x00, 0x7F, 0x00, 0x00,
    // ASCII 125: }
    0x00, 0x41, 0x36, 0x08, 0x00,
    // ASCII 126: ~
    0x08, 0x08, 0x2A, 0x1C, 0x08,
};

/**
 * @brief Font width in pixels.
 */
inline constexpr uint8_t FONT_5X7_WIDTH = 5;

/**
 * @brief Font height in pixels.
 */
inline constexpr uint8_t FONT_5X7_HEIGHT = 7;

/**
 * @brief First ASCII character in font (space).
 */
inline constexpr uint8_t FONT_5X7_FIRST_CHAR = 32;

/**
 * @brief Last ASCII character in font (tilde).
 */
inline constexpr uint8_t FONT_5X7_LAST_CHAR = 126;
