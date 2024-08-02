#define GSM_PIN ""
#define TINY_GSM_MODEM_SIM800
#define TINY_GSM_USE_GPRS true

const char apn[] = "internet.t-mobile.cz";
const char gprsUser[] = "";
const char gprsPass[] = "";

#include <TinyGsmClient.h>
#include <PubSubClient.h>

TinyGsm modem(Serial1);
TinyGsmClient client(modem);
PubSubClient mqtt(client);
