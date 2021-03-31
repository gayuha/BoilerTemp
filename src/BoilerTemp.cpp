#include <Arduino.h>

#include <DallasTemperature.h>
#include <Stepper.h>

#include <ESP8266WiFi.h>
#include <FS.h>
#include <LittleFS.h>
#include <time.h>

#include <sstream>

#include "Configuration.h"
#include "dst.h"
#include "index.h"

int ledStatus = LOW;

const char* const logFileName = LOGFILE;
const char* const dstFileName = DST_FILE;
const char* const valveOpenFileName = VALVE_OPEN_FILE;
const char* const valveCloseFileName = VALVE_CLOSE_FILE;

unsigned long lastTimeMeasured = millis() - TIME_BETWEEN_MEASUREMENTS;
unsigned long lastTimeValveMoved = millis() - TIME_BETWEEN_VALVE_MOVEMENTS + 10 * 60 * 1000;

// Valve status. Open means water can flow.
bool valveIsOpen = true;

bool tempsAreLogged = true;
bool historyIsUpdated = false;
String lastMeasurementTimestamp;

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

int dst = 0;

WiFiServer server(80);

String lastHTMLBody = "Yo!";

byte lastTemps[NUMBER_OF_SENSORS];

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
void cleanHistory(String filename);
float getTemp(const DeviceAddress sensorAddress);
void toggleLED(bool on);
void toggleLED();
void handleRequest(String request);
const String getTime(time_t);
void prepPoints(int sensor0, int sensor1);
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
    if (newDST < 0) { // dst is ambiguous
        return;
    }
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

void cleanHistory(String filename) {
    int i = 0;
    String newName;
    for (i = 0; LittleFS.exists(newName = "backup_" + String(i) + ".log"); ++i)
        ;
    renameFile(filename.c_str(), newName.c_str());
    positionTemp = 0;
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
    if (goodTemps == 0) {
        return BAD_TEMP;
    }
    return (totalTemp / goodTemps);
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
    if (millis() - lastTimeValveMoved < TIME_BETWEEN_VALVE_MOVEMENTS) {
        return;
    }

    if (lastTemps[0] == BAD_TEMP || lastTemps[1] == BAD_TEMP || lastTemps[2] == BAD_TEMP) {
        return;
    }

    time_t* now = (time_t*)calloc(sizeof(time_t), 1);
    *now = time(nullptr);
    byte* line = (byte*)malloc(TIMESTAMP_SIZE);
    memcpy(line, now, TIMESTAMP_SIZE);

    if (valveIsOpen) {
        if ((lastTemps[1] - lastTemps[0] >= 2) || ((abs(lastTemps[1] - lastTemps[0]) <= 2) && lastTemps[0] < 42) ||
            (lastTemps[1] <= lastTemps[2] - 2)) {
            // Our water is hotter than in the system, or
            // the system is too cold, or
            // water going out of the boiler is hotter than what enters.
            // Should close.
            appendFileAtPosition(valveCloseFileName, line, TIMESTAMP_SIZE, positionClose);
            positionClose += TIMESTAMP_SIZE;
            positionClose %= MAX_VALVE_BYTES_TO_READ;
            closeValve();
            valveIsOpen = false;
            lastTimeValveMoved = millis();
        }
    } else { // closed
        if (lastTemps[0] >= lastTemps[1] + 2) {
            // Water in the system is hotter than ours. Should open.
            valveIsOpen = true;
            appendFileAtPosition(valveOpenFileName, line, TIMESTAMP_SIZE, positionOpen);
            positionOpen += TIMESTAMP_SIZE;
            positionOpen %= MAX_VALVE_BYTES_TO_READ;
            openValve();
            lastTimeValveMoved = millis();
        }
    }

    free(now);
    free(line);
}

