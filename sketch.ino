#include <SPI.h>
#include <WiFi.h>
#include <AES.h>
#include <GCM.h>

#include <DallasTemperature.h>
#include <OneWire.h>

#include <BH1750.h>   
#include <Wire.h>


// Initialize the Wifi client library
WiFiClient client;

GCM<AES128> gcm;

char ssid[] = "{{NETWORK_NAME}}"; // your network SSID (name)
char pass[] = "{{NETWORK_PASS}}"; // your network password

char server[] = "{{SERVER}}"; // your server address
int port = {{PORT}}; // exposed port on your server 

byte key[32] = {
  {{ARDUINO_PASS}}
};
byte iv[12] = {
  {{ARDUINO_IV}}
};

int temperCount = 0;
int status = WL_IDLE_STATUS;

// last time you connected to the server, in milliseconds
unsigned long lastConnectionTime = 0; 
// delay between updates, in milliseconds
const unsigned long postingInterval = 30L * 60L * 1000L; 
const unsigned long twentyDayMillis = 20L * 24L * 60L * 60L * 1000L;

int moistureSensorControlPin = 3;

int moisturePin1 = A0;
int moisturePin2 = A1;
int moisturePin3 = A2;
int moisturePin4 = A3;

// Data wire is plugged into port 2 on the Arduino
#define ONE_WIRE_BUS 2

// Setup a oneWire instance to communicate with any OneWire devices
OneWire oneWire(ONE_WIRE_BUS);

// Pass our oneWire reference to Dallas Temperature. 
DallasTemperature temperatureSensors(&oneWire);

BH1750 lightMeter1(0x23);
BH1750 lightMeter2(0x5C);


void setup() {
  //Initialize serial and wait for port to open:
  Serial.begin(9600);
  while (!Serial) {
    // wait for serial port to connect. needed for native USB port only
    continue; 
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

  pinMode(moistureSensorControlPin, OUTPUT);

  Serial.println(F("Setup bemperature"));
  temperatureSensors.begin();

  Serial.println(F("Wire begin"));
  Wire.begin(); 

  Serial.println(F("Setup light"));
  lightMeter1.begin(BH1750::ONE_TIME_HIGH_RES_MODE, 0x23);
  lightMeter2.begin(BH1750::ONE_TIME_HIGH_RES_MODE, 0x5C);

  Serial.println(F("Setup finished"));
}

void loop() {
  if (client.available()) {
    parseResponse();
  }

  unsigned long timeNow = millis();

  // millis overflows after 50 days
  // we start checking after 20 days
  if (lastConnectionTime > twentyDayMillis && 
      timeNow < lastConnectionTime) {
    // reset on overflow
    lastConnectionTime = 0;
    return;
  }

  if (timeNow > lastConnectionTime &&
      timeNow - lastConnectionTime > postingInterval) {
    sendRequest(timeNow);
  }

  delay(1000);
}

// this method makes a HTTP connection to the server:
void sendRequest(unsigned long timeNow) {
  // close any connection before send a new request.
  // This will free the socket on the WiFi shield
  client.stop();

  // if there's a successful connection:
  if (client.connect(server, port)) {
    byte plaintext[16] = {
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };

    Serial.println(F("Enable moisture sensors"));
    digitalWrite(moistureSensorControlPin, HIGH); 
    delay(5000);

    int moistureValue1 = analogRead(moisturePin1);
    int moistureValue2 = analogRead(moisturePin2);
    int moistureValue3 = analogRead(moisturePin3);
    int moistureValue4 = analogRead(moisturePin4);

    Serial.println(F("Disable moisture sensors"));
    digitalWrite(moistureSensorControlPin, LOW); 

    temperatureSensors.requestTemperatures(); // Send the command to get temperatures
    float temperature1 = temperatureSensors.getTempCByIndex(0);
    float temperature2 = temperatureSensors.getTempCByIndex(1);

    // Check if reading was successful
    int temperatureValue1;
    if(temperature1 != DEVICE_DISCONNECTED_C) {
      temperatureValue1 = round(temperature1 * 10.0);
    } 
    else { temperatureValue1 = 0; }

    // Check if reading was successful
    int temperatureValue2;
    if(temperature2 != DEVICE_DISCONNECTED_C) {
      temperatureValue2 = round(temperature2 * 10.0);
    } 
    else { temperatureValue2 = 0; }

    float light1 = lightMeter1.readLightLevel();
    float light2 = lightMeter2.readLightLevel();

    int lightValue1 = round(map(light1, 0, 55000, 0, 1024));
    int lightValue2 = round(map(light2, 0, 55000, 0, 1024));

    writeValueToBufferAtOffset(moistureValue1, plaintext, 0);
    writeValueToBufferAtOffset(moistureValue2, plaintext, 2);
    writeValueToBufferAtOffset(moistureValue3, plaintext, 4);
    writeValueToBufferAtOffset(moistureValue4, plaintext, 6);
    writeValueToBufferAtOffset(temperatureValue1, plaintext, 8);
    writeValueToBufferAtOffset(temperatureValue2, plaintext, 10);
    writeValueToBufferAtOffset(lightValue1, plaintext, 12);
    writeValueToBufferAtOffset(lightValue2, plaintext, 14);
 
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
    lastConnectionTime = timeNow;
  } else {
    // if you couldn't make a connection:
    Serial.println(F("connection failed"));
    delay(1000);
  }
}

void writeValueToBufferAtOffset(int value, byte buffer[], int offset) {
  byte low4 = value;
  byte high4 = value >> 8;
  buffer[offset] = high4;
  buffer[offset + 1] = low4;
}

void parseResponse() {
  int parseIndex = 0;
  byte serverRandoms[12];
  byte serverSignature[16];
  byte serverIV[12];

  while (client.available()) {
    byte c = client.read();
    Serial.write(c);
    // payload starts after \n \r \n \r
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
      // Store payload
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
          Serial.println(F("\nValid signature!"));
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
