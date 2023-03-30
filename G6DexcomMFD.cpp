#include "G6DexcomMFD.h"
#include "fonts\FreeSans9pt7b.h"
#include "fonts\FreeSans12pt7b.h"
#include "fonts\FreeSans18pt7b.h"
#include "fonts\FreeSans24pt7b.h"

Adafruit_ST7789 DexcomMFD::tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
int DexcomMFD::hiHighLimit = 180;
int DexcomMFD::highLimit = 140;
int DexcomMFD::lowLimit = 90;
int DexcomMFD::loLowLimit = 60;
int DexcomMFD::glucoseDisplay = 0;
int DexcomMFD::rateDisplay = +7;
int DexcomMFD::battDisplay = 72;

void DexcomMFD::setupTFT()
{
    // turn on backlight
    pinMode(TFT_BACKLITE, OUTPUT);
    digitalWrite(TFT_BACKLITE, HIGH);

    // turn on the TFT / I2C power supply
    pinMode(TFT_I2C_POWER, OUTPUT);
    digitalWrite(TFT_I2C_POWER, HIGH);
    delay(10);

    // initialize TFT
    tft.init(135, 240); // Init ST7789 240x135
    tft.setRotation(0);
    tft.fillScreen(ST77XX_BLACK);

    Serial.println(F("Initialized"));

    set_hiHighBG(180);
    set_highBG(150);
    set_lowBG(80);
    set_loLowBG(65);
}

void DexcomMFD::drawScreen()
{
    const static int gluX = 20;
    const static int gluY = 60;
    const static int gluWd = 22;
    const static int gluTpWd = 14;
    const static int gluTpOfSt = gluWd - gluTpWd;
    const static int gluHt = 160;
    const static int gluMax = 200;
    const static int gluMin = 50;
    const static int gluPPP_100 = ( gluHt * 100 ) / (gluMax - gluMin); //glucose Pixels per Point x100
    const static int gluYMx = gluY + 2;
    const static int gluYHH = gluYMx + ((gluPPP_100 * (gluMax - hiHighLimit)) / 100);
    const static int gluYH = gluYMx + ((gluPPP_100 * (gluMax - highLimit)) / 100);
    const static int gluYL = gluYMx + ((gluPPP_100 * (gluMax - lowLimit)) / 100);
    const static int gluYLL = gluYMx + ((gluPPP_100 * (gluMax - loLowLimit)) / 100);
    const static int gluYMn = gluYMx + gluHt;
    int glucoseY = gluYMx + ((gluPPP_100 * (gluMax - glucoseDisplay)) / 100);
    if (glucoseY > gluYMn) glucoseY = gluYMn;
    if (glucoseY < gluYMx) glucoseY = gluYMx;
    tft.fillScreen(ST77XX_BLACK);
    tft.drawFastHLine(gluX, gluY, gluWd, ST77XX_WHITE);
    tft.drawFastHLine(gluX, gluY + 1, gluWd, ST77XX_WHITE);
    tft.drawFastHLine(gluX, gluY + gluHt + 2, gluWd, ST77XX_WHITE);
    tft.drawFastHLine(gluX, gluY + gluHt + 3, gluWd, ST77XX_WHITE);
    tft.drawFastHLine(gluX + gluWd, gluYH, 5, ST77XX_WHITE);
    tft.drawFastHLine(gluX + gluWd, gluYH + 1, 5, ST77XX_WHITE);
    tft.drawFastHLine(gluX + gluWd, gluYL, 5, ST77XX_WHITE);
    tft.drawFastHLine(gluX + gluWd, gluYL + 1, 5, ST77XX_WHITE);
    tft.drawFastVLine(gluX + gluWd, gluY, gluHt + 4, ST77XX_WHITE);
    tft.drawFastVLine(gluX + gluWd + 1, gluY, gluHt + 4, ST77XX_WHITE);
    tft.fillRect(gluX + gluTpOfSt, gluYMx, gluTpWd, gluYHH - gluYMx, GARMIN_RED_16); //High-High warning bar
    tft.fillRect(gluX + gluTpOfSt, gluYLL, gluTpWd, gluYMn - gluYLL, GARMIN_RED_16); //Low-Low Warning bar
    tft.fillRect(gluX + gluTpOfSt, gluYHH, gluTpWd, gluYH - gluYHH, GARMIN_YELLOW_16); //high caution bar
    tft.fillRect(gluX + gluTpOfSt, gluYL, gluTpWd, gluYLL - gluYL, GARMIN_YELLOW_16); //low caution bar
    tft.fillRect(gluX + gluTpOfSt, gluYH, gluTpWd, gluYL - gluYH, GARMIN_GREEN_16); //green control band
    tft.setTextColor(ST77XX_WHITE);
    tft.setFont(&FreeSans9pt7b);
    // tft.setCursor(gluX - 15, 12);
    tft.setCursor(gluX + gluTpOfSt - txtCenter("CGM"), 12);
    tft.println("CGM");
    tft.setCursor(gluX + gluWd + 8, gluYH + 5);
    tft.println(highLimit);
    tft.setCursor(gluX + gluWd + 8, gluYL + 5);
    tft.println(lowLimit);
    tft.setFont(&FreeSans18pt7b);
    // tft.setCursor(gluX - 20, gluY - 8);
    if (glucoseDisplay > 10)
    {
        tft.setCursor(gluX + gluTpOfSt - txtCenter(String(glucoseDisplay)), gluY - 8);
        tft.println(glucoseDisplay);
        tft.fillTriangle(gluX, glucoseY - (gluTpWd / 2), gluX + gluWd, glucoseY, gluX, glucoseY + (gluWd / 2), ST77XX_WHITE);
        tft.drawTriangle(gluX, glucoseY - (gluTpWd / 2), gluX + gluWd, glucoseY, gluX, glucoseY + (gluWd / 2), ST77XX_BLACK);
    }
    else
    {
        tft.setCursor(gluX + gluTpOfSt - txtCenter("---"), gluY - 8);
        tft.println("---");
    }


}

void DexcomMFD::set_glucoseValue(int bg_value)
{
    glucoseDisplay = bg_value;
}

void DexcomMFD::set_glucoseRate(int bg_rate)
{
    rateDisplay = bg_rate;
}

void DexcomMFD::set_battPct(int batt_pct)
{
    battDisplay = batt_pct;
}

void DexcomMFD::set_runtime(int runtime_secs)
{
    runtime = runtime_secs;
}

void DexcomMFD::set_dataAge(int dataAge_secs)
{
    dataAge = dataAge_secs;
}

void DexcomMFD::set_hiHighBG(int limit)
{
    if ( limit > highLimit) hiHighLimit = limit;
}

void DexcomMFD::set_highBG(int limit)
{
    if ( limit > lowLimit && limit < hiHighLimit) highLimit = limit;
}

void DexcomMFD::set_lowBG(int limit)
{
    if ( limit < highLimit && limit > loLowLimit) lowLimit = limit;
}

void DexcomMFD::set_loLowBG(int limit)
{
    if ( limit < lowLimit) loLowLimit = limit;
}

void DexcomMFD::set_highRate(int limit)
{
}

void DexcomMFD::set_lowRate(int limit)
{
}

void DexcomMFD::set_lowBatt(int limit)
{
}

void DexcomMFD::set_loLowBatt(int limit)
{
}

//returns the center offset for the given string
int DexcomMFD::txtCenter(const String &str)
{
    int16_t  x1, y1;
    uint16_t w, h;

    tft.getTextBounds(str, 20, 20, &x1, &y1, &w, &h);
    return w / 2;
}


