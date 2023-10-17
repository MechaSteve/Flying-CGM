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
#include <Preferences.h>
#include <driver/adc.h>
#include "esp_adc_cal.h"
#include "BLEDevice.h"
#include "BLEScan.h"
#include "DebugHelper.h"
#include "G6DexcomBLE.h"
#include "G6DexcomClient.h"
#include "G6DexcomMFD.h"

#define STATE_START_SCAN 0                                                                                              // Set this state to start the scan.
#define STATE_SCANNING   1                                                                                              // Indicates the esp is currently scanning for devices.
#define STATE_SLEEP      2                                                                                              // Finished with reading data from the transmitter.
#define STATE_WAIT       3
static int Status      = 0;                                                                                             // Set to 0 to automatically start scanning when esp has started.

// This transmitter ID is used to identify our transmitter if multiple dexcom transmitters are found.
// Updated 2023-10-15 to garbage. Create an include file and add to git-ignore.
// #define DEXCOM_CONFIG_DEFAULT_ID "8nXXnn"

/* Enable when used concurrently with xDrip / Dexcom CGM */           // Tells the transmitter to use the alternative bt channel.
#define DEXCOM_CONFIG_DEFAULT_ALT_CH false

/* Enable when problems with connecting */
// When true: disables bonding before auth handshake.
// Enables bonding after successful authenticated (and before bonding command) so transmitter then can initiate bonding.
#define DEXCOM_CONFIG_DEFAULT_FORCE_RE_BONDING false

// Defines the default number of glucose values to store
#define DEXCOM_CONFIG_DEFAULT_X_VALUES_TO_STORE 12

/* Optimization or connecting problems:
 * - pBLEScan->setInterval(100);             10-500 and > setWindow(..)
 * - pBLEScan->setWindow(99);                10-500 and < setInterval(..)
 * - pBLEScan->setActiveScan(false);         true, false
 * - BLEDevice::getScan()->start(0, true)    true, false */


// Variables which survives the deep sleep. Uses RTC_DATA memory.
RTC_DATA_ATTR static boolean error_last_connection = false;
RTC_DATA_ATTR static int glucoseCurrentValue;
// Variables which do not survive reset.
static boolean error_current_connection = false;                                                                        // To detect an error in the current session.
static boolean read_complete = false;



#define RW_MODE false
#define RO_MODE true

Preferences flashStorage;

// static globals for timer and previous values
// these will become static members of the DexomClient class
static uint32_t lastSec = 0; //roll-over proof seconds counter (may loose time when millis rolls)
static uint32_t lastUpdateSec = 0; //sec counter when the last refresh of the TFT was made (limit to ~1s)
static uint32_t lastConnectSec = 0; //sec counter when the last connection was made
static uint32_t lastDataSec = 0; //sec counter when the last data update was made (retained on reset)




/**
 * Method to check the reason the ESP woke up or was started.
 */
void wakeUpRoutine()
{
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER)
    {
        if(error_last_connection)                                                                                   // An error occured last session.
        {
            DexcomSecurity::forceRebondingEnable();
            SerialPrintln(DEBUG, "Error happened in last connection so set force rebonding to true.");
        }                                                                                                           // Otherwise keep the default force_rebonding setting. (could be false or true when changed manually).
        SerialPrintln(DEBUG, "Wakeup caused by timer from hibernation.");                                           // No need to restart / reset variables because all memory is lost after hibernation.
        //printSavedGlucose();                                                                                        // Only potential values available when woke up from deep sleep.
    }
    else
    {
        if (esp_reset_reason() == ESP_RST_SW)
        {
            SerialPrintln(DEBUG, "Reset trigged by software.");
            SerialPrintf(DEBUG, "Glucose value read from RTC memory: %d", glucoseCurrentValue);
            DexcomSecurity::forceRebondingEnable();
        }
        else
        {
            DexcomSecurity::forceRebondingEnable();
        }

    }
    flashStorage.begin("Dexcom", RO_MODE);
    if( !flashStorage.isKey("CurVal"))
    {
        flashStorage.end();
        flashStorage.begin("Dexcom", RW_MODE);
        flashStorage.putInt("CurVal", 0);
        flashStorage.end();
    }
    flashStorage.begin("Dexcom", RO_MODE);
    if( !flashStorage.isKey("DataAge"))
    {
        flashStorage.end();
        flashStorage.begin("Dexcom", RW_MODE);
        flashStorage.putInt("DataAge", 600);
        flashStorage.end();
    }

    // We set default values, so we always read data
    flashStorage.begin("Dexcom", RO_MODE);
    glucoseCurrentValue = flashStorage.getInt("CurVal");
    lastDataSec = flashStorage.getInt("DataAge");
    flashStorage.end();

    DexcomMFD::set_glucoseValue(glucoseCurrentValue);
    DexcomMFD::set_dataAge(lastDataSec);
}

