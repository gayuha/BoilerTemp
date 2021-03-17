#pragma once

#define SSID "IrishAP01"
#define WIFI_PASSWORD "0523423456ygif"
#define SSID2 "Irisha"
#define WIFI_PASSWORD2 "0523423456ygif"

#define ONE_WIRE_BUS 2
#define NUMBER_OF_SENSORS 3
const DeviceAddress sensorAdresses[NUMBER_OF_SENSORS] = {{0x28, 0x23, 0xD0, 0x77, 0x91, 0x09, 0x02, 0x0C},
                                                         {0x28, 0x95, 0x1B, 0x77, 0x91, 0x04, 0x02, 0x62},
                                                         {0x28, 0x61, 0x64, 0x11, 0x8C, 0x7D, 0x7B, 0x6D}};

#define LED_PIN 2
#define TIME_BETWEEN_MEASUREMENTS 15 * 60 * 1000 // in milliseconds
#define MAX_POINTS_ON_GRAPH 144                  // 15 minutes per point, 144 points, total of 36 hrs

#define MAX_TEMP_MEASUREMENTS 10
#define DELAY_BETWEEN_MEASUREMENTS 100 // in milliseconds
#define MIN_TEMPERATURE -10

#define TIMESTAMP_SIZE 4
#define TEMP_SIZE 1
#define TOTAL_LOG_LINE_SIZE (TIMESTAMP_SIZE + TEMP_SIZE * NUMBER_OF_SENSORS)
#define MAX_BYTES_TO_READ (TOTAL_LOG_LINE_SIZE * MAX_POINTS_ON_GRAPH)

#define BAD_TEMP 157

#define LOGFILE "temp.log"
#define POSITION_FILE "position.log"

#define IP 10, 100, 102, 50
#define GATEWAY 10, 100, 102, 1
#define SUBNET 255, 255, 255, 0

#define TIMEZONE 2

#define NTP_SERVER_1 "pool.ntp.org"
#define NTP_SERVER_2 "time.nist.gov"

#define DEBUG false