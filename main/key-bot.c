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

static const char *TAG = "key-bot";

// GPIO number of the touch pad
#define TOUCH_PAD_NUM 0
// Touch threshold
#define TOUCH_THRESHOLD 430
// Set the time threshold in seconds (how long has the change in touch_value be to change the key_present bool)
#define TIME_THRESHOLD 5

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

static void obtain_time(void);
static void wifi_power_save(void);

void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "Notification of a time synchronization event");
}

void app_main(void)
{
    // Initialize NVS
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

    // Initialize touch pad
    touch_pad_init();
    touch_pad_config(TOUCH_PAD_NUM, 0);
    // Set up GPIO
    esp_rom_gpio_pad_select_gpio(TOUCH_PAD_NUM);
    gpio_set_direction(TOUCH_PAD_NUM, GPIO_MODE_INPUT);

    // Variable storing the key state (key is present on the nail)
    bool key_present = false;
    // Variable storing the time below the threshold in milliseconds (true)
    int time_below_threshold_ms = 0;
    // Variable storing the time above the threshold in milliseconds (false)
    int time_above_threshold_ms = 0;
   
    // Infinite loop
    while(1) {
        // Get current time
        time(&now);
        localtime_r(&now, &timeinfo);
        // format time
        strftime(strftime_buf, sizeof(strftime_buf), "%Y-%m-%dT%H:%M:%S%z", &timeinfo);

        // Read touch value
        uint16_t touch_value;
        esp_err_t ret = touch_pad_read(TOUCH_PAD_NUM, &touch_value);

        // Check if key is present
        if (ret == ESP_OK) {
            // touch_value is below the threshold (true)
            if (touch_value < TOUCH_THRESHOLD) {
                // Reset time above threshold
                time_above_threshold_ms = 0;

                // Add 100ms to the time below threshold
                time_below_threshold_ms += 100;
    
                // Check if the time threshold has been reached
                if (time_below_threshold_ms >= TIME_THRESHOLD * 1000) {
                    // Key is present
                    if (!key_present) {
                        key_present = true;
                        // Print current time | touch value | key present bool
                        ESP_LOGI(TAG, "%s | Touch value: %d | Key: %s", strftime_buf, touch_value, key_present ? "present" : "not present");
                    }
                }
            // touch_value is above threshold (false)
            } else {
                // Reset time below threshold
                time_below_threshold_ms = 0;

                // Add 100ms to the time above threshold
                time_above_threshold_ms += 100;

                // Check if the time threshold has been reached
                if (time_above_threshold_ms >= TIME_THRESHOLD * 1000) {
                    // Key is not present
                    if (key_present) {
                        key_present = false;
                        // Print current time | touch value | key present bool
                        ESP_LOGI(TAG, "%s | Touch value: %d | Key: %s", strftime_buf, touch_value, key_present ? "present" : "not present");
                    }
                }
            }
        } else {
            ESP_LOGE(TAG, "Error reading touch sensor of %d: %s\n", TOUCH_PAD_NUM, esp_err_to_name(ret));
        }
   
        // Sleep for 100ms
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

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
