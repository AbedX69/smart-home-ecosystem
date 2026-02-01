Dev Log — Getting my ESP32 setup under control

Date: Tuesday, 20/01/2026

**Context / Why I’m doing this**
This isn’t my first day, but I’m trying to get organized and start documenting properly. A lot of this is still new to me.

**What I tried before**
I started with Arduino IDE because it felt beginner-friendly, but it became a pain to manage across multiple ESP32 variants. I have ~10 different boards, and switching + keeping things consistent was annoying.

***My ESP32 variants (current collection):***
@   ESP32-C6
@   ESP32-P4 module (16MB, ESP32-P4NRW32)
@   ESP32-S3-Zero
@   ESP32-S3 Super Mini
@   ESP32-D
@   ESP32-S3 WROOM
@   ESP-32E (relay board)
@   ESP32-C6 LCD 1.47"
@   ESP32-C6 Seeed Studio
@   ESP32-S3 Seeed Studio (LoRa board + antennas)

**Today’s goal**
Before building “real” projects, I wanted a simple baseline test:
blink the onboard LED on each board (just to confirm toolchain + flashing + pins are under control).

***What worked so far***
I successfully blinked onboard LEDs on 3 boards:
# ~~ESP32-D → LED on GPIO 2~~
# ~~ESP32-S3 WROOM → LED on GPIO 2~~
# ~~ESP32-S3 Seeed Studio → LED on GPIO 21~~

**What didn’t work yet**
Most of the other boards still aren’t blinking because I don’t know their correct onboard LED pin.

***Plan for tomorrow***
Get 1–2 more boards blinking (especially the tiny ones).
Figure out what pin the big ESP32-C6 LED is actually wired to.

**Notes / mindset**
Folder structure and project organization still needs work — but I want to first become confident in what I’m doing, not just copy-pasting whatever AI tells me.






##############################################################################################################################################################################################################################################################################################################################################################################################################################





### Dev Log — 21/01/2026 (Wed)

**Goal today:** make the workflow smoother between my main PC (coding) and test PC (flashing/testing).

#### What I did

* Improved my workstation setup:

  * I write code on the **main PC**.
  * I copy the project to the **test PC** over the network.
  * I build/run/flash from the **test PC** on real hardware.
* Ran a quick validation test by changing the LED blink timing from **200ms** to **2000ms**.
* Confirmed it works smoothly on **4 boards**:

  * **ESP32-D**
  * **ESP32-S3 WROOM**
  * **ESP32-C6 Seeed Studio**
  * **ESP32-S3 Seeed Studio**

#### Issues / notes

* PlatformIO setup on the test PC was rough at first — it was downloading/loading a lot of stuff.
* Later I want to optimize this so it starts/builds faster.

#### Content / documentation

* Filmed a short clip of the workflow.
* Next step: edit the video and upload it to YouTube as part of my build/documentation series.

#### Next milestone

* Start working on my **first real component: a rotary encoder**.
* I began creating the project/files for it today.
* Goal: get the rotary encoder working across the same boards listed above.




##############################################################################################################################################################################################################################################################################################################################################################################################################################




# Dev Log — Rotary Encoder Journey

**Date:** 22/01/2026 – 28/01/2026 (with 2-3 day break for work)

**Component:** Rotary Encoder (CLK, DT, SW button)

**Goal:** Get a single rotary encoder working reliably on ALL my ESP32 boards using interrupts.

---

## The Boards I Tested

| Board | Pins Used | Status |
|-------|-----------|--------|
| ESP32D (original) | CLK=18, DT=19, SW=5 | ✅ Works |
| ESP32-S3 WROOM | CLK=18, DT=19, SW=5 | ✅ Works |
| ESP32-S3 Seeed XIAO | CLK=1, DT=2, SW=3 | ✅ Works |
| ESP32-C6 WROOM | CLK=18, DT=19, SW=5 | ✅ Works |
| ESP32-C6 Seeed XIAO | CLK=0, DT=1, SW=2 | ✅ Works |

---

## What I Learned (A LOT)

This wasn't just about getting the encoder working. AI explained a ton of fundamental concepts to me because I'm still learning C++ and embedded programming. Here's what I learned:

### Polling vs Interrupts
- **Polling** = checking the pins over and over in a loop (bad - can miss fast turns)
- **Interrupts** = hardware automatically calls your function when pins change (good - never miss anything)

### ISR (Interrupt Service Routine)
- The function that runs automatically when an interrupt happens
- Must be fast, no logging, no memory allocation
- Must be in IRAM (Internal RAM) using `IRAM_ATTR` so it works even when Flash is busy

### The `this` Keyword
- Points to "myself" - the current object
- Like saying "my position" instead of "some encoder's position"

### Why ISR Must Be Static
- ESP-IDF requires ISR handlers to be static or free functions
- Static functions don't have `this`, so we pass it as an argument
- Inside ISR: cast `void* arg` back to `RotaryEncoder*` to access the object

### The `volatile` Keyword
- Tells compiler: "this variable can change at any time (from ISR)"
- Without it, compiler might cache values and never see ISR updates
- Any variable shared between ISR and main code needs `volatile`

