#include <Arduino.h>

#include <DallasTemperature.h>

#include <ESP8266WiFi.h>
#include <FS.h>
#include <LittleFS.h>
#include <time.h>

#include <sstream>

#include "Configuration.h"
#include "index.h"

bool sdConnected = false;
int ledStatus = LOW;

const char* const logFileName = LOGFILE;
time_t lastLoggedTime = millis();

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

int dst = DST;

WiFiServer server(80);

String lastHTML = "Yo!";
time_t lastTimeHTML = millis() - TIME_BETWEEN_HTML_UPDATES;

byte lastTemps[2];
time_t lastTimeTemps = millis() - TIME_BETWEEN_MEASUREMENTS;

String hotData = "";
String coldData = "";

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
byte* readSD(String filename, int maxLines = 200);
void initData();
void logTemps();
void updateHTML();

// Functions
void configureTime() {
    configTime(TIMEZONE * 3600, dst * 3600, NTP_SERVER_1, NTP_SERVER_2);
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
    delay(2000); // Make sure the CREATE and LASTWRITE times are different
    file.close();
}

void appendFile(const char* path, const byte* message, size_t size) {
    Serial.printf("Appending to file: %s\n", path);

    File file = LittleFS.open(path, "a");
    if (!file) {
        Serial.println("Failed to open file for appending.");
        return;
    }
    if (file.write(message, size)) {
        Serial.println("Message appended");
    } else {
        Serial.println("Append failed");
    }
    file.close();
}

void appendFile(const char* path, const char* message) {
    Serial.printf("Appending to file: %s, message: %s\n", path, message);

    File file = LittleFS.open(path, "a");
    if (!file) {
        Serial.println("Failed to open file for appending. Trying to write instead.");
        writeFile(path, message);
        return;
    }
    if (file.print(message)) {
        Serial.println("Message appended");
    } else {
        Serial.println("Append failed");
    }
    file.close();
}

void cleanHistory(String filename) {
    int i = 0;
    String newName;
    for (i = 0; LittleFS.exists(newName = "backup_" + String(i) + ".log"); ++i)
        ;
    renameFile(filename.c_str(), newName.c_str());
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
        sensors.requestTemperatures();
        temp = sensors.getTempC(sensorAddress);
        if (temp > -50) {
            totalTemp += temp;
            ++goodTemps;
        }
        delay(10);
    }
    if (goodTemps == 0) {
        return -127;
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
    if (request.indexOf("/dst=off") != -1) {
        dst = 0;
        configureTime();
        return;
    }
    if (request.indexOf("/dst=on") != -1) {
        dst = 1;
        configureTime();
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

time_t getTimeRaw() {
    return time(nullptr) - (TIMEZONE + dst) * 3600;
}

byte* readSD(String filename, int maxLines) {
    File logFile;
    byte* buffer = (byte*)calloc(maxLines, 1);
    logFile = LittleFS.open(filename.c_str(), "r");
    logFile.read(buffer, sizeof(buffer));
    logFile.close();
    return buffer;
}

String* parseTemps(const byte* bytes) {
    Serial.println("Parsing line");
    String timeStr;
    time_t time = 0;

    // Assuming little endian
    for (size_t i = 0, mult = 1; i < sizeof(time_t); i++, mult *= 2) {
        time += bytes[i] * mult;
    }
    timeStr = String(time);

    String tempHot = String(bytes[sizeof(time_t)]);
    String tempCold = String(bytes[sizeof(time_t) + 1]);

    String pointHot = "{";
    pointHot += "x: " + timeStr + "000, ";
    pointHot += "y: " + tempHot;
    pointHot += "}";

    String pointCold = "{";
    pointCold += "x: " + timeStr + "000, ";
    pointCold += "y: " + tempCold;
    pointCold += "}";

    String* points = new String[2];
    points[0] = pointHot;
    points[1] = pointCold;
    return points;
}

void initData() {
    Serial.println("Initializing data!");
    byte* history = readSD(logFileName);
    // hotData = "Hey! " + history;
    // coldData = "data inited!";
    hotData = coldData = "";
    Serial.println("Starting to parse points!");
    for (int i = 0; i < MAX_POINTS_ON_GRAPH; i++) {
        byte* line = history + i * 10;
        if (*line == 0) {
            break;
        }
        String* points = parseTemps(line);
        hotData += points[0] + ",";
        coldData += points[1] + ",";
        Serial.println("points[0] = " + points[0] + ", points[1] = " + points[1]);
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

    byte* history = readSD(logFileName);

    lastHTML = String(index_html);
    lastHTML = String("HTTP/1.1 200 OK\nContent-Type: text/html\n\n") + lastHTML;
    lastHTML.replace("_HEAD_", R"===(<meta http-equiv="refresh" content="300" >)===");
    lastHTML.replace("_IP_", WiFi.localIP().toString());
    lastHTML.replace("_SD-STATUS_", sdConnected ? "True" : "False");
    lastHTML.replace("_LED-STATUS_", ledStatus == HIGH ? "Off" : "On");
    lastHTML.replace("_TIME_", getTime());
    lastHTML.replace("_DST_", dst == 1 ? "On" : "Off");
    lastHTML.replace("_TEMP1_", String(lastTemps[0]));
    lastHTML.replace("_TEMP2_", String(lastTemps[1]));
    lastHTML.replace("_TEMP-DIFF_", String(lastTemps[0] - lastTemps[1]));

    lastHTML.replace("_HISTORY_", reinterpret_cast<char*>(history));
    lastHTML.replace("_HOTDATA_", hotData);
    lastHTML.replace("_COLDDATA_", coldData);

    lastHTML.replace("_LASTHOT_", String(lastTemps[0]));
    lastHTML.replace("_LASTCOLD_", String(lastTemps[1]));
}

void logTemps() {
    if (millis() - lastLoggedTime >= TIME_BETWEEN_MEASUREMENTS) {
        Serial.println("Logging Temps!");
        time_t* time = (time_t*)malloc(sizeof(time_t));
        *time = getTimeRaw();
        byte* line = (byte*)malloc(10);
        memcpy(line, time, 8);
        line[8] = lastTemps[0];
        line[9] = lastTemps[1];
        // String line = String(getTimeRaw()) + " " + String(lastTemps[0]) + " " + String(lastTemps[1]);
        appendFile(logFileName, line, 10);
        free(line);
        free(time);
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

    Serial.println("");
    Serial.println("WiFi connected.");

    server.begin();
    Serial.println("Server started.");

    pinMode(SS, OUTPUT);

    sensors.begin();

    configureTime();
    delay(1000);
    Serial.print("\nWaiting for time...");
    while (!time(nullptr)) {
        Serial.print(".");
        delay(500);
    }
    delay(1000);
    Serial.print("\nGot Time. Current time is: ");
    Serial.println(getTime());

    // for (int i = 0; i < 4; i++) {
    //     toggleLED(false);
    //     delay(300);
    //     toggleLED(true);
    //     delay(300);
    // }

    if (!LittleFS.begin()) {
        Serial.println("LittleFS mount failed");
        ESP.restart();
    }

    delay(200);
    // initData();

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
