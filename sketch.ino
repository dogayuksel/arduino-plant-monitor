#include <SPI.h>
#include <WiFi.h>
#include <AES.h>
#include <GCM.h>

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
    Serial.println("WiFi shield not present");
    // don't continue:
    while (true);
  }

  String fv = WiFi.firmwareVersion();
  if (fv != "1.1.0") {
    Serial.println("Please upgrade the firmware");
  }

  // attempt to connect to Wifi network:
  while (status != WL_CONNECTED) {
    Serial.print("Attempting to connect to SSID: ");
    Serial.println(ssid);
    // Connect to WPA/WPA2 network. Change this line if using open or WEP network:
    status = WiFi.begin(ssid, pass);

    // wait 10 seconds for connection:
    delay(10000);
  }
  // you're connected now, so print out the status:
  printWifiStatus();
  
  gcm.setKey(key, 16);
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
          Serial.println("\nValid signature!!!");
          temperCount = 0;
          for (int i = 0; i < 12; i++) {
            iv[i] = serverRandoms[i];
          }
        } else {
          Serial.println("\nRandoms have been tempered!");
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
    
    byte low1 = sensorValue1;
    byte high1 = sensorValue1 >> 8;
    plaintext[0] = high1;
    plaintext[1] = low1;

    byte low2 = sensorValue2;
    byte high2 = sensorValue2 >> 8;
    plaintext[2] = high2;
    plaintext[3] = low2;

    byte ciphertext[16];

    gcm.setIV(iv, 12);
    gcm.encrypt(ciphertext, plaintext, 16);
    byte tag[16];
    gcm.computeTag(tag, 16);

    Serial.println("\nconnecting...");
    // send the HTTP PUT request:
    client.println("POST /push HTTP/1.1");
    client.print("HOST: ");
    client.println(server);
    client.println("User-Agent: ArduinoWiFi/1.1");
    client.println("Connection: close");
    client.println("Content-Type: application/octet-stream");
    client.print("Content-Length: ");
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
    Serial.println("connection failed");
  }
}

void printWifiStatus() {
  // print the SSID of the network you're attached to:
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());

  // print your WiFi shield's IP address:
  IPAddress ip = WiFi.localIP();
  Serial.print("IP Address: ");
  Serial.println(ip);

  // print the received signal strength:
  long rssi = WiFi.RSSI();
  Serial.print("signal strength (RSSI):");
  Serial.print(rssi);
  Serial.println(" dBm");
}
