/**
 * This is a demo to be used with Good Display 2.7 touch epaper.
 * Wi-Fi provision is done using ESP-Rainmaker app
 * ON /OFF and additional settings can be controlled from there
 */ 
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
// I2C RTC
#include "ds3231.h"
// I2C Touch
#include "FT6X36.h"
// SPI Epaper display class
#include "goodisplay/gdey027T91.h"

// Non-Volatile Storage (NVS)
#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"
// Time & WiFi
#include <time.h>
#include <sys/time.h>
#include "protocol_examples_common.h"
#include "esp_sntp.h"
// Rainmaker
#include <esp_rmaker_core.h>
#include <esp_rmaker_standard_types.h>
#include <esp_rmaker_standard_params.h>
#include <esp_rmaker_standard_devices.h>
#include <esp_rmaker_ota.h>
#include <esp_rmaker_schedule.h>
#include <esp_rmaker_utils.h>
#include <esp_rmaker_factory.h>
#include <esp_rmaker_common_events.h>
#include <app_wifi.h>

// Strings in the UX (Labels of each form element)
#define DEVICE_PARAM_POWER "SWITCH"
#define DEVICE_NAME "Device name"
#define DEVICE_PARAM_CONSUMPTION "Device Watts"
#define DEVICE_PARAM_KW_HOUR "Cost of each KW"
#define DEVICE_PARAM_WIFI_RESET "Turn slider to 100 to reset WiFi"

// This are the default values for the inputs (ESP-Rainmaker Form)
char * switch_name = (char *)"EPD Switch";
char * switch_consumption = (char *)"50";  // Default values can be changed in the app
char * switch_kilowatt_hr = (char *)"0.5";

// Values stored in NVS
uint16_t nvs_watts = 50;            // Default value
uint16_t nvs_kw_cost = 500;         // 0.5 *1000 = 500
float  nvs_kw_float_cost = 0.5;

bool ready_mqtt = false;

// INTGPIO is touch interrupt, goes low when it detects a touch, coordinates are read by I2C
FT6X36 ts(CONFIG_TOUCH_INT);
EpdSpi io;
Gdey027T91 display(io);
uint8_t display_rotation = 1;

esp_rmaker_device_t *switch_device;

// Only debugging:
//#define DEBUG_COUNT_TOUCH

// Relay Latch IOs ON / OFF (On HIGH the transistor connects GND)
#define GPIO_RELAY_ON 1
#define GPIO_RELAY_OFF 2

#define RTC_INT GPIO_NUM_6
// Pulse to move the switch in millis
const uint16_t signal_ms = 50;

// Each time the counter hits this amount, store seconds counter in NVS and commit
// Make this please multiple of 60 or you can get an inexact count (Will also show Red alert in Serial)
const uint16_t nvs_save_each_secs = 120;

xQueueHandle on_min_counter_queue;

// Fonts are already included in components Fonts directory (Check it's CMakeFiles)
#include <Ubuntu_L7pt8b.h>
#include <Ubuntu_M16pt8b.h>
#include <Ubuntu_M36pt7b.h>
// Translations: select only one
#include "english.h"

static const char *TAG = "Cloud-switch";
struct tm rtcinfo;
nvs_handle_t storage_handle;
// Every 5 minutes the current_month count is updated
int32_t min_l = 0; // last month
int32_t min_c = 0; // current

esp_err_t ds3231_initialization_status = ESP_OK;

// Switch always starts OFF when Firmware starts
bool switch_state = false; // starts false = OFF

int switch_on_sec_count = 0;
int switch_all_sec_count = 0;

// I2C descriptor
i2c_dev_t dev;

extern "C"
{
   void app_main();
}

void delay(uint32_t millis) { vTaskDelay(millis / portTICK_PERIOD_MS); }

void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "Notification of a time synchronization event");
}

