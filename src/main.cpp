#include <Arduino.h>
#include <HardwareSerial.h>
#include <DS3231.h>
#include <Wire.h>
#include <esp_system.h>
#include <VL53L0X.h>
#include <ArduinoHttpClient.h>
#include <Update.h>

#include "SETTINGS.h"
#include "MQTT_SETTINGS.h"
#include "MODEM_SETTINGS.h"

HttpClient http(client, ota_server, ota_port);

void reset_sim800l();
void mqttCallback(char *topic, byte *payload, unsigned int len);
void sim800l_init();
void mainloop();
void rtc_check();
void serial_command(String cmd);
void performOTA();
void check_wire_sensors();
void first_run_measure();

boolean mqttConnect();
boolean _IsNetworkConnected();
boolean _IsGPRSConnected();
boolean _IsMQTTConnected();
boolean measure_level();

char *get_datetime();
char *get_runtime();

Runtime getRuntime()
{
    // Get the number of milliseconds since the last boot
    unsigned long uptimeMillis = millis();

    // Convert milliseconds to seconds
    unsigned long uptimeSeconds = uptimeMillis / 1000;

    // Calculate days, hours, minutes, and seconds
    unsigned long days = uptimeSeconds / 86400;           // 86400 seconds in a day
    unsigned long hours = (uptimeSeconds % 86400) / 3600; // 3600 seconds in an hour
    unsigned long minutes = (uptimeSeconds % 3600) / 60;  // 60 seconds in a minute
    unsigned long seconds = uptimeSeconds % 60;           // Remaining seconds

    // Return the result as a Runtime struct
    Runtime runtime;
    runtime.days = days;
    runtime.hours = hours;
    runtime.minutes = minutes;
    runtime.seconds = seconds;

    return runtime;
}

void setup()
{
    pinMode(reset_pin, OUTPUT);
    // put your setup code here, to run once:
    Serial.begin(115200);

    Serial.println("Starting.... waiting for 10 sec");
    delay(5000);
    Wire.begin(_sda, _scl);
    
    sprintf(sensor_status, "Sensor init");

    check_wire_sensors();
    
    sensor.setTimeout(500);    
    if (!sensor.init())
    {
        Serial.println("Failed to detect and initialize sensor!");
    }
    sensor.startContinuous(1000);
    
    //ESP32 dev
    // SerialAT.begin(115200);

    // lolin_s2_mini
    Serial1.begin(115200, SERIAL_8N1, rxpin, txpin);

    sim800l_init();

    Serial.println("--------------------------------------------------------");
    Serial.print("reset_reason: ");
    Serial.println(reset_reason);

    measure_level();
}

void loop()
{
    uint32_t timer = millis();

    _IsNetworkConnected();
    _IsGPRSConnected();
    _IsMQTTConnected();

    if (first_run)
    {
        
        first_run = false;
        // first_run_measure();        
        bool posted = mqtt.publish(test_level, water_level_converted, RETAINED);
        Serial.printf("Posted: %o \n", posted);
        first_run = false;
    }

    // mainloop
    if (timer - t_timer >= (t_loop * 60 * 1000))
    {        
        first_run = false;
        t_timer = timer;
        mainloop();
    }

    if (timer - t_rtc >= (t_rtc_check * 60 * 1000))
    {        
        first_run = false;
        t_rtc = timer;
        rtc_check();
    }

    if (ESP.getFreeHeap() < 10000)
    {
        ESP.restart();
    }

    // Serial Commanding
    static String inputString = ""; // A string to hold incoming data
    if (Serial.available() > 0)
    {
        // Read the incoming byte
        char incomingByte = Serial.read();

        // If the incoming byte is a newline character, process the input string
        if (incomingByte == '\n')
        {
            inputString.trim(); // Remove any trailing whitespace
            if (inputString.length() > 0)
            {
                serial_command(inputString);
                inputString = ""; // Clear the string for new input
            }
        }
        else
        {
            // Add the incoming byte to the input string
            inputString += incomingByte;
        }
    }

    mqtt.loop();
    yield();
}

void reset_sim800l()
{
    Serial.println("Reseting SIM800L....");
    digitalWrite(reset_pin, LOW);
    delay(1000);
    digitalWrite(reset_pin, HIGH);
    delay(1000);
    Serial.println("Reset SIM800L - Done!");
}

