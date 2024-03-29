#include <Arduino.h>

#include <DallasTemperature.h>

#include <ESP8266WiFi.h>
#include <FS.h>
#include <LittleFS.h>
#include <time.h>

#include <sstream>

#include "Configuration.h"
#include "dst.h"
#include "index.h"
#include "scripts.h"

int ledStatus = LOW;

const char* const logFileName = LOGFILE;
const char* const dstFileName = DST_FILE;
const char* const valveOpenFileName = VALVE_OPEN_FILE;
const char* const valveCloseFileName = VALVE_CLOSE_FILE;
const char* const cutoffFileName = CUTOFF_FILE;

unsigned long timeBetweenMeasurements = TIME_BETWEEN_MEASUREMENTS_OPEN;
unsigned long lastTimeMeasured = millis() - timeBetweenMeasurements;
unsigned long lastTimeValveMoved = millis() - TIME_BETWEEN_VALVE_MOVEMENTS + VALVE_CONTROL_STARTUP_DELAY;

// Valve status. Open means water can flow.
bool valveIsOpen = true;

bool tempsAreLogged = true;
bool historyIsUpdated = false;
String lastMeasurementTimestamp;

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

int dst = 0;

WiFiServer server(80);

byte lastTemps[NUMBER_OF_SENSORS];
byte lastLastTemps[NUMBER_OF_SENSORS];
byte tempQuality[NUMBER_OF_SENSORS];
int lastGoodTemps = 0;
byte lowerTempCutoff;
time_t* lastTempsTime;
bool droppedTemps = false;

#define TEMP_GLOBAL lastTemps[0]
#define TEMP_INSIDE lastTemps[1]
#define TEMP_TOP lastTemps[2]

String points = "";
String bytesToString_out = "";
byte* history;
String historyHex;
String valveOpenings;
String valveClosings;

int positionTemp, positionOpen, positionClose;

bool temperatureError = false;
bool debug = DEBUG;

// Stepper stepper((STEPS * MICROSTEPPING), STEP_PIN, DIR_PIN);

// Declarations
void configureTime();
void logSD(String filename, String line);
void rename(String from, String to);
float getTemp(const DeviceAddress sensorAddress);
void toggleLED(bool on);
void toggleLED();
bool handleRequest(String request, WiFiClient client);
const String getTime(time_t);
void logTemps();
uint32_t parseTime(const byte* bytes);
void updateHistory();
byte* readFileToByteArray(const char* path, int length);
void appendFileAtPosition(const char* path, const byte* message, size_t size, int pos);

// Functions
void setDST() {
    time_t now;
    while ((now = time(nullptr)) < 1000) {
        delay(200);
    }
    // bool newDST = isDST(now);
    byte* dstByte = readFileToByteArray(dstFileName, 1);
    if (dstByte == nullptr) {
        dstByte = (byte*)malloc(1);
        dstByte[0] = 0;
    }
    bool newDST = dstByte[0] & 1;
    bool isDiff = (dst != newDST);
    dst = newDST;
    if (isDiff) {
        appendFileAtPosition(dstFileName, dstByte, 1, 0);
        configureTime();
    }
    free(dstByte);
}

void configureTime() {
    configTime(TIMEZONE * 3600, dst * 3600, NTP_SERVER_1, NTP_SERVER_2);
    delay(500);
    setDST();
}

String byteToString(const byte _byte) {
    String out = "";
    for (int j = 7; j >= 0; j--) {
        out += (_byte & (1 << j)) ? "1" : "0";
    }
    return out;
}

String& bytesToString(const byte* bytes, int count = MAX_TEMP_BYTES_TO_READ) {
    Serial.println("Converting bytes to String!");
    bytesToString_out = "";
    if (bytes == nullptr) {
        Serial.println("Bytes are nullptr");
        return bytesToString_out;
    }
    for (int i = 0; i < count; i++) {
        // Serial.print(String(i) + ", ");
        if (i % TOTAL_LOG_LINE_SIZE == 0) {
            bytesToString_out += "\n";
        }
        for (int j = 1; j <= NUMBER_OF_SENSORS; j++) {
            if (i % TOTAL_LOG_LINE_SIZE == (TOTAL_LOG_LINE_SIZE - TEMP_SIZE * j)) {
                bytesToString_out += " ";
                break;
            }
        }
        bytesToString_out += byteToString(bytes[i]);
    }
    Serial.printf("Converted String length is %d\n", bytesToString_out.length());
    return bytesToString_out;
}

