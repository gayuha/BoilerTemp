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

int ledStatus = LOW;

const char* const logFileName = LOGFILE;
const char* const positionFile = POSITION_FILE;
time_t lastLoggedTime = millis();

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

int dst = 0;

WiFiServer server(80);

String lastHTML = "Yo!";
time_t lastTimeHTML = millis() - TIME_BETWEEN_HTML_UPDATES;

byte lastTemps[2];
time_t lastTimeTemps = millis() - TIME_BETWEEN_MEASUREMENTS;

String hotData = "";
String coldData = "";

int position;

bool temperatureError = false;

// Declarations
void configureTime();
void logSD(String filename, String line);
void rename(String from, String to);
void cleanHistory(String filename);
float getTemp(const DeviceAddress sensorAddress);
void toggleLED(bool on);
void toggleLED();
void handleRequest(String request);
const String getTime();
void initData();
void logTemps();
void updateHTML();

// Functions
void setDST() {
    time_t now;
    while ((now = time(nullptr)) < 1000) {
        delay(200);
    }
    bool newDST = isDST(now);
    bool isDiff = (dst != newDST);
    if (newDST < 0) { // dst is ambiguous
        return;
    }
    dst = newDST;
    if (isDiff) {
        configureTime();
    }
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

String bytesToString(const byte* bytes, int count = MAX_BYTES_TO_READ) {
    String out = "";
    for (int i = 0; i < count; i++) {
        // Serial.print(String(i) + ", ");
        if (i % TOTAL_LOG_LINE_SIZE == 0) {
            out += "\n";
        }
        if (i % TOTAL_LOG_LINE_SIZE == (TOTAL_LOG_LINE_SIZE - TEMP_SIZE * 2) ||
            i % TOTAL_LOG_LINE_SIZE == (TOTAL_LOG_LINE_SIZE - TEMP_SIZE)) {
            out += " ";
        }
        out += byteToString(bytes[i]);
    }
    return out;
}

int getPosition() {
    File file = LittleFS.open(positionFile, "r");
    if (!file) {
        Serial.println("Failed to open position file for reading. Creating a new one.");
        file = LittleFS.open(positionFile, "w");
        file.print("0");
        file.close();
        return 0;
    }

    int pos = 0;
    while (file.available()) {
        int c = file.read();
        if ('0' <= c && c <= '9') {
            pos = pos * 10 + (c - '0');
        }
    }
    file.close();
    return pos;
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
    Serial.printf("Writing file: %s\n", path);

    File file = LittleFS.open(path, "w");
    if (!file) {
        Serial.println("Failed to open file for writing");
        return;
    }
    if (file.print(message)) {
        Serial.println("File written");
    } else {
        Serial.println("Write failed");
    }
    delay(1100); // Make sure the CREATE and LASTWRITE times are different
    file.close();
}

void appendFile(const char* path, const byte* message, size_t size) {
    Serial.printf("Appending to file: %s, message: %s\n", path, bytesToString(message, size).c_str());

    File file = LittleFS.open(path, "a");
    if (!file) {
        Serial.println("Failed to open file for appending.");
        return;
    }
    if (file.write(message, size)) {
        Serial.println("Message appended.");
    } else {
        Serial.println("Append failed.");
    }
    file.close();
}

void appendFileAtPosition(const char* path, const byte* message, size_t size, int pos) {
    Serial.printf("Appending to file: %s, message: %s, position: %d\n", path, bytesToString(message, size).c_str(),
                  pos);

    File file = LittleFS.open(path, "r+");
    if (!file) {
        Serial.println("Failed to open file for appending. Trying to create the file.");
        file = LittleFS.open(path, "w");
        if (!file) {
            Serial.println("Failed to create the file.");
            return;
        }
        Serial.println("File created.");
    }
    file.seek(pos);
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

byte* readFileToByteArray(const char* path, int length) {
    Serial.printf("Reading file: %s\n", path);
    byte* out = (byte*)calloc(length, sizeof(byte));

    File file = LittleFS.open(path, "r");
    if (!file) {
        Serial.println("Failed to open file for reading");
        return nullptr;
    }

    // Serial.print("Read from file: ");
    int i = 0;
    while (file.available() && i < length) {
        out[i] = file.read();
        i++;
    }
    file.close();
    return out;
}

void savePosition(int pos) {
    writeFile(positionFile, String(pos).c_str());
}

void cleanHistory(String filename) {
    int i = 0;
    String newName;
    for (i = 0; LittleFS.exists(newName = "backup_" + String(i) + ".log"); ++i)
        ;
    renameFile(filename.c_str(), newName.c_str());
    position = 0;
    savePosition(position);
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

void updateTemps() {
    if (millis() - lastTimeTemps < TIME_BETWEEN_MEASUREMENTS) {
        return;
    }
    lastTemps[0] = getTemp(sensor1Address);
    lastTemps[1] = getTemp(sensor2Address);

    lastTimeTemps = millis();
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
    position = 0;
    savePosition(position);
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
    if (request.indexOf("/initdata") != -1) {
        initData();
        return;
    }
    if (request.indexOf("/listfiles") != -1) {
        Serial.println("Listing files on SD card!");
        listDir("/");
        return;
    }

    if (request.indexOf("/force") != -1) {
        Serial.println("Forcing Full Update!");
        lastLoggedTime = millis() - TIME_BETWEEN_MEASUREMENTS - 1;
        lastTimeHTML = millis() - TIME_BETWEEN_HTML_UPDATES - 1;
        lastTimeTemps = millis() - TIME_BETWEEN_MEASUREMENTS - 1;
        updateTemps();
        logTemps();
        updateHTML();
        return;
    }

    if (request.indexOf("/format") != -1) {
        formatLittleFS();
        return;
    }
}

const String getTime() {
    const time_t now = time(nullptr);

    char* charNow = (char*)malloc(50);
    strftime(charNow, 50, "%d.%m.%Y, %A, %H:%M", localtime(&now));
    String strNow = String(charNow);
    free(charNow);

    return (strNow);
}

String* parseTemps(const byte* bytes) {
    // Serial.println("Parsing line");
    String timeStr;
    uint32_t time = 0;

    // Assuming little endian
    for (size_t i = 0, mult = 1; i < TIMESTAMP_SIZE; i++, mult *= 256) {
        time += bytes[i] * mult;
        // Serial.printf("Mult: %d\n", mult);
    }
    // Serial.printf("Time parsed: %d\n", time);
    timeStr = String(time);

    String tempHot = String(bytes[TIMESTAMP_SIZE]);
    String tempCold = String(bytes[TIMESTAMP_SIZE + TEMP_SIZE]);

    String pointHot = "{";
    pointHot += "x:" + timeStr + "000,";
    pointHot += "y:" + tempHot;
    pointHot += "}";

    String pointCold = "{";
    pointCold += "x:" + timeStr + "000,";
    pointCold += "y:" + tempCold;
    pointCold += "}";

    String* points = new String[2];
    points[0] = pointHot;
    points[1] = pointCold;
    return points;
}

void initData() {
    Serial.println("Initializing data!");
    byte* history = readFileToByteArray(logFileName, MAX_BYTES_TO_READ);
    if (history == nullptr) {
        Serial.println("History byte array is nullptr.");
        return;
    }
    // hotData = "Hey! " + history;
    // coldData = "data inited!";
    hotData = coldData = "";
    Serial.println("Starting to parse points!");
    for (int i = 0; i < MAX_POINTS_ON_GRAPH; i++) {
        byte* line = history + i * TOTAL_LOG_LINE_SIZE;
        if (*line == 0) {
            break;
        }
        String* points = parseTemps(line);
        hotData += points[0] + ",";
        coldData += points[1] + ",";
        delete[] points;
        // Serial.println("points[0] = " + points[0] + ", points[1] = " + points[1]);
    }
    hotData.remove(hotData.length() - 1, 1);
    coldData.remove(coldData.length() - 1, 1);
    free(history);
}

void updateHTML() {
    if (millis() - lastTimeHTML < TIME_BETWEEN_HTML_UPDATES) {
        return;
    }
    lastTimeHTML = millis();
    Serial.println("Updating HTML!");

    initData();

    // byte* history = readFileToByteArray(logFileName, MAX_BYTES_TO_READ);
    // if (history == nullptr) {
    //     Serial.println("History byte array is nullptr.");
    //     return;
    // }
    // String historyStr = bytesToString(history);
    // free(history);
    // historyStr.replace("\n", "<br />\n");

    lastHTML = String(index_html);
    lastHTML = String("HTTP/1.1 200 OK\nContent-Type: text/html\n\n") + lastHTML;
    lastHTML.replace("_HEAD_", R"===(<meta http-equiv="refresh" content="300" >)===");
    lastHTML.replace("_IP_", WiFi.localIP().toString());
    lastHTML.replace("_LED-STATUS_", ledStatus == HIGH ? "Off" : "On");
    lastHTML.replace("_TIME_", getTime());
    lastHTML.replace("_DST_", dst == 1 ? "On" : "Off");
    lastHTML.replace("_TEMP1_", lastTemps[0] != BAD_TEMP ? String(lastTemps[0]) : "BAD");
    lastHTML.replace("_TEMP2_", lastTemps[1] != BAD_TEMP ? String(lastTemps[1]) : "BAD");
    lastHTML.replace("_TEMP-DIFF_", lastTemps[1] == BAD_TEMP || lastTemps[1] == BAD_TEMP
                                        ? "BAD"
                                        : String(lastTemps[0] - lastTemps[1]));

    // lastHTML.replace("_HISTORY_", historyStr);
    lastHTML.replace("_HISTORY_", temperatureError
                                      ? "<span style='color: red'>Error: last temperature measurement was bad.</span>"
                                      : "");
    lastHTML.replace("_HOTDATA_", hotData);
    lastHTML.replace("_COLDDATA_", coldData);

    lastHTML.replace("_LASTHOT_", String(lastTemps[0]));
    lastHTML.replace("_LASTCOLD_", String(lastTemps[1]));
}

void logTemps() {
    if (millis() - lastLoggedTime >= TIME_BETWEEN_MEASUREMENTS) {
        Serial.println("Logging Temps!");
        if (lastTemps[0] == BAD_TEMP || lastTemps[1] == BAD_TEMP) {
            Serial.println("Last measurements were bad, will not record.");
            temperatureError = true;
            lastTimeTemps = millis() - TIME_BETWEEN_MEASUREMENTS + 60 * 1000; // wait a minute before retrying
            lastLoggedTime = millis() - TIME_BETWEEN_MEASUREMENTS + 120 * 1000;
            return;
        }
        temperatureError = false;
        setDST();
        time_t* now = (time_t*)calloc(sizeof(time_t), 1);
        *now = time(nullptr);
        Serial.printf("Time: %ld\n", *now);
        byte* line = (byte*)malloc(TOTAL_LOG_LINE_SIZE);
        memcpy(line, now, TIMESTAMP_SIZE);
        line[TOTAL_LOG_LINE_SIZE - TEMP_SIZE * 2] = lastTemps[0];
        line[TOTAL_LOG_LINE_SIZE - TEMP_SIZE] = lastTemps[1];
        appendFileAtPosition(logFileName, line, TOTAL_LOG_LINE_SIZE, position);
        position += TOTAL_LOG_LINE_SIZE;
        position %= MAX_BYTES_TO_READ;
        savePosition(position);
        free(line);
        free(now);
        lastLoggedTime = millis();
    }
}

void setup() {
    Serial.begin(115200);
    delay(10);

    Serial.println("\n~~~ Setup Begin! ~~~\n\n");

    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
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
    Serial.printf("getWaitForConversion() = %d\n", sensors.getWaitForConversion());
    Serial.printf("getCheckForConversion() = %d\n", sensors.getCheckForConversion());
    Serial.printf("getResolution() = %d\n", sensors.getResolution());
    Serial.println("Configuring temperature sensors!");
    sensors.setWaitForConversion(true);
    sensors.setCheckForConversion(false);
    sensors.setResolution(9);
    Serial.printf("getWaitForConversion() = %d\n", sensors.getWaitForConversion());
    Serial.printf("getCheckForConversion() = %d\n", sensors.getCheckForConversion());
    Serial.printf("getResolution() = %d\n", sensors.getResolution());

    configureTime();
    delay(500);
    Serial.print("\nWaiting for time...");
    while (time(nullptr) < 1000) {
        Serial.print(".");
        delay(500);
    }
    delay(500);
    Serial.print("\nGot Time. Current time is: ");
    Serial.println(getTime());

    // for (int i = 0; i < 4; i++) {
    //     toggleLED(false);
    //     delay(300);
    //     toggleLED(true);
    //     delay(300);
    // }

    if (!LittleFS.begin()) {
        Serial.println("LittleFS mount failed. Restarting...");
        ESP.restart();
    }

    delay(200);

    position = getPosition();

    Serial.println("\n~~~ Setup Finished! ~~~\n\n");
}

void loop() {
    updateTemps();
    logTemps();

    WiFiClient client = server.available();
    if (!client) {
        return;
    }
    Serial.println("New client");
    unsigned long available = millis();
    while (!client.available()) {
        delay(10);
        if (millis() - available > 1500) {
            Serial.println("failed to connect.\n");
            return;
        }
    }
    String request = client.readStringUntil('\r');
    Serial.println(request);

    handleRequest(request);
    updateHTML();
    client.print(lastHTML);
    client.flush();

    delay(100);
    Serial.println("Client disonnected");
    Serial.println("");
}
