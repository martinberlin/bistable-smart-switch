/**
 * @file 6-button-actions will divide the display in 2 columns and 3 rows
 *       with named buttons the idea 
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "FT6X36.h"
#include "driver/gpio.h"

#include "goodisplay/touch/gdey027T91T.h"
// INTGPIO is touch interrupt, goes low when it detects a touch, which coordinates are read by I2C
FT6X36 ts(CONFIG_TOUCH_INT);
EpdSpi io;
Gdey027T91T display(io, ts);

bool display_dark_mode = true;
// 1||3 = Landscape  0||2 = Portrait mode. Using 0 usually the bottom is at the side of FPC connector
uint8_t display_rotation = 2;
// Default colors
uint16_t display_text_color = EPD_BLACK;
uint16_t display_back_color = EPD_WHITE;
// Fonts are already included in components Fonts directory (Check it's CMakeFiles)
#include <Ubuntu_L7pt8b.h>

extern "C"
{
   void app_main();
}
void delay(uint32_t millis) { vTaskDelay(millis / portTICK_PERIOD_MS); }

uint16_t t_counter = 0;

void touchEvent(TPoint p, TEvent e)
{
  #if defined(DEBUG_COUNT_TOUCH) && DEBUG_COUNT_TOUCH==1
    ++t_counter;
    printf("e %x %d  ",e,t_counter); // Working
  #endif

  if (e != TEvent::Tap) {
    return;
  }
    
    printf("x:%d y:%d\n", p.x , p.y);
  //draw_centered_text(&Ubuntu_L7pt8b,1,display.height()-20,display.width(),12,"x:%d y:%d", p.x , p.y);
  //display.update();
}


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
    display.fillRect(x, y, w, h, display_back_color);
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

void drawUX() {
    uint16_t x = display.width()/2;
    uint16_t y_inc = display.height()/3;
    uint16_t y = y_inc;
    display.drawLine(x, 0, x, display.height(), display_text_color);

    display.drawLine(0, y, display.width(), y, display_text_color);
    y += y_inc;
    display.drawLine(0, y, display.width(), y, display_text_color);
    y += y_inc;
    display.drawLine(0, y, display.width(), y, display_text_color);

    draw_centered_text(&Ubuntu_L7pt8b, y_inc/2 ,display.width()/2, display.width(), 20, "KINO");
}

void app_main(void)
{
  printf("CalEPD version: %s\nLast reset reason:%d\n", CALEPD_VERSION, esp_reset_reason());
  if (display_dark_mode) {
    display_text_color = EPD_WHITE;
    display_back_color = EPD_BLACK;
  }

  display.init();
  display.fillScreen(display_back_color);
  display.registerTouchHandler(touchEvent);
  display.displayRotation(display_rotation);
  display.setFont(&Ubuntu_L7pt8b);
  display.setTextColor(display_text_color);
  drawUX();
  display.update();

  // Touch loop. If youu find a smarter way to do this please make a Merge request ( github.com/martinberlin/FT6X36-IDF )
  while (true) {
    display.touchLoop();
  }
}