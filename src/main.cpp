#include <Arduino.h>
#include <HardwareSerial.h>
#include <DS3231.h>
#include <Wire.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <VL53L0X.h>
#include <HTTPClient.h>
#include <Update.h>
#include "time.h"
#include <Ticker.h>

#include "SETTINGS.h"
#include "MQTT_SETTINGS.h"
#include "MODEM_SETTINGS.h"

/* Functions */
void wait_loop(uint32_t time_ms);
void reset_sim800l();
void mqttCallback(char *topic, byte *payload, unsigned int len);
void sim800l_init();
void getAPItime();
void get_machine_time();
void f_led_blink(int count);
void f_heartbeat();
void f_mainloop();
void f_mqttloop();
void f_ledloop();

boolean mqttConnect();
boolean _IsGPRSConnected();
boolean _IsMQTTConnected();
boolean check_connection();


void setup()
{
    Serial.println("**************************** SETUP ****************************");
    esp_task_wdt_init(60, true); // 60 seconds timeout, true to panic on timeout
    esp_task_wdt_add(NULL);     // Add the current thread (main loop) to the watchdog

    pinMode(reset_pin, OUTPUT);     // Setup reset pin

    pinMode(led_pin, OUTPUT);       // LED control pin
    digitalWrite(led_pin, HIGH);
    
    Serial.begin(115200);           // Debug serial console

    Serial.println("Starting.... waiting for 10 sec");
    wait_loop(5000);

    Serial1.begin(115200, SERIAL_8N1, rxpin, txpin);    // SIM800L serial port

    esp_task_wdt_reset();
    sim800l_init();

    // Timers
    T_mqttloop.attach(t_mqtt_timer, f_mqttloop);
    T_ledloop.attach(connected_led_blink_interval, f_ledloop);

    Serial.print("reset_reason: ");
    Serial.println(reset_reason);

    // bool pub_heartbeat = mqtt.publish(test_heartbeat, "1");
    // Serial.printf("pub_heartbeat: %o \n", pub_heartbeat);

    getAPItime();
    get_machine_time();

    f_heartbeat();

    esp_task_wdt_reset();
    
    Serial.println("************************** SETUP END **************************");
}

void loop()
{
    uint32_t timer = millis();

    // Mainloop
    if (timer - t_timer >= (t_loop * 60 * 1000))
    {
        t_timer = timer;
        f_mainloop();

    }

    // Heartbeat
    if (timer - t_heartbeat_timer >= (t_heartbeat_loop * 60 * 1000))
    {
        t_heartbeat_timer = timer;
        f_heartbeat();
    }

    esp_task_wdt_reset();
}

void reset_sim800l()
{
    Serial.println("Reseting SIM800L....");
    digitalWrite(reset_pin, LOW);

    wait_loop(2000);
    digitalWrite(reset_pin, HIGH);

    Serial.println("Reset SIM800L - Done!");
}

void wait_loop(uint32_t time_ms)
{
    uint32_t resetStartTime = millis();
    uint32_t loop_counter = 0;
    while(millis() - resetStartTime < time_ms)
    {
        loop_counter += 1;
    }
}

void mqttCallback(char *topic, byte *payload, unsigned int len)
{
    Serial.print("\nMessage arrived [ ");
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
    wait_loop(10);
}

boolean mqttConnect()
{
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

    return mqtt.connected();
}


void sim800l_init()
{
    int attempts = 0;
    reset_sim800l();
    wait_loop(2000);

    Serial.printf("Modem name: %s\n", modem.getModemName());
    Serial.printf("Modem Info: _ %s _\n",modem.getModemInfo());
    Serial.printf("Modem Operable: %o\n", modem.testAT());

    if (GSM_PIN && modem.getSimStatus() != 3)
    {
        modem.simUnlock(GSM_PIN);
    }

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
        wait_loop(200);
    }

    if (!net_connected && !modem.isNetworkConnected())
    {
        Serial.println("Connection to cell network failed, do board reset");
        T_ledloop.detach();
        f_led_blink(100);
        wait_loop(1000);
        ESP.restart();
    }

    Serial.println("Connected to network");

    Serial.print("Signal: ");
    rssi = modem.getSignalQuality();
    Serial.println(rssi);

    f_led_blink(4);

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
    }
    if (!gprs_connected && !modem.isGprsConnected())
    {
        Serial.println("Connection to GPRS failed, do board reset");
        f_led_blink(1000);
        wait_loop(1000);
        ESP.restart();
    }

    Serial.print("IP: ");
    ip = modem.getLocalIP();
    Serial.println(ip);

    f_led_blink(4);

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

    f_led_blink(4);

    digitalWrite(led_pin, LOW);
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
                responseReceived = true; // Mark response received

                // {'unixtime': 1727069196, 'datetime': '2024-09-23T05:26:36.714195+00:00'}
                int timeIndex = line.indexOf("\"unixtime\": ") + 14;
                timeString = line.substring(timeIndex, line.indexOf(',', timeIndex));

                // Convert the extracted time string to a long integer
                unixTime = timeString.toInt();  // Use toLong() if toInt() causes overflow
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
}

