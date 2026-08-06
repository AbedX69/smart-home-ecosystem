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











# Dev Log — LoRa Ping-Pong Range Test

**Date:** 2026-05-03  
**Component:** LoRa SX1262 Ping-Pong (`lora-test`, `-DLORA_TEST_PINGPONG`)  
**Hardware:** 2× XIAO ESP32-S3 + Wio-SX1262 B2B kits  
**Framework:** ESP-IDF 5.5.0 via PlatformIO  

---

## Goal

Test bidirectional LoRa communication (ping-pong) between two boards, then do an outdoor range test.

- COM6 as **ping sender** (`node_id = 0x01`) — sends PING every 5s, waits for PONG
- COM9 as **pong responder** (`node_id = 0x02`) — listens continuously, replies with PONG

Verify: round-trip time, RSSI/SNR at distance, packet loss, max range.

---

## Bugs & Fixes

### 1. Responder never receives PINGs

- **Problem:** COM9 (responder) initialized fine, entered continuous RX, but never received any packets from COM6 (sender)
- **Investigation:**
  - Flashed COM6 as `LORA_TEST_TX` and COM9 as `LORA_TEST_RX` — packets received fine (RSSI -68 dBm, SNR 13 dB)
  - Confirmed radio link works, bug is specific to ping-pong code path
- **Root cause:** Stale cached build objects. The ping-pong firmware was compiled against an older version of the driver
- **Fix:** `pio run -e s3_seeed -t clean` then reflash both boards sequentially
- **Result:** 100% packet reception after clean build

### 2. Deadlock in responder callback

- **Problem:** `onPingPong` callback called `lora.send()` directly. `send()` blocks waiting for `_tx_done_sem`, but that semaphore is given inside `handleIrq()` — the same function that invoked the callback. IRQ task deadlocks waiting on itself.
- **Fix:** Moved `send()` out of callback into the main loop using a flag:

```cpp
// Callback just sets flag + copies data
static volatile bool send_pong = false;
static uint8_t pong_data[4];

static void onPingPong(const LoRaRxPacket* pkt) {
    if (pkt->data[0] == PKT_TYPE_PING) {
        pong_data[0] = PKT_TYPE_PONG;
        pong_data[1] = 0x02;
        pong_data[2] = pkt->data[2];
        pong_data[3] = pkt->data[3];
        send_pong = true;
    }
}

// Main loop does the actual send
while (true) {
    if (send_pong) {
        send_pong = false;
        lora.stopReceive();
        vTaskDelay(pdMS_TO_TICKS(50));
        lora.send(pong_data, 4);
        lora.startReceive();
    }
    vTaskDelay(pdMS_TO_TICKS(10));
}
```

### 3. TX timeout after moving board to power bank

- **Problem:** After disconnecting COM6 from laptop and plugging into power bank, `send()` returned `ESP_ERR_TIMEOUT` on every attempt. Persisted after reset.
- **Root cause:** B2B connector came loose when physically moving the board. The Wio-SX1262 module wasn't seated properly on the XIAO.
- **Fix:** Reseated the module firmly. TX resumed immediately.

### 4. Responder recovery after disconnect

- **Problem:** After the responder (node 0x02) goes offline and comes back, the sender (node 0x01) doesn't recover — keeps timing out even though responder is alive again
- **Status:** Observed but not root-caused yet. Sender was still printing "Sending PING" with timeouts, so the loop was alive but the radio may have been stuck. Workaround: reset the sender.

---

## Test Results

### Close Range (same desk, ~1m)

| Setting | RTT | RSSI | SNR | Packet Loss |
|---------|-----|------|-----|-------------|
| SF7 / BW125 | ~136 ms | -76 to -82 dBm | 12-13 dB | 0% |
| SF12 / BW125 | ~1790 ms | -65 to -71 dBm | 5-7 dB | 0% |

### Indoor Range (across house, multiple walls)

| Setting | RSSI | SNR | Packet Loss |
|---------|------|-----|-------------|
| SF12 / BW125 | -95 to -114 dBm | -11 to 11 dB | 0% |

Worst successful packet: **RSSI -114 dBm, SNR -11 dB** — still decoded correctly at SF12.

### Outdoor Range (urban Tel Aviv, in car)

| Setting | Distance | RSSI | SNR | Result |
|---------|----------|------|-----|--------|
| SF7 / BW125 | Same block | -95 to -109 dBm | 3-10 dB | Working, occasional drops |
| SF7 / BW125 | Next block (~150m) | — | — | Total loss |

Last successful SF7 packet before loss: **RSSI -109 dBm, SNR 3 dB**

### Antenna Observations

- **-70 dBm at 1 meter** with PCB antenna — abnormally weak for 22 dBm TX power
- **Car body added ~15 dB attenuation** (from -75 to -90 dBm just sitting in car)
- PCB antenna is the primary bottleneck, not the radio settings
- Ordered: 4× U.FL 915MHz 3dBi whip antennas + pigtails (₪23 total)

---

## Configuration Changes for SF12

```cpp
config.spreading_factor = 12;    // Was 7

// Increased timeouts for longer air time
lora.receiveOnce(10000);         // Was 3000

// Wait loop
while (waiting_pong &&
       (xTaskGetTickCount() - wait_start) < pdMS_TO_TICKS(10000)) {  // Was 3000
```

---

## Files Modified

| File | Change |
|------|--------|
| `main/main.cpp` | Deadlock fix (send from main loop, not callback), added `send_pong` flag + `pong_data` globals, SF12 config, increased timeouts |

---

## Key Takeaways

1. **Never call blocking driver functions from IRQ callbacks** — `send()` waits on a semaphore given by the same IRQ handler. Use a flag and handle it in the main loop.
2. **Clean builds matter** — stale cached objects caused phantom failures that looked like driver bugs. When in doubt, `pio run -t clean`.
3. **B2B connectors are fragile** — physically moving the board can unseat the module. Always check connection after moving.
4. **PCB antennas are the bottleneck** — 22 dBm TX power means nothing when the antenna eats 60 dB. External whip antennas should recover 20-30 dB.
5. **SF12 buys ~20 dB link budget over SF7** — but costs 50× air time (1.8s RTT vs 136ms). Worth it for sensors that transmit infrequently.
6. **TX_DONE fires even without valid RF** — the sender shows no errors when the responder is gone. Application-level ACK (like ping-pong) is the only way to know if the other side is alive.

---

## Next Steps

- [ ] Retest range with external whip antennas (when delivered)
- [ ] Investigate sender recovery issue after responder reconnect
- [ ] Integrate LoRa into main firmware architecture


















##############################################################################################################################################################################################################################################################################################################################################################################################################################


# Dev Log — May 19, 2026

## Project: Smart Light (SK6812 RGBW LED Strip Integration)

---

### Goal
Integrate SK6812 RGBW 4000K LED strips (144 LEDs each, ×2) into the smart light system — both the `SmartLightDevice` driver and the bench test wiring.

---

### What got done

**SmartLightDevice implemented** — was a stub, now fully functional. Takes a GPIO pin + LED count, owns an `AddressableLED` strip internally. State surface mirrors `SmartLightRemote`: on/off, brightness (0-100), hue (0-359), white channel (0-100). RGB and white are independent — white isn't derived from RGB, it's a separate 4000K channel controlled via the encoder's WHITE mode.

**Bench test main.cpp updated** — added two `SmartLightDevice` instances alongside the existing two `SmartLightRemote` panels. `syncToDevice()` copies panel state → device state and calls `update()` on each dirty frame. No wireless, direct wired sync on the same ESP32-S3.

**Build issues resolved:**
- `addressable_led.h` not found → added `-I` paths in `platformio.ini` and fixed `EXTRA_COMPONENT_DIRS` in CMakeLists to only pull needed components (was pulling all of `components/` causing epaper, mosfet_driver etc. to fail)
- `esp_log` not resolvable as component in PlatformIO's ESP-IDF 5.x → removed from `REQUIRES`, it's globally available
- `pdMS_TO_TICKS` not declared → added `#include <freertos/FreeRTOS.h>` to `addressable_led.cpp`
- `mem_block_symbols` must be ≥64 on ESP32D → bumped from 48 to 64

**Hardware debugging:**
- Strip didn't respond on GPIO 1 or GPIO 2 on ESP32-S3 — those are USB D-/D+ pins, internally pulled for USB
- Moved to separate ESP32D for isolated testing
- RMT backend: init succeeds, all test stages run, but zero LEDs respond on GPIO 4 (or 2, or 5)
- Touching the RX pin (GPIO 3) to the data line lit up 5 LEDs white — confirms strip is alive and wired correctly
- GPIO 3/RX floods constant HIGH, not controlled data — so strip hardware is fine, signal issue on other GPIOs

**Root cause theory:** RMT output on tested GPIOs isn't producing a clean enough signal for the strip. Could be level/drive strength or a board-specific routing issue.

**Decision:** add SPI backend to `AddressableLED` class instead of continuing to debug RMT GPIO issues.

**SPI backend added to AddressableLED:**
- New `TransportBackend` enum: `RMT` (default) or `SPI`
- Constructor gets optional 5th parameter — fully backward compatible, existing code unchanged
- SPI encoding: each LED data bit → one SPI byte (0xF8 for '1', 0xC0 for '0') at 6.4 MHz clock
- Produces identical NRZ timing: bit-1 = ~780ns HIGH / ~468ns LOW, bit-0 = ~312ns HIGH / ~937ns LOW
- DMA-backed transmission — zero CPU during send
- Reset pulse: 256 bytes of 0x00 appended (≈320µs LOW)
- SPI buffer allocated with `heap_caps_malloc(MALLOC_CAP_DMA)` for DMA compatibility
- Auto-selects SPI2_HOST, falls back to SPI3_HOST if SPI2 is busy
- For 144 SK6812 RGBW LEDs: 144 × 4 × 8 + 256 = 4864 bytes SPI buffer

---

### Files changed

| File | Change |
|---|---|
| `components/addressable/addressable_led.h` | Added `TransportBackend` enum, SPI members, `initSpi()`, `showSpi()`, `encodeSpiBuffer()`, `getBackend()` |
| `components/addressable/addressable_led.cpp` | SPI init/show/encode implementation, `mem_block_symbols` fix, FreeRTOS include |
| `components/addressable/CMakeLists.txt` | Added `freertos`, `esp_heap` to REQUIRES, removed `esp_log` |
| `modules/smart-light/smart_light_device.h` | Full implementation (was stub) |
| `modules/smart-light/smart_light_device.cpp` | Full implementation (was stub) |
| `modules/smart-light/CMakeLists.txt` | Added `addressable` to REQUIRES |
| `testing/smart-light-test/main/main.cpp` | Added SmartLightDevice + syncToDevice wired loop |
| `testing/sk6812-test/` | New test project for isolated strip testing |

---

### Next up
- Flash SPI backend test on ESP32D (GPIO 13 → DIN)
- Verify R/G/B/W channels individually — if colors are swapped, adjust ColorOrder
- Once confirmed working, bring SPI backend back to the S3 smart light bench test
- Pick non-USB GPIOs on S3 for the two strips (GPIO 41, 42 or similar)

---

### Hardware notes
- SK6812 RGBW strip: 144 LEDs, 4000K neutral white, single-wire protocol despite "SPI" in product name (3 wires: DIN, VCC, GND)
- Max current per strip at full RGBW: ~8.6A — need beefy 5V PSU
- ESP32D GPIO 1/3 are TX/RX — avoid for data. GPIO 2 has boot-mode implications
- ESP32-S3 GPIO 1/2 are USB — avoid for data









##############################################################################################################################################################################################################################################################################################################################################################################################################################
# Dev Log — LoRa Radio Bring-Up Fix (TCXO + B2B Pins) & Antenna Swap

**Date:** 2026-05-29
**Component:** LoRa SX1262 driver (`communication/lora`), tested via `lora-test` (`-DLORA_TEST_PINGPONG`)
**Hardware:** 2× XIAO ESP32-S3 + Wio-SX1262 (B2B kit) + external U.FL → SMA whip antennas (915 MHz, 3 dBi)
**Framework:** ESP-IDF via PlatformIO

---

## Goal

New external whip antennas arrived (replacing the bottlenecked PCB antennas). Get LoRa actually transmitting/receiving again, then re-run the ping-pong range test.

Reality check: the radio turned out to be completely dead on startup — two separate driver bugs had to be fixed before any range testing was possible.

---

## Bugs & Fixes

### 1. BUSY timeout on every command

- **Symptom:** `E LoRaSX1262: BUSY timeout!` repeating from boot, on every SPI command, before init even finished.
- **Cause:** The `XIAO_S3_WIO_B2B` pin preset had the wrong RST/BUSY/DIO1 pins — `3/4/2` instead of the correct `42/40/39`. NSS (41) and the SPI pins were correct, so the driver talked just enough to print the init banner, but it was polling GPIO4 for BUSY when the real BUSY line is GPIO40 → waited forever, timed out on everything.
- **Fix:** Corrected the preset to `RST=42 BUSY=40 DIO1=39` (the mapping confirmed working back in April). Also fixed the stale pinout comment block in the header that showed the same wrong numbers.

### 2. TX timeout, RX deaf (after the pin fix)

- **Symptom:** Pins fixed, BUSY timeouts gone, init clean — but every PING gave `TX timeout` and the responder received nothing.
- **Cause:** The driver never powered the **TCXO**. The Wio-SX1262 uses a temperature-compensated oscillator clocked through the SX1262's DIO3, not a bare crystal. Without `SetDIO3asTCXOCtrl`, the RF PLL can't lock — `SetTx` never completes (→ TX timeout) and the receiver is deaf. The driver also never ran a full `Calibrate(0x7F)`, only `CalibrateImage`.
- **Fix:** Added `setDio3AsTcxoCtrl()` (DIO3, 1.8 V, 5 ms startup) and `calibrate()` (full `Calibrate(0x7F)`), called in `begin()` right after `STDBY_RC` and before image calibration. First flash after this: PONGs immediately, 0% loss.

### 3. Sender recovery after responder reboot — CONFIRMED FIXED

