#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "esp_sleep.h"
#include "nvs_flash.h"
#include "esp_netif_sntp.h"
#include "lwip/ip_addr.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_pm.h"
#include "driver/gpio.h"
#include "driver/touch_pad.h"
#include "led_strip.h"

static const char *TAG = "key-bot";

//// CAPACITIVE SENSOR
// GPIO number of the capacitive sensor
#define TOUCH_PAD_NUM 0
// Touch threshold
#define TOUCH_THRESHOLD 410
// Set the time threshold in seconds (how long has the change in touch_value be to change the key_state bool)
#define TIME_THRESHOLD 1

//// WIFI
#ifndef INET6_ADDRSTRLEN
#define INET6_ADDRSTRLEN 48
#endif
/*set the ssid and password via "idf.py menuconfig"*/
#define DEFAULT_SSID CONFIG_EXAMPLE_WIFI_SSID
#define DEFAULT_PWD CONFIG_EXAMPLE_WIFI_PASSWORD
#define DEFAULT_LISTEN_INTERVAL CONFIG_EXAMPLE_WIFI_LISTEN_INTERVAL
#define DEFAULT_BEACON_TIMEOUT  CONFIG_EXAMPLE_WIFI_BEACON_TIMEOUT

#if CONFIG_EXAMPLE_POWER_SAVE_MIN_MODEM
#define DEFAULT_PS_MODE WIFI_PS_MIN_MODEM
#elif CONFIG_EXAMPLE_POWER_SAVE_MAX_MODEM
#define DEFAULT_PS_MODE WIFI_PS_MAX_MODEM
#elif CONFIG_EXAMPLE_POWER_SAVE_NONE
#define DEFAULT_PS_MODE WIFI_PS_NONE
#else

#define DEFAULT_PS_MODE WIFI_PS_NONE
#endif /*CONFIG_POWER_SAVE_MODEM*/

static void wifi_power_save(void);

//// TIME
static void obtain_time(void);

void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "Notification of a time synchronization event");
}

//// LED
// GPIO assignment for the LED
#define LED_STRIP_BLINK_GPIO  25
// LED numbers in the strip
#define LED_STRIP_LED_NUMBERS 1
// 10MHz resolution, 1 tick = 0.1us (led strip needs a high resolution)
#define LED_STRIP_RMT_RES_HZ  (10 * 1000 * 1000)

// Time duration of the visibility LED
#define VISIBILITY_LED_DURATION_MS 1000

static void blink_led(led_strip_handle_t led_strip, uint8_t red, uint8_t green, uint8_t blue);

static void visibility_led(led_strip_handle_t led_strip, bool visibility_led);

//// MAIN
void app_main(void)
{
    //// TIME SETUP
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    // Is time set? If not, tm_year will be (1970 - 1900).
    if (timeinfo.tm_year < (2016 - 1900)) {
        ESP_LOGI(TAG, "Time is not set yet. Connecting to WiFi and getting time over NTP.");
        obtain_time();
        // update 'now' variable with current time
        time(&now);
    }
    char strftime_buf[64];
    // Set timezone to Central European Summer Time (CEST)
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();

    //// CAPACITIVE SENSOR SETUP
    touch_pad_init();
    touch_pad_config(TOUCH_PAD_NUM, 0);
    // Set up GPIO for the capacitive sensor
    esp_rom_gpio_pad_select_gpio(TOUCH_PAD_NUM);
    gpio_set_direction(TOUCH_PAD_NUM, GPIO_MODE_INPUT);

    //// LED SETUP
    // LED Strip object handle
    led_strip_handle_t led_strip;

    // LED strip general initialization, according to your led board design
    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_STRIP_BLINK_GPIO,   // The GPIO that connected to the LED strip's data line
        .max_leds = LED_STRIP_LED_NUMBERS,        // The number of LEDs in the strip,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB, // Pixel format of your LED strip
        .led_model = LED_MODEL_WS2812,            // LED strip model
        .flags.invert_out = false,                // whether to invert the output signal
    };

    // LED strip backend configuration: RMT
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,        // different clock source can lead to different power consumption
        .resolution_hz = LED_STRIP_RMT_RES_HZ, // RMT counter clock frequency
        .flags.with_dma = false,               // DMA feature is available on ESP target like ESP32-S3
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    ESP_LOGI(TAG, "Created LED strip object with RMT backend");

    //// VARIABLES
    // key is logged as present on the sensor nail
    bool key_state = false;
    // OEMENTARY key state key is present on the nail (right now)
    bool momentary_key_state = false;
    // time the sensor spent below the threshold in milliseconds (momentary_key_state = true)
    uint32_t time_below_threshold_ms = 0;
    // time the sensor spent above the threshold in milliseconds (momentary_key_state = false)
    uint32_t time_above_threshold_ms = 0;
    // current touch value
    uint16_t touch_value;
    // time the visibility LED was turned on
    time_t visibility_led_on_time = 0; 
   
    // EVENT LOOP
    while(1) {
        // Get current time
        time(&now);
        localtime_r(&now, &timeinfo);
        // format time
        strftime(strftime_buf, sizeof(strftime_buf), "%Y-%m-%dT%H:%M:%S%z", &timeinfo);

        // Read touch value
        esp_err_t ret = touch_pad_read(TOUCH_PAD_NUM, &touch_value);

        // Check if key is present
        if (ret == ESP_OK) {
            // Determine whether touch_value is above or below threshold
            momentary_key_state = (touch_value < TOUCH_THRESHOLD);
            
            // If bellow_threshold is not the same as key_state, aka the key state has changed
            if (momentary_key_state != key_state) {
                // Update time_above_threshold_ms or time_below_threshold_ms accordingly
                uint32_t* time_ptr = momentary_key_state ? &time_below_threshold_ms : &time_above_threshold_ms;
                *time_ptr += 100;

                // Check if the time threshold has been reached
                if (*time_ptr >= TIME_THRESHOLD * 1000) {
                    // Change key_state to the new state
                    key_state = momentary_key_state;
                    
                    // Notify users of key state change
                    if (key_state) {
                        // Green LED feedback
                        blink_led(led_strip, 0, 255, 0);
                        // TODO: Send notification to Discord
                    } else {
                        // Red LED feedback
                        blink_led(led_strip, 255, 0, 0);
                        // Turn on visibility LED
                        visibility_led(led_strip, true);
                        // save time visibility LED was turned on
                        visibility_led_on_time = now;
                    }

                    // Log current time | key state
                    ESP_LOGI(TAG, "%s | Key: %s", strftime_buf, key_state ? "present" : "not present");
                    // Reset time_above_threshold_ms and time_below_threshold_ms
                    time_above_threshold_ms = 0;
                    time_below_threshold_ms = 0;
                }
            } else {
                // Reset time_above_threshold_ms and time_below_threshold_ms
                time_above_threshold_ms = 0;
                time_below_threshold_ms = 0;
            }
        } else {
            ESP_LOGE(TAG, "Error reading touch sensor of %d: %s\n", TOUCH_PAD_NUM, esp_err_to_name(ret));
        }

        // Check if visibility LED should be turned off
        if ((now - visibility_led_on_time) >= VISIBILITY_LED_DURATION_MS) {
            // Turn off visibility LED
            visibility_led(led_strip, false);
        }
   
        // Sleep for 100ms
        vTaskDelay(100 / portTICK_PERIOD_MS);
        }
}

