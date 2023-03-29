

#include "G6DexcomClient.h"
#include "DebugHelper.h"


uint16_t DexcomClient::currentBG = 0;
int DexcomClient::saveLastXValues = 12;
uint16_t DexcomClient::glucoseValues[72] = {   0,0,0,0 ,0,0,0,0 ,0,0,0,0 ,0,0,0,0 ,0,0,0,0 ,0,0,0,0
                                ,0,0,0,0 ,0,0,0,0 ,0,0,0,0 ,0,0,0,0 ,0,0,0,0 ,0,0,0,0
                                ,0,0,0,0 ,0,0,0,0 ,0,0,0,0 ,0,0,0,0 ,0,0,0,0 ,0,0,0,0};
std::string DexcomClient::backfillStream = "";
int DexcomClient::backfillExpectedSequence = 0;

/**
 * Calculate crc16 check sum for the given string.
 */
uint16_t DexcomClient::CRC_16_XMODEM(uint8_t* pData, size_t length)
{
    uint16_t crc = ~crc16_be((uint16_t)~0x0000, pData, length);       // calculate crc 16 xmodem
    uint8_t crcArray[2] = { (uint8_t)crc, (uint8_t)(crc >> 8) };                                                        // proper way of converting our bytes to string
    uint16_t crcSwapped =  0x100*crcArray[1] + crcArray[0];

    SerialPrint(DEBUG, "CRC_16_XMODEM of ");
    for (int i = 0; i < length; i++)
    {
        SerialPrint(DEBUG, pData[i], HEX);
        SerialPrint(DEBUG, " ");
    }
    SerialPrint(DEBUG, "is ");
    printHexArray(crcArray, 2);
    return crcSwapped;
}

/**
 * Returns true if invalid data was found / missing values or not x values are available.
 */
bool DexcomClient::needBackfill()
{
    if (DexcomConnection::lastConnectionWasError()) return true;                                                                            // Also request backfill if last time was an error (maybe error while backfilling so missed some data).

    for(int i = 0; i < saveLastXValues; i++)
    {
        if(glucoseValues[i] < 10 || glucoseValues[i] > 600)                                                             // This includes 0 values from initialisation.
            return true;
    }
    return false; // no reason to backfill
}


uint32_t transmitterElapsedTime = 0;
uint32_t sensorElapsedTime = 0;
/**
 * Read the time information from the transmitter.
 */
bool DexcomClient::readTimeMessage()
{
    uint8_t timeTxMessage[3] = {0x24, 0xE6, 0x64};
    DexcomConnection::ControlSendValue(timeTxMessage, 3);
    uint8_t timeRxBuffer[20];
    size_t timeRxLength = DexcomConnection::ControlWaitToReceiveValue(timeRxBuffer, 20);
    if ((timeRxLength != 16) || timeRxBuffer[0] != 0x25)
        return false;

    uint8_t status = timeRxBuffer[1];
    uint32_t currentTime = 0;               // seconds since transmitter activation
    uint32_t sessionStartTime = 0;          // currentTime when sensor was started
    memcpy(&currentTime, &(timeRxBuffer[2]), 4);
    memcpy(&sessionStartTime, &(timeRxBuffer[6]), 4);
    uint32_t sessionElapsedTime = currentTime - sessionStartTime;
    uint32_t sessionRemainingTime = (10*24*60*60) -  sessionElapsedTime;
    SerialPrintf(DATA, "Time - Status:              %d\n\r", status);
    SerialPrintf(DATA, "Time - since activation:    %d (%d days, %d hours)\n\r", currentTime,                             // Activation date is now() - currentTime * 1000
                                                                         currentTime / (60*60*24),                      // Days round down
                                                                         (currentTime / (60*60)) % 24);                 // Remaining hours
    SerialPrintf(DATA, "Time - since session start:    %d (%d days, %d hours)\n\r", sessionElapsedTime,                             // Session start = Activation date + sessionStartTime * 1000
                                                                         sessionElapsedTime / (60*60*24),                      // Days round down
                                                                         (sessionElapsedTime / (60*60)) % 24);                 // Remaining hours
    SerialPrintf(DATA, "Time - remaining in session:    %d (%d days, %d hours, %d minutes)\n\r", sessionRemainingTime,                             // Session start = Activation date + sessionStartTime * 1000
                                                                         sessionRemainingTime / (60*60*24),                      // Days round down
                                                                         (sessionRemainingTime / (60*60)) % 24,                 // Remaining hours
                                                                         (sessionRemainingTime / 60) % 60 );                 // Remaining minutes

    if(status == 0x81)                                                                                                  // readTimeMessage is first request where we get the status code
        SerialPrintln(DEBUG, "\nWARNING - Low Battery\n\r");                                                              // So show a message when low battery / expired.
    if(status == 0x83)
        SerialPrintln(DEBUG, "\nWARNING - Transmitter Expired\n\r");
    transmitterElapsedTime = currentTime;
    sensorElapsedTime = sessionElapsedTime;
    return true;
}

