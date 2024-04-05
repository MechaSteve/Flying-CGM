#include "G6DexcomMFD.h"
// TODO: figure out the correct fonts to include

// TODO: replace this with the correct library and class for the T-Display
Arduino_DataBus *bus = new Arduino_ESP32LCD8(7 /* DC */, 6 /* CS */, 8 /* WR */, 9 /* RD */, 39 /* D0 */, 40 /* D1 */, 41 /* D2 */, 42 /* D3 */,
                                             45 /* D4 */, 46 /* D5 */, 47 /* D6 */, 48 /* D7 */);
Arduino_ST7789 DexcomMFD::tft = Arduino_ST7789(bus, 5 /* RST */, 0 /* rotation */, true /* IPS */, 170 /* width */, 320 /* height */, 35 /* col offset 1 */,
                                      0 /* row offset 1 */, 35 /* col offset 2 */, 0 /* row offset 2 */);
int DexcomMFD::hiHighLimit = 200;
int DexcomMFD::highLimit = 150;
int DexcomMFD::lowLimit = 80;
int DexcomMFD::loLowLimit = 60;
int DexcomMFD::glucoseDisplay = 0;
int DexcomMFD::rateDisplay = +7;
int DexcomMFD::battDisplay = 72;
int DexcomMFD::dataAge = 1200;


// TODO: rework this for the T-Display
// TODO: Question, can create a static global 
void DexcomMFD::setupTFT()
{
    // turn on backlight
    pinMode(PIN_LCD_BL, OUTPUT);
    digitalWrite(PIN_LCD_BL, HIGH);

    // turn on the TFT / I2C power supply
    pinMode(PIN_POWER_ON, OUTPUT);
    digitalWrite(PIN_POWER_ON, HIGH);
    delay(10);

    // initialize TFT
    tft.begin();
    tft.setRotation(0);
    tft.fillScreen(BLACK);

    Serial.println(F("Initialized"));

    set_hiHighBG(220);
    set_highBG(170);
    set_lowBG(100);
    set_loLowBG(75);
}

