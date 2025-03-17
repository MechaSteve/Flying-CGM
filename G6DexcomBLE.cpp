/*
 * G6DexcomBLE
 *
 *  Created on: 2023.02.15
 *      Author: Stephen Culpepper
 * 
 */


#include <Arduino.h>
#include <Esp.h>
#include "mbedtls\aes.h"
#include "BLEDevice.h"
#include "BLEScan.h"
#include "BLEUUID.h"
#include "G6DexcomBLE.h"
#include "G6Transmitter.h"


// The remote service we wish to connect to.
static BLEUUID    serviceUUID("f8083532-849e-531c-c594-30f1f86a4ea5");                                                  // This service holds all the important characteristics.
static BLEUUID advServiceUUID("0000febc-0000-1000-8000-00805f9b34fb");                                                  // This service gets advertised by the transmitter.
static BLEUUID deviceInformationServiceUUID("180A");                                                                    // The default service for the general device informations.
// The characteristic of the remote serviceUUID service we are interested in.
static BLEUUID  communicationUUID("F8083533-849E-531C-C594-30F1F86A4EA5"); // NOTIFY, READ
static BLEUUID        controlUUID("F8083534-849E-531C-C594-30F1F86A4EA5"); // INDICATE, WRITE
static BLEUUID authenticationUUID("F8083535-849E-531C-C594-30F1F86A4EA5"); // INDICATE, READ, WRITE (G6 Plus INDICATE / WRITE)
static BLEUUID       backfillUUID("F8083536-849E-531C-C594-30F1F86A4EA5"); // NOTIFY, READ, WRITE (G6 Plus NOTIFY)
//static BLEUUID          xxxUUID("F8083537-849E-531C-C594-30F1F86A4EA5"); // READ
//static BLEUUID          yyyUUID("F8083538-849E-531C-C594-30F1F86A4EA5"); // NOTIFY, READ (G6 Plus only)
// The general characteristic of the device information service.
static BLEUUID manufacturerUUID("2A29"); // READ
static BLEUUID        modelUUID("2A24"); // READ
static BLEUUID     firmwareUUID("2A26"); // READ


//Overall Flow:
// 1) Initialize BLE hardware: BLEDevice::init("");  <<Static function>>
// 2) Setup Scanner: pBLEScan = BLEDevice::getScan(); pBLEScan->set... ; pBLEScan->start
// 3) CallBack from scan to MyAdvertisedDeviceCallbacks()::OnResult() matches dexcom
//   3.1) Stop Scan   <<Static function>>
//   3.2) myDevice = new BLEAdvertisedDevice(advertisedDevice);
// 4) setupBonding();  <<Static function>>
// 5) connectToTransmitter() <<requries myDevice and UUID defines>>
//    5.1) create client, register callbacks and connect
//    5.2) get and save services based on UUIDs
//    5.3) get and save characteristics based on service and UUID
//    5.4) register auth callback to remoteAuthentication characteristic
// 6) readDeviceInformations()  <<debug log only>>
// 7) authenticate(); <<uses authSendValue, authAwaitValue functions and transmitterID, useAlternativeChannel settings (make part of BLE and security config)
// 8) requestBond(); <<has internal condition to check if required based on authenticate() result>>
// 9) readTimeMessage();  <<blocks on ControlWaitToReceiveValue()>>
// 10) readGlucose();  <<blocks on ControlWaitToReceiveValue()>>
//   10.1) OPTIONAL readBatteryStatus(); <<blocks on ControlWaitToReceiveValue()>>
// 11) needBackfill()
//   11.1) register backfill callback
//   11.2) readBackfill() <<blocks on ControlWaitToReceiveValue()>>
// 12) sendDisconnect()
// 13) wait 290s, maybe could return to 4?

// New Flow
//
// Not Found -> Scan() -> Found -> Connect() -> Connected -> Auth() -> Authenticated -> Read(), Disconnect() -> !Connected
//                                    ^------------------------------------------------------------------------------`

// New encryption for G7 will require EC-DSA-256 encryption:
// Night Scout Implementation: https://github.com/NightscoutFoundation/xDrip/tree/master/libkeks/src/main/java/jamorham/keks
// Arduino Library: https://github.com/ShubhamAnnigeri/tinyECC-ArduinoIDE