/**
 * Read the Battery values.
 */
bool DexcomClient::readBatteryStatus()
{
    SerialPrintln(DEBUG, "Reading Battery Status.");
    uint8_t batteryStatusTxMessage[3] ={0x22, 0x20, 0x04};
    DexcomConnection::ControlSendValue(batteryStatusTxMessage, 3);
    uint8_t batteryStatusRxBuffer[16];
    size_t batteryStatusRxLength = DexcomConnection::ControlWaitToReceiveValue(batteryStatusRxBuffer, 16);
    if(!(batteryStatusRxLength == 10 || batteryStatusRxLength == 12) ||
         batteryStatusRxBuffer[0] != 0x23)
        return false;

    SerialPrintf(DATA, "Battery - Status:      %d\n\r", (uint8_t)batteryStatusRxBuffer[1]);
    SerialPrintf(DATA, "Battery - Voltage A:   %d\n\r", (uint16_t)(batteryStatusRxBuffer[2] + batteryStatusRxBuffer[3]*0x100));
    SerialPrintf(DATA, "Battery - Voltage B:   %d\n\r", (uint16_t)(batteryStatusRxBuffer[4] + batteryStatusRxBuffer[5]*0x100));
    if(batteryStatusRxLength == 12)                                                                           // G5 or G6 Transmitter.
    {
        SerialPrintf(DATA, "Battery - Resistance:  %d\n\r", (uint16_t)(batteryStatusRxBuffer[6] + batteryStatusRxBuffer[7]*0x100));
        SerialPrintf(DATA, "Battery - Runtime:     %d\n\r", (uint8_t)batteryStatusRxBuffer[8]);
        SerialPrintf(DATA, "Battery - Temperature: %d\n\r", (uint8_t)batteryStatusRxBuffer[9]);
    }
    else if(batteryStatusRxLength == 10)                                                                      // G6 Plus Transmitter.
    {
        SerialPrintf(DATA, "Battery - Runtime:     %d\n\r", (uint8_t)batteryStatusRxBuffer[6]);
        SerialPrintf(DATA, "Battery - Temperature: %d\n\r", (uint8_t)batteryStatusRxBuffer[7]);
    }
    return true;
}

/**
 * Reads the glucose values from the transmitter.
 */
