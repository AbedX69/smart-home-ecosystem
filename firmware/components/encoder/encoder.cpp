#include "encoder.h"
#include <esp_log.h>  // Angle brackets for ESP-IDF headers

static const char* TAG = "RotaryEncoder";

// Static instance pointer for ISR callback
RotaryEncoder* RotaryEncoder::instance = nullptr;

RotaryEncoder::RotaryEncoder(gpio_num_t clk, gpio_num_t dt, gpio_num_t sw, bool halfStep) 
    : pinCLK(clk), 
      pinDT(dt), 
      pinSW(sw), 
      position(0), 
      lastEncoded(0),
      lastButtonState(false),
      lastButtonChangeTime(0),
      lastRotationTime(0),
      halfStepMode(halfStep) 
{
    instance = this;  // Store instance for ISR access
}

RotaryEncoder::~RotaryEncoder() {
    // Clean up interrupts
    gpio_isr_handler_remove(pinCLK);
    gpio_isr_handler_remove(pinDT);
    instance = nullptr;
}

void RotaryEncoder::init() {
    ESP_LOGI(TAG, "Initializing rotary encoder on CLK=%d, DT=%d, SW=%d", pinCLK, pinDT, pinSW);
    
    // Configure rotation pins with pull-ups
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = (1ULL << pinCLK) | (1ULL << pinDT);
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_ANYEDGE;  // Trigger on both edges
    gpio_config(&io_conf);
    
    // Configure button pin with pull-up
    io_conf.pin_bit_mask = (1ULL << pinSW);
    io_conf.intr_type = GPIO_INTR_DISABLE;  // Button uses polling (less critical)
    gpio_config(&io_conf);
    
    // Read initial state
    uint8_t clk = gpio_get_level(pinCLK);
    uint8_t dt = gpio_get_level(pinDT);
    lastEncoded = (clk << 1) | dt;
    lastButtonState = (gpio_get_level(pinSW) == 0);
    
    ESP_LOGI(TAG, "Initial encoder state: CLK=%d DT=%d (0b%02b)", clk, dt, lastEncoded);
    
    // Install ISR service if not already installed
    // This call is safe to repeat - it returns ESP_ERR_INVALID_STATE if already installed
    esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to install ISR service: %s", esp_err_to_name(err));
        return;
    }
    
    // Attach interrupt handlers to both pins
    gpio_isr_handler_add(pinCLK, isrHandler, (void*)this);
    gpio_isr_handler_add(pinDT, isrHandler, (void*)this);
    
    ESP_LOGI(TAG, "Encoder initialized successfully");
}

// ISR Handler - MUST be in IRAM for fast execution
void IRAM_ATTR RotaryEncoder::isrHandler(void* arg) {
    RotaryEncoder* encoder = static_cast<RotaryEncoder*>(arg);
    
    uint8_t clk = gpio_get_level(encoder->pinCLK);
    uint8_t dt = gpio_get_level(encoder->pinDT);
    uint8_t encoded = (clk << 1) | dt;
    
    uint64_t now = esp_timer_get_time();
    if (now - encoder->lastRotationTime < 1000) {
        return;
    }
    encoder->lastRotationTime = now;
    
    uint8_t sum = (encoder->lastEncoded << 2) | encoded;
    // TEMPORARY DEBUG - Remove after testing
    static const char* TAG = "ISR";
    ESP_EARLY_LOGI(TAG, "Transition: old=%d%d new=%d%d sum=0x%02X", 
        (encoder->lastEncoded >> 1) & 1, 
        encoder->lastEncoded & 1,
        clk, 
        dt,
        sum);
    
    if (encoder->halfStepMode) {
        // Count only on the final transition of each detent
        if (sum == 0x0B) {  // Clockwise final transition
            encoder->position = encoder->position + 1;
        }
        else if (sum == 0x0E) {  // Counter-clockwise final transition (guess - verify)
            encoder->position = encoder->position - 1;
        }
    } else {
        // Original half-step counting for full-step encoders (32d, s3, etc)
        if (sum == 0b1101 || sum == 0b0100 || sum == 0b0010 || sum == 0b1011) {
            encoder->position = encoder->position + 1;
        }
        else if (sum == 0b1110 || sum == 0b0111 || sum == 0b0001 || sum == 0b1000) {
            encoder->position = encoder->position - 1;
        }
    }
    
    encoder->lastEncoded = encoded;
}

int32_t RotaryEncoder::getPosition() const {
    return position;
}

void RotaryEncoder::resetPosition() {
    position = 0;
}

void RotaryEncoder::setPosition(int32_t pos) {
    position = pos;
}

bool RotaryEncoder::isButtonPressed() const {
    // Button is active LOW (pressed = 0)
    return gpio_get_level(pinSW) == 0;
}

bool RotaryEncoder::wasButtonPressed() {
    bool currentState = isButtonPressed();
    uint64_t now = esp_timer_get_time();
    
    // Debounce: Only register change if enough time has passed
    if (now - lastButtonChangeTime < 50000) {  // 50ms debounce
        return false;  // Ignore bouncing
    }
    
    // Detect rising edge (transition from not-pressed to pressed)
    bool pressed = (currentState && !lastButtonState);
    
    // Update state if changed
    if (currentState != lastButtonState) {
        lastButtonState = currentState;
        lastButtonChangeTime = now;
    }
    
    return pressed;
}