//
// DexcomSecurity static members
//

/**
 * The Dexcom Reciever is connected (serivces have been found and pointers saved)
*/
bool DexcomSecurity::bonding = false;
volatile bool DexcomSecurity::bondingFinished = false;
bool DexcomSecurity::forceRebonding = false;

bool DexcomSecurity::authenticate()
{
    //Send AuthRequestTxMessage
    uint8_t authRequestTxBuffer[10] = {0x01, 0x19, 0xF3, 0x89, 0xF8, 0xB7, 0x58, 0x41, 0x33, 0 };  //0x02                 // 10byte, first byte = opcode (fix), [1] - [8] random bytes as challenge for the transmitter to encrypt,
    authRequestTxBuffer[9] = DexcomConnection::usingAlternateChannel() ? 0x01 : 0x02;                                                        // last byte 0x02 = normal bt channel, 0x01 alternative bt channel
    DexcomConnection::AuthSendValue(authRequestTxBuffer, 10);

    //Recv AuthChallengeRXMessage
    uint8_t authChallengeBuffer[20];
    size_t authChallengeSize = DexcomConnection::AuthWaitToReceiveValue(authChallengeBuffer, 20);                         // Wait until we received data from the notify callback.
    if ((authChallengeSize != 17) || (authChallengeBuffer[0] != 0x03))
    {
        SerialPrintln(ERROR, "Error wrong length or opcode!");
        return false;
    }
    uint8_t tokenHash[8];
    uint64_t challenge;
    memcpy(tokenHash, &authChallengeBuffer[1], 8);               // Start with 1 to skip opcode.
    memcpy(&challenge, &authChallengeBuffer[9], 8);             // store 8 bytes in a uint64_t
    //Here we could check if the tokenHash is the encrypted 8 bytes from the authRequestTxMessage ([1] to [8]);
    //To check if the Transmitter is a valid dexcom transmitter (because only the correct one should know the ID).

    //Send AuthChallengeTXMessage
    uint64_t hash = calculateHash(challenge, DexcomConnection::getTransmitterID());                                                         // Calculate the hash from the random 8 bytes the transmitter send us as a challenge.
    uint8_t authChallengeTXMessage[9] = {0x04, 0,0,0,0, 0,0,0,0};                                                                        // opcode
    memcpy(&authChallengeTXMessage[1], &hash, 8);                                                                                     // in total 9 byte.
    DexcomConnection::AuthSendValue(authChallengeTXMessage, 9);

    //Recv AuthStatusRXMessage
    uint8_t authStatusBuffer[8];
    size_t authStatusSize = DexcomConnection::AuthWaitToReceiveValue(authStatusBuffer, 8);                              // Response { 0x05, 0x01 = authenticated / 0x02 = not authenticated, 0x01 = no bonding, 0x02 bonding
    if(authStatusSize == 3 && authStatusBuffer[1] == 1)                                                // correct response is 0x05 0x01 0x02
    {
        SerialPrintln(DEBUG, "Authenticated!");
        bonding = authStatusBuffer[2] != 0x01;
        return true;
    }
    else
        SerialPrintln(ERROR, "Authenticated FAILED!");
    return false;
}

/**
 * Calculates the 8 byte Hash for the given data.
 */
uint64_t DexcomSecurity::calculateHash(uint64_t data, String id)
{
    uint64_t returnValue = 0;
    //data = data + data;  //done as a string concat
    uint8_t doubleData[16], hashBuffer[16];                                         // Use double the data to get 16 byte
    memcpy(&doubleData[0], &data, 8);
    memcpy(&doubleData[8], &data, 8);
    encrypt(doubleData, id, hashBuffer);
    memcpy(&returnValue, &hashBuffer[0], 8);                                         // Only use the first 8 byte of the hash (ciphertext)
    return returnValue;                                                                                           

}


/**
 * Encrypt using AES 182 ecb (Electronic Code Book Mode).
 */