bool DexcomClient::readGlucose()
{
    uint8_t glucoseTxMessageG5[3] = {0x30, 0x53, 0x36};                                                                // G5 = 0x30 the other 2 bytes are the CRC16 XMODEM value in twisted order
    uint8_t glucoseTxMessageG6[3] = {0x4e, 0x0a, 0xa9};                                                                // G6 = 0x4e
    std::string transmitterID = DexcomConnection::getTransmitterID();
    if(transmitterID[0] == 8 || (transmitterID[0] == 2 && transmitterID[1] == 2 && transmitterID[2] == 2))              // Check if G6 or one of the newest G6 plus (>2.18.2.88) see https://github.com/xdrip-js/xdrip-js/issues/87
        DexcomConnection::ControlSendValue(glucoseTxMessageG6, 3);
    else
        DexcomConnection::ControlSendValue(glucoseTxMessageG5, 3);

    uint8_t glucoseRxBuffer[20];
    size_t glucoseRxLength = DexcomConnection::ControlWaitToReceiveValue(glucoseRxBuffer, 20);
    if (glucoseRxLength < 16 || glucoseRxBuffer[0] != (transmitterID[0] != 8 ? 0x31 : 0x4f))                 // Opcode depends on G5 / G6
        return false;

    uint8_t status = glucoseRxBuffer[1];
    uint32_t sequence  = (uint32_t)(glucoseRxBuffer[2] +
                                    glucoseRxBuffer[3]*0x100  +
                                    glucoseRxBuffer[4]*0x10000 +
                                    glucoseRxBuffer[5]*0x1000000);
    uint32_t timestamp = (uint32_t)(glucoseRxBuffer[6] +
                                    glucoseRxBuffer[7]*0x100  +
                                    glucoseRxBuffer[8]*0x10000 +
                                    glucoseRxBuffer[9]*0x1000000);

    uint16_t glucoseBytes = (uint16_t)(glucoseRxBuffer[10] +
                                       glucoseRxBuffer[11]*0x100);
    boolean glucoseIsDisplayOnly = (glucoseBytes & 0xf000) > 0;
    uint16_t glucose = glucoseBytes & 0xfff;
    uint8_t state = glucoseRxBuffer[12];
    int trend = glucoseRxBuffer[13];
    if(state != 0x06)                                                                                                   // Not the ok state -> exit
    {
        SerialPrintf(ERROR, "\nERROR - Session Status / State NOT OK (%d)!\n\r", state);
        DexcomConnection::commFault("ERROR - We will not continue due to safety reasons (warmup, stopped, waiting for calibration(s), failed or expired.\n\r");
    }

    SerialPrintf(DATA, "Glucose - Status:      %d\n\r", status);
    SerialPrintf(DATA, "Glucose - Sequence:    %d\n\r", sequence);
    SerialPrintf(DATA, "Glucose - Timestamp:   %d\n\r", timestamp);                                                       // Seconds since transmitter activation
    SerialPrintf(DATA, "Glucose - DisplayOnly: %s\n\r", (glucoseIsDisplayOnly ? "true" : "false"));
    SerialPrintf(GLUCOSE, "Glucose - Glucose:     %d\n\r", glucose);
    SerialPrintf(DATA, "Glucose - State:       %d\n\r", state);
    SerialPrintf(DATA, "Glucose - Trend:       %d\n\r", trend);

    if(saveLastXValues > 0)                                                                                             // Array is big enough for min one value.
    {
        for(int i = saveLastXValues - 1; i > 0; i--)                                                                    // Shift all old values back to set the newest to position 0.
            glucoseValues[i] = glucoseValues[i-1];
        glucoseValues[0] = glucose;
    }
    return true;
}

/**
 * Reads the Sensor values like filtered / unfiltered raw data from the transmitter.
 */
bool DexcomClient::readSensor()
{
    uint8_t sensorTxMessage[3] = {0x2e, 0xac, 0xc5};
    DexcomConnection::ControlSendValue(sensorTxMessage, 3);
    uint8_t sensorRxBuffer[18];
    size_t sensorRxLength = DexcomConnection::ControlWaitToReceiveValue(sensorRxBuffer, 18);
    if((sensorRxLength != 16 && sensorRxLength != 8) || sensorRxBuffer[0] != 0x2f)
        return false;

    uint8_t status = (uint8_t)sensorRxBuffer[1];
    uint32_t timestamp = (uint32_t)(sensorRxBuffer[2] +
                                    sensorRxBuffer[3]*0x100  +
                                    sensorRxBuffer[4]*0x10000 +
                                    sensorRxBuffer[5]*0x1000000);
    SerialPrintf(DATA, "Sensor - Status:     %d\n\r", status);
    SerialPrintf(DATA, "Sensor - Timestamp:  %d\n\r", timestamp);
    if (sensorRxLength > 8)
    {
        uint32_t unfiltered = (uint32_t)(sensorRxBuffer[6] +
                                         sensorRxBuffer[7]*0x100  +
                                         sensorRxBuffer[8]*0x10000 +
                                         sensorRxBuffer[9]*0x1000000);
        uint32_t filtered   = (uint32_t)(sensorRxBuffer[10] +
                                         sensorRxBuffer[11]*0x100  +
                                         sensorRxBuffer[12]*0x10000 +
                                         sensorRxBuffer[13]*0x1000000);
        if (DexcomConnection::getTransmitterID()[0] == 8)                                                                                      // G6 Transmitter
        {
                int g6Scale = 34;
                unfiltered *= g6Scale;
                filtered *= g6Scale;
        }
        SerialPrintf(DATA, "Sensor - Unfiltered: %d\n\r", unfiltered);
        SerialPrintf(DATA, "Sensor - Filtered:   %d\n\r", filtered);
    }

  return true;
}

