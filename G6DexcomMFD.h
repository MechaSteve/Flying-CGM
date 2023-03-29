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

#include <Adafruit_GFX.h>    // Core graphics library
#include <Adafruit_ST7789.h> // Hardware-specific library for ST7789
#include <SPI.h>

#define GARMIN_GREEN_16 (15 << 11) + (49 << 5) + 11
#define GARMIN_YELLOW_16 (31 << 11) + (55 << 5) + 6
#define GARMIN_RED_16 (30 << 11) + (17 << 5) + 5

class DexcomMFD {
    static Adafruit_ST7789 tft;
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
        static void drawScreen();
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
};




#endif //G6DEXCOMMFD_H
