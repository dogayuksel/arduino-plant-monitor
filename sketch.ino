#include <SPI.h>
#include <WiFi.h>
#include <AES.h>
#include <GCM.h>
#include <RNG.h>
#include <TransistorNoiseSource.h>

char ssid[] = "Network_SSID";      //  your network SSID (name)
char pass[] = "Network_Password";   // your network password

GCM<AES128> gcm;
TransistorNoiseSource noise(A3);

byte key[32] = {
  0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
  0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF
};

int sensorPin1 = A0;
int sensorValue1 = 0;
int sensorPin2 = A1;
int sensorValue2 = 0;

int status = WL_IDLE_STATUS;
byte mac[6]; // the MAC address of your Wifi shield

// Initialize the Wifi client library
WiFiClient client;

int port = 3000; // exposed port on your server 
char server[] = "192.168.2.115"; // your server address

unsigned long lastConnectionTime = 0; // last time you connected to the server, in milliseconds
const unsigned long postingInterval = 30L * 60L * 1000L; // delay between updates, in milliseconds

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

  WiFi.macAddress(mac);
  RNG.begin("Arduino Plant Monitor 1.0");
  RNG.stir(mac, 6);
  RNG.addNoiseSource(noise);
}

void loop() {
  // if there's incoming data from the net connection.
  // send it out the serial port.  This is for debugging
  // purposes only:
  while (client.available()) {
    char c = client.read();
    Serial.write(c);
  }

  if (!RNG.available(12 * 2)) {
    RNG.loop();
  }

  // if ten seconds have passed since your last connection,
  // then connect again and send data:
  if (millis() - lastConnectionTime > postingInterval) {
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
    byte iv[12] = {
      0x00, 0x11, 0x22, 0x33, 0x44, 0x55,
      0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB
    };

    RNG.rand(iv, 12);
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
      Serial.println(char(ciphertext[i]));
      client.write(ciphertext[i]);
    }
    for (int i = 0; i < 16; i++) {
      Serial.println(char(tag[i]));
      client.write(tag[i]);
    }
    for (int i = 0; i < 12; i++) {
      Serial.println(char(iv[i]));
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