static bool obtain_time(void)
{
    // NTP init is already done by Rainmaker
    ESP_LOGI("DS3231", "RTC NTP Sync started");
    // wait for time to be set
    int retry = 0;
    const int retry_count = 10;
    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < retry_count) {
        ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }

    if (retry == retry_count) {
        ESP_LOGE("DS3231", "RTC NTP Sync failed");
        return false;
    }
    ESP_LOGI("DS3231", "RTC NTP Sync DONE");
    return true;
}

void setClock()
{
    // obtain time over NTP
    if(!obtain_time()) {
        ESP_LOGE(pcTaskGetName(0), "Fail to getting time over NTP.");
        while (1) { vTaskDelay(1); }
    }

    // update 'now' variable with current time
    time_t now;
    struct tm timeinfo;
    char strftime_buf[64];
    time(&now);
    now = now + (CONFIG_TIMEZONE*60*60);
    localtime_r(&now, &timeinfo);
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    ESP_LOGI(pcTaskGetName(0), "The current date/time is: %s", strftime_buf);

    ESP_LOGD(pcTaskGetName(0), "timeinfo.tm_sec=%d",timeinfo.tm_sec);
    ESP_LOGD(pcTaskGetName(0), "timeinfo.tm_min=%d",timeinfo.tm_min);
    ESP_LOGD(pcTaskGetName(0), "timeinfo.tm_hour=%d",timeinfo.tm_hour);
    ESP_LOGD(pcTaskGetName(0), "timeinfo.tm_wday=%d",timeinfo.tm_wday);
    ESP_LOGD(pcTaskGetName(0), "timeinfo.tm_mday=%d",timeinfo.tm_mday);
    ESP_LOGD(pcTaskGetName(0), "timeinfo.tm_mon=%d",timeinfo.tm_mon);
    ESP_LOGD(pcTaskGetName(0), "timeinfo.tm_year=%d",timeinfo.tm_year);

    printf("Setting tm_wday: %d\n\n", timeinfo.tm_wday);

    struct tm time = {
        .tm_sec  = timeinfo.tm_sec,
        .tm_min  = timeinfo.tm_min,
        .tm_hour = timeinfo.tm_hour,
        .tm_mday = timeinfo.tm_mday,
        .tm_mon  = timeinfo.tm_mon,  // 0-based
        .tm_year = timeinfo.tm_year + 1900,
        .tm_wday = timeinfo.tm_wday
    };

    if (ds3231_set_time(&dev, &time) != ESP_OK) {
        ESP_LOGE(pcTaskGetName(0), "Could not set time.");
        while (1) { vTaskDelay(1); }
    }
    ESP_LOGI(pcTaskGetName(0), "Set initial date time done");

    display.setCursor(10,60);
    display.print("Initial date time is saved on RTC.");
    display.setCursor(10,90);
    display.setFont(&Ubuntu_M16pt8b);
    display.printerf("%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);

    display.setFont(&Ubuntu_L7pt8b);
    display.setCursor(10,110);
    display.println("RTC alarm set to tick every sec.");
    ds3231_clear_alarm_flags(&dev, DS3231_ALARM_1);
    display.update();
    
    // Set an alarm to tick every minute
    //ds3231_set_alarm(&dev, DS3231_ALARM_2, &time, (ds3231_alarm1_rate_t)0,  &time, DS3231_ALARM2_EVERY_MIN);
    //ds3231_enable_alarm_ints(&dev, DS3231_ALARM_2);
    // More precise: Every second
    ds3231_set_alarm(&dev, DS3231_ALARM_1, &time, DS3231_ALARM1_EVERY_SECOND,  &time, DS3231_ALARM2_EVERY_MIN);
    ds3231_enable_alarm_ints(&dev, DS3231_ALARM_1);
    
}

static void IRAM_ATTR gpio_interrupt_handler(void *args)
{
    int pinNumber = (int)args;
    xQueueSendFromISR(on_min_counter_queue, &pinNumber, NULL);
}

void on_Task(void *params)
{
    int pinNumber; // IO with interrupt
  
    while (true)
    {
        if (xQueueReceive(on_min_counter_queue, &pinNumber, portMAX_DELAY))
        {
            switch_all_sec_count++;
            if (switch_all_sec_count % nvs_save_each_secs == 0) {
                // This should be done in a much more efficient way
                if (ds3231_get_time(&dev, &rtcinfo) == ESP_OK) {
                    // Each day 1 at 00:00 HRs reset secs counter and save last month in min_l
                    ESP_LOGI("DS3231", "Attempt RST counter|mday:%d hr:%d min:%d", rtcinfo.tm_mday,rtcinfo.tm_hour, rtcinfo.tm_min);
                    uint8_t save_each_mins = nvs_save_each_secs/60;
                    if (rtcinfo.tm_mday == 1 && rtcinfo.tm_hour == 0 && rtcinfo.tm_min <= save_each_mins*2 && min_c > save_each_mins) {
                        printf("RESET Counter and save last month totals\n\n");
                        nvs_set_i32(storage_handle, "min_l", min_c);
                        min_l = min_c;
                        min_c = -1;
                        switch_on_sec_count = 0;
                        switch_all_sec_count = 0;
                    }
                } else {
                    ESP_LOGE("on_Task", "DS3231 could not get_time");
                }
          }
          // Count only when switch is ON
            if (switch_state) {
                switch_on_sec_count++;
                if (switch_on_sec_count % nvs_save_each_secs == 0) {
                        min_c += nvs_save_each_secs /60;
                        nvs_set_i32(storage_handle, "min_c", min_c);
                        esp_err_t err = nvs_commit(storage_handle);
                        printf((err != ESP_OK) ? "NVS persist failed!\n" : "NVS commit\n");
                    }
          }
          ds3231_clear_alarm_flags(&dev, DS3231_ALARM_1);
          ds3231_clear_alarm_flags(&dev, DS3231_ALARM_2);

          printf("ON:%d sec,%d min|pow:%s\n",
          switch_on_sec_count, (int)min_c, (switch_state)?(char*)"ON":(char*)"OFF");
        }
    }
}

// Some GFX constants
uint16_t blockWidth = 42;
uint16_t blockHeight = display.height()/4;
uint16_t lineSpacing = 18;
uint16_t circleColor = EPD_BLACK;
uint16_t circleRadio = 10;
uint16_t selectTextColor  = EPD_WHITE;
uint16_t selectBackground = EPD_BLACK;
template <typename T> static inline void
swap(T& a, T& b)
{
  T t = a;
  a = b;
  b = t;
}

void draw_centered_text(const GFXfont *font, char * text, int16_t x, int16_t y, uint16_t w, uint16_t h) {
    // Draw external boundary where text needs to be centered in the middle
    //printf("drawRect x:%d y:%d w:%d h:%d\n\n", x, y, w, h);
    display.drawRect(x, y, w, h, EPD_DARKGREY);

    display.setFont(font);
    int16_t text_x = 0;
    int16_t text_y = 0;
    uint16_t text_w = 0;
    uint16_t text_h = 0;

    display.getTextBounds(text, x, y, &text_x, &text_y, &text_w, &text_h);
    //display.drawRect(text_x, text_y, text_w, text_h, EPD_BLACK); // text boundaries

    if (text_w > w) {
        printf("W: Text width out of bounds");
    }
    if (text_h > h) {
        printf("W: Text height out of bounds");
    }
    // Calculate the middle position
    text_x += (w-text_w)/2;
    uint ty = (h/2)+y+(text_h/2);
    display.setCursor(text_x, ty);
    display.print(text);
}

void switchState(bool state) {
  if (state) {
    gpio_set_level((gpio_num_t)GPIO_RELAY_ON, 1); // OFF
    delay(signal_ms);
    gpio_set_level((gpio_num_t)GPIO_RELAY_ON, 0); // OFF release
  } else {
    gpio_set_level((gpio_num_t)GPIO_RELAY_OFF, 1); // OFF
    delay(signal_ms);
    gpio_set_level((gpio_num_t)GPIO_RELAY_OFF, 0); // OFF release
  }
}

void getClock() {
  // Clean display
  display.fillScreen(EPD_WHITE);

  // Get RTC date and time
  float temp;
  if (ds3231_get_temp_float(&dev, &temp) != ESP_OK) {
      ESP_LOGE(TAG, "Could not get temperature.");
      return;
  }
  // Already got it in main() but otherwise could be done here
  if (ds3231_get_time(&dev, &rtcinfo) != ESP_OK) {
      ESP_LOGE(TAG, "Could not get time.");
      return;
  }
  //ESP_LOGI("CLOCK", "\n%s\n%02d:%02d", weekday_t[rtcinfo.tm_wday], rtcinfo.tm_hour, rtcinfo.tm_min);

  // Starting coordinates:
  uint16_t y_start = display.height()/2-40;
  uint16_t x_cursor = 21;
  
  // Print temperature
  display.setFont(&Ubuntu_M36pt7b);
  display.setTextColor(EPD_LIGHTGREY);
 
  display.setCursor(x_cursor, y_start);
  display.printerf("%d   °C", (int)temp); //%.1f for float

  display.setTextColor(EPD_BLACK);
  x_cursor = 5;
  y_start = display.height()-10;
  display.setFont(&Ubuntu_L7pt8b);
  display.setCursor(x_cursor, y_start);
  // Calculate consumption with this inputs: nvs_kw_float_cost nvs_watts switch_on_sec_count

  double total_kw = (double)nvs_watts / (double)1000;
  double switch_hrs = (double)switch_on_sec_count/(double)3600;
  double total_kw_current_mon = total_kw * switch_hrs;
  double total_kw_cost = total_kw_current_mon * nvs_kw_float_cost;
  //printf("total_kw * switch_hrs %.2f * %.2f\n", total_kw ,switch_hrs);
  //printf("total_kw_current_mon:%.2f secs:%d\n\n", total_kw_current_mon, switch_on_sec_count);
  display.printerf("%.2f Kw %.2f $  %.1f°C", total_kw_current_mon, total_kw_cost, temp);
  
  // Print clock HH:MM (Seconds excluded: rtcinfo.tm_sec)
  //display.printerf("%02d:%02d %d/%02d", rtcinfo.tm_hour, rtcinfo.tm_min, rtcinfo.tm_mday, rtcinfo.tm_mon+1);
  //printf("%02d:%02d %d/%02d", rtcinfo.tm_hour, rtcinfo.tm_min, rtcinfo.tm_mday, rtcinfo.tm_mon+1);
  // Convert seconds into HHH:MM:SS
  int hr,m,s;
  hr = (switch_on_sec_count/3600); 
  m = (switch_on_sec_count -(3600*hr))/60;
  s = (switch_on_sec_count -(3600*hr)-(m*60));

  display.setCursor(display.width()/2+30, y_start);
  display.printerf("%03d:%02d:%02d ON", hr, m, s);
}

void drawUX(){
  getClock();
  uint16_t dw = display.width();
  uint16_t dh = display.height();
  uint8_t  sw = 20;
  uint8_t  sh = 50;
  uint8_t  keyw = 16;
  uint8_t  keyh = 20;
  display.drawRoundRect(dw/2-sw/2, dh/2-sh/2, sw, sh, 4, EPD_BLACK);

  // OFF position
  if (!switch_state) {
    display.fillRoundRect(dw/2-keyw/2, dh/2, keyw, keyh, 5, EPD_BLACK);
    switchState(false);
    printf("Draw OFF\n\n");
  } else {
    display.fillRoundRect(dw/2-keyw/2, dh/2-keyh, keyw, keyh, 5, EPD_BLACK);
    switchState(true);
    printf("Draw ON\n\n");
  }
  
  char * label = (switch_state) ? (char *)"ON" : (char *)"OFF";
  draw_centered_text(&Ubuntu_L7pt8b, label, dw/2-22, dh/2-sh, 40, 20);
  display.update();
  // It does not work correctly with partial update leaves last position gray
  //display.updateWindow(dw/2-40, dh/2-keyh-40, 100, 86);
}

/* Callback to handle commands received from the RainMaker cloud */
static esp_err_t write_cb(const esp_rmaker_device_t *device, const esp_rmaker_param_t *param,
            const esp_rmaker_param_val_t val, void *priv_data, esp_rmaker_write_ctx_t *ctx)
{
    esp_err_t err;
    if (ctx) {
        ESP_LOGI(TAG, "Received write request via: %s", esp_rmaker_device_cb_src_to_str(ctx->src));
    }
    const char *device_name = esp_rmaker_device_get_name(device);
    const char *param_name = esp_rmaker_param_get_name(param);

    if (strcmp(param_name, DEVICE_PARAM_POWER) == 0) {
        ESP_LOGI(TAG, "POWER %d for %s-%s", (int)val.val.i, device_name, param_name);
      if (val.val.b) {
          switch_state = true;
        } else {
          switch_state = false;
        }
        drawUX();
    /**
     * @brief ISSUE: It seems the ui.text elements are not generating a write request in the Cloud 
     *               Other widgets like sliders work like expected
     */
    } else if (strcmp(param_name, DEVICE_PARAM_CONSUMPTION) == 0) {
        ESP_LOGI(TAG, "CONSUMPTION %d for %s-%s", (int)val.val.i, device_name, param_name);
        nvs_set_u16(storage_handle, "nvs_watts", (uint16_t) val.val.i);
        nvs_watts = val.val.i;
        err = nvs_commit(storage_handle);
        printf((err != ESP_OK) ? "NVS Failed to store %d\n" : "NVS Stored %d\n", (int)val.val.i);

    } else if (strcmp(param_name, DEVICE_PARAM_KW_HOUR) == 0) {
        ESP_LOGI(TAG, "COST KW_HOUR %.2f for %s-%s", val.val.f, device_name, param_name);
        nvs_set_u16(storage_handle, "nvs_kw_cost", val.val.f * 1000); // Validate this
        nvs_kw_float_cost = val.val.f;
        err = nvs_commit(storage_handle);
        printf((err != ESP_OK) ? "NVS Failed to store %.2f\n" : "NVS Stored %.2f\n", val.val.f);

    } else if (strcmp(param_name, DEVICE_PARAM_WIFI_RESET) == 0) {
        ESP_LOGI(TAG, "WIFI_RESET %d for %s-%s",
               (int) val.val.i, device_name, param_name);
        if (val.val.i == 100) {
            printf("Reseting WiFi credentials. Please reprovision your device\n\n");
            esp_rmaker_wifi_reset(1,10);
        }

    } else {
        /* Silently ignoring invalid params */
        return ESP_OK;
    }
    esp_rmaker_param_update_and_report(param, val);
    return ESP_OK;
}

/* Event handler for catching RainMaker events */
static void event_handler_rmk(void* arg, esp_event_base_t event_base,
                          int32_t event_id, void* event_data)
{
    printf("EVENT ID:%d\n", (int)event_id);
    display.setCursor(10,10);
    display.setTextColor(EPD_BLACK);
    if (event_base == RMAKER_EVENT) {
        switch (event_id) {
            case RMAKER_EVENT_INIT_DONE:
                ESP_LOGI(TAG, "RainMaker Initialised.");
                break;
            case RMAKER_EVENT_CLAIM_STARTED:
                ESP_LOGI(TAG, "RainMaker Claim Started.");
                break;
            case RMAKER_EVENT_CLAIM_SUCCESSFUL:
                ESP_LOGI(TAG, "RainMaker Claim Successful.");
                break;
            case RMAKER_EVENT_CLAIM_FAILED:
                ESP_LOGI(TAG, "RainMaker Claim Failed.");
                break;
            default:
                ESP_LOGW(TAG, "Unhandled RainMaker Event: %d", (int)event_id);
        }
    } else if (event_base == RMAKER_COMMON_EVENT) {
        switch (event_id) {
            case RMAKER_EVENT_REBOOT:
                ESP_LOGI(TAG, "Rebooting in %d seconds.", *((uint8_t *)event_data));
                break;
            case RMAKER_EVENT_WIFI_RESET:
                ESP_LOGI(TAG, "Wi-Fi credentials reset.");
                display.setCursor(10,20);
                display.print("Wi-Fi credentials");
                display.setCursor(10,40);
                display.print("CLEARED.");
                display.update();
                break;
            case RMAKER_EVENT_FACTORY_RESET:
                ESP_LOGI(TAG, "Node reset to factory defaults.");
                break;
            case RMAKER_MQTT_EVENT_CONNECTED:
                ESP_LOGI(TAG, "MQTT Connected.");
                break;
            case RMAKER_MQTT_EVENT_DISCONNECTED:
                ESP_LOGI(TAG, "MQTT Disconnected.");
                break;
            case RMAKER_MQTT_EVENT_PUBLISHED:
                ESP_LOGI(TAG, "MQTT Published. Msg id: %d.", *((int *)event_data));
                ready_mqtt = true;
                break;
            default:
                ESP_LOGW(TAG, "Unhandled RainMaker Common Event: %d", (int)event_id);
        }
    } else {
        ESP_LOGW(TAG, "Invalid event received!");
    }
}

// Handle touch
uint16_t t_counter = 0;

void touchEvent(TPoint p, TEvent e)
{
  #if defined(DEBUG_COUNT_TOUCH) && DEBUG_COUNT_TOUCH==1
    ++t_counter;
    printf("e %x %d  ",e,t_counter); // Working
  #endif

  if (e != TEvent::Tap && e != TEvent::DragStart && e != TEvent::DragMove && e != TEvent::DragEnd)
    return;

  switch_state = !switch_state;
  drawUX();
  //printf("state:%d\n", (int)switch_state);
}

// Generic function to read NVS values and leave feedback
void err_announcer(esp_err_t err, char * name, int value) {
    switch (err) {
        case ESP_OK:
            printf("OK %s=%d\n", name, value);
            break;
        case ESP_ERR_NVS_NOT_FOUND:
            printf("The %s is not initialized yet in NVS\n", name);
            break;
        default :
            ESP_LOGE(name, "Error (%s) reading!\n", esp_err_to_name(err));
    }
}

void app_main(void)
{
  //printf("CalEPD version: %s\n", CALEPD_VERSION);
  if (nvs_save_each_secs % 60 != 0) {
    ESP_LOGE("nvs_save_each_secs", "Should be multiple of 60 and current value is %d", nvs_save_each_secs);
  }
  esp_err_t err;
    // WiFi log level
    esp_log_level_set("wifi", ESP_LOG_ERROR);
    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK( err );
    err = nvs_open("storage", NVS_READWRITE, &storage_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!\n", esp_err_to_name(err));
    } else {
        printf("NVS opened\n");
    }

    // Read values from Non Volatile Storage
    err = nvs_get_i32(storage_handle, "min_c", &min_c); // Current month
    err_announcer(err, (char *)"min_c", min_c);
    err = nvs_get_i32(storage_handle, "min_l", &min_l); // Last month (still not shown in UX)
    err_announcer(err, (char *)"min_l", min_l);
    err = nvs_get_u16(storage_handle, "nvs_watts", &nvs_watts);
    err_announcer(err, (char *)"nvs_watts", nvs_watts);
    err = nvs_get_u16(storage_handle, "nvs_kw_cost", &nvs_kw_cost);
    err_announcer(err, (char *)"nvs_kw_cost", nvs_kw_cost);
    nvs_kw_float_cost = (float)nvs_kw_cost / 1000;
    
    // Init seconds to date
    switch_on_sec_count = min_c*60;

    /* Initialize Wi-Fi. Note that, this should be called before esp_rmaker_init()
     */
    app_wifi_init();

    /* Register an event handler to catch RainMaker events */
    //ESP_ERROR_CHECK(esp_event_handler_register(RMAKER_EVENT, ESP_EVENT_ANY_ID, &event_handler_rmk, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(RMAKER_COMMON_EVENT, ESP_EVENT_ANY_ID, &event_handler_rmk, NULL));
    
    /* Initialize the ESP RainMaker Agent.
     * Note that this should be called after app_wifi_init() but before app_wifi_start()
     * */
    esp_rmaker_config_t rainmaker_cfg = {
        .enable_time_sync = false,
    };
    esp_rmaker_node_t *node = esp_rmaker_node_init(&rainmaker_cfg, "ESP RainMaker Device", "Switch");
    if (!node) {
        ESP_LOGE(TAG, "Could not initialise node. Aborting!!!");
        vTaskDelay(5000/portTICK_PERIOD_MS);
        abort();
    }

     /* Create a device and add the relevant parameters to it */
    switch_device = esp_rmaker_device_create("EPD Switch", ESP_RMAKER_DEVICE_SWITCH, NULL);
    
    esp_rmaker_device_add_cb(switch_device, write_cb, NULL);
    esp_rmaker_param_t *switch_param = esp_rmaker_power_param_create(DEVICE_PARAM_POWER, switch_state);
    esp_rmaker_device_add_param(switch_device, switch_param);
    // Assign the power parameter as the primary, so that it can be controlled from the Home screen in the App
    esp_rmaker_device_assign_primary_param(switch_device, switch_param);

    // Name of the device
    esp_rmaker_param_t *device_param = esp_rmaker_name_param_create(DEVICE_NAME, switch_name);
    esp_rmaker_device_add_param(switch_device, device_param);

    // User provided consumption of the device plus cost of kiloWatt
    esp_rmaker_param_t *consumption_param = esp_rmaker_brightness_param_create(DEVICE_PARAM_CONSUMPTION, nvs_watts); // SLIDER 0->200
    esp_rmaker_param_add_bounds(consumption_param, esp_rmaker_int(0), esp_rmaker_int(200), esp_rmaker_int(10));
    esp_rmaker_device_add_param(switch_device, consumption_param);

    esp_rmaker_param_t *kilowatt_cost_param = esp_rmaker_param_create(DEVICE_PARAM_KW_HOUR, ESP_RMAKER_PARAM_TEMPERATURE,
        esp_rmaker_float(nvs_kw_float_cost), PROP_FLAG_READ | PROP_FLAG_WRITE);
    if (kilowatt_cost_param) {
        esp_rmaker_param_add_ui_type(kilowatt_cost_param, ESP_RMAKER_UI_TEXT);
    }
    esp_rmaker_device_add_param(switch_device, kilowatt_cost_param);


    esp_rmaker_param_t *reset_wifi = esp_rmaker_brightness_param_create(DEVICE_PARAM_WIFI_RESET, 0);
    esp_rmaker_param_add_bounds(reset_wifi, esp_rmaker_int(0), esp_rmaker_int(100), esp_rmaker_int(10));
    esp_rmaker_device_add_param(switch_device, reset_wifi);

    esp_rmaker_node_add_device(node, switch_device);

   //Initialize GPIOs direction & initial states
    gpio_set_direction((gpio_num_t)GPIO_RELAY_ON, GPIO_MODE_OUTPUT);
    gpio_set_direction((gpio_num_t)GPIO_RELAY_OFF, GPIO_MODE_OUTPUT);
    switchState(false); // OFF at the beginning

    // Initialize RTC
    ds3231_initialization_status = ds3231_init_desc(&dev, I2C_NUM_0, (gpio_num_t) CONFIG_SDA_GPIO, (gpio_num_t) CONFIG_SCL_GPIO);
    if (ds3231_initialization_status != ESP_OK) {
        ESP_LOGE(pcTaskGetName(0), "Could not init DS3231 descriptor since touch already did that");
    }
    // On start clear alarm
    ds3231_clear_alarm_flags(&dev, DS3231_ALARM_1);
    ds3231_clear_alarm_flags(&dev, DS3231_ALARM_2); // 2 needs to be initially cleared too
    printf("RTC int state: %d\n\n", gpio_get_level(RTC_INT));

    // Initialize epaper class
    display.init(false);
  /**
   * @brief Note: 4 gray uses a second buffer. 
   * Nice thing about disabling is that all what you write in gray will not be visible when you are in mono mode
   */
  display.setMonoMode(true); // 4 gray: false
  display.setRotation(display_rotation);
  display.setFont(&Ubuntu_L7pt8b);
  display.setTextColor(EPD_BLACK);

    /* Enable timezone service which will be require for setting appropriate timezone
      * from the phone apps for scheduling to work correctly.
      * For more information on the various ways of setting timezone, please check
      * https://rainmaker.espressif.com/docs/time-service.html.
      */
      esp_rmaker_timezone_service_enable();

      /* Enable scheduling. */
      esp_rmaker_schedule_enable();

      /* Start the ESP RainMaker Agent */
      esp_rmaker_start();
      
      /* Uncomment to reset WiFi credentials if you want to Provision your device again */
      //esp_rmaker_wifi_reset(1,10);return;

    /* Initialize RTC interruption */  
  gpio_pullup_en(RTC_INT);
  gpio_set_direction(RTC_INT, GPIO_MODE_INPUT);
  ESP_LOGI("RTC INT", "when IO %d is Low", (int)RTC_INT);
  // Setup interrupt for this IO that goes low on the interrupt
  gpio_set_intr_type(RTC_INT, GPIO_INTR_NEGEDGE);

  // Is already used by touch!
  gpio_install_isr_service(0);
  gpio_isr_handler_add(RTC_INT, gpio_interrupt_handler, (void *)RTC_INT);
  on_min_counter_queue = xQueueCreate(10, sizeof(int));
  xTaskCreate(on_Task, "on-counter", 2048, NULL, 1, NULL);

    // Rainmaker WiFi
    err = app_wifi_start(POP_TYPE_RANDOM);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not start Wifi. Aborting!!!");
        vTaskDelay(5000/portTICK_PERIOD_MS);
        abort();
    }

  // If time is not set then resync with WiFi (Make sure to add your WLAN access point name and password in:
  // idf.py menuconfig -> Example Connection configuration
  if (ds3231_get_time(&dev, &rtcinfo) != ESP_OK) {
     ESP_LOGE(TAG, "Could not get time, please check that the I2C wiring is correct and has pull-ups in place in SCL / SDA lines.");
     return;
  } else {
     ESP_LOGI("DS3231", "get_time: %02d-%02d", rtcinfo.tm_hour, rtcinfo.tm_min);
  }
    // If this values come out of RTC time then it's not sync, most probably had a reset and no backup-power
  if (rtcinfo.tm_year == 2000 && rtcinfo.tm_mon == 0) {
    // TODO: Check how to do directly with Rainmaker: esp_rmaker_time: The current time is: Tue Feb 28 13:17:36 2023 +0100[CET]
     printf("Y:%d m:%d -> Calling NTP internet sync\n\n", rtcinfo.tm_year, rtcinfo.tm_mon);
     display.setCursor(10,10);
     display.print("RTC time & second alarm not set");
     display.setCursor(10,40);
     display.print("Connecting to do NTP time sync");
     display.update();
     setClock();
  } else {
    ESP_LOGI("DS3231", "Time looks like already set %d-%d-%d", rtcinfo.tm_year, rtcinfo.tm_mon, rtcinfo.tm_mday);
  }

   drawUX();
   
   // Instantiate touch. Important pass here the 3 required variables including display width and height
   ts.begin(FT6X36_DEFAULT_THRESHOLD, display.width(), display.height());
   ts.setRotation(display.getRotation());
   ts.registerTouchHandler(touchEvent);
  
    for (;;) {
        ts.loop();
      }
      
}