void DexcomSecurity::encrypt(uint8_t* buffer, String id, uint8_t* output)
{
    mbedtls_aes_context aes;

    String key = "00" + id + "00" + id;                                                                            // The key (that also used the transmitter) for the encryption.

    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_enc(&aes, (const unsigned char *)key.c_str(), strlen(key.c_str()) * 8);
    mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, buffer, output);
    mbedtls_aes_free(&aes);
}

void DexcomSecurity::forceRebondingEnable() { forceRebonding = true; }
void DexcomSecurity::forceRebondingDisable() { forceRebonding = false; }
bool DexcomSecurity::forceRebondingEnabled() { return forceRebonding; }
bool DexcomSecurity::isBonded() { return bondingFinished; }


uint32_t DexcomSecurity::onPassKeyRequest()
{
    return 123456;
}
void DexcomSecurity::onPassKeyNotify(uint32_t pass_key) {}
bool DexcomSecurity::onConfirmPIN(uint32_t pass_key)
{
    return true;
}
//Unconditionally respond to security requests
bool DexcomSecurity::onSecurityRequest() { return true; }

void DexcomSecurity::onAuthenticationComplete(esp_ble_auth_cmpl_t auth_cmpl) {                                      // This function is the only one that gets currently triggered.
int reasonCode = -1;
    SerialPrint(DEBUG, "pair status = ");
    SerialPrintln(DEBUG, auth_cmpl.success ? "success" : "fail");
    if (auth_cmpl.success) { 
        SerialPrintln(DEBUG, "onAuthenticationComplete : finished with bonding.");
        bondingFinished = true;    
    }  // bonding completed successfully
    else {
        reasonCode = auth_cmpl.fail_reason;
        Serial.println(auth_cmpl.fail_reason, HEX);
        Serial.println(reasonCode);
    }   // bonding failed
}

/**
 * Enables BLE bonding.
 */
void DexcomSecurity::setupBonding()
{ //CHANGE : Changed return type to void from bool. return value is not used (causing device reset?)
    BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT);                                                                 // Enable security encryption.
    //TODO: Classified implementation will use either pMyDexcomSecurity or &MyDexcomSecurity
    //This current method works because the old Security class was "static", but not really because it relied on globals
    BLEDevice::setSecurityCallbacks(new DexcomSecurity());
    BLESecurity *pSecurity = new BLESecurity();
    pSecurity->setKeySize();
    pSecurity->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_ONLY);
    pSecurity->setCapability(ESP_IO_CAP_IO);
    pSecurity->setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
    SerialPrintln(DEBUG, "Enabled bonding.");
}

/**
 * We have successfully authorized and now want to bond.
 * First enable the BLE security bonding options and then indicate the transmitter that he can now initiate a bonding. 
 * Return true if no error occurs.
 */
bool DexcomSecurity::requestBond()
{
    if(bonding)
    {

        SerialPrintln(DEBUG, "rqst_bd");
        
        if(forceRebonding) {
          // Enable bonding after successful auth and before sending bond request to transmitter.
          SerialPrintln(DEBUG, "setup_bd_st");
          setupBonding();
          SerialPrintln(DEBUG, "setup_bd_ed");
        }

        SerialPrintln(DEBUG, "Sending Bond Request.");
        //Send KeepAliveTxMessage
        uint8_t keepAliveTxMessage[2] = {0x06, 0x19};                                                                  // Opcode 2 byte = 0x06, 25 as hex (0x19)
        DexcomConnection::AuthSendValue(keepAliveTxMessage, 2);
        SerialPrintln(DEBUG, "snd_kp_al");
        //Send BondRequestTxMessage
        uint8_t bondRequestTxMessage[1] = {0x07};                                                                      // Send bond command.
        DexcomConnection::AuthSendValue(bondRequestTxMessage, 1);
        SerialPrintln(DEBUG, "snd_bd_rq");
        //Wait for bonding to finish
        SerialPrintln(DEBUG, "Waiting for bond.");
        while (bondingFinished == false);                                                                               // Barrier waits until bonding has finished, IMPORTANT to set the bondingFinished variable to sig_atomic_t OR volatile
        //Wait
        SerialPrintln(DEBUG, "Bonding finished.");
    }
    else
        SerialPrintln(DEBUG, "Transmitter does not want to (re)bond so DONT send bond request (already bonded).");
    return true;
}

