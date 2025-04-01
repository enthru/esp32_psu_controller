#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <WiFiManager.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebSrv.h>
#include <Adafruit_ADS1X15.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Arduino.h>
#include <U8g2lib.h>
#include <DallasTemperature.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <esp_task_wdt.h>

#define WDT_TIMEOUT 3

#define OLED_SDA 21
#define OLED_SCL 22
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, OLED_SCL, OLED_SDA);


Preferences preferences;
WiFiManager wifiManager;

bool menu = false;

Adafruit_ADS1115 ads;
float voltage = 0.0;
float current = 0.0;
float maxCurrent = 0.0;
float maxVoltage = 0.0;

const int sampleInterval = 10;
const int averagingTime = 250;
const int numSamples = averagingTime / sampleInterval;

unsigned long lastSampleTime = 0;
unsigned long startTime = 0;
long sumV = 0;
int sampleCount = 0;
unsigned long lastISampleTime = 0;
unsigned long startITime = 0;
long sumI = 0;
int sampleICount = 0;

int16_t adc0;
int16_t adc1;

bool powerEnabled;
bool defaultEnabled;
bool protectionEnabled;

String powerState = "unknown";
String displayState = "unknown";

#define POWER_SWITCH 18
#define TEMP1_SENSOR 32

#define YELLOW_LED 17
#define TRIANGLE_BUTTON 16
#define ON_BUTTON 5
#define OFF_BUTTON 4

bool displaystate = false;

//ACS785
const float SENSITIVITY = 0.04;
const float OFFSET_VOLT = 2.472;         //half of 5v real voltage
const float VOLT_PER_COUNT = 0.0001875;  //5v real divided by adc counts
float adcVoltage = 0.00;

const char *PARAM_INPUT_1 = "powerState";
const char *PARAM_INPUT_2 = "displayState";

//temp1
OneWire oneWire(TEMP1_SENSOR);
DallasTemperature sensors(&oneWire);
float temp1 = 0.0;
int zeroTemp;

AsyncWebServer server(80);
AsyncEventSource events("/events");

unsigned long webLastTime = 0;
unsigned long guardLastTime = 0;
unsigned long tempLastTime = 0;

unsigned int webTimerDelay;
unsigned int guardTimerDelay = 10;
unsigned int tempDelay = 1000;
//checkbox with powerbutton
String powerStateValue;
String displayStateValue;
//webserver update
String inputMessage;
String inputParam;

volatile bool onPressed = false;
volatile bool offPressed = false;
volatile bool trianglePressed = false;

void IRAM_ATTR handleOnPress() {
  onPressed = true;
}

void IRAM_ATTR handleOffPress() {
  offPressed = true;
}

void IRAM_ATTR handleTrianglePress() {
  trianglePressed = true;
}

void getSensorReadings() {
  adc0 = ads.readADC_SingleEnded(1);
  adc1 = ads.readADC_SingleEnded(0);
  voltage = adc0 * VOLT_PER_COUNT;
  current = adc1 * VOLT_PER_COUNT;
  //currentVoltage = adc1 * VOLT_PER_COUNT;
}

