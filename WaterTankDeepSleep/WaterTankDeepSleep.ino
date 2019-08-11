/*
  Water tank monitor with deep sleep
*/
//=========================================================================
// WiFi
#include <ESP8266WiFi.h>
#include "credentials.h"
//const char* ssid     = "xxxx";
//const char* password = "xxxx";
IPAddress staticIP(192, 168, 1, 70);
IPAddress gateway(192, 168, 1, 250);
IPAddress subnet(255, 255, 255, 0);
IPAddress dns(8, 8, 8, 8);  //DNS
//================================================================
// Neon REST Service
#include "RestClient.h"
#include <ArduinoJson.h>
const char* proxyAddr = "192.168.1.130";
int proxyPort = 9000;
const int httpsPort = 443;
const char* neonURL = "restservice-neon.niwa.co.nz";
//Included in "credentials.h" which is not included in the GIT repository
//const char* neonUser = "xxxxxx";
//const char* neonPassword = "xxxxx";
const char* contentType = "application/json";
const char* importDataPath = "/NeonRESTService.svc/ImportData/4624?LoggerType=1";
const char* getSessionPath = "/NeonRESTService.svc/PostSession";
//================================================================
// NTP time synch
#include <WiFiUdp.h>
#include <TimeLib.h>
#include <ESP8266WiFi.h>
static const char ntpServerName[] = "192.168.1.130";
const int timeZone = 0;     // UTC
const unsigned int localPort = 8888;  // local port to listen for UDP packets
const int NTP_PACKET_SIZE = 48; // NTP time is in the first 48 bytes of message
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming & outgoing packets
//================================================================
const int analogSamples = 50;
String mDepth = "";
int wakeInterval = 900;     //15 minutes

int FLASH_START = 1;
int FLASH_CONNECTED = 2;
int FLASH_NTP = 3;
int FLASH_NEON = 4;

//================================================================

void setup() {
  Serial.begin(115200);
  Serial.setTimeout(2000);

  // Wait for serial to initialize.
  while (!Serial) { }

  Serial.println("Device Started");
  Serial.println("-------------------------------------");
  Serial.println("Running Deep Sleep Firmware!");
  Serial.println("-------------------------------------");

  pinMode(D1, OUTPUT);
  pinMode(D2, OUTPUT);

  flashStatus(FLASH_START);

  readDepth();

  delay(1000);


  int secIntoHour = 0;
  int secDiff = 0;

  if (!connect())
  {
    flashExit();
    Serial.println("No Wifi, sleeping for 5 minutes");
    ESP.deepSleep(300 * 1000000);
    return;
  }
  else
  {
    flashStatus(FLASH_CONNECTED);
    // Time is zero on startup
    time_t t;
    for (int retry = 0; retry < 5; retry++) {
      Serial.println("Contacting NTP server");
      t = getNtpTime();
      if (t != 0) {
        break;
      }
      Serial.println("Failed, retrying");
    }

    if (t == 0)
    {
      Serial.println("No NTP, sleeping for 5 minutes");
      ESP.deepSleep(300 * 1000000);
      flashExit();
      return;
    }
    else
    {
      setTime(t);
      Serial.print("Time from NTP server: ");
      Serial.println(getISO8601Time(false));
      flashStatus(FLASH_NTP);
      // Test the minute so that we don't send on startup
      secIntoHour = minute() * 60 + second();
      secDiff = secIntoHour  % wakeInterval;
      if (secDiff > (wakeInterval - 5) || secDiff < 30)
      {
        if (pushToNeon())
        {
          flashStatus(FLASH_NEON);
        }
        else
        {
          flashExit();
        }
      }
      else
      {
        delay(1000);
        flashExit();
        Serial.println("Not time to push data to Neon yet");
      }
    }
  }
  Serial.println(getISO8601Time(false));
  // Calculate how many microseconds to wait until waking up on the interval
  secIntoHour = minute() * 60 + second();
  secDiff = secIntoHour  % wakeInterval;
  long sleepTime = (wakeInterval -  secDiff) * 1000000;
  Serial.print("Sleeping for ");
  Serial.print(sleepTime);
  Serial.println(" us");
  ESP.deepSleep(sleepTime);
}

