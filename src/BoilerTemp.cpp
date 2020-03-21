#include <Arduino.h>

#include <DallasTemperature.h>

#include <ESP8266WiFi.h>
#include <SD.h>
#include <SPI.h>
#include <time.h>

#include "Configuration.h"

const char* ssid = SSID;
const char* password = WIFI_PASSWORD;

bool sdConnected = false;
int ledStatus = LOW;

const char* const logFileName = LOGFILE;
unsigned long lastMeasuredTime = millis() - TIME_BETWEEN_MEASUREMENTS; // write first measurement on startup

const int oneWireBus = 2;
OneWire oneWire(oneWireBus);
DallasTemperature sensors(&oneWire);

int dst = DST;

WiFiServer server(80);

void configureTime();
bool connectSD();
void logSD(String filename, String line);
void rename(String from, String to);
void cleanHistory(String filename);
float getTemp(int sensorIndex);
void toggleLED(bool on);
void toggleLED();
void handleRequest(String request);
const String getTime();
String readSD(String filename);

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

float getTemp(int sensorIndex) {
    sensors.requestTemperatures();
    return (sensors.getTempCByIndex(sensorIndex));
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

void setup() {
    Serial.begin(115200);
    delay(10);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    Serial.println();
    Serial.println();
    Serial.print("Connecting to ");
    Serial.print(ssid);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    IPAddress ip(IP);
    IPAddress gateway(GATEWAY);
    IPAddress subnet(SUBNET);
    WiFi.config(ip, gateway, subnet);

    Serial.println("");
    Serial.println("WiFi connected");

    server.begin();
    Serial.println("Server started");

    Serial.print("Use this URL to connect: ");
    Serial.print("http://");
    Serial.print(WiFi.localIP());
    Serial.println("/");

    Serial.println();
    Serial.println("Initializing SD card...");
    pinMode(SS, OUTPUT);
    if (!connectSD()) {
        Serial.println("SD did not initiliaze");
    } else {
        Serial.println("SD initialized.");
    }

    sensors.begin();

    configureTime();
    delay(1000);
    Serial.print("\nWaiting for time");
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
        delay(100);
    }

    Serial.println("\nSetup Finished!\n\n");
}

void loop() {
    if (millis() - lastMeasuredTime >= TIME_BETWEEN_MEASUREMENTS) {
        float temperatureC = getTemp(0);
        logSD(logFileName, getTime() + String(" => ") + String(temperatureC) + String(" &deg;C"));
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

    float temperatureC = getTemp(0);
    client.print("Temperature: ");
    client.print(temperatureC);
    client.println(" &deg;C<br />");

    client.println("</p>");

    client.println("<p><br /><br />");
    String history = readSD(logFileName);
    history.replace("\n", "<br />");
    client.println(history);
    client.println("</p>");

    client.println("</body>");
    client.println("</html>");
    delay(10);
    Serial.println("Client disonnected");
    Serial.println("");
}
