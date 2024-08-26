// MQTT broker setup
const char *broker = "194.182.80.42";
const int mqtt_port = 65535;
const char *user = "enter1";
const char *password = "opurt8";
const bool RETAINED = true;
unsigned int Keepalive = 400;

// info topics
const char *willTopic = "GSMCottage/water/status";
const char *test_level = "GSMCottage/water/level";
const char *test_hour = "GSMCottage/water/info/hour";
const char *test_rssi = "test/rssi";
const char *test_ip = "test/ip";
const char *test_reset_cause = "test/reset_cause";
const char *test_info = "test/mainloop";
const char *test_update = "GSMCottage/water/info/update";
const char *test_fail = "GSMCottage/water/info/fail";
const char *test_heartbeat = "test/hb";

// command topics
const char *test_start = "GSMCottage/water/cmd/start";          // payload: "6"
const char *test_stop = "GSMCottage/water/cmd/stop";            // payload: "22"
const char *test_telemetry = "GSMCottage/water/cmd/telemetry";  // payload: "30"
const char *test_reset = "GSMCottage/water/cmd/reset";          // payload: "reset"
const char *test_get = "GSMCottage/water/cmd/get";              // payload: "all, update, level"
// last will setup
const char *test_status = willTopic;
unsigned int willQos = 1;
bool willRetain = true;
const char *willMessage = "Offline";