int getTempPosition() {
    updateHistory();
    if (history == nullptr) {
        return 0;
    }
    uint32_t latestTime = 0;
    int latestPosition = 0;
    // Serial.printf("Position: %d\n", positionTemp);
    for (int pointIndex = 0; pointIndex < MAX_POINTS_ON_GRAPH; pointIndex++) {
        // Serial.printf("pointIndex: %d\n", pointIndex);
        byte* line = history + pointIndex * TOTAL_LOG_LINE_SIZE;

        uint32_t currentTime = parseTime(line);
        if (currentTime > latestTime) {
            latestTime = currentTime;
            latestPosition = pointIndex * TOTAL_LOG_LINE_SIZE;
        }
        // Serial.println("points[0] = " + points[0] + ", points[1] = " + points[1]);
    }
    return (latestPosition + TOTAL_LOG_LINE_SIZE) % MAX_TEMP_BYTES_TO_READ;
}

int getValvePosition(bool openOrClose) {
    byte* valveBytes;
    if (openOrClose == VALVE_OPEN) {
        valveBytes = readFileToByteArray(valveOpenFileName, MAX_VALVE_BYTES_TO_READ);
    } else {
        valveBytes = readFileToByteArray(valveCloseFileName, MAX_VALVE_BYTES_TO_READ);
    }
    if (valveBytes == nullptr) {
        return 0;
    }
    uint32_t latestTime = 0;
    int latestPosition = 0;
    // Serial.printf("Position: %d\n", positionTemp);
    for (int pointIndex = 0; pointIndex < MAX_VALVES_ON_GRAPH; pointIndex++) {
        // Serial.printf("pointIndex: %d\n", pointIndex);
        byte* line = valveBytes + pointIndex * TIMESTAMP_SIZE;

        uint32_t currentTime = parseTime(line);
        if (currentTime > latestTime) {
            latestTime = currentTime;
            latestPosition = pointIndex * TIMESTAMP_SIZE;
        }
        // Serial.println("points[0] = " + points[0] + ", points[1] = " + points[1]);
    }
    free(valveBytes);
    return (latestPosition + TIMESTAMP_SIZE) % MAX_VALVE_BYTES_TO_READ;
}

void updateValvePositions() {
    positionOpen = getValvePosition(VALVE_OPEN);
    positionClose = getValvePosition(VALVE_CLOSE);
}

uint32_t getValveLast(bool openOrClose) {
    byte* valveBytes;
    int lastPos;
    uint32_t lastTimestamp;

    // Serial.print("Getting valve last ");
    if (openOrClose == VALVE_OPEN) {
        valveBytes = readFileToByteArray(valveOpenFileName, MAX_VALVE_BYTES_TO_READ);
        lastPos = positionOpen - TIMESTAMP_SIZE;
        // Serial.print("opened. ");
    } else {
        valveBytes = readFileToByteArray(valveCloseFileName, MAX_VALVE_BYTES_TO_READ);
        lastPos = positionClose - TIMESTAMP_SIZE;
        // Serial.print("closed. ");
    }
    if (valveBytes == nullptr) {
        // Serial.println("Failed. Could not read file.");
        return 0;
    }
    lastPos %= MAX_VALVE_BYTES_TO_READ;
    lastTimestamp = parseTime(valveBytes + lastPos);
    // Serial.printf("\nlastPos is %d, lastTimestamp is %d\n", lastPos, lastTimestamp);
    free(valveBytes);

    return lastTimestamp;
}

void renameFile(const char* path1, const char* path2) {
    Serial.printf("Renaming file %s to %s\n", path1, path2);
    if (LittleFS.rename(path1, path2)) {
        Serial.println("File renamed");
    } else {
        Serial.println("Rename failed");
    }
}

void writeFile(const char* path, const char* message) {
    Serial.printf("Writing file: %s, message: %s... ", path, message);

    File file = LittleFS.open(path, "w");
    if (!file) {
        Serial.println("Failed to open file for writing");
        return;
    }
    if (file.print(message)) {
        Serial.println("File written.");
    } else {
        Serial.println("Write failed.");
    }
    delay(1100); // Make sure the CREATE and LASTWRITE times are different
    file.close();
}

