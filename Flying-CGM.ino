/**
 * A ESP32 BLE client that can read (glucose, raw, ..) data from the dexcom G6 (G5) transmitter.
 * 
 * Developed in the context of my bachelor thesis at the Technical University (TU) of Darmstadt 
 * under the supervision of the Telecooperation Lab.
 * 
 * Specifications Hardware / Software:
 * - ESP32-WROOM-32D (ESP32_DevKitc_V4)
 * - espressif v1.0.4  (https://dl.espressif.com/dl/package_esp32_index.json)
 * - Arduino Studio 1.8.10
 * - Dexcom G6 Transmitter 81xxxx (Model: SW11163, Firmware: 1.6.5.25 / 1.6.13.139)
 * - Dexcom G6 Plus Transmitter 8Gxxxx (unknown)
 * 
 * Author: Max Kaiser
 * Copyright (c) 2020
 * 28.05.2020
 */


// STEPHEN'S NOTES
//
// I can't seem to get it to connect twice in a row
//
// the old version was accidentally rebooting the controller, and that was allowing it to connect again
// I think that may be the fastest hack. I can get the past 12 values, which is more than enough to 
// set the current value and trend, the only trick will be to not glitch the TFT too much on reboot.
//
// best way to avoid glithching would be to just reset the BLE module to whatever state it is in on reboot, and clear any other variables
// 
// worst hack would be to save last received values in flash (along with dextime value)
//
//
// THEORY: The transmitter is attempting to connect to esp32 and not getting auth'ed in time
//
// add variable to keep track of bonding setup complete, and do not re-init the BLEDevice
//
// run function needs an error path
// instead of just haveing a single line if not exit state, make entire logic into a large nested if statement.
//  the true path runs the next step, and the else runs exit state.
 
#include <Arduino.h>
#include <Esp.h>
#include "BLEDevice.h"
#include "BLEScan.h"
#include "DebugHelper.h"
#include "G6DexcomBLE.h"
#include "G6DexcomClient.h"

#define STATE_START_SCAN 0                                                                                              // Set this state to start the scan.
#define STATE_SCANNING   1                                                                                              // Indicates the esp is currently scanning for devices.
#define STATE_SLEEP      2                                                                                              // Finished with reading data from the transmitter.
#define STATE_WAIT       3
static int Status      = 0;                                                                                             // Set to 0 to automatically start scanning when esp has started.

static BLEUUID advServiceUUID("0000febc-0000-1000-8000-00805f9b34fb");                                                  // This service gets advertised by the transmitter.


static BLEScan* pBLEScan;
static std::string transmitterID = "8XC0FT";              /* Set here your transmitter ID */                            // This transmitter ID is used to identify our transmitter if multiple dexcom transmitters are found.
static boolean useAlternativeChannel = false;      /* Enable when used concurrently with xDrip / Dexcom CGM */           // Tells the transmitter to use the alternative bt channel.
static boolean bonding = false;                                                                                         // Gets set by Auth handshake "StatusRXMessage" and shows if the transmitter would like to bond with the client.
static boolean force_rebonding = false;               /* Enable when problems with connecting */                        // When true: disables bonding before auth handshake. Enables bonding after successful authenticated (and before bonding command) so transmitter then can initiate bonding.
/* Optimization or connecting problems: 
 * - pBLEScan->setInterval(100);             10-500 and > setWindow(..)
 * - pBLEScan->setWindow(99);                10-500 and < setInterval(..)
 * - pBLEScan->setActiveScan(false);         true, false
 * - BLEDevice::getScan()->start(0, true)    true, false */


// Variables which survives the deep sleep. Uses RTC_SLOW memory.
#define saveLastXValues 12                                                                                              // This saves the last x glucose levels by requesting them through the backfill request.
RTC_SLOW_ATTR static uint16_t glucoseValues[saveLastXValues] = {0};                                                     // Reserve space for 1 hour a 5 min resolution of glucose values.
RTC_SLOW_ATTR static boolean error_last_connection = false;
static boolean error_current_connection = false;                                                                        // To detect an error in the current session.
static boolean read_complete = false;

// Shared variables (used in the callbacks)
static volatile boolean connected = false;                                                                              // Indicates if the ble client is connected to the transmitter. Used to detect a transmitter timeout.
static std::string AuthCallbackResponse = "";
static std::string ControlCallbackResponse = "";
// Use "volatile" so that the compiler does not optimise code related with
// this variable and delete the empty while loop which is used as a barrier.
static volatile boolean bondingFinished = false;                                                                        // Get set when the bonding has finished, does not indicates if it was successful.

