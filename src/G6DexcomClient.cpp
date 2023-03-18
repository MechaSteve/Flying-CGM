#include "G6DexcomClient.h"

/**
 * Calculate crc16 check sum for the given string.
 */
std::string DexcomClient::CRC_16_XMODEM(std::string message)
{
    uint16_t crc = ~crc16_be((uint16_t)~0x0000, reinterpret_cast<const uint8_t*>(&message[0]), message.length());       // calculate crc 16 xmodem 
    uint8_t crcArray[2] = { (uint8_t)crc, (uint8_t)(crc >> 8) };                                                        // proper way of converting our bytes to string
    std::string crcString = reinterpret_cast<char *>(crcArray);

    SerialPrint(DEBUG, "CRC_16_XMODEM of ");
    for (int i = 0; i < message.length(); i++)
    {
        SerialPrint(DEBUG, (uint8_t)message[i], HEX);
        SerialPrint(DEBUG, " ");
    }
    SerialPrint(DEBUG, "is ");
    printHexString(crcString);
    return crcString;
}

uint32_t transmitterStartTime = 0;
/**
 * Read the time information from the transmitter.
 */
bool DexcomClient::readTimeMessage()
{
    std::string transmitterTimeTxMessage = {0x24, 0xE6, 0x64}; 
    ControlSendValue(transmitterTimeTxMessage);
    std::string transmitterTimeRxMessage = ControlWaitToReceiveValue();
    if ((transmitterTimeRxMessage.length() != 16) || transmitterTimeRxMessage[0] != 0x25)
        return false;
    
    uint8_t status = (uint8_t)transmitterTimeRxMessage[1];
    uint32_t currentTime = (uint32_t)(transmitterTimeRxMessage[2] + 
                                      transmitterTimeRxMessage[3]*0x100  + 
                                      transmitterTimeRxMessage[4]*0x10000 + 
                                      transmitterTimeRxMessage[5]*0x1000000);
    uint32_t sessionStartTime = (uint32_t)(transmitterTimeRxMessage[6] + 
                                           transmitterTimeRxMessage[7]*0x100  + 
                                           transmitterTimeRxMessage[8]*0x10000 + 
                                           transmitterTimeRxMessage[9]*0x1000000);
    SerialPrintf(DATA, "Time - Status:              %d\n\r", status);
    SerialPrintf(DATA, "Time - since activation:    %d (%d days, %d hours)\n\r", currentTime,                             // Activation date is now() - currentTime * 1000
                                                                         currentTime / (60*60*24),                      // Days round down
                                                                         (currentTime / (60*60)) % 24);                 // Remaining hours
    SerialPrintf(DATA, "Time - since session start: %d\n\r", sessionStartTime);                                           // Session start = Activation date + sessionStartTime * 1000

    if(status == 0x81)                                                                                                  // readTimeMessage is first request where we get the status code
        SerialPrintln(DEBUG, "\nWARNING - Low Battery\n\r");                                                              // So show a message when low battery / expired.
    if(status == 0x83)
        SerialPrintln(DEBUG, "\nWARNING - Transmitter Expired\n\r");
    transmitterStartTime = currentTime;
    return true;
}

/**
 * Read the Battery values.
 */