// TIME
static void obtain_time(void)
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );

#if CONFIG_PM_ENABLE
    // Configure dynamic frequency scaling:
    // maximum and minimum frequencies are set in sdkconfig,
    // automatic light sleep is enabled if tickless idle support is enabled.
    esp_pm_config_t pm_config = {
            .max_freq_mhz = CONFIG_EXAMPLE_MAX_CPU_FREQ_MHZ,
            .min_freq_mhz = CONFIG_EXAMPLE_MIN_CPU_FREQ_MHZ,
#if CONFIG_FREERTOS_USE_TICKLESS_IDLE
            .light_sleep_enable = true
#endif
    };
    ESP_ERROR_CHECK( esp_pm_configure(&pm_config) );
#endif // CONFIG_PM_ENABLE

    wifi_power_save();

    ESP_LOGI(TAG, "Initializing and starting SNTP");
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_SNTP_TIME_SERVER);
    config.sync_cb = time_sync_notification_cb;

    esp_netif_sntp_init(&config);

    // wait for time to be set
    time_t now = 0;
    struct tm timeinfo = { 0 };
    int retry = 0;
    const int retry_count = 15;
    while (esp_netif_sntp_sync_wait(2000 / portTICK_PERIOD_MS) == ESP_ERR_TIMEOUT && ++retry < retry_count) {
        ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
    }
    time(&now);
    localtime_r(&now, &timeinfo);

    esp_netif_sntp_deinit();
}

// EVENT HANDLER (wifi)
static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

/*init wifi as sta and set power save mode*/
static void wifi_power_save(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = DEFAULT_SSID,
            .password = DEFAULT_PWD,
            .listen_interval = DEFAULT_LISTEN_INTERVAL,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_inactive_time(WIFI_IF_STA, DEFAULT_BEACON_TIMEOUT));

    ESP_LOGI(TAG, "esp_wifi_set_ps().");
    esp_wifi_set_ps(DEFAULT_PS_MODE);
}

// LED
// Blink LED strip 2 times at specified RGB color, used for key state change and knock command
static void blink_led(led_strip_handle_t led_strip, uint8_t red, uint8_t green, uint8_t blue) {
    ESP_LOGI(TAG, "Blinking LED 2 times, color: %u %u %u", red, green, blue);
    for (int i = 0; i < 2; i++) {
        // Set the LED pixel using the specified RGB values
        ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, 0, green, red, blue));
        // Refresh the strip to send data
        ESP_ERROR_CHECK(led_strip_refresh(led_strip));
        // Wait for 50ms
        vTaskDelay(pdMS_TO_TICKS(50));
        /* Set all LED off to clear all pixels */
        ESP_ERROR_CHECK(led_strip_clear(led_strip));
        // Wait for 500ms
        vTaskDelay(pdMS_TO_TICKS(150));
    };
}

// Toggle visibility (white) LED
static void visibility_led(led_strip_handle_t led_strip, bool visibility_led) {
    if (visibility_led) {
        ESP_LOGI(TAG, "Visibility LED turned ON");
        // Set the LED pixel using the specified RGB values
        ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, 0, 255, 255, 255));
        // Refresh the strip to send data
        ESP_ERROR_CHECK(led_strip_refresh(led_strip));
    } else {
        ESP_LOGI(TAG, "Visibility LED turned OFF");
        /* Set all LED off to clear all pixels */
        ESP_ERROR_CHECK(led_strip_clear(led_strip));
    };
}