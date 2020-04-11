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
unsigned long lastMeasuredTime = millis() - TIME_BETWEEN_MEASUREMENTS; // write first measurement on startup

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

int dst = DST;

WiFiServer server(80);

// Declarations
void configureTime();
bool connectSD();
void logSD(String filename, String line);
void rename(String from, String to);
void cleanHistory(String filename);
float getTemp(const int sensorIndex);
float getTemp(const DeviceAddress sensorAddress);
void toggleLED(bool on);
void toggleLED();
void handleRequest(String request);
const String getTime();
String readSD(String filename);
void setupOTA();

// Functions
void configureTime() { configTime(TIMEZONE * 3600, dst * 3600, "pool.ntp.org", "time.nist.gov"); }

bool connectSD() { return (sdConnected = SD.begin(SS)); }

void logSD(String filename, String line) {
    File logFile;
    logFile = SD.open(filename, FILE_WRITE);
    if (!logFile) {
        Serial.println("Failed to open file on SD for writing.");
        sdConnected = false;
        connectSD();
        return;
    }
    logFile.println(line);
    logFile.flush();
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

float getTemp(const int sensorIndex) {
    float temp = -100;
    sensors.requestTemperatures();
    while ((temp = sensors.getTempCByIndex(sensorIndex)) < -50) {
        delay(100);
    }
    return (temp);
}
float getTemp(const DeviceAddress sensorAddress) {
    float temp = -100;
    sensors.requestTemperatures();
    while ((temp = sensors.getTempC(sensorAddress)) < -50) {
        delay(100);
    }
    return (temp);
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

String readSD(String filename) {
    File logFile;
    String out = "";
    logFile = SD.open(filename, FILE_READ);
    if (!logFile) {
        Serial.println("Failed to open file on SD for reading.");
        sdConnected = false;
        connectSD();
        return "";
    }
    while (logFile.available() != 0) {
        String LineString = logFile.readStringUntil('\n');
        LineString.concat("\n" + out);
        out = LineString;
    }
    return out;
}

String parseHot(String line) {
    int i1 = 0, i2 = 0;
    for (i1 = 0; line.charAt(i1) != ' ' && i1 < 50; i1++) {
    }
    String time = line.substring(0, i1);

    for (i2 = i1 + 1; line.charAt(i2) != ' ' && i2 < 50; i2++) {
    }
    String tempHot = line.substring(i1 + 1, i2);

    String point = "{";
    point += "x: " + time + "000, ";
    point += "y: " + tempHot;
    point += "}";
    return point;
}

String parseCold(String line) {
    int i1 = 0, i2 = 0, i3 = 0;
    for (i1 = 0; line.charAt(i1) != ' ' && i1 < 50; i1++) {
    }
    String time = line.substring(0, i1);

    for (i2 = i1 + 1; line.charAt(i2) != ' ' && i2 < 50; i2++) {
    }
    for (i3 = i2 + 1; line.charAt(i3) != ' ' && i3 < 50; i3++) {
    }
    String tempCold = line.substring(i2 + 1, i3);

    String point = "{";
    point += "x: " + time + "000, ";
    point += "y: " + tempCold;
    point += "}";
    return point;
}

String* parseTemps(String line) {
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

    String points[2] = {pointHot, pointCold};
    return points;
}

void sendHTML(WiFiClient client) {
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/html");
    client.println("");

    float temp1 = getTemp(sensor1Address);
    float temp2 = getTemp(sensor2Address);

    String hotData = "";
    String coldData = "";

    String history = readSD(logFileName);
    // history.replace("\n", "<br />\n");
    std::istringstream stream(history.c_str());
    std::string line;
    while (std::getline(stream, line)) {
        String* points = parseTemps(String(line.c_str()));
        hotData += points[0] + ",";
        coldData += points[1] + ",";
    }
    hotData.remove(hotData.length() - 1, 1);

    String html = String(index_html);
    html.replace("_HEAD_", R"===(<meta http-equiv="refresh" content="120" >)===");
    html.replace("_IP_", WiFi.localIP().toString());
    html.replace("_SD-STATUS_", sdConnected ? "True" : "False");
    html.replace("_LED-STATUS_", ledStatus == HIGH ? "Off" : "On");
    html.replace("_TIME_", getTime());
    html.replace("_DST_", dst == 1 ? "On" : "Off");
    html.replace("_TEMP1_", String(temp1));
    html.replace("_TEMP2_", String(temp2));
    html.replace("_TEMP-DIFF_", String(temp1 - temp2));
    html.replace("_HISTORY_", "");
    html.replace("_HOTDATA_", hotData);
    html.replace("_COLDDATA_", coldData);

    html.replace("_LASTHOT_", String(temp1));
    html.replace("_LASTCOLD_", String(temp2));

    client.println(html);
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

    sensors.begin();

    configureTime();
    delay(500);
    Serial.print("\nWaiting for time...");
    while (!time(nullptr)) {
        Serial.print(".");
        delay(500);
    }
    Serial.print("\nGot Time. Current time is: ");
    Serial.println(getTime());

    for (int i = 0; i < 4; i++) {
        toggleLED(false);
        delay(300);
        toggleLED(true);
        delay(300);
    }

    Serial.println("\n~~~ Setup Finished! ~~~\n\n");
}

void loop() {
    ArduinoOTA.handle();
    if (millis() - lastMeasuredTime >= TIME_BETWEEN_MEASUREMENTS) {
        float temp1 = getTemp(sensor1Address);
        float temp2 = getTemp(sensor2Address);
        logSD(logFileName,
              String(getTimeRaw()) + " " + String(temp1) + " " + String(temp2) + " " + String(temp1 - temp2));
        lastMeasuredTime = millis();
    }

    WiFiClient client = server.available();
    if (!client) {
        return;
    }
    Serial.println("New client");
    unsigned long available = millis();
    while (!client.available()) {
        delay(10);
        if (millis() - available > 3000) {
            Serial.println("failed to connect.\n");
            return;
        }
    }
    String request = client.readStringUntil('\r');
    Serial.println(request);
    client.flush();

    handleRequest(request);
    sendHTML(client);

    delay(10);
    Serial.println("Client disonnected");
    Serial.println("");
}
