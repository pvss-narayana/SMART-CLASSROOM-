// ================== LIBRARIES ==================
#include <Wire.h>
#include <Adafruit_Fingerprint.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>

// ================== GOOGLE SCRIPT ==================
#define GOOGLE_SCRIPT_ID "// your API KEY //"

// ================== OLED ==================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ================== FINGERPRINT ==================
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&Serial2);

// ================== WIFI ==================
const char* ssid = "WI-FI Name";
const char* password = "Password";
bool wifiConnected = false;

// ================== TIME ==================
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 19800, 60000);

// ================== PINS ==================
#define BUZZER_PIN 27
#define IR_ENTRY 32
#define IR_EXIT 33
#define L298_ENA 26   // PWM pin
#define L298_IN1 25
#define L298_IN2 14
#define DHTPIN 4
#define DHTTYPE DHT22

// ================== OBJECTS ==================
LiquidCrystal_I2C lcd(0x27, 16, 2);
DHT dht(DHTPIN, DHTTYPE);

// ================== VARIABLES ==================
String NAME, ID;
int studentsInside = 0;
int studentsLeft = 0;

bool waitingForEntry = false;
bool waitingForExit = false;
bool fingerprintVerified = false;

unsigned long waitStartTime = 0;
const unsigned long WAIT_TIMEOUT = 5000;

// OLED
bool showName = false;
unsigned long nameStartTime = 0;

bool showExitName = false;
unsigned long exitNameStartTime = 0;

// ================== SETUP ==================
void setup() {

  Serial.begin(115200);
  Serial2.begin(57600, SERIAL_8N1, 16, 17);  // RX, TX

  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(IR_ENTRY, INPUT_PULLUP);
  pinMode(IR_EXIT, INPUT_PULLUP);
  pinMode(L298_IN1, OUTPUT);
  pinMode(L298_IN2, OUTPUT);
  digitalWrite(L298_IN1, HIGH);
  digitalWrite(L298_IN2, LOW);

  ledcSetup(0, 25000, 8);
  ledcAttachPin(L298_ENA, 0);
  ledcWrite(0, 0);


  Wire.begin(21, 22);

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.display();

  lcd.init();
  lcd.backlight();

  dht.begin();

  connectWiFi();
  timeClient.begin();

  if (!finger.verifyPassword()) {
    Serial.println("Fingerprint sensor not found");
    while (1)
      ;
  }
}

// ================== LOOP ==================
void loop() {

  timeClient.update();
  updateTemperatureLCD();
  updateOLED();

  // -------- ENTRY FLOW (Fingerprint FIRST) --------

  // Step 1: Scan fingerprint
  if (!fingerprintVerified && !waitingForEntry && !waitingForExit) {
    int id = getFingerprintIDez();

    if (id != -1 && finger.confidence >= 60) {
      fingerprintVerified = true;
      waitingForEntry = true;

      ID = String(id);
      setName(id);

      showName = true;
      nameStartTime = millis();

      tone(BUZZER_PIN, 2000);  // Start buzzer
      waitStartTime = millis();
    }
  }

  // Step 2: Wait for IR ENTRY sensor
  if (waitingForEntry && !waitingForExit) {

    if (digitalRead(IR_ENTRY) == LOW) {
      noTone(BUZZER_PIN);
      waitingForEntry = false;
      fingerprintVerified = false;
      studentsInside++;

      sendEntryData(timeClient.getFormattedTime());
    }

    // Timeout
    if (millis() - waitStartTime > WAIT_TIMEOUT) {
      noTone(BUZZER_PIN);
      waitingForEntry = false;
      fingerprintVerified = false;
    }
  }

  // -------- EXIT FLOW (UNCHANGED) --------
  if (!waitingForExit && digitalRead(IR_EXIT) == LOW) {
    waitingForExit = true;
    waitStartTime = millis();
    tone(BUZZER_PIN, 1800);
  }

  if (waitingForExit) {
    int id = getFingerprintIDez();

    if (id != -1 && finger.confidence >= 60) {
      noTone(BUZZER_PIN);
      waitingForExit = false;

      if (studentsInside > 0) studentsInside--;
      studentsLeft++;

      ID = String(id);
      setName(id);
      showExitName = true;
      exitNameStartTime = millis();
      sendExitData(timeClient.getFormattedTime());
    }

    if (millis() - waitStartTime > WAIT_TIMEOUT) {
      waitingForExit = false;
      noTone(BUZZER_PIN);
    }
  }

}

// ================== FUNCTIONS ==================