bool DexcomClient::readBatteryStatus()
{
    SerialPrintln(DEBUG, "Reading Battery Status.");
    std::string batteryStatusTxMessage ={0x22, 0x20, 0x04};
    ControlSendValue(batteryStatusTxMessage);
    std::string batteryStatusRxMessage = ControlWaitToReceiveValue();
    if(!(batteryStatusRxMessage.length() == 10 || batteryStatusRxMessage.length() == 12) || 
         batteryStatusRxMessage[0] != 0x23)
        return false;
    
    SerialPrintf(DATA, "Battery - Status:      %d\n\r", (uint8_t)batteryStatusRxMessage[1]);
    SerialPrintf(DATA, "Battery - Voltage A:   %d\n\r", (uint16_t)(batteryStatusRxMessage[2] + batteryStatusRxMessage[3]*0x100));
    SerialPrintf(DATA, "Battery - Voltage B:   %d\n\r", (uint16_t)(batteryStatusRxMessage[4] + batteryStatusRxMessage[5]*0x100));
    if(batteryStatusRxMessage.length() == 12)                                                                           // G5 or G6 Transmitter.
    {
        SerialPrintf(DATA, "Battery - Resistance:  %d\n\r", (uint16_t)(batteryStatusRxMessage[6] + batteryStatusRxMessage[7]*0x100));
        SerialPrintf(DATA, "Battery - Runtime:     %d\n\r", (uint8_t)batteryStatusRxMessage[8]);
        SerialPrintf(DATA, "Battery - Temperature: %d\n\r", (uint8_t)batteryStatusRxMessage[9]);
    }
    else if(batteryStatusRxMessage.length() == 10)                                                                      // G6 Plus Transmitter.
    {
        SerialPrintf(DATA, "Battery - Runtime:     %d\n\r", (uint8_t)batteryStatusRxMessage[6]);
        SerialPrintf(DATA, "Battery - Temperature: %d\n\r", (uint8_t)batteryStatusRxMessage[7]);
    }
    return true;
}

/**
 * Reads the glucose values from the transmitter.
 */
