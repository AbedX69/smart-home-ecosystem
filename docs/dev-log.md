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