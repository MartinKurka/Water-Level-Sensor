#include <Arduino.h>
#include <HardwareSerial.h>
#include <DS3231.h>
#include <Wire.h>

uint32_t i = 1;
uint8_t rxpin = 9;
uint8_t txpin = 11;
uint8_t reset_pin = 12;

// SIM800L
#define TINY_GSM_MODEM_SIM800
#define TINY_GSM_USE_GPRS true

#include <TinyGsmClient.h>
#include <PubSubClient.h>

TinyGsm modem(Serial1);
TinyGsmClient client(modem);
PubSubClient mqtt(client);

#define GSM_PIN ""

const char apn[] = "internet.t-mobile.cz";
const char gprsUser[] = "";
const char gprsPass[] = "";

// MQTT setup
const char *broker = "194.182.80.42";
const int port = 65535;
const char *user = "enter1";
const char *password = "opurt8";
const boolean RETAINED = true;
uint16_t Keepalive = 300;

// Topics
const char *willTopic = "test/status";
const char *test_data = "test/data";
const char *test_status = willTopic;
uint8_t willQos = 1;
bool willRetain = true;
const char *willMessage = "0";

uint32_t t_timer = 0;
uint32_t t_loop = 30;   // min

uint32_t t_rtc = 0;
uint32_t t_rtc_check = 10;  // min

bool first_run = true;
int telemetry_time_interval[2] = {6, 22};
void reset_sim800l();
void mqttCallback(char *topic, byte *payload, unsigned int len);
void sim800l_init();

boolean mqttConnect();
boolean _IsNetworkConnected();
boolean _IsGPRSConnected();
boolean _IsMQTTConnected();

char *get_datetime();

DS3231 myRTC;
bool century = false;
bool h12Flag;
bool pmFlag;
uint8_t current_hour = 0;

const uint8_t buffer_len = 1;
char buffer[buffer_len];

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
        Serial.println(current_hour);

        Serial.print("Freeheap: ");
        Serial.println(ESP.getFreeHeap());
        
        Serial.println("--------------------------------------------------------");
    }

    // mainloop
    if (timer - t_timer >= (t_loop * 60 * 1000))
    {
        t_timer = timer;
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
        }

        Serial.print("Freeheap: ");
        Serial.println(ESP.getFreeHeap());
        Serial.println("");

        Serial.println("--------------------------------------------------------");        
        i++;
    }
    
    if (timer - t_rtc >= (t_rtc_check * 60 * 1000))
    {
        t_rtc = timer;
        char *datetime = get_datetime();
        Serial.println(datetime);
        free(datetime);

        Serial.print("current_hour: ");
        Serial.println(current_hour);
    }

    if (ESP.getFreeHeap() < 10000)
    {
        ESP.restart();
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
    Serial.println("Message arrived [");
    Serial.print(topic);
    Serial.print("]: ");
    Serial.write(payload, len);
    Serial.println("");
    Serial.print("payload: ");
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
    Serial.println(modem.getSignalQuality());

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
    Serial.println(modem.getLocalIP());

    // MQTT Broker setup
    mqtt.setServer(broker, port);    
    mqtt.setKeepAlive(Keepalive);
    mqtt.setCallback(mqttCallback);
    mqttConnect();
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