### Bit Manipulation
- `<<` = left shift (moves bits left, fills with zeros)
- `>>` = right shift (moves bits right)
- `|` = bitwise OR (combines bits)
- `&` = bitwise AND (masks bits)
- `->` = access member through pointer (same as `(*ptr).member`)
- `0b1101` = binary number prefix
- `0x0D` = hexadecimal number prefix

### Debouncing
- Mechanical switches "bounce" - they don't transition cleanly
- One press can look like 5-10 rapid on/off/on/off
- Solution: ignore transitions that happen too quickly (1ms for rotation, 50ms for button)

### Quadrature Encoding (How Encoders Work)
- Two pins (CLK and DT) produce signals 90° out of phase
- Creates 4 possible states: 00, 01, 10, 11
- Direction is determined by which path through the states
- We track transitions using a 4-bit code: `sum = (old_state << 2) | new_state`

---

## The Problems I Faced

### Problem 1: Different Boards = Different Counts Per Click

**Symptom:** Same encoder, same code, but:
- C6 Seeed: 2 counts per click
- S3 WROOM, C6, 32d : 1 counts per click (nothing registered!)

**Root Cause:** Different ESP32 boards take different paths through the encoder state machine.

```
The encoder states form a square:

        00 ←───────────── 01
        ↑↓                ↑↓
        ↓↑                ↓↑
        10 ───────────────→ 11

C6 Seeed path (CW):  00 → 10 → 11 (triggers 0x0B)
S3 WROOM path (CW):  11 → 01 → 00 (triggers 0x04)

They go DIFFERENT DIRECTIONS around the square!
```

**Solution:** Count on BOTH possible endpoint transitions:

```cpp
// Clockwise: 10→11 (0x0B) OR 01→00 (0x04)
if (sum == 0x0B || sum == 0x04) {
    position = position + 1;
}
// Counter-clockwise: 11→10 (0x0E) OR 00→01 (0x01)
else if (sum == 0x0E || sum == 0x01) {
    position = position - 1;
}
```

Each click only hits ONE of these values, so no double-counting!

---

### Problem 2: S3 Seeed Button Triggering Randomly

**Symptom:** When turning the encoder, the button kept "pressing" by itself.

```
I (249315) ENCODER_TEST: >>> Button PRESSED! Resetting position to 0 <<<
I (249615) ENCODER_TEST: >>> Button PRESSED! Resetting position to 0 <<<
```

I wasn't touching the button at all!

**Initial Theory:** Electrical noise or weak pull-up resistors on pins 0, 1, 2.

**Attempted Fix:** Changed to different pins (3, 5, 7).

**Result:** Nothing worked at all - no readings!

**Actual Root Cause:** The pins on Seeed XIAO boards are labeled differently!

The silkscreen labels (D0, D1, D2) are NOT the same as GPIO numbers:
- D0 = GPIO 1
- D1 = GPIO 2
- D2 = GPIO 3

**Solution:** Use the correct GPIO numbers that match the board labels:

```ini
[env:s3_seeed]
board = seeed_xiao_esp32s3
build_flags = 
    -DENCODER_CLK=1   # GPIO D0
    -DENCODER_DT=2    # GPIO D1
    -DENCODER_SW=3    # GPIO D2
```

This fixed both the random button triggers AND the "no reading" issue!

---

### Problem 3: Compiler Warnings About Volatile

**Symptom:** Warnings during compilation:

```
warning: '++' expression of 'volatile'-qualified type is deprecated [-Wvolatile]
  encoder->position++;
```

**Cause:** Using `++` or `--` on volatile variables is considered "deprecated" because it's actually two operations (read + write) and the compiler worries about race conditions.

**Solution:** Use explicit assignment instead:

```cpp
// Instead of:
encoder->position++;

// Use:
encoder->position = encoder->position + 1;
```

---

### Problem 4: Position Counting Every OTHER Click

**Symptom:** Turn encoder 4 times, only get 2 counts.

**Cause:** The `halfStepMode` logic was too restrictive - only counting on ONE specific transition that didn't match our encoder's pattern.

**Solution:** Same as Problem 1 - count on both possible endpoints per direction.

---

## The Debugging Process

AI had me add debug logging to see exactly what was happening:

```cpp
ESP_EARLY_LOGI(TAG, "Transition: old=%d%d new=%d%d sum=0x%02X", 
    (encoder->lastEncoded >> 1) & 1, 
    encoder->lastEncoded & 1,
    clk, dt, sum);
```

Then I tested each board:
1. Flash the code
2. Open serial monitor
3. Turn encoder ONE click slowly
4. Write down all the transition codes (sum values)
5. Compare across boards

This revealed that different boards produce different sum values for the same physical action.

**Example Output - C6 Seeed (one CW click):**
```
I (266173) ISR: Transition: old=00 new=10 sum=0x02
I (266193) ISR: Transition: old=10 new=11 sum=0x0B  ← triggers count
```