void updateTemps() {
    if (millis() - lastTimeMeasured < TIME_BETWEEN_MEASUREMENTS) {
        return;
    }

    for (int i = 0; i < NUMBER_OF_SENSORS; i++) {
        lastTemps[i] = getTemp(sensorAdresses[i]);
    }

    if (!debug) {
        for (int i = 0; i < NUMBER_OF_SENSORS; i++) {
            if (lastTemps[i] == BAD_TEMP) {
                Serial.println("Last measurements were bad, will not record.");
                temperatureError = true;
                lastTimeMeasured = millis() - TIME_BETWEEN_MEASUREMENTS + 60 * 1000; // wait a minute before retrying
                return;
            }
        }
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
    if (ledStatus == HIGH) {
        digitalWrite(LED_PIN, LOW);
        ledStatus = LOW;
    } else {
        digitalWrite(LED_PIN, HIGH);
        ledStatus = HIGH;
    }
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
    lastTimeMeasured = millis() - TIME_BETWEEN_MEASUREMENTS - 1;
    updateTemps();
    logTemps();
}

void handleRequest(String request) {
    if (request.indexOf("/led=off") != -1) {
        toggleLED(false);
        return;
    }
    if (request.indexOf("/led=on") != -1) {
        toggleLED(true);
        return;
    }
    if (request.indexOf("/led") != -1) {
        toggleLED();
        return;
    }
    if (request.indexOf("/cleanhistory") != -1) {
        cleanHistory(logFileName);
        return;
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
        return;
    }

    if (request.indexOf("/listfiles") != -1) {
        Serial.println("Listing files on SD card!");
        listDir("/");
        return;
    }

    if (request.indexOf("/force") != -1) {
        Serial.println("Forcing Full Update!");
        forceUpdate();
        return;
    }

    if (request.indexOf("/format") != -1) {
        formatLittleFS();
        forceUpdate();
        return;
    }

    if (request.indexOf("/debugon") != -1) {
        Serial.printf("Debug was %s, ", debug ? "on" : "off");
        debug = true;
        Serial.printf("now it is %s.\n", debug ? "on" : "off");
        forceUpdate();
        return;
    }

    if (request.indexOf("/debugoff") != -1) {
        Serial.printf("Debug was %s, ", debug ? "on" : "off");
        debug = false;
        Serial.printf("now it is %s.\n", debug ? "on" : "off");
        forceUpdate();
        return;
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
        return;
    }

    if (request.indexOf("/openvalve") != -1) {
        Serial.print("Opening valve.\n");
        openValve();
        return;
    }

    if (request.indexOf("/closevalve") != -1) {
        Serial.print("Closing valve.\n");
        closeValve();
        return;
    }

    if (request.indexOf("/dst") != -1) {
        Serial.print("Changing DST.\n");
        byte* dstByte = (byte*)malloc(1);
        dstByte[0] = !dst;
        appendFileAtPosition(dstFileName, dstByte, 1, 0);
        free(dstByte);
        setDST();
        return;
    }
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

String dataToPoint(uint32_t timestamp, int temperature) {
    String point = "{";
    point += "x:";
    point.concat(timestamp);
    point += "000,";
    point += "y:";
    point.concat(temperature);
    point += "}";
    return point;
}

String parseTemps(const byte* bytes, int sensor) {
    // Serial.println("Parsing line");
    uint32_t time = parseTime(bytes);

    byte temperature = bytes[TIMESTAMP_SIZE + TEMP_SIZE * sensor];

    return dataToPoint(time, temperature);
}

String parseDiffTemps(const byte* bytes, int sensor0, int sensor1) {
    // Serial.println("Parsing line");
    uint32_t time = parseTime(bytes);

    byte temperature0 = bytes[TIMESTAMP_SIZE + TEMP_SIZE * sensor0];
    byte temperature1 = bytes[TIMESTAMP_SIZE + TEMP_SIZE * sensor1];

    return dataToPoint(time, (int)temperature0 - (int)temperature1);
}

void updateHistory() {
    if (!historyIsUpdated) {
        Serial.print("History is old, updating...\n");
        free(history);
        history = readFileToByteArray(logFileName, MAX_TEMP_BYTES_TO_READ);
        historyIsUpdated = true;

        if (history == nullptr) {
            historyHex = "\"\"";
            return;
        }

        historyHex = "\"";

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
        historyHex += "\"";
    } else {
        Serial.print("History is okay, not updating.\n");
    }
}

// sensor0-sensor1
void prepPoints(int sensor0, int sensor1) {
    if (sensor1 >= 0) {
        Serial.printf("Starting to create diff points for sensors %d and %d... ", sensor0, sensor1);
    } else {
        Serial.printf("Starting to create points for sensor %d... ", sensor0);
    }
    points = "";
    updateHistory();
    if (history == nullptr) {
        Serial.println("History byte array is nullptr.");
        return;
    }

    int pointCount = 0;
    // Serial.printf("Position: %d\n", positionTemp);
    int lastPosition = (positionTemp / TOTAL_LOG_LINE_SIZE - 1 + MAX_TEMP_BYTES_TO_READ) % MAX_POINTS_ON_GRAPH;
    // Serial.printf("lastPosition: %d\n", lastPosition);
    for (int pointIndex = positionTemp / TOTAL_LOG_LINE_SIZE;; pointIndex++) {
        // Serial.printf("pointIndex: %d\n", pointIndex);
        pointIndex %= MAX_POINTS_ON_GRAPH;
        byte* line = history + pointIndex * TOTAL_LOG_LINE_SIZE;
        bool allZero = true;
        for (int i = 0; i < TOTAL_LOG_LINE_SIZE; i++) {
            if (line[i] != 0) {
                allZero = false;
                break;
            }
        }

        if (allZero) {
            if (pointIndex == lastPosition) {
                break;
            }
            continue;
        }
        ++pointCount;

        String point;
        if (sensor1 < 0) {
            point = parseTemps(line, sensor0);
        } else {
            point = parseDiffTemps(line, sensor0, sensor1);
        }
        // Serial.printf("Point: %s\n", point.c_str());
        if ((line[TIMESTAMP_SIZE + TEMP_SIZE * sensor0] != BAD_TEMP &&
             (sensor1 < 0 || line[TIMESTAMP_SIZE + TEMP_SIZE * sensor1] != BAD_TEMP)) ||
            debug) {
            point.concat(",");
            points += point;
        }

        if (pointIndex == lastPosition) {
            lastMeasurementTimestamp = getTime(parseTime(line));
            break;
        }
        // Serial.println("points[0] = " + points[0] + ", points[1] = " + points[1]);
    }
    Serial.printf("Parsed %d points.\n", pointCount);
    points.remove(points.length() - 1, 1);

    // Serial.printf("Points: %s\n", points.c_str());
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
    uint32_t earliestMeasurement = parseTime(history + positionTemp);
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
    client.print("HTTP/1.1 200 OK\nContent-Type: text/html\n\n");

    client.print("<html><script>");

    prepPoints(0, -1);
    client.print("var _INGDATA_ = [");
    client.print(points);
    client.print("];\n");

    prepPoints(2, -1);
    client.print("var _OUTLDATA_ = [");
    client.print(points);
    client.print("];\n");

    prepPoints(1, -1);
    client.print("var _INLDATA_ = [");
    client.print(points);
    client.print("];\n");

    prepPoints(1, 2);
    client.print("var _DIFFDATA_ = [");
    client.print(points);
    client.print("];\n");

    updateHistory();
    client.print("var _HISTORY_ = ");
    client.print(historyHex);
    client.print(";\n");

    prepValveMovements(valveOpenFileName, positionOpen, valveOpenings);
    client.print("var _VALVEOPENINGS_ = [");
    client.print(valveOpenings);
    client.print("];\n");

    prepValveMovements(valveCloseFileName, positionClose, valveClosings);
    client.print("var _VALVECLOSINGS_ = [");
    client.print(valveClosings);
    client.print("];\n");

    client.print("</script>");

    // refresh to the homepage
    client.print(R"===(<meta http-equiv="refresh" content=")===");
    client.print((TIME_BETWEEN_MEASUREMENTS - (millis() - lastTimeMeasured)) / 1000 + 30);
    client.print(R"===(; URL=http://)===");
    client.print(WiFi.localIP().toString());
    client.print(R"===(/" />)===");

    lastHTMLBody = String(compressed_html);
    lastHTMLBody.replace("_IP_", WiFi.localIP().toString());
    lastHTMLBody.replace("_LED-STATUS_", ledStatus == HIGH ? "Off" : "On");
    lastHTMLBody.replace("_TIME_", lastMeasurementTimestamp);
    lastHTMLBody.replace("_DST_", dst == 1 ? "On" : "Off");

    lastHTMLBody.replace("_VALVESTATUS_", valveIsOpen ? "Open" : "Closed");
    uint32_t valveLastOpened, valveLastClosed;
    valveLastOpened = getValveLast(VALVE_OPEN);
    valveLastClosed = getValveLast(VALVE_CLOSE);
    lastHTMLBody.replace("_VALVEOPENED_", getTime(valveLastOpened));
    lastHTMLBody.replace("_VALVECLOSED_", getTime(valveLastClosed));

    lastHTMLBody.replace("_TEMP1_", lastTemps[0] != BAD_TEMP ? String(lastTemps[0]) : "BAD");
    lastHTMLBody.replace("_TEMP2_", lastTemps[1] != BAD_TEMP ? String(lastTemps[1]) : "BAD");
    lastHTMLBody.replace("_TEMP3_", lastTemps[2] != BAD_TEMP ? String(lastTemps[2]) : "BAD");
    lastHTMLBody.replace("_TEMP-DIFF_", lastTemps[1] != BAD_TEMP && lastTemps[2] != BAD_TEMP
                                            ? String(lastTemps[1] - lastTemps[2])
                                            : "BAD");

    client.print(lastHTMLBody);
    client.print("</html>");

    client.flush();
}

void logTemps() {
    if (tempsAreLogged) {
        return;
    }

    Serial.println("Logging Temps!");
    temperatureError = false;
    setDST();
    time_t* now = (time_t*)calloc(sizeof(time_t), 1);
    *now = time(nullptr);
    Serial.printf("Time: %ld\n", *now);
    byte* line = (byte*)malloc(TOTAL_LOG_LINE_SIZE);
    memcpy(line, now, TIMESTAMP_SIZE);
    for (int i = 0; i < NUMBER_OF_SENSORS; i++) {
        line[TIMESTAMP_SIZE + TEMP_SIZE * i] = lastTemps[i];
    }
    appendFileAtPosition(logFileName, line, TOTAL_LOG_LINE_SIZE, positionTemp);
    positionTemp += TOTAL_LOG_LINE_SIZE;
    positionTemp %= MAX_TEMP_BYTES_TO_READ;
    free(line);
    free(now);

    lastMeasurementTimestamp = getTime(time(nullptr));
    historyIsUpdated = false;
    tempsAreLogged = true;
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

    Serial.print("\nStarting time configuration!");
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

    positionTemp = getTempPosition();
    Serial.printf("positionTemp is: %d\n", positionTemp);

    updateValvePositions();
    Serial.printf("positionOpen is: %d\n", positionOpen);
    Serial.printf("positionClose is: %d\n", positionClose);

    uint32_t valveLastOpened, valveLastClosed;
    valveLastOpened = getValveLast(VALVE_OPEN);
    valveLastClosed = getValveLast(VALVE_CLOSE);
    valveIsOpen = valveLastOpened >= valveLastClosed;

    Serial.printf("Valve last opened on: %s\n", getTime(valveLastOpened).c_str());
    Serial.printf("Valve last closed on: %s\n", getTime(valveLastClosed).c_str());
    Serial.printf("Valve is currently %s.\n", valveIsOpen ? "Open" : "Closed");

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
        if (millis() - available > 1500) {
            Serial.println("~~ failed to connect.\n");
            return;
        }
    }
    String request = client.readStringUntil('\r');
    Serial.println(request);

    handleRequest(request);
    sendHTML(client);

    delay(100);
    Serial.println("~~ Client disonnected");
    Serial.println("");
}
