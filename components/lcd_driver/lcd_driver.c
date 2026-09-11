#include "lcd_driver.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "LCD";

// GPIO pins for ESP32-S3-Touch-LCD-3.49 V1
#define LCD_CS_PIN      9
#define LCD_PCLK_PIN    10
#define LCD_D0_PIN      11
#define LCD_D1_PIN      12
#define LCD_D2_PIN      13
#define LCD_D3_PIN      14
#define LCD_RST_PIN     21
#define LCD_BL_PIN      8
#define LCD_HOST        SPI3_HOST
#define LCD_PCLK_HZ     (40 * 1000 * 1000)

#define AXS_OPCODE_WRITE_CMD   0x02
#define AXS_OPCODE_WRITE_COLOR 0x32

static SemaphoreHandle_t s_flush_sem = NULL;

static bool on_color_trans_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    BaseType_t need_yield = pdFALSE;
    if (s_flush_sem) {
        xSemaphoreGiveFromISR(s_flush_sem, &need_yield);
    }
    return need_yield == pdTRUE;
}

static void lcd_reset(void)
{
    gpio_set_direction(LCD_RST_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(LCD_RST_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(30));
    gpio_set_level(LCD_RST_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(250));
    gpio_set_level(LCD_RST_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(120));
}

static esp_err_t tx_cmd(esp_lcd_panel_io_handle_t io, int cmd, const void *param, size_t param_size)
{
    int qspi_cmd = (AXS_OPCODE_WRITE_CMD << 24) | ((cmd & 0xFF) << 8);
    return esp_lcd_panel_io_tx_param(io, qspi_cmd, param, param_size);
}

static esp_err_t tx_color(esp_lcd_panel_io_handle_t io, int cmd, const void *param, size_t param_size)
{
    int qspi_cmd = (AXS_OPCODE_WRITE_COLOR << 24) | ((cmd & 0xFF) << 8);
    return esp_lcd_panel_io_tx_color(io, qspi_cmd, param, param_size);
}

typedef struct {
    int cmd;
    const void *data;
    size_t data_bytes;
    unsigned int delay_ms;
} lcd_init_cmd_t;

static const lcd_init_cmd_t vendor_specific_init_default[] = {
    {0xBB, (uint8_t[]){0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5A, 0xA5}, 8, 0},
    {0xA0, (uint8_t[]){0x00, 0x10, 0x00, 0x02, 0x00, 0x00, 0x64, 0x3F, 0x20, 0x05, 0x3F, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x00}, 17, 0},
    {0xA2, (uint8_t[]){0x30, 0x04, 0x0A, 0x3C, 0xEC, 0x54, 0xC4, 0x30, 0xAC, 0x28, 0x7F, 0x7F, 0x7F, 0x20, 0xF8, 0x10, 0x02, 0xFF, 0xFF, 0xF0, 0x90, 0x01, 0x32, 0xA0, 0x91, 0xC0, 0x20, 0x7F, 0xFF, 0x00, 0x54}, 31, 0},
    {0xD0, (uint8_t[]){0x30, 0xAC, 0x21, 0x24, 0x08, 0x09, 0x10, 0x01, 0xAA, 0x14, 0xC2, 0x00, 0x22, 0x22, 0xAA, 0x03, 0x10, 0x12, 0x40, 0x14, 0x1E, 0x51, 0x15, 0x00, 0x40, 0x10, 0x00, 0x03, 0x3D, 0x12}, 30, 0},
    {0xA3, (uint8_t[]){0xA0, 0x06, 0xAA, 0x08, 0x08, 0x02, 0x0A, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x55, 0x55}, 22, 0},
    {0xC1, (uint8_t[]){0x33, 0x04, 0x02, 0x02, 0x71, 0x05, 0x24, 0x55, 0x02, 0x00, 0x41, 0x00, 0x53, 0xFF, 0xFF, 0xFF, 0x4F, 0x52, 0x00, 0x4F, 0x52, 0x00, 0x45, 0x3B, 0x0B, 0x02, 0x0D, 0x00, 0xFF, 0x40}, 30, 0},
    {0xC3, (uint8_t[]){0x00, 0x00, 0x00, 0x50, 0x03, 0x00, 0x00, 0x00, 0x01, 0x80, 0x01}, 11, 0},
    {0xC4, (uint8_t[]){0x00, 0x24, 0x33, 0x90, 0x50, 0xea, 0x64, 0x32, 0xC8, 0x64, 0xC8, 0x32, 0x90, 0x90, 0x11, 0x06, 0xDC, 0xFA, 0x04, 0x03, 0x80, 0xFE, 0x10, 0x10, 0x00, 0x0A, 0x0A, 0x44, 0x50}, 29, 0},
    {0xC5, (uint8_t[]){0x18, 0x00, 0x00, 0x03, 0xFE, 0x78, 0x33, 0x20, 0x30, 0x10, 0x88, 0xDE, 0x0D, 0x08, 0x0F, 0x0F, 0x01, 0x78, 0x33, 0x20, 0x10, 0x10, 0x80}, 23, 0},
    {0xC6, (uint8_t[]){0x05, 0x0A, 0x05, 0x0A, 0x00, 0xE0, 0x2E, 0x0B, 0x12, 0x22, 0x12, 0x22, 0x01, 0x00, 0x00, 0x3F, 0x6A, 0x18, 0xC8, 0x22}, 20, 0},
    {0xC7, (uint8_t[]){0x50, 0x32, 0x28, 0x00, 0xa2, 0x80, 0x8f, 0x00, 0x80, 0xff, 0x07, 0x11, 0x9F, 0x6f, 0xff, 0x26, 0x0c, 0x0d, 0x0e, 0x0f}, 20, 0},
    {0xC9, (uint8_t[]){0x33, 0x44, 0x44, 0x01}, 4, 0},
    {0xCF, (uint8_t[]){0x34, 0x1E, 0x88, 0x58, 0x13, 0x18, 0x56, 0x18, 0x1E, 0x68, 0xF7, 0x00, 0x65, 0x0C, 0x22, 0xC4, 0x0C, 0x77, 0x22, 0x44, 0xAA, 0x55, 0x04, 0x04, 0x12, 0xA0, 0x08}, 27, 0},
    {0xD5, (uint8_t[]){0x3E, 0x3E, 0x88, 0x00, 0x44, 0x04, 0x78, 0x33, 0x20, 0x78, 0x33, 0x20, 0x04, 0x28, 0xD3, 0x47, 0x03, 0x03, 0x03, 0x03, 0x86, 0x00, 0x00, 0x00, 0x30, 0x52, 0x3f, 0x40, 0x40, 0x96}, 30, 0},
    {0xD6, (uint8_t[]){0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE, 0x95, 0x00, 0x01, 0x83, 0x75, 0x36, 0x20, 0x75, 0x36, 0x20, 0x3F, 0x03, 0x03, 0x03, 0x10, 0x10, 0x00, 0x04, 0x51, 0x20, 0x01, 0x00}, 30, 0},
    {0xD7, (uint8_t[]){0x0a, 0x08, 0x0e, 0x0c, 0x1E, 0x18, 0x19, 0x1F, 0x00, 0x1F, 0x1A, 0x1F, 0x3E, 0x3E, 0x04, 0x00, 0x1F, 0x1F, 0x1F}, 19, 0},
    {0xD8, (uint8_t[]){0x0B, 0x09, 0x0F, 0x0D, 0x1E, 0x18, 0x19, 0x1F, 0x01, 0x1F, 0x1A, 0x1F}, 12, 0},
    {0xD9, (uint8_t[]){0x00, 0x0D, 0x0F, 0x09, 0x0B, 0x1F, 0x18, 0x19, 0x1F, 0x01, 0x1E, 0x1A, 0x1F}, 13, 0},
    {0xDD, (uint8_t[]){0x0C, 0x0E, 0x08, 0x0A, 0x1F, 0x18, 0x19, 0x1F, 0x00, 0x1E, 0x1A, 0x1F}, 12, 0},
    {0xDF, (uint8_t[]){0x44, 0x73, 0x4B, 0x69, 0x00, 0x0A, 0x02, 0x90}, 8, 0},
    {0xE0, (uint8_t[]){0x19, 0x20, 0x0A, 0x13, 0x0E, 0x09, 0x12, 0x28, 0xD4, 0x24, 0x0C, 0x35, 0x13, 0x31, 0x36, 0x2f, 0x03}, 17, 0},
    {0xE1, (uint8_t[]){0x38, 0x20, 0x09, 0x12, 0x0E, 0x08, 0x12, 0x28, 0xC5, 0x24, 0x0C, 0x34, 0x12, 0x31, 0x36, 0x2f, 0x27}, 17, 0},
    {0xE2, (uint8_t[]){0x19, 0x20, 0x0A, 0x11, 0x09, 0x06, 0x11, 0x25, 0xD4, 0x22, 0x0B, 0x33, 0x12, 0x2D, 0x32, 0x2f, 0x03}, 17, 0},
    {0xE3, (uint8_t[]){0x38, 0x20, 0x0A, 0x11, 0x09, 0x06, 0x11, 0x25, 0xC4, 0x21, 0x0A, 0x32, 0x11, 0x2C, 0x32, 0x2f, 0x27}, 17, 0},
    {0xE4, (uint8_t[]){0x19, 0x20, 0x0D, 0x14, 0x0D, 0x08, 0x12, 0x2A, 0xD4, 0x26, 0x0E, 0x35, 0x13, 0x34, 0x39, 0x2f, 0x03}, 17, 0},
    {0xE5, (uint8_t[]){0x38, 0x20, 0x0D, 0x13, 0x0D, 0x07, 0x12, 0x29, 0xC4, 0x25, 0x0D, 0x35, 0x12, 0x33, 0x39, 0x2f, 0x27}, 17, 0},
    {0xBB, (uint8_t[]){0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 8, 0},
    {0x13, (uint8_t[]){0x00}, 0, 0},
    {0x11, (uint8_t[]){0x00}, 0, 200},
    {0x29, (uint8_t[]){0x00}, 0, 200},
    {0x2C, (uint8_t[]){0x00, 0x00, 0x00, 0x00}, 4, 0},
    {0x22, (uint8_t[]){0x00}, 0, 200},
};

esp_err_t lcd_init(lcd_dev_t *dev, uint16_t width, uint16_t height)
{
    dev->width = LCD_PHYS_WIDTH;
    dev->height = LCD_PHYS_HEIGHT;
    dev->io_handle = NULL;

    ESP_LOGI(TAG, "Initializing AXS15231B QSPI LCD (phys %dx%d)", LCD_PHYS_WIDTH, LCD_PHYS_HEIGHT);

    lcd_reset();

    spi_bus_config_t buscfg = {
        .data0_io_num = LCD_D0_PIN,
        .data1_io_num = LCD_D1_PIN,
        .sclk_io_num = LCD_PCLK_PIN,
        .data2_io_num = LCD_D2_PIN,
        .data3_io_num = LCD_D3_PIN,
        .max_transfer_sz = LCD_PHYS_WIDTH * 64 * sizeof(uint16_t),
    };

    esp_err_t ret = spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    if (!s_flush_sem) {
        s_flush_sem = xSemaphoreCreateBinary();
    }

    esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = LCD_CS_PIN,
        .dc_gpio_num = -1,
        .spi_mode = 3,
        .pclk_hz = LCD_PCLK_HZ,
        .trans_queue_depth = 10,
        .on_color_trans_done = on_color_trans_done,
        .user_ctx = NULL,
        .lcd_cmd_bits = 32,
        .lcd_param_bits = 8,
        .flags = {
            .quad_mode = true,
        },
    };

    esp_lcd_panel_io_handle_t io_handle = NULL;
    ret = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LCD panel IO init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    dev->io_handle = io_handle;

    // Sleep out
    tx_cmd(io_handle, 0x11, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(120));

    // MADCTL: default orientation (RGB order)
    uint8_t madctl = 0x00;
    tx_cmd(io_handle, 0x36, &madctl, 1);

    // COLMOD: 16-bit/pixel (RGB565)
    uint8_t colmod = 0x55;
    tx_cmd(io_handle, 0x3A, &colmod, 1);

    // Vendor initialization sequence
    int num_cmds = sizeof(vendor_specific_init_default) / sizeof(vendor_specific_init_default[0]);
    for (int i = 0; i < num_cmds; i++) {
        tx_cmd(io_handle, vendor_specific_init_default[i].cmd,
               vendor_specific_init_default[i].data,
               vendor_specific_init_default[i].data_bytes);
        if (vendor_specific_init_default[i].delay_ms > 0) {
            vTaskDelay(pdMS_TO_TICKS(vendor_specific_init_default[i].delay_ms));
        }
    }

    ESP_LOGI(TAG, "AXS15231B QSPI initialization complete");

    // Initialize backlight PWM via LEDC
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = LEDC_TIMER_3,
        .freq_hz = 50000,
        .clk_cfg = LEDC_SLOW_CLK_RC_FAST,
    };
    ledc_timer_config(&ledc_timer);

    ledc_channel_config_t ledc_channel = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_1,
        .timer_sel = LEDC_TIMER_3,
        .gpio_num = LCD_BL_PIN,
        .duty = 255,
        .hpoint = 0,
    };
    ledc_channel_config(&ledc_channel);

    ESP_LOGI(TAG, "QSPI LCD initialized successfully");
    return ESP_OK;
}