void DexcomMFD::drawScreen()
{
    const static int gluX = 70;
    const static int gluY = 80;
    const static int gluWd = 22;
    const static int gluTpWd = 14;
    const static int gluTpOfSt = gluWd - gluTpWd;
    const static int gluHt = 178;
    const static int gluMax = 250;
    const static int gluMin = 40;
    const static int gluPPP_100 = ( gluHt * 100 ) / (gluMax - gluMin); //glucose Pixels per Point x100
    const static int gluYMx = gluY + 2;
    const static int gluYHH = gluYMx + ((gluPPP_100 * (gluMax - hiHighLimit)) / 100);
    const static int gluYH = gluYMx + ((gluPPP_100 * (gluMax - highLimit)) / 100);
    const static int gluYL = gluYMx + ((gluPPP_100 * (gluMax - lowLimit)) / 100);
    const static int gluYLL = gluYMx + ((gluPPP_100 * (gluMax - loLowLimit)) / 100);
    const static int gluYMn = gluYMx + gluHt;
    int glucoseY = gluYMx + ((gluPPP_100 * (gluMax - glucoseDisplay)) / 100);
    int textOffset = glucoseDisplay > 99 ? 24 : 12;
    if (glucoseY > gluYMn) glucoseY = gluYMn;
    if (glucoseY < gluYMx) glucoseY = gluYMx;
    tft.fillScreen(BLACK);
    tft.drawFastHLine(gluX, gluY, gluWd, WHITE);
    tft.drawFastHLine(gluX, gluY + 1, gluWd, WHITE);
    tft.drawFastHLine(gluX, gluY + gluHt + 2, gluWd, WHITE);
    tft.drawFastHLine(gluX, gluY + gluHt + 3, gluWd, WHITE);
    tft.drawFastHLine(gluX + gluWd, gluYH, 5, WHITE);
    tft.drawFastHLine(gluX + gluWd, gluYH + 1, 5, WHITE);
    tft.drawFastHLine(gluX + gluWd, gluYL, 5, WHITE);
    tft.drawFastHLine(gluX + gluWd, gluYL + 1, 5, WHITE);
    tft.drawFastVLine(gluX + gluWd, gluY, gluHt + 4, WHITE);
    tft.drawFastVLine(gluX + gluWd + 1, gluY, gluHt + 4, WHITE);
    // tft.fillRect(gluX + gluTpOfSt, gluYMx, gluTpWd, gluYHH - gluYMx, RED); //High-High warning bar
    // tft.fillRect(gluX + gluTpOfSt, gluYLL, gluTpWd, gluYMn - gluYLL, RED); //Low-Low Warning bar
    // tft.fillRect(gluX + gluTpOfSt, gluYHH, gluTpWd, gluYH - gluYHH, YELLOW); //high caution bar
    // tft.fillRect(gluX + gluTpOfSt, gluYL, gluTpWd, gluYLL - gluYL, YELLOW); //low caution bar
    // tft.fillRect(gluX + gluTpOfSt, gluYH, gluTpWd, gluYL - gluYH, GREEN); //green control band
    pfdColorVTape(gluX + gluTpOfSt, gluYH, gluYL, gluTpWd, GREEN);
    pfdColorVTape(gluX + gluTpOfSt, gluYHH, gluYH, gluTpWd, YELLOW);
    pfdColorVTape(gluX + gluTpOfSt, gluYL, gluYLL, gluTpWd, YELLOW);
    pfdColorVTape(gluX + gluTpOfSt, gluYMx, gluYHH, gluTpWd, RED);
    pfdColorVTape(gluX + gluTpOfSt, gluYLL, gluYMn, gluTpWd, RED);
    tft.setTextColor(WHITE);
    tft.setFont(u8g2_font_helvB14_te);
    // tft.setCursor(gluX - 15, 12);
    tft.setCursor(gluX + gluTpOfSt - 20, 36);
    tft.println("CGM");
    tft.setCursor(gluX + gluWd + 8, gluYH + 5);
    tft.println(highLimit);
    tft.setCursor(gluX + gluWd + 8, gluYL + 5);
    tft.println(lowLimit);
    tft.setFont(u8g2_font_inb21_mr);
    // tft.setCursor(gluX - 20, gluY - 8);
    if (glucoseDisplay > 10)
    {
        // Serial.print("Value: ");
        // Serial.print(String(glucoseDisplay));
        // Serial.print(" Text offset: ");
        // Serial.println(txtCenter(String(glucoseDisplay)));

        tft.setCursor(gluX + gluTpOfSt - textOffset, gluY - 8);
        tft.println(glucoseDisplay);
        tft.fillTriangle(gluX, glucoseY - (gluTpWd / 2), gluX + gluWd, glucoseY, gluX, glucoseY + (gluWd / 2), WHITE);
        tft.drawTriangle(gluX, glucoseY - (gluTpWd / 2), gluX + gluWd, glucoseY, gluX, glucoseY + (gluWd / 2), BLACK);
    }
    else
    {
        tft.setCursor(gluX + gluTpOfSt - txtCenter("---"), gluY - 8);
        tft.println("---");
    }


}

void DexcomMFD::pfdColorVTape( uint16_t x, uint16_t y1, uint16_t y2, uint16_t w, uint16_t color) {
  uint16_t color_1, color_2, color_3;
  uint16_t h = y2 - y1;
  color_1 = color_2 = color_3 = color;
  if (color == GREEN){
    color_1 = GREEN_1;
    color_2 = GREEN_2;
    color_3 = GREEN_3;
  }
  if (color == YELLOW){
    color_1 = YELLOW_1;
    color_2 = YELLOW_2;
    color_3 = YELLOW_3;
  }
  if (color == RED){
    color_1 = RED_1;
    color_2 = RED_2;
    color_3 = RED_3;
  }
  if (h > 0) {
    for(int i = 0; i < w; i++) {
      if (w > 2 && (i == 0 || i == w-1)) tft.drawFastVLine(x+i, y1, h, color_3);
      else if (w > 4 && (i == 1 || i == w-2)) tft.drawFastVLine(x+i, y1, h, color_2);
      else if (w > 6 && (i == 2 || i == w-3)) tft.drawFastVLine(x+i, y1, h, color_1);
      else tft.drawFastVLine(x+i, y1, h, color);
    }
  }
}

