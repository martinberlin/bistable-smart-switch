/**
 * This is a demo to be used with Good Display 2.7 touch epaper 
 */ 
#include <stdio.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
// I2C RTC
#include "pcf8563.h"
// I2C Touch
#include "FT6X36.h"
// Non-Volatile Storage (NVS)
#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"
// Time & WiFi
#include <time.h>
#include <sys/time.h>
#include "esp_wifi.h"
#include "protocol_examples_common.h"
#include "esp_sntp.h"
// Translations: select only one
#include "english.h"
//#include "gdew027w3.h"
#include "goodisplay/gdey027T91.h"
#include "goodisplay/gdey029T94.h"

// INTGPIO is touch interrupt, goes low when it detects a touch, which coordinates are read by I2C
// #define DEBUG_COUNT_TOUCH   // Only debugging
FT6X36 ts(CONFIG_TOUCH_INT);
EpdSpi io;
Gdey027T91 display(io);

// 1||3 = Landscape  0||2 = Portrait mode. Using 0 usually the bottom is at the side of FPC connector
uint8_t display_rotation = 2;
// Mono mode = true for faster monochrome update. 4 grays is nice but slower
bool display_mono_mode = true;


/** DEVICE CONSUMTION CONFIG 
    Please edit this two values to make the switch aware of how much consumes the lamp you are controlling */
#define DEVICE_CONSUMPTION_WATTS 100
double  DEVICE_KW_HOUR_COST    = 0.4;  // € or $ in device appears only $ since there is an issue printing Euro sign


// Relay Latch (high) / OFF
#define GPIO_RELAY_ON 1
#define GPIO_RELAY_OFF 3
// GPIO_NUM_2 in C3
#define RTC_INT GPIO_NUM_2
// Pulse to move the switch in millis
const uint16_t signal_ms = 50;

// Each time the counter hits this amount, store seconds counter in NVS and commit
// Make this please multiple of 60 or you can get an inexact count (Will also show Red alert in Serial)
const uint16_t nvs_save_each_secs = 240;

xQueueHandle on_min_counter_queue;

// FONT used for title / message body - Only after display library
//Converting fonts with ümlauts: ./fontconvert *.ttf 18 32 252

// Fonts are already included in components Fonts directory (Check it's CMakeFiles)
#include <Ubuntu_L7pt8b.h>
#include <Ubuntu_M16pt8b.h>
#include <Ubuntu_M36pt7b.h>

static const char *TAG = "PCF switch";
struct tm rtcinfo;
nvs_handle_t storage_handle;
// Every minute the current_month count is updated
int32_t min_l = 0;
int32_t min_c = 0;

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

static void initialize_sntp(void)
{
    ESP_LOGI(TAG, "Initializing SNTP");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    ESP_LOGI(TAG, "Your NTP Server is %s", CONFIG_NTP_SERVER);
    sntp_setservername(0, CONFIG_NTP_SERVER);
    sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    sntp_init();
}

static bool obtain_time(void)
{
    ESP_ERROR_CHECK( esp_netif_init() );
    ESP_ERROR_CHECK( esp_event_loop_create_default() );

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    ESP_ERROR_CHECK(example_connect());

    initialize_sntp();

    // wait for time to be set
    int retry = 0;
    const int retry_count = 10;
    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < retry_count) {
        ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }

    ESP_ERROR_CHECK( example_disconnect() );
    if (retry == retry_count) return false;
    return true;
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
                if (pcf8563_get_time(&dev, &rtcinfo) == ESP_OK) {
                    // Each day 1 at 00:00 HRs reset secs counter and save last month in min_l
                    ESP_LOGI("PCF8563", "Attempt RST counter|mday:%d hr:%d min:%d", rtcinfo.tm_mday,rtcinfo.tm_hour, rtcinfo.tm_min);
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
          // Clear timer flags
          pcf8563_get_flags(&dev);

          printf("ON:%d sec,%d min|pow:%s\n",
          switch_on_sec_count, (int)min_c, (switch_state)?(char*)"ON":(char*)"OFF");
        }
    }
}