/**
 * Reads out the last glucose calibration value.
 */
bool DexcomClient::readLastCalibration()
{
    uint8_t calibrationDataTxMessage[3] = {0x32, 0x11, 0x16};
    DexcomConnection::ControlSendValue(calibrationDataTxMessage, 3);
    uint8_t calibrationDataRxBuffer[22];
    size_t calibrationDataRxLength = DexcomConnection::ControlWaitToReceiveValue(calibrationDataRxBuffer, 22);
    if ((calibrationDataRxLength != 19 && calibrationDataRxLength != 20) ||
        (calibrationDataRxBuffer[0] != 0x33))
    return false;

    uint16_t glucose   = (uint16_t)(calibrationDataRxBuffer[11] + calibrationDataRxBuffer[12]*0x100);
    uint32_t timestamp = (uint32_t)(calibrationDataRxBuffer[13] +
                                    calibrationDataRxBuffer[14]*0x100  +
                                    calibrationDataRxBuffer[15]*0x10000 +
                                    calibrationDataRxBuffer[16]*0x1000000);
    SerialPrintf(DATA, "Calibration - Glucose:   %d\n\r", glucose);
    SerialPrintf(DATA, "Calibration - Timestamp: %d\n\r", timestamp);

  return true;
}

/**
 * Reads the last glucose values from the transmitter when the esp was not connected.
 */
bool DexcomClient::readBackfill()
{
    if(transmitterElapsedTime == 0)                                                                                       // The read time command must be send first to get the current time.
        return false;

    backfillStream = "";                                                                                                // Empty the backfill stream.
    backfillExpectedSequence = 1;                                                                                       // Set to the first message.

    uint8_t backfillTxBuffer[20] = { 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0};
    //uint8_t backfill_opcode[4] = {0x50, 0x05, 0x02, 0x00};                                   // 12 + 6 byte fill + 2 byte crc = 20 byte
    uint32_t backfill_opcode = 0x00020550;
    // Set backfill_start to 0 to get all values of the last ~150 measurements (~12,5h)
    uint32_t backfill_start = transmitterElapsedTime - (saveLastXValues * 5) * 60;                                        // Get the last x values. Only need x-1 because we already have the current value but request one more to be sure that we get x-1.
    uint32_t backfill_end   = transmitterElapsedTime - 60;                                                                // Do not request the current value. (But is not anyway available by backfill)


    memcpy(&backfillTxBuffer[0], &backfill_opcode, 4);
    memcpy(&backfillTxBuffer[4], &backfill_start, 4);
    memcpy(&backfillTxBuffer[8], &backfill_end, 4);

    //backfillTxMessage += {0, 0, 0, 0, 0, 0};                                                          // Fill up to 18 byte.
    uint16_t backfill_crc = CRC_16_XMODEM(backfillTxBuffer, 18);                                            // Add crc 16.
    memcpy(&backfillTxBuffer[18], &backfill_crc, 2);

	SerialPrintf(DEBUG,  "Request backfill from %d to %d (current %d).\n\r", backfill_start, backfill_end, transmitterElapsedTime);
    DexcomConnection::ControlSendValue(backfillTxBuffer, 20);

    SerialPrintln(DATA, "Waiting for backfill data...");
    uint8_t backfillRxBuffer[22];
    size_t backfillRxLength = DexcomConnection::ControlWaitToReceiveValue(backfillRxBuffer, 22);        // We will receive this normally after all backfill data has been send by the transmitter.
    if (backfillRxLength != 20 || backfillRxBuffer[0] != 0x51)
        return false;

    uint8_t status          = backfillRxBuffer[1];
    uint8_t backFillStatus  = backfillRxBuffer[2];
    uint8_t identifier      = backfillRxBuffer[3];
    uint32_t timestampStart = (uint32_t)(backfillRxBuffer[4] +
                                         backfillRxBuffer[5]*0x100  +
                                         backfillRxBuffer[6]*0x10000 +
                                         backfillRxBuffer[7]*0x1000000);
    uint32_t timestampEnd   = (uint32_t)(backfillRxBuffer[8] +
                                         backfillRxBuffer[9]*0x100  +
                                         backfillRxBuffer[10]*0x10000 +
                                         backfillRxBuffer[11]*0x1000000);
    SerialPrintf(DATA, "Backfill - Status:          %d\n\r", status);
    SerialPrintf(DATA, "Backfill - Backfill Status: %d\n\r", backFillStatus);
    SerialPrintf(DATA, "Backfill - Identifier:      %d\n\r", identifier);
    SerialPrintf(DATA, "Backfill - Timestamp Start: %d\n\r", timestampStart);
    SerialPrintf(DATA, "Backfill - Timestamp End:   %d\n\r", timestampEnd);
    printSavedGlucose();

    delay(2*1000);                                                                                                      // Wait 2 seconds to be sure that all backfill data has arrived.
    return true;
}