const char header_html[] PROGMEM = R"rawliteral(
  <!DOCTYPE HTML><html>
  <head>
    <title>PSU controller</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">     
    <link rel="stylesheet" href="https://use.fontawesome.com/releases/v5.7.2/css/all.css" integrity="sha384-fnmOCqbTlWIlj8LyTjo7mOUStjsKC4pOpQbqyi7RrhN7udi9RwhKkMHpvLbHG9Sr" crossorigin="anonymous">
    <link rel="icon" href="data:,">
    <style>
      html {font-family: Arial; display: inline-block; text-align: center;}
      p { font-size: 1.2rem;}
      body {  margin: 0;}
      .topnav { overflow: hidden; background-color: #50B8B4; color: white; font-size: 1rem; }
      .content { padding: 20px; }
      .card { background-color: white; box-shadow: 2px 2px 12px 1px rgba(140,140,140,.5); }
      .cards { max-width: 800px; margin: 0 auto; display: grid; grid-gap: 2rem; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); }
      .reading { font-size: 1.4rem; }
      h2 {font-size: 3.0rem;}
      .switch {position: relative; display: inline-block; width: 60px; height: 34px} 
      .switch input {display: none}
      .slider {position: absolute; top: 0; left: 0; right: 0; bottom: 0; background-color: #ccc; border-radius: 17px}
      .slider:before {position: absolute; content: ""; height: 26px; width: 26px; left: 4px; bottom: 4px; background-color: #fff; -webkit-transition: .4s; transition: .4s; border-radius: 34px}
      input:checked+.slider {background-color: #2196F3}
      input:checked+.slider:before {-webkit-transform: translateX(26px); -ms-transform: translateX(26px); transform: translateX(26px)}
    </style>
  </head>
)rawliteral";

String processor(const String &var) {
  if (var == "HEADER") {
    return String(header_html);
  }
  if (var == "VOLTAGE") {
    return String(voltage);
  } else if (var == "CURRENT") {
    return String(current);
  } else if (var == "TEMP1") {
    return String(temp1);
  } else if (var == "POWERBUTTON") {
    powerStateValue = powerState;
    return "<label class=\"switch\"><input type=\"checkbox\" onchange=\"togglePowerCheckbox(this)\" id=\"powerStateOutput\" " + powerStateValue + "><span class=\"slider\"></span></label>";
  } else if (var == "DISPLAYBUTTON") {
    displayStateValue = displayState;
    return "<label class=\"switch\"><input type=\"checkbox\" onchange=\"toggleDisplayCheckbox(this)\" id=\"displayStateOutput\" " + displayStateValue + "><span class=\"slider\"></span></label>";
  }
  return String();
}

String menu_processor(const String &var) {
  if (var == "HEADER") {
    return String(header_html);
  } else if (var == "MAXCURRENT") {
    return String(maxCurrent);
  } else if (var == "MAXVOLTAGE" ) {
    return String(maxVoltage);
  } else if (var == "DEFAULTENABLED") {
    return String(defaultEnabled);
  } else if (var == "WEBTIMERDELAY") {
    return String(webTimerDelay);
  }
  return String();
}

const char index_html[] PROGMEM = R"rawliteral(
  %HEADER%
  <body>
    <div class="topnav" id="topnava">
      <h1>Main screen</h1>
      <span id="headerinfo"></span>
    </div>
    <div class="content">
      <div class="cards">
        <div class="card">
          <p><i class="fas" style="color:#059e8a;"></i> Voltage</p><p><span class="reading"><span id="voltage">%voltage%</span> V</span></p>
        </div>
        <div class="card">
          <p><i class="fas" style="color:#059e8a;"></i> Output</p><p><span class="reading"></span></p>
          %POWERBUTTON%
        </div>
        <div class="card">
          <p><i class="fas" style="color:#059e8a;"></i> Display off</p><p><span class="reading"></span></p>
          %DISPLAYBUTTON%
        </div>
        <div class="card">
          <p><i class="fas" style="color:#059e8a;"></i> Current</p><p><span class="reading"><span id="current">%current%</span> A</span></p>
        </div>
        <div class="card">
          <p><i class="fas fa-thermometer-half" style="color:#059e8a;"></i> Temperature</p><p><span class="reading"><span id="temp1">%TEMP1%</span> C</span></p>
        </div>
      </div>
      <br><a href="/menu.html">Enter menu</a>
    </div>
  <script>
  if (!!window.EventSource) {
  var source = new EventSource('/events');
  
  source.addEventListener('open', function(e) {
    console.log("Events Connected");
  }, false);
  
  source.addEventListener('error', function(e) {
    if (e.target.readyState != EventSource.OPEN) {
      console.log("Events Disconnected");
    }
  }, false);
  
  source.addEventListener('message', function(e) {
    console.log("message", e.data);
  }, false);

  source.addEventListener('headerinfo', function(e) {
    console.log("headerinfo", e.data);
    document.getElementById("headerinfo").innerHTML = e.data;
  }, false);

  source.addEventListener('navcolor', function(e) {
    console.log("navcolor", e.data);
    document.getElementById("topnava").style.backgroundColor = e.data;
  }, false);

  source.addEventListener('current', function(e) {
    console.log("current", e.data);
    document.getElementById("current").innerHTML = e.data;
  }, false);

  source.addEventListener('voltage', function(e) {
    console.log("voltage", e.data);
    document.getElementById("voltage").innerHTML = e.data;
  }, false);

  source.addEventListener('temp1', function(e) {
    console.log("temp1", e.data);
    document.getElementById("temp1").innerHTML = e.data;
  }, false);
  }

  function togglePowerCheckbox(element) {
    var xhr = new XMLHttpRequest();
    if(element.checked){ xhr.open("GET", "/update?powerState=1", true); }
    else { xhr.open("GET", "/update?powerState=0", true); }
    xhr.send();
  }

  function toggleDisplayCheckbox(element) {
    var xhr = new XMLHttpRequest();
    if(element.checked){ xhr.open("GET", "/update?displayState=1", true); }
    else { xhr.open("GET", "/update?displayState=0", true); }
    xhr.send();
  }

  setInterval(function ( ) {
    var xhttp = new XMLHttpRequest();
    xhttp.onreadystatechange = function() {
      if (this.readyState == 4 && this.status == 200) {
        var inputChecked;
        if( this.responseText == 1){ 
          inputChecked = true;
          console.log("Power", "on");
        }
        else { 
          inputChecked = false;
          console.log("Power", "off");
        }
        document.getElementById("powerStateOutput").checked = inputChecked;
      }
    };
    xhttp.open("GET", "/powerState", true);
    xhttp.send();
  }, 500);

  setInterval(function ( ) {
    var xhttp = new XMLHttpRequest();
    xhttp.onreadystatechange = function() {
      if (this.readyState == 4 && this.status == 200) {
        var inputChecked;
        if( this.responseText == 1){ 
          inputChecked = true;
          console.log("Display", "on");
        }
        else { 
          inputChecked = false;
          console.log("Display", "off");
        }
        document.getElementById("displayStateOutput").checked = inputChecked;
      }
    };
    xhttp.open("GET", "/displayState", true);
    xhttp.send();
  }, 500);

  </script>
  </body>
  </html>)rawliteral";

const char menu_html[] PROGMEM = R"rawliteral(
  %HEADER%
  <body>
    <div class="topnav" id="topnava">
      <h1>Menu screen</h1>
    </div>
    <div class="content">
        <form action="/get">
          <p>Max current: <input type="text" name="maxCurrent" id="maxCurrent" value="%MAXCURRENT%"> <input type="submit" value="Submit"></p>
        </form>
        <form action="/get">
          <p>Max voltage: <input type="text" name="maxVoltage" id="maxVoltage" value="%MAXVOLTAGE%"> <input type="submit" value="Submit"></p>
        </form>
        <form action="/get">
          <p>Default enabled: <input type="text" name="defaultEnabled" id="defaultEnabled" value="%DEFAULTENABLED%"> <input type="submit" value="Submit"></p>
        </form>
        <p>Timers and delays in milliseconds: </p>
        <form action="/get">
          <p>Web page: <input type="text" name="webTimerDelay" id="webTimerDelay" value="%WEBTIMERDELAY%"> <input type="submit" value="Submit"></p>
        </form>
        <form action="/get">
          <p>Temp check: <input type="text" name="guardTempDelay" id="guardTempDelay" value="%GUARDTEMPDELAY%"> <input type="submit" value="Submit"></p>
        </form>
        <br><a href="/">Return to main screen</a>
    </div>
  <script>
    if (!!window.EventSource) {
      var source = new EventSource('/events');
      
      source.addEventListener('open', function(e) {
        console.log("Events Connected");
      }, false);
    
      source.addEventListener('navcolor', function(e) {
        console.log("navcolor", e.data);
        document.getElementById("topnava").style.backgroundColor = e.data;
      }, false);
    }
  </script>
  </body>
  </html>)rawliteral";