/**
 * The Dexcom Receiver is connected (services have been found and pointers saved)
*/
volatile bool DexcomConnection::connected = false;
bool DexcomConnection::errorConnection = false;
volatile bool DexcomConnection::errorLastConnection = false;
bool DexcomConnection::alternateChannel = false;        // Option to use the alternate data channel (true if using with pump)

unsigned long DexcomConnection::disconnectTime = 0;
uint8_t DexcomConnection::AuthResponseBuffer[32];
volatile size_t DexcomConnection::AuthResponseLength = 0;
uint8_t DexcomConnection::BackfillResponseBuffer[32];
volatile size_t DexcomConnection::BackfillResponseLength = 0;
uint8_t DexcomConnection::ControlResponseBuffer[32];
volatile size_t DexcomConnection::ControlResponseLength = 0;
String DexcomConnection::transmitterID = DEXCOM_CONFIG_DEFAULT_ID;       // Static storage of one transmitter ID (only the last two characters matter)


BLERemoteCharacteristic* DexcomConnection::pRemoteCommunication = NULL;
BLERemoteCharacteristic* DexcomConnection::pRemoteControl = NULL;
BLERemoteCharacteristic* DexcomConnection::pRemoteAuthentication = NULL;
BLERemoteCharacteristic* DexcomConnection::pRemoteBackfill = NULL;
BLERemoteCharacteristic* DexcomConnection::pRemoteManufacturer = NULL;            // Uses deviceInformationServiceUUID
BLERemoteCharacteristic* DexcomConnection::pRemoteModel = NULL;                   // Uses deviceInformationServiceUUID
BLERemoteCharacteristic* DexcomConnection::pRemoteFirmware = NULL;                // Uses deviceInformationServiceUUID

BLEScan* DexcomConnection::pBLEScan = NULL;                                                           // The scanner used to look for the device
BLEAdvertisedDevice* DexcomConnection::myDevice = NULL;                           // The remote device (transmitter) found by the scan and set by scan callback function.
BLEClient* DexcomConnection::pClient = NULL;                                      // Is global so we can disconnect everywhere when an error occurred.

/**
 * Getter functions
*/

bool DexcomConnection::isConnected() { return connected; }
bool DexcomConnection::isFound() { return myDevice != NULL; }
bool DexcomConnection::lastConnectionWasError() { return errorLastConnection; }

void DexcomConnection::useAlternateChannel() { alternateChannel = true; }
void DexcomConnection::usePrimaryChannel() { alternateChannel = false; }
bool DexcomConnection::usingAlternateChannel() { return alternateChannel; }

void DexcomConnection::onConnect(BLEClient* bleClient) 
{
    SerialPrintln(DATA, "onConnect");
    connected = true;
}

void DexcomConnection::onDisconnect(BLEClient* bleClient)
{
    SerialPrintln(DATA, "onDisconnect");  
    connected = false;                          //change state
    errorLastConnection = errorConnection;      //save error or lack there of
}

/**
 * Returns time since last disconnect in ms, if it has not connected, will return 999,999,999
*/
unsigned long DexcomConnection::sinceDisconnect() { return disconnectTime == 0 ? 999999999 : millis() - disconnectTime; }



/**
 * Function to update the transmitter ID. 
 * Returns true if the update is successful.
 * Fails if the ID is not exactly 6 characters or if the 5th and 6th are not A-Z
*/
bool DexcomConnection::setTransmitterID(String updatedTransmitterID)
{
    if (updatedTransmitterID.length() == 6) {
        if (updatedTransmitterID[4] >= 'A' && updatedTransmitterID[4] <= 'Z') {
            if (updatedTransmitterID[5] >= 'A' && updatedTransmitterID[5] <= 'Z') {
                transmitterID = updatedTransmitterID;
            }
        }
    }
    return transmitterID == updatedTransmitterID;
}

String DexcomConnection::getTransmitterID()
{
    return transmitterID;
}

/**
 * The different callbacks for notify and indicate if new data from the transmitter is available.
 */ 