/**
 * Prints out the last x glucose levels.
 */
void DexcomClient::printSavedGlucose()
{
    SerialPrintf(GLUCOSE, "Last %d glucose values (current -> past):\n", saveLastXValues);
    for(int i = 0; i < saveLastXValues; i++)
    {
        if(glucoseValues[i] == 0)                                                                                       // The initialisation value.
            break;
        SerialPrintf(GLUCOSE, "%d ", glucoseValues[i]);
    }
    SerialPrintln(GLUCOSE, "");
}

/**
 * This method saves the backfill data received from the backfill characteristic callback.
 */
bool DexcomClient::saveBackfill(std::string backfillParseMessage)
{
    if (backfillParseMessage.length() < 2)                                                                              // Minimum is sequence + identifier.
        return false;

    uint8_t sequence   = (uint8_t)backfillParseMessage[0];
    uint8_t identifier = (uint8_t)backfillParseMessage[1];

    if(sequence != backfillExpectedSequence)
    {
        SerialPrintln(ERROR,  "Backfill Data Error - WRONG ORDER...\n\r");
        backfillExpectedSequence = 0;                                                                                   // After one out of order package the other packages can't be used.
        return false;
    }
    backfillExpectedSequence += 1;

    if(sequence == 1)
    {
        uint16_t backfillRequestCounter = (uint16_t)(backfillParseMessage[2] + backfillParseMessage[3]*0x100);
        uint16_t unknown                = (uint16_t)(backfillParseMessage[4] + backfillParseMessage[5]*0x100);
        SerialPrintf(DATA,  "Backfill Data - Request Counter: %d\n\r", backfillRequestCounter);
        SerialPrintf(DATA,  "Backfill Data - Unknown:         %d\n\r", unknown);
        backfillStream = backfillParseMessage.substr(6);                                                                // Empty string and set payload.
    }
    else
        backfillStream += backfillParseMessage.substr(2);                                                               // Add data.

    //SerialPrintf(DATA, "Backfill Data - Sequence: %d   Identifier: %d   Data: ", sequence, identifier);
    //printHexString(backfillParseMessage);

    while(backfillStream.length() >= 8)
    {
        std::string data = backfillStream.substr(0,8);                                                                  // Get the first 8 byte.
        if(backfillStream.length() > 8)                                                                                 // More than 8 byte?
            backfillStream = backfillStream.substr(8);                                                                  // Trim of the first 8 byte.
        else                                                                                                            // Exactly 8 byte:
            backfillStream = "";                                                                                        // Empty string.
        parseBackfill(data);
    }
    return true;
}

/**
 * This method parsed 8 bytes representing the timestamp and glucose values.
 */
void DexcomClient::parseBackfill(std::string data)
{
    uint32_t dextime = (uint32_t)(data[0] +
                                  data[1]*0x100  +
                                  data[2]*0x10000 +
                                  data[3]*0x1000000);
    uint16_t glucose = (uint16_t)(data[4] + data[5]*0x100);
    uint8_t type     = (uint8_t)data[6];
    uint8_t trend    = (uint8_t)data[7];

    if(saveLastXValues > 1)                                                                                             // Array is big enough for min 1 backfill value (and the current value).
    {
        for(int i = saveLastXValues - 1; i > 1; i--)                                                                    // Shift all old values (but not the first) back to save these.
            glucoseValues[i] = glucoseValues[i-1];
        glucoseValues[1] = glucose;
    }

    SerialPrintf(GLUCOSE,  "Backfill -> Dextime: %d   Glucose: %d   Type: %d\n\r", dextime, glucose, type);
}

int DexcomClient::get_glucose()
{
    return glucoseValues[0] > 0 ? glucoseValues[0] : -1;
}
