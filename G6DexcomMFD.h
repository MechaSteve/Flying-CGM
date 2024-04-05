/**
 * Header File with functions to render data in an avionics style
 * Rendering is specific to the Adafruit ST77xx TFT display
 *  *
 *
 * Author: Stephen Culpepper
 * 2023.03.28
 */

#ifndef G6DEXCOMMFD_H
#define G6DEXCOMMFD_H

#include "Arduino.h"
#include "U8g2lib.h"
#include "Arduino_GFX_Library.h"    // Core graphics library
#include "pin_config.h"

#define GARMIN_GREEN_16 (15 << 11) + (49 << 5) + 11
#define GARMIN_YELLOW_16 (31 << 11) + (55 << 5) + 6
#define GARMIN_RED_16 (30 << 11) + (17 << 5) + 5

// Color definitions
//#define GREEN 0x07E0       ///<   0, 255,   0
#define GREEN_1 0x0700       ///<   0, 224,   0
#define GREEN_2 0x0600       ///<   0, 192,   0
#define GREEN_3 0x0540       ///<   0, 168,   0
//#define YELLOW 0xFFE0      ///< 255, 255,   0
#define YELLOW_1 0xE700       ///< 224, 224,   0
#define YELLOW_2 0xC600       ///< 192, 192,   0
#define YELLOW_3 0xAD40       ///< 168, 168,   0
//#define RED 0xF800         ///< 255,   0,   0
#define RED_1 0xE800       ///< 224,   0,   0
#define RED_2 0xD000       ///< 192,   0,   0
#define RED_3 0xC000       ///< 168,   0,   0

class DexcomMFD {
    static Arduino_ST7789 tft;
    static int glucoseDisplay;
    static int rateDisplay;
    static int battDisplay;
    static int runtime;
    static int dataAge;
    static int hiHighLimit;
    static int highLimit;
    static int lowLimit;
    static int loLowLimit;
    static int highRateLimit;
    static int lowRateLimit;
    static int lowBattLimit;
    static int loLowBattLimit;

    public:
        static void setupTFT();
        static void drawGrid();
        static void drawScreen();
        static void drawTime(uint32_t time);
        static void drawVBat(int mVolts);
        static void drawPBat(int pct);
        static void set_glucoseValue(int bg_value);
        static void set_glucoseRate(int bg_rate);
        static void set_battPct(int batt_pct);
        static void set_runtime(int runtime_secs);
        static void set_dataAge(int dataAge_secs);
        static void set_hiHighBG(int limit);
        static void set_highBG(int limit);
        static void set_lowBG(int limit);
        static void set_highRate(int limit);
        static void set_lowRate(int limit);
        static void set_loLowBG(int limit);
        static void set_lowBatt(int limit);
        static void set_loLowBatt(int limit);

    private:
        static int txtCenter(const String &str);
        static void pfdColorVTape( uint16_t x, uint16_t y1, uint16_t y2, uint16_t w, uint16_t color);
};

/** generic draw indicator data struct
 *
 * this contains basic position, size, and scale information for the indicator
 *
 * x, y  far left and top coordinates of graph
 * ht total height of the space INSIDE the bracket
 * wd total width of the top and bottom of the bracket
 * tpWd width of the color tape inside the bracket
 * max : maximum value of the graph
 * min : minimum value of the graph
 *
 **/
typedef struct {
  unsigned int x, y;
  unsigned int height, width, tapeWidth;
  int max, min;
} DexcomIndicator;




#endif //G6DEXCOMMFD_H
