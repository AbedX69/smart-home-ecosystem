#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>

// All component headers
#include "button.h"
#include "buzzer.h"
#include "relay.h"
#include "touch.h"
#include "vibration.h"
#include "pwm_dimmer.h"
#include "mosfet_driver.h"
#include "encoder.h"
#include "max98357.h"
#include "pca9548a.h"

// Display drivers (choose one or comment out as needed)
#include "ssd1306.h"       // I2C OLED
// #include "st7789.h"      // SPI TFT
// #include "ili9341.h"     // SPI TFT with touch
// #include "gc9a01.h"      // SPI round TFT
// #include "epaper.h"      // SPI e‑paper
// #include "ssd1357.h"     // SPI RGB OLED

static const char *TAG = "MAIN";

// ----------------------------------------------------------------------------
// Pin definitions (must match build flags or provide defaults)
// ----------------------------------------------------------------------------
#ifndef BUTTON_PIN
#define BUTTON_PIN GPIO_NUM_4
#endif
#ifndef BUZZER_PIN
#define BUZZER_PIN GPIO_NUM_5
#endif
#ifndef RELAY_PIN
#define RELAY_PIN GPIO_NUM_6
#endif
#ifndef RELAY_ACTIVE_LOW
#define RELAY_ACTIVE_LOW true
#endif
#ifndef TOUCH_PIN
#define TOUCH_PIN GPIO_NUM_7
#endif
#ifndef VIBRATION_PIN
#define VIBRATION_PIN GPIO_NUM_8
#endif
#ifndef PWM_DIMMER_PIN
#define PWM_DIMMER_PIN GPIO_NUM_9
#endif
#ifndef MOSFET_PIN
#define MOSFET_PIN GPIO_NUM_10
#endif
#ifndef ENCODER_CLK
#define ENCODER_CLK GPIO_NUM_11
#endif
#ifndef ENCODER_DT
#define ENCODER_DT GPIO_NUM_12
#endif
#ifndef ENCODER_SW
#define ENCODER_SW GPIO_NUM_13
#endif
#ifndef I2C_SDA
#define I2C_SDA GPIO_NUM_21
#endif
#ifndef I2C_SCL
#define I2C_SCL GPIO_NUM_22
#endif
#ifndef MAX98357_DIN
#define MAX98357_DIN GPIO_NUM_25
#endif
#ifndef MAX98357_BCLK
#define MAX98357_BCLK GPIO_NUM_26
#endif
#ifndef MAX98357_LRC
#define MAX98357_LRC GPIO_NUM_27
#endif
#ifndef SSD1306_ADDR
#define SSD1306_ADDR 0x3C
#endif


// Cast the integer macros to gpio_num_t when constructing
Button        button(static_cast<gpio_num_t>(BUTTON_PIN));
Buzzer        buzzer(static_cast<gpio_num_t>(BUZZER_PIN));
Relay         relay(static_cast<gpio_num_t>(RELAY_PIN), true);   // active low
TouchSensor   touch(static_cast<gpio_num_t>(TOUCH_PIN));
Vibration     vibration(static_cast<gpio_num_t>(VIBRATION_PIN));
PWMDimmer     pwmDimmer(static_cast<gpio_num_t>(PWM_DIMMER_PIN));
MosfetDriver  mosfet(static_cast<gpio_num_t>(MOSFET_PIN));
RotaryEncoder encoder(static_cast<gpio_num_t>(ENCODER_CLK),
                      static_cast<gpio_num_t>(ENCODER_DT),
                      static_cast<gpio_num_t>(ENCODER_SW));
SSD1306       oled(static_cast<gpio_num_t>(I2C_SDA),
                   static_cast<gpio_num_t>(I2C_SCL),
                   SSD1306_ADDR);   // I2C display

PCA9548A       i2cMux(static_cast<gpio_num_t>(I2C_SDA), static_cast<gpio_num_t>(I2C_SCL));
MAX98357       audio(static_cast<gpio_num_t>(MAX98357_DIN), static_cast<gpio_num_t>(MAX98357_BCLK), static_cast<gpio_num_t>(MAX98357_LRC));

// ----------------------------------------------------------------------------
// Main application
// ----------------------------------------------------------------------------
extern "C" void app_main() {
    ESP_LOGI(TAG, "Starting combined component test");

    // ------------------------------------------------------------------------
    // Initialize all components
    // ------------------------------------------------------------------------
    button.init();
    buzzer.init();
    relay.init();
    touch.init();
    vibration.init();
    pwmDimmer.init();
    mosfet.init();
    encoder.init();
    audio.init();

    // I2C multiplexer and display
    i2cMux.init();
    oled.init();

    // ------------------------------------------------------------------------
    // Set initial states
    // ------------------------------------------------------------------------
    relay.off();
    pwmDimmer.setBrightness(0);
    mosfet.off();
    audio.setEnabled(true);

    oled.clear();
    oled.drawString(0, 0, "Combined Test");
    oled.update();

    ESP_LOGI(TAG, "All components initialized, starting main loop");

    // ------------------------------------------------------------------------
    // Main loop
    // ------------------------------------------------------------------------
    int counter = 0;
    while (1) {
        // Update polled components
        button.update();
        touch.update();

        // Check encoder position (ISR updates automatically)
        int32_t pos = encoder.getPosition();
        ESP_LOGD(TAG, "Encoder pos: %ld", pos);

        // Check button press
        if (button.wasPressed()) {
            ESP_LOGI(TAG, "Button pressed");
            buzzer.beep();
            relay.toggle();
        }

        // Check touch
        if (touch.wasTouched()) {
            ESP_LOGI(TAG, "Touch detected");
            vibration.tap();
            pwmDimmer.fadeTo(100, 500);
        }

        // Every 5 seconds, do something on the audio and display
        if (counter % 50 == 0) {
            ESP_LOGI(TAG, "Playing audio beep and updating display");
            audio.beep(100);

            char buf[32];
            snprintf(buf, sizeof(buf), "Count: %d", counter);
            oled.clear();
            oled.drawString(0, 0, buf);
            oled.drawString(0, 20, "Components OK");
            oled.update();
        }

        vTaskDelay(pdMS_TO_TICKS(100));
        counter++;
    }
}