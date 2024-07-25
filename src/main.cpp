#include <Arduino.h>
#include <HardwareSerial.h>
#include <DS3231.h>
#include <Wire.h>

#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>

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
uint8_t telemetry_time_interval[2] = {9, 22};

// MQTT setup
const char *broker = "194.182.80.42";
const int port = 65535;
const char *user = "enter1";
const char *password = "opurt8";

const char *willTopic = "test/status";
const char *test_data = "test/data";
const char *test_status = willTopic;
uint8_t willQos = 1;
bool willRetain = true;
const char *willMessage = "0";

uint32_t t_timer = 0;
uint32_t t_loop = 15; // min
bool first_run = true;

void reset_sim800l();
void mqttCallback(char *topic, byte *payload, unsigned int len);
void sim800l_init();
void get_remaining_credit();

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

/*------------------------------ WIFI -----------------------------------*/
const char *host = "esp32";
const char *wifi_ssid = "SPARTA";
const char *wifi_password = "9306111078";

WebServer server(80);

/* Style */
String style =
    "<style>#file-input,input{width:100%;height:44px;border-radius:4px;margin:10px auto;font-size:15px}"
    "input{background:#f1f1f1;border:0;padding:0 15px}body{background:#3498db;font-family:sans-serif;font-size:14px;color:#777}"
    "#file-input{padding:0;border:1px solid #ddd;line-height:44px;text-align:left;display:block;cursor:pointer}"
    "#bar,#prgbar{background-color:#f1f1f1;border-radius:10px}#bar{background-color:#3498db;width:0%;height:10px}"
    "form{background:#fff;max-width:258px;margin:75px auto;padding:30px;border-radius:5px;text-align:center}"
    ".btn{background:#3498db;color:#fff;cursor:pointer}</style>";

/* Login page */
String loginIndex =
    "<form name=loginForm>"
    "<h1>ESP32 Login</h1>"
    "<input name=userid placeholder='User ID'> "
    "<input name=pwd placeholder=Password type=Password> "
    "<input type=submit onclick=check(this.form) class=btn value=Login></form>"
    "<script>"
    "function check(form) {"
    "if(form.userid.value=='admin' && form.pwd.value=='admin')"
    "{window.open('/serverIndex')}"
    "else"
    "{alert('Error Password or Username')}"
    "}"
    "</script>" +
    style;

/* Server Index Page */
String serverIndex =
    "<script src='https://ajax.googleapis.com/ajax/libs/jquery/3.2.1/jquery.min.js'></script>"
    "<form method='POST' action='#' enctype='multipart/form-data' id='upload_form'>"
    "<input type='file' name='update' id='file' onchange='sub(this)' style=display:none>"
    "<label id='file-input' for='file'>   Choose file...</label>"
    "<input type='submit' class=btn value='Update'>"
    "<br><br>"
    "<div id='prg'></div>"
    "<br><div id='prgbar'><div id='bar'></div></div><br></form>"
    "<script>"
    "function sub(obj){"
    "var fileName = obj.value.split('\\\\');"
    "document.getElementById('file-input').innerHTML = '   '+ fileName[fileName.length-1];"
    "};"
    "$('form').submit(function(e){"
    "e.preventDefault();"
    "var form = $('#upload_form')[0];"
    "var data = new FormData(form);"
    "$.ajax({"
    "url: '/update',"
    "type: 'POST',"
    "data: data,"
    "contentType: false,"
    "processData:false,"
    "xhr: function() {"
    "var xhr = new window.XMLHttpRequest();"
    "xhr.upload.addEventListener('progress', function(evt) {"
    "if (evt.lengthComputable) {"
    "var per = evt.loaded / evt.total;"
    "$('#prg').html('progress: ' + Math.round(per*100) + '%');"
    "$('#bar').css('width',Math.round(per*100) + '%');"
    "}"
    "}, false);"
    "return xhr;"
    "},"
    "success:function(d, s) {"
    "console.log('success!') "
    "},"
    "error: function (a, b, c) {"
    "}"
    "});"
    "});"
    "</script>" +
    style;

void setup()
{
    // put your setup code here, to run once:
    Serial.begin(115200);

    Serial.println("Starting.... waiting for 10 sec");
    delay(10000);
    Wire.begin(3, 5);

    pinMode(15, OUTPUT);
    pinMode(reset_pin, OUTPUT);

    /* -------------------------------------- wifi and webserver ------------------------------------*/
    // Connect to WiFi network
    WiFi.begin(wifi_ssid, wifi_password);
    Serial.println("");

    // Wait for connection
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }
    Serial.println("");
    Serial.print("Connected to ");
    Serial.println(wifi_ssid);
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    /*use mdns for host name resolution*/
    if (!MDNS.begin(host))
    { // http://esp32.local
        Serial.println("Error setting up MDNS responder!");
        while (1)
        {
            delay(1000);
        }
    }
    Serial.println("mDNS responder started");
    /*return index page which is stored in serverIndex */
    server.on("/", HTTP_GET, []()
              {
        server.sendHeader("Connection", "close");
        server.send(200, "text/html", loginIndex); });
    server.on("/serverIndex", HTTP_GET, []()
              {
        server.sendHeader("Connection", "close");
        server.send(200, "text/html", serverIndex); });
    /*handling uploading firmware file */
    server.on(
        "/update", HTTP_POST, []()
        {
        server.sendHeader("Connection", "close");
        server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
        ESP.restart(); },
        []()
        {
            HTTPUpload &upload = server.upload();
            if (upload.status == UPLOAD_FILE_START)
            {
                Serial.printf("Update: %s\n", upload.filename.c_str());
                if (!Update.begin(UPDATE_SIZE_UNKNOWN))
                { // start with max available size
                    Update.printError(Serial);
                }
            }
            else if (upload.status == UPLOAD_FILE_WRITE)
            {
                /* flashing firmware to ESP*/
                if (Update.write(upload.buf, upload.currentSize) != upload.currentSize)
                {
                    Update.printError(Serial);
                }
            }
            else if (upload.status == UPLOAD_FILE_END)
            {
                if (Update.end(true))
                { // true to set the size to the current progress
                    Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
                }
                else
                {
                    Update.printError(Serial);
                }
            }
        });
    server.begin();
    /* ----------------------------------- end of wifi and webserver --------------------------------*/

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

        char *datetime = get_datetime();
        Serial.println(datetime);
        free(datetime);

        String res = (String)current_hour;
        bool posted = mqtt.publish(test_data, res.c_str());

        i++;
        first_run = false;
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
        i++;

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
            Serial.println("Time is out of 6h and 22h");
        }

        Serial.print("Freeheap: ");
        Serial.println(ESP.getFreeHeap());
        Serial.println("");

        Serial.println("--------------------------------------------------------");
    }

    if (ESP.getFreeHeap() < 10000)
    {
        ESP.restart();
    }

    server.handleClient();
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
    mqtt.setKeepAlive(3600);

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

    get_remaining_credit();

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
    mqtt.setKeepAlive(3600);
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

void get_remaining_credit()
{

    // USSD command for checking balance
    String ussd_cmd = "AT+CUSD=1,\"*103#\""; // Change *103# to your network's USSD code
    String response = modem.sendUSSD(ussd_cmd.c_str());
    Serial.println(response);
}
