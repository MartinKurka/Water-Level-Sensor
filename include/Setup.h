/* -----------------------   SETUP  -----------------------*/

// DS3231 pinout setup
uint8_t _sda = 3;
uint8_t _scl = 5;

// sim800l pinout setup
uint8_t sim800l_rx = 9;
uint8_t sim800l_tx = 11;
uint8_t reset_pin = 12;

// telemetry period
uint32_t t_loop = 30; // min

// telemetry interval {start hour, stop hour}
int telemetry_time_interval[2] = {6, 22};

/* -----------------   Program variables  ----------------*/
#include <DS3231.h>
#include <Wire.h>

esp_reset_reason_t reset_reason = esp_reset_reason();

unsigned int i = 1;
String ip = "";
String jsonData = "";
int rssi = 0;

unsigned long t_timer = 0;
unsigned long t_rtc = 0;
unsigned long t_rtc_check = 10; // min

DS3231 myRTC;
bool century = false;
bool h12Flag;
bool pmFlag;

uint8_t current_hour = 0;
bool first_run = true;
const uint8_t buffer_len = 1;
char buffer[buffer_len];

char runtimeString[256];