void mqttCallback(char *topic, byte *payload, unsigned int len)
{
    Serial.print("Message arrived [ ");
    Serial.print(topic);
    Serial.print(" ] ");

    Serial.print("payload: ");
    Serial.write(payload, len);
    Serial.print(", len: ");
    Serial.println(len);
    String message;
    for (int i = 0; i < len; i++)
    {
        message += (char)payload[i];
    }

    Serial.print(F("message: "));
    Serial.println(message);

    // Start hour
    if (strcmp(topic, test_start) == 0)
    {
        Serial.print("Current start: ");
        Serial.println(telemetry_time_interval[0]);

        telemetry_time_interval[0] = message.toInt();
        Serial.print("Changed Start: ");
        Serial.println(telemetry_time_interval[0]);
    }

    // Stop hour
    else if (strcmp(topic, test_stop) == 0)
    {
        Serial.print("Current stop: ");
        Serial.println(telemetry_time_interval[1]);

        telemetry_time_interval[1] = message.toInt();
        Serial.print("Changed Stop: ");
        Serial.println(telemetry_time_interval[1]);
    }

    // change telemetry
    else if (strcmp(topic, test_telemetry) == 0)
    {
        Serial.print("Current t_loop: ");
        Serial.println(t_loop);

        t_loop = message.toInt();
        Serial.print("Changed t_loop: ");
        Serial.println(t_loop);
    }

    // force reset
    else if (strcmp(topic, test_reset) == 0)
    {
        Serial.println("Reset topic");
        if (message == "reset")
        {
            Serial.println("Reset command");
            mqtt.publish(test_reset, "ESP go to reset");
            delay(1000);
            ESP.restart();
        }
        else
        {
            Serial.print("Unknown command: ");
            Serial.println(message);
        }
    }

    // get all values
    else if (strcmp(topic, test_get) == 0)
    {
        Serial.println("Get topic");
        if (message == "all")
        {
            Serial.println("Get all command");

            snprintf(jsonData, sizeof(jsonData),
                     ";%s;%d;%u;%s;%d;%s;%u;%u;%d;%d;%S;%s",
                     get_datetime(), current_hour, ESP.getFreeHeap(),
                     get_runtime(), reset_reason, ip.c_str(),
                     t_timer, t_loop, telemetry_time_interval[0],
                     telemetry_time_interval[1], (String)sensor_status, (String)water_level);
            String result = (String)jsonData;

            Serial.printf("JSON: %s \n", result.c_str());

            // test_info
            if (_IsMQTTConnected())
            {
                Serial.println("MQTT Connected");
                boolean message = mqtt.publish(test_info, jsonData);
                Serial.printf("MQTT responce: %o", message);
            }
            else
            {
                Serial.println("MQTT Disconnected !!!");
            }
        }
        else if (message == "update")
        {
            Serial.println("Update command over MQTT");
            serial_command(message);
        }
        else if (message == "level")
        {
            Serial.println("Level command over MQTT");
            measure_level();                    
            bool posted = mqtt.publish(test_level, water_level_converted, RETAINED);
            Serial.printf("Posted: %o \n", posted);

        }
        
        else if (message == "status")
        {
            Serial.println("Status command over MQTT");                  
            bool posted = mqtt.publish(test_status, "Online");
            Serial.printf("Posted: %o \n", posted);
        }
    }
    else
    {
        Serial.println("Topic is subscribed but has no function");
    }
    Serial.println("--------------------------------------------------------");
}

boolean mqttConnect()
{
    Serial.print("Connecting to ");
    Serial.print(broker);

    // Connect to MQTT Broker
    boolean status = mqtt.connect("sim800L_weather_station", user, password, willTopic, willQos, willRetain, willMessage);
    mqtt.setKeepAlive(Keepalive);

    if (status == false)
    {
        Serial.println(" fail");
        return false;
    }
    Serial.println(" success");

    bool posted = mqtt.publish(test_status, "Online");

    // Subscibe topic
    mqtt.subscribe(test_start);
    mqtt.subscribe(test_stop);
    mqtt.subscribe(test_telemetry);
    mqtt.subscribe(test_reset);
    mqtt.subscribe(test_get);

    delay(100);

    return mqtt.connected();
}