bool connect() {

  // Connect to Wifi.
  Serial.println();
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  unsigned long wifiConnectStart = millis();

  while (WiFi.status() != WL_CONNECTED) {
    // Check to see if
    if (WiFi.status() == WL_CONNECT_FAILED) {
      Serial.println("Failed to connect to WiFi. Please verify credentials: ");
      delay(10000);
    }

    delay(500);
    Serial.println("...");
    // Only try for 5 seconds.
    if (millis() - wifiConnectStart > 15000) {
      Serial.println("Failed to connect to WiFi");
      return false;
    }
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.println();
  Serial.println("Connected!");
  Serial.printf("RSSI: %d dBm\n", WiFi.RSSI());
  return true;
}

void readDepth() {

  Serial.println("Power up sensor");
  digitalWrite(D1, HIGH);     // D1 = GPIO5, ON should turn on 5V regulator
  delay(1000);                // Wait for sensor to stabilise
  Serial.println("Read sensor");
  long raw = 0;
  for (int i = 0; i < analogSamples; i++)
  {
    raw += analogRead(A0);
  }
  int volts = raw / analogSamples;

  // To scale to mm of water
  // 4-20mA = 0-5000mm via 150R resistor
  // 600-3000mV = 0-5000mm
  // 186-931 bits
  int depth = map(volts, 186, 931, 0, 5000);
  depth = depth + 100;    // Empirical offset
  mDepth = String((float)depth / 1000);
  //  DEBUG("Volts: %.2f,  Depth: %s m\n", volts, mDepth.c_str());
  Serial.print("A0: ");
  Serial.print(volts);
  Serial.print("mV,  Depth: ");
  Serial.print(mDepth.c_str());
  Serial.println("m");
  Serial.println("Power off sensor");
  digitalWrite(D1, LOW);     // D1 = GPIO5, OFF should turn off 5V regulator

}


void loop() {
  Serial.println("Reached loop, error state, start deepsleep");
  ESP.deepSleep(300 * 1000000);
  return;
}

//==============================================================================
// NTP Methods
//==============================================================================
time_t getNtpTime()
{
  WiFiUDP Udp;
  Udp.begin(localPort);
  IPAddress ntpServerIP; // NTP server's ip address
  while (Udp.parsePacket() > 0) ; // discard any previously received packets
  // get a random server from the pool
  WiFi.hostByName(ntpServerName, ntpServerIP);
  sendNTPpacket(ntpServerIP, Udp);
  uint32_t beginWait = millis();
  while (millis() - beginWait < 2000) {
    int size = Udp.parsePacket();
    if (size >= NTP_PACKET_SIZE) {
      Udp.read(packetBuffer, NTP_PACKET_SIZE);  // read packet into the buffer
      unsigned long secsSince1900;
      // convert four bytes starting at location 40 to a long integer
      secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
      secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
      secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
      secsSince1900 |= (unsigned long)packetBuffer[43];
      return secsSince1900 - 2208988800UL + timeZone * SECS_PER_HOUR;
    }
  }
  return 0; // return 0 if unable to get the time
}

// send an NTP request to the time server at the given address
void sendNTPpacket(IPAddress & address, WiFiUDP &Udp )
{
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12] = 49;
  packetBuffer[13] = 0x4E;
  packetBuffer[14] = 49;
  packetBuffer[15] = 52;
  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  Udp.beginPacket(address, 123); //NTP requests are to port 123
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}

char* getTime(int offset)
{
  static char timeString[9];

  char buff[3];
  int hr = hour() + offset;
  if (hr >= 24)
  {
    hr -= 24;
  }
  int mi = minute();
  int se = second();

  itoa(hr, buff, 10);
  if (hr < 10)
  {
    strcpy(timeString, "0");
    strcat(timeString, buff);
  }
  else
  {
    strcpy(timeString, buff);
  }
  strcat(timeString, ":");

  itoa(mi, buff, 10);
  if (mi < 10)
  {
    strcat(timeString, "0");
  }
  strcat(timeString, buff);
  strcat(timeString, ":");

  itoa(se, buff, 10);
  if (se < 10)
  {
    strcat(timeString, "0");
  }
  strcat(timeString, buff);
  return timeString;
}