bool DexcomClient::readGlucose()
{
    std::string glucoseTxMessageG5 = {0x30, 0x53, 0x36};                                                                // G5 = 0x30 the other 2 bytes are the CRC16 XMODEM value in twisted order
    std::string glucoseTxMessageG6 = {0x4e, 0x0a, 0xa9};                                                                // G6 = 0x4e
    if(transmitterID[0] == 8 || (transmitterID[0] == 2 && transmitterID[1] == 2 && transmitterID[2] == 2))              // Check if G6 or one of the newest G6 plus (>2.18.2.88) see https://github.com/xdrip-js/xdrip-js/issues/87
        ControlSendValue(glucoseTxMessageG6);
    else
        ControlSendValue(glucoseTxMessageG5);
    
    std::string glucoseRxMessage = ControlWaitToReceiveValue();
    if (glucoseRxMessage.length() < 16 || glucoseRxMessage[0] != (transmitterID[0] != 8 ? 0x31 : 0x4f))                 // Opcode depends on G5 / G6
        return false;

    uint8_t status = (uint8_t)glucoseRxMessage[1];
    uint32_t sequence  = (uint32_t)(glucoseRxMessage[2] + 
                                    glucoseRxMessage[3]*0x100  + 
                                    glucoseRxMessage[4]*0x10000 + 
                                    glucoseRxMessage[5]*0x1000000);
    uint32_t timestamp = (uint32_t)(glucoseRxMessage[6] + 
                                    glucoseRxMessage[7]*0x100  + 
                                    glucoseRxMessage[8]*0x10000 + 
                                    glucoseRxMessage[9]*0x1000000);

    uint16_t glucoseBytes = (uint16_t)(glucoseRxMessage[10] + 
                                       glucoseRxMessage[11]*0x100);
    boolean glucoseIsDisplayOnly = (glucoseBytes & 0xf000) > 0;
    uint16_t glucose = glucoseBytes & 0xfff;
    uint8_t state = (uint8_t)glucoseRxMessage[12];
    int trend = (int)glucoseRxMessage[13];
    if(state != 0x06)                                                                                                   // Not the ok state -> exit
    {
        SerialPrintf(ERROR, "\nERROR - Session Status / State NOT OK (%d)!\n\r", state);
        ExitState("ERROR - We will not continue due to safety reasons (warmup, stopped, waiting for calibration(s), failed or expired.\n\r");
    }

    SerialPrintf(DATA, "Glucose - Status:      %d\n\r", status);
    SerialPrintf(DATA, "Glucose - Sequence:    %d\n\r", sequence);
    SerialPrintf(DATA, "Glucose - Timestamp:   %d\n\r", timestamp);                                                       // Seconds since transmitter activation
    SerialPrintf(DATA, "Glucose - DisplayOnly: %s\n\r", (glucoseIsDisplayOnly ? "true" : "false"));
    SerialPrintf(GLUCOSE, "Glucose - Glucose:     %d\n\r", glucose);
    SerialPrintf(DATA, "Glucose - State:       %d\n\r", state);
    SerialPrintf(DATA, "Glucose - Trend:       %d\n\r", trend);

    if(saveLastXValues > 0)                                                                                             // Array is big enouth for min one value.
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
    std::string sensorTxMessage = {0x2e, 0xac, 0xc5};
    ControlSendValue(sensorTxMessage);
    std::string sensorRxMessage = ControlWaitToReceiveValue();
    if((sensorRxMessage.length() != 16 && sensorRxMessage.length() != 8) || sensorRxMessage[0] != 0x2f)
        return false;

    uint8_t status = (uint8_t)sensorRxMessage[1];
    uint32_t timestamp = (uint32_t)(sensorRxMessage[2] + 
                                    sensorRxMessage[3]*0x100  + 
                                    sensorRxMessage[4]*0x10000 + 
                                    sensorRxMessage[5]*0x1000000);
    SerialPrintf(DATA, "Sensor - Status:     %d\n\r", status);
    SerialPrintf(DATA, "Sensor - Timestamp:  %d\n\r", timestamp);
    if (sensorRxMessage.length() > 8)
    {
        uint32_t unfiltered = (uint32_t)(sensorRxMessage[6] + 
                                         sensorRxMessage[7]*0x100  + 
                                         sensorRxMessage[8]*0x10000 + 
                                         sensorRxMessage[9]*0x1000000);
        uint32_t filtered   = (uint32_t)(sensorRxMessage[10] + 
                                         sensorRxMessage[11]*0x100  + 
                                         sensorRxMessage[12]*0x10000 + 
                                         sensorRxMessage[13]*0x1000000);
        if (transmitterID[0] == 8)                                                                                      // G6 Transmitter
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
    std::string calibrationDataTxMessage = {0x32, 0x11, 0x16};
    ControlSendValue(calibrationDataTxMessage);
    std::string calibrationDataRxMessage = ControlWaitToReceiveValue();
    if ((calibrationDataRxMessage.length() != 19 && calibrationDataRxMessage.length() != 20) || 
        (calibrationDataRxMessage[0] != 0x33)) 
    return false;

    uint16_t glucose   = (uint16_t)(calibrationDataRxMessage[11] + calibrationDataRxMessage[12]*0x100);
    uint32_t timestamp = (uint32_t)(calibrationDataRxMessage[13] + 
                                    calibrationDataRxMessage[14]*0x100  + 
                                    calibrationDataRxMessage[15]*0x10000 + 
                                    calibrationDataRxMessage[16]*0x1000000);
    SerialPrintf(DATA, "Calibration - Glucose:   %d\n\r", glucose);
    SerialPrintf(DATA, "Calibration - Timestamp: %d\n\r", timestamp);

  return true;
}

/**
 * Reads the last glucose values from the transmitter when the esp was not connected.
 */
bool DexcomClient::readBackfill()
{
    if(transmitterStartTime == 0)                                                                                       // The read time command must be send first to get the current time.
        return false;
    
    backfillStream = "";                                                                                                // Empty the backfill stream.
    backfillExpectedSequence = 1;                                                                                       // Set to the first message.

    std::string backfillTxMessage = {0x50, 0x05, 0x02, 0x00};                                                           // 18 + 2 byte crc = 20 byte
    // Set backfill_start to 0 to get all values of the last ~150 measurements (~12,5h)
    uint32_t backfill_start = transmitterStartTime - (saveLastXValues * 5) * 60;                                        // Get the last x values. Only need x-1 because we already have the current value but request one more to be sure that we get x-1.
    uint32_t backfill_end   = transmitterStartTime - 60;                                                                // Do not request the current value. (But is not anyway available by backfill)

    backfillTxMessage += {uint8_t(backfill_start>>0)};
    backfillTxMessage += {uint8_t(backfill_start>>8)};
    backfillTxMessage += {uint8_t(backfill_start>>16)};
    backfillTxMessage += {uint8_t(backfill_start>>24)};

    backfillTxMessage += {uint8_t(backfill_end>>0)};
    backfillTxMessage += {uint8_t(backfill_end>>8)};
    backfillTxMessage += {uint8_t(backfill_end>>16)};
    backfillTxMessage += {uint8_t(backfill_end>>24)};

    backfillTxMessage += {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};                                                          // Fill up to 18 byte.
    backfillTxMessage += CRC_16_XMODEM(backfillTxMessage);                                                              // Add crc 16.

	SerialPrintf(DEBUG,  "Request backfill from %d to %d (current %d).\n\r", backfill_start, backfill_end, transmitterStartTime);
    ControlSendValue(backfillTxMessage);
    SerialPrintln(DATA, "Waiting for backfill data...");
    std::string backfillRxMessage = ControlWaitToReceiveValue();                                                        // We will receive this normally after all backfill data has been send by the transmitter.
    if (backfillRxMessage.length() != 20 || backfillRxMessage[0] != 0x51)
        return false;

    uint8_t status          = (uint8_t)backfillRxMessage[1];
    uint8_t backFillStatus  = (uint8_t)backfillRxMessage[2];
    uint8_t identifier      = (uint8_t)backfillRxMessage[3];
    uint32_t timestampStart = (uint32_t)(backfillRxMessage[4] + 
                                         backfillRxMessage[5]*0x100  + 
                                         backfillRxMessage[6]*0x10000 + 
                                         backfillRxMessage[7]*0x1000000);
    uint32_t timestampEnd   = (uint32_t)(backfillRxMessage[8] + 
                                         backfillRxMessage[9]*0x100  + 
                                         backfillRxMessage[10]*0x10000 + 
                                         backfillRxMessage[11]*0x1000000);
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

    if(saveLastXValues > 1)                                                                                             // Array is big enouth for min 1 backfill value (and the current value).
    {
        for(int i = saveLastXValues - 1; i > 1; i--)                                                                    // Shift all old values (but not the first) back to save these.
            glucoseValues[i] = glucoseValues[i-1];
        glucoseValues[1] = glucose;
    }
    
    SerialPrintf(GLUCOSE,  "Backfill -> Dextime: %d   Glucose: %d   Type: %d\n\r", dextime, glucose, type);
}


/**
 * Encrypt using AES 182 ecb (Electronic Code Book Mode).
 */
std::string DexcomClient::encrypt(std::string buffer, std::string id)
{
    mbedtls_aes_context aes;

    std::string key = "00" + id + "00" + id;                                                                            // The key (that also used the transmitter) for the encryption.
    unsigned char output[16];

    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_enc(&aes, (const unsigned char *)key.c_str(), strlen(key.c_str()) * 8);
    mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, (const unsigned char *)buffer.c_str(), output);
    mbedtls_aes_free(&aes);

    std::string returnVal = "";
    for (int i = 0; i < 16; i++)                                                                                        // Convert unsigned char array to string.
    {
        returnVal += output[i];
    }
    return returnVal;
}

/**
 * Calculates the Hash for the given data.
 */
std::string DexcomClient::calculateHash(std::string data, std::string id)
{
    if (data.length() != 8)
    {
        SerialPrintln(ERROR, "cannot hash");
        return NULL;
    }

    data = data + data;                                                                                                 // Use double the data to get 16 byte
    std::string hash = encrypt(data, id);
    return hash.substr(0, 8);                                                                                           // Only use the first 8 byte of the hash (ciphertext)
}