void DexcomMFD::drawGrid()
{
    int w = 170;
    int h = 320;
    for (int x = 9; x < w; x+=10) tft.drawFastVLine(x, 0, h, DARKGREY);
    for (int y = 9; y < h; y+=10) tft.drawFastHLine(0, y, w, DARKGREY);
}

void DexcomMFD::drawTime(uint32_t time)
{
    int timeMins = time / 60;
    int timeSecs = time - (60 * timeMins);
    int minTens = timeMins / 10;
    int minOnes = timeMins - (10 * minTens);
    int secTens = timeSecs / 10;
    int secOnes = timeSecs - (10 * secTens);
    int y = 300;
    int x = 20;
    int wDig = 12;
    int hDig = 20;


    if (minTens > 9) 
    {
        minTens = minOnes = secTens = secOnes = 9;
    }

    // drawGrid();
    uint16_t color = DARKGREEN;
    if (time > 300) color = YELLOW;
    if (time > 600) color = RED;

    tft.drawRoundRect(x-4, y-hDig-2, 5*wDig + 8, hDig + 8, 3, color); //age status
    tft.drawRoundRect(x-3, y-hDig-1, 5*wDig + 6, hDig + 6, 3, color); //age status

    tft.fillRoundRect(x-2, y-hDig, 5*wDig + 4, hDig + 4, 3, BLACK); //Erase old time
    tft.setTextColor(WHITE);
    tft.setFont(u8g2_font_helvB14_te);
    tft.setCursor(x, y);
    if (minTens > 0) tft.println(minTens);
    tft.setCursor(x + wDig, y);
    tft.println(minOnes);
    tft.setCursor(x + 2*wDig, y);
    tft.println(":");
    tft.setCursor(x + 3*wDig, y);
    tft.println(secTens);
    tft.setCursor(x + 4*wDig, y);
    tft.println(secOnes);

}

void DexcomMFD::drawVBat(int mVolts)
{
    int y = 300;
    int x = 100;
    int wDig = 9;
    int hDig = 20;
    int volts = mVolts / 1000;
    int tenths = (mVolts - 1000*volts) / 100;
    int hundreths = (mVolts - 1000*volts - 100*tenths) / 10;

    tft.fillRoundRect(x-2, y-hDig, 5*wDig + 4, hDig + 4, 3, BLACK); //Erase old time
    tft.setTextColor(WHITE);
    tft.setFont(u8g2_font_helvB10_te);
    tft.setCursor(x, y);
    tft.println(volts);
    tft.setCursor(x + wDig, y);
    tft.println(".");
    tft.setCursor(x + 2*wDig, y);
    tft.println(tenths);
    tft.setCursor(x + 3*wDig, y);
    tft.println(hundreths);
    tft.setCursor(x + 4*wDig, y);
    tft.println("v");

}

void DexcomMFD::drawPBat(int pct)
{
    int y = 280;
    int x = 110;
    int wDig = 9;
    int hDig = 20;
    int hundreds = pct / 100;
    int tens = (pct - 100*hundreds) / 10;
    int ones = pct - 100*hundreds - 10*tens;

    tft.fillRoundRect(x-2, y-hDig, 5*wDig + 4, hDig + 4, 3, BLACK); //Erase old time
    tft.setTextColor(WHITE);
    tft.setFont(u8g2_font_helvB10_te);
    if(pct >= 100)
    {        
        tft.setCursor(x, y);
        tft.println(hundreds);
    }
    if(pct >= 10)
    {        
        tft.setCursor(x + wDig, y);
        tft.println(tens);
    }
    tft.setCursor(x + 2*wDig, y);
    tft.println(ones);
    tft.setCursor(x + 3*wDig, y);
    tft.println("%");

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


