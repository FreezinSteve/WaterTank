/*
  Water tank monitor with deep sleep
*/
//=========================================================================
// WiFi
#include <ESP8266WiFi.h>
#include "credentials.h"
//const char* ssid     = "xxxx";
//const char* password = "xxxx";
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
static const char ntpServerName[] = "time.nist.gov";
const int timeZone = 0;     // UTC
const unsigned int localPort = 8888;  // local port to listen for UDP packets
const int NTP_PACKET_SIZE = 48; // NTP time is in the first 48 bytes of message
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming & outgoing packets
//================================================================
const int analogSamples = 50;
String mDepth = "";
int wakeInterval = 900;     //15 minutes

void setup() {
  Serial.begin(115200);
  Serial.setTimeout(2000);

  // Wait for serial to initialize.
  while (!Serial) { }

  Serial.println("Device Started");
  Serial.println("-------------------------------------");
  Serial.println("Running Deep Sleep Firmware!");
  Serial.println("-------------------------------------");

  readDepth();

  connect();

  // Time is zero on startup
  time_t t;
  for (int retry = 0; retry < 3; retry++) {
    Serial.println("Contacting NTP server");
    t = getNtpTime();
    if (t != 0) {
      break;
    }
    Serial.println("Failed, retrying");
  }

  if (t == 0)
  {
    Serial.println("Failed to get time from NTP server");
  }
  else
  {
    setTime(t);
    Serial.print("Time from NTP server: ");
    Serial.println(getISO8601Time(false));
    // Test the minute so that we don't send on startup
    if ((minute() % (wakeInterval / 60)) == 0)
    {
      pushToNeon();
    }
    else
    {
      Serial.println("Not time to push data to Neon yet");
    }
  }
  // Calculate how many microseconds to wait until waking up on the interval
  int secIntoHour = minute() * 60 + second();
  int secDiff = secIntoHour  % wakeInterval;
  long sleepTime = (wakeInterval -  secDiff) * 1000000;
  Serial.print("Sleeping for ");
  Serial.print(sleepTime);
  Serial.println(" us");
  ESP.deepSleep(sleepTime);
}

void connect() {

  // Connect to Wifi.
  Serial.println();
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  // WiFi fix: https://github.com/esp8266/Arduino/issues/2186
  WiFi.persistent(false);
  WiFi.mode(WIFI_OFF);
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
      return;
    }
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.println();
  Serial.println("Connected!");
}

void readDepth() {

  long raw = 0;
  for (int i = 0; i < analogSamples; i++)
  {
    raw += analogRead(A0);
  }
  float volts = raw / analogSamples;

  // To scale to mm of water
  // 1 psi = 703.07mm
  // sensor = 0 psi = 500mV, 15psi = 4500mV (ignore overrange of sensor)
  // ESP8266 0-1024 = 0 to 3300mV
  // m = 8.497
  // c = 1318
  int depth = (int)(volts * 8.497  - 1318 );
  mDepth = String((float)depth / 1000);
  //  DEBUG("Volts: %.2f,  Depth: %s m\n", volts, mDepth.c_str());
  Serial.print("A0: ");
  Serial.print(volts);
  Serial.print("mV,  Depth: ");
  Serial.print(mDepth.c_str());
  Serial.println("m");

}

void loop() {
  //
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
  while (millis() - beginWait < 1500) {
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
void pushToNeon()
{

  RestClient client = RestClient(neonURL, proxyAddr, proxyPort);  ;
  client.setContentType(contentType);
  char sessionHeader[70];
  for (int retry = 0; retry < 3; retry++)
  {
    int httpStatus = getSessionToken(client, sessionHeader);
    if (httpStatus == 200)
    {
      httpStatus = pushData(client, sessionHeader);
      break;
    }
  }
}

int getSessionToken(RestClient &client, char* sessionHeader)
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

int pushData(RestClient &client, char* sessionHeader)
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