void sim800l_init()
{
    reset_sim800l();
    delay(2000);
    String modemInfo = modem.getModemInfo();
    Serial.print("Modem Info: ");
    Serial.println(modemInfo);

    if (GSM_PIN && modem.getSimStatus() != 3)
    {
        modem.simUnlock(GSM_PIN);
    }

    // modem.gprsConnect(apn, gprsUser, gprsPass);

    Serial.print("Waiting for network...");
    if (!modem.waitForNetwork())
    {
        Serial.println(" fail");
        delay(10000);
        return;
    }
    Serial.println(" success");

    if (modem.isNetworkConnected())
    {
        Serial.println("Network connected");
    }
    Serial.print("Signal: ");
    rssi = modem.getSignalQuality();
    Serial.println(rssi);

    Serial.print(F("Connecting to "));
    Serial.print(apn);
    if (!modem.gprsConnect(apn, gprsUser, gprsPass))
    {
        Serial.println(" fail");
        delay(10000);
        return;
    }
    Serial.println(" success");

    if (modem.isGprsConnected())
    {
        Serial.println("GPRS connected");
    }

    Serial.print("IP: ");
    ip = modem.getLocalIP();
    Serial.println(ip);

    // MQTT Broker setup
    mqtt.setServer(broker, port);
    mqtt.setKeepAlive(Keepalive);
    mqtt.setCallback(mqttCallback);
    mqttConnect();

    // Send IP
    bool post_ip = mqtt.publish(test_ip, ip.c_str(), RETAINED);
    Serial.print("post_ip: ");
    Serial.println(post_ip);

    // Send RSSI
    bool post_rssi = mqtt.publish(test_rssi, ((String)rssi).c_str(), RETAINED);
    Serial.print("post_rssi: ");
    Serial.println(post_rssi);

    // Send reset cause
    bool post_reset_case = mqtt.publish(test_reset_cause, ((String)reset_reason).c_str(), RETAINED);
    Serial.print("post_reset_case: ");
    Serial.println(post_reset_case);

    yield();
}

boolean _IsNetworkConnected()
{
    if (!modem.isNetworkConnected())
    {
        Serial.println("Disconnected from network");
        if (!modem.waitForNetwork())
        {
            Serial.println(" fail");
            sim800l_init();
        }
        Serial.println("Reconnected to Network");
    }
    yield();
    return modem.isNetworkConnected();
}
boolean _IsGPRSConnected()
{
    if (!modem.isGprsConnected())
    {
        Serial.println("Disconnected from GPRS");
        modem.gprsDisconnect();
        modem.gprsConnect(apn, gprsUser, gprsPass);
        if (modem.isGprsConnected())
        {
            Serial.println("Reconnected to GPRS");
        }
        else
        {
            Serial.println("Fail in reconnection");
            sim800l_init();
        }
    }
    yield();
    return modem.isGprsConnected();
}
boolean _IsMQTTConnected()
{
    if (!mqtt.connected())
    {
        Serial.println("Disconnected from MQTT Broker");
        mqttConnect();
    }
    yield();
    return mqtt.connected();
}

char *get_datetime()
{
    int year = myRTC.getYear() + 2000;
    int month = myRTC.getMonth(century);
    int date = myRTC.getDate();
    int hour = myRTC.getHour(h12Flag, pmFlag); // 24-hour format
    int minute = myRTC.getMinute();
    int second = myRTC.getSecond();

    current_hour = hour;

    // Format the date and time as a string
    snprintf(buffer_datetime, size_t(buffer_datetime), "%04d-%02d-%02d %02d:%02d:%02d", year, month, date, hour, minute, second);

    yield();
    return buffer_datetime;
}

void mainloop()
{
    Serial.println("Mainloop");
    Serial.print("i: ");
    Serial.println(i);

    Serial.println(get_datetime());

    Serial.print("current_hour: ");
    Serial.println(current_hour);

    if ((telemetry_time_interval[0] <= current_hour) && (current_hour <= telemetry_time_interval[1]))
    {
        String res_ = "Time is between " + (String)telemetry_time_interval[0] + "h and " + (String)telemetry_time_interval[1] + "h";
        Serial.println(res_);

        measure_done = measure_level();
        if (measure_done)
        {
            if (_IsMQTTConnected())
            {
                Serial.println("MQTT connected");
                bool posted = mqtt.publish(test_level, water_level_converted, RETAINED);
                Serial.printf("Posted: %o \n", posted);
            }
            else
            {
                Serial.println("MQTT is not connected when telemetry time is OK");
            }
        }
        else
        {
            String message = "Sensor fail";
            Serial.println(message);
            
            bool m_fail = mqtt.publish(test_fail, message.c_str());
            Serial.printf("Posted: %o \n", m_fail);
        }
    }
    else
    {
        String res_ = "Time is out off " + (String)telemetry_time_interval[0] + "h and " + (String)telemetry_time_interval[1] + "h";
        Serial.println(res_);
    }

    Serial.print("Freeheap: ");
    Serial.println(ESP.getFreeHeap());

    Serial.print("Runtime: ");
    Serial.println(get_runtime());
    Serial.println("--------------------------------------------------------");
    i++;
    yield();
}