void updateOLED() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // ---- ENTRY NAME DISPLAY ----
  if (showName) {
    if (millis() - nameStartTime < 8000) {
      display.setTextSize(1);
      display.setCursor(0, 10);
      display.print("ENTRY:");

      display.setTextSize(2);
      display.setCursor(0, 30);
      display.print(NAME);
    } else {
      showName = false;
    }
  }

  // ---- EXIT NAME DISPLAY ----
  else if (showExitName) {
    if (millis() - exitNameStartTime < 8000) {
      display.setTextSize(1);
      display.setCursor(0, 10);
      display.print("EXIT:");

      display.setTextSize(2);
      display.setCursor(0, 30);
      display.print(NAME);
    } else {
      showExitName = false;
    }
  }

  // ---- DEFAULT DASHBOARD ----
  else {
    display.setTextSize(1);
    display.setCursor(0, 15);
    display.print("students Inside: ");
    display.print(studentsInside);

    display.setCursor(0, 35);
    display.print("Left : ");
    display.print(studentsLeft);
  }

  display.display();
}

void updateTemperatureLCD() {
  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate < 2000) return;
  lastUpdate = millis();

  float t = dht.readTemperature();
  if (isnan(t)) return;

  lcd.setCursor(0, 0);
  lcd.print("Temp: ");
  lcd.print(t);
  lcd.print(" C   ");

  // -------- FAN SPEED CONTROL (L298N) --------
int speedPWM = 0;

if (t >= 34) {
  speedPWM = 250;   // FAST
}
else if (t >= 30) { 
  speedPWM = 180;   // MEDIUM
}
else if (t >= 28) {
  speedPWM = 100;    // SLOW
}
else {
  speedPWM = 0;     // OFF
}

ledcWrite(0, speedPWM);


lcd.setCursor(0, 1);
lcd.print("Fan : ");
if (t >= 34) lcd.print("FAST ");
else if (t >= 30) lcd.print("MEDIUM ");
else if (t >= 28) lcd.print("SLOW ");
else lcd.print("OFF ");


}

void connectWiFi() {
  WiFi.begin(ssid, password);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
    delay(500);
  }
  wifiConnected = (WiFi.status() == WL_CONNECTED);
}

int getFingerprintIDez() {
  if (finger.getImage() != FINGERPRINT_OK) return -1;
  if (finger.image2Tz() != FINGERPRINT_OK) return -1;
  if (finger.fingerFastSearch() != FINGERPRINT_OK) return -1;
  return finger.fingerID;
}

void setName(int id) {
  if (id == 1) NAME = "Student1";
  else if (id == 2) NAME = "Student2";
  else if (id == 3) NAME = "Student3";
  else if (id == 4) NAME = "Student4";
  else if (id == 5) NAME = "Student5";
  else if (id == 6) NAME = "Student6";
  else if (id == 7) NAME = "Student7";
  else if (id == 8) NAME = "Student8";
  else if (id == 9) NAME = "Student9";
  else if (id == 29) NAME = "Student10";
  else NAME = "Student";
}

void sendEntryData(String timeStamp) {

  if (!wifiConnected) return;

  WiFiClientSecure client;
  client.setInsecure();

  String json =
    "{\"id\":\"" + ID + "\",\"name\":\"" + NAME + "\",\"entry\":\"" + timeStamp + "\"}";

  if (client.connect("script.google.com", 443)) {
    Serial.println("Connected to Google");
    Serial.println("Sending to Google...");

    client.println("POST /macros/s/" GOOGLE_SCRIPT_ID "/exec HTTP/1.1");
    client.println("Host: script.google.com");
    client.println("Content-Type: application/json");
    client.print("Content-Length: ");
    client.println(json.length());
    client.println("Connection: close");
    client.println();
    client.println(json);

    Serial.println("Waiting for response...");

    // ⏳ WAIT FOR RESPONSE
    unsigned long timeout = millis();
    while (client.connected() && millis() - timeout < 3000) {
      while (client.available()) {
        Serial.write(client.read());
      }
    }
    Serial.println("\nResponseended");
  }

  client.stop();
}
void sendExitData(String timeStamp) {

  if (!wifiConnected) return;

  WiFiClientSecure client;
  client.setInsecure();

  String json =
    "{\"id\":\"" + ID +"\",\"name\":\"" + NAME + "\",\"exit\":\"" + timeStamp + "\"}";

  if (client.connect("script.google.com", 443)) {

    client.println("POST /macros/s/" GOOGLE_SCRIPT_ID "/exec HTTP/1.1");
    client.println("Host: script.google.com");
    client.println("Content-Type: application/json");
    client.print("Content-Length: ");
    client.println(json.length());
    client.println("Connection: close");
    client.println();
    client.println(json);

    // ⏳ WAIT FOR RESPONSE
    unsigned long timeout = millis();
    while (client.connected() && millis() - timeout < 3000) {
      while (client.available()) {
        Serial.write(client.read());
      }
    }
  }

  client.stop();
}
