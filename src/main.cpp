// demo: CAN-BUS Shield, receive data with interrupt mode, and set mask and filter

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSserver.h>
#include <WiFiManager.h> //https://github.com/tzapu/WiFiManager
#define OTA_DEBUG
#include <ArduinoOTA.h>
#include <ESP8266mDNS.h>
#include <SoftwareSerial.h>
#include <JC_Button.h>          // https://github.com/JChristensen/JC_Button
// #define GEN_DEBUG
#include "ReturnZeroSignalHandler.h"
#include <ticker.h>

#include <TimeLib.h>
#include <NtpClientLib.h>
#define SHOW_TIME_PERIOD 5000
#define NTP_TIMEOUT 1500

int8_t timeZone = -5;
int8_t minutesTimeZone = 0;
const PROGMEM char *ntpServer = "pool.ntp.org";
boolean NTPsynced = false;


// SoftwareSerial toDisplayInterceptor(14,16); //rx,tx   - currenlty not used

#define FridgeBoardSerial Serial
#define debugTx Serial1 //GPIO2
#define logger (&debugTx)
#define OffOnDisplayPin 4
#define STACK_PROTECTOR 512                    // bytes
Button myBtn(0);       // CONFIG_PIN GPIO0 - define the button
RZ_Signal fromDisplayInterceptor(14, 12, 500, &debugTx); //in,out

// Indicates whether ESP has WiFi credentials saved from previous session
bool initialConfigNeeded = false;
const char *Hostname = "FridgeInterceptor";

#define TELNET_MAX_CLIENTS 2
WiFiServer server(23); // for telnet
WiFiClient serverClients[TELNET_MAX_CLIENTS];
#define TELNET_DISPLAY_TX_clientID 0 //telnet client ID for panel UART TX interceptor
#define TELNET_DISPLAY_RX_clientID 2 //telnet client ID for panel UART RX interceptor
#define TELNET_BOARD_clientID 1      //telnet client ID for board UART

void init_WiFiManager(const char *Hostname)
{
  pinMode(LED_BUILTIN, OUTPUT);
  //WiFi.disconnect(); // forget SSID for debugging
  WiFi.persistent(false);
  WiFi.disconnect(true);
  WiFi.begin("Channing", "kuti2kuti");
  WiFi.printDiag(debugTx); //Remove this line if you do not want to see WiFi password printed
  if (WiFi.SSID() == "")
  {
    debugTx.println("We haven't got any access point credentials, so get them now");
    initialConfigNeeded = true;
  }
  else
  {
    digitalWrite(LED_BUILTIN, HIGH); // Turn led off as we are not in configuration mode.
    WiFi.mode(WIFI_STA);             // Force to station mode because if device was switched off while in access point mode it will start up next time in access point mode.
    unsigned long startedAt = millis();
    debugTx.print("After waiting ");
    int connRes = WiFi.waitForConnectResult();
    float waited = (millis() - startedAt);
    debugTx.print(waited / 1000);
    debugTx.print(" secs in setup() connection result is ");
    debugTx.println(connRes);
  }

  if (WiFi.status() != WL_CONNECTED)
  {
    debugTx.println("failed to connect, finishing setup anyway");
  }
  else
  {
    debugTx.print("local ip: ");
    debugTx.println(WiFi.localIP());
  }
}

void init_ArduinoOTA(const char *Hostname)
{
  ArduinoOTA.setHostname(Hostname);

  ArduinoOTA.onStart([]() {
    fromDisplayInterceptor.disable(); //stop itnerrupting
    debugTx.println("Start OTA");
  });

  ArduinoOTA.onEnd([]() {
    debugTx.println("End OTA");
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    // debugTx.printf("Progress: %u%%\n", (progress / (total / 100)));
  });

  ArduinoOTA.onError([](ota_error_t error) {
    debugTx.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR)
      debugTx.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR)
      debugTx.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR)
      debugTx.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR)
      debugTx.println("Receive Failed");
    else if (error == OTA_END_ERROR)
      debugTx.println("End Failed");
    fromDisplayInterceptor.begin(); //resume
  });

  ArduinoOTA.begin();
}