/**
 * Set up the ESP32 ble.
 */
void setup()
{    
    std::string id = DexcomConnection::getTransmitterID();
    DexcomMFD::setupTFT();
    Serial.begin(115200);
    setupLipo();
    Serial.println("Start...");
    Serial.print("Looking for transmitter: ");
    Serial.println((char *)id.c_str());
    wakeUpRoutine();
    DexcomMFD::drawScreen();
    DexcomMFD::drawTime(lastDataSec);
    DexcomMFD::drawVBat(readVBat());
}


/**
 * This is the main loop function.
 */
void loop()
{
    int timeDelta = (millis() / 1000) - lastUpdateSec;
    if (timeDelta > 0) {
      lastUpdateSec += timeDelta;
      saveDataAge(lastDataSec + timeDelta);
      DexcomMFD::drawTime(lastDataSec);
      if (lastUpdateSec / 10 > (lastUpdateSec - timeDelta) / 10) {
        Serial.println("loop");
        DexcomMFD::drawVBat(readVBat());
      }
    }

    switch (Status)
    {
      case STATE_START_SCAN:
        //pBLEScan->start(0, true);                                                                         // false = maybe helps with connection problems.
        DexcomConnection::find();
        if (DexcomConnection::isFound()) Status = STATE_SCANNING;
        //break;

      case STATE_SCANNING:
        if(DexcomConnection::isFound()) {
            // A device (transmitter) was found by the scan (callback).
            // Note the time offset when the device is found.
            lastConnectSec = millis() / 1000;
            run();                                                                                                      // This function is blocking until all tansmitter communication has finished.
            // pBLEScan->clearResults();   // delete results fromBLEScan buffer to release memory
            Status = STATE_WAIT;
            glucoseCurrentValue = DexcomClient::get_glucose();
            DexcomMFD::set_glucoseValue(glucoseCurrentValue);
            lc709203f();
            DexcomMFD::drawScreen();
            DexcomMFD::drawVBat(readVBat());
            lastUpdateSec = millis() / 1000;
            flashStorage.begin("Dexcom", RW_MODE);
            flashStorage.putInt("CurVal", glucoseCurrentValue);
            flashStorage.end();
        }
        break;

      case STATE_WAIT :
        if ((millis() / 1000) - lastConnectSec > 295) {
            esp_restart();
        }
        break;
    }
}

/**
 * This function can be called in an error case.
 */
void ExitState(std::string message)
{
    SerialPrintln(ERROR, message.c_str());
    DexcomConnection::disconnect();                                                                                              // Disconnect to trigger onDisconnect event and go to sleep.
}


/**
 * This method will perform a full transmitter connect and read data.
 */
 // CHANGE this function return type has been changed from bool to void. an unset return causes a reboot