void DexcomConnection::indicateControlCallback(BLERemoteCharacteristic* pBLERemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify) 
{
    SerialPrint(DEBUG, "indicateControlCallback - read ");
    SerialPrint(DEBUG, length, DEC);
    SerialPrintln(DEBUG, " byte data: ");
    printHexArray(pData, length);
    ControlResponseLength = length;
    memcpy(&ControlResponseBuffer[0], pData, length > 32 ? 32 : length);
}

void DexcomConnection::indicateAuthCallback(BLERemoteCharacteristic* pBLERemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify) 
{
    SerialPrint(DEBUG, "indicateAuthCallback - read ");
    SerialPrint(DEBUG, length, DEC);
    SerialPrintln(DEBUG, " byte data: ");
    printHexArray(pData, length);
    AuthResponseLength = length;
    memcpy(&AuthResponseBuffer[0], pData, length > 32 ? 32 : length);
}

void DexcomConnection::notifyBackfillCallback(BLERemoteCharacteristic* pBLERemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify) 
{
    SerialPrint(DEBUG, "notifyBackfillCallback - read ");
    SerialPrint(DEBUG, length, DEC);
    SerialPrintln(DEBUG, " byte data: ");
    printHexArray(pData, length);
    BackfillResponseLength = length;
    memcpy(&BackfillResponseBuffer[0], pData, length > 32 ? 32 : length);
}

/**
 * Reads the device informations which are not dexcom specific.
 */
bool DexcomConnection::readDeviceInformations()
{
    

    if(!pRemoteManufacturer->canRead())                                                                                 // Check if the characteristic is readable.
        return false;
    SerialPrint(DEBUG, "The Manufacturer value was: ");
    SerialPrintln(DEBUG, pRemoteManufacturer->readValue().c_str());                                                     // Read the value of the device information characteristics.
    
    if(!pRemoteModel->canRead())
        return false;
    SerialPrint(DEBUG, "The Model value was: ");
    SerialPrintln(DEBUG, pRemoteModel->readValue().c_str());
    
    if(!pRemoteFirmware->canRead())
        return false;
    SerialPrint(DEBUG, "The Firmware value was: ");
    SerialPrintln(DEBUG, pRemoteFirmware->readValue().c_str());
    return true;
}

/**
 * Wrapper function to send data to the authentication characteristic.
 */
bool DexcomConnection::AuthSendValue(uint8_t* pData, size_t length)
{
    AuthResponseLength = 0;                                                                                          // Reset to invalid because we will write to the characteristic and must wait until new data arrived from the notify callback.
    return writeValue("AuthSendValue", pRemoteAuthentication, pData, length);
}


/**
 * Barrier to wait until new data arrived through the notify callback.
 */
size_t DexcomConnection::AuthWaitToReceiveValue(uint8_t* pData, size_t max_length)
{
    while(connected)                                                                                                    // Only loop until we lost connection.
    {
        if(AuthResponseLength != 0)
        {
            size_t returnSize = AuthResponseLength > max_length ? max_length : AuthResponseLength;
            memcpy(pData, &AuthResponseBuffer[0], returnSize);                                                      // Save the new value.
            AuthResponseLength = 0;                                                                                  // Reset because we handled the new data.
            return returnSize;
        }
    }
    commFault("Error timeout in AuthWaitToReceiveValue");                                                               // The transmitter disconnected so exit.
    return 0;
}


/**
 * Barrier to wait until new data arrived through the notify callback.
 */
size_t DexcomConnection::BackfillWaitToReceiveValue(uint8_t* pData, size_t max_length)
{
    while(connected)                                                                                                    // Only loop until we lost connection.
    {
        if(BackfillResponseLength != 0)
        {
            size_t returnSize = BackfillResponseLength > max_length ? max_length : BackfillResponseLength;
            memcpy(pData, &BackfillResponseBuffer[0], returnSize);                                                      // Save the new value.
            BackfillResponseLength = 0;                                                                                  // Reset because we handled the new data.
            return returnSize;
        }
    }
    commFault("Error timeout in BackfillWaitToReceiveValue");                                                               // The transmitter disconnected so exit.
    return 0;
}

/**
 * Wrapper function to send data to the control characteristic.
 */