static BLEAdvertisedDevice* myDevice = NULL;                                                                            // The remote device (transmitter) found by the scan and set by scan callback function.
static BLEClient* pClient = NULL;                                                                                       // Is global so we can disconnect everywhere when an error occured.

// static globals for timer and previous values
// these will become static members of the DexomClient class
static uint32_t lastSec = 0; //roll-over proof seconds counter (may loose time when millis rolls)
static uint32_t lastReportSec = 0; //sec counter when the last refresh of the CGM was made 
static uint32_t lastUpdateSec = 0; //sec counter when the last refresh of the TFT was made (limit to ~10s)
static uint32_t lastConnectSec = 0; //sec counter when the last connection was made

/**
 * Scan for BLE servers and find the first one that advertises the service we are looking for.
 * Also check that the transmitter has the ID in the bluetooth name so that we connect only to this 
 * dexcom transmitter (if multiple are around).
 */
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks 
{
    void onResult(BLEAdvertisedDevice advertisedDevice)                                                                 // Called for each advertising BLE server.
    {
        SerialPrint(DEBUG, "BLE Advertised Device found: ");
        SerialPrintln(DEBUG, advertisedDevice.toString().c_str());

        // We have found a device, let us now see if it contains the service we are looking for.
        if (advertisedDevice.haveServiceUUID() && advertisedDevice.isAdvertisingService(advServiceUUID) &&              // If the advertised service is the dexcom advertise service (not the main service that contains the characteristics).
            advertisedDevice.haveName() && advertisedDevice.getName() == ("Dexcom" + transmitterID.substr(4,2)))
        {
            pBLEScan->stop();                                                                               // We found our transmitter so stop scanning for now.
            SerialPrintln(DEBUG, "Found Dexcom");
            myDevice = new BLEAdvertisedDevice(advertisedDevice);                                                       // Save device as new copy, myDevice also triggers a state change in main loop.
        }
    } 
};

/**
 * Method to check the reason the ESP woke up or was started.
 */
void wakeUpRoutine() 
{
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    switch(wakeup_reason)
    {
        case ESP_SLEEP_WAKEUP_TIMER :
            if(error_last_connection)                                                                                   // An error occured last session.
            {
                force_rebonding = true;
                SerialPrintln(DEBUG, "Error happened in last connection so set force rebonding to true.");
            }                                                                                                           // Otherwise keep the default force_rebonding setting. (could be false or true when changed manually).
            SerialPrintln(DEBUG, "Wakeup caused by timer from hibernation.");                                           // No need to restart / reset variables because all memory is lost after hibernation.
            //printSavedGlucose();                                                                                        // Only potential values available when woke up from deep sleep.
            break;
        default :
            force_rebonding = true;                                                                                     // Force bonding when esp first started after power off (or flash).
            SerialPrintln(DEBUG, "Wakeup was not caused by deep sleep (normal start).");                                // Problem with allways this case? See https://forum.mongoose-os.com/discussion/1628/tg0wdt-sys-reset-immediately-after-waking-from-deep-sleep
            break;
    }
}

/**
 * Returns true if invalid data was found / missing values or not x values are available.
 */
bool needBackfill()
{
    if (DexcomConnection::lastConnectionWasError()) return true;                                                                            // Also request backfill if last time was an error (maybe error while backfilling so missed some data).
    
    for(int i = 0; i < saveLastXValues; i++)
    {
        if(glucoseValues[i] < 10 || glucoseValues[i] > 600)                                                             // This includes 0 values from initialisation.
            return true;
    }
    return false; // no reason to backfill
}

/**
 * Set up the ESP32 ble.
 */
void setup() 
{
    Serial.begin(115200);
    SerialPrintln(DEBUG, "Start...");
    delay(5000);
    SerialPrintln(DEBUG, "5s delay complete...");
    SerialPrintln(DEBUG, "Starting wake-up routine...");
    wakeUpRoutine();
    SerialPrintln(DEBUG, "Starting ESP32 dexcom client application...");
    //BLEDevice::init("");                                                                                    // Possible source of error if we cant connect to the transmitter.
    //BLEScannerSetup();
}


/**
 * This is the main loop function.
 */
