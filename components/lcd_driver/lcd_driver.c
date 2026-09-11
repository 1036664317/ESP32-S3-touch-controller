#include "lcd_driver.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
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
    gpio_config_t rst_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << LCD_RST_PIN),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&rst_conf);

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

esp_err_t lcd_init(lcd_dev_t *dev, uint16_t width, uint16_t height)
{
    dev->width = LCD_PHYS_WIDTH;
    dev->height = LCD_PHYS_HEIGHT;
    dev->io_handle = NULL;

    ESP_LOGI(TAG, "Initializing AXS15231B QSPI LCD (phys %dx%d)", LCD_PHYS_WIDTH, LCD_PHYS_HEIGHT);

    // 1. Hardware Reset
    lcd_reset();

    // 2. Configure QSPI Bus
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

    // 3. Create Panel IO
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

    // 4. Send official Waveshare AXS15231B init sequence
    // Sleep out (0x11)
    tx_cmd(io_handle, 0x11, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(120));

    // MADCTL (0x36): default orientation
    uint8_t madctl = 0x00;
    tx_cmd(io_handle, 0x36, &madctl, 1);

    // COLMOD (0x3A): 16-bit RGB565 (0x55)
    uint8_t colmod = 0x55;
    tx_cmd(io_handle, 0x3A, &colmod, 1);

    // Normal display mode ON (0x13)
    tx_cmd(io_handle, 0x13, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(10));

    // Display ON (0x29)
    tx_cmd(io_handle, 0x29, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(120));

    ESP_LOGI(TAG, "AXS15231B QSPI initialization complete (Display ON)");

    // 5. Backlight: Active LOW PWM on GPIO8 (duty 0 = 100% brightness, duty 255 = OFF)
    // First drive GPIO low to immediately turn on backlight, then configure LEDC
    gpio_config_t bl_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << LCD_BL_PIN),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&bl_conf);
    gpio_set_level(LCD_BL_PIN, 0);  // Low level = Backlight ON

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
        .duty = 0,  // 0 = Full brightness (0xff - 255 in Waveshare BSP)
        .hpoint = 0,
    };
    ledc_channel_config(&ledc_channel);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);

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
    // Backlight is active LOW on this hardware (0 = 100%, 255 = 0%)
    uint32_t duty = 255 - brightness;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
    return ESP_OK;
}
