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
#include "rom/crc.h"
#include "DebugHelper.h"



class DexcomClient
{
    static uint16_t currentBG;

    public:
        static bool findAndConnect();

    private:
        static std::string CRC_16_XMODEM(std::string message);


};


#endif /* G6DEXCOMCLIENT_H */