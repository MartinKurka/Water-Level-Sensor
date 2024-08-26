#include <esp_system.h>
#include <DS3231.h>
#include <Wire.h>

/* ---------------- SETUP -------------------------*/
uint8_t led_pin = 8;

// pin setup for I2C
uint8_t _sda = 6;
uint8_t _scl = 7;

// pin setup for MODEM
uint8_t rxpin = 20;
uint8_t txpin = 21;
uint8_t reset_pin = 9;

// telemetry period
uint32_t t_loop = 30; // min
uint32_t t_heartbeat_loop = 1;   // min

// telemetry period
int telemetry_time_interval[2] = {6, 22};

// OTA URL link
const char* ota_server = "194.182.80.42";
const char* ota_path = "/download/firmware.bin";
const char* ota_full_path = "http://194.182.80.42/download/firmware.bin";
const int   ota_port = 80;

// tank setup
unsigned int tank_low_limit = 0;        // cm
unsigned int tank_high_limit = 100;     // cm

// NTP Server details
const char* serverAddress = "worldtimeapi.org";  // Server address
int ntp_port = 80;  // HTTP port

// API endpoint for time (replace with your timezone or UTC)
const char* timeAPI = "/api/timezone/Etc/UTC";

// Time structure to hold time values
struct tm machine_rtc;

/* ---------- Supported variables -----------------*/

esp_reset_reason_t reset_reason = esp_reset_reason();


uint32_t i = 1;
uint32_t t_timer = 0;
uint32_t t_rtc = 0;
uint32_t t_rtc_check = 5; // min
uint32_t t_heartbeat_timer = 0;

bool first_run = true;
float water_level = 0.0;
char water_level_converted[] = "0.0";

uint8_t current_hour = 0;
char buffer_datetime[127];
char jsonData[512];

// RTC
DS3231 myRTC;
byte ds3231_addr = 0x68;
bool RTC_CONNECTED = false;
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

// measured sensor
VL53L0X sensor;
byte vl53l0x_addr = 0x29;
bool VL53L0X_CONNECTED = false;
bool measure_done = false;

char sensor_status[20];
