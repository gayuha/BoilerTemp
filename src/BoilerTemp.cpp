#include <Arduino.h>
#include <ArduinoOTA.h>

#include <DallasTemperature.h>

#include <ESP8266WiFi.h>
#include <SD.h>
#include <SPI.h>
#include <time.h>

#include "Configuration.h"

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
    sensors.requestTemperatures();
    return (sensors.getTempCByIndex(sensorIndex));
}
float getTemp(const DeviceAddress sensorAddress) {
    sensors.requestTemperatures();
    return (sensors.getTempC(sensorAddress));
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
    }
    if (request.indexOf("/led=on") != -1) {
        toggleLED(true);
    }
    if (request.indexOf("/led") != -1) {
        toggleLED();
    }
    if (request.indexOf("/sd") != -1) {
        connectSD();
    }
    if (request.indexOf("/dst=off") != -1) {
        dst = 0;
        configureTime();
    }
    if (request.indexOf("/dst=on") != -1) {
        dst = 1;
        configureTime();
    }
    if (request.indexOf("/cleanhistory") != -1) {
        cleanHistory(logFileName);
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

void sendHTML(WiFiClient client) {
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/html");
    client.println("");
    client.println("<html>");
    client.println("<head>");
    client.println("<meta http-equiv=\"refresh\" content=\"60\" >");
    client.println("</head>");
    client.println("<body>");
    client.println("<p>");
    client.print("IP: ");
    client.println(WiFi.localIP());
    client.println("<br />");
    client.print("SD Connected: ");
    client.print(sdConnected ? "True" : "False");
    client.println("<br />");

    client.print("Led is: ");
    client.print(ledStatus == HIGH ? "Off" : "On");
    client.println("<br />");

    client.print("Time: ");
    client.print(getTime());
    client.println("<br />");

    client.print("DST: ");
    client.print(dst == 1 ? "On" : "Off");
    client.println("<br />");

    client.print("Temperature 1: ");
    client.print(getTemp(sensor1Address));
    client.println(" &deg;C<br />");

    client.print("Temperature 2: ");
    client.print(getTemp(sensor2Address));
    client.println(" &deg;C<br />");

    client.println("</p>");

    client.println("<p><br /><br />");
    String history = readSD(logFileName);
    history.replace("\n", "<br />");
    client.println(history);
    client.println("</p>");

    client.println("</body>");
    client.println("</html>");
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
        logSD(logFileName, getTime() + String(" => ") + String(getTemp(sensor1Address)) + String(" &deg;C") + ", " +
                               String(getTemp(sensor2Address)) + String(" &deg;C"));
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
