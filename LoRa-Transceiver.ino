#include <WiFi.h>
#include <WebServer.h>
#include <TinyGPS++.h>
#include <BluetoothSerial.h>
#include <SPI.h>
#include <LoRa.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <AESLib.h>
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"

// --- set SyncWord to "205"
#define LORA_SYNC_WORD 0xCD

// --- Pin setting GPS ---
#define RXD2 3
#define TXD2 1
#define GPS_BAUD 9600

// --- Pin setting LoRa ---
#define SCK   5
#define MISO 19
#define MOSI 27
#define SS   18
#define RST  14
#define DIO0 26

// --- Pin Setting OLED ---
#define OLED_SDA 4
#define OLED_SCL 15 
#define OLED_RST 16
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// --- initialize GPS ---
TinyGPSPlus gps;
HardwareSerial gpsSerial(2);

// --- WiFi settings ---
const char* ssid = "LoRa1";
const char* password = "1aRoL666";

// --- Webserver on Port 80 ---
WebServer server(80);

// --- Frequency ---
#define BAND 868E6 // Europa

// --- Current LoRa transmission power ---
int txPower = 14;

// --- AES encryption ---
AESLib aesLib;
byte aesKey[16] = { 0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,
                    0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F,0x10 };
byte aesIV[16]  = { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
                    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 };

// --- Objects ---
BluetoothSerial SerialBT;  
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RST);

String receivedText = "";

// --- Set Bluetooth PIN ---
void setBluetoothPin(const char* pin) {
  esp_bt_pin_type_t pin_type = ESP_BT_PIN_TYPE_FIXED;
  esp_bt_pin_code_t pin_code;
  snprintf((char*)pin_code, 17, "%s", pin); // max 16 Zeichen
  esp_bt_gap_set_pin(pin_type, strlen(pin), pin_code);
}

// --- HTML webpage with slider ---
String getPage() {
  String html = "<!DOCTYPE html><html><head><title>LoRa Power Control</title>";
  html += "<style>body{font-family:sans-serif;text-align:center;margin-top:40px;} ";
  html += "input[type=range]{width:80%;}</style></head><body>";
  html += "<h1>LoRa1 transmisson power</h1>";
  html += "<p>Currently: <b>" + String(txPower) + " dBm</b></p>";
  html += "<form action=\"/set\" method=\"GET\">";
  html += "<input type=\"range\" name=\"power\" min=\"14\" max=\"20\" value=\"" + String(txPower) + "\" oninput=\"this.nextElementSibling.value=this.value\">";
  html += "<output>" + String(txPower) + "</output> dBm<br><br>";
  html += "<input type=\"submit\" value=\"Confirm\">";
  html += "</form>";
  html += "</body></html>";
  return html;
}

// --- Handler for root ---
void handleRoot() {
  server.send(200, "text/html", getPage());
}

// --- Handler to set the power ---
void handleSetPower() {
  if (server.hasArg("power")) {
    txPower = server.arg("power").toInt();
    if (txPower < 14) txPower = 14;
    if (txPower > 20) txPower = 20;
    LoRa.setTxPower(txPower, PA_OUTPUT_PA_BOOST_PIN);
    Serial.println("New transmission power: " + String(txPower) + " dBm");
  }
  server.send(200, "text/html", getPage());
}

// --- Function to send GPS-Data ---
void sendGPSData() {
  if (gps.location.isValid()) {
    String gpsMessage = "LoRa1: " + String(gps.location.lat(), 6) + 
                        "," + String(gps.location.lng(), 6) + 
                        ", Altitude:" + String(gps.altitude.meters(), 1) + "m" +
                        ", Speed:" + String(gps.speed.kmph(), 1) + "km/h";

    byte buffer[256];
    gpsMessage.getBytes(buffer, gpsMessage.length() + 1);

    byte encrypted[256];
    int encryptedLen = aesLib.encrypt(buffer, gpsMessage.length(), encrypted, aesKey, 128, aesIV);

    LoRa.beginPacket();
    LoRa.write(encrypted, encryptedLen);
    LoRa.endPacket();

    Serial.println("GPS sent: " + gpsMessage);
    SerialBT.println("GPS sent: " + gpsMessage);
  } else {
    Serial.println("No valid GPS-Fix!");
    SerialBT.println("No valid GPS-Fix!");
  }
}

