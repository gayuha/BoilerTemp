#pragma once

#define SSID "IrishAP01"
#define WIFI_PASSWORD "0523423456ygif"
#define SSID2 "Irisha"
#define WIFI_PASSWORD2 "0523423456ygif"

#define ONE_WIRE_BUS D4
#define NUMBER_OF_SENSORS 3
const DeviceAddress sensorAdresses[NUMBER_OF_SENSORS] = {{0x28, 0x23, 0xD0, 0x77, 0x91, 0x09, 0x02, 0x0C},
                                                         {0x28, 0x95, 0x1B, 0x77, 0x91, 0x04, 0x02, 0x62},
                                                         {0x28, 0x61, 0x64, 0x11, 0x8C, 0x7D, 0x7B, 0x6D}};

#define LED_PIN 2 // Should put an led. built-in interferes with serial
// #define TIME_BETWEEN_MEASUREMENTS 10 * 1000 // in milliseconds
#define TIME_BETWEEN_MEASUREMENTS 10 * 60 * 1000    // in milliseconds
#define TIME_BETWEEN_VALVE_MOVEMENTS 60 * 60 * 1000 // in milliseconds
#define MAX_POINTS_ON_GRAPH 216                     // 10 minutes per point, 216 points, total of 36 hrs
// #define MAX_VALVES_ON_GRAPH ((TIME_BETWEEN_MEASUREMENTS / TIME_BETWEEN_VALVE_MOVEMENTS) * MAX_POINTS_ON_GRAPH)
#define MAX_VALVES_ON_GRAPH 36

#define MAX_TEMP_MEASUREMENTS 10
#define DELAY_BETWEEN_MEASUREMENTS 100 // in milliseconds
#define MIN_TEMPERATURE -10

#define TIMESTAMP_SIZE 4
#define TEMP_SIZE 1
#define TOTAL_LOG_LINE_SIZE (TIMESTAMP_SIZE + TEMP_SIZE * NUMBER_OF_SENSORS)
#define MAX_TEMP_BYTES_TO_READ (TOTAL_LOG_LINE_SIZE * MAX_POINTS_ON_GRAPH)
#define MAX_VALVE_BYTES_TO_READ (TIMESTAMP_SIZE * MAX_VALVES_ON_GRAPH)

#define BAD_TEMP 157

#define LOGFILE "temp.log"
#define VALVE_OPEN_FILE "valveopen.bin"
#define VALVE_CLOSE_FILE "valveclose.bin"
#define DST_FILE "dst"

#define IP 10, 100, 102, 50
#define GATEWAY 10, 100, 102, 1
#define SUBNET 255, 255, 255, 0

#define TIMEZONE 2

#define NTP_SERVER_1 "pool.ntp.org"
#define NTP_SERVER_2 "time.nist.gov"

#define STEP_PIN D6
#define DIR_PIN D5
#define ENABLE_PIN D7

#define STEPS 2000
#define MICROSTEPPING 1

#define VALVE_OPEN_DIR true

#define MOTOR_DELAY_BETWEEN_STEPS 4 // in milliseconds
#define MOTOR_PULLEY_RIDGES 20
#define VALVE_PULLEY_RIDGES 280

#define DEBUG false

#define SERIAL_BAUDRATE 115200

#define VALVE_OPEN true
#define VALVE_CLOSE false