- **Status:** Not a new bug. The recovery fix landed back in May (don't give the TX semaphore on RX timeouts; drain stale token at top of `send()`). This session **verified it in the field**: the responder rebooted mid-run, the sender ate 2 timeouts while it was actually gone, then resumed on its own with no manual reset. Previously this required power-cycling the sender.

---

## Test Results (indoor — see limitation below)

### Bench (~1 m), antenna sanity check

| Config | RSSI | SNR | RTT | Loss |
|--------|------|-----|-----|------|
| SF12 / BW125 | -48 to -66 dBm | +4 to +6 dB | ~1784 ms | 0% (steady state) |

PCB antenna previously read ~-70 dBm at the same distance and couldn't hold a cross-house link at all. Whips give a ~15–20 dB improvement at 1 m.

### Across the house

| Config | RSSI | SNR | RTT | Notes |
|--------|------|-----|-----|-------|
| SF12 / BW125 | -100 to -107 dBm | +5 dB | ~1783 ms | Solid once locked; occasional multipath-fade timeouts |
| SF7 / BW125  | -100 to -107 dBm | +5 to +10 dB | ~128 ms | Holds reliably after lock; startup/fade timeouts at first |

Both spreading factors held a usable link through the whole house with margin to spare — at SF7, ~-105 dBm with SNR +5 still leaves roughly 15 dB before the SF7 decode floor (~-123 dBm).

---

## Limitation — range is NOT measured

These are indoor results. Indoors, **wall penetration and multipath fades dominate, not distance** — RSSI does not track range (saw -55 dBm and -107 dBm a few steps apart), and the dropouts are fade nulls, not link-budget limits. No outdoor line-of-sight site was available this session.

**The actual range number is still unmeasured.** The old PCB-antenna benchmark (total loss at ~150 m urban, SF7) has not been re-matched. The indoor data confirms the new antennas clearly outperform the PCB antenna (which couldn't even cross the house), but it does **not** establish a max range.

→ Carried forward: outdoor LOS walk at SF7 vs the 150 m benchmark, when a field is available.

---

## Configuration Changes

```cpp
// TCXO power-up added in begin() (Wio-SX1262 has a TCXO, not an XTAL)
setDio3AsTcxoCtrl(0x02, 5000);   // 0x02 = 1.8 V, 5 ms startup
calibrate(0x7F);                 // full recalibration after clock switch

// B2B preset corrected (lora_sx1262.h)
.reset = 42,   // was 3
.busy  = 40,   // was 4
.dio1  = 39    // was 2

// Test SF for antenna comparison (lora-test main.cpp)
config.spreading_factor = 7;     // was 12
```

---

## Files Modified

| File | Change |
|------|--------|
| `communication/lora/lora_sx1262.h`   | Fix B2B preset pins (`42/40/39`); fix pinout comment; declare `setDio3AsTcxoCtrl` + `calibrate` |
| `communication/lora/lora_sx1262.cpp` | Add `setDio3AsTcxoCtrl()` and `calibrate()`; call both in `begin()` |
| `testing/lora-test/main/main.cpp`    | `spreading_factor` 12 → 7 (test config only) |

---

## Key Takeaways

1. **No TCXO power = dead radio, silently.** The Wio-SX1262 needs the TCXO powered via DIO3. Without it the PLL never locks: TX times out, RX is deaf — but SPI and init "succeed," so it looks healthy at first glance.
2. **Enabling the TCXO requires a full `Calibrate(0x7F)`** afterward. `CalibrateImage` alone is not enough.
3. **A wrong BUSY pin throws BUSY timeouts on every command, from boot.** Correct NSS is enough to print the init banner, which is misleading — check the pin line in the banner.
4. **Bring-up bugs in a shared component are invisible until something uses it.** This driver was broken for *all* `communication/lora` consumers, not just the test app.
5. **Indoor range testing is meaningless.** Multipath fades and wall loss dominate; RSSI doesn't correlate with distance. A real range number needs outdoor line-of-sight.
6. **Sender-recovery fix verified in the field** (responder reboot → sender auto-resumed, no reset).

---

## Next Steps

- [x] Fix TCXO + B2B pins; verify ping-pong both directions, 0% loss
- [x] Confirm sender recovery after responder reboot
- [x] Commit/push TCXO + pin fix to `communication/lora`
- [ ] Outdoor LOS range test at SF7 vs the ~150 m PCB benchmark (needs an open field)
- [ ] Integrate LoRa into the main firmware now that the driver works

##############################################################################################################################################################################################################################################################################################################################################################################################################################
# Dev Log — Garage Door Controller (bench test)

**Date:** 2026-06-04
**Target:** ESP32-C6 DevKitC-1
**Goal:** Prove the controller logic on the bench with low-power hardware (two buzzers) before committing to motor + high-power hardware.

---

## Pin map (bench)

| GPIO | Role | Notes |
|------|------|-------|
| 10 | buzzer1 = **POWER** | 1500 Hz, LEDC ch0/timer0 |
| 11 | buzzer2 = **DIRECTION** | 500 Hz, LEDC ch1/timer1 |
| 18 | button | active-low, to GND, internal pull-up |

Moved off GPIO 4/5 because those are **strapping pins on the C6** (would tick at boot). Passive buzzers only.

---

## Behavior implemented

**Single-button cycle** (was 2-button, changed this session):
- press while moving → **STOP**
- press while stopped-mid → **reverse** (opposite of last direction)
- press while open → **close**; while closed → **open**
- first press after boot → opens (UP). No position sensor; ends inferred from travel-time timeout.

Always passes through STOP before reversing — verified safe under spam-clicking.

**Relay model** (what the buzzers stand in for):
- POWER relay = motor run on/off.
- DIRECTION relay = OFF→up, ON→down.
- On DOWN: direction energizes **first**, settle gap, **then** power → direction contact switches with no load.
- On STOP: power off first, then direction.

---

## Driver / code changes

- **`buzzer.h` / `buzzer.cpp`** — LEDC channel + timer are now constructor args (default ch0/timer0, so old single-buzzer code is unchanged). The stock driver hardcoded channel 0, so two `Buzzer` objects collided and played the same thing on both pins. Now two run independently at different pitches.
- **`garage_door_device.h/.cpp`** — class is now **state-machine only** (no hardware outputs). The caller maps `state()` → outputs. Removed an earlier over-engineered output abstraction (function callbacks / templates) in favor of this.
- **`main.cpp`** — owns the two buzzers, maps state→sound, and does the direction-before-power sequencing.
- Removed stale 2-button doc comment from the header (didn't match the code).

---

## Timing constants (two, different jobs)

- **`DIR_SETTLE_MS`** = 300 ms (in `main.cpp`) — dead-time so the **direction relay contact** switches with no power on it.
- **`GARAGE_MIN_DWELL_MS`** = 700 ms (in the driver) — after stopping, ignore move-start presses to protect the **motor** from rapid move→stop→reverse hammering. Stopping is never gated. Applies to `cmdToggle` / `cmdUp` / `cmdDown`. Blocked presses log `press ignored (dwell)`.

---

## Build gauntlet (resolved)

The test project compiles components from the shared `components/` tree, which caused a chain of issues:

1. **Missing `partitions_large.csv`** (inherited from main project) → removed custom partition line, set `board_build.flash_size = 2MB`.
2. **`SPI3_HOST` undeclared** in `addressable_led.cpp` — the C6 only has SPI2. Root cause wasn't the driver; it was that **every** component in `components/` was being compiled.
3. **CMake scoping** — `EXTRA_COMPONENT_DIRS` pointed at the whole `../../../components` folder. Fixed by pointing it at the **specific** folders needed (`button`, `buzzer`) + the garage module. (Tried `set(COMPONENTS ...)` first — that broke PlatformIO's injected build-info component, error: "Failed to find the default IDF component.")
4. **Stale `REQUIRES`** — `garage-controller` module still required `relay` (unused now). Fixed to `REQUIRES button driver esp_timer`. (Don't list `esp_log` — global in IDF 5.5.)

Build → flash → runs. Confirmed on hardware.

---

## Test results

- Spam-click test: clean UP↔DOWN alternation, every move separated by STOPPED, no direct UP→DOWN, no double-moves. Debounce (50 ms) holding — fastest real gaps ~180 ms.

---

## Open / next

- **Motor + high-power specs: TBD** (will provide). Then motor-side wiring — DC motor = DPDT direction relay (polarity swap), single-phase AC = run-winding swap; relay contact ratings, fusing, isolation. High-voltage = handle carefully.
- **Swap buzzers → relays:** buzzer1→power relay, buzzer2→direction relay; `tone()`→`on()`, `stop()`→`off()`; move the DIR_SETTLE sequencing into the relay output path.
- **Wireless phase:** remote ESP32 → ESP-NOW → device. `cmdUp()`/`cmdDown()`/`stop()` already exist for a remote to call.
- **Verify flash size:** board def reports 8 MB but one upload detected 2 MB — run `esptool flash_id` to confirm and set the override correctly.

##############################################################################################################################################################################################################################################################################################################################################################################################################################

# Dev Log — Smart Light (SK6812 RGBW bring-up)

**Dates:** 2026-06-07 (protocol capture) + 2026-06-10 (first ESP32 direct drive)
**Strip:** SK6812 RGBW, 144 px / 1 m, 4000K
**Targets:** ESP32D devkit (active) — test app now compiles for ESP32D / S3 / C6
**Goal:** Prove the ESP32 can drive the strip itself (RMT), using the SP630E + logic analyzer work as the reference.

---

## Session 1 — 06/07: SP630E reference + protocol capture

- SP630E first showed a **repeating 3-LED color pattern** → it defaulted to WS2812B/RGB (3 bytes/px) on a 4-byte/px strip. Fixed: BanlanX app → SK6812/RGBW mode.
- Logic analyzer (FX2 clone, 24 MHz, PulseView/sigrok, WinUSB via Zadig) on SP630E output: **T1H ≈ 623 ns, T0H ≈ 291 ns** — matches SK6812 datasheet (600/300), validates the SPI backend constants (`0xFC` / `0xE0` @ 8 MHz).
- SN74HCT125N wiring planned: VCC→5V, GND common, pin 1 (1OE)→GND, pin 2 (1A)←GPIO 13, pin 3 (1Y)→DIN.
- Power reality: bench supply is 3 A max. Full strip white ≈ 8.6 A @ 5V → protocol work only, low brightness.

---

## Session 2 — 06/10: ESP32 → strip (RMT test app)

### Bench

| What | Value |
|------|-------|
| Board | ESP32D devkit |
| Data | GPIO 13 → strip DIN (**direct**, '125 ended up bypassed — see below) |
| Power | bench 5V → strip V+, all grounds common |
| Brightness | 25/255 (~10%) → ~0.3 A one channel lit, safe on 3 A |

### Code fixes before first flash (test `main.cpp`)

1. **32-bit overflow in ns→tick math.** `300 * 10000000` (3e9) overflows int32 → garbage RMT durations. Fixed with `(uint64_t)` cast. Sanity log now prints `T0H=3 T0L=9 T1H=6 T1L=6` ✓.
2. **Per-target config** via `CONFIG_IDF_TARGET_*`: ESP32D = GPIO 13 / 64 RMT symbols, S3 = GPIO 4 / 48, C6 = GPIO 10 / 48. RMT memory blocks are **48 on S3/C6, 64 on classic ESP32** — 48 on the ESP32D aborts with `mem_block_symbols must be even and at least 64`.
3. Encoder callback: propagate `RMT_ENCODING_MEM_FULL` in the reset-symbol state (was silently dropped).

### Wrong-board incident (honest section)

Thought the C6 was flashed; boot log said `boot.esp32`, "Multicore app" → it was the **ESP32D**. The mem_block error (#2 above) was the tell. Bonus bullet dodged: the placeholder pin was GPIO 10, which on classic ESP32 is an **internal flash pin (6–11)** — the early abort prevented driving it. Per-target `#if` added so env/board mismatch can't silently half-work again.

### Build gauntlet

- `SPI3_HOST` undeclared compiling `addressable_led.cpp` for C6 (component pulled in via `EXTRA_COMPONENT_DIRS`; C6 has GPSPI2 only). Fix: wrap the SPI3 fallback in `#if SOC_SPI_PERIPH_NUM > 2`. **Verify:** guard was written during the session but the c6 env was never rebuilt after — confirm it's actually in the component before next C6 build.

### The patch bug + analysis

First run **through the '125**: each frame frozen but wrong — strip in patches (LED 1 = X, 2–8 = Y, 9–11 = Z, …), stable until the next transmit. RED frame (`19 00 00 00` repeating) produced: b b w w w b g orange g g b w b w g — **never red**.

Decoded: every observed color is the red frame at a shifted offset —

| Seen | Stream offset |
|------|---------------|
| white `00 00 00 19` | +1 byte |
| blue `00 00 19 00` | +2 bytes |
| green `00 19 00 00` | +3 bytes |
| orange | sub-byte bit shift across R+G |

→ bits being **inserted/lost mid-frame**; each break makes downstream LEDs re-sync at a new offset = one patch per break.

- `MEM_SYMBOLS` 64 → 256: no change → **not** an RMT refill underrun.
- **GPIO 13 direct to DIN (bypassing the '125): clean.** Fault isolated to the shifter path.

### Root cause (working theory) + byte order

- '125 was running with **no series resistor and no decoupling cap** (neither installed yet). Fast 5V HCT edges through breadboard jumpers into an unterminated DIN → ringing/reflections → strip double-clocks → inserted bits. Matches the symptom exactly.
- Direct 3.3V drive is clean but **out of spec** (SK6812 VIH = 0.7×VDD = 3.5V). Works on this bench with short wire; acceptable for protocol work, not for the final install.
- Byte order confirmed empirically: **GRBW** (red↔green swapped under RGBW). Matches `AddressableLED`'s documented SK6812_RGBW default. Test app fixed.

### Latent bug found in shared component (not fixed yet)

`addressable_led.cpp` `createEncoder()` has the same overflow pattern with `uint32_t` constants — defined wraparound, but `t0l_ticks` / `t1h_ticks` (900 ns values) compute to **0** → component's **RMT backend is broken** and was never exercised (only the SPI backend was validated vs the SP630E). Fix: `(uint64_t)` cast, same as the test app.

---

## Test results

- Direct GPIO 13, GRBW order, brightness 25: R / G / B / W all correct, stable, 144 px.

## Open / next

- '125 rework: **330–470Ω series resistor at DIN + 100nF across pins 14/7**, then re-test; if still dirty, analyzer on both sides of the chip (CH0 = GPIO 13, CH1 = DIN) and diff against the SP630E capture.
- Fix `createEncoder()` overflow in the shared component + confirm the `SOC_SPI_PERIPH_NUM` guard landed.
- Brightness ramp test: ceiling ≈ 85–90/255 all-white on the 3 A supply; full brightness waits for the ~10 A supply.
- Then: drive the strip from the `AddressableLED` class (RMT backend) instead of the raw test app.








##############################################################################################################################################################################################################################################################################################################################################################################################################################


# Dev Log — Smart Light Bench Integration

**Date:** 2026-06-30
**Board:** ESP32-S3 (WROOM dev board), single MCU running both remote UI + device
**Session goal:** Get the dual-LED bench test driving real SK6812 strips with a clean,
wireless-ready architecture, then wire and validate the full 2-channel hardware.

---

## Starting state

Three conflicting versions of the test were floating around:

- `dual_led_controller.cpp` — original clean demo, **virtual LEDs only**, slow per-pixel `drawPieSlice`.
- a half-merged `main.cpp` — broken merge of demo + fast arc renderer. Wouldn't compile:
  function bodies spliced into each other (`angleInArc`/`getAngle`/`drawArcScanline` interleaved),
  two `drawArcFast` definitions, duplicate `innerRadius`, duplicate `SPI2_HOST`.
- `smart_light_device.cpp` v1.0.0 — clean, drives a real strip via `AddressableLED`.

Plus an uploaded `smart_light_remote.{h,cpp}` v1.0.0 that was already clean (view-only) and a
`gc9a01` driver that had since gained the block-write API + shared-bus support. Discarded the
broken merge; built on the clean uploads.

---

## Bugs fixed

### Bug 1 — Device init log prints LED count in the GPIO field
**Problem:** `smart_light_device.cpp` `init()` logged the LED count twice; the `on GPIO %d`
field received `getNumLeds()` instead of the data pin (device never stored the pin).

```cpp
// before
ESP_LOGI(TAG, "Initialized: %d SK6812 RGBW LEDs on GPIO %d",
         _strip.getNumLeds(), (int)_strip.getNumLeds());
```
**Fix:** drop the bogus arg (or store the pin if the GPIO is wanted in the log).
Cosmetic only — no functional impact. *(Left as a one-liner for application.)*

### Bug 2 — Stale strip-pin comment in `main.cpp`
**Problem:** header comment claimed strips on **GPIO 1/2**, but the `#define`s used **41/42**.
GPIO 1/2 would have been a bad choice on S3 regardless. Code was correct; comment was wrong.
**Fix:** comment corrected to match the 41/42 defines.

### Bug 3 — CMake source list (watch item)
**Problem:** the component `CMakeLists.txt` only registers `addressable_led.cpp`.
**Fix:** ensure the app/component CMake lists all sources:
`gc9a01.cpp`, `touch.cpp`, `encoder.cpp`, `smart_light_remote.cpp`, `smart_light_device.cpp`,
`addressable_led.cpp`, `main.cpp`.

---

## Change — SmartLightRemote now owns its inputs

**Decision:** `SmartLightRemote` was view-only (state + display ref); `main.cpp` wired touch,
encoder, and strip around it. Refactored so the remote **owns its encoder + touch + display
reference + state** and exposes a single `update()`. The **strip stays in `SmartLightDevice`** —
that is the seam where the wireless split happens (remote = UI MCU, device = strip MCU).
Putting the strip inside the remote would only have to come back out later.

- New ctor: `SmartLightRemote(display, index, encClk, encDt, encSw, touchPin, activeHigh=true)`
- `init()` sets up encoder + touch; `update()` polls → updates state → redraws, returns dirty.
- Arc renderer (`render()` + `drawArcFast` + angle LUT) carried over **byte-for-byte** from v1.0.0.
- `main.cpp` loop collapsed to `if (panel.update()) syncToDevice(...)` per channel.

Bumped remote to **v1.1.0**.

---

## Hardware config (validated)

| Block | Detail |
|---|---|
| Displays ×2 | GC9A01, shared SPI2 — MOSI 11, SCK 12; CS 38/21, DC 39/47, RST 40/48; BLK 3 (disp2 BLK → 3.3V) |
| Encoders ×2 | CLK/DT/SW = 6/7/15 and 16/17/18, 3.3V |
| Touch ×2 | TTP223 active-high, OUT = 4 and 5, 3.3V |
| Strips ×2 | SK6812 RGBW, data = 41 and 42, **NUM_LEDS = 20** (temporary) |
| Power | one phone charger per strip @ 5V; **common ground** to ESP32 + everything |
| Level shift | **none** — bare 3.3V data (20-LED short run) |
| Caps/resistors | **none yet** |

Shared SPI bus confirmed working: GC9A01 `init()` tolerates `ESP_ERR_INVALID_STATE`, so both
displays share `SPI2_HOST` on common MOSI/SCK with separate CS.

---

## Results

| Test | Result |
|---|---|
| Both displays init + render on shared SPI bus | PASS |
| Both encoders independent (rotate + press) | PASS |
| Both touch sensors toggle on/off | PASS |
| Mode cycle BRIGHTNESS → COLOR → WHITE | PASS |
| Brightness / hue / white adjust + arc redraw | PASS |
| Remote → device sync drives strips | PASS |
| Bare 3.3V data integrity @ 20 LEDs | PASS (clean, no glitch) |
| Overall 2-channel bench | **Works perfectly** |

---

## Verified checklist

- [x] Refactored remote compiles + runs
- [x] Two self-contained remotes, no pin collisions
- [x] Shared SPI bus across both displays
- [x] Two encoders / two touch / two strips all independent
- [x] Per-loop remote→device sync working
- [x] Bare data path adequate at 20 LEDs
- [x] Common ground confirmed (charger GND ↔ ESP32 GND)
- [ ] GRBW byte order re-verified at full white *(do while at 20 LEDs)*
- [ ] SN74HCT125N path (pending part + 144-LED scale)
- [ ] Both-ends injection (pending 144 LEDs)

---

## Files modified

| File | Change |
|---|---|
| `smart_light_remote.h` | v1.1.0 — ctor takes encoder + touch pins; `init()`/`update()` added |
| `smart_light_remote.cpp` | v1.1.0 — input ownership + `update()`; renderer unchanged |
| `main.cpp` | slimmed to `if (panel.update()) syncToDevice()`; pin comment fixed |
| `smart_light_device.cpp` | (pending) one-line init-log fix |

---

## Key takeaways

- **Strip belongs to the device, not the remote.** Keeping `AddressableLED` inside
  `SmartLightDevice` and syncing remote→device each loop is the exact seam for the future
  wireless hop — no rework needed when it splits across two MCUs.
- **Shared SPI bus works** once the driver swallows `ESP_ERR_INVALID_STATE` on the 2nd
  `spi_bus_initialize`. Required for ESP32-C6 later (one usable SPI host).
- **Bare 3.3V SK6812 data is fine on a short bench run.** At 20 LEDs, no level shifter, no
  series resistor, no cap — clean. The SN74HCT125N + 330Ω + 1000µF are scale/reliability fixes,
  not smoke-test blockers.
- **Never parallel separate supplies into one rail** — neither two chargers on one strip, nor
  chargers stacked for more amps. One supply per rail; both-ends injection feeds one rail at two
  points.
- **Current scales with LED count** — dropping to 20 LEDs (~1.2A full white) made a single 2A
  charger sufficient and unlocked full-brightness color testing while waiting on the 10A supply.

---

## Next steps

- [ ] Re-verify GRBW byte order at full white (now, at 20 LEDs)
- [ ] On 10A supply arrival: set `NUM_LEDS` back to **144**
- [ ] Add SN74HCT125N (2 buffers) + **330Ω** series per data line + **100nF** across chip
- [ ] Add **1000µF** across each strip 5V/GND; switch to **both-ends injection** on the single 5V rail
- [ ] NVS-backed dynamic LED count (replace hardcoded `NUM_LEDS`)
- [ ] Solder permanent build once pin map is frozen
- [ ] Begin wireless split: move `SmartLightDevice` behind a transport, replace direct `syncToDevice()`




##############################################################################################################################################################################################################################################################################################################################################################################################################################










# Dev Log — Wireless POC Complete (Hub → ESP-NOW → Strip)

**Date:** 2026-07-10
**Projects:** `firmware/devices/testing/smart-light-test/hub` (ESP32-S3 WROOM) + `.../strip-node` (Seeed XIAO ESP32-C6)
**Milestone:** Smart light split into two wireless pieces — POC verified end-to-end.

---

## Bugs

### Bug 1 — `EspNowManager` undeclared identifier (strip node)

**Problem:**
Build failed with `EspNowManager` undeclared even though the compiler resolved a header. Root cause: `main/main.cpp` line 24 included ESP-IDF's **built-in** raw API header, not the project component:

```cpp
#include "esp_now.h"          // WRONG — IDF built-in, class never declared
```

Compiler finds a *valid* header, so no "file not found" — the class simply doesn't exist. Prior session's hypothesis (on-disk file mismatch vs. reviewed version) confirmed.

**Fix:**

```cpp
#include "esp_now_manager.h"  // component header (pulls in raw esp_now.h itself)
```

**Result:** Strip node builds clean. Flash 83.3% / RAM 11.3%.

---

### Bug 2 — Hub CMakeLists: wrong path depth + missing component

**Problem:**
`EXTRA_COMPONENT_DIRS doesn't exist: .../devices/components/addressable`. Hub CMakeLists still had **three** `../` traversals (stale from before projects were nested into `hub/` and `strip-node/` subfolders — same class of bug fixed on the strip node last session). It was also missing the `esp_now` component entirely, which the hub needs as sender.

**Fix:** Mirror the working strip-node CMakeLists (ground truth):

```cmake
set(EXTRA_COMPONENT_DIRS
    "../../../../components/addressable"
    "../../../../components/display/gc9a01"
    "../../../../components/display/shared"
    "../../../../components/touch"
    "../../../../components/encoder"
    "../../../modules/smart-light"
    "../../../../wireless/communication/esp_now"
)
```

Note: `platformio.ini` `build_flags` were already at the correct depth — only CMakeLists was stale.

---

### Bug 3 — `REQUIRES esp_now_manager` unknown component (hub)

**Problem:**
`Failed to resolve component 'esp_now_manager' required by component 'main'`. The component's **registered name** is `esp_now` (folder + its own CMakeLists), not the header/file name.

**Fix:** In `hub/main/CMakeLists.txt`: `esp_now_manager` → `esp_now` in REQUIRES.

**Result:** Hub builds and flashes clean.

---

### Bug 4 — Strip renders random per-LED colors (hardware)

**Problem:**
Wireless state arrived perfectly (log showed correct on/bri/hue/w on every message) but LEDs showed random colors, reshuffling on every received message (~1 s keepalive).

**Diagnosis path:** Payload provably correct → fault in render path. Suspects: (1) WiFi+RMT contention on C6 (no RMT DMA), (2) signal integrity. Added a boot test pattern (solid red 3 s, radio off) to `strip-node/main/main.cpp` as discriminator.

**Actual cause:** Loose **ground wire** on the XIAO C6. No common ground → data line has no voltage reference → every SK6812 frame decodes as garbage, fresh corruption per render (hence the reshuffle-per-message signature).

**Fix:** Reseated ground. Strip renders perfectly.

---

### Bug 5 — Hub display 1 dark (transient)

**Problem:** Display 1 off, its encoder still functional.

**Result:** Resolved on its own; both GC9A01s init successfully in boot log. Most likely same loose-wiring era as Bug 4. **Watch item** — if it recurs, treat as real.

---

## Results

| Test | Result |
|---|---|
| Strip node build (c6_seeed) | ✅ SUCCESS — flash 83.3%, RAM 11.3% |
| Hub build (s3_wroom) | ✅ SUCCESS |
| Strip node MAC | `B4:3A:45:8A:81:74` (COM7) |
| Hub MAC | `B8:F8:62:E0:A0:74` (COM4) |
| Peer add on hub | ✅ `Peer added: B4:3A:45:8A:81:74` |
| On/off toggle over air | ✅ tracked at strip |
| Brightness ramp (50→100) | ✅ smooth, every step received |
| Hue sweep (0→200) | ✅ tracked |
| White channel (0→52) | ✅ tracked |
| Render on strip | ✅ clean after ground fix |
| Both hub displays init | ✅ per boot log |

**Milestone closed: smart light works as two separate pieces over ESP-NOW.**

---

## Verified checklist

- [x] Strip node compiles, flashes, prints MAC
- [x] Hub compiles with corrected component paths + `esp_now` REQUIRES
- [x] `STRIP_MAC` set to `{0xB4, 0x3A, 0x45, 0x8A, 0x81, 0x74}`
- [x] Full state (on, brightness, hue, white) transfers hub → strip
- [x] Strip renders received state correctly on 10 POC LEDs
- [x] `pio device list` serial ↔ MAC mapping used to identify COM ports (COM4 = hub, COM7 = strip)

---

## Files modified

| File | Change |
|---|---|
| `strip-node/main/main.cpp` | `esp_now.h` → `esp_now_manager.h`; added boot test pattern (diagnostic) |
| `hub/CMakeLists.txt` | Path depth 3→4 `../`; added `wireless/communication/esp_now` |
| `hub/main/CMakeLists.txt` | REQUIRES `esp_now_manager` → `esp_now` |
| `hub/main/main.cpp` | `STRIP_MAC` set to strip node's MAC |
| `hub/platformio.ini` | Removed `board_build.partitions = partitions_large.csv` from c6 envs |

---

## Key takeaways

1. **A wrong-but-existing include yields "undeclared identifier," not "file not found."** IDF's built-in `esp_now.h` silently shadows the intent to include `esp_now_manager.h`. Name-collision hazard — verify the include *name*, not just that a header resolves.
2. **CMake REQUIRES uses the registered component name** (`esp_now`), never the header/file name.
3. **When sibling projects exist, the one that builds is ground truth** — diff its CMakeLists/ini first instead of re-deriving paths.
4. **Loose/missing common ground = random per-LED colors reshuffling every render.** Data line loses its reference; every frame decodes as garbage. First hardware check for "random colors" from now on: ground continuity.
5. **Boot test pattern (render before radio init) cleanly discriminates** radio/RMT contention from signal-integrity faults. Cheap, reusable diagnostic.
6. **`pio device list` hardware serials match MACs** — fastest way to map COM ports to boards in multi-device sessions.

---

## Next steps

- [ ] Remove (or shorten to ~1 s) the boot test pattern in `strip-node/main/main.cpp`
- [ ] Fix `SmartLightDevice` init log printing wrong GPIO (says 10, actual 18 — wrong variable in log statement)
- [ ] GC9A01 driver: skip `spi_bus_initialize` when bus already up (currently logs E on second display)
- [ ] Encoder driver: guard `gpio_install_isr_service` already-installed (benign E on second encoder)
- [ ] TouchSensor reports `initial state: TOUCHED` at boot on both pads — verify no phantom toggle; consider settling/ignoring initial read
- [ ] Watch for display 1 dropout recurrence
- [ ] Confirm 10A fast-blow fuse installed before any full-strip power test
- [ ] Level-shifter rework: SN74HCT125N + 330–470 Ω series + 100 nF (bench still direct 3.3 V drive, out of spec)
- [ ] Scale-up plan: 10 → 288 LEDs (requires fuse + level shifter + both-ends injection from single supply)
- [ ] OTA partition layout — strip node already at 83.3% flash on default table
- [ ] NVS-backed LED count (replace hardcoded `NUM_LEDS`)








##############################################################################################################################################################################################################################################################################################################################################################################################################################




# Dev Log — 21/07/2026 (Tue)

**Project:** Smart Home Ecosystem — firmware repository
**Scope:** Integrate system-core component bundle; restructure firmware tree; rewire all app build files
**Outcome:** ✅ Restructure complete, system components integrated, builds verified

---

## Summary

Two jobs in one session. First, drop the system-core bundle (`auto_pair` v2, `config_store` v1.1, `core_types`, `device_registry`, `message_protocol`) into the repo as proper ESP-IDF components. Second, collapse the tree into five top-level folders and repoint all 31 app build files at the new paths.

The bundle went in cleanly. The build wiring did not — three failed approaches before the right one, all documented below.

---

## Repository structure

**Before:**

```
firmware/
├── components/
├── devices/          modules, production, testing
└── system/           ota, transport/{ble,esp_now,lora,mesh,wifi,zigbee}
```

**After:**

```
firmware/
├── components/       drivers — grouped: audio/, display/, i2c/
├── devices/          modules, production
├── system/           auto_pair, config_store, core_types,
│                     device_registry, message_protocol, ota
├── testing/          drivers, devices, wireless
└── wireless/         ble, esp_now, lora, mesh, wifi, zigbee
```

Rationale: transports are a peer concern, not a sub-concern of `system`. Tests are a peer of devices, not a child. `system/` now holds only ecosystem logic.

---

## Bugs & fixes

### 1. `auto_pair` v1 header vs v2 implementation — caught before compiling

**Problem:** Repo held `auto_pair` v1.0.0 (18/02). Bundle supplied v2.0.0 (13/07). Dropping the v2 header next to the v1 `.cpp` would not compile — the two are structurally different:

| | v1 | v2 |
|---|---|---|
| `PairState` | has `WAITING` | no `WAITING` |
| `PairRequestInfo` | `fw_version` | `fw_major` + `first_seen_us` |
| retry counter | `_retry_count` (u8) | `_request_count` (u32) |
| thread safety | none | `_mutex` |
| controller list | none | `_paired[8]`, persisted |
| NVS load | `loadPairing()` | `loadDevicePairing()` |

**Fix:** Treat the bundle as all-or-nothing. Verified `auto_pair.cpp` v2 defines all 38 declared methods, contains zero v1 symbols, and its NVS keys (`ap_paired`, `ap_ctrl_mac`, `ap_devs`) are all under the 15-char NVS limit.

**Second trap avoided:** `core_types.h` defines `DeviceRole` and `TRANSPORT_*`. The old `device_registry.h` defined them too — keeping both would have caused redefinition errors. The bundle's `device_registry.h` has those sections emptied and includes `core_types.h` instead.

---

### 2. Blanket `EXTRA_COMPONENT_DIRS` pulled in every component

**Problem:** Wrote all seven component directories into all 31 apps, on the assumption that ESP-IDF only builds what's listed in `REQUIRES`. It doesn't — **IDF builds every component it discovers.** `encoder-test` therefore tried to build `zigbee`:

```
CMake Error at build.cmake:328 (message):
  Failed to resolve component 'esp-zigbee-lib' required by component
  'zigbee': unknown name.
```

It also pulled in `mdns` via the wifi component's `idf_component.yml`.

**Fix:** `EXTRA_COMPONENT_DIRS` must be **narrow** — only the dirs an app actually needs. The original per-app lists already did this correctly; they only needed their paths remapped, not replacing.

---

### 3. `set(COMPONENTS ...)` is incompatible with PlatformIO

**Problem:** Tried `set(COMPONENTS main esptool_py)` to keep discovery broad but the build minimal. Under PlatformIO:

```
Error! Failed to find the default IDF component with build information
for generic files. Check that the EXTRA_COMPONENT_DIRS option is not
overridden in your CMakeLists.txt.
```

**Fix:** Removed. Works under plain `idf.py`, not under PlatformIO's wrapper. Narrow `EXTRA_COMPONENT_DIRS` is the only approach that works here.

---

### 4. `git mv` infinite retry loop on locked directory

**Problem:** `git mv devices/testing/garage-controller-test testing/devices/...` hit a locked file and entered an unbounded prompt loop:

```
Rename from '...' to '...' failed. Should I try again? (y/n)
```
— repeating ~60 times before `fatal: Permission denied`.

**Fix:** Ctrl+C, close VS Code (it held the handle), use `Move-Item` instead. Git detects renames at commit time from content hashes, so `git mv` gains nothing here and fails far worse.

**Rule going forward:** use `Move-Item` for directory moves on Windows, never `git mv`.

---

### 5. PowerShell `-like '??*'` — `?` is a wildcard

**Problem:** The remap script tagged unconvertible paths with a `??` prefix and detected them with:

```powershell
if ($c -like '??*') { $c = $c.Substring(2) }
```

`?` matches **any single character** in `-like`. So `'??*'` matches any string of 2+ characters — i.e. every path. Every entry was reported UNRECOGNISED, and every entry would have had its first two characters removed:

```
components/button  ->  mponents/button
```

Caught only because the script was run as a dry run first.

**Fix:**

```powershell
if ($c.StartsWith('??')) { ... }
```

**Rule:** `-like` is wildcard matching (`*`, `?`, `[ ]`). For literal prefixes use `.StartsWith()`; for regex use `-match`.

---

### 6. `communication/ota` mapped to the wrong destination

**Problem:** Rule order put the generic `^communication/(.+)$` before the specific `^communication/ota$`, so `ota-test` was remapped to `wireless/ota` — which doesn't exist. OTA is a service and lives at `system/ota`.

**Fix:** Moved all `ota` rules above the generic transport rules. In a `switch -Regex`, specific patterns must precede general ones.

---

### 7. Bare `communication` matched no rule

**Problem:** `system-test` and `zigbee-test` used `"../../communication"` — the whole directory, no trailing component. No pattern matched it, so it fell through to the passthrough default.

**Fix:** Added `^communication$` → `wireless`. Both apps now point at all six transports, exactly as before the move. Flagged in the script output as a known risk: they will attempt to build `zigbee` and hit the `esp-zigbee-lib` error. **Pre-existing condition, not introduced today.**

---

## Path remapping applied

| Old | New |
|---|---|
| `wireless/communication/<x>` | `wireless/<x>` |
| `system/transport/<x>` | `wireless/<x>` |
| `communication/<x>` | `wireless/<x>` |
| `communication/ota` | `system/ota` |
| `modules/<x>` | `devices/modules/<x>` |
| `components/...` | unchanged |

Depths recomputed per app: 3 levels for `testing/drivers/<app>` and `testing/devices/<app>`, 4 for nested (`display-test/gc9a01-test`, `smart-light-test/hub`).

Apps with no existing list received `components`, plus the group folder where the component is nested (`display-test/*` → also `components/display`, etc.).

---

## Build results

| App | Target | RAM | Flash | Time | Result |
|---|---|---|---|---|---|
| `testing/drivers/encoder-test` | esp32d | 3.5% (11,444 B) | 18.1% (189,989 B) | 49.7 s | ✅ PASS |
| `testing/devices/smart-light-test/hub` | s3_wroom | 13.9% (45,492 B) | 74.9% (785,101 B) | 58.9 s | ✅ PASS |

Full 31-app sweep running separately.

⚠️ Hub is at **74.9% flash**. Worth watching — the pairing UI and system components aren't linked in yet.

---

## Verified

- [x] Bundle complete — 13 files, all five components, correct folder structure
- [x] `auto_pair.cpp` v2 defines every method the v2 header declares
- [x] No v1 symbols anywhere in the v2 implementation
- [x] NVS keys within the 15-character limit
- [x] No duplicate/stale copies of the five components anywhere in the tree
- [x] `firmware/` reduced to five top-level folders
- [x] All 31 app CMakeLists remapped, zero unrecognised paths
- [x] Known-good app builds clean (`encoder-test`)
- [x] Production-path app builds clean (`smart-light-test/hub`)
- [ ] Full 31-app sweep — running
- [ ] Committed

---

## Files modified

**Added** — `firmware/system/`:
- `core_types/` — `core_types.h`, `CMakeLists.txt` *(new, header-only, zero deps)*
- `message_protocol/` — `.h`, `.cpp`, `CMakeLists.txt` *(v1.0.0, memset fix)*
- `config_store/` — `.h`, `.cpp`, `CMakeLists.txt` *(v1.1.0 — `exists()` any-type, `getString` termination)*
- `device_registry/` — `.h`, `.cpp`, `CMakeLists.txt` *(patched — types moved to core_types)*
- `auto_pair/` — `.h`, `.cpp`, `CMakeLists.txt` *(v2.0.0 — NVS persistence, mutex, peer hooks)*

**Moved:**
- `system/transport/*` → `wireless/*`
- `devices/testing/drivers` → `testing/drivers`
- `devices/testing/wireless` → `testing/wireless`
- `devices/testing/{garage-controller-test, smart-light-test}` → `testing/devices/`

**Rewritten:** 31 app root `CMakeLists.txt` — `EXTRA_COMPONENT_DIRS` remapped

**Deleted:** `system/transport/` (empty), `devices/testing/` (empty)

---

## Key takeaways

1. **ESP-IDF builds every component it discovers, not just what's in `REQUIRES`.** `EXTRA_COMPONENT_DIRS` is a build-set declaration, not a search hint. Keep it narrow.
2. **`set(COMPONENTS ...)` doesn't work under PlatformIO.** Narrow `EXTRA_COMPONENT_DIRS` is the only lever available.
3. **Component grouping breaks flat discovery.** IDF looks exactly one level down, so `components/display/gc9a01` needs `components/display` listed — `components` alone won't find it.
4. **`-like` is wildcard matching in PowerShell.** `?` and `*` are wildcards. Use `.StartsWith()` for literal prefixes.
5. **Dry-run every bulk file rewrite.** The `??` bug would have corrupted 31 files silently. Preview cost nothing and caught it.
6. **Use `Move-Item`, not `git mv`, for directory moves on Windows.** Git detects renames from content at commit time; `git mv` adds a retry loop that can't be escaped cleanly.
7. **In `switch -Regex`, specific patterns must come before general ones.** First match wins.
8. **A header/implementation version mismatch is a silent trap.** Compare declared vs defined method sets before trusting a partial file drop.

---

## Next steps

- [ ] Commit the restructure
- [ ] Review 31-app sweep results; triage failures into *path problem* (`CMake Error`, `does not exist`, `unknown name`) vs *pre-existing code bug* (`error:` in a `.cpp`)
- [ ] `.gitignore` — `managed_components/`, `dependencies.lock`, `.pio/`; then `git rm -r --cached` the ~200 mdns files currently tracked
- [ ] **Wire `auto_pair` into strip node + hub:**
  - [ ] Add `"../../../../system"` to both apps' `EXTRA_COMPONENT_DIRS`
  - [ ] Add system components to each `main/CMakeLists.txt` `REQUIRES`
  - [ ] Strip node: `pair.begin(DeviceRole::LIGHT, "Strip")`
  - [ ] Hub: `beginAsController()`, log requests, accept over serial
  - [ ] Confirm both MACs persist to NVS and survive reboot
- [ ] GC9A01 pairing popup — only after serial flow is proven
- [ ] Narrow `system-test` / `zigbee-test` component dirs if they're ever needed
- [ ] Watch hub flash usage (74.9% before pairing code is linked)

**Not blocking:** `message_protocol` v2 stays queued. `auto_pair` v2 rides on `CUSTOM_0/1/2` within the existing 10-byte payload — pairing can be built and tested on v1 of the protocol.



##############################################################################################################################################################################################################################################################################################################################################################################################################################







# Dev Log — Pairing Validation, TX Policy, and OTA Groundwork

**Date:** Thursday, 23/07/2026

**Goal:** Close out the pairing layer with the two outstanding validation tests, kill the 2-second keepalive spam, then lay OTA groundwork — dual-slot partition tables on both devices, firmware identity reporting, and a staged image on the hub.

---

## Part 1 — Pairing validation

### Test 1: Fresh pair after NVS erase

**Purpose:** Confirm the SOLID_ON fix — removing an 800 ms `vTaskDelay` from the strip's pairing-LED callback — actually killed the ACK retry storm.

**Method:** Full flash erase on the strip, reflash, hub left untouched. The strip was still in the hub's NVS paired list, so this exercised the silent re-accept path — which is the realistic re-flash scenario anyway.

**Result: PASS**

```
RX CMD PAIR_ACCEPT seq=18103 → TX ACK PAIR_ACCEPT [OK]
RX CMD SET_LIGHT   seq=18104 → TX ACK SET_LIGHT   [OK]
```

One ACK per seq. **Zero `TX ACK~` (cached-duplicate) lines** anywhere in the session. Previously this same window produced one real ACK plus two cache replays.

Confirmed for free in the same log:

- NVS erase genuinely took — `Not paired — broadcasting pair requests as "Strip 1"`
- Encoder + touch path end-to-end, which had never been verified: payload went `00 32 00 00 00` (off) → `01 32 …` (touch on) → brightness sweep `33→45` → hue bytes moving → white `00→18`
- ~70 consecutive keepalives, every one ACKed first try, zero retries

**On the 14.5-second pairing delay:** seven PAIR_REQ broadcasts before the hub answered. Cause was mundane — the hub wasn't powered on yet when the strip started broadcasting. Not a bug. A genuinely clean fresh pair later in the session took **70 ms** (request at 3790 ms → accepted at 3860 ms).

### Test 2: Hub reboot

**Purpose:** Confirm the hub reloads paired devices from NVS without re-pairing.

**Result: PASS.** The boot banner was lost to USB re-enumeration on reset, but the behaviour is conclusive:

```
Reconnecting to COM4     Connected!
I (5609) TX CMD SET_LIGHT seq=44735  A0:74→81:74   ← unicast to strip
I (5609) RX ACK SET_LIGHT [OK]        81:74→A0:74
```

Unicasting to `B4:3A:45:8A:81:74` at 5.6 s uptime. That MAC can only have come from NVS. Zero `PAIR REQUEST` / `Auto-accepting` lines. Second reboot identical at 3.5 s. Knobs, mode switching, hue and white all worked immediately afterwards with no pairing exchange.

Later confirmed directly once the banner was caught:

```
I (1341) AutoPair: Controller mode — 1 paired device(s) loaded from NVS
I (1351) EspNowManager: Peer added: B4:3A:45:8A:81:74
```

**Pairing layer is done.** Retry/backoff, RX dedup, cached-ACK replay, and NVS persistence are all verified on hardware from both sides.

---

## Part 2 — Bugs

### 1. Hub blasted an identical SET_LIGHT every 2 seconds

**Problem:** The hub sent the same payload on a fixed 2 s timer regardless of whether anything had changed. Hundreds of identical lines:

```
TX CMD SET_LIGHT seq=18244  01 38 01 4A 14
TX CMD SET_LIGHT seq=18245  01 38 01 4A 14
TX CMD SET_LIGHT seq=18246  01 38 01 4A 14
```

Two costs: it buries real events in the log, and it will contend with OTA chunk traffic on the same radio.

**Fix:** Send-on-change (already handled by `panel.update()` returning true) plus a 30 s idle heartbeat. Three edits in hub `main/main.cpp`.

```cpp
#define HEARTBEAT_US      30000000LL    /* 30 s idle heartbeat */
static int64_t s_last_tx_us[2] = {0, 0};
```

Stamp inside `sendPanelState` — one line, so every existing call site gets it free:

```cpp
    MessageProtocol::instance().sendLightState(
        mac, panel.isOn(), panel.brightness(), panel.hue(),
        panel.whiteBright(), reliable);
    s_last_tx_us[panel.index()] = esp_timer_get_time();
```

Replaced the `static uint8_t ka` block in the 500 ms housekeeping:

```cpp
            int64_t hb = esp_timer_get_time();
            if (hb - s_last_tx_us[0] > HEARTBEAT_US) sendPanelState(panel0, false);
            if (hb - s_last_tx_us[1] > HEARTBEAT_US) sendPanelState(panel1, false);
```

**Result:** measured `seq=52854 @ 66754 ms` → `seq=52855 @ 96914 ms` = **30,160 ms**. That gap previously held 15 messages. Re-confirmed from a cold boot: main loop entered at 1531 ms, first transmission at 30521 ms.

**Known cost:** the 2 s keepalive was silently covering "strip rebooted, needs refreshing." At 30 s a rebooted strip can sit wrong for up to half a minute. The correct fix is not a faster heartbeat — it's the strip persisting its own state to NVS so it comes back right by itself. Rule #1: device owns state.

### 2. Hub doesn't dedup inbound ACKs — deliberately skipped

Only fires on retries, and there were zero retries across ~1300 messages once bug 1 and the SOLID_ON fix were in. Lives in `message_protocol.cpp`, not `main.cpp`, and amounts to cosmetic log noise. Not worth touching before OTA.

### 3. Latent — unpaired panel never stamps its heartbeat timer

**Problem:** Panel 1 has no light paired to it, so `sendPanelState(panel1, …)` hits the `getLightMac` early return and never reaches the stamp. `s_last_tx_us[1]` stays 0, so the heartbeat condition is true every 500 ms forever.

**Impact:** none visible — no radio traffic, no log lines, and it self-corrects the moment a second strip is paired.

**Fix (not yet applied):** move the stamp above the early return.

### 4. Build failed — partition table doesn't fit in configured flash size

**Problem:**

```
Partitions tables occupies 16.0MB of flash (16777216 bytes) which does
not fit in configured flash size 8MB.
```

`esptool flash_id` reported device ID `0x4018`, and `0x18` = 2^24 = 16 MB. The chip is right; the PlatformIO board definition for `esp32-s3-devkitc-1` claims 8 MB.

**Fix:** `board_upload.flash_size = 16MB` in `platformio.ini`, plus `sdkconfig.defaults`:

```
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_ESPTOOLPY_FLASHSIZE="16MB"
```

Then **delete `sdkconfig.s3_wroom`**. PlatformIO generates that file once and reuses it forever; edits to `sdkconfig.defaults` are ignored until it's removed. This bit twice today.

**Retracted:** `board_upload.maximum_size` was bad advice on my part. PlatformIO derives the app size limit from the partition table itself — overriding it to full flash just makes the usage percentage meaningless.

### 5. Stale `board_build.partitions` in the `[env]` base section

**Problem:** `board_build.partitions = partitions.csv` sat in the shared `[env]` block, so every env inherited a path to a file that no longer existed.

**Fix:** Removed from `[env]`; each board env now names its own table explicitly.

### 6. Version string showed a git hash instead of 0.1.0

**Problem:** Hub reported `FW v9352954-dirty` — ESP-IDF's fallback when `CONFIG_APP_PROJECT_VER` isn't set.

**Fix:** Same cached-sdkconfig trap as bug 4. Deleted `sdkconfig.s3_wroom` again, this time with all four config lines present in `sdkconfig.defaults`.

---

## Part 3 — OTA groundwork

### Partition tables

OTA needs two app slots: the running firmware stays in one while the incoming image downloads into the other, so a failed or broken transfer leaves a working device. The strip had a single ~1 MB slot at 85 % full on a 4 MB chip — three quarters of it unused.

**Design decision — tables keyed by flash size, not by device role.** First attempt named them `hub` and `strip-node`, which bakes in exactly the board coupling we're trying to remove. A partition table doesn't care what a device does, only how much flash the chip has. A 4 MB C6 running strip firmware and a 4 MB C3 running garage firmware use the identical file.

Uniform rule inside each: two equal app slots, remainder becomes `storage`. A controller uses that storage to stage firmware; a leaf node ignores it. No role encoded.

**Location:** `firmware/system/partitions/` — not `devices/`. Per-device placement means each project owns a copy. `system/` is the ecosystem contract layer, and rule #4 ("OTA partitions on every permanent device") is a system-wide rule; `ota_manager` depends on this exact layout. The table is the storage half of the contract, the way `message_protocol` is the wire half.

| File | app0 / app1 | storage | Fits |
|---|---|---|---|
| `partitions_4mb.csv` | 1.81 MB each | 320 KB | exact |
| `partitions_8mb.csv` | 3 MB each | 1.94 MB | exact |
| `partitions_16mb.csv` | 4 MB each | 7.94 MB | exact |

Each `platformio.ini` env points at the matching table:

```ini
[env:c6_seeed]
board_build.partitions = ../../../../system/partitions/partitions_4mb.csv

[env:s3_wroom]
board_build.partitions = ../../../../system/partitions/partitions_16mb.csv
board_upload.flash_size = 16MB
```

### Firmware identity

Added `logFirmwareIdentity()` as the first call in `app_main` on both devices, reading the running partition directly:

```cpp
static void logFirmwareIdentity(void) {
    const esp_partition_t* running = esp_ota_get_running_partition();
    const esp_app_desc_t*  desc    = esp_app_get_description();
    ESP_LOGI(TAG, "  FW v%s   built %s %s", desc->version, desc->date, desc->time);
    ESP_LOGI(TAG, "  Slot: %s @ 0x%06lX  (%lu KB)",
             running->label,
             (unsigned long)running->address,
             (unsigned long)running->size / 1024);
}
```

### Staged image

Wrote the strip's firmware into the hub's `storage` partition with esptool. Looks wrong and isn't — a C6 image on an S3, but at `0x810000`, which is typed `data`. The S3 bootloader only executes from `app0`/`app1`, so it's cargo, not code. `--force` needed to get past the chip-mismatch guard.

```powershell
pio pkg exec -p tool-esptoolpy -- esptool.py --port COM4 --chip esp32s3 `
  write_flash --force 0x810000 ..\strip-node\.pio\build\c6_seeed\firmware.bin
```

`logStagedImage()` reads the app descriptor back at offset 32 (24-byte image header + 8-byte first segment header), checks the magic word, and reports the version.

---

## Results

| | Chip | Flash | Table | App slot | Used | Second slot |
|---|---|---|---|---|---|---|
| Strip | ESP32-C6FH4 | 4 MB | `partitions_4mb.csv` | 1.81 MB | 47.1 % (895,527 B) | ✅ `app1` |
| Hub | ESP32-S3 + 8 MB PSRAM | 16 MB | `partitions_16mb.csv` | 4 MB | 19.2 % (803,285 B) | ✅ `app1` |

Strip went from **85 % → 47.1 %** on identical firmware — it was cramped, now it isn't.

Final banners:

```
I (410) strip_node:   FW v0.1.0   built Jul 23 2026 19:55:02
I (420) strip_node:   Slot: app0 @ 0x010000  (1856 KB)

I (391) hub:   FW v0.1.0   built Jul 23 2026 20:01:03
I (391) hub:   Slot: app0 @ 0x010000  (4096 KB)
I (411) hub: Staged image: "test_smart_light" v0.1.0  built Jul 23 2026 19:55:02
```

The staged image's build timestamp matches the strip's own banner to the second — the hub is reading the strip's real firmware out of its storage partition.

---

## Verified

- [x] Fresh pair after NVS erase — one ACK per seq, zero cached duplicates
- [x] SOLID_ON fix confirmed: retry storm gone
- [x] Encoder + touch + hue + white path end-to-end
- [x] Hub reboot reloads paired devices from NVS, no re-pairing
- [x] Knobs work immediately after hub reboot
- [x] 30 s heartbeat measured at 30,160 ms, and from cold boot
- [x] Both partition tables applied and confirmed from inside the running firmware
- [x] Strip app slot = 1,900,544 B = 0x1D0000, matches `partitions_4mb.csv`
- [x] Hub app slot = 4,194,304 B = 0x400000, matches `partitions_16mb.csv`
- [x] Firmware version + slot reported on both devices
- [x] Strip firmware staged on hub and parsed correctly
- [ ] Committed

---

## Files modified

**Added** — `firmware/system/partitions/`:
- `partitions_4mb.csv` *(new)*
- `partitions_8mb.csv` *(new)*
- `partitions_16mb.csv` *(new)*

**Modified** — `testing/devices/smart-light-test/hub/`:
- `main/main.cpp` — heartbeat constants + `s_last_tx_us[]`, stamp in `sendPanelState`, replaced 2 s keepalive block, `logFirmwareIdentity()`, `logStagedImage()`, OTA/partition includes
- `platformio.ini` — removed stale `board_build.partitions` from `[env]`, per-env tables, `board_upload.flash_size = 16MB`
- `sdkconfig.defaults` — flash size + project version

**Modified** — `testing/devices/smart-light-test/strip-node/`:
- `main/main.cpp` — `logFirmwareIdentity()`, OTA includes
- `platformio.ini` — 4 MB table
- `sdkconfig.defaults` *(new)* — project version

**Deleted:** `sdkconfig.s3_wroom`, `sdkconfig.c6_seeed` (regenerated)

---

## Key takeaways

1. **Partition tables belong to flash size, not to devices.** Keying by role duplicates the file and recreates board coupling. Keying by capacity means a new chip is one line in `platformio.ini`.
2. **PlatformIO caches the generated `sdkconfig.<env>` and ignores `sdkconfig.defaults` until you delete it.** Cost two round trips today — once on flash size, once on version string. Delete it every time defaults change.
3. **A board definition's flash size is a claim, not a fact.** `flash_id` device ID `0x4018` = 16 MB. Trust the chip.
4. **The app-size denominator in the build output is the partition slot.** `used X from 1900544` is proof the table applied — no other value produces that number. Faster than hunting the boot log.
5. **Writing foreign-architecture firmware into a `data` partition is safe.** The bootloader only executes `app` partitions. `--force` gets past esptool's chip guard.
6. **A single removed `vTaskDelay` fixed the retry storm.** Blocking inside a protocol callback stalls the RX path long enough for the sender to time out and retry.
7. **Full erase costs nothing now that pairing is automatic.** Measured re-pair: 70 ms. Erase freely.
8. **Verify from inside the running firmware, not from the build log.** `esp_ota_get_running_partition()` reports what the device is actually executing.

---

## Next steps

- [ ] Commit
- [ ] **OTA transfer over ESP-NOW** — the big one:
  - [ ] Version comparison: strip reports its version, hub decides whether to offer an update
  - [ ] Chunked transfer with resume (~900 KB over 24-byte payloads)
  - [ ] Checksum verify before slot flip
  - [ ] `esp_ota_set_boot_partition` + rollback if the new image doesn't boot
- [ ] Strip persists its own light state to NVS — closes the 30 s stale window from bug 1
- [ ] One-line fix for bug 3 (stamp above early return)
- [ ] BSP layer — pin maps and chip quirks (RMT `mem_block_symbols` 64 on ESP32 vs 48 on C6/S3) are still hard-coded. Partitions were only the first piece of board-agnostic.
- [ ] Verify flash size on the three unconfirmed boards before trusting their env entries (`esp32dev`, `esp32-c6-devkitc-1`, `seeed_xiao_esp32s3`)

**Cosmetic, logged, untouched:**
- `SmartLightDevice` logs GPIO 10 in its init message; actual data pin is 18
- `spi_bus_initialize(809): SPI bus already initialized` on second GC9A01 — expected, shared bus, but logged as an error
- `gpio_install_isr_service(526): already installed` on the second encoder
- Both touch sensors report `initial state: TOUCHED` at boot










##############################################################################################################################################################################################################################################################################################################################################################################################################################


# Dev Log — Repo Hygiene, Roadmap Freeze, and the Identity Layer

**Date:** Saturday, 25/07/2026

**Goal:** Clean the repository of tracked build artifacts, agree a phase order for the rest of the smart light, then lay the first piece of the v3 wire format — a permanent device identity that does not depend on the radio.

---

## Part 1 — Roadmap

The old task list had grown by accretion and put the strip on the ceiling before OTA existed. Re-ordered around a single constraint: **anything hard to change later must be frozen before the strip goes up.**

| Phase | Work | Why here |
|---|---|---|
| 0 | Repo hygiene | Trivial, blocks nothing, do it first |
| 1 | Freeze v3 wire format — UIDs + house/room/node | Pairing UI renders identity; multi-hub tests need addressing |
| 2 | Make the strip un-brickable — OTA over ESP-NOW | Gate for everything physical |
| 3 | Power at real load — level shifter, 10 → 144 LEDs | Needs Phase 2 done first, in case firmware must change |
| 4 | Hub hardware + UI — ST7789V2 + GC9A01, pairing UI, BSP | Builds on frozen identity |
| 5 | Solder, enclosure, install | **END OF PHASE 5 = HARDWARE FREEZE** |
| 6 | Multi-device interference + addressing | |
| 7 | Garage controller | |

**Two moves from the original ordering.** UIDs came forward from near-last to Phase 1: the pairing UI displays device identity, so building it on MAC means rebuilding it afterwards, and the multi-device test is meaningless without addressing. Hanging the strip went from step 3 to Phase 5, behind a working rollback test.

**Standalone operation — resolved.** Initial instinct was that a strip with a dead hub is meaningless because the hub is the switch. Wrong: the strip has its own 5V PSU and the hub is only a *logical* switch, so the strip stays lit with the hub off. The real question is what happens when the *strip* power-cycles. Decision: persist last state in NVS plus a `power_on_behavior` byte (last / off / on), defaulting to last-state, with NVS writes debounced ~10 s to protect flash endurance during encoder sweeps.

**Factory reset — no wall switch exists.** The strip wires directly to mains, so power-cycle-count reset is not available. Instead: if the strip has not heard from its paired hub in ~10 minutes it re-opens for pairing while staying lit. Self-healing, no hardware. A button inside the enclosure gets added anyway as insurance.

---

## Part 2 — Repo hygiene

**Problem:** the repository tracked 1,144 files. 846 of them were not source.

| Files | What |
|---|---|
| 771 | `managed_components/espressif__mdns` — auto-downloaded by ESP-IDF, committed into 7 test projects |
| 73 | generated `sdkconfig.<env>` |
| 2 | `dependencies.lock` |
| **298** | **actual project files** |

The `sdkconfig.<env>` entries are the ones that actually cost time: PlatformIO regenerates them and then ignores `sdkconfig.defaults` until they are deleted. That trap burned two round trips on 23/07 (flash size, then version string). Tracked in git, a fresh clone would start pre-poisoned with a stale config and the same bug would be rediscovered with no reason to suspect it.

**Fix:**

```gitignore
managed_components/
dependencies.lock
sdkconfig.*
!sdkconfig.defaults
```

The negation matters — `sdkconfig.defaults` is source, `sdkconfig.s3_wroom` is output.

```powershell
git rm -r --cached -q .
git add .
git commit -m "chore: untrack managed_components and generated sdkconfig"
```

**Result:** 846 deletions, verified against a tree dump before committing (predicted 846, `git status` showed 847 — the extra line being `.gitignore` itself as modified). 7 `sdkconfig.defaults` survived, all of them real; the other 28 were Espressif's own example configs inside the vendored library. `managed_components` tracked count: 0.

Push wrote 42 objects totalling 2.94 KiB — deletions only create tree objects, never blobs.

---

## Part 3 — Wire format design (frozen, not yet implemented)

The v2 packet carries `src_mac[6]` and `dst_mac[6]`. On ESP-NOW those 12 bytes duplicate what the radio already does; on LoRa they are meaningless. MAC in the wire format welds the protocol to one transport, which breaks ecosystem rule #3.

**Reclaiming those 12 bytes pays for the entire addressing scheme with zero size change:**

| Field | Bytes | |
|---|---|---|
| `magic` `proto_ver` `msg_type` `cmd_id` `flags` `seq` | 10 | unchanged |
| ~~`src_mac[6]` `dst_mac[6]`~~ | ~~12~~ | **removed** |
| `house_id` | 2 | new |
| `src_uid` | 4 | new |
| `dst_uid` | 4 | new |
| `dst_room` `dst_node` | 2 | new |
| `payload_len` `status` | 2 | unchanged |
| **header** | **24** | identical to v2 |

Packet stays 48 bytes. Reliability, dedup, and retry logic are untouched — they key off `seq`, not addressing.

**Addressing:** `dst_uid != 0` is unicast; `dst_uid == 0` falls through to `dst_room`/`dst_node` with `0xFF` as wildcard at either level. `house_id` mismatch drops at the top of `processMessage`, which makes two neighbouring installations mutually invisible — Phase 6's interference problem solved as a side effect. `HOUSE_UNASSIGNED` (0) matches everything in both directions so a factory-fresh device can still complete pairing.

**MAC does not disappear, it moves down** into the transport as a `uid → MAC` table, populated from the paired list. LoRa later swaps the table, not the format.

**room/node live in NVS, never in `#define`.** Compile-time constants would mean reflashing a ceiling-mounted device to change which room it is in. `SET_IDENTITY` is reserved in the CmdId map now; the app or main controller fills it in over the air later.

---

## Part 4 — Bugs

### 1. `esp_efuse_mac_get_default()` returns a truncated EUI-64 on the C6

**Problem:** the strip reported a UID derived from `B4:3A:45:FF:FE:8A` while its actual MAC is `B4:3A:45:8A:81:74`.

The C6 stores its address in eFuse as 8 bytes in EUI-64 form — the real MAC with `FF:FE` inserted after the OUI, giving `B4:3A:45:FF:FE:8A:81:74`. Reading "the first 6 bytes" returns the prefix and discards `81:74`, which is the only genuinely unique part.

**Severity:** of the 6 bytes being hashed, `B4:3A:45` is Espressif's OUI (identical on every chip they ship) and `FF:FE` is constant padding. **One byte varies — 256 possible UIDs.** At ~20 devices that is a better-than-even chance of a collision, and a UID collision means two nodes answering to one address.

**Fix:**

```cpp
esp_err_t err = esp_read_mac(mac, ESP_MAC_BASE);
```

`ESP_MAC_BASE` is the address every interface derives from, correctly collapsed back to EUI-48. Still read from eFuse, so still erase-proof. Preferred over `ESP_MAC_WIFI_STA` because the garage node on LoRa may never bring up WiFi.

**Verified:** hub reported `UID FEBDCDC3`; `CRC32(B8:F8:62:E0:A0:74)` computed independently = `FEBDCDC3`. Exact match. The old broken value `4E086A08` also reproduces exactly as `CRC32(B4:3A:45:FF:FE:8A)`, confirming the diagnosis rather than guessing at it.

### 2. Unpaired panel never stamps its heartbeat timer (carried from 23/07)

**Problem:** `sendPanelState()` hit the `getLightMac()` early return before reaching the timestamp, so `s_last_tx_us[1]` stayed 0 and the heartbeat condition was true every 500 ms forever.

**Fix:** stamp first, then return.

```cpp
static void sendPanelState(const SmartLightRemote& panel, bool reliable) {
    s_last_tx_us[panel.index()] = esp_timer_get_time();   /* stamp first */

    uint8_t mac[6];
    if (!getLightMac(panel.index(), mac)) return;
    ...
}
```

### 3. Two dead `-I` paths in every `platformio.ini`

**Problem:** `-I../../../../wireless/communication/esp_now` — there is no `communication/` level. `-I../../../modules/smart-light` resolves to `testing/modules/smart-light`, which does not exist.

**Impact:** none on the build. ESP-IDF supplies real include paths through `REQUIRES`; these flags only feed IntelliSense, which is why VS Code has been showing red squiggles on headers that compile fine.

**Status:** logged, not yet fixed.

### 4. Boot output lost to USB CDC enumeration

**Not a firmware bug**, but it cost most of the session. `pio run -t upload -t monitor` prints `Hard resetting via RTS pin...` *before* the monitor attaches, so the first ~1.2 s on the S3 and ~3.5 s on the C6 is unrecoverable. Splitting upload and monitor into two commands does not help — the port is still enumerating.

Symptom is distinctive: the log opens mid-line, e.g. `segment 0: paddr=00010020 vaddr=420a0020 size=I (3520) EspNowManager:`.

**Workaround:** log identity late, after a known-visible line, rather than in the first milliseconds. Blunt alternative for genuinely early output is a `vTaskDelay(1500)` at the top of `app_main` — debugging only.

### 5. `SmartLightDevice` logs the LED count in the GPIO slot

```
I (513) SmartLightDevice: Initialized: 10 SK6812 RGBW LEDs on GPIO 10
```

Previously logged as "wrong GPIO". It is worse than that — `getNumLeds()` is passed twice, and both values happening to be 10 hid it. **Status:** open.

---

## Part 5 — `device_identity`

New component at `firmware/system/device_identity/`. Two deliberately separate ideas:

- **UID** — permanent, derived, never configured. `CRC32(eFuse base MAC)`.
- **house / room / node** — assigned, stored in NVS, changeable at runtime and therefore over the air.

**Why derived and not generated:** the obvious design is `esp_random()` on first boot saved to NVS. During development a full erase happens constantly, and after each one the device would invent a new identity — its own hub would see a stranger and every paired relationship would silently break.

**Why 4 bytes:** 2 bytes gives ~7% collision probability across 100 devices. 4 bytes is free anyway, since the header lands on exactly 24 either way.

`UID_NONE` (0) is the "address by room+node" sentinel, so `uidFromMac()` bumps a hashed 0 to 1. Odds are 1 in 4.3 billion; a silent address collision would be miserable to debug.

---

## Results

| | Reported | Independently computed | Match |
|---|---|---|---|
| Hub UID | `FEBDCDC3` | `CRC32(B8:F8:62:E0:A0:74)` = `FEBDCDC3` | ✅ |
| Hub house | `0x2B6C` | minted once, persisted | ✅ |
| Strip UID (old, broken) | `4E086A08` | `CRC32(B4:3A:45:FF:FE:8A)` = `4E086A08` | ✅ diagnosis confirmed |
| Strip UID (expected) | — | `CRC32(B4:3A:45:8A:81:74)` = `EE907A91` | ⏳ pending |

Image growth from linking `device_identity`: hub 803,285 → 805,568 B, strip 895,527 → 897,280 B.

**Erase test, strip:** `failed to load RF calibration data (0x1102)` confirms a genuinely blank flash. Re-pair took **30 ms** (`Not paired` at 3820 ms → `PAIRED` at 3850 ms), with the hub logging `Known device re-requesting — auto re-accepting` from its own surviving NVS entry.

---

## Verified

- [x] 846 non-source files untracked, predicted count matched actual
- [x] `sdkconfig.defaults` survived (7, all real); `managed_components` tracked count 0
- [x] Pushed clean — 42 objects, 2.94 KiB
- [x] `device_identity` compiles and links into both devices
- [x] Hub UID matches independently computed CRC32 of its real MAC
- [x] Hub house id minted once and stable across reboots
- [x] EUI-64 truncation bug diagnosed and reproduced numerically, not guessed
- [x] Strip survives full erase and re-pairs in 30 ms
- [ ] **Strip UID confirmed as `EE907A91` after the `ESP_MAC_BASE` fix**
- [ ] Strip UID confirmed identical across a full erase
- [ ] This session committed

---

## Files modified

**Added** — `firmware/system/device_identity/`:
- `device_identity.h` *(new)*
- `device_identity.cpp` *(new)*
- `CMakeLists.txt` *(new)*

**Modified** — `firmware/system/core_types/`:
- `core_types.h` — v1.0.0 → v1.1.0; `DeviceUid`, `HouseId`/`RoomId`/`NodeId`, wildcard and unassigned sentinels

**Modified** — `firmware/testing/devices/smart-light-test/hub/`:
- `main/main.cpp` — `device_identity.h`, `id.begin()`, first-boot house provisioning, late identity banner, bug 2 fix
- `main/CMakeLists.txt` — `device_identity` in `REQUIRES`
- `CMakeLists.txt` — `device_identity` in `EXTRA_COMPONENT_DIRS`
- `platformio.ini` — `-I` for `device_identity`

**Modified** — `firmware/testing/devices/smart-light-test/strip-node/`:
- `main/main.cpp` — `device_identity.h`, `DeviceIdentity::instance().begin()`, late identity banner
- `main/CMakeLists.txt`, `CMakeLists.txt`, `platformio.ini` — as above

**Modified** — root:
- `.gitignore` — `managed_components/`, `dependencies.lock`, `sdkconfig.*`, `!sdkconfig.defaults`

---

## Key takeaways

1. **`esp_efuse_mac_get_default()` is not portable across ESP32 variants.** On the C6 it hands back a truncated EUI-64 whose varying part has been cut off. Use `esp_read_mac(mac, ESP_MAC_BASE)`.
2. **Verify derived identifiers against an independent computation.** Reading `UID FEBDCDC3` in a log proves nothing. Computing `CRC32` of the MAC separately and getting the same value proves the input was right — and recomputing the *old broken* value confirmed the diagnosis instead of leaving it a plausible story.
3. **A UID's entropy is the entropy of its input, not its width.** A 32-bit hash of a 6-byte buffer with 5 constant bytes has 256 possible outputs.
4. **`git rm -r --cached .` then `git add .` re-applies `.gitignore` across the whole tree in one shot.** Predict the deletion count first and compare before committing.
5. **`sdkconfig.defaults` is source; `sdkconfig.<env>` is output.** The `!` negation line is what keeps the distinction.
6. **USB CDC enumeration eats early boot logs and no monitor timing fixes it.** Log identity after a line you can already see.
7. **Compile-time device configuration is a trap for anything that gets installed.** A `#define ROOM_ID` means a ladder. NVS plus a reserved wire-format command costs the same today and nothing later.
8. **Build duration is a staleness signal.** A 7.7 s build where previous ones took 9–18 s means nothing recompiled — usually an unsaved file.

---

## Next steps

- [ ] Confirm strip `UID EE907A91`, then re-run the erase test and confirm it is unchanged
- [ ] **Phase 1 remainder — `message_protocol` v3:**
  - [ ] New 24-byte header: drop `src_mac`/`dst_mac`, add `house_id` / `src_uid` / `dst_uid` / `dst_room` / `dst_node`
  - [ ] `uid → MAC` table in `esp_now_manager`, populated from the paired list
  - [ ] `auto_pair` exchanges UIDs and pushes `house_id` to the joining node
  - [ ] Callback signatures: `const uint8_t src_mac[6]` → `DeviceUid src_uid`
  - [ ] Reserve `SET_IDENTITY` / `GET_IDENTITY` / `REPORT_IDENTITY` at 0x74–0x76
- [ ] Change `storage` partition subtype from `spiffs` to `undefined` in all three tables, so nothing can format the staged image
- [ ] Strip persists light state to NVS + `power_on_behavior` byte, writes debounced ~10 s

**Cosmetic, logged, untouched:**
- `SmartLightDevice` logs `getNumLeds()` in the GPIO slot (bug 5)
- Dead `-I` paths in both `platformio.ini` files (bug 3)
- `spi_bus_initialize(809): SPI bus already initialized` on second GC9A01
- `gpio_install_isr_service(526): already installed` on second encoder
- Both touch sensors report `initial state: TOUCHED` at boot



##############################################################################################################################################################################################################################################################################################################################################################################################################################



# Dev Log — 27/07/2026 (Mon)

**Project:** Smart Home Ecosystem — firmware repository
**Scope:** Close Phase 1 open items; freeze the v3 wire format (UID + house/room/node addressing)
**Outcome:** ✅ Phase 1 complete — MAC removed from the wire, pairing now commissions

---

## Summary

Three jobs. First, close the two boxes left open on 25/07 — the strip's UID had never actually
printed. Second, two small bugs found along the way. Third, the session's real work:
**message_protocol v3**, which takes MAC out of the packet header entirely and replaces it with
a permanent device UID plus logical house/room/node addressing.

Six files were rewritten in one pass and both projects built clean on the first attempt. Pairing
from blank flash on both sides completed in **70 ms**, including commissioning.

---

## Bugs

### Bug 1 — Strip identity banner was never on disk

**Problem:**
Three reflashes across two sessions never printed the strip's UID. The 25/07 diagnosis (stale
binary) was wrong. `Select-String main\main.cpp -Pattern "uidString"` returned nothing — the
editor edit had never been saved. The `device_identity.h` include *was* present, which is why
the build succeeded and the absence looked like a runtime problem.

**Fix:**
Inserted the banner from PowerShell instead of the editor, then verified on disk before
spending a flash cycle:

```powershell
$c = $c.Replace($anchor, $anchor + $banner)
Set-Content -Path $f -Value $c -NoNewline
Select-String -Path $f -Pattern "uidString"     # must print before flashing
```

**Result:** `UID EE907A91` — an exact match for the CRC32 of `B4:3A:45:8A:81:74` computed
independently on 25/07. Confirmed identical after a full `erase_flash`.

---

### Bug 2 — Strip configured for 2 MB flash on a 4 MB chip

**Problem:**
Every build printed `Warning! Flash memory size mismatch detected. Expected 4MB, found 2MB!`
`sdkconfig.defaults` had no `CONFIG_ESPTOOLPY_FLASHSIZE` line at all, so ESP-IDF fell back to
its 2 MB default.

Nothing failed today because `app0` ends at `0x1E0000`, just under the 2 MB ceiling. But
`app1` (`0x1E0000`–`0x3B0000`) and `storage` both sit entirely *above* it — OTA had nowhere to
write. The partition CSV was correct throughout; this was the bootloader's view of the chip,
which is a separate setting.

**Fix:**

```
CONFIG_ESPTOOLPY_FLASHSIZE_2MB=n
CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y
CONFIG_ESPTOOLPY_FLASHSIZE="4MB"
```

Then `Remove-Item sdkconfig.c6_seeed` — without deleting the cache the change is ignored.
Rebuild took 51 s (vs 7–22 s incremental), confirming full regeneration.

**Result:** `sdkconfig.c6_seeed:599 CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y`. Warning gone. Hub was
already correct at 16 MB from the 23/07 session.

---

### Bug 3 — `SmartLightDevice` logged the LED count as the GPIO number

**Problem:**
Logged `10 SK6812 RGBW LEDs on GPIO 10` while actually driving GPIO 18. Not a hardcoded
string — a copy-paste bug passing the same getter twice:

```cpp
ESP_LOGI(TAG, "Initialized: %d SK6812 RGBW LEDs on GPIO %d",
         _strip.getNumLeds(), (int)_strip.getNumLeds());   // ← twice
```

`AddressableLED` had no pin getter, and `SmartLightDevice` takes the pin in its constructor
without storing it, so there was nothing correct to pass.

**Fix:**
Added `getPin()` to `AddressableLED` (the class that owns the pin) rather than caching a copy
in `SmartLightDevice`. The hub UI will want it later too.

```cpp
gpio_num_t AddressableLED::getPin() const { return pin; }
```

**Result:** `Initialized: 10 SK6812 RGBW LEDs on GPIO 18`. Closes an item open since 10/07.

---

## Wire format v3

MAC is gone from the header. A MAC is a property of a *radio*, not a device — ESP-NOW has one,
LoRa does not — so carrying it in the packet welded the protocol to one transport and broke
ecosystem rule #3 (swappable transport).

| off | size | field | notes |
|---|---|---|---|
| 0 | 4 | `magic` | `"SMM3"` |
| 4 | 4 | `src_uid` | |
| 8 | 4 | `dst_uid` | `UID_NONE` → group address |
| 12 | 2 | `house` | checked before anything else |
| 14 | 2 | `seq` | |
| 16 | 1 | `proto_ver` | 3 |
| 17 | 1 | `msg_type` | |
| 18 | 1 | `cmd_id` | |
| 19 | 1 | `flags` | |
| 20 | 1 | `dst_room` | `ROOM_ALL` = every room |
| 21 | 1 | `dst_node` | `NODE_ALL` = every node in room |
| 22 | 1 | `payload_len` | |
| 23 | 1 | `status` | |
| 24 | 24 | `payload` | |

**48 bytes total, unchanged.** Two MAC arrays (12 B) out, two UIDs + house/room/node (12 B) in.

Fields were reordered so every multi-byte value lands on its natural alignment — v2 had
`src_mac` starting at offset 10. Free to fix during a break, impossible afterwards. Enforced
by `static_assert` on `offsetof`.

**Addressing:** `dst_uid != UID_NONE` → unicast. `dst_uid == UID_NONE` → group via
`dst_room`/`dst_node`. Broadcast is `UID_NONE + ROOM_ALL + NODE_ALL`. `house` is an
independent first gate; `HOUSE_UNASSIGNED (0)` matches everything in both directions so a
factory-fresh device can still pair.

---

## The UID → MAC seam

Removing MAC from the wire does not remove ESP-NOW's need for one. Resolution lives behind
**two functions**, injected into `MessageProtocol`:

```cpp
msg.setUidResolver([](DeviceUid uid, uint8_t mac[6]) {
    return AutoPair::instance().resolveUid(uid, mac);
});
msg.setPeerObservedCallback([](DeviceUid uid, const uint8_t mac[6]) {
    AutoPair::instance().noteAddress(uid, mac);
});
```

`auto_pair` owns the table today. When `device_registry v2` arrives it implements these two
functions and **nothing else in the ecosystem changes** — that was the whole argument for
deferring it rather than building it now against a requirement set of "one node, no UI".

**Resolution failure is not an error.** A miss falls back to broadcast and the receiver filters
on `dst_uid`. The resolver is a bandwidth optimisation, not a correctness requirement — which
is exactly what lets pairing work before any mapping exists, and what makes a stale entry
self-correct instead of bricking communication.

The table is seeded from NVS at `begin()` (so unicast works on the first packet after reboot)
and refreshed by observation of *all* inbound traffic in our house, not just packets addressed
to us.

---

## Pairing now commissions

`PAIR_ACCEPT` gained a 4-byte payload: `[house_lo, house_hi, room, node]`. Without it a paired
device would sit at `HOUSE_UNASSIGNED` forever, matching every house in radio range — which
defeats the entire point of the house id.

- `beginAsController()` **self-provisions**: mints a house if it has none, takes room 1 / node 1.
  A hub that could accept pairings while stamping house 0 would adopt, and be adopted by,
  anything nearby.
- New devices inherit the hub's room and get the lowest unused node ≥ 2.
- `SET_LOCATION` (0x74) re-addresses a device over the air, **authorised to the device's own
  paired controller only** — otherwise any device in range could re-address the installation.
- `unpair()` clears house/room/node by default; a device that left its house must stop
  answering to it.

NVS record layout changed → new key `ap_devs2`. Old `ap_devs` is orphaned, not migrated.

---

## Test results

| Test | Result |
|---|---|
| Strip UID after ESP_MAC_BASE fix | ✅ `EE907A91` — matches independent CRC32 |
| Strip UID identical across full erase | ✅ unchanged |
| Hub mints house on blank flash | ✅ `0x6F19`, room 1 node 1 |
| Pair + commission from blank flash, both sides | ✅ **70 ms** (3881 → 3951 ms) |
| `PAIR_ACCEPT` payload | ✅ `19 6F 01 02` = house `0x6F19`, r1, n2 |
| ACK unicast before any pairing record existed | ✅ peer-observed hook resolved it |
| Strip reboot — identity + pairing from NVS | ✅ zero `PAIR_REQ`, peer added at 3782 ms |
| Hub reboot — `ap_devs2` record survives | ✅ `[0] EE907A91 Light r1/n2 "Strip 1"` |
| Heartbeat unicast both ways post-reboot | ✅ 30.5 s intervals, ACKs match on UID |

---

## Build results

| App | Target | RAM | Flash | Time | Result |
|---|---|---|---|---|---|
| `smart-light-test/strip-node` | c6_seeed | 11.9% (38,944 B) | 47.5% (902,099 B) | 22.0 s | ✅ PASS |
| `smart-light-test/hub` | s3_wroom | 14.5% (47,428 B) | 19.3% (810,913 B) | 17.7 s | ✅ PASS |

Strip grew +4,866 B over v2. Six files rewritten, **zero compile errors on first build**.

---

## Verified

- [x] Strip UID confirmed as `EE907A91` after the ESP_MAC_BASE fix
- [x] Strip UID confirmed identical across a full erase
- [x] Strip flash size corrected to 4 MB — `app1` and `storage` now addressable
- [x] `SmartLightDevice` logs the correct GPIO
- [x] v3 header is 48 B with all multi-byte fields naturally aligned
- [x] Both projects build clean
- [x] Full pair + commission from blank flash on both sides
- [x] Identity and pairing survive reboot on both devices
- [x] Unicast works on first packet after reboot (address table seeded from NVS)
- [ ] `SET_LOCATION` exercised on hardware — implemented, never called
- [ ] `sendCommandToGroup` exercised on hardware — implemented, never called
- [ ] Committed and pushed

---

## Files modified

**`firmware/system/message_protocol/`**
- `message_protocol.h` — v3.0.0. New packet struct, `MsgUidResolver`, `MsgPeerObservedCb`,
  `SET_LOCATION`, `msgEncodeLocation`/`msgDecodeLocation`, `sendCommandToGroup`,
  `sendLocation`. All callbacks and senders take `DeviceUid`.
- `message_protocol.cpp` — v3.0.0. `begin()` hard-fails without `DeviceIdentity`. Two-gate RX
  filter. Dedup rekeyed to UID. Log format now `FEBDCDC3→EE907A91` / `ALL` / `r1/n2`.
- `CMakeLists.txt` — `REQUIRES` += `core_types device_identity`

**`firmware/system/auto_pair/`**
- `auto_pair.h` / `auto_pair.cpp` — v3.0.0. UID-keyed throughout. Owns the address table.
  `beginAsController()` self-provisions. `PAIR_ACCEPT` carries location. `SET_LOCATION`
  handler with authorisation check. NVS key → `ap_devs2`.
- `CMakeLists.txt` — `REQUIRES` += `device_identity`

**`firmware/components/addressable/`**
- `addressable_led.h` / `.cpp` — added `getPin()`

**`firmware/testing/devices/smart-light-test/`**
- `strip-node/main/main.cpp` — v3 handler signature; `app_main` reordered so the protocol
  layer is fully wired before the radio starts; `ConfigStore` before `DeviceIdentity`;
  `begin()` failures now bail.
- `strip-node/sdkconfig.defaults` — flash size 4 MB
- `hub/main/main.cpp` — same reordering; `getLightUid()` replaces `getLightMac()`; paired-list
  boot banner; manual `provisionAsNewHouse()` removed (now in `beginAsController()`).

---

## Key takeaways

1. **Verify an edit is on disk before spending a flash cycle.** Two sessions were lost to a
   file that was never saved. `Select-String` for the new symbol costs nothing and would have
   caught it immediately. Build time is a weak signal; on-disk content is the real one.
2. **Partition CSV and `CONFIG_ESPTOOLPY_FLASHSIZE` are independent.** A correct partition
   table can still be unreachable if the bootloader thinks the chip is smaller. Silent until
   OTA needs the second slot.
3. **Fix alignment during a wire-format break or never.** Reordering v3's header cost nothing
   today and would have been impossible after devices shipped.
4. **A resolver that may fail is more robust than one that must succeed.** Falling back to
   broadcast means a missing or stale UID→MAC entry degrades bandwidth, not function — that
   single property is what allows bootstrap (pairing before any mapping exists) and
   self-healing (a device that changes radios recovers on its next packet).
5. **Learn addresses from observation, not just from pairing.** The peer-observed hook proved
   load-bearing on its first run: the strip ACKed the `PAIR_ACCEPT` as true unicast, before
   any pairing record existed on its side.
6. **Refuse to start rather than start wrong.** `MessageProtocol::begin()` fails if
   `DeviceIdentity` isn't ready, because a packet accidentally stamped house 0 would be
   accepted by every installation in radio range — a silent, near-undiagnosable failure.
7. **Wire order before power order.** Both `app_main`s now finish protocol setup before the
   radio starts; the RX task delivers packets the instant `enm.begin()` returns.

---

## Next steps

- [ ] Exercise `SET_LOCATION` and `sendCommandToGroup` on the bench
- [ ] **Phase 2 — make the strip un-brickable:** OTA transport-neutral sink API
      (`beginWrite`/`writeChunk`/`finishWrite`/`abortWrite`), ESP-NOW bulk plane, chunked
      transfer, CRC32 verify, boot flip, deliberate-corrupt-image rollback test
- [ ] Strip state persistence in NVS + `power_on_behavior` byte (debounce writes ~10 s)
- [ ] Change `storage` partition subtype `spiffs` → `undefined` so nothing can format it
- [ ] Pairing recovery: re-open for pairing if the hub is unheard for ~10 min, staying lit
- [ ] GC9A01: hoist `spi_bus_initialize` into a shared display-bus module (still logs E on the
      second display)
- [ ] TouchSensor reports `initial state: TOUCHED` at boot on both pads — phantom touch
- [ ] Encoder: guard `gpio_install_isr_service` already-installed (benign E on second encoder)




##############################################################################################################################################################################################################################################################################################################################################################################################################################


# Dev Log � 27/07/2026 (Mon, session 2)

**Project:** Smart Home Ecosystem � firmware repository
**Scope:** Close the SET_LOCATION desync gap left open by v3; exercise group addressing on hardware
**Outcome:** ? Both unticked boxes from session 1 closed. Phase 1 is done.

---

## The gap

v3 shipped `MessageProtocol::sendLocation()`, which re-addresses a device over the air.
It works � and it desynchronises the hub, because `MessageProtocol` knows nothing about
the paired list. Move the strip to room 2 and the strip updates its NVS while the hub's
`PairedDevice` record still reads r1/n2. Every later `sendCommandToGroup(2, ...)` would
silently miss a device the hub believes is somewhere else. No error, no log, no symptom
until someone notices a light not responding.

The fix belongs in `auto_pair`, which owns the record.

## `relocateDevice(uid, room, node)`

Sends `SET_LOCATION` reliably and updates the local record **only once the device ACKs**.

Committing first and sending second would produce the mirror-image bug. Send-then-commit
means a failed move leaves both sides consistent at the old address � a stale hub record is
strictly worse than a move that didn't happen.

"Wait for the ACK" is not a straight line, though. `sendCommandReliable()` returns when the
first TX is queued, not when the ACK lands, so the commit has to happen on a later code path.
Hence a small in-flight table (`_reloc`) plus `noteDeliveryResult()`, fed from the hub's
existing `MsgDeliveryCb`. One line in `main.cpp`; no change to `message_protocol`.

**Correlation is on `(cmd, dst_uid)`, not `seq`.** The ACK can arrive before
`sendCommandReliable()` has returned, so a seq recorded afterwards would race against its own
completion. One move per device at a time makes the uid a sufficient key, and the "already in
flight" guard is what enforces that.

Two refusals rather than two silent overwrites:
- An explicitly requested room/node that is already occupied ? `ESP_ERR_INVALID_ARG`.
  Two devices at one address makes group sends hit both � the exact class of bug being closed.
- A second move for a device whose first is still unconfirmed ? `ESP_ERR_INVALID_STATE`.

`node = NODE_UNASSIGNED` auto-allocates: keeps the current number if the device is already in
the target room, otherwise the lowest free one. `allocateNodeLocked()` returns `NODE_ALL-1`
on a full room, which would collide silently, so the result is verified rather than trusted.

`MessageProtocol::sendLocation()` still exists and is now the footgun � it moves the device
and leaves the hub stale. Controller code must not call it.

## Bench test � passed

Temporary timed sequence in the hub's main loop, removed after passing. Four stages:

| Stage | Action | Result |
|---|---|---|
| 1 | `relocateDevice(uid, room 2)` | ? Moving ? ACK ? confirmed, ~30 ms end to end |
| 2 | `IDENTIFY` ? room 2 | ? received, strip flashed |
| 3 | `IDENTIFY` ? room 1 (old) | ? **not received**, strip silent |
| 4 | `IDENTIFY` ? broadcast | ? received, strip flashed |

Stage 4 is the control. Without it, a strip that had crashed during stage 2 would make
stage 3's silence look like a pass.

The proof is in what the strip log does *not* contain: the hub logged
`TX CMD IDENTIFY seq=4651 FEBDCDC3?r1/*` and the strip has no matching RX line, while
4649 (r2) and 4652 (ALL) are both present. The drop happened in the RX room filter,
not on the radio.

Hub NVS confirmed across a reboot: banner came back `[0] EE907A91 Light r2/n2`.

**Still unverified:** the strip's own NVS after a power cycle. The serial link dropped and
reconnected mid-session, which looks like a reboot in the log but is not one � the strip's
uptime counter ran straight through it. Worth doing before the strip goes on a ceiling.

---

## Key takeaways

1. **A shared fact needs a single owner.** House/room/node lives in two places � the device
   and the hub's record. Any code path that writes one without the other is a desync waiting
   to happen. `sendLocation()` sat in the layer that couldn't see the record; that was the
   whole bug.
2. **Order of operations decides which side goes stale.** Send-then-commit and
   commit-then-send are both "correct" until a packet is lost. Only one of them fails safe.
3. **A test that can only pass is not a test.** Stages 2 and 4 exist to prove the silence in
   stage 3 meant filtering rather than a dead node.
4. **Flash delta is a better staleness signal than build duration.** A 7.9 s build looked
   suspicious but added 2.6 KB � real. Duration only tells you how *much* recompiled.
5. **`Select-String` is case-insensitive by default.** A count of `_reloc` returned 33
   instead of 27 because `AUTOPAIR_MAX_RELOCATES` matched too. Same family as the
   `-like '??*'` trap: the tool's default was not the assumed one.
6. **Line endings: this repo is LF.** `\
\
` in a PowerShell `.Replace()` silently
   injects CRLF into an LF file. Two lines were mixed before being caught. Here-strings pasted
   through the console arrive as LF, which is why only the explicit escapes were affected.
   Check before committing, not after.
7. **`esp_app_desc` build timestamps go stale on incremental builds.** The hub banner read
   `built Jul 25` on an image built today, because the file carrying `__DATE__` didn't
   recompile. Harmless today; **not** harmless in Phase 2, where identifying which image is in
   which slot is the entire job. OTA will need a version source that isn't this field.

---

## Files changed

- `system/auto_pair/auto_pair.h` � `AUTOPAIR_MAX_RELOCATES`, `relocateDevice()`,
  `noteDeliveryResult()`, `PendingRelocate`, `_reloc[]`, `nodeFreeLocked()`
- `system/auto_pair/auto_pair.cpp` � the three new functions; ctor clears `_reloc`;
  `forgetDevice()` / `forgetAll()` drop in-flight moves
- `testing/devices/smart-light-test/hub/main/main.cpp` � one line wiring
  `noteDeliveryResult()` into the delivery callback

Untouched: `message_protocol.*`, strip `main.cpp`, all `CMakeLists.txt`.

---

## Next steps

- [ ] Power-cycle the strip and confirm its NVS still reads room 2
- [ ] **Phase 2 � make the strip un-brickable:** OTA transport-neutral sink API
      (`beginWrite`/`writeChunk`/`finishWrite`/`abortWrite`), ESP-NOW bulk plane,
      chunked transfer, CRC32 verify, boot flip, deliberate-corrupt-image rollback test
- [ ] OTA needs a firmware version source that survives incremental builds (see takeaway 7)
- [ ] `processPairMessage()` returns void, so an unauthorised `SET_LOCATION` is dropped by
      `onSetLocation()` and still ACKs OK. Benign today � the hub is always the authorised
      controller � but "ACKed" currently means received, not applied. Needs a return value,
      which touches both `main.cpp` files.
- [ ] Strip state persistence in NVS + `power_on_behavior` byte (debounce writes ~10 s)
- [ ] Change `storage` partition subtype `spiffs` ? `undefined` so nothing can format it
- [ ] Pairing recovery: re-open for pairing if the hub is unheard for ~10 min, staying lit
- [ ] GC9A01: hoist `spi_bus_initialize` into a shared display-bus module
- [ ] TouchSensor reports `initial state: TOUCHED` at boot on both pads � phantom touch
- [ ] Encoder: guard `gpio_install_isr_service` already-installed




##############################################################################################################################################################################################################################################################################################################################################################################################################################































# Dev Log - 05/08/2026 (Wed)

Phase 2 continued. The OTA core/HTTP split was already on disk from the previous session but
uncommitted, and the `ota-test` caller was half-ported. This session finished the port,
committed the split, and then spent most of its time on hardware verification - which is where
the useful findings came from.

Outcome: the sink API, boot flip, validation timer, manual rollback, and automatic rollback are
all verified on real silicon, on both chips, on the partition layout the strip will actually
ship with.

---

## Where things stood

`git status` at session start showed the split had landed but never been committed:
`ota_manager.{h,cpp}` modified, `system/ota_http/` untracked. A grep across the OTA files
confirmed `ota-test/main/main.cpp` was the only remaining caller of the old member API.

Lesson worth repeating: the dev log and the commit history both lagged behind disk, because
edits happen via PowerShell and get committed later. Grepping the files was the only
authoritative answer. Neither doc nor git could have told us.

---

## The caller port

`main.cpp` had nine member calls across three build modes:
`registerWebUI` / `registerUploadHandler` / `registerStatusHandler` (x3 modes),
plus `setUpdateURL` and `checkForUpdate`. All became `ota_http_*` free functions.
`ota_http` added to `EXTRA_COMPONENT_DIRS` and to `main`'s `REQUIRES`.

The `ota_http` component's own `CMakeLists.txt` already listed `esp_http_server` and
`esp_http_client` - checked rather than assumed, which saved a round trip.

Both `esp32d` and `s3_wroom` built clean. Worth noting the build succeeding is stronger
evidence than watching `ota_http.cpp` compile: if the component had not been found, the
`ota_http_*` calls would have failed at link with undefined references.

---

## PROJECT_VER does not work as a build flag

Previous entry, takeaway 7, called for a version source that survives incremental builds.
This session found the answer and also found that the existing mechanism was never working.

`platformio.ini` carried `-DPROJECT_VER=\"1.2.0\"` in `build_flags`, and the file's own
comments documented that as the way to set the version. It is not. `-D` puts the value on the
compiler command line; `esp_app_desc` is populated by CMake, which reads `PROJECT_VER` set
before `project()`, else `version.txt`, else `git describe`. The board was reporting
`cc0876f-dirty` - the git-describe fallthrough - and had been all along.

Fix: `set(PROJECT_VER "1.3.0")` immediately before `project(ota-test)` in `CMakeLists.txt`,
and remove the `-D` flag. Leaving both in place produced
`warning: "PROJECT_VER" redefined` with no way to tell from the log which one won.

Verification that does not rely on inference: `esp_app_desc` sits at a fixed offset after the
image header, with the version string at byte 0x30 of the `.bin`. Reading it straight out of
the artifact settles the question. Flash size cannot - the field is a fixed 32 bytes, so
`1.2.0`, `1.3.0` and `cc0876f-dirty` all produce identical image sizes.

The git-describe string is arguably the better source for the real hub and strip, since it ties
an image to a commit. Deferred; not a Phase 2 decision.

---

## Sink API verified on the S3

Sequence on COM4 (S3, 16MB, MAC `b8:f8:62:e0:a0:74`), 828096-byte images:

    factory (cc0876f-dirty)  ->  ota_0 (1.3.0)  ->  ota_1 (1.4.0)

Every stage of the sink appeared in the log in order: `Write opened on ota_0 @ 0x110000`,
progress to 100%, `Staged image: ota-test v1.3.0` (finishWrite reading `esp_app_desc` back out
of the staged slot), `Image accepted: ... Reboot when ready.`

That last line matters: the sink does **not** reboot. `OTAHttp` did, two seconds later. That is
exactly the separation the split was for, working on hardware. An ESP-NOW transfer will be able
to ACK its final chunk before rebooting.

`Pending: YES` on the new boot, then `Firmware validated! Rollback cancelled.` at 836ms.

---

## The factory-partition red herring

Manual rollback from ota_1 on the S3 landed on **factory**, not ota_0. The bootloader said
`Defaulting to factory image` outright.

The initial read was that otadata had no valid OTA slot to fall back to, and that this was a
real hole - because neither `partitions_4mb.csv` nor `partitions_16mb.csv` has a factory
partition. Both are two-slot layouts. If rollback needed factory, the real devices had no net.

That read was wrong, and the reason it was wrong is the important part:
**ota-test was not building against the project's partition tables.**
`board_build.partitions = partitions_two_ota.csv` resolved to PlatformIO's built-in table,
which *does* have a factory partition. The test was run on a layout that does not match the
target, so its result said nothing about the target.

Fix: per-env tables pointing at `system/partitions/`, `partitions_16mb.csv` under
`[env:s3_wroom]` and `partitions_4mb.csv` under `[env:c6_seeed]`. The other envs now have no
table and will fail loudly rather than silently falling back to a factory-containing default.

---

## Rollback verified on the strip's real layout

Rerun on COM7 (C6, 4MB, base MAC `b4:3a:45:8a:81:74`) with `partitions_4mb.csv`. Slots are
labelled `app0` / `app1` (the CSV name; the subtype is what the bootloader reads), 1856KB each,
no factory. 923456-byte images.

Manual rollback: `app1 (1.5.0)` -> `app0 (1.4.0)`. Correct. The S3 result was an artifact.

Automatic rollback, the one that actually matters for a ceiling-mounted node - built with
`-DOTA_TEST_ROLLBACK`, version `9.9.9` so it could not be confused with anything, uploaded, then
left alone:

    W (10958) Self-tests not calling validate() - rollback will happen in ~20s
    I (25958) Running... pending=YES
    E (30618) OTAManager: Validation timeout expired! Auto-rolling back...
    I (30748) esp_ota_ops: Rollback to previously worked partition.
    ->  Loaded app from partition at offset 0x10000, App version: 1.4.0

Timer fired 618ms past a 30s deadline. Nobody touched anything. That is the un-brickable
guarantee demonstrated rather than assumed.

---

## Cross-architecture rejection (unplanned)

Both boards hardcode the same softAP SSID, so with both powered it is ambiguous which one the
browser is talking to. A C6 image got uploaded to the S3 by accident. The sink rejected it:
`Upload failed: Image validation failed`, and the S3 kept running its existing image. The
boot flip never happened.

`esp_ota_end()`'s chip-ID check catches architecture mismatch. Not a substitute for the planned
`DeviceRole` check in the offer handshake - that one runs before any bytes transfer, this one
after a full 828KB upload - but useful to know the floor exists.

---

## Credentials leak

`platformio.ini` carried a live WiFi SSID and password in `build_flags`, uncommented, tracked,
and already pushed to a public repo in two commits (`4e2daf2`, `59e8129`).

History rewriting was not the fix. GitHub keeps orphaned objects reachable by SHA after a
force-push, forks and clones keep their own copies, and anything that scraped the repo already
has it. The credential was rotated instead, which makes the leaked string worthless. That is
the only action that fully closes it.

Ongoing pattern: `secrets.ini`, gitignored, pulled in via `extra_configs` in `platformio.ini`.
Placeholders stay in the tracked file. Note `build_flags` is replaced wholesale rather than
merged, so `secrets.ini` must repeat every flag it wants, including the mode selector.

---

## Key takeaways

1. **A test on the wrong configuration is worse than no test.** The S3 rollback result looked
   like a design flaw for twenty minutes. It was a partition table that did not match the
   target. Verify the test rig matches the thing being tested before believing the result.
2. **`-DPROJECT_VER` never worked, and the file documented it as if it did.** A wrong comment
   is more expensive than a missing one. Both were corrected.
3. **Read the artifact, not the build log.** Version strings live at a fixed offset in the
   `.bin`. Inferring from flash size delta cannot work for fixed-width fields.
4. **`git status` answers "did that edit land" better than memory or the dev log.** Both lag
   disk in this workflow. Grep is authoritative.
5. **Rotate leaked credentials; do not try to erase them.** Public repo means assume scraped.
   Rewriting history is theatre once it is pushed.
6. **Line endings, again.** This repo is LF. A `.Replace()` with a hardcoded CRLF is a silent
   no-op - it happened once this session, on the `extra_configs` insert, and cost a round trip.
   Detect the newline from the file content rather than assuming.
7. **Two boards, one SSID is a foot-gun.** Uploading C6 firmware to an S3 was only caught
   because the sink rejected it. Bench setups need distinguishable identifiers.
8. **C6 RSSI at bench distance is -87 to -93 dBm**, with the link falling back to 1Mbps and the
   PC repeatedly dropping and rejoining. WiFi association churn is irrelevant to ESP-NOW, which
   needs no association - but weak RF is weak RF, and this is worth remembering when the bulk
   plane starts losing chunks. The XIAO C6 has a U.FL connector; whether the onboard antenna is
   selected has not been checked.

---

## Files changed

- `system/ota/ota_manager.{h,cpp}`, `system/ota/CMakeLists.txt` - transport-neutral sink, no
  HTTP dependency
- `system/ota_http/{ota_http.h,ota_http.cpp,CMakeLists.txt}` - new component, HTTP as an
  ordinary consumer of the sink
- `testing/wireless/ota-test/main/main.cpp` - member calls to `ota_http_*` free functions
- `testing/wireless/ota-test/main/CMakeLists.txt` - `REQUIRES ota ota_http`
- `testing/wireless/ota-test/CMakeLists.txt` - `ota_http` in `EXTRA_COMPONENT_DIRS`,
  `set(PROJECT_VER)` before `project()`
- `testing/wireless/ota-test/platformio.ini` - per-env partition tables, credential
  placeholders, `extra_configs`, corrected comments
- `testing/devices/smart-light-test/{hub,strip-node}/sdkconfig.defaults` -
  `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`
- `.gitignore` - `secrets.ini`

Commits: `0f59be9`, `cac00eb`, `0065db3`, `85331b6`, `8b451de`.

---

## Next steps

- [ ] **ESP-NOW bulk plane** - wire format first: chunk header layout, bitmap encoding,
      gap-list format, where CRC32 lives. 230-byte chunks over raw `esp_now_send`, no per-chunk
      ACK, resend driven from the control plane. ~4000 chunks for a 923KB image.
- [ ] Role check in the OTA offer handshake - `DeviceRole` byte compared before any bytes
      transfer, identity from UID + registry lookup
- [ ] Hub-side policy: a node that rolls back and is then offered the same image will loop.
      Consider tracking "this UID rejected this version" - belongs in the bulk-plane design
- [ ] Strip state persistence in NVS + `power_on_behavior` byte (debounce writes ~10 s)
- [ ] Pairing recovery: re-open for pairing if the hub is unheard for ~10 min, staying lit
- [ ] Power-cycle the strip and confirm its NVS still reads room 2 (carried forward, still open)
- [ ] `processPairMessage()` returns void, so an unauthorised `SET_LOCATION` is dropped and
      still ACKs OK. "ACKed" means received, not applied. Needs a return value.
- [ ] Reset `PROJECT_VER` in `ota-test/CMakeLists.txt` - currently left at `9.9.9`
- [ ] Reflash `smart_light_hub` to COM4 and `smart_light_strip` to COM7; both boards currently
      carry ota-test
- [ ] Check whether the XIAO C6's RF switch selects the onboard antenna (see takeaway 8)
- [ ] Two PlatformIO cores installed (6.1.18 and 6.1.19) - a source of build inconsistency
- [ ] GC9A01: hoist `spi_bus_initialize` into a shared display-bus module
- [ ] TouchSensor reports `initial state: TOUCHED` at boot on both pads - phantom touch
- [ ] Encoder: guard `gpio_install_isr_service` already-installed






































##############################################################################################################################################################################################################################################################################################################################################################################################################################








# Dev Log — 06/08/2026 (Thu)

**Project:** Smart Home Ecosystem — firmware repository
**Scope:** Phase 2 — ESP-NOW bulk plane: freeze the wire format, extend the OTA sink, build the receiver
**Outcome:** Receiver compiles, links, and boots on hardware. Sender does not exist yet, so zero transfer testing. Both boards finally off `ota-test` firmware.

---

## Commits

| Hash | Subject |
|------|---------|
| `1724d57` | freeze ESP-NOW bulk plane wire format |
| *(fill)* | add offset-write and CRC32 verify to OTAManager sink |
| *(fill)* | correct s3_wroom flash size to 16MB |
| `a067083` | add ESP-NOW bulk plane receiver |
| `40f110b` | wire OTA rollback and bulk receiver into strip-node |

---

## The correction that reshaped the design

Last session's design put an **in-order write constraint** at the centre: `esp_ota_write` is
sequential, the C6 can't buffer a 923 KB image in 354 KB of heap, therefore chunks must be
written in arrival order, therefore a windowed transfer with a per-window gap bitmap.

That was wrong. **`esp_ota_write_with_offset()` exists** and writes non-contiguously. It is
documented for exactly this case — OTA packets arriving out of order.

It works only because `esp_ota_begin()` is given a real image size, which erases the whole
needed range up front. Reading the IDF source (`components/app_update/esp_ota_ops.c`)
confirmed the enforcement: `esp_ota_write_with_offset` opens with
`assert(it->need_erase == 0)`. `need_erase` is set by `(image_size == OTA_WITH_SEQUENTIAL_WRITES)`,
i.e. `0xfffffffe`. `beginWrite()` never passes that, so we are safe — and if anyone ever
changes it, the failure is a loud panic, not silent corruption.

**Everything downstream collapsed:**

| Was | Now |
|-----|-----|
| Windowed transfer, 64 chunks | No window at all |
| Per-window bitmap | Whole-image bitmap, 481 B for a real image |
| 15 KB RAM buffer option | Zero buffering — chunk *k* writes to offset *k × 240* |
| 230-byte chunks | 240-byte chunks (16-aligned for future flash encryption) |

Also checked and cleared: `esp_ota_write_with_offset` is a bare `esp_partition_write`, nothing
buffered or held back, so reading the slot back **before** `esp_ota_end()` returns exactly what
was written. IDF's own `ota_calc_partition_bin_sha()` does the same thing on an open handle.
The CRC-before-finish plan is sound.

---

## Wire format (frozen — `system/ota_bulk/ota_bulk.h`, 235 lines, no implementation)

**Chunk frame** — raw `esp_now_send`, 8 + 240 = 248 B (limit 250):

```
off  size  field
  0     2  magic         0x4F54, the demux tag
  2     2  chunk_index   flash offset is index * chunk_size
  4     4  session_id    esp_random() per transfer
  8   240  payload
```

Header is 8 bytes rather than 6 so the payload pointer stays 8-byte aligned in the RX buffer.
No `payload_len` field — derivable from index and image size, and ESP-NOW hands you the frame
length anyway.

**Control plane** — CmdId `0x80`–`0x84` on the ordinary 48-byte reliable path, inheriting
retry/backoff, dedup and ACK for free. Six messages per transfer, not four thousand.

- `0x80 OTA_OFFER` — 18/24 B. **The ACK is the accept/reject**; there is no separate accept
  message. `NOT_SUPPORTED` / `BUSY` / `FAIL` / `OK`.
- `0x81 OTA_PASS_END` — "that's everything this pass"
- `0x82 OTA_GAP_REPORT` — 23/24 B, up to 4 runs of `(start, count)`
- `0x83 OTA_COMPLETE`
- `0x84 OTA_ABORT`

`0x85`–`0x8F` remain free.

**Gap runs, not a paginated bitmap:** a 923 KB image is 3,845 chunks — a 481-byte bitmap needs
21 packets to ship back. The common case is three missing chunks, which runs describe in one.

Six `static_assert`s hold every payload under the 24-byte `MessagePacket` limit, so a future
field that overflows is a build error rather than silent truncation on the wire.

---

## OTAManager sink extension

- **`writeChunkAt(data, len, offset)`** — wraps `esp_ota_write_with_offset`. Duplicate offsets
  are the caller's problem: `_bytes_written` counts bytes accepted, so writing a chunk twice
  inflates it and breaks the completeness check in `finishWrite()`. Gate on a bitmap.
- **`verifyCrc32(expected, len)`** — reads the slot back in 256 B blocks (mirroring IDF's own
  loop) and compares. Catches a chunk written at the wrong offset with structurally valid
  content, which `esp_ota_end()`'s image check cannot see.
- **`WriteMode` guard** — `NONE`/`SEQUENTIAL`/`OFFSET`, set on first write, rejects mixing.
  Offset writes bump `wrote_size`, so a later `writeChunk()` would land at a wrong offset. That
  direction *is* silent corruption; the other direction panics on the assert.

No `PROGRESS` event from `writeChunkAt` — out-of-order arrival makes `_bytes_written` a count
rather than a position, and the bulk plane reports progress from its own bitmap.

---

## Receiver — `system/ota_bulk/ota_bulk_rx.{h,cpp}`

**Chunks go through a queue and a dedicated task, not straight to flash from the RX callback.**
A flash program op disables the cache, which stalls anything executing from flash including the
WiFi task itself. On a single-core C6 that means dropping the very chunks you're about to ask
for again — an intermittent failure that would look like an RF problem. Queue is 16 × 244 B
≈ 3.9 KB, allocated from heap at runtime.

**The bitmap is owned by the task.** `tryConsume()` deliberately does *not* check it for
duplicates — that would mean reading it from a second context for what is only an optimisation.
The task re-checks before every write, so a duplicate costs one queue slot and nothing else.

**Gap reports are emitted by the task after the queue drains,** not synchronously in the
`PASS_END` handler. Reporting from the handler would list chunks sitting in the queue unwritten,
and the hub would resend them pointlessly.

**`_session` is written last on start and first on stop,** so the RX callback never sees a
half-built session.

**Authorisation happens before any bytes move:** `src_uid == AutoPair::getControllerUid()` and
`target_role == self_role`, both on the `OTA_OFFER` ACK path.

**Gap-run walker fuzz-tested on the host** — 3,000 random bitmaps against a brute-force
reference, zero mismatches. Realistic case (3 holes in 3,845 chunks) produces 3 runs in a single
packet.

**Known cost:** `processControl` runs in the RX callback and takes `_mtx`, which the writer task
holds across a flash write. A control packet arriving mid-write blocks the ESP-NOW RX task for
up to ~1 ms. Bounded and acceptable, but it's the first thing to look at if pairing traffic gets
flaky during a transfer.

---

## strip-node wiring

- `OTAManager::begin(OTA_DEFAULT_TIMEOUT_S)` — **this is what arms the rollback timer.** The
  strip wasn't calling it. Without it a bad image never rolls back and the strip bricks, which
  is the entire point of Phase 2.
- `validate()` gated on **the first command reaching `onCommand()`**. That packet came off the
  radio and passed house + room/node filtering, so the receive path is proven end to end.
  Booting alone is not proof — per "WHAT validate() SHOULD MEAN" in `ota_manager.h`, perfect
  LEDs and a broken receive path is a ladder.
- `tryConsume()` first in the receive callback, so raw 248-byte frames never reach the 48-byte
  parser.

---

## Hardware verification

Both boards reflashed off `ota-test` firmware — an item that had been carried for three sessions.

**Strip (COM7, XIAO C6):**
```
smart_light_strip v0.1.0 sha cb16dba2
Running: app0 @ 0x00010000 (1856KB)   Next slot: app1 @ 0x001E0000
Pending: no    Rollback: available
OtaBulkRx: Bulk RX ready (role=Light, queue=16 x 244 B, chunk=240 B)
AutoPair: Already paired to controller FEBDCDC3 (house 0x6F19 room 2 node 2)
```

**Hub (COM4, S3 WROOM):**
```
smart_light_hub v0.1.0 sha 15273af8
Slot: app0 @ 0x010000 (4096 KB)
[0] EE907A91  Light  r2/n2  "Strip 1"
Staged image: none (storage empty)
```

**Both NVS sides survived a full app-partition rewrite.** This closes the Phase 1 item recorded
as "strip NVS after true power-cycle unverified" — and it's a stronger test than the original,
since the app partition was completely overwritten underneath it.

Live traffic confirms `onCommand()` runs in normal operation (hub keepalive every 30.5 s,
strip ACKs each one), so the `validate()` gate has a real trigger path rather than a
theoretical one.

Build matrix: `esp32d`, `s3_seeed`, `c6_seeed`, `s3_wroom` all green.

---

## Pre-existing defects found (none related to OTA)

1. **`ota-test` `s3_wroom` had never built.** Pointed at `partitions_16mb.csv` while the board
   default configured 8 MB, so it died at partition generation before compiling a single
   source. **Fixed** — added `board_upload.flash_size` / `maximum_size`. Note this means the S3
   rollback verification from last session came from somewhere other than this env.
2. **`strip-node` `s3_wroom` has the identical bug.** Not fixed.
3. **`esp_now` path mismatch in strip-node** — `EXTRA_COMPONENT_DIRS` says `wireless/esp_now`,
   `build_flags` says `wireless/communication/esp_now`. One is wrong; since it builds, the `-I`
   flags are apparently not load-bearing. Not fixed.
4. **GCC ICE** (`internal compiler error: Segmentation fault`) in `esp_lcd_panel_rgb.c` on
   `s3_seeed` — stale build dir, cleared by `pio run -t clean`.

---

## Learnings

1. **`esp_ota_write_with_offset()` removes the in-order constraint entirely.** Verified from IDF
   source, not just docs — the `assert(need_erase == 0)` is the thing that makes it safe.
2. **`esp_rom_crc32_le` applies `~` at both ends.** Seed **0** gives standard zlib CRC-32
   (matches Python `zlib.crc32()`). Seeding `0xFFFFFFFF` gives a value that matches nothing —
   and the failure would appear only at the last step of every transfer.
3. **PS 5.1 `Get-Content -Raw` reads as ANSI, not UTF-8.** On files full of `─` and `═` that
   silently mangles every box character on read *and* on write-back. `-Encoding UTF8` on both
   ends; `[System.IO.File]::WriteAllText` with `UTF8Encoding($false)` to avoid adding a BOM.
4. **`\"` is a C escape, not a PowerShell one.** Any line containing a C string literal goes in
   a single-quoted PS string.
5. **Anchor `.Replace()` on pure-ASCII substrings.** The comment line contained an en-dash
   (`0x80–0x8F`); matching non-ASCII through the console is how you get a silent no-op.
6. **Verify with `git diff`, not a proxy metric.** A U+2500 character count was used as a
   corruption canary and produced a false alarm, because the canary was a guess about house
   style rather than a fact. Predicted counts were wrong three times this session; `git diff`
   was right every time.
7. **`pio run` with no `-e` builds `default_envs` only**, not every env. A "successful build"
   can mean neither real target was compiled.
8. **Flash-size delta proves the linker included new code.** After adding the component but
   before wiring it, the image was 902,381 B — `--gc-sections` had stripped `OtaBulkRx` because
   nothing referenced it. After wiring: 926,781 B. Compiling and linking are different claims.
9. **A serial reflash of the app partition doesn't touch NVS.** Both devices came back knowing
   each other.

---

## Next Steps

- [ ] **Design the hub's image source** — nothing puts a `.bin` into the hub's `storage`
      partition yet (`Staged image: none`). `logStagedImage()` already reads from there, so
      there's existing intent — read `system/ota_http` before designing. **This blocks the
      sender, which blocks all receiver testing.**
- [ ] `ota_bulk_tx` — hub-side sender: pacing on the ESP-NOW send callback, pass/gap loop
- [ ] End-to-end transfer test on hardware, including a deliberately corrupted image
- [ ] NVS state persistence (`last_state` + `power_on_behavior`, ~10 s debounce)
- [ ] Pairing recovery (strip re-opens for pairing after ~10 min with no hub contact, stays lit)
- [ ] `strip-node` `s3_wroom` flash-size fix (same as the `ota-test` one)
- [ ] `esp_now` path mismatch in `strip-node`
- [ ] `.gitattributes` for the CRLF warnings (still skipped, still harmless)



















##############################################################################################################################################################################################################################################################################################################################################################################################################################
##############################################################################################################################################################################################################################################################################################################################################################################################################################
##############################################################################################################################################################################################################################################################################################################################################################################################################################
##################################################################################################################################################################################################################
