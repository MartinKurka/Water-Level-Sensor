#include <esp_system.h>
#include <DS3231.h>
#include <Wire.h>

/* ---------------- SETUP -------------------------*/
// pin setup for MODEM
uint8_t rxpin = 9;
uint8_t txpin = 11;
uint8_t reset_pin = 12;

// telemetry period
uint32_t t_loop = 30; // min

// telemetry period
int telemetry_time_interval[2] = {6, 22};

/* ---------- Supported variables -----------------*/

esp_reset_reason_t reset_reason = esp_reset_reason();

uint32_t i = 1;
uint32_t t_timer = 0;
uint32_t t_rtc = 0;
uint32_t t_rtc_check = 10; // min
bool first_run = true;
float water_level = 0;

const uint8_t buffer_len = 1;
uint8_t current_hour = 0;
char buffer[buffer_len];

// RTC
DS3231 myRTC;
bool century = false;
bool h12Flag;
bool pmFlag;

// Runtime
char runtimeString[256];
struct Runtime {
    unsigned long days;
    unsigned long hours;
    unsigned long minutes;
    unsigned long seconds;
};