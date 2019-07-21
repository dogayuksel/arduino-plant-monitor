#include <SPI.h>
#include <WiFi.h>
#include <AES.h>
#include <GCM.h>

#include <DallasTemperature.h>
#include <OneWire.h>

// Data wire is plugged into port 2 on the Arduino
#define ONE_WIRE_BUS 2

// Setup a oneWire instance to communicate with any OneWire devices
OneWire oneWire(ONE_WIRE_BUS);

// Pass our oneWire reference to Dallas Temperature. 
DallasTemperature sensors(&oneWire);

char ssid[] = "Network_SSID";      //  your network SSID (name)
char pass[] = "Network_Password";   // your network password

GCM<AES128> gcm;

byte key[32] = {
  0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
  0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF
};

int sensorPin1 = A0;
int sensorValue1 = 0;
int sensorPin2 = A1;
int sensorValue2 = 0;

float temperature = 0.0;
int temperatureValue = 0;

int lightPin = A2;
int lightValue = 0;

int status = WL_IDLE_STATUS;

// Initialize the Wifi client library
WiFiClient client;

int port = 3000; // exposed port on your server 
char server[] = "192.168.2.115"; // your server address

unsigned long lastConnectionTime = 0; // last time you connected to the server, in milliseconds
const unsigned long postingInterval = 30L * 60L * 1000L; // delay between updates, in milliseconds

int parseIndex = 0;
byte serverRandoms[12];
byte serverSignature[16];
byte serverIV[12];
byte iv[12] = {
  0x00, 0x11, 0x22, 0x33, 0x44, 0x55,
  0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB
};

int temperCount = 0;

void setup() {
  //Initialize serial and wait for port to open:
  Serial.begin(9600);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }

  // check for the presence of the shield:
  if (WiFi.status() == WL_NO_SHIELD) {
    Serial.println(F("WiFi shield not present"));
    // don't continue:
    while (true);
  }

  String fv = WiFi.firmwareVersion();
  if (fv != "1.1.0") {
    Serial.println(F("Please upgrade the firmware"));
  }

  // attempt to connect to Wifi network:
  while (status != WL_CONNECTED) {
    Serial.print(F("Attempting to connect to SSID: "));
    Serial.println(ssid);
    // Connect to WPA/WPA2 network. Change this line if using open or WEP network:
    status = WiFi.begin(ssid, pass);

    // wait 10 seconds for connection:
    delay(10000);
  }
  // you're connected now, so print out the status:
  printWifiStatus();
  
  gcm.setKey(key, 16);

  sensors.begin();
}

void loop() {
  while (client.available()) {
    byte c = client.read();
    Serial.write(c);
    if (parseIndex == 0) {
      if (c == 0x0D) { parseIndex = 1; }
    } else if (parseIndex == 1) {
      if (c == 0x0A) { parseIndex = 2; }
      else { parseIndex = 0; } 
    } else if (parseIndex == 2) {
      if (c == 0x0D) { parseIndex = 3; }
      else { parseIndex = 0; } 
    } else if (parseIndex == 3) {
      if (c == 0x0A) { parseIndex = 4; }
      else { parseIndex = 0; } 
    } else if (parseIndex >= 4) {
      // Store byte
      if (parseIndex < (4 + 12)) {
        serverRandoms[parseIndex - 4] = c;
        parseIndex += 1;
      } else if (parseIndex < (4 + 12 + 16)) {
        serverSignature[parseIndex - 4 - 12] = c;
        parseIndex += 1;
      } else if (parseIndex < (4 + 12 + 16 + 12 - 1 )) {
        serverIV[parseIndex - 4 - 12 - 16] = c;
        parseIndex += 1;
      } else {
        // Last byte
        serverIV[parseIndex - 4 - 12 - 16] = c;
        parseIndex = 0;
        gcm.setIV(serverIV, 12);
        gcm.addAuthData(serverRandoms, 12);
        if (gcm.checkTag(serverSignature, 16)) {
          Serial.println(F("\nValid signature!!!"));
          temperCount = 0;
          for (int i = 0; i < 12; i++) {
            iv[i] = serverRandoms[i];
          }
        } else {
          Serial.println(F("\nRandoms have been tempered!"));
          // Postpone next connection
          temperCount += 1;
          lastConnectionTime += temperCount * postingInterval;
        }
      }
    }
  }

  if (lastConnectionTime > (20L * 24L * 60L * 60L * 1000L) && 
      millis() < (lastConnectionTime - (20L * 24L * 60L * 60L * 1000L))) {
    // reset on overflow (20 days of difference)
    lastConnectionTime = 0;
  }

  if (millis() > lastConnectionTime && millis() - lastConnectionTime > postingInterval) {
    httpRequest();
  }
}