void allServersPrint(String s)
{
  for (int i = 0; i < TELNET_MAX_CLIENTS; i++)
    if (serverClients[i].availableForWrite() > 0)
      serverClients[i].print(s);
}
void allServersPrintLn(String s = "") { allServersPrint(s + "\r\n"); }

void processSyncEvent (NTPSyncEvent_t ntpState) {
  if (ntpState < 0) {
    NTPsynced = false;
    debugTx.printf ("Time Sync error: %d\n", ntpState);
    if (ntpState == noResponse)
      debugTx.println ("NTP server not reachable");
    else if (ntpState == invalidAddress)
      debugTx.println ("Invalid NTP server address");
    else if (ntpState == errorSending)
      debugTx.println ("Error sending request");
    else if (ntpState == responseError)
      debugTx.println ("NTP response error");
  } else {
    if (ntpState == timeSyncd) {
      NTPsynced = true;
      debugTx.print ("Got NTP time: ");
      debugTx.println (NTP.getTimeDateString (NTP.getLastNTPSync ()));
    }
  }
}
boolean syncEventTriggered = false; // True if a time even has been triggered
NTPSyncEvent_t ntpState = noResponse; // Last triggered event
void setup()
{
  static WiFiEventHandler e1, e2, e3;
  
  pinMode(OffOnDisplayPin, OUTPUT);
  digitalWrite(OffOnDisplayPin, 0);

  FridgeBoardSerial.begin(9600);     //UART0: //Fridge Board UART Interface
  debugTx.begin(115200);             //UART1: Not used currenlty
  // toDisplayInterceptor.begin(500); //Rx used for interceptor listening, Tx used for  interceptor output

  Hostname += ESP.getChipId();
  init_WiFiManager(Hostname); //it writes on debugTx for debugging
  WiFi.hostname(Hostname);

  //start server telnet
  server.begin();
  server.setNoDelay(true);
  init_ArduinoOTA(Hostname);

  myBtn.begin();  
  // pinMode(A0, INPUT);  analog tempreture readout
  fromDisplayInterceptor.begin();
  fromDisplayInterceptor.startMirroring();


  NTP.onNTPSyncEvent ([](NTPSyncEvent_t event) {
        ntpState = event;
        syncEventTriggered = true;
        processSyncEvent(event);
    });
  NTP.setNTPTimeout (NTP_TIMEOUT);
  NTP.begin(ntpServer, timeZone, true, minutesTimeZone);
}

size_t handlingTelnetComm()
{
  //check if there are any new clients
  if (server.hasClient())
  {
    //find free/disconnected spot
    int i;
    for (i = 0; i < TELNET_MAX_CLIENTS; i++)
      if (!serverClients[i])
      { // equivalent to !serverClients[i].connected()
        serverClients[i] = server.available();
        logger->print("New client: index ");
        logger->print(i);
        break;
      }

    //no free/disconnected spot so reject
    if (i == TELNET_MAX_CLIENTS)
    {
      server.available().println("busy");
      // hints: server.available() is a WiFiClient with short-term scope
      // when out of scope, a WiFiClient will
      // - flush() - all data will be sent
      // - stop() - automatically too
      logger->printf("server is busy with %d active connections\n", TELNET_MAX_CLIENTS);
    }
  }

  // determine maximum output size "fair TCP use"
  // client.availableForWrite() returns 0 when !client.connected()
  size_t maxToTcp = 0;
  for (int i = 0; i < TELNET_MAX_CLIENTS; i++)
    if (serverClients[i])
    {
      size_t afw = serverClients[i].availableForWrite();
      if (afw)
      {
        if (!maxToTcp)
        {
          maxToTcp = afw;
        }
        else
        {
          maxToTcp = std::min(maxToTcp, afw);
        }
      }
      else
      {
        // warn but ignore congested clients
        logger->println("one client is congested");
      }
    }
  return maxToTcp;
}