esp_err_t lcd_draw_bitmap(lcd_dev_t *dev, int x_start, int y_start, int x_end, int y_end, const void *color_data)
{
    if (!dev || !dev->io_handle) return ESP_ERR_INVALID_STATE;

    // Set column address (CASET 0x2A)
    uint8_t caset[4] = {
        (uint8_t)((x_start >> 8) & 0xFF),
        (uint8_t)(x_start & 0xFF),
        (uint8_t)(((x_end - 1) >> 8) & 0xFF),
        (uint8_t)((x_end - 1) & 0xFF)
    };
    tx_cmd(dev->io_handle, 0x2A, caset, 4);

    // In QSPI mode, AXS15231B does not use RASET.
    // Line 0 uses RAMWR (0x2C), subsequent continuous lines use RAMWRC (0x3C).
    size_t len = (x_end - x_start) * (y_end - y_start) * sizeof(uint16_t);
    if (y_start == 0) {
        tx_color(dev->io_handle, 0x2C, color_data, len);
    } else {
        tx_color(dev->io_handle, 0x3C, color_data, len);
    }

    if (s_flush_sem) {
        if (xSemaphoreTake(s_flush_sem, pdMS_TO_TICKS(500)) != pdTRUE) {
            ESP_LOGW(TAG, "DMA transfer timeout at line %d-%d", y_start, y_end);
        }
    }

    return ESP_OK;
}