void appendFileAtPosition(const char* path, const byte* message, size_t size, int pos) {
    Serial.printf("Appending to file: %s, message: %s, position: %d... ", path, bytesToString(message, size).c_str(),
                  pos);

    File file = LittleFS.open(path, "r+");
    if (!file) {
        Serial.print("Failed to open file for appending. Trying to create the file... ");
        file = LittleFS.open(path, "w");
        if (!file) {
            Serial.println("Failed to create the file.");
            return;
        }
        Serial.println("File created.");
    }
    if (pos >= 0) {
        file.seek(pos);
    }
    if (file.write(message, size)) {
        Serial.println("Message appended.");
    } else {
        Serial.println("Append failed.");
    }
    file.close();
}

void readFile(const char* path) {
    Serial.printf("Reading file: %s\n", path);

    File file = LittleFS.open(path, "r");
    if (!file) {
        Serial.println("Failed to open file for reading");
        return;
    }

    Serial.print("Read from file: ");
    while (file.available()) {
        Serial.write(byteToString(file.read()).c_str());
    }
    file.close();
}

/**
 * Don't forget to free() the returned memory!!!
 */
byte* readFileToByteArray(const char* path, int length) {
    Serial.printf("Reading file: %s... ", path);
    byte* out = (byte*)calloc(length, sizeof(byte));
    if (out == nullptr) {
        Serial.println("Failed to allocate memory.");
        return nullptr;
    }

    File file = LittleFS.open(path, "r");
    if (!file) {
        Serial.println("Failed to open file for reading");
        return nullptr;
    }

    // Serial.print("Read from file: ");
    int i = 0;
    while (file.available() && i < length) {
        out[i++] = file.read();
    }
    file.close();
    Serial.printf("Read %d bytes.\n", i);
    return out;
}

void listDir(const char* dirname) {
    Serial.printf("Listing directory: %s\n", dirname);

    Dir root = LittleFS.openDir(dirname);

    while (root.next()) {
        File file = root.openFile("r");
        Serial.print("  FILE: ");
        Serial.print(root.fileName());
        Serial.print("  SIZE: ");
        Serial.print(file.size());
        time_t cr = file.getCreationTime();
        time_t lw = file.getLastWrite();
        file.close();
        struct tm* tmstruct = localtime(&cr);
        Serial.printf("    CREATION: %d-%02d-%02d %02d:%02d:%02d\n", (tmstruct->tm_year) + 1900, (tmstruct->tm_mon) + 1,
                      tmstruct->tm_mday, tmstruct->tm_hour, tmstruct->tm_min, tmstruct->tm_sec);
        tmstruct = localtime(&lw);
        Serial.printf("  LAST WRITE: %d-%02d-%02d %02d:%02d:%02d\n", (tmstruct->tm_year) + 1900, (tmstruct->tm_mon) + 1,
                      tmstruct->tm_mday, tmstruct->tm_hour, tmstruct->tm_min, tmstruct->tm_sec);
    }
}

float getTemp(const DeviceAddress sensorAddress) {
    float temp = -100;
    float totalTemp = 0;
    int goodTemps = 0;
    for (int i = 0; i < MAX_TEMP_MEASUREMENTS; i++) {
        delay(DELAY_BETWEEN_MEASUREMENTS);
        sensors.requestTemperatures();
        temp = sensors.getTempC(sensorAddress);
        if (temp >= MIN_TEMPERATURE) {
            totalTemp += temp;
            ++goodTemps;
        }
    }
    lastGoodTemps = goodTemps;
    if (goodTemps == 0) {
        return BAD_TEMP;
    }
    return (totalTemp / goodTemps);
}

float getTemp(int sensor) {
    return getTemp(sensorAdresses[sensor]);
}

void readTempCutoffFromFile() {
    byte* cutoffBytes = readFileToByteArray(cutoffFileName, 1);
    if (cutoffBytes == nullptr) {
        lowerTempCutoff = 40;
    } else {
        lowerTempCutoff = cutoffBytes[0];
        free(cutoffBytes);
    }
}

void setTempCutoff(byte temp) {
    lowerTempCutoff = temp;
    appendFileAtPosition(cutoffFileName, &temp, 1, 0);
}

void step(bool dir, int count) {
    digitalWrite(ENABLE_PIN, LOW);
    switch (dir) {
    case false:
        digitalWrite(DIR_PIN, LOW);
        break;
    case true:
        digitalWrite(DIR_PIN, HIGH);
        break;
    }
    for (int i = 0; i < count; i++) {
        digitalWrite(STEP_PIN, HIGH);
        delay(MOTOR_DELAY_BETWEEN_STEPS);
        digitalWrite(STEP_PIN, LOW);
        delay(MOTOR_DELAY_BETWEEN_STEPS);
    }
    digitalWrite(ENABLE_PIN, HIGH);
}