void wifiConfigureOnEvent()
{

  //Local intialization. Once its business is done, there is no need to keep it around
  debugTx.println("Configuration portal requested.");
  WiFiManager wifiManager;
  digitalWrite(LED_BUILTIN, LOW);
  //sets timeout in seconds until configuration portal gets turned off.
  //If not specified device will remain in configuration mode until
  //switched off via webserver or device is restarted.
  //wifiManager.setConfigPortalTimeout(600);

  //it starts an access point
  //and goes into a blocking loop awaiting configuration
  if (!wifiManager.startConfigPortal(Hostname, NULL))
  {
    debugTx.println("Not connected to WiFi but continuing anyway.");
  }
  else
  {
    initialConfigNeeded = false;
    //if you get here you have connected to the WiFi
    initialConfigNeeded = false;
    debugTx.println("connected...yeey :)");
  }
  digitalWrite(LED_BUILTIN, HIGH); // Turn led off as we are not in configuration mode.
  ESP.reset();                     // This is a bit crude. For some unknown reason webserver can only be started once per boot up
  // so resetting the device allows to go back into config mode again when it reboots.
  delay(5000);
}

void handleTelnet_UART_listener_writer(Stream &serialPort, int TelnetClientID, size_t maxPackageLength, uint8_t sbuf[], size_t buf_length)
{

  /* push UART data to all connected telnet clients
        // if client.availableForWrite() was 0 (congested)
        // and increased since then,
         ensure write space is sufficient: */
  if (serverClients[TelnetClientID].availableForWrite() >= buf_length)
  {
    size_t tcp_sent = serverClients[TelnetClientID].write(sbuf, buf_length);
    // serverClients[TelnetClientID].println(buf_length);
    if (tcp_sent != maxPackageLength)
      logger->printf("len mismatch: serial-read:%zd tcp-write:%zd\n", buf_length, tcp_sent);
  }
  while (serverClients[TelnetClientID].available())
  {
    logger->print("TelnetTextAvailableForUARTwrite");
    serialPort.write(serverClients[TelnetClientID].read());
  }
}
 

void ICACHE_RAM_ATTR restartDisplay_ISR(){
  digitalWrite(OffOnDisplayPin, 0); //turn display back on
}

 
char globSeqBuffChar_[3000];
boolean globSeqBuff_[3000];
Ticker delayAction;
void copyTelnnetToSequencer(int TelnetClientID){
  if (serverClients[TelnetClientID].available()>500){
    logger->print("TelnetTextAvailableForSequencing ");    
    int size=serverClients[TelnetClientID].readBytes(globSeqBuffChar_, 3000);
    for (int i=0;i<size;i++){
      if(globSeqBuffChar_[i]=='0')
        globSeqBuff_[i]=false;
      else if(globSeqBuffChar_[i]=='1')
        globSeqBuff_[i]=true;
    }
    logger->print("TelnetTextLength ");logger->print(size);
    digitalWrite(OffOnDisplayPin, 1); //turn off display
    fromDisplayInterceptor.writeSequence(globSeqBuff_, size);  
    delayAction.once_scheduled(10, restartDisplay_ISR);
    logger->print("\nTelnetSequencingDone\n");
  }
}


