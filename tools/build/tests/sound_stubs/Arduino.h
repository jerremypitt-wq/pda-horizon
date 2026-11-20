#pragma once
#include <stdint.h>
#define OUTPUT 1
#define LOW 0
uint32_t millis();
void pinMode(uint8_t, uint8_t);
void digitalWrite(uint8_t, uint8_t);
#if TEST_ARDUINO_MAJOR >= 3
bool ledcAttach(uint8_t pin, uint32_t freq, uint8_t resolution);
uint32_t ledcWriteTone(uint8_t pin, uint32_t freq);
bool ledcWrite(uint8_t pin, uint32_t duty);
#else
double ledcSetup(uint8_t channel, double freq, uint8_t resolution);
void ledcAttachPin(uint8_t pin, uint8_t channel);
double ledcWriteTone(uint8_t channel, double freq);
void ledcWrite(uint8_t channel, uint32_t duty);
#endif
