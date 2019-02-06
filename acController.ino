#include <FS.h>                   //this needs to be first, or it all crashes and burns...

#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino

//needed for library
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager
#include <ESP8266mDNS.h>
#include <ArduinoJson.h>          //Version 5 of https://github.com/bblanchon/ArduinoJson

#include <time.h>


#define BLUE_LED_PIN     2 // Blue LED.
#define RELAY_PIN        4 // Relay control.
#define OPTOCOUPLER_PIN  5 // Optocoupler input.

#define RELAY_TURN_OFF   LOW
#define RELAY_TURN_ON    HIGH

#define AP_SSID               "ACRELAY_AP"

ESP8266WebServer server(80);

WiFiServer telnet(23);
WiFiClient telnetClients;
int disconnectedClient = 1;


int enableTime = 0;
time_t now; 

#define DEBUG_LOG_LN(x) { Serial.println(x); if(telnetClients) { telnetClients.println(x); } }
#define DEBUG_LOG(x) { Serial.print(x); if(telnetClients) { telnetClients.print(x); } }

#define DEBUG_LOG_INFO_LN(x) { if(enableTime) { now = time(nullptr); DEBUG_LOG(now); DEBUG_LOG(":"); } DEBUG_LOG(__LINE__); DEBUG_LOG(":"); DEBUG_LOG_LN(x); }
#define DEBUG_LOG_INFO(x) { if(enableTime) { now = time(nullptr); DEBUG_LOG(now); DEBUG_LOG(":"); } DEBUG_LOG(__LINE__); DEBUG_LOG(":"); DEBUG_LOG(x); }


//flag for saving data
bool shouldSaveConfig = false;

//callback notifying us of the need to save config
void saveConfigCallback () {
  DEBUG_LOG_INFO_LN("Should save config");
  shouldSaveConfig = true;
}


void configModeCallback (WiFiManager *myWiFiManager) {
}


void handleRoot() {
  server.send(200, "text/plain", "hello from esp8266!");
}

void handleDisconnect(){
  server.send(200, "text/plain", "Done");
  DEBUG_LOG_INFO_LN("Disconnecting");
  WiFi.disconnect();
  DEBUG_LOG_INFO_LN("Restarting");
  ESP.restart();
}


void handleNotFound(){
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET)?"GET":"POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i=0; i<server.args(); i++){
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
}

void handleOn() {
  // should check time here
  digitalWrite(RELAY_PIN, RELAY_TURN_ON);
  server.send(200, "text/plain", "On");  
}


void handleOff() {
  digitalWrite(RELAY_PIN, RELAY_TURN_OFF);
  server.send(200, "text/plain", "Off");  
}


void setup() {
  Serial.begin(115200);
  
  DEBUG_LOG_INFO_LN();

  //clean FS, for testing
  //SPIFFS.format();

  //read configuration from FS json
  DEBUG_LOG_INFO_LN("mounting FS...");

  // Need to set the SPIFFS size in arduino ide for this to work
  if (SPIFFS.begin()) {
    DEBUG_LOG_INFO_LN("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      DEBUG_LOG_INFO_LN("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        DEBUG_LOG_INFO_LN("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
          DEBUG_LOG_INFO_LN("parsed json");

          // here we read stuff from json if any
        } else {
          DEBUG_LOG_INFO_LN("failed to load json config");
        }
      }
    }
  } else {
    DEBUG_LOG_INFO_LN("failed to mount FS");
  }
  //end read

  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;

  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  wifiManager.setAPCallback(configModeCallback);  

  //reset settings - for testing
  //wifiManager.resetSettings();

  //set minimu quality of signal so it ignores AP's under that quality
  //defaults to 8%
  //wifiManager.setMinimumSignalQuality();
  
  //sets timeout until configuration portal gets turned off
  //useful to make it all retry or go to sleep
  //in seconds
  //wifiManager.setTimeout(120);

  //fetches ssid and pass and tries to connect
  //if it does not connect it starts an access point with the specified name
  //here  "AutoConnectAP"
  //and goes into a blocking loop awaiting configuration
  if (!wifiManager.autoConnect(AP_SSID)) {
    DEBUG_LOG_INFO_LN("failed to connect and hit timeout");
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(5000);
  }

  //if you get here you have connected to the WiFi
  DEBUG_LOG_INFO_LN("connected...yeey :)");

  //save the custom parameters to FS
  if (shouldSaveConfig) {
    DEBUG_LOG_INFO_LN("saving config");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      DEBUG_LOG_INFO_LN("failed to open config file for writing");
    }

    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();
    //end save
  }

  pinMode(BLUE_LED_PIN, OUTPUT);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(OPTOCOUPLER_PIN, INPUT);  

  DEBUG_LOG_INFO_LN("local ip");
  DEBUG_LOG_INFO_LN(WiFi.localIP());

  if (MDNS.begin("acController")) {
    DEBUG_LOG_LN("MDNS responder started");
    
    MDNS.addService("http", "tcp", 80);
    
    MDNS.addService("telnet", "tcp", 23);
  }
  else {
    DEBUG_LOG_LN("MDNS responder failed");    
  }

  server.on("/", handleRoot);
  server.on("/disconnectWifi", handleDisconnect);
  server.on("/v1/off", handleOff);
  server.on("/v1/on", handleOn);
  
  server.onNotFound(handleNotFound);

  server.begin();
  DEBUG_LOG_LN("HTTP server started");  

  telnet.begin();
  telnet.setNoDelay(true);
  DEBUG_LOG_INFO_LN("Telnet server started");  

  configTime(0, 0, "2.es.pool.ntp.org", "1.europe.pool.ntp.org", "2.europe.pool.ntp.org");
  Serial.println("Waiting for time");
  DEBUG_LOG_INFO(" ");
  while (!time(nullptr)) {
    DEBUG_LOG(".");
    delay(1000);
  }
  DEBUG_LOG_LN("");
  now = time(nullptr);
  // This time is not always correct... but who cares.., later it becomes correct
  DEBUG_LOG_LN(ctime(&now));
  

  enableTime = 1;


  wifi_set_sleep_type(LIGHT_SLEEP_T);
  
  delay(2000);
  // WiFi.disconnect();
}





void loop() {
  uint8_t i;
  server.handleClient();
  time_t currentTime = time(nullptr);

  if (telnet.hasClient()) {
    if (!telnetClients || !telnetClients.connected()) {
      if (telnetClients) {
        telnetClients.stop();
        DEBUG_LOG_INFO_LN("Telnet Client Stop");
      }
      telnetClients = telnet.available();
      DEBUG_LOG_INFO_LN("New Telnet client");
      telnetClients.flush();  // clear input buffer, else you get strange characters 
      disconnectedClient = 0;
    }
  }
  else {
    if(!telnetClients.connected()) {
      if(disconnectedClient == 0) {
        DEBUG_LOG_INFO_LN("Client Not connected");
        telnetClients.stop();
        disconnectedClient = 1;
      }
    }
  }

  MDNS.update();

  // Wait between measurements.
  delay(500);
  
//  DEBUG_LOG_INFO("Memory: ");
//  DEBUG_LOG_LN(ESP.getFreeHeap());
  
}
