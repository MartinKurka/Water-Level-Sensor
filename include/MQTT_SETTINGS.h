// MQTT broker setup
const char *broker = "194.182.80.42";
const int port = 65535;
const char *user = "enter1";
const char *password = "opurt8";
const bool RETAINED = true;
unsigned int Keepalive = 300;

// info topics
const char *willTopic = "test/status";
const char *test_data = "test/data";
const char *test_rssi = "test/rssi";
const char *test_ip = "test/ip";
const char *test_reset_cause = "test/reset_cause";
const char *test_info = "test/info";
const char *test_update = "test/info/update";
const char *test_fail = "test/fail";

// command topics
const char *test_start = "test/start";          // payload: "6"
const char *test_stop = "test/stop";            // payload: "22"
const char *test_telemetry = "test/telemetry";  // payload: "30"
const char *test_reset = "test/reset";          // payload: "reset"
const char *test_get = "test/get";              // payload: "all, update"

// last will setup
const char *test_status = willTopic;
unsigned int willQos = 1;
bool willRetain = true;
const char *willMessage = "0";