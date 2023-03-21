/**
 * Some helper functions for debugging and printing values.
 * 
 * Author: Max Kaiser
 * Reorganized by Stephen Culpepper
 * Copyright (c) 2023
 * 2023.03.15
 */


#include <Arduino.h>
#include <Esp.h>


#ifndef DEBUGHELPER_H
#define DEBUGHELPER_H

typedef enum 
{ 
    DEBUG   = 0,                    // Tag for normal (debug) output (bytes send / recv, notify, callbacks).
    DATA    = 1,                    // Tag for messages with calculated / parsed data from the transmitter.
    ERROR   = 2,                    // Tag for error messages.
    GLUCOSE = 3                     // Only for the one print message with the glucose value.
} OutputType;

typedef enum 
{ 
    FULL           = 0,             // Prints all output.
    NO_DEBUG       = 1,             // Prints only errors or data from the transmitter.
    ONLY_ERROR     = 2,             // Prints only errors and lines with glucose.
    ONLY_GLUCOSE   = 3,             // Print only lines with the glucose values (NO ERRORS!).
    NONE           = 4              // Do not print anything - used when no serial monitor is connected.
} OutputLevel;


static OutputLevel outputLevel = FULL;                    /* Change to your output level  */                            // Set this to NONE if no serial connection is used.


template<typename... Args> 
void SerialPrintf(int type, const char * f, Args... args)                                    // Use C++11 variadic templates
{
    if(type >= outputLevel)
        Serial.printf(f, args...);
    else
        delay(10);                                                                                                      // Use a delay as compensation for serial.print()
}

/** 
 * Wrapper functions for Serial.print(..) to allow filtering and setting an output log level.
 */
void SerialPrint(OutputType type, const char * text);
void SerialPrint(OutputType type, uint8_t value, int mode);

void SerialPrintln(OutputType type);
void SerialPrintln(OutputType type, const char * text);

/**
 * Prints a sting as hex values.
 */
void printHexString(std::string value);

/**
 * Prints an uint8_t array as hex values.
 */
void printHexArray(uint8_t *data, size_t length);


/**
 * Converts an uint8_t array to string.
 */
std::string uint8ToString(uint8_t *data, size_t length);


#endif /* DEBUGHELPER_H */
