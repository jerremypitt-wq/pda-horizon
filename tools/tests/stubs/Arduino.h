#pragma once
#include <stdint.h>
#define INPUT 0
#define OUTPUT 1
#define LOW 0
#define HIGH 1
void pinMode(uint8_t pin, uint8_t mode);
void digitalWrite(uint8_t pin, uint8_t value);
int digitalRead(uint8_t pin);
void delayMicroseconds(unsigned us);