boolean check_connection()
{
    bool net_status = modem.isNetworkConnected();
    bool gprs_status = modem.isGprsConnected();
    bool mqtt_status = mqtt.connected();
    Serial.printf("CONN - mqtt: %o, gprs: %o, net: %o \n", mqtt_status, gprs_status, net_status);

    if (!mqtt_status && gprs_status && net_status)
    {
        f_led_blink(4);

        Serial.println("CONN - Reconnection to MQTT Broker");
        mqtt.disconnect();
        Serial.printf("CONN - MQTT connected: %o\n", mqttConnect());
    }

    else if (!gprs_status && net_status)
    {
        f_led_blink(4);

        Serial.println("CONN - Reconnection to GPRS");
        mqtt.disconnect();
        Serial.printf("CONN - GPRS connected: %o\n", _IsGPRSConnected());
        Serial.printf("CONN - MQTT connected: %o\n", mqttConnect());
    }

    else if (!net_status)
    {
        f_led_blink(4);

        Serial.println("CONN - Reconnection to Cell network");
        mqtt.disconnect();
        modem.gprsDisconnect();
        sim800l_init();
        net_status = modem.isNetworkConnected();
        gprs_status = modem.isGprsConnected();
        mqtt_status = mqtt.connected();
        Serial.printf("CONN - Reconnection mqtt: %o, gprs: %o, net: %o \n", mqtt_status, gprs_status, net_status);
    }

    return mqtt_status && gprs_status && net_status;
}

void f_led_blink(int count)
{
    T_ledloop.detach();

    if (led_pin && (count > 0))
    {
        for (int i = 0; i <= count; i++)
        {
            digitalWrite(led_pin, LOW);
            wait_loop(250);
            digitalWrite(led_pin, HIGH);
            wait_loop(250);
        }
    }

    wait_loop(250);
    T_ledloop.attach(connected_led_blink_interval, f_ledloop);
}

void f_heartbeat()
{
    Serial.println("\n------------------------ HEARTBEAT LOOP -----------------------");
    if (!is_time_updated)
    {
        getAPItime();
    }

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

    check_connection();

    if (mqtt.state() == MQTT_CONNECTED)
    {
        bool pub_heartbeat = mqtt.publish(test_heartbeat, "1");
        Serial.printf("pub_heartbeat: %o \n", pub_heartbeat);
        wait_loop(1);
        digitalWrite(led_pin, LOW);
    }
    else
    {
        Serial.printf("Fail publish, Code: %d \n", mqtt.state());
    }
    Serial.printf("Free heap: %d\n", ESP.getFreeHeap());
    Serial.println("---------------------- HEARTBEAT LOOP END ---------------------");
}

void f_mainloop()
{
    Serial.println("\n>>>>>>>>>>>>>>>>>>>>>>>>>> MAIN LOOP >>>>>>>>>>>>>>>>>>>>>>>>>>");
    if (!is_time_updated)
    {
        getAPItime();
    }

    get_machine_time();
    Serial.printf("MQTT STATE: %d \n", mqtt.state());

    check_connection();

    if (mqtt.state() == MQTT_CONNECTED)
    {
        bool pub_mainloop = mqtt.publish(test_info, "1");
        Serial.printf("pub_mainloop: %o \n", pub_mainloop);
        wait_loop(1);
        digitalWrite(led_pin, LOW);
    }
    else
    {
        Serial.printf("Fail publish, Code: %d \n", mqtt.state());
    }
    Serial.println(">>>>>>>>>>>>>>>>>>>>>>>> MAIN LOOP END >>>>>>>>>>>>>>>>>>>>>>>>");
}

void f_mqttloop()
{
    Serial.print(".");
    mqtt.loop();
}

void f_ledloop()
{
    led_status = !led_status;
    digitalWrite(led_pin, led_status);
}