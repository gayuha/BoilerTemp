#define SSID "IrishAP01"
#define WIFI_PASSWORD "0523423456ygif"
#define SSID2 "Irisha"
#define WIFI_PASSWORD2 "0523423456ygif"

#define ONE_WIRE_BUS 2
const DeviceAddress sensor1Address = {0x28, 0x23, 0xD0, 0x77, 0x91, 0x09, 0x02, 0x0C};
const DeviceAddress sensor2Address = {0x28, 0x95, 0x1B, 0x77, 0x91, 0x04, 0x02, 0x62};

#define LED_PIN 2
#define TIME_BETWEEN_MEASUREMENTS 5 * 60 * 1000 // in milliseconds

#define LOGFILE "temp.log"

#define IP 10, 100, 102, 50
#define GATEWAY 10, 100, 102, 1
#define SUBNET 255, 255, 255, 0

#define TIMEZONE 2
#define DST 0