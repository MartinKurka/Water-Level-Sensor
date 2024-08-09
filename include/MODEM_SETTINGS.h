#define GSM_PIN ""
#define TINY_GSM_MODEM_SIM800
#define TINY_GSM_USE_GPRS true

#include <HardwareSerial.h>
HardwareSerial SerialAT(2); // RX - 16, TX - 17

const char apn[] = "internet.t-mobile.cz";
const char gprsUser[] = "";
const char gprsPass[] = "";

#include <TinyGsmClient.h>
#include <PubSubClient.h>

TinyGsm modem(SerialAT);
TinyGsmClient client(modem);
PubSubClient mqtt(client);

String ip = "";
int8_t rssi = 0;