void run()
{
    error_current_connection = false;                                                                                    // Set to false to start.
    read_complete = false;

    if(!DexcomSecurity::forceRebondingEnabled())
        DexcomSecurity::setupBonding();

    Serial.print("Waited ");
    Serial.print(lastConnectSec);
    Serial.println(" seconds. ");

    if (!error_current_connection) {
        Serial.println("try connect");
        error_current_connection = !DexcomConnection::connect();                                                    // Connect to the found transmitter.
        if (error_current_connection) { ExitState("We have failed to connect to the transmitter!"); }
        else { SerialPrintln(DEBUG, "We are now connected to the transmitter."); }
    }

    // Authenticate with the transmitter.
    if (!error_current_connection) {

        Serial.println("try to authenticate");
        error_current_connection = !DexcomSecurity::authenticate();
        if (error_current_connection) { ExitState("Error while trying to authenticate!"); }
        else { SerialPrintln(DEBUG, "Successfully authenticated."); }
    }

    // Enable encryption and requesting bonding.
    if (!error_current_connection) {

        Serial.println("try to bond");
        error_current_connection = !DexcomSecurity::requestBond();
        if (error_current_connection) { ExitState("Error while trying to bond!"); }
        else { SerialPrintln(DEBUG, "Successfully bonded."); }
    }

    // Read the general device informations like model no. and manufacturer.

    if (!error_current_connection) {

        Serial.println("try to read device information");
        error_current_connection = !DexcomConnection::readDeviceInformations();
        if (error_current_connection) { ExitState("Error while reading device informations!"); }    // If empty strings are read from the device information Characteristic, try reading device information after successfully authenticated.
        else { SerialPrintln(DEBUG, "Successfully read device instructions."); }
    }

    // Register the control channel callback.
    if (!error_current_connection) {

        Serial.println("try to register control callback");
        error_current_connection = !DexcomConnection::controlRegister();
        if (error_current_connection) { ExitState("Error while trying to register!"); }
        else { SerialPrintln(DEBUG, "Successfully registered."); }
    }

    // Reading current time from the transmitter (important for backfill).
    if (!error_current_connection) {

        Serial.println("try to read time message");
        error_current_connection = !DexcomClient::readTimeMessage();
        if (error_current_connection) { ExitState("Error reading Time Message!"); }
        else { SerialPrintln(DEBUG, "Successfully read time message."); }
    }

    // Reading current battery status
    if (!error_current_connection) {

        Serial.println("try to read battery status");
        error_current_connection = !DexcomClient::readBatteryStatus();
        if (error_current_connection) { ExitState("Error reading battery status!"); }
        else { SerialPrintln(DEBUG, "Successfully read battery status."); }
    }

    //Read current glucose level to save it.
    if (!error_current_connection) {

        Serial.println("try to read current glucose");
        error_current_connection = !DexcomClient::readGlucose();
        if (error_current_connection) { ExitState("Error reading current glucose!"); }
        else 
        { 
            SerialPrintln(DEBUG, "Successfully read current glucose."); 
            saveDataAge(0);
        }
    }

    if(!error_current_connection) read_complete = true;

    // Optional: read sensor raw (unfiltered / filtered) data.
    //if(!readSensor())
        //SerialPrintln(ERROR, "Can't read raw Sensor values!");

    // Optional: read time and glucose of last calibration.
    //if(!readLastCalibration())
        //SerialPrintln(ERROR, "Can't read last calibration data!");


    if(DexcomClient::needBackfill())
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



// TODO: Update this to configure the analog port for the batery voltage sense rename setupAnalogLiPo
void setupLipo()
{
    Serial.println(F("Setting up analog read of battery voltage"));
    esp_adc_cal_characteristics_t adc_chars;
    esp_adc_cal_value_t val_type = esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1100, &adc_chars);
}

int readVBat()
{
    esp_adc_cal_characteristics_t adc_chars;
    // Get the internal calibration value of the chip
    esp_adc_cal_value_t val_type = esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1100, &adc_chars);
    uint32_t raw = analogRead(PIN_BAT_VOLT);
    uint32_t v1 = esp_adc_cal_raw_to_voltage(raw, &adc_chars) * 2; //The partial pressure is one-half
    Serial.print("Batt_Voltage: ");
    Serial.print(v1);
    Serial.println(" mV");
    return (int)v1;
}

// Replage this with analogLiPo
void lc709203f() {
  Serial.print("Batt_Voltage: unknown");
  // Serial.print(lc.cellVoltage(), 3);
  // DexcomMFD::set_battPct(lc.cellPercent());
}

void saveDataAge(int32_t newAge) {
    flashStorage.begin("Dexcom", RW_MODE);
    lastDataSec = newAge;
    flashStorage.putInt("DataAge", lastDataSec);
    flashStorage.end();
}
