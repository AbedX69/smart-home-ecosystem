#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <driver/gpio.h>
#include <driver/rmt_tx.h>
#include <driver/rmt_encoder.h>
#include <string.h>

static const char* TAG = "led";

#define LED_PIN     GPIO_NUM_13
#define NUM_LEDS    10

// SK6812 RGBW datasheet timing
#define T0H_NS  300
#define T0L_NS  900
#define T1H_NS  600
#define T1L_NS  600
#define RESET_US 80
#define RMT_HZ  10000000

static rmt_channel_handle_t chan;
static rmt_encoder_handle_t enc;
static uint8_t buf[NUM_LEDS * 4];  // RGBW, 4 bytes per LED

typedef struct {
    rmt_encoder_t base;
    rmt_encoder_t* bytes_enc;
    rmt_encoder_t* copy_enc;
    rmt_symbol_word_t reset;
    int state;
} led_enc_t;

static size_t IRAM_ATTR encode_cb(rmt_encoder_t* e, rmt_channel_handle_t ch,
    const void* data, size_t sz, rmt_encode_state_t* st) {
    led_enc_t* le = __containerof(e, led_enc_t, base);
    rmt_encode_state_t ss = RMT_ENCODING_RESET;
    size_t n = 0;
    switch (le->state) {
        case 0:
            n += le->bytes_enc->encode(le->bytes_enc, ch, data, sz, &ss);
            if (ss & RMT_ENCODING_COMPLETE) le->state = 1;
            if (ss & RMT_ENCODING_MEM_FULL) { *st = RMT_ENCODING_MEM_FULL; return n; }
            /* FALLTHROUGH */
        case 1:
            n += le->copy_enc->encode(le->copy_enc, ch, &le->reset, sizeof(le->reset), &ss);
            if (ss & RMT_ENCODING_COMPLETE) { le->state = 0; *st = RMT_ENCODING_COMPLETE; }
            break;
    }
    return n;
}
static esp_err_t reset_cb(rmt_encoder_t* e) {
    led_enc_t* le = __containerof(e, led_enc_t, base);
    rmt_encoder_reset(le->bytes_enc); rmt_encoder_reset(le->copy_enc);
    le->state = 0; return ESP_OK;
}
static esp_err_t del_cb(rmt_encoder_t* e) {
    led_enc_t* le = __containerof(e, led_enc_t, base);
    rmt_del_encoder(le->bytes_enc); rmt_del_encoder(le->copy_enc);
    delete le; return ESP_OK;
}

void fillRGBW(uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
    // RGBW order per datasheet
    for (int i = 0; i < NUM_LEDS; i++) {
        buf[i*4+0] = r;
        buf[i*4+1] = g;
        buf[i*4+2] = b;
        buf[i*4+3] = w;
    }
    rmt_transmit_config_t tc = {}; tc.loop_count = 0;
    rmt_transmit(chan, enc, buf, sizeof(buf), &tc);
    rmt_tx_wait_all_done(chan, pdMS_TO_TICKS(1000));
}

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "SK6812 RGBW - RMT on GPIO 13");

    rmt_tx_channel_config_t cc = {};
    cc.gpio_num = LED_PIN;
    cc.clk_src = RMT_CLK_SRC_DEFAULT;
    cc.resolution_hz = RMT_HZ;
    cc.mem_block_symbols = 64;
    cc.trans_queue_depth = 1;
    ESP_ERROR_CHECK(rmt_new_tx_channel(&cc, &chan));

    uint32_t t0h = T0H_NS * RMT_HZ / 1000000000;
    uint32_t t0l = T0L_NS * RMT_HZ / 1000000000;
    uint32_t t1h = T1H_NS * RMT_HZ / 1000000000;
    uint32_t t1l = T1L_NS * RMT_HZ / 1000000000;

    led_enc_t* le = new led_enc_t();
    le->base.encode = encode_cb;
    le->base.reset = reset_cb;
    le->base.del = del_cb;
    le->state = 0;

    rmt_bytes_encoder_config_t bc = {};
    bc.bit0.level0 = 1; bc.bit0.duration0 = t0h;
    bc.bit0.level1 = 0; bc.bit0.duration1 = t0l;
    bc.bit1.level0 = 1; bc.bit1.duration0 = t1h;
    bc.bit1.level1 = 0; bc.bit1.duration1 = t1l;
    bc.flags.msb_first = true;
    ESP_ERROR_CHECK(rmt_new_bytes_encoder(&bc, &le->bytes_enc));

    rmt_copy_encoder_config_t cpc = {};
    ESP_ERROR_CHECK(rmt_new_copy_encoder(&cpc, &le->copy_enc));

    uint32_t rt = RESET_US * RMT_HZ / 1000000;
    le->reset.level0 = 0; le->reset.duration0 = rt/2;
    le->reset.level1 = 0; le->reset.duration1 = rt/2;

    enc = &le->base;
    ESP_ERROR_CHECK(rmt_enable(chan));

    ESP_LOGI(TAG, "Running. T0H=%lu T0L=%lu T1H=%lu T1L=%lu ticks", t0h, t0l, t1h, t1l);

    while (true) {
        ESP_LOGI(TAG, "RED");    fillRGBW(255,0,0,0);   vTaskDelay(pdMS_TO_TICKS(3000));
        ESP_LOGI(TAG, "GREEN");  fillRGBW(0,255,0,0);   vTaskDelay(pdMS_TO_TICKS(3000));
        ESP_LOGI(TAG, "BLUE");   fillRGBW(0,0,255,0);   vTaskDelay(pdMS_TO_TICKS(3000));
        ESP_LOGI(TAG, "WHITE");  fillRGBW(0,0,0,255);   vTaskDelay(pdMS_TO_TICKS(3000));
        ESP_LOGI(TAG, "OFF");    fillRGBW(0,0,0,0);     vTaskDelay(pdMS_TO_TICKS(2000));
    }
}