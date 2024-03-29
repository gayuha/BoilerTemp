#pragma once

#define SSID "SSID"
#define WIFI_PASSWORD "PASSWORD"
#define SSID2 "SSID2"
#define WIFI_PASSWORD2 "PASSWORD2"

#define ONE_WIRE_BUS D4
#define NUMBER_OF_SENSORS 3
const DeviceAddress sensorAdresses[NUMBER_OF_SENSORS] = {{0x28, 0x23, 0xD0, 0x77, 0x91, 0x09, 0x02, 0x0C},
                                                         {0x28, 0xFF, 0xD3, 0x74, 0xC0, 0x16, 0x04, 0x8A},
                                                         {0x28, 0x95, 0x1B, 0x77, 0x91, 0x04, 0x02, 0x62}};

#define LED_PIN 2                                   // Should put an led. built-in interferes with serial
#define TIME_BETWEEN_MEASUREMENTS_CLOSED 30 * 1000  // in milliseconds, when valve is closed
#define TIME_BETWEEN_MEASUREMENTS_OPEN 30 * 1000    // in milliseconds, when valve is open
#define TIME_BETWEEN_VALVE_MOVEMENTS 30 * 60 * 1000 // in milliseconds
#define TIME_BETWEEN_RETRIES 1 * 60 * 1000          // in milliseconds
#define TIME_BETWEEN_HTML_REFRESHES 5 * 60 * 1000   // in milliseconds
#define VALVE_CONTROL_STARTUP_DELAY 10 * 60 * 1000  // in milliseconds
#define MAX_POINTS_ON_GRAPH 1500
// #define MAX_VALVES_ON_GRAPH ((TIME_BETWEEN_MEASUREMENTS_CLOSED / TIME_BETWEEN_VALVE_MOVEMENTS) * MAX_POINTS_ON_GRAPH)
#define MAX_VALVES_ON_GRAPH 50
#define MIN_OPEN_TEMP 30 // minimum temperature, below which the valve will not open. if open below that, it will close.

#define MAX_TEMP_MEASUREMENTS 5
#define DELAY_BETWEEN_MEASUREMENTS 200 // in milliseconds
#define MIN_TEMPERATURE -5
#define LOWER_TEMP_CUTOFF_HYSTERESIS 12 // how far we let the temperature fall before closing the valve
#define TEMP_MARGIN_BEFORE_OPENING 3    // how much higher the global temp needs to be above the top temp before opening
#define TEMP_MARGIN_BEFORE_CLOSING 2 // how much hotter the top water needs to be above the global water before closing

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
#define CUTOFF_FILE "cutoff.bin"

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