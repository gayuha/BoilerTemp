#include <Arduino.h>
#include <ArduinoOTA.h>

#include <DallasTemperature.h>

#include <ESP8266WiFi.h>
#include <SD.h>
#include <SPI.h>
#include <time.h>

#include <sstream>

#include "Configuration.h"
#include "index.h"

bool sdConnected = false;
int ledStatus = LOW;

const char* const logFileName = LOGFILE;
time_t lastLoggedTime = millis() - TIME_BETWEEN_MEASUREMENTS; // write first measurement on startup

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

int dst = DST;

WiFiServer server(80);

String lastHTML = "Yo!";
time_t lastTimeHTML = millis() - TIME_BETWEEN_HTML_UPDATES;

float lastTemps[2];
time_t lastTimeTemps = millis() - TIME_BETWEEN_MEASUREMENTS;

String hotData = "";
String coldData = "";

// Declarations
void configureTime();
bool connectSD();
void logSD(String filename, String line);
void rename(String from, String to);
void cleanHistory(String filename);
float getTemp(const DeviceAddress sensorAddress);
void toggleLED(bool on);
void toggleLED();
void handleRequest(String request);
const String getTime();
String readSD(String filename, int maxLines = 200);
void setupOTA();
void initData();
void logTempsToSD();

// Functions
void configureTime() { configTime(TIMEZONE * 3600, dst * 3600, NTP_SERVER_1, NTP_SERVER_2); }

bool connectSD() { return (sdConnected = SD.begin(SS)); }

void logSD(String filename, String line) {
    File logFile;
    if (!SD.exists(filename)) {
        // file does not exist
        Serial.println("File does not exist. Will attempt to create it.");
    }
    logFile = SD.open(filename, FILE_WRITE);
    if (!logFile) {
        Serial.println("Failed to open file on SD for writing.");
        sdConnected = false;
        connectSD();
        return;
    }
    logFile.println(line);
    logFile.flush();
    logFile.close();
    // Serial.println("Wrote to SD: ");
    // Serial.println(line);
}

void rename(String from, String to) {
    String data = readSD(from);
    logSD(to, data);
    SD.remove(from);
}

void cleanHistory(String filename) {
    int i = 0;
    String newName;
    for (i = 0; SD.exists(newName = "backup_" + String(i) + ".log"); ++i)
        ;
    rename(filename, newName);
}