esp_err_t lcd_fill_screen(lcd_dev_t *dev, uint16_t color)
{
    if (!dev || !dev->io_handle) return ESP_ERR_INVALID_STATE;

    ESP_LOGI(TAG, "Filling full screen %dx%d with color 0x%04X", LCD_PHYS_WIDTH, LCD_PHYS_HEIGHT, color);

    const int chunk_lines = 64;
    size_t chunk_bytes = LCD_PHYS_WIDTH * chunk_lines * sizeof(uint16_t);
    uint16_t *buf = heap_caps_malloc(chunk_bytes, MALLOC_CAP_DMA);
    if (!buf) {
        ESP_LOGE(TAG, "Failed to allocate fill screen DMA buffer");
        return ESP_ERR_NO_MEM;
    }

    for (int i = 0; i < LCD_PHYS_WIDTH * chunk_lines; i++) {
        buf[i] = color;
    }

    for (int y = 0; y < LCD_PHYS_HEIGHT; y += chunk_lines) {
        int lines = chunk_lines;
        if (y + lines > LCD_PHYS_HEIGHT) {
            lines = LCD_PHYS_HEIGHT - y;
        }
        lcd_draw_bitmap(dev, 0, y, LCD_PHYS_WIDTH, y + lines, buf);
    }

    free(buf);
    ESP_LOGI(TAG, "Full screen fill complete");
    return ESP_OK;
}

esp_err_t lcd_set_brightness(uint8_t brightness)
{
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, brightness);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
    return ESP_OK;
}