void rtc_check()
{
    Serial.println(get_datetime());

    Serial.print("current_hour: ");
    Serial.print(current_hour);
    Serial.print(", Free heap: ");
    Serial.print(ESP.getFreeHeap());
    Serial.print(", Runtime: ");
    Serial.println(get_runtime());
    Serial.println("--------------------");
}

char *get_runtime()
{
    Runtime runtime = getRuntime();
    sprintf(runtimeString, "Runtime: %lu days, %lu hours, %lu minutes, %lu seconds", runtime.days, runtime.hours, runtime.minutes, runtime.seconds);
    return runtimeString;
}

void serial_command(String cmd)
{
    if (cmd == "info")
    {
        Serial.println("--------------------");

        snprintf(jsonData, sizeof(jsonData),
                 "{\"datetime\":\"%s\",\"current_hour\":%d,\"Free heap\":%u,"
                 "\"Runtime\":\"%s\",\"reset_reason\":%d,\"ip\":\"%s\",\"t_timer\":%u,"
                 "\"t_loop\":%u,\"start\":%d,\"stop\":%d,\"sensor_status\":%d,"
                 "\"water_level\":%d}",
                 get_datetime(), current_hour, ESP.getFreeHeap(),
                 get_runtime(), reset_reason, ip.c_str(),
                 t_timer, t_loop, telemetry_time_interval[0],
                 telemetry_time_interval[1], sensor_status, water_level);

        Serial.printf("JSON: %s \n", jsonData);
        Serial.println("--------------------");
    }
    else if (cmd == "update")
    {
        Serial.println("OTA command, check GPRS");
        if (_IsNetworkConnected() && _IsGPRSConnected() && _IsMQTTConnected())
        {
            Serial.println("GPRS OK - OTA starting");
            performOTA();
            Serial.println("\n\nOTA DONE");
        }
        else
        {
            Serial.println("Disconnected from GPRS or MQTT - OTA not started");
        }
    }
    else if (cmd = "level")
    {
        measure_level();
    }

    else
    {
        Serial.print("Unknown command: ");
        Serial.println(cmd);
    }
}

boolean measure_level()
{
    Serial.println("--- Measure process ---");

    // Check if the sensor is connected and working
    if (!sensor.init())
    {
        Serial.println("Sensor not detected!");
        sprintf(sensor_status, "Sensor Not Detected");
        return false;
    }

    uint16_t distance_single = sensor.readRangeSingleMillimeters();
    uint16_t distance_period = sensor.readRangeContinuousMillimeters();
    if ((distance_single > tank_low_limit) && (((float)distance_single / 10) < tank_high_limit))
    {
        Serial.print(F("distance_single: "));
        Serial.println(distance_single);

        water_level = ((float)distance_single / 10);

        Serial.print(F("water_level: "));
        Serial.println(water_level);

        // convert water level into string
        dtostrf(water_level, 5, 2, water_level_converted);

        Serial.print(F("Water level converted: "));
        Serial.println(water_level_converted);
        sprintf(sensor_status, "Measure Done");

        Serial.println("Measure Done");
        return true;
    }
    else
    {   
        Serial.printf("Sensor: %d \n", distance_single);
        Serial.println("Something wrong with sensor");
        sprintf(sensor_status, "Measure Fail");
        return false;
    }
}

