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