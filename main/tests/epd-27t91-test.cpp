#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
// Minimal EPD instantiation example with CalEPD
#include "goodisplay/gdey027T91.h"
#include "goodisplay/gdey029T94.h"

EpdSpi io;
Gdey027T91 display(io);
//Gdey029T94 display(io);

extern "C"
{
   void app_main();
}
void delay(uint32_t millis) { vTaskDelay(millis / portTICK_PERIOD_MS); }

void app_main() {
    display.init();
    display.setMonoMode(false); // 4 gray mode
    int adv_y = display.height()/5+7;
    int y = adv_y;
    int x = display.width()/2;
    int radius = 30;
    printf("x:%d y:%d  circle 1\n", x, y);
    display.fillCircle(x, y, radius, EPD_BLACK);

    y += adv_y;
    printf("x:%d y:%d circle 2\n", x, y);
    display.fillCircle(x, y, radius, EPD_DARKGREY);

    y += adv_y;
    printf("x:%d y:%d circle 3\n", x, y);
    display.fillCircle(x, y, radius, EPD_LIGHTGREY);

    y += adv_y;
    display.drawCircle(x, y, radius, EPD_BLACK);
    display.update();
}