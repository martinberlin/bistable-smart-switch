/**
 * This is a demo to be used with Good Display 2.7 touch epaper 
 */ 
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "ds3231.h"
#include "FT6X36.h"
// Non-Volatile Storage (NVS) - borrrowed from esp-idf/examples/storage/nvs_rw_value
#include "nvs_flash.h"
#include "nvs.h"
#include <time.h>
#include <sys/time.h>
// Translations: select only one
#include "english.h"
//#include "gdew027w3.h"
#include "goodisplay/gdey027T91.h"
#include "goodisplay/gdey029T94.h"
// INTGPIO is touch interrupt, goes low when it detects a touch, which coordinates are read by I2C
FT6X36 ts(CONFIG_TOUCH_INT);
EpdSpi io;
Gdey027T91 display(io);
//Gdey029T94 display(io);
// Only debugging:
//#define DEBUG_COUNT_TOUCH

// Relay Latch (high) / OFF
#define GPIO_RELAY_ON 1
#define GPIO_RELAY_OFF 3

#define RTC_INT GPIO_NUM_6
// Pulse to move the switch in millis
const uint16_t signal_ms = 50;
xQueueHandle on_min_counter_queue;

// FONT used for title / message body - Only after display library
//Converting fonts with ümlauts: ./fontconvert *.ttf 18 32 252

// Fonts are already included in components Fonts directory (Check it's CMakeFiles)
#include <Ubuntu_M8pt8b.h>
#include <Ubuntu_M36pt7b.h>

static const char *TAG = "DS3231 switch";
struct tm rtcinfo;
nvs_handle_t storage_handle;
esp_err_t ds3231_initialization_status = ESP_OK;
uint8_t display_rotation = 1;
// Switch always starts OFF when Firmware starts
bool switch_state = false; // starts false = OFF

// I2C descriptor
i2c_dev_t dev;

extern "C"
{
   void app_main();
}
void delay(uint32_t millis) { vTaskDelay(millis / portTICK_PERIOD_MS); }

static void IRAM_ATTR gpio_interrupt_handler(void *args)
{
    int pinNumber = (int)args;
    xQueueSendFromISR(on_min_counter_queue, &pinNumber, NULL);
}

void on_Task(void *params)
{
    int pinNumber, count = 1, on_count = 0;
  
    while (true)
    {
        if (xQueueReceive(on_min_counter_queue, &pinNumber, portMAX_DELAY))
        {
          if (switch_state) {
            on_count++;
          }
          printf("RTC_INT minute alarm ticked: %d mins ON (total:%d times) Switch state: %d\n", on_count, count++, switch_state);
          ds3231_clear_alarm_flags(&dev, DS3231_ALARM_2);
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
    display.fillRect(x, y, w, h, EPD_WHITE);

    display.setFont(font);
    int16_t text_x = 0;
    int16_t text_y = 0;
    uint16_t text_w = 0;
    uint16_t text_h = 0;

    display.getTextBounds(text, x, y, &text_x, &text_y, &text_w, &text_h);
    //printf("text_x:%d y:%d w:%d h:%d\n\n", text_x,text_y,text_w,text_h);
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
  display.printerf("%.1f °C", temp);

  display.setTextColor(EPD_DARKGREY);
  x_cursor = 10;
  y_start = display.height()-10;
  display.setFont(&Ubuntu_M8pt8b);
  display.setCursor(x_cursor, y_start);
  // Print clock HH:MM (Seconds excluded: rtcinfo.tm_sec)
  display.printerf("%02d:%02d %s %d/%d", rtcinfo.tm_hour, rtcinfo.tm_min, weekday_t[rtcinfo.tm_wday], rtcinfo.tm_mday, rtcinfo.tm_mon+1);

  //clockLayout(rtcinfo.tm_hour, rtcinfo.tm_min, rtcinfo.tm_sec);

  /* ESP_LOGI(pcTaskGetName(0), "%04d-%02d-%02d %02d:%02d:%02d, Week day:%d, %.2f °C", 
      rtcinfo.tm_year, rtcinfo.tm_mon + 1,
      rtcinfo.tm_mday, rtcinfo.tm_hour, rtcinfo.tm_min, rtcinfo.tm_sec, rtcinfo.tm_wday, temp); */
}

void drawUX(){
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
  draw_centered_text(&Ubuntu_M8pt8b, label, dw/2-22, dh/2-sh, 40, 20);
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

void app_main(void)
{
  printf("CalEPD version: %s\n", CALEPD_VERSION);

  gpio_pullup_en(RTC_INT);
  gpio_set_direction(RTC_INT, GPIO_MODE_INPUT);
  ESP_LOGI("RTC INT", "when IO %d is Low", (int)RTC_INT);
  // Setup interrupt for this IO
  
  gpio_set_intr_type(RTC_INT, GPIO_INTR_NEGEDGE);
  on_min_counter_queue = xQueueCreate(10, sizeof(int));
  xTaskCreate(on_Task, "on-counter", 2048, NULL, 1, NULL);

  // Is already used by touch!
  gpio_install_isr_service(0);
  gpio_isr_handler_add(RTC_INT, gpio_interrupt_handler, (void *)RTC_INT);


   //Initialize GPIOs direction & initial states
  gpio_set_direction((gpio_num_t)GPIO_RELAY_ON, GPIO_MODE_OUTPUT);
  gpio_set_direction((gpio_num_t)GPIO_RELAY_OFF, GPIO_MODE_OUTPUT);
  switchState(false); // OFF at the beginning
  
   // Initialize NVS
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      // NVS partition was truncated and needs to be erased
      // Retry nvs_flash_init
      ESP_ERROR_CHECK(nvs_flash_erase());
      err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);

  err = nvs_open("storage", NVS_READWRITE, &storage_handle);
  if (err != ESP_OK) {
      ESP_LOGE(TAG, "Error (%s) opening NVS handle!\n", esp_err_to_name(err));
  }
  // Initialize RTC
  ds3231_initialization_status = ds3231_init_desc(&dev, I2C_NUM_0, (gpio_num_t) CONFIG_SDA_GPIO, (gpio_num_t) CONFIG_SCL_GPIO);
  if (ds3231_initialization_status != ESP_OK) {
      ESP_LOGE(pcTaskGetName(0), "Could not init DS3231 descriptor since touch already did that");
  }
  // On start clear alarm
  ds3231_clear_alarm_flags(&dev, DS3231_ALARM_2);
  delay(100);
  printf("RTC int state: %d\n\n", gpio_get_level(RTC_INT));

  // Test Epd class
  display.init();
  display.setMonoMode(false); // 4 gray
  display.setRotation(display_rotation);
  //display.update();
  display.setFont(&Ubuntu_M8pt8b);
  display.setTextColor(EPD_BLACK);

  // Please find setClock in set-rtc-clock.cpp
  drawUX();
   
  // Instantiate touch. Important pass here the 3 required variables including display width and height
  printf("Touch will complain that GPIO isr service is already installed (RTC minute counter did it)\n");
   ts.begin(FT6X36_DEFAULT_THRESHOLD, display.width(), display.height());
   ts.setRotation(display.getRotation());
   ts.registerTouchHandler(touchEvent);
  
    for (;;) {
        ts.loop();
      }
      
}
