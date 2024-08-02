#include "sim800l.h"
#include <Arduino.h>

void reset_sim800l(int pin)
{
    Serial.println("Reseting SIM800L....");
    digitalWrite(pin, LOW);
    delay(1000);
    digitalWrite(pin, HIGH);
    delay(1000);
    Serial.println("Reset SIM800L - Done!");
}