void setup() {
  pinMode(POWER_SWITCH, OUTPUT);
  digitalWrite(POWER_SWITCH, 0);
  Serial.begin(115200);
  u8g2.begin();
  u8g2.setPowerSave(0);

  pinMode(ON_BUTTON, INPUT_PULLUP);
  pinMode(OFF_BUTTON, INPUT_PULLUP);
  pinMode(TRIANGLE_BUTTON, INPUT_PULLUP);
  pinMode(YELLOW_LED, OUTPUT);
  digitalWrite(YELLOW_LED, LOW);

  attachInterrupt(digitalPinToInterrupt(ON_BUTTON), handleOnPress, FALLING);
  attachInterrupt(digitalPinToInterrupt(OFF_BUTTON), handleOffPress, FALLING);
  attachInterrupt(digitalPinToInterrupt(TRIANGLE_BUTTON), handleTrianglePress, FALLING);

  wifiManager.autoConnect("PSU_CONTROL");
  ArduinoOTA.setPort(3232);
  ArduinoOTA.setHostname("psu_controller");
  ArduinoOTA.setPassword("admin");

  preferences.begin("file", false);
  maxCurrent = preferences.getFloat("maxCurrent", 5.0);
  maxVoltage = preferences.getFloat("maxVoltage", 13.0);
  defaultEnabled = preferences.getBool("defaultEnabled", 0);
  powerEnabled = defaultEnabled;
  digitalWrite(POWER_SWITCH, powerEnabled);
  digitalWrite(YELLOW_LED, powerEnabled);
  protectionEnabled = preferences.getBool("protectionEnabled", 1);
  webTimerDelay = preferences.getInt("webTimerDelay", 1000);

  ArduinoOTA
    .onStart([]() {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH)
        type = "sketch";
      else  // U_SPIFFS
        type = "filesystem";

      Serial.println("Start updating " + type);
    })
    .onEnd([]() {
      Serial.println("\nEnd");
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    })
    .onError([](ota_error_t error) {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });

  ArduinoOTA.begin();
  sensors.begin();

  for (int i = 3; i > 0; i--) {
    if (!ads.begin()) {
      Serial.println("Failed to initialize ADS.");
      delay(1000);
    }
  }

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", index_html, processor);
  });

  server.on("/menu.html", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", menu_html, menu_processor);
  });

  server.on("/powerState", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", String(digitalRead(POWER_SWITCH)).c_str());
  });

  server.on("/displayState", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", String(displaystate).c_str());
  });

  server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam(PARAM_INPUT_1)) {
      inputMessage = request->getParam(PARAM_INPUT_1)->value();
      inputParam = PARAM_INPUT_1;
      digitalWrite(POWER_SWITCH, inputMessage.toInt());
      powerEnabled = !powerEnabled;
      digitalWrite(YELLOW_LED, powerEnabled);
    } else if (request->hasParam(PARAM_INPUT_2)) {
      inputMessage = request->getParam(PARAM_INPUT_2)->value();
      inputParam = PARAM_INPUT_2;
      displaystate = !displaystate;
      u8g2.setPowerSave(displaystate);
    }
    request->send(200, "text/plain", "OK");
  });

  server.on("/get", HTTP_GET, [](AsyncWebServerRequest *request) {
    String inputMessage;
    String inputParam;
    if (request->hasParam("maxCurrent")) {
      inputMessage = request->getParam("maxCurrent")->value();
      inputParam = "maxCurrent";
      maxCurrent = inputMessage.toFloat();
      preferences.begin("file", false);
      preferences.putFloat("maxCurrent", inputMessage.toFloat());
      preferences.end();
    }  
    else if (request->hasParam("maxVoltage")) {
      inputMessage = request->getParam("maxVoltage")->value();
      inputParam = "maxVoltage";
      maxVoltage = inputMessage.toFloat();
      preferences.begin("file", false);
      preferences.putFloat("maxVoltage", inputMessage.toFloat());
      preferences.end();
    }
    else if (request->hasParam("defaultEnabled")) {
      inputMessage = request->getParam("defaultEnabled")->value();
      inputParam = "defaultEnabled";
      preferences.begin("file", false);
      preferences.putBool("defaultEnabled", inputMessage.toInt());
      preferences.end();
    } else if (request->hasParam("webTimerDelay")) {
      inputMessage = request->getParam("webTimerDelay")->value();
      inputParam = "webTimerDelay";
      webTimerDelay = inputMessage.toInt();
      preferences.begin("file", false);
      preferences.putInt("webTimerDelay", inputMessage.toInt());
      preferences.end();
    } else {
      inputMessage = "No message sent";
      inputParam = "none";
    }

    Serial.println(inputMessage);
    request->send(200, "text/html", inputParam + " set with value: " + inputMessage + "<br><a href=\"/menu.html\">Return to menu</a>");
  });

  events.onConnect([](AsyncEventSourceClient *client) {
    if (client->lastId()) {
      Serial.printf("Client reconnected! Last message ID that it got is: %u\n", client->lastId());
    }
    client->send("hello!", NULL, millis(), 10000);
  });

  server.addHandler(&events);
  server.begin();
  preferences.end();
  startTime = millis();
  
  sensors.requestTemperatures();
  temp1 = sensors.getTempCByIndex(0);
}