void openValve() {
    step(VALVE_OPEN_DIR, STEPS * MICROSTEPPING / 4 * VALVE_PULLEY_RIDGES / MOTOR_PULLEY_RIDGES + 5);
}

void closeValve() {
    step(!VALVE_OPEN_DIR, STEPS * MICROSTEPPING / 4 * VALVE_PULLEY_RIDGES / MOTOR_PULLEY_RIDGES + 5);
}

void controlValve() {
    if (millis() < TIME_BETWEEN_VALVE_MOVEMENTS + lastTimeValveMoved) {
        return;
    }

    for (int i = 0; i < NUMBER_OF_SENSORS; i++) {
        if (lastTemps[i] == BAD_TEMP) {
            return;
        }
    }

    time_t* now = (time_t*)calloc(sizeof(time_t), 1);
    *now = time(nullptr);
    byte* line = (byte*)malloc(TIMESTAMP_SIZE);
    memcpy(line, now, TIMESTAMP_SIZE);

    if (valveIsOpen) {
        if ((TEMP_TOP >= TEMP_GLOBAL + TEMP_MARGIN_BEFORE_CLOSING) ||
            (TEMP_INSIDE > TEMP_GLOBAL + TEMP_MARGIN_BEFORE_CLOSING) || (TEMP_GLOBAL < lowerTempCutoff) ||
            (TEMP_GLOBAL < MIN_OPEN_TEMP)) {
            // Our water is hotter than in the system, or
            // Our water is lower than the cutoff, or
            // Global water is too cold.
            // Should close.
            appendFileAtPosition(valveCloseFileName, line, TIMESTAMP_SIZE, positionClose);
            positionClose += TIMESTAMP_SIZE;
            positionClose %= MAX_VALVE_BYTES_TO_READ;
            closeValve();
            valveIsOpen = false;
            lastTimeValveMoved = millis();
            timeBetweenMeasurements = TIME_BETWEEN_MEASUREMENTS_CLOSED;
        }
    } else { // closed
        if ((TEMP_GLOBAL >= TEMP_TOP + TEMP_MARGIN_BEFORE_OPENING) && (TEMP_GLOBAL >= MIN_OPEN_TEMP)) {
            // Water in the system is hotter than ours.
            // Should open.
            valveIsOpen = true;
            appendFileAtPosition(valveOpenFileName, line, TIMESTAMP_SIZE, positionOpen);
            positionOpen += TIMESTAMP_SIZE;
            positionOpen %= MAX_VALVE_BYTES_TO_READ;
            openValve();
            setTempCutoff(TEMP_TOP);
            lastTimeValveMoved = millis();
            timeBetweenMeasurements = TIME_BETWEEN_MEASUREMENTS_OPEN;
        }
    }

    free(now);
    free(line);
}

void updateTemps() {
    if (millis() < timeBetweenMeasurements + lastTimeMeasured) {
        return;
    }

    for (int i = 0; i < NUMBER_OF_SENSORS; i++) {
        lastTemps[i] = getTemp(i);
        tempQuality[i] = (float)lastGoodTemps / MAX_TEMP_MEASUREMENTS * 100;
    }

    if (!debug) {
        for (int i = 0; i < NUMBER_OF_SENSORS; i++) {
            if (lastTemps[i] == BAD_TEMP) {
                Serial.println("Last measurements were bad, will not record.");
                temperatureError = true;
                lastTimeMeasured = millis() - timeBetweenMeasurements + TIME_BETWEEN_RETRIES; // wait before retrying
                lastTimeValveMoved = millis() - TIME_BETWEEN_VALVE_MOVEMENTS + VALVE_CONTROL_STARTUP_DELAY;
                return;
            }
        }
    }

    if (valveIsOpen && (TEMP_TOP - LOWER_TEMP_CUTOFF_HYSTERESIS > lowerTempCutoff)) {
        setTempCutoff(TEMP_TOP - LOWER_TEMP_CUTOFF_HYSTERESIS);
    }

    lastTimeMeasured = millis();
    tempsAreLogged = false;

    controlValve();
}

void toggleLED(bool on) {
    if (on) {
        digitalWrite(LED_PIN, LOW);
        ledStatus = LOW;
    } else {
        digitalWrite(LED_PIN, HIGH);
        ledStatus = HIGH;
    }
}

void toggleLED() {
    toggleLED(ledStatus == HIGH);
}

