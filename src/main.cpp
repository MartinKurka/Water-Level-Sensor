#include <Arduino.h>
#include <HardwareSerial.h>
#include <DS3231.h>
#include <Wire.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <VL53L0X.h>
// #include <ArduinoHttpClient.h>
#include <HTTPClient.h>
#include <Update.h>
#include "time.h"

#include "SETTINGS.h"
#include "MQTT_SETTINGS.h"
#include "MODEM_SETTINGS.h"

// HttpClient http(client, serverAddress, ntp_port);
// HTTPClient http;

struct tm machine_rtc;

void reset_sim800l();
void mqttCallback(char *topic, byte *payload, unsigned int len);
void sim800l_init();
void getAPItime();
void get_machine_time();
void led_blink(int count);

boolean mqttConnect();
boolean _IsNetworkConnected();
boolean _IsGPRSConnected();
boolean _IsMQTTConnected();
boolean check_connection();

// char *get_datetime();
// char *get_runtime();

void setup()
{
    esp_task_wdt_init(60, true); // 60 seconds timeout, true to panic on timeout
    esp_task_wdt_add(NULL);     // Add the current thread (main loop) to the watchdog

    pinMode(reset_pin, OUTPUT);
    pinMode(led_pin, OUTPUT);
    digitalWrite(led_pin, HIGH);
    // put your setup code here, to run once:
    Serial.begin(115200);

    Serial.println("Starting.... waiting for 10 sec");
    delay(5000);

    // dfrobot_beetle_esp32c3
    Serial1.begin(115200, SERIAL_8N1, rxpin, txpin);

    sim800l_init();

    Serial.println("--------------------------------------------------------");
    Serial.print("reset_reason: ");
    Serial.println(reset_reason);

    // bool pub_heartbeat = mqtt.publish(test_heartbeat, "1");
    // Serial.printf("pub_heartbeat: %o \n", pub_heartbeat);

    getAPItime();
    Serial.println();
    get_machine_time();

    esp_task_wdt_reset();
}

void loop()
{
    uint32_t timer = millis();

    // heartbeat loop
    if (timer - t_heartbeat_timer >= (t_heartbeat_loop * 60 * 1000))
    {
        
        Serial.println("Heartbeat Loop ------------------------------------------");
        if (!is_time_updated)
        {
            getAPItime();
        }

        t_heartbeat_timer = timer;
        digitalWrite(led_pin, LOW);
        get_machine_time();
        Serial.printf("Current hour: %d\n", current_hour);
        /*    
        #define MQTT_CONNECTION_TIMEOUT     -4
        #define MQTT_CONNECTION_LOST        -3
        #define MQTT_CONNECT_FAILED         -2
        #define MQTT_DISCONNECTED           -1
        #define MQTT_CONNECTED               0
        #define MQTT_CONNECT_BAD_PROTOCOL    1
        #define MQTT_CONNECT_BAD_CLIENT_ID   2
        #define MQTT_CONNECT_UNAVAILABLE     3
        #define MQTT_CONNECT_BAD_CREDENTIALS 4
        #define MQTT_CONNECT_UNAUTHORIZED    5
        */
        Serial.printf("MQTT STATE: %d \n", mqtt.state());
        mqtt.loop();

        check_connection();

        if (mqtt.state() == MQTT_CONNECTED)
        {
            bool pub_heartbeat = mqtt.publish(test_heartbeat, "1");
            Serial.printf("pub_heartbeat: %o \n", pub_heartbeat);
            delay(1);
            digitalWrite(led_pin, LOW);
        }
        else
        {
            Serial.printf("Fail publish, Code: %d \n", mqtt.state());
        }
        Serial.println("Heartbeat Loop End --------------------------------------");
        esp_task_wdt_reset();
    }

    // Mainloop
    if (timer - t_timer >= (t_loop * 60 * 1000))
    {
        
        Serial.println("Main Loop -----------------------------------------------");
        if (!is_time_updated)
        {
            getAPItime();
        }

        t_timer = timer;
        digitalWrite(led_pin, LOW);
        get_machine_time();
        Serial.printf("MQTT STATE: %d \n", mqtt.state());
        Serial.println("MainLoop");
        mqtt.loop();

        check_connection();

        if (mqtt.state() == MQTT_CONNECTED)
        {
            bool pub_mainloop = mqtt.publish(test_info, "1");
            Serial.printf("pub_mainloop: %o \n", pub_mainloop);
            delay(1);
            digitalWrite(led_pin, LOW);
        }
        else
        {
            Serial.printf("Fail publish, Code: %d \n", mqtt.state());
        }
        Serial.println("Main Loop End -------------------------------------------");
        esp_task_wdt_reset();
    }

    esp_task_wdt_reset();
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
    delay(10);
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
    delay(10);
}