void setup() {
  Serial.begin(115200);

  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, RXD2, TXD2);

  // set LoRa parameter
  LoRa.setSpreadingFactor(12);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(8);
  LoRa.enableCrc();

  // start Bluetooth
  if(!SerialBT.begin("LoRa1")) {
    Serial.println("Bluetooth start failed!");
    while(1);
  }
  setBluetoothPin("1234");
  Serial.println("Bluetooth started. Pairing-PIN = 1234");

  // OLED reset
  pinMode(OLED_RST, OUTPUT);
  digitalWrite(OLED_RST, LOW);
  delay(20);
  digitalWrite(OLED_RST, HIGH);

  Wire.begin(OLED_SDA, OLED_SCL);
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3c, false, false)) { 
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); 
  }
  
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.setCursor(0,0);
  display.println("LoRa & BT Ready");
  display.display();

  // initialize LoRa
  SPI.begin(SCK, MISO, MOSI, SS);
  LoRa.setPins(SS, RST, DIO0);		

  if (!LoRa.begin(BAND)) {
    Serial.println("LoRa init failed!");
    display.clearDisplay();
    display.setCursor(0,0);
    display.println("LoRa FAILED");
    display.display();
    while (1);
  }

  LoRa.setSyncWord(LORA_SYNC_WORD);

  LoRa.setTxPower(txPower, PA_OUTPUT_PA_BOOST_PIN);
  Serial.println("LoRa started!");

  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password, 1, 1);

  server.on("/", handleRoot);
  server.on("/set", handleSetPower);
  server.begin();
}

void loop() {
  server.handleClient();

  while (gpsSerial.available() > 0) {
    gps.encode(gpsSerial.read());
  }

  // --- Bluetooth-Receiver ---
  if (SerialBT.available()) {
    char receivedChar = SerialBT.read();
    receivedText += receivedChar;
    
    if (receivedChar == '\n') {
      receivedText.trim();

      if (receivedText.equalsIgnoreCase("GPS")) {
        sendGPSData();
      } else {
        // Normaler Text
        byte buffer[256];
        int len = receivedText.length();
        receivedText.getBytes(buffer, len + 1);

        byte encrypted[256];
        int encryptedLen = aesLib.encrypt(buffer, len, encrypted, aesKey, 128, aesIV);

        LoRa.beginPacket();
        LoRa.write(encrypted, encryptedLen);
        LoRa.endPacket();

        Serial.print("Sended (BT->LoRa): ");
        Serial.println(receivedText);
      }
      receivedText = "";
    }
  }

  // --- LoRa-Receiver ---
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    byte received[256];
    int len = 0;
    while (LoRa.available()) {
      received[len++] = LoRa.read();
    }

    byte decrypted[256];
    int decryptedLen = aesLib.decrypt(received, len, decrypted, aesKey, 128, aesIV);

    String decryptedMessage = "";
    for(int i = 0; i < decryptedLen; i++) {
      if(decrypted[i] >= 32 && decrypted[i] <= 126) {
        decryptedMessage += (char)decrypted[i];
      }
    }

    // OLED
    display.clearDisplay();
    display.setCursor(0,0);
    display.print(decryptedMessage);
    display.display();

    // Also send via Bluetooth
    SerialBT.println("LoRa1: " + decryptedMessage);
    Serial.println("Received and decrypted " + decryptedMessage);

    // Check for "GPSr" command
    if (decryptedMessage.equalsIgnoreCase("GPSr")) {
      sendGPSData();
    }
  }
}