void printAddress(DeviceAddress deviceAddress) {
    for (uint8_t i = 0; i < 8; i++) {
        Serial.print("0x");
        if (deviceAddress[i] < 0x10)
            Serial.print("0");
        Serial.print(deviceAddress[i], HEX);
        if (i < 7)
            Serial.print(", ");
    }
    Serial.println("");
}

void formatLittleFS() {
    Serial.println("Formatting LittleFS!");
    LittleFS.format();
    positionTemp = 0;
}

void forceUpdate() {
    lastTimeMeasured = millis() - timeBetweenMeasurements - 1;
    updateTemps();
    logTemps();
}

/**
 * @brief Serves a page.
 *
 * @param request The request String.
 * @param client Client to answer to.
 * @return true if a page was served.
 * @return false if a page was not served (and thus we need to serve it after calling this function).
 */
bool handleRequest(String request, WiFiClient client) {
    if (request.indexOf("/led=off") != -1) {
        toggleLED(false);
        return false;
    }
    if (request.indexOf("/led=on") != -1) {
        toggleLED(true);
        return false;
    }
    if (request.indexOf("/led") != -1) {
        toggleLED();
        return false;
    }
    if (request.indexOf("/getaddress") != -1) {
        DeviceAddress Thermometer;

        sensors.begin();
        Serial.println("===");
        Serial.println("Locating devices...");
        Serial.print("Found ");
        int deviceCount = sensors.getDeviceCount();
        Serial.print(deviceCount, DEC);
        Serial.println(" devices.");
        Serial.println("");

        Serial.println("Printing addresses...");
        for (int i = 0; i < deviceCount; i++) {
            Serial.print("Sensor ");
            Serial.print(i + 1);
            Serial.print(" : ");
            sensors.getAddress(Thermometer, i);
            printAddress(Thermometer);
        }
        Serial.println("===");
        return false;
    }

    if (request.indexOf("/listfiles") != -1) {
        Serial.println("Listing files on SD card!");
        listDir("/");
        return false;
    }

    if (request.indexOf("/force") != -1) {
        Serial.println("Forcing Full Update!");
        forceUpdate();
        return false;
    }

    if (request.indexOf("/format") != -1) {
        Serial.println("Formatting LittleFS and forcing Full Update!");
        formatLittleFS();
        forceUpdate();
        return false;
    }

    if (request.indexOf("/debugon") != -1) {
        Serial.printf("Debug was %s, ", debug ? "on" : "off");
        debug = true;
        Serial.printf("now it is %s.\n", debug ? "on" : "off");
        forceUpdate();
        return false;
    }

    if (request.indexOf("/debugoff") != -1) {
        Serial.printf("Debug was %s, ", debug ? "on" : "off");
        debug = false;
        Serial.printf("now it is %s.\n", debug ? "on" : "off");
        forceUpdate();
        return false;
    }

    if (request.indexOf("/delete") != -1) {
        Serial.print("Deleting last point.\n");
        positionTemp -= TOTAL_LOG_LINE_SIZE;
        positionTemp += TOTAL_LOG_LINE_SIZE * MAX_POINTS_ON_GRAPH;
        positionTemp %= TOTAL_LOG_LINE_SIZE * MAX_POINTS_ON_GRAPH;
        byte* line = (byte*)calloc(sizeof(byte), TOTAL_LOG_LINE_SIZE);
        appendFileAtPosition(logFileName, line, TOTAL_LOG_LINE_SIZE, positionTemp);
        free(line);
        historyIsUpdated = false;
        return false;
    }

    if (request.indexOf("/openvalve") != -1) {
        Serial.print("Opening valve.\n");
        openValve();
        return false;
    }

    if (request.indexOf("/closevalve") != -1) {
        Serial.print("Closing valve.\n");
        closeValve();
        return false;
    }

    if (request.indexOf("/dst") != -1) {
        Serial.print("Changing DST.\n");
        byte* dstByte = (byte*)malloc(1);
        dstByte[0] = !dst;
        appendFileAtPosition(dstFileName, dstByte, 1, 0);
        free(dstByte);
        setDST();
        return false;
    }

    if (request.indexOf("/scripts.js") != -1) {
        Serial.print("Providing js file.\n");
        client.print("HTTP/1.1 200 OK\nContent-Type: text/javascript\n");
        client.print("\n");
        client.print(scripts_compressed_js);
        return true;
    }

    return false;
}

const String getTime(time_t timestamp) {

    char* charNow = (char*)malloc(50);
    strftime(charNow, 50, "%d.%m.%Y, %A, %H:%M", localtime(&timestamp));
    String strNow = String(charNow);
    free(charNow);

    return (strNow);
}