// Some GFX constants
uint16_t blockHeight = display.height()/4;
uint16_t lineSpacing = 18;
uint16_t circleColor = EPD_BLACK;
uint16_t circleRadio = 10;
uint16_t selectTextColor  = EPD_WHITE;
uint16_t selectBackground = EPD_BLACK;

void draw_centered_text(const GFXfont *font, int16_t x, int16_t y, uint16_t w, uint16_t h, const char* format, ...) {
    // Handle printf arguments
    va_list args;
    va_start(args, format);
    char max_buffer[1024];
    int size = vsnprintf(max_buffer, sizeof max_buffer, format, args);
    va_end(args);
    string text = "";
    if (size < sizeof(max_buffer)) {
      text = string(max_buffer);
      
    } else {
      ESP_LOGE("draw_centered_text", "max_buffer out of range. Increase max_buffer!");
    }
    // Draw external boundary where text needs to be centered in the middle
    //printf("drawRect x:%d y:%d w:%d h:%d\n\n", x, y, w, h);
    display.fillRect(x, y, w, h, EPD_WHITE);
    display.setFont(font);
    int16_t text_x = 0;
    int16_t text_y = 0;
    uint16_t text_w = 0;
    uint16_t text_h = 0;

    display.getTextBounds(text.c_str(), x, y, &text_x, &text_y, &text_w, &text_h);

    if (text_w > w) {
        ESP_LOGI("draw_centered_text", "W: Text width out of bounds");
    }
    if (text_h > h) {
        ESP_LOGI("draw_centered_text", "W: Text height out of bounds");
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

void setClock()
{
    // obtain time over NTP
    ESP_LOGI(pcTaskGetName(0), "Connecting to WiFi and getting time over NTP.");
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

    // RTC is already initialized at this point

    ESP_LOGD(pcTaskGetName(0), "timeinfo.tm_sec=%d",timeinfo.tm_sec);
    ESP_LOGD(pcTaskGetName(0), "timeinfo.tm_min=%d",timeinfo.tm_min);
    ESP_LOGD(pcTaskGetName(0), "timeinfo.tm_hour=%d",timeinfo.tm_hour);
    ESP_LOGD(pcTaskGetName(0), "timeinfo.tm_wday=%d",timeinfo.tm_wday);
    ESP_LOGD(pcTaskGetName(0), "timeinfo.tm_mday=%d",timeinfo.tm_mday);
    ESP_LOGD(pcTaskGetName(0), "timeinfo.tm_mon=%d",timeinfo.tm_mon);
    ESP_LOGD(pcTaskGetName(0), "timeinfo.tm_year=%d",timeinfo.tm_year);

    struct tm time = {
        .tm_sec  = timeinfo.tm_sec,
        .tm_min  = timeinfo.tm_min,
        .tm_hour = timeinfo.tm_hour,
        .tm_mday = timeinfo.tm_mday,
        .tm_mon  = timeinfo.tm_mon,  // 0-based
        .tm_year = timeinfo.tm_year + 1900,
        .tm_wday = timeinfo.tm_wday
    };

    if (pcf8563_set_time(&dev, &time) != ESP_OK) {
        ESP_LOGE(pcTaskGetName(0), "pcf8563 Could not set time.");
        while (1) { vTaskDelay(1); }
    }
    ESP_LOGI(pcTaskGetName(0), "Set initial date time done");
}

void getClock() {
  display.fillScreen(EPD_WHITE); // Clean display
  // I2CDEV: Could not read from device in the 2x time. TODO: Find why
  // Get RTC date and time
  if (pcf8563_get_time(&dev, &rtcinfo) != ESP_OK) {
      ESP_LOGE(TAG, "getClock: Could not get time. Error coming second time on read attempt. Not fixed yet");
      return;
  }

  ESP_LOGI("CLOCK", "\n%s\n%02d:%02d", weekday_t[rtcinfo.tm_wday], rtcinfo.tm_hour, rtcinfo.tm_min);

  // Starting coordinates:
  uint16_t y_start = display.height()-10;
  uint16_t x_cursor = 1;
  // For portrait mode update this coordinates
  if (display_rotation == 0 || display_rotation == 2) {
    y_start = 5;
  }
  
  // Calculate consumption with this inputs: DEVICE_KW_HOUR_COST nvs_watts switch_on_sec_count
  double total_kw = (double)DEVICE_CONSUMPTION_WATTS / (double)1000;
  double switch_hrs = (double)switch_on_sec_count/(double)3600;
  double total_kw_current_mon = total_kw * switch_hrs;
  double total_kw_cost = total_kw_current_mon * DEVICE_KW_HOUR_COST;

  // Temp excluded:  %.1f°C
  //draw_centered_text(const GFXfont *font, int16_t x, int16_t y, uint16_t w, uint16_t h, const char* format, ...)
  draw_centered_text(&Ubuntu_L7pt8b,x_cursor,y_start,display.width(),12,"%.2f Kw %.2f $", total_kw_current_mon, total_kw_cost);
  
  // Convert seconds into HHH:MM:SS
  int hr,m,s;
  hr = (switch_on_sec_count/3600);
  m = (switch_on_sec_count -(3600*hr))/60;
  s = (switch_on_sec_count -(3600*hr)-(m*60));

  x_cursor = display.width()/2+30;
  if (display_rotation == 0 || display_rotation == 2) {
    y_start = display.height()-20;
    x_cursor = 1;
  }

  draw_centered_text(&Ubuntu_L7pt8b,x_cursor,y_start,display.width(),12,"%03d:%02d:%02d ON", hr, m, s);
}

void drawUX() {
  getClock();
  uint16_t dw = display.width();
  uint16_t dh = display.height();
  uint8_t  sw = 20;
  uint8_t  sh = 50;
  uint8_t  keyw = 16;
  uint8_t  keyh = 16;
  display.fillRoundRect(dw/2-sw/2, dh/2-sh/2, sw, sh, 4, EPD_WHITE);
  display.drawRoundRect(dw/2-sw/2, dh/2-sh/2, sw, sh, 4, EPD_BLACK);

  // OFF position
  if (!switch_state) {
    display.fillRoundRect(dw/2-keyw/2, dh/2, keyw, keyh, 5, EPD_BLACK);
    gpio_set_level((gpio_num_t)GPIO_RELAY_OFF, 1); // OFF
    delay(signal_ms);
    gpio_set_level((gpio_num_t)GPIO_RELAY_OFF, 0); // OFF release
    printf("Draw OFF\n\n");
  } else {
    display.fillRoundRect(dw/2-keyw/2, dh/2-keyh, keyw, keyh, 5, EPD_BLACK);
    gpio_set_level((gpio_num_t)GPIO_RELAY_ON, 1); // ON
    delay(signal_ms);
    gpio_set_level((gpio_num_t)GPIO_RELAY_ON, 0); // ON release
    printf("Draw ON\n\n");
  }
  
  char * label = (switch_state) ? (char *)"ON" : (char *)"OFF";
  draw_centered_text(&Ubuntu_L7pt8b, dw/2-22, dh/2-sh, 40, 20, label);

  display.update();
  // It does not work correctly with partial update leaves last position gray
  //display.updateWindow(dw/2-40, dh/2-keyh-40, 100, 86);
}

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
  printf("CalEPD version: %s\nLast reset reason:%d\n", CALEPD_VERSION, esp_reset_reason());

  // Initialize NVS
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      // NVS partition was truncated and needs to be erased
      // Retry nvs_flash_init
      ESP_ERROR_CHECK(nvs_flash_erase());
      err = nvs_flash_init();
  } else {
    printf("NVS initialized\n");
  }
  ESP_ERROR_CHECK(err);

  err = nvs_open("storage", NVS_READWRITE, &storage_handle);
  if (err != ESP_OK) {
      ESP_LOGE(TAG, "Error (%s) opening NVS handle!\n", esp_err_to_name(err));
  } else {
    printf("NVS opened\n");
  }

  gpio_pullup_en(RTC_INT);
  gpio_set_direction(RTC_INT, GPIO_MODE_INPUT);
  ESP_LOGI("RTC INT", "when IO %d is Low", (int)RTC_INT);
  // Setup interrupt for this IO that goes low on the interrupt
  gpio_set_intr_type(RTC_INT, GPIO_INTR_NEGEDGE);


  //Initialize GPIOs direction & initial states
  gpio_set_direction((gpio_num_t)GPIO_RELAY_ON, GPIO_MODE_OUTPUT);
  gpio_set_direction((gpio_num_t)GPIO_RELAY_OFF, GPIO_MODE_OUTPUT);
  switchState(false); // OFF at the beginning
  
  // Read values from Non Volatile Storage
  err = nvs_get_i32(storage_handle, "min_c", &min_c); // Current month
  err_announcer(err, (char *)"min_c", min_c);
  err = nvs_get_i32(storage_handle, "min_l", &min_l); // Last month (still not shown in UX)
  err_announcer(err, (char *)"min_l", min_l);
  // Initialize ON count with NVS count
  switch_on_sec_count = min_c*60;
  // Initialize Epd class, set rotation, default Font and orientation
  display.init();
  /**
   * @brief Note: 4 gray uses a second buffer. 
   * Nice thing about disabling is that all what you write in gray will not be visible when you are in mono mode
   */
  display.setMonoMode(display_mono_mode); // 4 gray: false
  display.setRotation(display_rotation);
  display.setFont(&Ubuntu_L7pt8b);
  display.setTextColor(EPD_BLACK);

// Initialize RTC
  if (pcf8563_init_desc(&dev, I2C_NUM_0, (gpio_num_t) CONFIG_SDA_GPIO, (gpio_num_t)CONFIG_SCL_GPIO) != ESP_OK) {
      ESP_LOGE(pcTaskGetName(0), "Could not init PCF8563 descriptor since touch already did that");
  }
  
  // If time is not set then resync with WiFi (Make sure to add your WLAN access point name and password in:
  // idf.py menuconfig -> Example Connection configuration
  
  if (pcf8563_get_time(&dev, &rtcinfo) != ESP_OK) {
     ESP_LOGE(TAG, "Could not get time, please check that the I2C wiring is correct and has pull-ups in place in SCL / SDA lines.");
     return;
  }
  printf("pcf8563 TIME: %02d:%02d %02d/%02d/%d\n\n", rtcinfo.tm_hour, rtcinfo.tm_min,
                                  rtcinfo.tm_mday, rtcinfo.tm_mon+1, rtcinfo.tm_year);
  
  // If this values are matched then RTC clock needs to get time from NTP
  if (rtcinfo.tm_year == 2151) {
     printf("Y:%d m:%d -> Calling NTP internet sync\n\n", rtcinfo.tm_year, rtcinfo.tm_mon);
     display.setCursor(10,10);
     display.print("RTC time & second alarm not set");
     display.setCursor(10,40);
     display.print("Connecting to do NTP time sync");
     display.update();
     setClock();
  }
  
  printf("bef settimer RTC int state: %d (Should be 1 at start)\n\n", gpio_get_level(RTC_INT));
  // Every second         1 sec= 1 HZ , divider (If it would be two then will tick 2x per second)
  pcf8563_set_timer(&dev, PCF8563_CLK_1HZ, 1);
  pcf8563_enable_timer(&dev);
  pcf8563_get_flags(&dev);
  printf("after RTC int state: %d (Should be 1 at start)\n\n", gpio_get_level(RTC_INT));


  // PCF Minute alarm: Still need to find out how to correctly set it and extend my class
  gpio_install_isr_service(0); // Is already used by touch!
  // Reason is that RTC is in control of this Interrupt when PCF starts
  gpio_isr_handler_add(RTC_INT, gpio_interrupt_handler, (void *)RTC_INT);
  on_min_counter_queue = xQueueCreate(10, sizeof(int));
  xTaskCreate(on_Task, "on-counter", 2048, NULL, 1, NULL);

  ts.begin(FT6X36_DEFAULT_THRESHOLD, display.width(), display.height());
  ts.setRotation(display.getRotation());
  ts.registerTouchHandler(touchEvent);
  
  drawUX();
  // Touch loop. If youu find a smarter way to do this please make a Merge request ( github.com/martinberlin/FT6X36-IDF )
  while (true) {
    ts.loop();
  }
}