boolean sequence16_6[] = {0,1,0,1,1,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,1,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,1,1,0,1,0,1,1,1,0,1,1,1,0,1,0,1,1,1,0,1,1,1,0,1,0,1,0,1,1,1,1,1,0,1,1,1,0,1,0,1,1,1,0,1,0,1,1,1,0,1,0,1,1,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,1,1,0,1,1,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,1,1,0,1,0,1,1,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,1,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,1,1,0,1,0,1,1,1,0,1,1,1,0,1,0,1,1,1,0,1,1,1,0,1,0,1,0,1,1,1,1,1,0,1,1,1,0,1,0,1,1,1,0,1,0,1,1,1,0,1,0,1,1,1,1,1,0,1,1,1,1,1,1,0,0,1,1,1,1,1,0,1,1,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,1,1,0,1,0,1,1,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,1,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,1,1,0,1,0,1,1,1,0,1,1,1,0,1,0,1,1,1,0,1,1,1,0,1,0,1,0,1,1,1,1,1,0,1,1,1,0,1,0,1,1,1,0,1,0,1,1,1,0,1,0,1,1,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,1,1,0,1,1,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,1,1,0,1,0,1,1,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,1,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,1,1,0,1,0,1,1,1,0,1,1,1,0,1,0,1,1,1,0,1,1,1,0,1,0,1,0,1,1,1,1,1,0,1,1,1,0,1,0,1,1,1,0,1,0,1,1,1,0,1,0,1,1,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,1,1,0,1,1,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,1,1,0,1,0,1,1,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,1,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,1,1,0,1,0,1,1,1,0,1,1,1,0,1,0,1,1,1,0,1,1,1,0,1,0,1,0,1,1,1,1,1,0,1,1,1,0,1,0,1,1,1,0,1,0,1,1,1,0,1,0,1,1,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,1,1,0,1,1,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,1,1,0,1,0,1,1,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0};
boolean sequence21_1[] = {1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,1,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,1,1,0,1,0,1,1,1,0,1,0,1,1,1,0,1,1,1,0,1,0,1,0,1,0,1,1,1,1,1,0,1,1,1,0,1,0,1,1,1,0,1,0,1,1,1,0,1,0,1,1,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,1,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,1,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,1,1,0,1,0,1,1,1,0,1,0,1,1,1,0,1,1,1,0,1,0,1,0,1,0,1,1,1,1,1,0,1,1,1,0,1,0,1,1,1,0,1,0,1,1,1,0,1,0,1,1,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,1,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,1,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,1,1,0,1,0,1,1,1,0,1,0,1,1,1,0,1,1,1,0,1,0,1,0,1,0,1,1,1,1,1,0,1,1,1,0,1,0,1,1,1,0,1,0,1,1,1,0,1,0,1,1,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,1,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,1,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,1,1,0,1,0,1,1,1,0,1,0,1,1,1,0,1,1,1,0,1,0,1,0,1,0,1,1,1,1,1,0,1,1,1,0,1,0,1,1,1,0,1,0,1,1,1,0,1,0,1,1,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,1,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,1,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,1,1,0,1,0,1,1,1,0,1,0,1,1,1,0,1,1,1,0,1,0,1,0,1,0,1,1,1,1,1,0,1,1,1,0,1,0,1,1,1,0,1,0,1,1,1,0,1,0,1,1,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,1,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,1,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,1,1,0,1,0,1,1,1,0,1,0,1,1,1,0,1,1,1,0,1,0,1,0,1,0,1,1,1,1,1,0,1,1,1,0,1,0,1,1,1,0,1,0,1,1,1,0,1,0,1,1,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,1,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0};
  