bool DexcomConnection::ControlSendValue(uint8_t* pData, size_t length)
{
    ControlResponseLength = 0;                                                                                          
    return writeValue("ControlSendValue", pRemoteControl, pData, length);
}


/**
 * Barrier to wait until new data arrived through the notify callback.
 */
size_t DexcomConnection::ControlWaitToReceiveValue(uint8_t* pData, size_t max_length)
{
    while(connected)                                                                                                    // Only loop until we lost connection.
    {
        if(ControlResponseLength != 0)
        {
            size_t returnSize = ControlResponseLength > max_length ? max_length : ControlResponseLength;
            memcpy(pData, &ControlResponseBuffer[0], returnSize);                                                      // Save the new value.
            ControlResponseLength = 0;                                                                                  // Reset because we handled the new data.
            return returnSize;
        }
    }
    commFault("Error timeout in ControlWaitToReceiveValue");                                                            // The transmitter disconnected so exit.
    return 0;
}

/**
 * Create a BLE scanner and block while looking for a dexcom transmitter with the correct ID
*/
void DexcomConnection::find() 
{

    BLEDevice::init("");                                                                                    // Possible source of error if we cant connect to the transmitter.

    pBLEScan = BLEDevice::getScan();                                                                        // Retrieve a Scanner.
    pBLEScan->setAdvertisedDeviceCallbacks(new nestedAdvertisedDeviceCallbacks());                              // Set the callback to informed when a new device was detected.
    pBLEScan->setInterval(100); //100 works                                                                 // The time in ms how long each search intrevall last. Important for fast scanning so we dont miss the transmitter waking up.
    pBLEScan->setWindow(99); //60-99 works                                                                  // The actual time that will be searched. Interval - Window = time the esp is doing nothing (used for energy efficiency).
    pBLEScan->setActiveScan(false); 
    pBLEScan->start(3, true);                                                                               // false = maybe helps with connection problems.
}

void DexcomConnection::advertisedDeviceCallback(BLEAdvertisedDevice advertisedDevice)
{
    SerialPrint(DEBUG, "BLE Advertised Device found: ");
    SerialPrintln(DEBUG, advertisedDevice.toString().c_str());

    // We have found a device, let us now see if it contains the service we are looking for.
    if (advertisedDevice.haveServiceUUID() && advertisedDevice.isAdvertisingService(advServiceUUID) &&              // If the advertised service is the dexcom advertise service (not the main service that contains the characteristics).
        advertisedDevice.haveName() && 
         (advertisedDevice.getName() == ("Dexcom" + transmitterID.substring(4,6)) ||
          advertisedDevice.getName() == transmitterID)
        )
    {
        pBLEScan->stop();                                                                               // We found our transmitter so stop scanning for now.
        SerialPrintln(DEBUG, "Found Dexcom");
        myDevice = new BLEAdvertisedDevice(advertisedDevice);                                                       // Save device as new copy, myDevice also triggers a state change in main loop.
    }
}