**Example Output - S3 WROOM (one CW click):**
```
I (166827) ISR: Transition: old=11 new=01 sum=0x0D
I (166847) ISR: Transition: old=01 new=00 sum=0x04  ← triggers count
```

Different paths, same result once we handle both!

---

## Final Code Structure

```
components/
└── encoder/
    ├── encoder.cpp     # Implementation with ISR and state machine
    ├── encoder.h       # Class definition and documentation
    └── CMakeLists.txt  # Build configuration

main/
└── main.cpp            # Test application
```

**platformio.ini** handles different pin configurations per board:

```ini
[env:esp32d]
board = esp32dev
build_flags = 
    -DENCODER_CLK=18
    -DENCODER_DT=19
    -DENCODER_SW=5

[env:s3_wroom]
board = esp32-s3-devkitc-1
build_flags = 
    -DENCODER_CLK=18
    -DENCODER_DT=19
    -DENCODER_SW=5

[env:s3_seeed]
board = seeed_xiao_esp32s3
build_flags = 
    -DENCODER_CLK=1   # D0
    -DENCODER_DT=2    # D1
    -DENCODER_SW=3    # D2

[env:c6]
board = esp32-c6-devkitc-1
build_flags = 
    -DENCODER_CLK=18
    -DENCODER_DT=19
    -DENCODER_SW=5

[env:c6_seeed]
board = seeed_xiao_esp32c6
build_flags = 
    -DENCODER_CLK=0
    -DENCODER_DT=1
    -DENCODER_SW=2
```

---

## Key Takeaways

1. **Same code can behave differently on different boards** - GPIO timing, pull-up strength, and interrupt handling vary between ESP32 variants.

2. **Debug logging is essential** - Adding transition logging helped identify exactly what each board was doing.

3. **Board pin labels ≠ GPIO numbers** - Always check the pinout diagram! Seeed XIAO boards especially have different numbering.

4. **Handle multiple cases** - Instead of assuming one path through the state machine, handle all valid paths.

5. **Interrupts > Polling** - The main loop can sleep 50ms and still catch every encoder turn because ISR handles it instantly.

6. **Documentation matters** - I now have a heavily documented codebase that explains everything from basic concepts to implementation details.

---

## What's Next

- [ ] Try different encoder hardware to see if code still works to see if the same code works on it using the same 5 boards.
- [ ] Start working on next component...

---

## Files Produced

| File | Description |
|------|-------------|
| `encoder.h` | Header with Doxygen docs + beginner explanations |
| `encoder.cpp` | Implementation with full state machine documentation |
| `main.cpp` | Test application with usage examples |
| `CMakeLists.txt` | Build configuration with explanations |
| `platformio.ini` | Multi-board pin configurations |

All files have **both** professional Doxygen-style documentation AND detailed beginner explanations stacked together.







##############################################################################################################################################################################################################################################################################################################################################################################################################################






Dev Log — Button & Touch Sensor Components
Date: 30/01/2026
Components: Tactile Button, Outemu Mechanical Switch, TTP223 Touch, HTTM Touch + RGB LED
Goal: Build reusable button and touch components for all 5 ESP32 boards.

Boards Tested
BoardPinsStatusESP32DGPIO 18, 19✅ WorksESP32-S3 WROOMGPIO 18, 19✅ WorksESP32-S3 Seeed XIAOGPIO 1, 2✅ WorksESP32-C6 WROOMGPIO 18, 19✅ WorksESP32-C6 Seeed XIAOGPIO 0, 1✅ Works

What I Learned
Buttons Use Polling, Not Interrupts

Encoder needs interrupts (fast rotation)
Buttons are slow (human speed) → polling every 10ms is fine

Why 4 Pins on Tactile Buttons

Pins 1-2 connected internally, pins 3-4 connected internally
Use diagonal pair (1-4 or 2-3) to cross the switch
Extra pins for PCB stability, not electrical need

TTP223 Jumper Settings
JumperOpenBridgedA (TOG)MomentaryToggle (tap on/off)B (AHLB)Active HIGH + 7s timeoutActive LOW + no timeout
HTTM Has Same Chip as TTP223

Default: toggle mode + RGB cycling
Removed resistor "102" → converted to momentary mode

Touch Sensitivity

Add capacitor between touch pad and GND
No cap = most sensitive, 47pF = least sensitive
Ceramic capacitors have no polarity

No Debounce Needed for Touch

Mechanical buttons bounce → need 50ms debounce
Capacitive touch has no moving parts → chip filters internally


Project Structure
firmware/
├── components/
│   ├── button/      ← Tactile + mechanical switches
│   └── touch/       ← TTP223 + HTTM capacitive
└── testing/
    ├── button-test/ ← All 5 boards configured
    └── touch-test/  ← All 5 boards configured

Key Takeaways

Polling works for slow inputs — no interrupts needed for buttons/touch
Diagonal wiring on 4-pin buttons — pins are paired internally
Touch modules have hidden settings — jumpers/resistors change behavior
TTP223 = HTTM — same chip, HTTM adds RGB LED
No pull-up on touch — active output drives the line








##############################################################################################################################################################################################################################################################################################################################################################################################################################






