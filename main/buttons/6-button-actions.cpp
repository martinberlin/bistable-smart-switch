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
uint8_t display_rotation = 0;
// Default colors
uint16_t display_text_color = EPD_BLACK;
uint16_t display_back_color = EPD_WHITE;

char * BUTTON1 = (char *)"KINO";
char * BUTTON2 = (char *)"FUN";
char * BUTTON3 = (char *)"SEX";
char * BUTTON4 = (char *)"BLINDS";
char * BUTTON5 = (char *)"VENT";
char * BUTTON6 = (char *)"PLAY";
uint8_t last_button = 0;
// Fonts are already included in components Fonts directory (Check it's CMakeFiles)
#include <Ubuntu_M8pt8b.h>

extern "C"
{
   void app_main();
}
void delay(uint32_t millis) { vTaskDelay(millis / portTICK_PERIOD_MS); }

uint16_t t_counter = 0;
const uint16_t x_mid = display.width()/2;
const uint16_t y_inc = display.height()/3;
const uint8_t font_height = 10;

void draw_centered_text(const GFXfont *font, int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t back_color, const char* format, ...) {
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
    display.fillRect(x, y, w, h, back_color);
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

uint16_t color = EPD_WHITE;
uint16_t textcolor = EPD_BLACK;

void drawButton(bool pressed=false, uint8_t number=0) {

    switch (number)
    {
    case 1:
        color = (pressed) ? EPD_WHITE : EPD_BLACK;
        textcolor = (pressed) ? EPD_BLACK : EPD_WHITE;
        display.fillRect(1,1,x_mid-1,y_inc,color);
        display.setTextColor(textcolor);
        draw_centered_text(&Ubuntu_M8pt8b, 1, y_inc/2-font_height, x_mid, font_height*2, color, BUTTON1);
        if (pressed) display.updateWindow(1,1,x_mid-1,y_inc,color);
      break;
    case 2:
        color = (pressed) ? EPD_WHITE : EPD_BLACK;
        textcolor = (pressed) ? EPD_BLACK : EPD_WHITE;
        display.fillRect(x_mid,1,x_mid,y_inc,color);
        display.setTextColor(textcolor);
        draw_centered_text(&Ubuntu_M8pt8b, x_mid, y_inc/2-font_height, x_mid, font_height*2, color, BUTTON2);
        if (pressed) display.updateWindow(x_mid,1,x_mid,y_inc,color);
      break;
    case 3:
        color = (pressed) ? EPD_WHITE : EPD_BLACK;
        textcolor = (pressed) ? EPD_BLACK : EPD_WHITE;
        display.fillRect(1,y_inc,x_mid,y_inc,color);
        display.setTextColor(textcolor);
        draw_centered_text(&Ubuntu_M8pt8b, 1, y_inc+(y_inc/2)-font_height, x_mid, font_height*2, color, BUTTON3);
        if (pressed) display.updateWindow(1,y_inc,x_mid,y_inc,color);
      break;
    case 4:
        color = (pressed) ? EPD_WHITE : EPD_BLACK;
        textcolor = (pressed) ? EPD_BLACK : EPD_WHITE;
        display.fillRect(x_mid,y_inc,x_mid,y_inc,color);
        display.setTextColor(textcolor);
        draw_centered_text(&Ubuntu_M8pt8b, x_mid, y_inc+(y_inc/2)-font_height, x_mid, font_height*2, color, BUTTON4);
        if (pressed) display.updateWindow(x_mid,y_inc,x_mid,y_inc,color);
      break;
    case 5:
        color = (pressed) ? EPD_WHITE : EPD_BLACK;
        textcolor = (pressed) ? EPD_BLACK : EPD_WHITE;
        display.fillRect(1,y_inc*2,x_mid,y_inc,color);
        display.setTextColor(textcolor);
        draw_centered_text(&Ubuntu_M8pt8b, 1, (y_inc*2)+(y_inc/2)-font_height, x_mid, font_height*2, color, BUTTON5);
        if (pressed) display.updateWindow(1,y_inc*2,x_mid,y_inc,color);
      break;
    case 6:
        color = (pressed) ? EPD_WHITE : EPD_BLACK;
        textcolor = (pressed) ? EPD_BLACK : EPD_WHITE;
        display.fillRect(x_mid,y_inc*2,x_mid,y_inc,color);
        display.setTextColor(textcolor);
        draw_centered_text(&Ubuntu_M8pt8b, x_mid, (y_inc*2)+(y_inc/2)-font_height, x_mid, font_height*2, color, BUTTON6);
        if (pressed) display.updateWindow(x_mid,y_inc*2,x_mid,y_inc,color);
      break;

    default:
      ESP_LOGE("drawButton", "NUMBER %d not implemented as a button", number);
      break;
    }


}

void drawUX() {
    //printf("-->last button:%d\n", last_button);
    
    drawButton(true, last_button);
    // Update other buttons skipping the pressed one
    for (uint8_t number = 1; (number < 7); number++) {
      if (number != last_button) {
        drawButton(false, number);
      } else {
        printf(">Skip btn %d\n\n", number);
      }
    }

    uint16_t y = y_inc;
    display.drawLine(x_mid, 0, x_mid, display.height(), display_text_color);
    display.drawLine(0, y, display.width(), y, display_text_color);
    y += y_inc;
    display.drawLine(0, y, display.width(), y, display_text_color);
    y += y_inc;
    display.drawLine(0, y, display.width(), y, display_text_color);
    display.update();
}

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
  // 1st column presses
  if (p.x>=1 && p.x<=display.width()/2) {
    // Top left corner Button 1
    if (p.y>=1 && p.y<=y_inc) {
      drawButton(true, 1);
      last_button = 1;
      delay(50);
      drawUX();
    } else if (p.y>=y_inc && p.y<=y_inc*2) {
      drawButton(true, 3);
      last_button = 3;
      delay(50);
      drawUX();
    } else if (p.y>=y_inc*2 && p.y<=y_inc*3) {
      drawButton(true, 5);
      last_button = 5;
      delay(50);
      drawUX();
    }

    // 2nd column
  } else {
    // Button 2
    if (p.y>=1 && p.y<=y_inc) {
      drawButton(true, 2);
      last_button = 2;
      delay(50);
      drawUX();
    } else if (p.y>=y_inc && p.y<=y_inc*2) {
      drawButton(true, 4);
      last_button = 4;
      delay(50);
      drawUX();
    } else if (p.y>=y_inc*2 && p.y<=y_inc*3) {
      drawButton(true, 6);
      last_button = 6;
      delay(50);
      drawUX();
    }
  }
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
  display.setFont(&Ubuntu_M8pt8b);
  display.setTextColor(display_text_color);
  drawUX();

  // Touch loop. If youu find a smarter way to do this please make a Merge request ( github.com/martinberlin/FT6X36-IDF )
  while (true) {
    display.touchLoop();
  }
}