/**
 * Initiate a connection to the transmitter by recording the references for services and characteristics
*/
bool DexcomConnection::connect()
{
    errorConnection = false;

    SerialPrint(DEBUG, "Forming a connection to ");
    SerialPrintln(DEBUG, myDevice->getAddress().toString().c_str());

    pClient = BLEDevice::createClient();                                                                                // We specify the security settings later after we have successful authorized with the transmitter.
    SerialPrintln(DEBUG, " - Created client");

    pClient->setClientCallbacks(new DexcomConnection());                                                                // Callbacks for onConnect() onDisconnect()
    SerialPrintln(DEBUG, " - Callbacks assigned attempting connection to myDevice");

    // Connect to the remote BLE Server.
    if(!pClient->connect(myDevice))                                                                                     // Notice from the example: if you pass BLEAdvertisedDevice instead of address, it will be recognized type of peer device address (public or private)
        return false;
    
    SerialPrintln(DEBUG, " - Connected to server");

    // Obtain a reference to the service.
    BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
    if (pRemoteService == nullptr) 
    {
        SerialPrint(ERROR, "Failed to find our service UUID: ");
        SerialPrintln(ERROR, serviceUUID.toString().c_str());
        pClient->disconnect();
        return false;
    }
    BLERemoteService* pRemoteServiceInfos = pClient->getService(deviceInformationServiceUUID);
    if (pRemoteServiceInfos == nullptr)
    {
        SerialPrint(ERROR, "Failed to find our service UUID: ");
        SerialPrintln(ERROR, deviceInformationServiceUUID.toString().c_str());
        pClient->disconnect();
        return false;
    }
    SerialPrintln(DEBUG, " - Found our services");

    // Obtain a reference to the characteristic in the service of the remote BLE server.
    SerialPrintln(DEBUG, getCharacteristic(&pRemoteCommunication, pRemoteService, communicationUUID)    ? "Found Communication Char."   : "Did not find Communication Char.");
    SerialPrintln(DEBUG, getCharacteristic(&pRemoteControl, pRemoteService, controlUUID)                ? "Found Control Char."         : "Did not find Control Char.");
    SerialPrintln(DEBUG, getCharacteristic(&pRemoteAuthentication, pRemoteService, authenticationUUID)  ? "Found Authentication Char."  : "Did not find Authentication Char.");
    SerialPrintln(DEBUG, getCharacteristic(&pRemoteBackfill, pRemoteService, backfillUUID)              ? "Found Backfill Char."        : "Did not find Backfill Char.");

    SerialPrintln(DEBUG, getCharacteristic(&pRemoteManufacturer, pRemoteServiceInfos, manufacturerUUID) ? "Found Manufacturer Char."    : "Did not find Manufacturer Char.");
    SerialPrintln(DEBUG, getCharacteristic(&pRemoteModel, pRemoteServiceInfos, modelUUID)               ? "Found Model Char."           : "Did not find Model Char.");
    SerialPrintln(DEBUG, getCharacteristic(&pRemoteFirmware, pRemoteServiceInfos, firmwareUUID)         ? "Found Firmware Char."        : "Did not find Firmware Char.");
    SerialPrintln(DEBUG, " - Found our characteristics");

    
    SerialPrint(DEBUG, "The Manufacturer value was: ");
    SerialPrintln(DEBUG, pRemoteManufacturer->readValue().c_str());                                                     // Read the value of the device information characteristics.

    forceRegisterNotificationAndIndication(indicateAuthCallback, pRemoteAuthentication, false);                         // Needed to work with G6 Plus (and G6) sensor. The command below only works for G6 (81...) transmitter.
    //registerForIndication(indicateAuthCallback, pRemoteAuthentication);                                               // We only register for the Auth characteristic. When we are authorised we can register for the other characteristics.

    return !errorConnection;
}

/**
 * Sending command to initiate a disconnect from the transmitter.
 */
bool DexcomConnection::disconnect()
{ 
    SerialPrintln(DEBUG, "Initiating a disconnect.");
    uint8_t disconnectTxMessage[1] = {0x09}; 
    ControlSendValue(disconnectTxMessage, 1);
    while(connected);                                                                                                   // Wait until onDisconnect callback was called and connected status flipped.
    return true;
}


/**
 * Gets and checks the characteristic from the remote service specified by the characteristics UUID.
 * Uses pRemoteCharacteristic as an out parameter to get pointer.
 */
bool DexcomConnection::getCharacteristic(BLERemoteCharacteristic** pRemoteCharacteristic, BLERemoteService* pRemoteService, BLEUUID uuid) // Use *pRemoteCharacteristic as an out parameter so get address/pointer of this pointer.
{
    *pRemoteCharacteristic = pRemoteService->getCharacteristic(uuid);                                                   // Write to where the pointer points (the pRemoteCharacteristic pointer address).
    if (*pRemoteCharacteristic == nullptr) 
    {
        SerialPrint(DEBUG, "Failed to find our characteristic for UUID: ");
        SerialPrintln(DEBUG, uuid.toString().c_str());
        return false;
    }
    return true;
}

bool DexcomConnection::backfillRegister() 
{
    if( pRemoteControl != nullptr) { return forceRegisterNotificationAndIndication(notifyBackfillCallback, pRemoteBackfill, false); }
    return false;
}