void printDirectory(File dir, int numTabs) {
    while (true) {

        File entry = dir.openNextFile();
        if (!entry) {
            // no more files
            break;
        }
        for (uint8_t i = 0; i < numTabs; i++) {
            Serial.print('\t');
        }
        Serial.print(entry.name());
        if (entry.isDirectory()) {
            Serial.println("/");
            printDirectory(entry, numTabs + 1);
        } else {
            // files have sizes, directories do not
            Serial.print("\t\t");
            Serial.print(entry.size(), DEC);
            time_t cr = entry.getCreationTime();
            time_t lw = entry.getLastWrite();
            struct tm* tmstruct = localtime(&cr);
            Serial.printf("\tCREATION: %d-%02d-%02d %02d:%02d:%02d", (tmstruct->tm_year) + 1900, (tmstruct->tm_mon) + 1,
                          tmstruct->tm_mday, tmstruct->tm_hour, tmstruct->tm_min, tmstruct->tm_sec);
            tmstruct = localtime(&lw);
            Serial.printf("\tLAST WRITE: %d-%02d-%02d %02d:%02d:%02d\n", (tmstruct->tm_year) + 1900,
                          (tmstruct->tm_mon) + 1, tmstruct->tm_mday, tmstruct->tm_hour, tmstruct->tm_min,
                          tmstruct->tm_sec);
        }
        entry.close();
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
    lastTimeTemps = millis();
    lastTemps[0] = getTemp(sensor1Address);
    lastTemps[1] = getTemp(sensor2Address);
    // lastTemps[0] = 11;
    // lastTemps[1] = 12;
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
    if (request.indexOf("/sd") != -1) {
        connectSD();
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
        File root = SD.open("/");
        if (!root) {
            Serial.println("Failed to open root directory!");
        }
        printDirectory(root, 0);
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

time_t getTimeRaw() { return time(nullptr) - (TIMEZONE + DST) * 3600; }

String readSD(String filename, int maxLines) {
    File logFile;
    String out = "";
    if (sdConnected) {
        if (!(logFile = SD.open(filename, FILE_READ))) {
            // file does not exist
            Serial.println("Failed to open file on SD for reading - File does not exist.");
            return "";
        }
    }
    int i = 0;
    do {
        logFile = SD.open(filename, FILE_READ);
        i++;
        delay(100);
    } while (!logFile && i < 5);
    if (!logFile) {
        Serial.println("Failed to open file on SD for reading.");
        sdConnected = false;
        connectSD();
        return "Failed to open file on SD for reading: " + String(logFile);
    }
    i = 0;
    while (logFile.available() != 0 && i < maxLines) {
        String LineString = logFile.readStringUntil('\n');
        LineString.concat("\n" + out);
        out = LineString;
        i++;
    }
    logFile.close();
    return out;
}

String* parseTemps(String line) {
    Serial.println("Parsing line: " + line);
    int i1 = 0, i2 = 0, i3 = 0;
    for (i1 = 0; line.charAt(i1) != ' ' && i1 < 50; i1++) {
    }
    String time = line.substring(0, i1);

    for (i2 = i1 + 1; line.charAt(i2) != ' ' && i2 < 50; i2++) {
    }
    String tempHot = line.substring(i1 + 1, i2);
    for (i3 = i2 + 1; line.charAt(i3) != ' ' && i3 < 50; i3++) {
    }
    String tempCold = line.substring(i2 + 1, i3);

    String pointHot = "{";
    pointHot += "x: " + time + "000, ";
    pointHot += "y: " + tempHot;
    pointHot += "}";

    String pointCold = "{";
    pointCold += "x: " + time + "000, ";
    pointCold += "y: " + tempCold;
    pointCold += "}";

    String* points = new String[2];
    points[0] = pointHot;
    points[1] = pointCold;
    return points;
}

void initData() {
    Serial.println("Initializing data!");
    String history = "";
    history = readSD(logFileName);
    // hotData = "Hey! " + history;
    // coldData = "data inited!";
    hotData = coldData = "";
    std::istringstream stream(history.c_str());
    std::string line;
    Serial.println("Starting to parse points!");
    for (int i = 0; i < MAX_POINTS_ON_GRAPH && std::getline(stream, line); i++) {
        String* points = parseTemps(String(line.c_str()));
        hotData += points[0] + ",";
        coldData += points[1] + ",";
        Serial.println("points[0] = " + points[0] + ", points[1] = " + points[1]);
    }
    hotData.remove(hotData.length() - 1, 1);
    coldData.remove(coldData.length() - 1, 1);
}

void updateHTML() {
    if (millis() - lastTimeHTML < TIME_BETWEEN_HTML_UPDATES) {
        return;
    }
    lastTimeHTML = millis();

    // initData();
    float temp1 = lastTemps[0];
    float temp2 = lastTemps[1];

    String history = readSD(logFileName);

    lastHTML = String(index_html);
    lastHTML = String("HTTP/1.1 200 OK\nContent-Type: text/html\n\n") + lastHTML;
    lastHTML.replace("_HEAD_", R"===(<meta http-equiv="refresh" content="300" >)===");
    lastHTML.replace("_IP_", WiFi.localIP().toString());
    lastHTML.replace("_SD-STATUS_", sdConnected ? "True" : "False");
    lastHTML.replace("_LED-STATUS_", ledStatus == HIGH ? "Off" : "On");
    lastHTML.replace("_TIME_", getTime());
    lastHTML.replace("_DST_", dst == 1 ? "On" : "Off");
    lastHTML.replace("_TEMP1_", String(temp1));
    lastHTML.replace("_TEMP2_", String(temp2));
    lastHTML.replace("_TEMP-DIFF_", String(temp1 - temp2));

    history.replace("\n", "<br />\n");
    lastHTML.replace("_HISTORY_", history);
    lastHTML.replace("_HOTDATA_", hotData);
    lastHTML.replace("_COLDDATA_", coldData);

    lastHTML.replace("_LASTHOT_", String(temp1));
    lastHTML.replace("_LASTCOLD_", String(temp2));
}

void setupOTA() {
    ArduinoOTA.setPort(3232);
    ArduinoOTA.onStart([]() {
        String type;
        if (ArduinoOTA.getCommand() == U_FLASH)
            type = "sketch";
        else // U_SPIFFS
            type = "filesystem";

        // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
        Serial.println("Start updating " + type);
    });

    ArduinoOTA.onEnd([]() { Serial.println("\nEnd"); });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    });

    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR)
            Serial.println("Auth Failed");
        else if (error == OTA_BEGIN_ERROR)
            Serial.println("Begin Failed");
        else if (error == OTA_CONNECT_ERROR)
            Serial.println("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR)
            Serial.println("Receive Failed");
        else if (error == OTA_END_ERROR)
            Serial.println("End Failed");
    });

    ArduinoOTA.begin();
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

    Serial.println("Setting up OTA...");
    setupOTA();
    Serial.println("OTA setup end.");

    Serial.println("");
    Serial.println("WiFi connected.");

    server.begin();
    Serial.println("Server started.");

    pinMode(SS, OUTPUT);

    for (int i = 0; i < 10 && !connectSD(); i++) {
        delay(100);
    }

    sensors.begin();

    configureTime();
    delay(100);
    Serial.print("\nWaiting for time...");
    while (!time(nullptr)) {
        Serial.print(".");
        delay(500);
    }
    Serial.print("\nGot Time. Current time is: ");
    Serial.println(getTime());

    // for (int i = 0; i < 4; i++) {
    //     toggleLED(false);
    //     delay(300);
    //     toggleLED(true);
    //     delay(300);
    // }

    delay(200);
    // initData();

    Serial.println("\n~~~ Setup Finished! ~~~\n\n");
}

void logTempsToSD() {
    if (millis() - lastLoggedTime >= TIME_BETWEEN_MEASUREMENTS) {
        float temp1 = lastTemps[0];
        float temp2 = lastTemps[1];
        String line = String(getTimeRaw()) + " " + String(temp1) + " " + String(temp2) + " " + String(temp1 - temp2);
        logSD(logFileName, line);
        /*
        String* points = parseTemps(String(line.c_str()));
        hotData = points[0] + "," + hotData;
        coldData = points[1] + "," + coldData;*/
        lastLoggedTime = millis();
    }
}

void loop() {
    ArduinoOTA.handle();
    updateTemps();
    logTempsToSD();

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