boolean mqttConnect()
{
    esp_task_wdt_reset();
    Serial.print("Connecting to ");
    Serial.print(broker);

    // Connect to MQTT Broker
    boolean status = mqtt.connect("sim800L_test", user, password, willTopic, willQos, willRetain, willMessage);
    mqtt.setKeepAlive(Keepalive);

    if (status == false)
    {
        Serial.println(" fail");
        return false;
    }
    Serial.println(" success");

    // bool posted = mqtt.publish(test_status, "Online");

    // Subscibe topic
    mqtt.subscribe(test_get);
    delay(100);

    esp_task_wdt_reset();

    return mqtt.connected();
}


void sim800l_init()
{
    int attempts = 0;
    reset_sim800l();
    delay(2000);

    esp_task_wdt_reset();

    String modemInfo = modem.getModemInfo();
    Serial.print("Modem Info: ");
    Serial.println(modemInfo);

    if (GSM_PIN && modem.getSimStatus() != 3)
    {
        modem.simUnlock(GSM_PIN);
    }

    esp_task_wdt_reset();

    /* =========================== CELL NETWORK CONNECTION =========================== */

    Serial.print("Waiting for network...");
    int timeout_ms = 60000;
    bool net_connected = false;

    modem.getRegistrationStatus();

    for (uint32_t start = millis(); millis() - start < timeout_ms;)
    {
        // Serial.printf("time %d, timeout = %d, rssi: %d, cell: %o\n", (millis() - start), timeout_ms, modem.getSignalQuality(), modem.isNetworkConnected());

        if (modem.getSignalQuality() > 0 && modem.isNetworkConnected())
        {
            Serial.print(" success\n");
            net_connected = true;
            break;
        }
        delay(200);
        yield();
        esp_task_wdt_reset();
    }

    if (!net_connected && !modem.isNetworkConnected())
    {
        Serial.println("Connection to cell network failed, do board reset");
        led_blink(100);
        delay(1000);
        ESP.restart();
    }

    Serial.println("Connected to network");

    esp_task_wdt_reset();

    Serial.print("Signal: ");
    rssi = modem.getSignalQuality();
    Serial.println(rssi);

    led_blink(2);

    /* =========================== GPRS CONNECTION =========================== */

    Serial.print(F("Connecting to "));
    Serial.println(apn);

    bool gprs_connected = false;

    modem.gprsDisconnect();
    modem.gprsConnect(apn, gprsUser, gprsPass);

    for (uint32_t start = millis(); millis() - start < timeout_ms;)
    {
        Serial.printf("time %d, timeout = %d, GPRS: %d\n", (millis() - start), timeout_ms, modem.isGprsConnected());

        if (modem.isGprsConnected())
        {
            Serial.println(" success");
            gprs_connected = true;
            break;
        }
        delay(200);
        yield();
        esp_task_wdt_reset();
    }
    if (!gprs_connected && !modem.isGprsConnected())
    {
        Serial.println("Connection to GPRS failed, do board reset");
        led_blink(1000);
        delay(1000);
        ESP.restart();
    }

    esp_task_wdt_reset();

    Serial.print("IP: ");
    ip = modem.getLocalIP();
    Serial.println(ip);

    led_blink(2);

    /* =========================== MQTT CONNECTION =========================== */

    // MQTT Broker setup
    mqtt.setServer(broker, mqtt_port);
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

    esp_task_wdt_reset();

    led_blink(2);

    digitalWrite(led_pin, LOW);
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
    esp_task_wdt_reset();
    return modem.isNetworkConnected();
}
boolean _IsGPRSConnected()
{
    esp_task_wdt_reset();
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
    esp_task_wdt_reset();
    return modem.isGprsConnected();
}
boolean _IsMQTTConnected()
{
    if (!mqtt.connected())
    {
        Serial.println("Disconnected from MQTT Broker");
        mqttConnect();
    }
    return mqtt.connected();
}