bool DexcomConnection::backfillRegister(notify_callback callbackFunction) 
{
    if( pRemoteControl != nullptr) { return forceRegisterNotificationAndIndication(callbackFunction, pRemoteBackfill, false); }
    return false;
}

bool DexcomConnection::controlRegister() 
{
    if( pRemoteControl != nullptr) { return forceRegisterNotificationAndIndication(indicateControlCallback, pRemoteControl, false); }
    return false;
}



/////////////////////////////////////
//
//      PRIVATE
//
/////////////////////////////////////


/**
 * Write a string to the given characteristic.
 */
bool DexcomConnection::writeValue(String caller, BLERemoteCharacteristic *pRemoteCharacteristic, uint8_t* pData, size_t length)
{
    SerialPrint(DEBUG, caller.c_str());
    SerialPrint(DEBUG, " - Writing Data = ");
    printHexArray(pData, length);
    //BLERemoteCharacteristic: writeValue(std::string newValue, bool response = false);                                 //Not possible to send 0x00 within a string because this method converts std::string to c string using c_str()
    //pRemoteCharacteristic->writeValue(data, true);    /* important must be true so we don't flood the transmitter */  //And a c string ends with a 0x00 so not the full message gets send. (Only the part before the first 0x00 gets send)
    
    /* important must be true so we don't flood the transmitter */
    pRemoteCharacteristic->writeValue(pData, length, true);                                                      // true = wait for response (acknowledgment) from the transmitter.
    return true;
}


/**
 * Register for notification, also check if notification is available.
 */
bool DexcomConnection::registerForNotification(notify_callback _callback, BLERemoteCharacteristic *pBLERemoteCharacteristic)
{
    if (pBLERemoteCharacteristic->canNotify())                                                                          // Check if the characteristic has the potential to notify.
    {
        pBLERemoteCharacteristic->registerForNotify(_callback);
        SerialPrint(DEBUG, " - Registered for notify on UUID: ");
        SerialPrintln(DEBUG, pBLERemoteCharacteristic->getUUID().toString().c_str());
        return true;
    }
    else
    {
        SerialPrint(ERROR, " - Notify NOT available for UUID: ");
        SerialPrintln(ERROR, pBLERemoteCharacteristic->getUUID().toString().c_str());
    }
    return false;
}


/**
 * Register for indication AND notification, without checking.
 */
bool DexcomConnection::forceRegisterNotificationAndIndication(notify_callback _callback, BLERemoteCharacteristic *pBLERemoteCharacteristic, bool isNotify)
{
    pBLERemoteCharacteristic->registerForNotify(_callback, isNotify);                                                   // Register first for indication(/notification) (because this is the correct one)
    pBLERemoteCharacteristic->getDescriptor(BLEUUID((uint16_t)0x2902))->writeValue((uint8_t *)bothOn, 2, true);         // True to wait for acknowledge, set to both, manually set the bytes because there is no such function to set both.
    SerialPrint(DEBUG, " - FORCE registered for indicate and notify on UUID: ");
    SerialPrintln(DEBUG, pBLERemoteCharacteristic->getUUID().toString().c_str());
    return true;
}


/**
 * Register for indication, also check if indications are available.
 */
bool DexcomConnection::registerForIndication(notify_callback _callback, BLERemoteCharacteristic *pBLERemoteCharacteristic)
{
    if (pBLERemoteCharacteristic->canIndicate())
    {
        pBLERemoteCharacteristic->registerForNotify(_callback, false);  // false = indication, true = notification
        SerialPrint(DEBUG, " - Registered for indicate on UUID: ");
        SerialPrintln(DEBUG, pBLERemoteCharacteristic->getUUID().toString().c_str());
        return true;
    }
    else
    {
        SerialPrint(ERROR, " - Indicate NOT available for UUID: ");
        SerialPrintln(ERROR, pBLERemoteCharacteristic->getUUID().toString().c_str());
    }
    return false;
}


void DexcomConnection::commFault(String faultMessage)
{
    errorConnection = true;                         // Set to true to indicate that an error has occurred.
    SerialPrintln(ERROR, faultMessage.c_str());
    pClient->disconnect();   
}