char* getISO8601Time(boolean zeroSeconds)
{
  static char timeString[20];

  char buff[5];
  int yr = year();
  int mo = month();
  int da = day();
  int hr = hour();
  int mi = minute();
  int se;
  if (zeroSeconds)
  {
    se = 0;
  }
  else
  {
    se = second();
  }

  itoa(yr, buff, 10);
  strcpy(timeString, buff);
  strcat(timeString, "-");
  itoa(mo, buff, 10);
  if (mo < 10)
  {
    strcat(timeString, "0");
  }
  strcat(timeString, buff);
  strcat(timeString, "-");
  itoa(da, buff, 10);
  if (da < 10)
  {
    strcat(timeString, "0");
  }
  strcat(timeString, buff);
  strcat(timeString, "T");
  itoa(hr, buff, 10);
  if (hr < 10)
  {
    strcat(timeString, "0");
  }
  strcat(timeString, buff);
  strcat(timeString, ":");
  itoa(mi, buff, 10);
  if (mi < 10)
  {
    strcat(timeString, "0");
  }
  strcat(timeString, buff);
  strcat(timeString, ":");
  itoa(se, buff, 10);
  if (se < 10)
  {
    strcat(timeString, "0");
  }
  strcat(timeString, buff);
  return timeString;
}

//=========================================================================
bool pushToNeon()
{
  RestClient client = RestClient(neonURL, proxyAddr, proxyPort);  ;
  client.setContentType(contentType);
  client.setTerminator('}');
  char sessionHeader[70];
  int httpStatus = 0;

  for (int retry = 0; retry < 3; retry++)
  {
    client.setTimeout(15000);
    httpStatus = getSessionToken(client, sessionHeader);
    if (httpStatus == 200)    {
      break;
    }
  }
  if (httpStatus != 200) {
    return false;
  }

  for (int retry = 0; retry < 3; retry++)
  {
    client.setTimeout(15000);
    httpStatus = pushData(client, sessionHeader);
    if (httpStatus == 200)
    {
      return true;
    }
  }
  return false;
}

int getSessionToken(RestClient & client, char* sessionHeader)
{
  DynamicJsonBuffer sendJsonBuffer;
  JsonObject& cred = sendJsonBuffer.createObject();
  cred["Username"] = neonUser;
  cred["Password"] = neonPassword;
  char json[100];
  cred.printTo(json);
  String response;
  int statusCode = client.post(getSessionPath, json, &response);
  if (statusCode == 200)
  {
    DynamicJsonBuffer recvJsonBuffer;
    JsonObject& root = recvJsonBuffer.parseObject(response);
    strcpy(sessionHeader, "X-Authentication-Token: ");
    strcat(sessionHeader, root.get<String>("Token").c_str());
    client.setHeader(sessionHeader);
  }
  //DEBUG("#GetSessionToken status code = %d\n", statusCode);
  Serial.print("#GetSessionToken status code = ");
  Serial.println(statusCode);

  return statusCode;
}

int pushData(RestClient & client, char* sessionHeader)
{
  DynamicJsonBuffer jsonBuffer;

  JsonObject& root = jsonBuffer.createObject();
  JsonArray& Data = root.createNestedArray("Data");

  JsonObject& item = Data.createNestedObject();
  item["SensorNumber"] = "0";
  item["ImportType"] = "0";

  JsonArray& itemSamples = item.createNestedArray("Samples");

  JsonObject& itemSample = itemSamples.createNestedObject();
  itemSample["Time"] = getISO8601Time(true);
  itemSample["Value"] = mDepth;

  char jsonData[1024];
  root.printTo((char*)jsonData, root.measureLength() + 1);

  int statusCode = client.post(importDataPath, (char*)jsonData);
  Serial.print("#ImportData status code = ");
  Serial.println(statusCode);
  return statusCode;
}


void flashStatus(int count)
{
  for (int i = 0; i < count; i++)
  {
    digitalWrite(D2, HIGH);
    delay(100);
    digitalWrite(D2, LOW);
    delay(200);
  }
}

void flashExit()
{
  digitalWrite(D2, HIGH);
  delay(1000);
  digitalWrite(D2, LOW);
}