void loop() {
  ArduinoOTA.handle();
  
  if (onPressed) {
    powerEnabled = true;
    onPressed = false;
    delay(50);
    digitalWrite(POWER_SWITCH, powerEnabled);
    digitalWrite(YELLOW_LED, powerEnabled);
  }
  if (offPressed) {
    powerEnabled = false;
    offPressed = false;
    delay(50);
    digitalWrite(POWER_SWITCH, powerEnabled);
    digitalWrite(YELLOW_LED, powerEnabled);
  }
  if (trianglePressed) {
    trianglePressed = false;
    delay(50);
    if (displaystate) {
      u8g2.setPowerSave(0);
      displaystate = false;
    } else {
      u8g2.setPowerSave(1);
      displaystate = true;
    }
  }

  unsigned long currentTime = millis();
  if (currentTime - lastSampleTime >= sampleInterval) {
    lastSampleTime = currentTime;
    sumI += ads.readADC_SingleEnded(1);
    sumV += ads.readADC_SingleEnded(0);
    sampleCount++;
  }
    
  if (currentTime - startTime >= averagingTime) {
    if (sampleCount > 0) {
      float averageV = sumV / (float)sampleCount;
      float averageI = sumI / (float)sampleCount;
      float iVoltagee = averageI*VOLT_PER_COUNT - 2.53;
      voltage = averageV*VOLT_PER_COUNT*7.948;
      current = iVoltagee/0.114;
      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_t0_12_tf);  
      u8g2.drawStr(1, 10, "Voltage:");
      u8g2.setFont(u8g2_font_t0_22_tf); 
      u8g2.drawStr(1, 27, String(voltage).c_str());
      u8g2.setFont(u8g2_font_t0_12_tf);
      u8g2.drawStr(1, 43, "Current:");
      u8g2.setFont(u8g2_font_t0_22_tf); 
      u8g2.drawStr(1, 60, String(current).c_str());
      u8g2.setFont(u8g2_font_t0_12_tf);
      u8g2.drawStr(72, 10, "Status:");
      u8g2.setFont(u8g2_font_t0_22_tf);
      if (digitalRead(POWER_SWITCH)) { u8g2.drawStr(75, 27, "ON"); } else { u8g2.drawStr(75, 27, "OFF"); }
      u8g2.setFont(u8g2_font_t0_12_tf);
      u8g2.drawStr(72, 43, "Temp:");
      u8g2.setFont(u8g2_font_t0_22_tf);
      u8g2.drawStr(72, 60, String(temp1).c_str());
      u8g2.sendBuffer();
    }
    sumV = 0;
    sumI = 0;
    sampleCount = 0;
    startTime = currentTime;
  }
  
  if ((millis() - tempLastTime) > tempDelay) {
    sensors.requestTemperatures();
    temp1 = sensors.getTempCByIndex(0);
    events.send(String(temp1).c_str(),"temp1",millis());
    tempLastTime = millis();
    //set pwm based on temp
    zeroTemp = (int)temp1 - 26;
    if (zeroTemp < 1) { zeroTemp = 1; } else if (zeroTemp > 35) { zeroTemp = 35; }
  }


  if ((millis() - guardLastTime) > guardTimerDelay) {
    if (((current > maxCurrent) || (voltage > maxVoltage)) && (protectionEnabled)) {
      powerEnabled = false;
      digitalWrite(POWER_SWITCH, powerEnabled);
      digitalWrite(YELLOW_LED, powerEnabled);
      powerEnabled = false;
      events.send(String(powerState).c_str(), "powerstate", millis());
      Serial.println("Alarm!");
    }
    guardLastTime = millis();
  }

  if ((millis() - webLastTime) > webTimerDelay) {
    unsigned long allSeconds = millis() / 1000;
    int runHours = allSeconds / 3600;
    int secsRemaining = allSeconds % 3600;
    int runMinutes = secsRemaining / 60;
    int runSeconds = secsRemaining % 60;
    char buf[30];
    sprintf(buf, "Uptime: %02d:%02d:%02d", runHours, runMinutes, runSeconds);
    events.send(String(buf).c_str(), "headerinfo", millis());
    events.send(String(voltage).c_str(), "voltage", millis());
    events.send(String(current).c_str(), "current", millis());
    webLastTime = millis();
  }

}
