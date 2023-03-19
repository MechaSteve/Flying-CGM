/**
 * Header File with functions to communitcate data with with dexcom transmitter
 * after a connection has been created and authenticated.
 * Also includes defines specifict to the G6 protocol and services
 * 
 * 
 * Author: Stephen Culpepper
 * 2023.03.15
 */

#ifndef G6DEXCOMCLIENT_H
#define G6DEXCOMCLIENT_H


#include <Arduino.h>
#include <Esp.h>
#include "DebugHelper.h"
#include "G6DexcomBLE.h"



class DexcomClient
{
    public:
        static bool findAndConnect();
        static bool needBackfill();
        static bool readTimeMessage();
        static bool readBatteryStatus();
        static bool readGlucose();
        static bool readSensor();
        static bool readLastCalibration();
        static bool readBackfill();
        static bool saveBackfill(std::string backfillParseMessage);
        static void parseBackfill(std::string data);
    private:
        static uint16_t currentBG;
        static int saveLastXValues;
        static uint16_t glucoseValues[72];
        static std::string backfillStream;
        static int backfillExpectedSequence;
        static std::string CRC_16_XMODEM(std::string message);
        static void printSavedGlucose();
};

#endif /* G6DEXCOMCLIENT_H */