// this method makes a HTTP connection to the server:
void httpRequest() {
  // close any connection before send a new request.
  // This will free the socket on the WiFi shield
  client.stop();

  // if there's a successful connection:
  if (client.connect(server, port)) {

    byte plaintext[16] = {
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    sensorValue1 = analogRead(sensorPin1);
    sensorValue2 = analogRead(sensorPin2);

    sensors.requestTemperatures(); // Send the command to get temperatures
    temperature = sensors.getTempCByIndex(0);

    // Check if reading was successful
    if(temperature != DEVICE_DISCONNECTED_C) {
      temperatureValue = round(temperature * 10);
    } 
    else {
      temperatureValue = 0;
    }

    // max voltage 0.55V
    lightValue = map(analogRead(lightPin), 0, 113, 0, 1024);
    
    byte low1 = sensorValue1;
    byte high1 = sensorValue1 >> 8;
    plaintext[0] = high1;
    plaintext[1] = low1;

    byte low2 = sensorValue2;
    byte high2 = sensorValue2 >> 8;
    plaintext[2] = high2;
    plaintext[3] = low2;

    byte low3 = temperatureValue;
    byte high3 = temperatureValue >> 8;
    plaintext[4] = high3;
    plaintext[5] = low3;

    byte low4 = lightValue;
    byte high4 = lightValue >> 8;
    plaintext[6] = high4;
    plaintext[7] = low4;

    byte ciphertext[16];

    gcm.setIV(iv, 12);
    gcm.encrypt(ciphertext, plaintext, 16);
    byte tag[16];
    gcm.computeTag(tag, 16);

    Serial.println(F("\nconnecting..."));
    // send the HTTP PUT request:
    client.println(F("POST /push HTTP/1.1"));
    client.print(F("HOST: "));
    client.println(server);
    client.println(F("User-Agent: ArduinoWiFi/1.1"));
    client.println(F("Connection: close"));
    client.println(F("Content-Type: application/octet-stream"));
    client.print(F("Content-Length: "));
    client.println(44);
    client.println();    
    for (int i = 0; i < 16; i++) {
      client.write(ciphertext[i]);
    }
    for (int i = 0; i < 16; i++) {
      client.write(tag[i]);
    }
    for (int i = 0; i < 12; i++) {
      client.write(iv[i]);
    }
    client.println();
    client.println();

    // note the time that the connection was made:
    lastConnectionTime = millis();
  } else {
    // if you couldn't make a connection:
    Serial.println(F("connection failed"));
  }
}

void printWifiStatus() {
  // print the SSID of the network you're attached to:
  Serial.print(F("SSID: "));
  Serial.println(WiFi.SSID());

  // print your WiFi shield's IP address:
  IPAddress ip = WiFi.localIP();
  Serial.print(F("IP Address: "));
  Serial.println(ip);

  // print the received signal strength:
  long rssi = WiFi.RSSI();
  Serial.print(F("signal strength (RSSI):"));
  Serial.print(rssi);
  Serial.println(F(" dBm"));
}
