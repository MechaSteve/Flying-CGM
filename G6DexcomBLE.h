/**
 * Header File with functions to setup the BLE module for communications with the Dexcom G6
 * Also includes defines specifict to the G6 protocol and services
 * 
 * 
 * Author: Stephen Culpepper
 * 2023.03.15
 */

#ifndef G6DEXCOMBLE_H
#define G6DEXCOMBLE_H


#include <Arduino.h>
#include <Esp.h>
#include "rom/crc.h"
#include "mbedtls/aes.h"
#include "BLEDevice.h"
#include "BLEScan.h"
#include "DebugHelper.h"


// Byte values for the notification / indication.
const uint8_t bothOff[]        = {0x0, 0x0};
const uint8_t notificationOn[] = {0x1, 0x0};
const uint8_t indicationOn[]   = {0x2, 0x0};
const uint8_t bothOn[]         = {0x3, 0x0};


/**
 * Container for state and callbacks for the secure bonding process.
 * The transmitter will request / initiate the bonding.
 */
class DexcomSecurity : public BLESecurityCallbacks 
{
    static bool bonding;
    static bool bondingFinished;
    static bool forceRebonding;

    public:
        static bool authenticate();
        static void forceRebondingEnable();
        static void forceRebondingDisable();
        static bool isBonded();
        uint32_t onPassKeyRequest();
        void onPassKeyNotify(uint32_t pass_key);
        bool onConfirmPIN(uint32_t pass_key);
        bool onSecurityRequest();
        void onAuthenticationComplete(esp_ble_auth_cmpl_t auth_cmpl);
        static bool requestBond();
        static void setupBonding();
    private:
        static std::string calculateHash(std::string data, std::string id);
        static std::string encrypt(std::string buffer, std::string id);
};

class DexcomConnection : public BLEClientCallbacks
{
    static bool connected;                  // Indicates if the ble client is connected to the transmitter. Used to detect a transmitter timeout.
    static std::string AuthCallbackResponse;
    static std::string BackfillCallbackResponse;
    static std::string ControlCallbackResponse;
    static std::string transmitterID;       // Static storage of one transmitter ID (only the last two characters matter)
    static bool alternateChannel;        // Option to use the alternate data channel (true if using with pump)
    static bool errorConnection;            // Used to hold error status until the connection is disconnected.
    static bool errorLastConnection;
    static unsigned long disconnectTime;
    static BLERemoteCharacteristic* pRemoteCommunication;
    static BLERemoteCharacteristic* pRemoteControl;
    static BLERemoteCharacteristic* pRemoteAuthentication;
    static BLERemoteCharacteristic* pRemoteBackfill;
    static BLERemoteCharacteristic* pRemoteManufacturer;            // Uses deviceInformationServiceUUID
    static BLERemoteCharacteristic* pRemoteModel;                   // Uses deviceInformationServiceUUID
    static BLERemoteCharacteristic* pRemoteFirmware;                // Uses deviceInformationServiceUUID
    
    static BLEScan* pBLEScan;                                       // The scanner used to look for the device
    static BLEAdvertisedDevice* myDevice;                           // The remote device (transmitter) found by the scan and set by scan callback function.
    static BLEClient* pClient;                                      // Is global so we can disconnect everywhere when an error occured.

    public:  
        static bool setTransmitterID(std::string updatedTransmitterID);    //returns true if the new transmitter ID is valid, and the value is updated.
        static std::string getTransmitterID();
        static void useAlternateChannel();
        static void usePrimaryChannel();
        static bool usingAlternateChannel();

        static bool find();
        static void advertisedDeviceCallback(BLEAdvertisedDevice advertisedDevice);
        static bool isFound();
        static bool connect();
        void onConnect(BLEClient *bleClient);
        static bool isConnected();
        static bool readDeviceInformations();

        static bool AuthSendValue(std::string value);  
        static std::string AuthWaitToReceiveValue();
        static bool backfillRegister();
        static std::string BackfillWaitToReceiveValue();
        static bool controlRegister();
        static bool ControlSendValue(std::string value);
        static std::string ControlWaitToReceiveValue();

        static bool disconnect();
        void onDisconnect(BLEClient *bleClient);
        static unsigned long sinceDisconnect();
        static bool lastConnectionWasError();
        static bool resetConnection();
        static void commFault(std::string faultMessage);

    private:
        static void indicateControlCallback(BLERemoteCharacteristic* pBLERemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify);
        static void indicateAuthCallback(BLERemoteCharacteristic *pBLERemoteCharacteristic, uint8_t *pData, size_t length, bool isNotify);
        static void notifyBackfillCallback(BLERemoteCharacteristic *pBLERemoteCharacteristic, uint8_t *pData, size_t length, bool isNotify);
        static bool writeValue(std::string caller, BLERemoteCharacteristic* pRemoteCharacteristic, std::string data);
        static bool getCharacteristic(BLERemoteCharacteristic** pRemoteCharacteristic, BLERemoteService* pRemoteService, BLEUUID uuid) ;
        static bool registerForNotification(notify_callback _callback, BLERemoteCharacteristic *pBLERemoteCharacteristic);
        static bool forceRegisterNotificationAndIndication(notify_callback _callback, BLERemoteCharacteristic *pBLERemoteCharacteristic, bool isNotify);
        static bool registerForIndication(notify_callback _callback, BLERemoteCharacteristic *pBLERemoteCharacteristic);

        class nestedAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks
        {
            void onResult(BLEAdvertisedDevice advertisedDevice)                                                                 // Called for each advertising BLE server.
            {
                DexcomConnection::advertisedDeviceCallback(advertisedDevice);
            }
        };
};


#endif /* G6DEXCOMBLE_H */