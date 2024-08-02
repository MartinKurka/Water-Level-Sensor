#define GSM_PIN ""
const char apn[] = "internet.t-mobile.cz";
const char gprsUser[] = "";
const char gprsPass[] = "";

// SIM800L
#define TINY_GSM_MODEM_SIM800
#define TINY_GSM_USE_GPRS true

#include <TinyGsmClient.h>
#include <PubSubClient.h>

TinyGsm modem(Serial1);
TinyGsmClient client(modem);
PubSubClient mqtt(client);

String ip = "";
String jsonData = "";
int8_t rssi = 0;