void loop() 
{
    if ((millis() / 1000) - lastUpdateSec > 10) {
      lastUpdateSec = millis() / 1000;
      Serial.println("lp");
    }
      
    switch (Status)
    {
      case STATE_START_SCAN:
        pBLEScan->start(0, true);                                                                         // false = maybe helps with connection problems.
        Status = STATE_SCANNING;
        //break;

      case STATE_SCANNING:
        if(myDevice != NULL) {
          // A device (transmitter) was found by the scan (callback).
          run();                                                                                                      // This function is blocking until all tansmitter communication has finished.
          // pBLEScan->clearResults();   // delete results fromBLEScan buffer to release memory
          Status = STATE_WAIT;
        }
        break;

      case STATE_WAIT :
        if ((millis() / 1000) - lastReportSec > 270) {
          myDevice = NULL;
          BLEDevice::init("");                                                                                    // Possible source of error if we cant connect to the transmitter.
          BLEScannerSetup();
          Status = STATE_START_SCAN;
        }
        break;
          
    }
    
    
}


/**
 * Setup the scanner
 */
 void BLEScannerSetup() {
    pBLEScan = BLEDevice::getScan();                                                                           // Retrieve a Scanner.
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());                                          // Set the callback to informed when a new device was detected.
    pBLEScan->setInterval(100); //100 works                                                                             // The time in ms how long each search intervall last. Important for fast scanning so we dont miss the transmitter waking up.
    pBLEScan->setWindow(99); //60-99 works                                                                              // The actual time that will be searched. Interval - Window = time the esp is doing nothing (used for energy efficiency).
    pBLEScan->setActiveScan(false); 
    }

/**
 * This function can be called in an error case.
 */
void ExitState(std::string message)
{
    error_current_connection = true;                                                                                    // Set to true to indicate that an error has occured.
    SerialPrintln(ERROR, message.c_str());
    pClient->disconnect();                                                                                              // Disconnect to trigger onDisconnect event and go to sleep.
}
/**
 * This method will perform a full transmitter connect and read data.
 */
 // CHANGE this function return type has been changed from bool to void. an unset return causes a reboot
void run()
{
    error_current_connection = false;                                                                                    // Set to false to start.
    read_complete = false;

    if(!force_rebonding)
        DexcomSecurity::setupBonding();                                                                                                // Enable bonding from the start on, so transmitter does not want to (re)bond.


    lastConnectSec = millis() / 1000;
    Serial.print("Waited ");
    Serial.print(lastConnectSec - lastReportSec);
    Serial.println(" seconds. ");
    
    if (!error_current_connection) {
        Serial.println("try connect");
        error_current_connection = !DexcomConnection::connect();                                                    // Connect to the found transmitter.
        if (error_current_connection) { ExitState("We have failed to connect to the transmitter!"); }
        else { SerialPrintln(DEBUG, "We are now connected to the transmitter."); }
    }
    
    if(!DexcomConnection::readDeviceInformations())                                                                 // Read the general device informations like model no. and manufacturer.
        SerialPrintln(DEBUG, "Error while reading device informations!");                                               // If empty strings are read from the device information Characteristic, try reading device information after successfully authenticated. 

    if(!authenticate())                                                                                                 // Authenticate with the transmitter.
        ExitState("Error while trying to authenticate!");
    
    if(!DexcomSecurity::requestBond())                                                                                                  // Enable encryption and requesting bonding.
        ExitState("Error while trying to bond!");
    
    DexcomConnection::controlRegister();                             // Now register (after auth) to receive new data on the control characteristic.

    // Reading current time from the transmitter (important for backfill).
    if(!DexcomClient::readTimeMessage())
        SerialPrintln(ERROR, "Error reading Time Message!");
    
    // Optional: reading battery status.
    //if(!readBatteryStatus())
        //SerialPrintln(ERROR, "Can't read Battery Status!");

    //Read current glucose level to save it.
    if(!DexcomClient::readGlucose())
        SerialPrintln(ERROR, "Can't read Glucose!");

    // Optional: read sensor raw (unfiltered / filtered) data.
    //if(!readSensor())
        //SerialPrintln(ERROR, "Can't read raw Sensor values!");

    // Optional: read time and glucose of last calibration.
    //if(!readLastCalibration())
        //SerialPrintln(ERROR, "Can't read last calibration data!");

    if(needBackfill())
    {
        DexcomConnection::backfillRegister();                         // Now register on the backfill characteristic.       
        // Read backfill of the last x values to also saves them.
        if(!DexcomClient::readBackfill())
            SerialPrintln(ERROR, "Can't read backfill data!");
    }
                                                                                  // When we reached this point no error occured.
    //Let the Transmitter close the connection.
    DexcomConnection::disconnect();
}