void performOTA()
{
    bool m_ota_start = mqtt.publish(test_update, "Start OTA update");
    Serial.printf("m_ota_start: %o\n", m_ota_start);

    http.get(ota_full_path);
    int statusCode = http.responseStatusCode();
    int contentLength = http.contentLength();

    Serial.print("Status: ");
    Serial.print(statusCode);
    Serial.print(", contentLength: ");
    Serial.println(contentLength);

    if (statusCode == 200)
    {
        bool canBegin = Update.begin(contentLength);

        if (canBegin)
        {
            Serial.println("Downloading firmware...");
            uint8_t buff[128] = {0};
            int written = 0;
            int progress_last = 0;

            while (http.connected() && (written < contentLength))
            {
                size_t len = http.available();
                if (len)
                {
                    int c = http.readBytes(buff, ((len > sizeof(buff)) ? sizeof(buff) : len));
                    Update.write(buff, c);
                    written += c;

                    // Print progress
                    int progress = (written * 100) / contentLength;
                    if (progress != progress_last)
                    {
                        Serial.printf("Progress: %d %%\n", progress);
                        progress_last = progress;
                    }
                }
                delay(1);
            }

            if (Update.end())
            {
                Serial.println("OTA update finished.");
                if (Update.isFinished())
                {
                    Serial.println("Rebooting device.");
                    delay(100);
                    ESP.restart();
                }
                else
                {
                    Serial.println("OTA update not completed.");
                }
            }
            else
            {
                Serial.printf("OTA update failed. Error #: %d\n", Update.getError());
            }
        }
        else
        {
            Serial.println("Not enough space to begin OTA.");
        }
    }
    else
    {
        Serial.printf("HTTP error: %d\n", statusCode);
    }

    http.stop();
}

void check_wire_sensors()
{    
    byte error, address;
    int nDevices = 0;

    Serial.println("Scanning Wire bus...");

    for (address = 1; address < 127; address++)
    {
        Wire.beginTransmission(address);
        error = Wire.endTransmission();

        if (error == 0)
        {
            Serial.print("I2C device found at address 0x");
            if (address < 16)
            {
                Serial.print("0");
            }
            Serial.print(address, HEX);
            Serial.println("  !");

            if (address == ds3231_addr)
            {
                RTC_CONNECTED = true;
                Serial.printf("address: %X, ds3231_addr: %X, RTC_CONNECTED %o -- \n", address, ds3231_addr, RTC_CONNECTED);
            }
            else if (address == vl53l0x_addr)
            {
                VL53L0X_CONNECTED = true;
                Serial.printf("address: %X, vl53l0x_addr: %X, VL53L0X_CONNECTED %o -- \n", address, vl53l0x_addr, VL53L0X_CONNECTED);
            }
            else
            {
                Serial.printf("address: %X \n", address);
            }          

            nDevices++;
        }
        else if (error == 4)
        {
            Serial.print("Unknown error at address 0x");
            if (address < 16)
                Serial.print("0");
            Serial.println(address, HEX);
        }
    }

    if (nDevices == 0)
    {
        Serial.println("No I2C devices found\n");
        while (1) {}
    }
    else
    {
        Serial.println("Scan Successfully done\n");
    }
    
    # 
    if (VL53L0X_CONNECTED)
    {
        Serial.println("Detected and initialized VL53L0X sensor");
    }
    else
    {
        Serial.println("Failed to detect and initialize VL53L0X sensor!");
    }

    if (RTC_CONNECTED)
    {
        Serial.println("Detected and initialized DS3231 sensor sensor");
    }
    else
    { 
        Serial.println("Failed to detect and initialize DS3231 sensor sensor!");
    }
}

void first_run_measure()
{    
    first_run = false;

    Serial.println("\nFirst run.....");
    Serial.print("i: ");
    Serial.println(i);
    
    _IsNetworkConnected();
    _IsGPRSConnected();
    _IsMQTTConnected();

    Serial.println(get_datetime());

    String res = (String)current_hour;
    bool posted = mqtt.publish(test_hour, res.c_str());
    Serial.printf("posted: %o \n", posted);

    measure_done = measure_level();
    if (measure_done)
    {
        int loop = 0;
        while(loop <= 3)
        {
            if (_IsMQTTConnected())
            {
                Serial.println("MQTT connected");
                bool posted = mqtt.publish(test_level, water_level_converted, RETAINED);
                Serial.printf("Posted: %o \n", posted);
                break;
            }
            else
            {
                Serial.println("MQTT is not connected when time is OK");
                Serial.printf("loop %d \n", loop);
                delay(50);
                _IsNetworkConnected();
                _IsGPRSConnected();
                _IsMQTTConnected();
            }
            loop += 1;
        }
    }
    else
    {
        String message = "Sensor fail";
        Serial.println(message);
        
        bool m_fail = mqtt.publish(test_fail, message.c_str());
        Serial.printf("Posted: %o \n", m_fail);
    }

    Serial.print("current_hour: ");
    Serial.print(current_hour);
    Serial.print(", Freeheap: ");
    Serial.print(ESP.getFreeHeap());
    Serial.print(", Runtime: ");
    Serial.println(get_runtime());
    Serial.println("--------------------------------------------------------");
    
    i++;
}