void loop()
{
  size_t len;
  String refr_temp;
  size_t maxToTcp;
  static int hour_prev;
    
  static int i = 0;
  static int previousMillis = 0;

  ArduinoOTA.handle();
  maxToTcp = handlingTelnetComm();


  len = std::min((size_t)STACK_PROTECTOR, maxToTcp);
  // //check UART for data
  // uint8_t sbuf[len];
  // size_t serial_got;
  // if (fromDisplayInterceptor.available()>frameSize) {      // If anything comes on serial RX
  //   // len = std::min(len, (size_t) frameSize);
  //   len = frameSize;
  //   serial_got=fromDisplayInterceptor.readBytes(sbuf, len);
  //   //fromDisplayInterceptor.write(sbuf,serial_got);   // read it and send it out on serial TX

  //   for(size_t i=0; i<serial_got; i++)
  //     sbuf[i]=sbuf[i]^0x55;

  //   handleTelnet_UART_listener_writer(fromDisplayInterceptor, TELNET_DISPLAY_TX_clientID, maxToTcp, sbuf, serial_got);
  // }

  copyTelnnetToSequencer(TELNET_DISPLAY_TX_clientID);

  // if (FridgeBoardSerial.available())
  // { // If anything comes on board serial
  //   serial_got = FridgeBoardSerial.readBytes(sbuf, len);
  //   handleTelnet_UART_listener_writer(FridgeBoardSerial, TELNET_BOARD_clientID, maxToTcp, sbuf, serial_got);
  // }
  // if (toDisplayInterceptor.available()) {      // If anything comes on serial RX
  //   serial_got=toDisplayInterceptor.readBytes(sbuf, len);
  //   handleTelnet_UART_listener_writer(fromDisplayInterceptor, TELNET_DISPLAY_RX_clientID, maxToTcp, sbuf, serial_got);
  // }
  yield();  
  myBtn.read(); 
  if (myBtn.pressedFor(1000) || initialConfigNeeded)
  { //if CONFIG_PIN pulled to ground or wifi was not setup yet
    debugTx.println("wifi configuration");
    wifiConfigureOnEvent();
  }else if(myBtn.wasReleased()){
    debugTx.println("short_press");
    // debugTx.println((int) (*(&sequence + 1) - sequence));
    // fromDisplayInterceptor.writeSequence(sequence, (int) (*(&sequence + 1) - sequence));
  }



  if (ntpState == timeSyncd && ((millis () - previousMillis) > SHOW_TIME_PERIOD))  {
    //Serial.println(millis() - last);
    debugTx.println ( hour()); 
    previousMillis = millis ();
    debugTx.print (i); Serial.print (" ");
    debugTx.print (NTP.getTimeDateString ()); debugTx.print (" ");
    debugTx.print (NTP.isSummerTime () ? "Summer Time. " : "Winter Time. ");
    debugTx.print ("WiFi is ");
    debugTx.print (WiFi.isConnected () ? "connected" : "not connected"); debugTx.print (". ");
    debugTx.print ("Uptime: ");
    debugTx.print (NTP.getUptimeString ()); debugTx.print (" since ");
    debugTx.println (NTP.getTimeDateString (NTP.getFirstSync ()).c_str ());
    debugTx.printf ("Free heap: %u\n", ESP.getFreeHeap ());
    allServersPrintLn(NTP.getTimeDateString ());
    i++;
  }
  delay (0);

  if (hour()==2 && hour_prev==1){
    digitalWrite(OffOnDisplayPin, 1); //turn off display
    fromDisplayInterceptor.writeSequence(sequence21_1, (int) (*(&sequence21_1 + 1) - sequence21_1));
    delayAction.once_scheduled(5, restartDisplay_ISR);
    allServersPrintLn("Set temperature to 1C and -21C");
  }else if (hour()==7 && hour_prev==6){
    digitalWrite(OffOnDisplayPin, 1); //turn off display
    fromDisplayInterceptor.writeSequence(sequence16_6, (int) (*(&sequence16_6 + 1) - sequence16_6));
    delayAction.once_scheduled(5, restartDisplay_ISR);
    allServersPrintLn("Set temperature to 6C and -16C");
  }
  hour_prev = hour();

  int volatile a = 5; //analogRead(A0);
  refr_temp = String(a / 850 / 1024 / 2200 * (2200 + 5000) * 50 - 130);
  // debugTx.print(refr_temp);
  //allServersPrintLn(refr_temp);
}
/*********************************************************************************************************
  END FILE
*********************************************************************************************************/  