void getAPItime()
{
    long unixTime = 0;
    String timeString = "";
    check_connection();
    // Connect to server
    if (client.connect("194.182.80.42", 80)) {
        client.println("GET /unixtime HTTP/1.1");
        client.println("Host: 194.182.80.42");
        client.println("Connection: close");
        client.println(); // End of headers

        // Wait for response with timeout
        unsigned long startTime = millis();
        bool responseReceived = false;

        // Wait for response
        while (client.connected() || client.available()) {
            if (client.available()) {
                String line = client.readStringUntil('\n');
                Serial.println(line); // Print response
                responseReceived = true; // Mark response received

                // {'unixtime': 1727069196, 'datetime': '2024-09-23T05:26:36.714195+00:00'}
                int timeIndex = line.indexOf("\"unixtime\": ") + 14;
                Serial.printf("timeIndex: %d\n", timeIndex);
                timeString = line.substring(timeIndex, line.indexOf(',', timeIndex));

                // Convert the extracted time string to a long integer
                unixTime = timeString.toInt();  // Use toLong() if toInt() causes overflow

                // Correct print statements
                Serial.printf("Extracted timeString: _%s_\n", timeString.c_str());  // Use %s for String
                Serial.printf("Converted unixTime: %ld\n", unixTime);  // Use %ld for long integers
                Serial.println("--------------------------");
            }

            // Check for timeout (e.g., 5000 ms)
            if (millis() - startTime > 5000) {
                Serial.println("Timeout waiting for response");
                break;
            }
        }

        if (!responseReceived) {

            Serial.println("No response received");
        }

        Serial.printf("time: %d\n", timeString);
        // Add 2 hours (7200 seconds) to the UNIX timestamp
        unixTime += 7200;

        Serial.printf("unixTime: %d\n", unixTime);

        // Set the time from the adjusted UNIX timestamp
        struct timeval tv;
        tv.tv_sec = unixTime;
        tv.tv_usec = 0;
        settimeofday(&tv, NULL);

        is_time_updated = true;

        client.stop();        

    } else {
        Serial.println("Connection to server failed");
    }

    // Serial.println("GET Time From API");

    // check_connection();

    // http.setTimeout(20000);
    // http.connectionKeepAlive();

    // // Send GET request to the server
    // Serial.println("Attempting to send HTTP GET request");
    // int result = http.get(timeAPI);
    // Serial.printf("\nHTTP GET request sent, result: %d\n", result);

    // /*
    // Read the response status code and body
    // HTTP_SUCCESS =0
    // HTTP_ERROR_CONNECTION_FAILED =-1
    // HTTP_ERROR_API =-2
    // HTTP_ERROR_TIMED_OUT =-3
    // HTTP_ERROR_INVALID_RESPONSE =-4
    // */

    // int statusCode = http.responseStatusCode();
    // Serial.printf("Status %d", statusCode);

    // if (statusCode == 200)
    // {
    //     String response = http.responseBody();
    //     Serial.println("Received time data: " + response);

    //     // Extract the UNIX timestamp from the response
    //     int timeIndex = response.indexOf("\"unixtime\":") + 11;
    //     String timeString = response.substring(timeIndex, response.indexOf(',', timeIndex));
    //     long unixTime = timeString.toInt();

    //     // Add 2 hours (7200 seconds) to the UNIX timestamp
    //     unixTime += 7200;

    //     // Set the time from the adjusted UNIX timestamp
    //     struct timeval tv;
    //     tv.tv_sec = unixTime;
    //     tv.tv_usec = 0;
    //     settimeofday(&tv, NULL);

    //     Serial.println("Internal RTC time updated with +2h offset!");
    //     is_time_updated = true;
    // }
    // else
    // {
    //     Serial.println("Failed to get time from server, status code: " + String(statusCode));
    //     is_time_updated = false;
    // }
    // esp_task_wdt_reset();
    // http.stop();
}

void get_machine_time()
{
    // Regularly print the current time
    if (getLocalTime(&machine_rtc))
    {
        Serial.printf("Current time: %02d:%02d:%02d %02d/%02d/%04d\n",
                      machine_rtc.tm_hour, machine_rtc.tm_min, machine_rtc.tm_sec,
                      machine_rtc.tm_mday, machine_rtc.tm_mon + 1, machine_rtc.tm_year + 1900);
        current_hour = machine_rtc.tm_hour;
    }
    else
    {
        Serial.println("Failed to obtain time");
    }
    esp_task_wdt_reset();
}

boolean check_connection()
{
    bool net_status = modem.isNetworkConnected();
    bool gprs_status = modem.isGprsConnected();
    bool mqtt_status = mqtt.connected();
    Serial.printf("CONN - mqtt: %o, gprs: %o, net: %o \n", mqtt_status, gprs_status, net_status);

    if (!mqtt_status && gprs_status && net_status)
    {
        Serial.println("CONN - Reconnection to MQTT Broker");
        mqtt.disconnect();
        Serial.printf("CONN - MQTT connected: %o\n", mqttConnect());
    }

    else if (!gprs_status && net_status)
    {
        Serial.println("CONN - Reconnection to GPRS");
        mqtt.disconnect();
        Serial.printf("CONN - GPRS connected: %o\n", _IsGPRSConnected());
        Serial.printf("CONN - MQTT connected: %o\n", mqttConnect());
    }

    else if (!net_status)
    {
        Serial.println("CONN - Reconnection to Cell network");
        mqtt.disconnect();
        modem.gprsDisconnect();
        sim800l_init();
        net_status = modem.isNetworkConnected();
        gprs_status = modem.isGprsConnected();
        mqtt_status = mqtt.connected();
        Serial.printf("CONN - Reconnection mqtt: %o, gprs: %o, net: %o \n", mqtt_status, gprs_status, net_status);
    }
    esp_task_wdt_reset();

    return mqtt_status && gprs_status && net_status;
}

void led_blink(int count)
{
    if (led_pin && (led_blink > 0))
    {
        for (int i = 0; i <= count; i++)
        {
            digitalWrite(led_pin, LOW);
            delay(250);
            digitalWrite(led_pin, HIGH);
            delay(250);
            yield();
            esp_task_wdt_reset();
        }
    }
}