uint32_t parseTime(const byte* bytes) {
    uint32_t time = 0;

    // Assuming little endian
    for (size_t i = 0, mult = 1; i < TIMESTAMP_SIZE; i++, mult *= 256) {
        time += bytes[i] * mult;
    }
    // Serial.printf("Time parsed: %d\n", time);
    return time;
}

void historyByteToHex() {
    Serial.printf("Generating history hex bytes... ");
    if (history == nullptr) {
        Serial.printf("History is nullptr.\n");
        return;
    }
    historyHex = "";
    for (int i = 0; i < MAX_TEMP_BYTES_TO_READ; i++) {
        char msb = (char)((history[i] & 0b11110000) >> 4);
        char lsb = (char)(history[i] & 0b00001111);
        if (msb <= 9) {
            msb += '0';
        } else {
            msb += 'A' - 10;
        }
        if (lsb <= 9) {
            lsb += '0';
        } else {
            lsb += 'A' - 10;
        }
        historyHex += msb;
        historyHex += lsb;
    }
    Serial.printf("Finished generating history hex bytes!\n");
}

void updateHistory() {
    if (!historyIsUpdated) {
        Serial.print("History is old, updating...\n");
        free(history);
        history = readFileToByteArray(logFileName, MAX_TEMP_BYTES_TO_READ);
        historyIsUpdated = true;
        historyByteToHex();
    } else {
        Serial.print("History is okay, not updating.\n");
    }
}

void prepValveMovements(const char* fileName, int pos, String& valveMovements) {
    Serial.printf("Starting to create valve annotations... ");
    valveMovements = "";
    byte* valveBytes = readFileToByteArray(fileName, MAX_VALVE_BYTES_TO_READ);
    if (valveBytes == nullptr) {
        return;
    }

    int pointCount = 0;
    int lastPosition = (pos / TIMESTAMP_SIZE - 1 + MAX_VALVE_BYTES_TO_READ) % MAX_VALVES_ON_GRAPH;

    uint32_t earliestMeasurement = UINT32_MAX;
    for (int i = 0; i < MAX_POINTS_ON_GRAPH; i++) {
        uint32_t measurement = parseTime(history + ((positionTemp + i * TOTAL_LOG_LINE_SIZE) % MAX_TEMP_BYTES_TO_READ));
        if (measurement == 0) {
            continue;
        }
        earliestMeasurement = measurement < earliestMeasurement ? measurement : earliestMeasurement;
    }

    for (int pointIndex = pos / TIMESTAMP_SIZE;; pointIndex++) {
        // Serial.printf("pointIndex: %d\n", pointIndex);
        pointIndex %= MAX_VALVES_ON_GRAPH;
        byte* line = valveBytes + pointIndex * TIMESTAMP_SIZE;

        if (parseTime(line) <= earliestMeasurement) {
            if (pointIndex == lastPosition) {
                break;
            }
            continue;
        }
        ++pointCount;

        String point = String(parseTime(line));
        // Serial.printf("Point: %s\n", point.c_str());

        point.concat("000,");
        valveMovements.concat(point);

        if (pointIndex == lastPosition) {
            break;
        }
        // Serial.println("points[0] = " + points[0] + ", points[1] = " + points[1]);
    }
    Serial.printf("Parsed %d valve movements.\n", pointCount);
    valveMovements.remove(valveMovements.length() - 1, 1);
    free(valveBytes);

    // Serial.printf("Points: %s\n", points.c_str());
}

