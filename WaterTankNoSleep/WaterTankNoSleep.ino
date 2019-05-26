/*
  Water tank monitor
*/
//=========================================================================
// WiFi
#include <ESP8266WiFi.h>
#include "credentials.h"
//const char* ssid     = "xxxx";
//const char* password = "xxxx";
//=========================================================================
// OTA
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
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
time_t prevDisplay = 0; // when the digital clock was displayed
const uint32_t CLOCK_UPDATE_RATE = 1000;
uint32_t clockTimer = 0;       // time for next clock update
//================================================================
// Remote debugging
#include "RemoteDebug.h"
RemoteDebug Debug;
//================================================================
const int analogSamples = 50;
String mDepth = "";
int lastMinute = -1;
int neonPushInterval = 5;      // 'n' minutes push rate

void setup(void) {

  Serial.begin(115200);

  //================================================
  // Init WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  Serial.print("Connecting.");
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print(".");
  }
  Serial.print("\nConnected to: ");
  Serial.println(ssid);
  Serial.println(WiFi.localIP());
  //================================================
  // Init remote debugging
  Debug.setResetCmdEnabled(true);
  Debug.setSerialEnabled(true);
  Debug.begin("Telnet_WaterTank");
  //================================================
  // Init OTA
  // Port defaults to 8266
  ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname("water_tank");

  ArduinoOTA.onStart([]() {
    //
  });
  ArduinoOTA.onEnd([]() {
    //
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    //
  });
  ArduinoOTA.onError([](ota_error_t error) {
    //
  });
  ArduinoOTA.begin();
  //================================================
  // Init NTP and time
  setSyncProvider(getNtpTime);
  setSyncInterval(3600);
}

void loop() {
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
  DEBUG("Volts: %.2f,  Depth: %s m\n", volts, mDepth.c_str());

  int m = minute();
  if ((m % neonPushInterval) == 0)
  {
    if (m != lastMinute)
    {
      lastMinute = m;
      pushToNeon();
    }
  }

  delay(500);
  Debug.handle();
  ArduinoOTA.handle();
  yield();
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
  // SSL connection
  //RestClient client = RestClient(neonURL, httpsPort, fingerprint);
  //RestClient client = RestClient(neonURL, httpsPort, 1);
  RestClient client = RestClient(neonURL, proxyAddr, proxyPort);  ;
  client.setContentType(contentType);
  char sessionHeader[70];
  int httpStatus = getSessionToken(client, sessionHeader);
  if (httpStatus == 200)
  {
    httpStatus = pushData(client, sessionHeader);
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
  DEBUG("#GetSessionToken status code = %d\n", statusCode);
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
  DEBUG("#ImportData status code = %d\n", statusCode);
  return statusCode;
}
