#include "Runtime.h"
#include <Arduino.h>

// Function definition
Runtime getRuntime() {
    // Get the number of milliseconds since the last boot
    unsigned long uptimeMillis = millis();
    
    // Convert milliseconds to seconds
    unsigned long uptimeSeconds = uptimeMillis / 1000;
    
    // Calculate days, hours, minutes, and seconds
    unsigned long days = uptimeSeconds / 86400; // 86400 seconds in a day
    unsigned long hours = (uptimeSeconds % 86400) / 3600; // 3600 seconds in an hour
    unsigned long minutes = (uptimeSeconds % 3600) / 60; // 60 seconds in a minute
    unsigned long seconds = uptimeSeconds % 60; // Remaining seconds

    // Return the result as a Runtime struct
    Runtime runtime;
    runtime.days = days;
    runtime.hours = hours;
    runtime.minutes = minutes;
    runtime.seconds = seconds;
    
    return runtime;
}