void sendHTML(WiFiClient client) {
    Serial.println("Sending HTML!");
    // Headers
    client.print("HTTP/1.1 200 OK\nContent-Type: text/html\n");
    client.print("\n");

    client.print("<html><script>");

    updateHistory();
    client.print("const _HISTORY_ = \"");
    client.print(historyHex);
    client.print("\";\n");

    prepValveMovements(valveOpenFileName, positionOpen, valveOpenings);
    client.print("const _VALVEOPENINGS_=[");
    client.print(valveOpenings);
    client.print("];\n");

    prepValveMovements(valveCloseFileName, positionClose, valveClosings);
    client.print("const _VALVECLOSINGS_=[");
    client.print(valveClosings);
    client.print("];\n");

    client.print("const TIMESTAMP_SIZE=");
    client.print(TIMESTAMP_SIZE);
    client.print(";\n");
    client.print("const SENSOR_COUNT=");
    client.print(NUMBER_OF_SENSORS);
    client.print(";\n");

    for (int i = 0; i < NUMBER_OF_SENSORS; i++) {
        String tempString = "_TEMP";
        tempString.reserve(9);
        tempString.concat(i + 1);
        tempString.concat("_");
        client.print("const ");
        client.print(tempString);
        client.print("=");

        client.print(lastTemps[i] != BAD_TEMP ? String(lastTemps[i]) : "\"BAD\"");
        client.print(";\n");

        tempString = "_TEMP";
        tempString.concat(i + 1);
        tempString.concat("Q_");
        client.print("const ");
        client.print(tempString);
        client.print("=");
        client.print(String(tempQuality[i]));
        client.print(";\n");
    }

    uint32_t valveLastOpened, valveLastClosed;
    valveLastOpened = getValveLast(VALVE_OPEN);
    valveLastClosed = getValveLast(VALVE_CLOSE);
    client.print("const _VALVEOPENED_=\"");
    client.print(getTime(valveLastOpened));
    client.print("\";\n");
    client.print("const _VALVECLOSED_=\"");
    client.print(getTime(valveLastClosed));
    client.print("\";\n");
    client.print("const _VALVESTATUS_=");
    client.print(valveIsOpen ? "\"Open\"" : "\"Closed\"");
    client.print(";\n");

    client.print("const _IP_=\"");
    client.print(WiFi.localIP().toString());
    client.print("\";\n");
    client.print("const _DST_=\"");
    client.print(dst == 1 ? "On" : "Off");
    client.print("\";\n");
    client.print("const _TIME_=\"");
    client.print(lastMeasurementTimestamp);
    client.print("\";\n");

    client.print("const _INFO_=");
    client.print("\"Some info will be here\"");
    client.print(";\n");

    client.print("</script>");

    // refresh to the homepage
    client.print(R"===(<meta http-equiv="refresh" content=")===");
    client.print((TIME_BETWEEN_HTML_REFRESHES - (millis() - lastTimeMeasured)) / 1000);
    client.print(R"===(; URL=http://)===");
    client.print(WiFi.localIP().toString());
    client.print(R"===(/" />)===");

    client.print(compressed_html);
    client.print("</html>");

    client.flush();
}

void logTemps() {
    if (tempsAreLogged) {
        return;
    }
    tempsAreLogged = true;

    Serial.println("Logging Temps!");

    bool tempsWereUpdated = false;
    for (int i = 0; i < NUMBER_OF_SENSORS; i++) {
        if (lastLastTemps[i] != lastTemps[i]) {
            tempsWereUpdated = true;
            break;
        }
    }

    if (!tempsWereUpdated) {
        Serial.println("Temps were not updated, will not log.");
        droppedTemps = true;
        *lastTempsTime = time(nullptr);
        return;
    }

    temperatureError = false;
    setDST();
    time_t* now = (time_t*)calloc(sizeof(time_t), 1);
    byte* line = (byte*)malloc(TOTAL_LOG_LINE_SIZE);

    // update last temperature
    if (droppedTemps) {
        // we dropped the last measurement, so record it now
        Serial.printf("Last temperature time: %lld\n", *lastTempsTime);
        memcpy(line, lastTempsTime, TIMESTAMP_SIZE);
        for (int i = 0; i < NUMBER_OF_SENSORS; i++) {
            line[TIMESTAMP_SIZE + TEMP_SIZE * i] = lastLastTemps[i];
        }
        appendFileAtPosition(logFileName, line, TOTAL_LOG_LINE_SIZE, positionTemp);
        positionTemp += TOTAL_LOG_LINE_SIZE;
        positionTemp %= MAX_TEMP_BYTES_TO_READ;
    }

    // update current temperature
    *now = time(nullptr);
    Serial.printf("Current temperature time: %lld\n", *now);
    memcpy(line, now, TIMESTAMP_SIZE);
    for (int i = 0; i < NUMBER_OF_SENSORS; i++) {
        line[TIMESTAMP_SIZE + TEMP_SIZE * i] = lastTemps[i];
    }
    appendFileAtPosition(logFileName, line, TOTAL_LOG_LINE_SIZE, positionTemp);
    positionTemp += TOTAL_LOG_LINE_SIZE;
    positionTemp %= MAX_TEMP_BYTES_TO_READ;

    free(line);
    free(now);

    for (int i = 0; i < NUMBER_OF_SENSORS; i++) {
        lastLastTemps[i] = lastTemps[i];
    }

    droppedTemps = false;
    lastMeasurementTimestamp = getTime(time(nullptr));
    historyIsUpdated = false;
}

