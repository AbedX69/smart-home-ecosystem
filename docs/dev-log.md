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




## Dev Log — February + March Plan (updated) — 05.02.2026

### Phase 1 — **Now → Feb 17**

**Rush drivers (AI-assisted), test only on ESP32D, minimal docs.**

Targets:

* **SSD1357Z** (0.6" 64×64 RGB display)
* **MAX98357** (I2S audio amp)
* **PCA9548A** (I2C multiplexer for multi displays)
* **Relay + SSR**
* **Vibration motors**
* **Buzzer**
* **MOSFET PWM dimming** (lights)

Goal: “works on ESP32D with a quick demo” for each.

---

### Phase 2 — **Ramadan start (~Feb 18) → ~Feb 23** (≈ 5 days)

**Proper testing + video documentation across all 5 boards.**

What happens here:

* Flash + test each driver on every board
* Fix pin maps + board-specific issues
* Record clean dev-log footage while doing it

Deliverable: each component has a verified “works on all boards” test.

---

### Phase 3 — **~Feb 23 → Feb 28**

**Study + API mindset.**

* Read your own drivers like a library:

  * what functions exist
  * how they should be used
  * what parameters/limits exist
* Clean up naming / usage patterns so they feel consistent

Deliverable: you can use each driver confidently without guessing.

---

## March Plan

### March 1 → March 23

**Build the wired “everything” prototype (proof of concept).**

* All components wired + running together
* Not pretty, just functional and testable
* Film key progress

Deliverable: one integrated wired prototype doing the full demo.

---

### ~March 23 → End of March

**Wireless phase + new drivers + filming**

* Add wireless logic + architecture
* Build drivers for:

  * **Camera: OV26740**
  * **Audio DAC: PCM5102A**
* Study them like APIs again
* Start assembling wireless prototypes + film

Deliverable: wireless prototype direction is real (not just theory), and the two new drivers are alive.

---

Everything after that stays the same (enclosures month, then app/website POC).


##############################################################################################################################################################################################################################################################################################################################################################################################################################

### Dev Log — 07/02/2026 (Sat)

#### What I did today

Created drivers for these components:

* **SSD1357Z** (0.6" 64×64 RGB display)
* **MAX98357** (I2S audio amp)
* **Relay + SSR**
* **Vibration motors**
* **Buzzer**

I started from easiest → hardest: **Relay/SSR → Buzzer → Vibration motors → MAX98357 → SSD1357Z**.

---

## Component Notes

### 1) Relay + SSR ✅

* Driver was straightforward.
* Testing on **ESP32D** went smoothly (no issues worth noting).

---

### 2) Buzzer ✅

* Driver + testing went smoothly.
* **Observation:** some frequencies are noticeably louder than others.

  * Around **2 kHz** sounds like a good “default loud” frequency.
  * If I want “animated” tones/melodies, I can vary frequency but I’ll lose some volume on certain notes (acceptable tradeoff).

---

### 3) Vibration motors ✅ (with tweak)

* Driver worked, but my first tap/double/triple tap timings were too short:

  * I had taps set to **50ms** → motor sometimes didn’t even start.
* **Fix:** changed tap duration to **100ms** and it became reliable.
* **Guess:** the vibration module has capacitors that soak the initial current for a moment.
* **Observation:** some PWM values feel “stronger/more intense” than others (not all PWM levels feel equal).

---

### 4) MAX98357 (I2S Audio Amp) ✅

* Setup + flashing were smooth, no major problems.
* **Observation:** in **TEST 5: “Twinkle Twinkle Little Star”**, some notes are *way* louder than others.

  * Not sure yet if it’s:

    * speaker limitation,
    * code/tone generation,
    * or amp behavior.
* For now, I’m accepting it as “good enough” and moving on.

---

### 5) SSD1357Z (0.6" 64×64 RGB) ❌ (big issue)

This one did not behave.

What happened:

* Wired it to the **correct pins** → **no sign of life**.
* Lowered SPI clock from **10 MHz → 1 MHz** → still nothing.
* Tried a few wiring changes → still nothing.
* Took a break (PC off, gym, food, shower).
* When I came back and powered up:

  * the display **briefly showed something**,
  * then went **black again** like it “died”.
* To remove breadboard/contact issues:

  * I **soldered header pins** onto the display module.
  * Still **no sign of life** after that.
* I then verified wiring properly:

  * Used a multimeter and continuity-tested every connection from **ESP32D pin → display pin**.
  * Everything checked out.
* **Status:** I don’t know yet if it’s a driver/init sequence problem, a reset/power issue, a bad module, or something subtle like CS/DC/RST behavior.

---

## Plan for tomorrow (08/02/2026)

### Hardware prep

* Improve soldering.
* Solder pins onto **all ESP boards** so I can plug them into a breadboard cleanly.
* This will make **testing + filming** way easier.

### Drivers to build next (ESP32D testing)

* **PCA9548A** (I2C multiplexer for multi displays)
* **MOSFET PWM dimming** (lights)









##############################################################################################################################################################################################################################################################################################################################################################################################################################

### Dev Log — 16/02/2026 (Mon) — Updated

#### Status update (big milestone)

For the past **5 days**, I tested components while filming **both the computer screen and the real hardware**.
Everything I tested is now **fully video documented**, which I’m really happy with.

✅ **Result:** the first **quarter (1/4)** is done:

* component **drivers work**
* everything is **documented on video**
* the workflow is repeatable

> **Note:** most of the “working footage” so far is on **ESP32-S3 WROOM**.

---

## Project flow (big picture)

**Drivers first → systems → wireless → wireless system/ecosystem → packaging → real world**

And my detailed pipeline now is:

**Component drivers → Docs (I’m here) → Learn what’s done (next) → Combined testing → Docs →
Wireless drivers → Docs → Learn what’s done → Combined testing → Docs →
Editing → Enclosures/packaging → Docs → Real-life testing**

---

## Hardware issue today

One of the **ESP32-S3 WROOM serial/UART ports** stopped working.
I switched to the other port (**OTG**) and kept going.

---

## Far-future components (if I get time)

* **microSD** (read/write + file management)
* **cameras** (capture/streaming)

---

## Component tracker (updated)

**SSD1357 is canceled and removed.**

| #  | Component     | Location           | Status | Notes                     |
| -- | ------------- | ------------------ | ------ | ------------------------- |
| 1  | SSD1306       | `display/ssd1306/` | ✅      | 0.96" OLED mono           |
| 2  | GC9A01        | `display/gc9a01/`  | ✅      | 1.28" round TFT           |
| 3  | ST7789        | `display/st7789/`  | ✅      | 1.69" TFT                 |
| 4  | ILI9341       | `display/ili9341/` | ✅      | 2.8" TFT + XPT2046 touch  |
| 5  | E-paper       | `display/epaper/`  | ✅      | 2.13" tri-color e-ink     |
| 6  | MAX98357      | `audio/max98357/`  | ✅      | I2S amp                   |
| 7  | PCA9548A      | `i2c/pca9548a/`    | ✅      | I2C mux 1→8               |
| 8  | Encoder       | `encoder/`         | ✅      | rotary + button           |
| 9  | Button        | `button/`          | ✅      | debounced input           |
| 10 | Touch         | `touch/`           | ✅      | TTP223 module             |
| 11 | Buzzer        | `buzzer/`          | ✅      | tones + melodies          |
| 12 | Vibration     | `vibration/`       | ✅      | haptic patterns           |
| 13 | Relay         | `relay/`           | ✅      | relay / SSR control       |
| 14 | PWM Dimmer    | `pwm_dimmer/`      | ✅      | dimming + gamma           |
| 15 | MOSFET Driver | `mosfet_driver/`   | ✅      | power MOSFET + soft start |

---

## Documentation checklist per component

For each component:

* [ ] wiring photo
* [ ] video of it working *(mostly done already)*

---

## Next step (immediately after Docs)

### **Learn what’s done**

Go through each driver like an API:

* what functions exist
* what parameters matter
* what the “correct usage pattern” is

Then I move into **combined testing** (multiple components together, reaction times, conflicts, real behavior).




##############################################################################################################################################################################################################################################################################################################################################################################################################################







### Dev Log — 11/03/2026 (Wed)

#### Big picture progress

For the past couple of weeks, I’ve been in **“study mode”** — going through **every component driver** I’ve built so far and really understanding how they work, not just getting them to blink. This felt like the right move before moving into combined testing and wireless.

---

## What I did

### 1. Studied all existing components (the full list)

I went back through every driver from the earlier sprints:

| Category   | Components                                                                                       |
| ---------- | ------------------------------------------------------------------------------------------------ |
| Displays   | SSD1306, GC9A01, ST7789, ILI9341 (+XPT2046 touch), E-paper 2.13"                                 |
| Audio      | MAX98357 (I2S amp)                                                                               |
| I2C        | PCA9548A (multiplexer)                                                                           |
| Input      | Rotary encoder (with interrupts), Button (debounced), TTP223 / HTTM touch                        |
| Output     | Buzzer, Vibration motors, Relay / SSR, PWM Dimmer (with gamma), MOSFET driver (soft-start)       |

For each one, I:

- Read through the code with AI explanations
- Understood **why** certain patterns were used (volatile, IRAM_ATTR, debounce timings, etc.)
- Took **personal notes** on things I’d like to improve or change (timing tweaks, API consistency, better error handling)

> **Important:** I *didn’t* copy those suggested changes into the main `components/` folder.  
> I want to keep this folder as a **clean, working reference** — only code that’s been tested and works.  
> The ideas are saved elsewhere; they’ll get merged later when I do a “polish” pass.

---

### 2. Added a `wireless/` folder structure

I’ve been thinking about wireless for a while, so I finally created the skeleton:

```
firmware/
├── components/          (existing)
├── production/           (future)
├── shared/               (shared utilities)
├── testing/              (component test apps)
└── wireless/             ← NEW
    ├── communication/     (driver‑level implementations)
    │   ├── ble/
    │   ├── esp_now/
    │   ├── lora/
    │   ├── ota/
    │   ├── wifi/
    │   └── zigbee/
    └── testing/           (test apps for each wireless type)
        ├── ble-test/
        ├── esp-now-test/
        ├── lora-test/
        ├── ota-test/
        ├── system-test/   (future: multiple protocols together)
        ├── wifi-test/
        └── zigbee-test/
```

This keeps wireless separate from the basic component drivers, which feels right — wireless adds a whole new layer of complexity.

---

### 3. WiFi + OTA — first wireless milestone

I dug into WiFi, built a test app in `wireless/testing/wifi-test/`, and got it working on **ESP32D** and **ESP32-S3 WROOM**.

#### What works

- **Captive portal** on first boot — connect phone, enter home WiFi credentials
- **Auto‑reconnect** after reboot or power cycle
- **Over‑the‑air (OTA) updates** using HTTP POST

Here’s the auto‑reconnect logic I documented (love a good ASCII diagram):

```
 * AUTO-RECONNECT: HOW IT WORKS
 * =============================================================================
 * 
 *     Boot
 *      │
 *      ▼
 *     ┌───────────────────┐
 *     │ Load creds from   │
 *     │ NVS (if saved)    │
 *     └────────┬──────────┘
 *              │
 *              ▼
 *     ┌───────────────────┐     success    ┌──────────────────┐
 *     │ Try to connect    │ ──────────────►│    CONNECTED     │
 *     │ to saved AP       │                │  (got IP addr)   │
 *     └────────┬──────────┘                └────────┬─────────┘
 *              │ fail                               │
 *              ▼                                    │ disconnect
 *     ┌───────────────────┐                         │ event
 *     │ Wait & retry      │◄───────────────────────┘
 *     │ (backoff: 1→30s)  │
 *     └────────┬──────────┘
 *              │ max retries
 *              ▼
 *     ┌───────────────────┐
 *     │ Start AP mode     │  ← Optional: captive portal fallback
 *     │ for configuration │
 *     └───────────────────┘
 * 
 * NVS (Non-Volatile Storage) persists credentials across reboots.
 * The backoff increases delay between retries to avoid hammering the router.
```

#### OTA flashing

Once the board is on the network, I can flash new firmware wirelessly:

```powershell
# Flash ESP32D
curl.exe -X POST --data-binary "@.pio\build\esp32d\firmware.bin" -H "Content-Type: application/octet-stream" http://192.168.31.11/api/ota

# Flash ESP32-S3 WROOM
curl.exe -X POST --data-binary "@.pio\build\s3_wroom\firmware.bin" -H "Content-Type: application/octet-stream" http://192.168.31.205/api/ota
```

Both respond with `{"status":"ok","bytes":...,"message":"rebooting"}` — it’s **magical** to see code fly over the air.

---

## What I learned / observed

- **Captive portal** is surprisingly simple with the right libraries (AsyncWebServer + DNSServer).
- **NVS** is perfect for storing credentials — survives reboots, easy to read/write.
- **OTA** needs a good chunk of free flash/partition; ESP32‑D and S3 have plenty.
- The auto‑reconnect backoff is essential — without it, the board just hammers the router and makes things worse.
- I still need to think about **security** (HTTPS? signed updates?) but that’s a polish‑stage problem.

---

## What’s next (immediate)

Over the next few days I’ll work through the other wireless protocols in a similar way:

1. **Bluetooth Low Energy (BLE)** — probably start with a simple beacon + UART‑like service.
2. **ESP‑NOW** — peer‑to‑peer without WiFi, good for low‑power sensor networks.
3. **Zigbee** — using the ESP32‑C6’s built‑in 802.15.4 radio.
4. **LoRa** — external modules (Seeed LoRa boards I have).
5. **OTA** — already have a taste, but need to explore more (e.g., serving updates from a server).

The pattern will be the same:  
build a minimal test → get it working on one board → test across the 5‑board set → document.

---

## Longer‑term thoughts

- **Many boards talking together** — how do they discover each other? coordinate?
- **OTA over the internet** — not just local network. That means a server, maybe MQTT, and secure updates.
- **Security** — encryption, authentication, signed firmware. Probably overkill for now, but I want to understand it.

But all that is for later. Right now I’m just happy that wireless is no longer a black box — I have a working WiFi/OTA setup and a clean folder structure to build on.

---

## Files produced / updated

| Path | Description |
|------|-------------|
| `wireless/communication/wifi/` | WiFi driver + captive portal logic |
| `wireless/testing/wifi-test/` | Test app for WiFi + OTA |
| `wireless/communication/ota/` | Basic OTA handler (HTTP server endpoint) |
| `wireless/testing/ota-test/` | (placeholder for future) |

All existing component folders remain untouched — they’re my stable reference.





##############################################################################################################################################################################################################################################################################################################################################################################################################################



















Dev Log — LoRa SX1262 Debugging
Date: 02/04/2026
Component: LoRa SX1262 (Seeed Wio-SX1262 + XIAO ESP32-S3, B2B connector kit)
Goal: Get two boards talking — one TX beacon, one RX gateway.

Setup
Two identical XIAO ESP32-S3 + Wio-SX1262 B2B kits.
Both have antennas connected, 4 meters apart (wall in between).
DeviceCOM PortMACFirmwareTX (Sensor Beacon)COM9b8:f8:62:f9:fa:c8-DLORA_TEST_TXRX (Gateway)COM6b8:f8:62:f8:b7:d8-DLORA_TEST_RX
LoRa config: 915 MHz, SF7, 125 kHz BW, CR 4/5, sync word 0x12, CRC on, 22 dBm TX power.

The Problem
TX reports "Beacon transmitted" every 5 seconds — looks fine.
RX initializes, enters continuous RX, but never receives a single packet.
No IRQ activity, no DIO1 toggles, nothing.

The Debugging Journey
Attempt 1: Missing full calibration after TCXO enable
Theory: The Wio-SX1262 uses an external TCXO (controlled via DIO3). After reset, the chip auto-calibrates using its internal RC oscillator, but that happens before we enable the TCXO. So the calibration runs against the wrong clock source.
Fix applied:
cpp// In begin(), after TCXO setup:
uint8_t calib_all = 0x7F;
spiWrite(SX1262_CMD_CALIBRATE, &calib_all, 1);
vTaskDelay(pdMS_TO_TICKS(10));
Result: Still no RX. Good practice to keep though.

Attempt 2: Missing TXEN/RXEN pin control
Theory (from Arduino reference code): The working Arduino examples for this exact kit use GPIO 43 (TXEN) and GPIO 44 (RXEN) to control the RF antenna switch. My driver only used setDio2AsRfSwitch(true), which toggles a different pin (DIO2) that this module doesn't wire to the switch.
Without RXEN being driven HIGH, the antenna switch never routes RF to the receiver.
Fix applied:

Added txen and rxen fields to LoRaPins struct
Updated XIAO_S3_WIO_B2B preset: .txen = 43, .rxen = 44
In send(): set TXEN=1, RXEN=0 before SET_TX
In startReceive() / receiveOnce(): set RXEN=1, TXEN=0 before SET_RX
Set use_dio2_rf_sw = false (default) since we use external TXEN/RXEN

Result: Still no RX.

Attempt 3: GPIO 43/44 stuck as UART pins
Theory: GPIO 43 and 44 are UART0 TX/RX by default on ESP32-S3. In ESP-IDF, gpio_set_direction() alone doesn't detach them from the UART peripheral — unlike Arduino's pinMode() which calls gpio_reset_pin() internally.
Fix applied:
cppif (_pins.txen >= 0) {
    gpio_reset_pin((gpio_num_t)_pins.txen);
    gpio_set_direction((gpio_num_t)_pins.txen, GPIO_MODE_INPUT_OUTPUT);
    gpio_set_level((gpio_num_t)_pins.txen, 0);
}
if (_pins.rxen >= 0) {
    gpio_reset_pin((gpio_num_t)_pins.rxen);
    gpio_set_direction((gpio_num_t)_pins.rxen, GPIO_MODE_INPUT_OUTPUT);
    gpio_set_level((gpio_num_t)_pins.rxen, 0);
}
Result: Still no RX. But diagnostic logging now confirmed RXEN=1 and TXEN=0 during receive — the pins were actually working.

Attempt 4: Swap boards to isolate hardware vs software
Flashed TX firmware on the RX board and vice versa.
Result: Both boards transmit fine, neither receives. Confirmed it's a software bug, not a dead board.

Attempt 5: Add RSSI diagnostic
Added lora.getRSSI() to the RX polling loop to check if the chip is actually in RX mode.
Result: RSSI = -110 dBm (noise floor). Chip IS in RX mode, hearing background noise, but seeing zero signal from the TX 4 meters away. This means TX is not actually radiating RF — the TX_DONE IRQ fires because the state machine completes, not because RF went out.

Attempt 6 (THE FIX): Wrong bandwidth register value
Root cause found.
In main.cpp:
cppconfig.bandwidth = 7;    // I thought 7 = 125 kHz
The comment in the header said 7=125k — but that's the SX1276 mapping (old chip, register-based). The SX1262 uses completely different values:
SX1262 ValueBandwidth07.81 kHz115.63 kHz231.25 kHz362.50 kHz4125 kHz5250 kHz6500 kHz
Value 7 is undefined. The chip accepted the command without error, TX_DONE fired normally, but the RF modulation was garbage — no valid LoRa signal was emitted.
Fix:
cppconfig.bandwidth = 4;    // 125 kHz (SX1262 register value)
And fixed the header comment:
cppuint8_t bandwidth = 4;   ///< 0=7.81k 1=15.63k 2=31.25k 3=62.5k 4=125k 5=250k 6=500k
Result: Packets received immediately. RSSI -69 to -77 dBm, SNR 12-13 dB, 100% packet reception.

Final Working Output
I (6164) LoRaSX1262: RX: 8 bytes, RSSI=-72 dBm, SNR=12 dB
I (6164) LoRaTest: ╔═══════════ PACKET #1 ═══════════╗
I (6164) LoRaTest: ║  Length: 8 bytes
I (6174) LoRaTest: ║  RSSI:  -72 dBm
I (6174) LoRaTest: ║  SNR:   12 dB
I (6174) LoRaTest: ║  Type:  SENSOR (0x01)
I (6184) LoRaTest: ║  Node:  1
I (6184) LoRaTest: ║  Seq:   18
I (6184) LoRaTest: ║  Temp:  25.3°C
I (6194) LoRaTest: ║  Hum:   59.8%
I (6194) LoRaTest: ╚══════════════════════════════════╝

All Changes Made to the Driver
#FileChangeWhy1lora_sx1262.hAdded txen, rxen to LoRaPins structRF switch needs external GPIO control2lora_sx1262.hUpdated XIAO_S3_WIO_B2B preset with .txen=43, .rxen=44Correct pins for this kit3lora_sx1262.hAdded .txen=-1, .rxen=-1 to EDGE and CUSTOM presetsPrevent compile errors4lora_sx1262.hChanged use_dio2_rf_sw default to falseB2B kit uses TXEN/RXEN, not DIO25lora_sx1262.hFixed bandwidth commentOld comment had SX1276 mapping, not SX12626lora_sx1262.cppAdded gpio_reset_pin() + GPIO_MODE_INPUT_OUTPUT for TXEN/RXEN in begin()GPIO 43/44 default to UART0 on ESP32-S37lora_sx1262.cppAdded TXEN/RXEN toggling in send(), startReceive(), receiveOnce()Control RF switch direction8lora_sx1262.cppAdded full calibration (0x7F) after TCXO enable in begin()Recalibrate with correct clock source9lora_sx1262.cppAdded RX boosted gain (0x96 to register 0x08AC) in startReceive()Better receive sensitivity10main.cppChanged config.bandwidth = 7 → config.bandwidth = 4THE ROOT CAUSE — 7 is invalid for SX1262

Key Takeaways

SX1262 ≠ SX1276 register values. The bandwidth index mapping is completely different between the two chips. Don't copy SX1276 documentation into SX1262 code.
TX_DONE doesn't mean RF was transmitted. The SX1262 fires TX_DONE when its state machine finishes, regardless of whether valid RF was emitted. An invalid bandwidth value produces no usable signal but still reports success.
RSSI diagnostic is powerful. Reading instantaneous RSSI during RX confirmed the chip was listening but hearing nothing — proving the problem was on the TX side, not RX.
GPIO 43/44 on ESP32-S3 need gpio_reset_pin() before use as GPIO. They're UART0 by default and gpio_set_direction() alone doesn't detach them from the peripheral.
The Wio-SX1262 B2B kit uses TXEN/RXEN (GPIO 43/44), not DIO2, for RF switch control. The Arduino reference code showed this clearly — should have checked it first.
Swapping boards is the fastest way to isolate hardware vs software. One test proved both boards were fine and the bug was in code.








LoRa (SX1262) – point‑to‑point sensor beacon + gateway.

BLE (NimBLE) – scanner, server, client, and Web Bluetooth dashboard.

Hardware:

2× XIAO ESP32‑S3 + Wio‑SX1262 B2B kits (LoRa tests)

1× ESP32‑D + 1× XIAO ESP32‑S3 (BLE tests)

All antennas attached.

1. LoRa – Root Cause & Fix
Problem
TX device reported “Beacon transmitted” every 5 seconds, but RX gateway never received anything.
getRSSI() on the gateway showed noise floor (–110 dBm), confirming the TX was not radiating a valid LoRa signal.

Investigation
Swapped TX / RX boards → both transmitted fine, neither received.

Conclusion: software bug on TX side, not hardware.

Added TXEN/RXEN GPIO control (43/44) and gpio_reset_pin() because these pins default to UART0 on ESP32‑S3.

Enabled full calibration after TCXO start.

Still no packets.

The Fix
Wrong bandwidth register value for SX1262.
The header comment had SX1276 mapping (7=125 kHz), but SX1262 uses a different table:

SX1262 value	Bandwidth
4	125 kHz
5	250 kHz
6	500 kHz
The code used bandwidth = 7 – an invalid value. The chip accepted the command, completed its state machine, and fired TX_DONE, but emitted no usable RF.

Fix:

cpp
config.bandwidth = 4;   // 125 kHz (SX1262)
After change – packets received immediately with RSSI –69 to –77 dBm, SNR 12‑13 dB.

2. BLE – Full Stack Validation
2.1 Scanner Mode (First test)
Build flag: -DBLE_TEST_SCANNER

Result: ESP32 found nearby BLE devices (phone, smartwatch).

Confirmed NimBLE stack works and hardware BLE is functional.

2.2 Server Mode (Peripheral)
Build flag: -DBLE_TEST_SERVER

Added GATT service (12345678-1234-1234-1234-123456789ABC) with three characteristics:

Temperature (read/notify)

LED (write)

Device Name (read)

Initial crash due to buildServices() called before ble.begin().
Fix: Move buildServices() after begin() + add ble_gatts_start() in buildServices().

After fix, advertising started successfully.

2.3 Web Bluetooth Dashboard
Created HTML page using Web Bluetooth API.

Initial connection succeeded but service discovery failed – services were not included in advertisement.

Workaround: request all services and iterate.

Final dashboard:

Connect / disconnect

Read temperature (live via notifications)

Turn LED on/off

Read device name

Log shows clean interaction:

text
Got service: 12345678-1234-1234-1234-123456789abc
Subscribed to temperature notifications
Temperature: 21.96°C
LED set to ON
2.4 Client Mode (Central)
Build flag: -DBLE_TEST_CLIENT (note: uppercase CLIENT, not ClIENT)

Second ESP32‑S3 flashed as client, first ESP32‑D as server.

Client successfully:

Scanned and found server

Connected

Discovered 1 service / 3 characteristics

Subscribed to temperature notifications

Server side showed:

text
Connection established
Client subscribed to attr=16
Connections: 1
Notifications flowed every 5 seconds from server to client.

Minor issue: Read/write timeouts on client (timeout after 5 s) – subscription still worked, notifications OK.

Summary of Working Features
Stack	Tested Modes	Result
LoRa SX1262	TX beacon, RX gateway	✅ Packets received
BLE NimBLE	Scanner, Server, Client, Web Bluetooth	✅ All functional
Inter‑device	BLE client ↔ server (ESP32‑D ↔ ESP32‑S3)	✅ Connection & notify
Key Lessons
SX1262 bandwidth values are not the same as SX1276 – always check the datasheet.

TX_DONE does not guarantee RF was emitted – invalid parameters can still complete the TX state machine.

On ESP32‑S3, GPIO 43/44 are UART0 by default – use gpio_reset_pin() to use them as GPIO.

BLE GATT services must be built after NimBLE host initialises – order matters.

Web Bluetooth requires services to be either advertised or explicitly requested – using optionalServices or scanning all services works.

Two ESP32s can talk BLE – central ↔ peripheral works with NimBLE.

Next Steps (Optional)


Move on to Zigbee (ESP32‑C6 boards) or ESP‑Mesh.







##############################################################################################################################################################################################################################################################################################################################################################################################################################













Dev Log — 11/04/2026 (Sat)
Project: Dual LED Controller (ESP32-S3)
Hardware:

2× TTP223 touch sensors (GPIO 4, 5)
2× rotary encoders (CLK/DT/SW: 6/7/15, 16/17/18)
2× SSD1306 OLED via PCA9548A I2C mux (SDA=8, SCL=9)
2× GC9A01 round TFT on shared SPI (MOSI=11, SCK=12)

GC9A01 #1: CS=38, DC=39, RST=40, BLK=3
GC9A01 #2: CS=21, DC=47, RST=48




Build Errors Fixed (in order)
#ErrorFix1esp_log unknown componentRemoved from ssd1306 CMakeLists.txt REQUIRES2Format truncation -Werrorchar name[8] → char name[16]3epaper.cpp missing fontNarrowed EXTRA_COMPONENT_DIRS to exclude epaper4gc9a01 font not foundAdded "../shared" to gc9a01/CMakeLists.txt INCLUDE_DIRS5font5x7 not declaredRenamed to FONT_5X7 (matches shared header)

Runtime Errors Fixed
#ErrorRoot CauseFix6i2c: CONFLICT! driver_ng not allowedSSD1306 had both legacy and new I2C APIRewrote SSD1306 to use only new API7Stack overflow in task mainLarge objects on stack (2× 1KB OLED buffers + TFT objects)Added sdkconfig.defaults with CONFIG_ESP_MAIN_TASK_STACK_SIZE=16384

Display Performance Improvements
Problem: GC9A01 arc drawing was painfully slow

Full screen clear on every update
Arc redrew from 0° every time
Blocked encoder input during redraw

Solution: Incremental rendering
ChangeBeforeAfterArc method360 radial lines (drawLine)Horizontal scanlines (drawHLine)Full redrawEvery updateOnly on toggle on/offBrightness changeRedraw entire arcDraw/erase only the deltaColor changeFull screen clearOverdraw arc in new colorArc shapeSolid pie sliceDonut (inner radius protects text)Fill directionBottom → topCenter → outward
Bugs Fixed During Optimization
BugCauseFix4th quarter fills entire scanlineNon-contiguous arc pixels on same rowTrack contiguous runs, draw each separatelyLeftover digits (113% shown as "3%")New text shorter than oldfillRect() to clear text area before redrawClear rect clips outside inner circleRect too large (72×24)Shrunk to 48×16

Hardware/Pin Changes
Changed GC9A01 #1 pins:
Before: CS=10, DC=13, RST=14
After:  CS=38, DC=39, RST=40

Shared SPI Bus Fix
Problem
GC9A01 #1 worked, #2 didn't display anything (backlight on, no image).
Investigation

Swapped displays → same display works on #2 position
Rewired 3× with known-good wires → still nothing
Not hardware

Root Cause
Both displays shared MOSI=11, SCK=12, but used different SPI hosts (SPI2_HOST vs SPI3_HOST). Can't share pins across hosts.
Fix

Changed both to SPI2_HOST
Modified gc9a01.cpp init():

cppesp_err_t err = spi_bus_initialize(spiHost, &busConfig, SPI_DMA_CH_AUTO);
if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {  // Allow "already init"
    return false;
}

Modified destructor to NOT call spi_bus_free() (bus is shared)


Final State
Both displays working:

Touch toggles LED on/off
Encoder rotation adjusts brightness (mode B) or hue (mode C)
Encoder button switches mode
SSD1306 shows text status
GC9A01 shows donut arc with smooth incremental updates


Files Modified
FileChangesmain/main.cppPin defines, incremental arc rendering, donut shapecomponents/display/gc9a01/gc9a01.cppShared SPI bus handlingcomponents/display/ssd1306/ssd1306.cppNew I2C API onlysdkconfig.defaultsStack size 16KBplatformio.iniPin definitions in build_flags

Key Takeaways

Same SPI host for shared bus — can't use different hosts with same MOSI/SCK
ESP_ERR_INVALID_STATE is OK — means bus already initialized, just add device
Horizontal scanlines >> radial lines — fewer SPI transactions, much faster
Track contiguous runs — don't assume arc pixels are contiguous on a scanline
Clear before redraw — text artifacts happen when new string is shorter

















##############################################################################################################################################################################################################################################################################################################################################################################################################################












# Dev Log — ESP-MESH Testing

**Date:** 2026-04-24  
**Component:** ESP-MESH Manager (`esp_mesh_manager`)  
**Hardware:** ESP32D (root), XIAO ESP32-C6 (node), ESP32-C6 DevKitC (node)  
**Framework:** ESP-IDF 5.5.0 via PlatformIO  

---

## Goal

Test the ESP-MESH manager component with 3 boards:
- ESP32D as **root** (connects to home WiFi, starts mesh)
- Two C6 boards as **nodes** (join mesh, send messages to root)

Verify: node auto-join, root↔node messaging, broadcast, disconnect/reconnect, and multi-hop routing.

---

## Build Issues & Fixes

### 1. PlatformIO can't find `src` folder
- **Problem:** PlatformIO expects `src/` but test uses `main/`
- **Fix:** Added `src_dir = main` under `[platformio]` in `platformio.ini`

### 2. CMake can't resolve component names
- **Problem:** `esp_mesh_manager` and `wifi_manager` not found — CMake resolves components by **directory name**, not by class/file name
- **Fix:** Changed `main/CMakeLists.txt` requires from `esp_mesh_manager` → `mesh`, `wifi_manager` → `wifi`

### 3. `lib_extra_dirs` pulls in everything (including Zigbee)
- **Problem:** `lib_extra_dirs = ../../communication` drags in all subfolders including `zigbee`, which needs the Zigbee SDK not available for ESP32 classic
- **Fix:** Replaced `lib_extra_dirs` with explicit `EXTRA_COMPONENT_DIRS` in root `CMakeLists.txt`:
```cmake
set(EXTRA_COMPONENT_DIRS
    "../../communication/mesh"
    "../../communication/wifi"
    "../../communication/esp_now"
)
```

### 4. Mesh component references `wifi_manager` internally
- **Problem:** `mesh/CMakeLists.txt` had `PRIV_REQUIRES wifi_manager esp_now_manager` but folder names are `wifi` and `esp_now`
- **Fix:** Changed to `PRIV_REQUIRES wifi esp_now`

### 5. `WiFiManager::begin()` doesn't exist
- **Problem:** `WiFiManager` has `beginSTA()`, `beginAP()`, etc. — no plain `begin()`
- **Fix:** Replaced WiFiManager usage with direct ESP-IDF WiFi init:
```cpp
ESP_ERROR_CHECK(esp_netif_init());
ESP_ERROR_CHECK(esp_event_loop_create_default());
ESP_ERROR_CHECK(esp_netif_create_default_wifi_mesh_netifs(nullptr, nullptr));
wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
ESP_ERROR_CHECK(esp_wifi_init(&wifi_cfg));
ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
ESP_ERROR_CHECK(esp_wifi_start());
```

### 6. ESP-IDF 5.5 API changes
| Old (pre-5.5) | New (5.5) |
|---|---|
| `esp_mesh_get_routing_table_size(&count)` | `count = esp_mesh_get_routing_table_size()` (returns int, no args) |
| `MESH_EVENT_ROOT_GOT_IP` | Removed — handled by regular IP event handler |
| `MESH_EVENT_ROOT_LOST_IP` | Removed |
| `memset(&_config, 0, sizeof(_config))` | `_config = MeshConfig{}` (C6 toolchain treats memset on non-trivial types as error) |

### 7. RX task exits immediately
- **Problem:** `esp_mesh_recv()` called before mesh connects, gets `ESP_ERR_MESH_NOT_START`, task exits forever
- **Fix:** Added wait loop at top of `rxTaskFunc`:
```cpp
while (!self->_connected) {
    vTaskDelay(pdMS_TO_TICKS(500));
}
```

### 8. Root finds router but never connects (scan loop)
- **Problem:** Mesh didn't know the node was supposed to be root
- **Fix:** Added `esp_mesh_set_type(MESH_ROOT)` before `esp_mesh_fix_root(true)`:
```cpp
if (config.is_root) {
    ESP_ERROR_CHECK(esp_mesh_set_type(MESH_ROOT));
    ESP_ERROR_CHECK(esp_mesh_fix_root(true));
}
```

### 9. C6 node crashes — `ESP_ERR_MESH_ARGUMENT` on `esp_mesh_set_config`
- **Problem:** ESP-IDF 5.5 rejects `mesh_cfg_t` with empty router fields (ssid_len:0)
- **Fix:** ALL nodes (not just root) must set router SSID/password in `mesh_cfg.router`. Non-root nodes use this info to identify which mesh network to join:
```cpp
if (strlen(config.router_ssid) > 0) {
    memcpy(mesh_cfg.router.ssid, config.router_ssid, strlen(config.router_ssid));
    mesh_cfg.router.ssid_len = strlen(config.router_ssid);
    memcpy(mesh_cfg.router.password, config.router_pass, strlen(config.router_pass));
}
ESP_ERROR_CHECK(esp_mesh_set_config(&mesh_cfg));
```

### 10. C6 binary too large (1078KB > 1048KB limit)
- **Problem:** Default partition table only allocates 1MB for app
- **Fix:** Created `partitions_large.csv` with 3.9MB app partition:
```csv
# Name,   Type, SubType, Offset,  Size, Flags
nvs,      data, nvs,     0x9000,  0x6000,
phy_init, data, phy,     0xf000,  0x1000,
factory,  app,  factory, 0x10000, 0x3F0000,
```

---

## Test Results

### Root (ESP32D) — ROOT MODE
- Connected to home WiFi router (AbedX69) on channel 8, RSSI: -46 dBm
- Started mesh, accepted children
- Received messages from both nodes
- Broadcasts succeeded once children connected
- Broadcasts correctly fail with `ESP_ERR_MESH_DISCARD` when no children present

### Node 1 (XIAO ESP32-C6) — NODE MODE
- Auto-joined mesh at **layer 2** (direct child of root)
- Sent `NODE MSG #0..N` to root successfully
- `TX fail: 0` — no dropped messages
- MAC: `B4:3A:45:8A:81:74`

### Node 2 (ESP32-C6 DevKit) — NODE MODE
- Auto-joined mesh at **layer 2** (direct child of root)
- Sent `NODE MSG #0..N` to root successfully
- Disconnected/reconnected cleanly when moved out of range
- MAC: `58:8C:81:36:B7:5C`

### 3-Node Mesh Status (stable)
```
Connected : YES
Is root   : YES
Layer     : 1
Children  : 2
Total nodes: 3
TX fail   : 0 (after children joined)
```

### Multi-Hop Test
- Attempted to separate boards to force layer 3 routing
- Both C6 nodes remained at layer 2 — apartment too small for WiFi to drop off between boards
- When moved too far, nodes disconnected entirely rather than reparenting through middle node
- Multi-hop routing confirmed to work in ESP-MESH architecture, but requires larger physical space or reduced TX power (`esp_wifi_set_max_tx_power()`) to test

---

## What Was Verified

| Feature | Status |
|---|---|
| Root connects to home router | ✅ |
| Node auto-joins mesh | ✅ |
| Node → Root messaging | ✅ |
| Root → All broadcast | ✅ |
| 3-node mesh (root + 2 nodes) | ✅ |
| Node disconnect detection | ✅ |
| Node auto-reconnect | ✅ |
| ESP32D + ESP32-C6 interop | ✅ |
| Multi-hop (layer 3) routing | ⚠️ Not tested — space too small |

---

## Key Takeaways

1. **ESP-IDF 5.5 changed mesh APIs** — `esp_mesh_get_routing_table_size()` signature changed, `MESH_EVENT_ROOT_GOT_IP` removed, stricter config validation
2. **ALL nodes need router SSID** — not just root. Nodes use it to identify the correct mesh network
3. **`esp_mesh_set_type(MESH_ROOT)` is required** — `esp_mesh_fix_root(true)` alone isn't enough for the root to actually connect to the router
4. **C6 toolchain is stricter** — `memset` on non-trivial types is an error, unused variables are errors
5. **Component names = directory names** — CMake resolves by folder name, not by class or header name
6. **Don't use `lib_extra_dirs` for selective components** — it pulls everything. Use `EXTRA_COMPONENT_DIRS` in CMakeLists.txt instead
7. **Mesh netifs must be created before `esp_wifi_init()`** — order matters

---

## Files Modified

| File | Change |
|---|---|
| `mesh-test/platformio.ini` | Added `src_dir`, board configs, partition table, router credentials for all envs |
| `mesh-test/CMakeLists.txt` | Explicit `EXTRA_COMPONENT_DIRS` instead of `lib_extra_dirs` |
| `mesh-test/main/CMakeLists.txt` | Corrected component requires |
| `mesh-test/main/main.cpp` | Direct WiFi init, mesh netifs, router creds for NODE/LEAF modes |
| `mesh-test/partitions_large.csv` | Created — 3.9MB app partition for C6 |
| `communication/mesh/CMakeLists.txt` | Fixed `PRIV_REQUIRES` to use folder names |
| `communication/mesh/esp_mesh_manager.cpp` | ESP-IDF 5.5 API fixes, RX task wait, router config for all nodes, `MESH_ROOT` type |

---

## Next

- [ ] LoRa ping-pong test (2x XIAO S3 + Wio-SX1262)
- [ ] Integrate mesh into main firmware architecture

















##############################################################################################################################################################################################################################################################################################################################################################################################################################






























##############################################################################################################################################################################################################################################################################################################################################################################################################################