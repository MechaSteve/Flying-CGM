/*
 * DebugHelper.cpp
 *
 *  Created on: 2023.02.15
 *      Author: Stephen Culpepper
 * 
 */


#include <Arduino.h>
#include <Esp.h>
#include <string.h>
#include "DebugHelper.h"


void SerialPrint(OutputType type, const char * text)
{
    if(type >= outputLevel)                                                                     // Only print if OutputType is more specific than OutputLevel
        Serial.print(text);
}
void SerialPrint(OutputType type, uint8_t value, int mode)
{
    if(type >= outputLevel)
        Serial.print(value, mode);
}

void SerialPrintln(OutputType type)
{
    if(type >= outputLevel)
        Serial.println();
    else
        delay(10);
}
void SerialPrintln(OutputType type, const char * text)
{
    if(type >= outputLevel)
        Serial.println(text);
    else
        delay(10);
}

void printHexArray(uint8_t *data, size_t length)
{
    for (size_t i = 0; i < length; i++)
    {
        SerialPrint(DEBUG, data[i], HEX);
        SerialPrint(DEBUG, " ");
    }
    SerialPrintln(DEBUG);
}

void printHexString(String value)
{
    for (int i = 0; i < value.length(); i++)
    {
        SerialPrint(DEBUG, (uint8_t)value[i], HEX);
        SerialPrint(DEBUG, " ");
    }
    SerialPrintln(DEBUG);
}

String uint8ToString(uint8_t *data, size_t length)
{
    String value = "";
    for (size_t i = 0; i < length; i++)
    {
        value += (char)data[i];
    }
    return value;
}