void setup() {
    Serial.begin(SERIAL_BAUDRATE);
    delay(100);

    Serial.println("\n~~~ Setup Begin! ~~~\n\n");

    pinMode(LED_PIN, OUTPUT);
    pinMode(ENABLE_PIN, OUTPUT);
    pinMode(STEP_PIN, OUTPUT);
    pinMode(DIR_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    digitalWrite(ENABLE_PIN, HIGH);
    Serial.println();
    Serial.println();
    Serial.print("Connecting to ");
    Serial.print(SSID);
    WiFi.begin(SSID, WIFI_PASSWORD);
    while (WiFi.waitForConnectResult() != WL_CONNECTED) {
        Serial.println("Failed to connect!");
        Serial.print("Connecting to ");
        Serial.print(SSID2);
        WiFi.begin(SSID2, WIFI_PASSWORD2);
        while (WiFi.waitForConnectResult() != WL_CONNECTED) {
            Serial.println("Failed to connect!");
            delay(500);
            ESP.restart();
        }
    }
    WiFi.config(IPAddress(IP), IPAddress(GATEWAY), IPAddress(SUBNET));

    Serial.println("\nWiFi connected.");

    server.begin();
    Serial.println("Server started.");

    pinMode(SS, OUTPUT);

    Serial.println("Starting temperature sensors!");
    sensors.begin();
    Serial.println("Configuring temperature sensors!");
    sensors.setWaitForConversion(true);
    sensors.setCheckForConversion(false);
    sensors.setResolution(9);
    Serial.printf("getWaitForConversion() = %d\n", sensors.getWaitForConversion());
    Serial.printf("getCheckForConversion() = %d\n", sensors.getCheckForConversion());
    Serial.printf("getResolution() = %d\n", sensors.getResolution());

    if (!LittleFS.begin()) {
        Serial.println("LittleFS mount failed. Restarting...");
        ESP.restart();
    }
    Serial.println("LittleFS Mounted!");

    Serial.print("\nStarting time configuration!\n");
    configureTime();
    delay(500);
    Serial.print("\nWaiting for time...");
    while (time(nullptr) < 1000) {
        Serial.print(".");
        delay(500);
    }
    delay(500);
    Serial.print("\nGot Time. Current time is: ");
    Serial.println(getTime(time(nullptr)));

    delay(200);

    historyHex = "";
    if (historyHex.reserve(2 * MAX_TEMP_BYTES_TO_READ)) {
        Serial.printf("Successfully reserved memory for history hex.");
    } else {
        Serial.printf("Failed to reserve memory for history hex. Restarting...");
        ESP.restart();
    }

    positionTemp = getTempPosition();
    Serial.printf("positionTemp is: %d\n", positionTemp);

    updateValvePositions();
    Serial.printf("positionOpen is: %d\n", positionOpen);
    Serial.printf("positionClose is: %d\n", positionClose);

    uint32_t valveLastOpened, valveLastClosed;
    valveLastOpened = getValveLast(VALVE_OPEN);
    valveLastClosed = getValveLast(VALVE_CLOSE);
    valveIsOpen = valveLastOpened >= valveLastClosed;
    timeBetweenMeasurements = valveIsOpen ? TIME_BETWEEN_MEASUREMENTS_OPEN : TIME_BETWEEN_MEASUREMENTS_CLOSED;

    Serial.printf("Valve last opened on: %s\n", getTime(valveLastOpened).c_str());
    Serial.printf("Valve last closed on: %s\n", getTime(valveLastClosed).c_str());
    Serial.printf("Valve is currently %s.\n", valveIsOpen ? "Open" : "Closed");

    readTempCutoffFromFile();
    Serial.printf("Lower temperature cutoff set to: %dC\n", lowerTempCutoff);

    lastTempsTime = (time_t*)calloc(sizeof(time_t), 1);

    Serial.println("\n~~~ Setup Finished! ~~~\n\n");
}

void loop() {
    updateTemps();
    logTemps();

    WiFiClient client = server.available();
    if (!client) {
        return;
    }
    Serial.println("~~ New client");
    unsigned long available = millis();
    while (!client.available()) {
        delay(10);
        if (millis() > 1500 + available) {
            Serial.println("~~ failed to connect.\n");
            return;
        }
    }
    String request = client.readStringUntil('\r');
    Serial.println(request);

    if (!handleRequest(request, client)) {
        sendHTML(client);
    }

    delay(100);
    Serial.println("~~ Client disonnected");
    Serial.println("");
}
