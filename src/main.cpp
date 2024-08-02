#include <Arduino.h>
#include <HardwareSerial.h>
#include <DS3231.h>
#include <Wire.h>
#include <esp_system.h>
#include "SETTINGS.h"
#include "MQTT_SETTINGS.h"
#include "MODEM_SETTINGS.h"

void reset_sim800l();
void mqttCallback(char *topic, byte *payload, unsigned int len);
void sim800l_init();
void mainloop();
void rtc_check();
void serial_command(String cmd);
void measure_level();

boolean mqttConnect();
boolean _IsNetworkConnected();
boolean _IsGPRSConnected();
boolean _IsMQTTConnected();

char *get_datetime();
char *get_runtime();

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

void setup()
{
    // put your setup code here, to run once:
    Serial.begin(115200);

    Serial.println("Starting.... waiting for 10 sec");
    delay(5000);
    Wire.begin(3, 5);

    pinMode(15, OUTPUT);
    pinMode(reset_pin, OUTPUT);

    Serial1.begin(115200, SERIAL_8N1, rxpin, txpin);

    sim800l_init();
    digitalWrite(15, HIGH);
    Serial.println("--------------------------------------------------------");
    Serial.print("reset_reason: ");
    Serial.println(reset_reason);
}

void loop()
{
    uint32_t timer = millis();

    _IsNetworkConnected();
    _IsGPRSConnected();
    _IsMQTTConnected();

    if (first_run)
    {
        Serial.print("i: ");
        Serial.println(i);

        i++;
        first_run = false;
        char *datetime = get_datetime();
        Serial.println(datetime);
        free(datetime);

        String res = (String)current_hour;
        bool posted = mqtt.publish(test_data, res.c_str());
        Serial.print("posted: ");
        Serial.println(posted);

        Serial.print("current_hour: ");
        Serial.print(current_hour);
        Serial.print(", Freeheap: ");
        Serial.print(ESP.getFreeHeap());
        Serial.print(", Runtime: ");
        Serial.println(get_runtime());
        Serial.println("--------------------------------------------------------");
    }

    // mainloop
    if (timer - t_timer >= (t_loop * 60 * 1000))
    {
        t_timer = timer;
        mainloop();
    }

    if (timer - t_rtc >= (t_rtc_check * 60 * 1000))
    {
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
            // return all value
            jsonData = "{";
            jsonData += "\"datetime\":" + String(get_datetime()) + ",";
            jsonData += "\"current_hour\":" + String(current_hour) + ",";
            jsonData += "\"Free heap\":" + String(ESP.getFreeHeap()) + ",";
            jsonData += "\"Runtime\":" + String(get_runtime()) + ",";
            jsonData += "\"reset_reason\":" + String(reset_reason) + ",";
            jsonData += "\"ip\":" + String(ip) + ",";
            jsonData += "\"t_timer\":" + String(t_timer) + ",";
            jsonData += "\"t_loop\":" + String(t_loop) + ",";
            jsonData += "\"start\":" + String(telemetry_time_interval[0]) + ",";
            jsonData += "\"stop\":" + String(telemetry_time_interval[1]) + ",";
            jsonData += "}";

            // test_info            
            bool done = mqtt.publish(test_info, jsonData.c_str());
            Serial.print("MQTT responce: ");
            Serial.println(done);
        }
    }
    else
    {
        Serial.println("Topic is subscribed but has no function");
    }
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

    bool posted = mqtt.publish(test_status, "1");

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
    const uint16_t buffer_len = 30;
    char *buffer = (char *)malloc(buffer_len * sizeof(char)); // Allocate memory for the buffer
    if (buffer == nullptr)
    {
        Serial.println("Failed to allocate memory");
        return nullptr;
    }

    int year = myRTC.getYear() + 2000;
    int month = myRTC.getMonth(century);
    int date = myRTC.getDate();
    int hour = myRTC.getHour(h12Flag, pmFlag); // 24-hour format
    int minute = myRTC.getMinute();
    int second = myRTC.getSecond();

    current_hour = hour;

    // Format the date and time as a string
    snprintf(buffer, buffer_len, "%04d-%02d-%02d %02d:%02d:%02d", year, month, date, hour, minute, second);

    yield();
    return buffer;
}

void mainloop()
{
    Serial.println("Mainloop");
    Serial.print("i: ");
    Serial.println(i);

    char *datetime = get_datetime();
    Serial.println(datetime);
    free(datetime);

    Serial.print("current_hour: ");
    Serial.println(current_hour);

    if ((telemetry_time_interval[0] <= current_hour) && (current_hour <= telemetry_time_interval[1]))
    {
        String res_ = "Time is between " + (String)telemetry_time_interval[0] + "h and " + (String)telemetry_time_interval[1] + "h";
        Serial.println("Time is between 6h and 22h");
        String res = (String)current_hour;
        measure_level();

        if (_IsMQTTConnected())
        {
            bool posted = mqtt.publish(test_data, res.c_str());

            Serial.print("posted: ");
            Serial.println(posted);
        }
        else
        {
            Serial.println("MQTT is not connected when time is OK");
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
}

void rtc_check()
{
    char *datetime = get_datetime();
    Serial.println(datetime);
    free(datetime);

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
        char *datetime = get_datetime();
        Serial.println(datetime);
        free(datetime);

        Serial.print("current_hour: ");
        Serial.println(current_hour);
        Serial.print("Free heap: ");
        Serial.println(ESP.getFreeHeap());
        Serial.print("Runtime: ");
        Serial.println(get_runtime());
        Serial.print("reset_reason: ");
        Serial.println(reset_reason);
        Serial.print("ip: ");
        Serial.println(ip);
        Serial.print("t_timer: ");
        Serial.println(t_timer);
        Serial.print("t_loop: ");
        Serial.println(t_loop);
        Serial.print("t_rtc: ");
        Serial.println(t_rtc);
        Serial.print("t_rtc_check: ");
        Serial.println(t_rtc_check);
        Serial.print("telemetry_time_interval start: ");
        Serial.println(telemetry_time_interval[0]);
        Serial.print("telemetry_time_interval stop: ");
        Serial.println(telemetry_time_interval[1]);

        Serial.println("--------------------");
    }
    else
    {
        Serial.print("Unknown command: ");
        Serial.println(cmd);
    }
}

void measure_level()
{
    // placeholder for future
}