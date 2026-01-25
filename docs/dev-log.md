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




