/*
 * Common mistakes & tips:
 *   * Don't just connect the IR LED directly to the pin, it won't
 *     have enough current to drive the IR LED effectively.
 *   * Make sure you have the IR LED polarity correct.
 *     See: https://learn.sparkfun.com/tutorials/polarity/diode-and-led-polarity
 *   * Typical digital camera/phones can be used to see if the IR LED is flashed.
 *     Replace the IR LED with a normal LED if you don't have a digital camera
 *     when debugging.
 *   * Avoid using the following pins unless you really know what you are doing:
 *     * Pin 0/D3: Can interfere with the boot/program mode & support circuits.
 *     * Pin 1/TX/TXD0: Any serial transmissions from the ESP8266 will interfere.
 *     * Pin 3/RX/RXD0: Any serial transmissions to the ESP8266 will interfere.
 *   * ESP-01 modules are tricky. We suggest you use a module with more GPIOs
 *     for your first time. e.g. ESP-12 etc.
 */
#ifndef UNIT_TEST
#include <Arduino.h>
#endif
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <IRsend.h>
#include <WiFiClient.h>
#include <IRrecv.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include <ArduinoHttpClient.h>

// =====================================================
// Setup
int cycleCount = 1; // Loop delay
// Helps to calibrate timers to cycle count
float secondsCalibrator = 0;
bool isManualMode = false;
float acceptableWattage = 24.0; // This is the maximum wattage you allow the automatic settings to fall back to (in Watts)
// =====================================================


// =====================================================
// Ambient Light Sensor
#define lightSensorPin A0 //Ambient light sensor reading
float lightReading;
float ambientLightReading;
float ambientLightWithMaxLightReading;
float ambientLightWithEveningLightReading;
// =====================================================

// =====================================================
// Light Controls through IR Transmitter
IRsend irsend(4);  // An IR LED is controlled by GPIO pin 4 (D2)
// =====================================================


// =====================================================
// PIR Motion Sensor
int pirInputPin = D7;
int pirValue;
int timeDeltaSinceLightOn = 0;
int maxSecondsBeforeLightOff = 20;
int gracePeriod = 0;

int definedGracePeriod = 1; // light ramp up timing
// =====================================================

// =====================================================
// IR Receiver
uint16_t RECV_PIN = D4;
IRrecv irrecv(RECV_PIN);
decode_results results;
// =====================================================

// =====================================================
// WIFI Setup
const char* ssid = "Pretzel";
const char* password = "2609199265";
// =====================================================

// =====================================================
// Web Server Settings

// For getting sunrise & sunset times from the API
String serverAddress = "https://api.sunrise-sunset.org";
MDNSResponder mdns;

ESP8266WebServer server(80);

// Define NTP Client to get time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

// For state of day (sunrise, sunset, sleep) settings
String state = "unset";

// Global state for light fixture on/off
bool lightDidTurnOn = false;
// =====================================================

void handleRoot() {
    runLightReading();
    String lightTextColour;
    String lightText;
    String manualModeTextColour;
    String manualText;


    if (lightDidTurnOn) {
        lightTextColour = "has-text-success";
        lightText = "On";
    } else {
        lightTextColour = "has-text-danger";
        lightText = "Off";
    }

    if (isManualMode) {
        manualModeTextColour = "has-text-danger";
        manualText = "No";
    } else {
        manualModeTextColour = "has-text-success";
        manualText = "Yes";
    }

    String html = "<html>" \
                "<head>" \
                "<meta charset=\"utf-8\">" \
                "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">" \
                "<link rel=\"stylesheet\" href=\"https://cdn.jsdelivr.net/npm/bulma@0.9.3/css/bulma.min.css\">" \
                "<title>ER Light Manager</title></head>" \
                "<body>" \
                  "<section class=\"hero is-medium is-link\">" \
                    "<div class=\"hero-body\"><p class=\"title\">" \
                      "Hello." \
                    "</p>" \
                    "<p class=\"subtitle\">" \
                      "Welcome to the Light Manager backend." \
                    "</p>" \
                    "</div>" \
                   "</section>" \
                   "<nav class=\"level my-5\">"
                        "<div class=\"level-item has-text-centered\">"
                            "<div>"
                                "<p class=\"heading\">Current Light Level</p>"
                                "<p class=\"title\">" + String(lightReading) + "</p>"
                            "</div>"
                        "</div>"
                        "<div class=\"level-item has-text-centered\">"
                            "<div>"
                                "<p class=\"heading\">Light Fixture</p>"
                                "<p class=\"title " + lightTextColour + "\">" + lightText + "</p>"
                            "</div>"
                        "</div>"
                        "<div class=\"level-item has-text-centered\">"
                            "<div>"
                                "<p class=\"heading\">Auto Mode?</p>"
                                "<p class=\"title " + manualModeTextColour + "\">" + manualText  + "</p>"
                            "</div>"
                        "</div>"
                        "<div class=\"level-item has-text-centered\">"
                            "<div>"
                                "<p class=\"heading\">Ambient Reading</p>"
                                "<p class=\"title\">" + String(ambientLightReading) + "</p>"
                            "</div>"
                        "</div>"
                        "<div class=\"level-item has-text-centered\">"
                            "<div>"
                                "<a class=\"button is-gray is-light\" href=\"/\">Refresh</a>" \
                            "</div>"
                        "</div>"
                    "</nav>"
                   "<div class=\"columns my-5\">" \
                    "<div class=\"column is-6 is-offset-3 buttons\">" \
                      "<a class=\"button is-primary\" href=\"lm?fixtureOnOff=true\">Turn on/off</a>" \
                      "<a class=\"button is-info\" href=\"lm?auto=true\">Run Auto Mode</a>"\
                      "<a class=\"button is-light\" href=\"lm?code=33454215\">Reduce brightness</a>" \
                      "<a class=\"button is-dark\" href=\"lm?code=33441975\">Increase brightness</a>" \
                      "<a class=\"button is-info is-light\" href=\"lm?code=33472575\">Decrease colour temperature</a>" \
                      "<a class=\"button is-danger is-light\" href=\"lm?code=33439935\">Increase colour temperature</a>" \
                      "<a class=\"button is-success is-light\" href=\"lm?code=33448095\">Change colour temperature</a>" \
                      "<a class=\"button is-warning\" href=\"lm?code=33464415\">Neutral colour</a><br><br><br>" \
                      "<a class=\"button is-danger\" href=\"lm?reset=true\">Reset</a>"\
                    "</div>" \
                   "</div>" \
                "</body>" \
              "</html>";

    server.send(200, "text/html", html);
}
void handleGUICommands() {
    runLightReading(); 
    for (uint8_t i = 0; i < server.args(); i++) {
        if (server.argName(i) == "code") {
            transmitIR(server.arg(i));
            isManualMode = true;
            state = "unset";
            Serial.println("Set to manual mode.");
        } else if (server.argName(i) == "auto") {
            isManualMode = false;
            Serial.println("Set to auto mode.");
        } else if (server.argName(i) == "reset") {
            initialiseLighting();
            isManualMode = false;
        } else if (server.argName(i) == "fixtureOnOff") {
            isManualMode = true;
            triggerFixtureOnOff(!lightDidTurnOn);
        }
    }
    server.sendHeader("Location", String("/"), true);
    server.send (302, "text/plain", "");
}

void handleNotFound() {
    String message = "File Not Found\n\n";
    message += "URI: ";
    message += server.uri();
    message += "\nMethod: ";
    message += (server.method() == HTTP_GET)?"GET":"POST";
    message += "\nArguments: ";
    message += server.args();
    message += "\n";
    for (uint8_t i = 0; i < server.args(); i++)
        message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
    server.send(404, "text/plain", message);
}

void handleIR(String wordCommand) {
    if (wordCommand == "onOff") {
        transmitIR("33456255");
    } else if (wordCommand == "decreaseBrightness") {
        transmitIR("33454215");
    } else if (wordCommand == "increaseBrightness") {
        transmitIR("33441975");
    } else if (wordCommand == "decreaseColourTemp") {
        transmitIR("33472575");
    } else if (wordCommand == "increaseColourTemp") {
        transmitIR("33439935");
    } else if (wordCommand == "changeColourTemp") {
        transmitIR("33448095");
    } else if (wordCommand == "eveningLight") {
        transmitIR("33464415");
    }
}

void transmitIR(String command) {
    uint32_t code;
    if (command == "33454215" || command == "33441975" || command == "33472575" || command == "33439935") {
        code = strtoul(command.c_str(), NULL, 10);
        irsend.sendNEC(code, 32, 2); // with repeat
    } else {
        code = strtoul(command.c_str(), NULL, 10);
        irsend.sendNEC(code, 32);
    }
}

//void getSolarTimesData() {
//  if (WiFi.status() == WL_CONNECTED) {
//
//      String lat = "1.3521"; // SG lat
//      String lng = "103.8198"; // SG lng
//
//      String serverPath = "/json?lat=" + lat + "&lng=" + lng;
//
//      int err = 0;
//      err = http.get(serverPath);
//      
//      if (err == 0) {
//        Serial.println("startedRequest ok");
//    
//        err = http.responseStatusCode();
//        if (err >= 0) {
//          Serial.print("Got status code: ");
//          Serial.println(err);
//          int bodyLen = http.contentLength();
//          Serial.print("Content length is: ");
//          Serial.println(bodyLen);
//          Serial.println();
//          Serial.println("Body returned follows:");
//        
//          // Now we've got to the body, so we can print it out
//          unsigned long timeoutStart = millis();
//          char c;
//          // Whilst we haven't timed out & haven't reached the end of the body
//          while ( (http.connected() || http.available()) && (!http.endOfBodyReached()) && ((millis() - timeoutStart) < kNetworkTimeout) ) {
//              if (http.available()) {
//                c = http.read();
//                // Print out this character
//                Serial.print(c);
//               
//                // We read something, reset the timeout counter
//                timeoutStart = millis();
//              } else {
//                // We haven't got any data, so let's pause to allow some to
//                // arrive
//                delay(kNetworkDelay);
//              }
//          }
//        } else {    
//          Serial.print("Getting response failed: ");
//          Serial.println(err);
//        }
//      } else {
//        Serial.print("Connect failed: ");
//        Serial.println(err);
//      }
//      http.stop();
//    } else {
//      Serial.println("WiFi Disconnected");
//    }
//}

String runLightController(String state) {
    // get current time
    timeClient.update();
    int currentHour = timeClient.getHours();
    int currentMinute = timeClient.getMinutes();
    

    Serial.print("Hour: ");
    Serial.println(currentHour);
    Serial.print("Minutes: ");
    Serial.println(currentMinute);
    // get sunrise & sunset times of Singapore from API
    //  getSolarTimesData();

    // sunrise/sunset/sleep config
    int sunriseHour = 6;
    int sunriseMinute = 58;
    
    int sunsetHour = 19;
    int sunsetMinute = 06;

    int sleepHour = 23;
    int sleepMinute = 0;

    bool isSunrise;
    bool isSunset;
    bool isSleep;

    if ((currentHour == sunriseHour && currentMinute >= sunriseMinute) || (currentHour == sunsetHour && currentMinute < sunsetMinute) || (currentHour > sunriseHour && currentHour < sunsetHour)) {
        isSunrise = true;
        isSunset = false;
        isSleep = false;
    } else if ((currentHour == sunsetHour && currentMinute >= sunsetMinute) || (currentHour == sleepHour && currentMinute < sleepMinute) || (currentHour > sunsetHour && currentHour < sleepHour)) {
        isSunrise = false;
        isSunset = true;
        isSleep = false;
    } else {
        isSunrise = false;
        isSunset = false;
        isSleep = true;
    }
    
    //  Serial.println(isSunrise ? "isSunrise" : isSunset ? "isSunset" : isSleep ? "isSleep" : "Something is wrong with the timing logic.");
    
    // light controller module
    if (isSunrise) {
        if (state != "sunrise") {
          Serial.println("Setting sunrise");
          // turn on the lights      
          triggerFixtureOnOff(true);
          // slow ramp up (evening light)
          handleIR("eveningLight");
          // decrease colour temperature (cooler)
          for (int i = 0; i < 3; i++) {
              handleIR("decreaseColourTemp");
          }
          state = "sunrise";
          lightDidTurnOn = true;
        } else {
          // test brightness logic
          if (currentHour >= 10 && currentMinute >= 10) {
              float difference = (ambientLightWithMaxLightReading - ambientLightReading) * (acceptableWattage / 66.0);
              float lowerBoundBrightness = ambientLightReading + (difference * 0.8);
              float upperBoundBrightness = ambientLightReading + (difference * 1.2);
  
              Serial.print("lower bound:");
              Serial.println(lowerBoundBrightness);
  
              Serial.print("upper bound: ");
              Serial.println(upperBoundBrightness);
              
              if (lightReading < lowerBoundBrightness) {
                handleIR("increaseBrightness");
              } else if (lightReading > upperBoundBrightness) {
                handleIR("decreaseBrightness");
              }
          }
        }
    } else if (isSunset) {

        if (state != "sunset") {
        Serial.println("Setting sunset");
        // evening light       
        handleIR("eveningLight");
        // increase colour temp
        for (int i = 0; i < 9; i++) {
            handleIR("increaseColourTemp");
        }
        // reduce brightness
        for (int i = 0; i < 9; i++) {
            handleIR("decreaseBrightness");
        }
        state = "sunset";
        lightDidTurnOn = true;
        }
        
    } else if (isSleep) {

        if (state != "sleep") {
        Serial.println("Setting sleep");
        triggerFixtureOnOff(false);
        state = "sleep";
        }
    }
    return state;
}

void triggerFixtureOnOff(bool didTriggerOn) {
    float lightOnCutOff = ambientLightReading + ((ambientLightWithEveningLightReading - ambientLightReading) * 0.015);
    float lightOffCutOff = ambientLightReading + ((ambientLightWithEveningLightReading - ambientLightReading) * 0.2);
    Serial.print("Light On Cut Off: ");
    Serial.println(lightOnCutOff);
    Serial.print("Light Off Cut Off: ");
    Serial.println(lightOffCutOff);

    if (didTriggerOn) {
        while (lightReading <= lightOnCutOff) {
            handleIR("onOff");
            delay(400);
            runLightReading();
        }
        
        lightDidTurnOn = true;
        
    } else {
        while (lightReading > lightOffCutOff and !didTriggerOn) {
            handleIR("onOff");
            delay(400);
            runLightReading();
        }
        
        lightDidTurnOn = false;
        timeDeltaSinceLightOn = 0;
    }

    Serial.print("Light fixture is turned on: ");
    Serial.println(lightDidTurnOn ? "Yes" : "No");
}

void initialiseLighting() {
    state = "unset";
    Serial.println("Setting up your light fixture...");

    // MAX Light Initialisation
    handleIR("eveningLight");
    handleIR("onOff");
    delay(500);
    handleIR("changeColourTemp");
    delay(1000);
    ambientLightWithMaxLightReading = analogRead(lightSensorPin) * 0.0976;
    handleIR("onOff");
    delay(1000);

    // change to evening light to initialise ON 
    handleIR("eveningLight");
    delay(1000);
    ambientLightWithEveningLightReading = analogRead(lightSensorPin) * 0.0976;
    
    // turn off to confirm lighting is OFF and set lightDidTurnOn state to FALSE
    handleIR("onOff");
    lightDidTurnOn = false;
    delay(1000);
    ambientLightReading = analogRead(lightSensorPin) * 0.0976;

    Serial.print("Ambient light: ");
    Serial.println(ambientLightReading);

    Serial.print("Ambient light with evening light: ");
    Serial.println(ambientLightWithEveningLightReading);

    Serial.print("Ambient light with full brightness: ");
    Serial.println(ambientLightWithMaxLightReading);
    Serial.println("Light fixture is set up!");
}

void runLightReading() {
    lightReading = analogRead(lightSensorPin) * 0.0976; //Read light level
    Serial.print("Light reading is: ");
    Serial.print(lightReading);
    Serial.println(" %");
}

void runMotionDetector() {
    // Motion detection readings
    pirValue = digitalRead(pirInputPin);
    if (pirValue == HIGH) {
        timeDeltaSinceLightOn = 0;
        Serial.print("Motion detected! Time Delta Since Lights On reset to: ");
        Serial.println(timeDeltaSinceLightOn);
        
        if (!lightDidTurnOn && gracePeriod == 0) {
            triggerFixtureOnOff(true);
        } else if (!lightDidTurnOn && gracePeriod > 0) {
            gracePeriod--;
        }
    } else {
        Serial.println("No motion detected");
        
        //  counter if no motion detected
        if (lightDidTurnOn) {
            timeDeltaSinceLightOn += 1;
            
            Serial.print("Motion not detected! Time Delta Since Lights On: ");
            Serial.println(timeDeltaSinceLightOn);
        }
        
        if (lightDidTurnOn && (timeDeltaSinceLightOn >= maxSecondsBeforeLightOff)) {
            Serial.print("It's been ");
            Serial.print(timeDeltaSinceLightOn);
            Serial.print(" seconds since the light has been turned on with no motion detected, turning off now...");
            triggerFixtureOnOff(false);
            gracePeriod = definedGracePeriod;
        }
    }
}

void runReadIR() {
    // IR readings
    if (irrecv.decode(&results)) {
        if (results.value >> 32)  // print() & println() can't handle printing long longs. (uint64_t)
            Serial.print((uint32_t) (results.value >> 32), HEX);  // print the first part of the message
        Serial.println((uint32_t) (results.value & 0xFFFFFFFF), HEX); // print the second part of the message
        irrecv.resume();  // Receive the next value
    }
}

void setup(void) {
  
    irsend.begin();
    irrecv.enableIRIn();

    Serial.begin(115200);
    WiFi.begin(ssid, password);
    Serial.println("");

    // Wait for connection
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("");
    Serial.print("Connected to ");
    Serial.println(ssid);
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    if (mdns.begin("esp8266", WiFi.localIP())) {
        Serial.println("MDNS responder started");
    }

    server.on("/", handleRoot);
    server.on("/lm", handleGUICommands);

    server.on("/inline", [](){
        server.send(200, "text/plain", "this works as well");
    });

    server.onNotFound(handleNotFound);

    server.begin();
    Serial.println("HTTP server started");
    
    // Initialize a NTPClient to get time
    timeClient.begin();
    // Set offset time in seconds to adjust for your timezone, for example:
    // GMT +1 = 3600
    // GMT +8 = 28800
    // GMT -1 = -3600
    // GMT 0 = 0
    timeClient.setTimeOffset(28800);

    // Sensor initialisation
    pinMode(lightSensorPin, INPUT);
    pinMode(pirInputPin, INPUT);
    Serial.println("Initialising motion sensor...");

    int pirInitialisingSeconds = 5;
    for (int i = 0; i <= pirInitialisingSeconds; i++) {
        Serial.print("Duration remaining: ");
        Serial.println(pirInitialisingSeconds-i);
        delay(1000);  // let the PIR initialise first
    }
    Serial.println("Motion sensor initialised!");
    
    // initialise lights
    initialiseLighting();
}

void loop(void) {
    
    // Light readings runs only every 1 second
    if (secondsCalibrator >= 1) {
        runLightReading(); 
        if (!isManualMode) {
            // run motion detector service
            runMotionDetector();
    
            // runReadIR();
    
            // Light Manager module
            state = runLightController(state);
            Serial.print("Current state: ");
            Serial.println(state);
        }
        secondsCalibrator = 0.0;
        Serial.println("");
        Serial.println("==========CYCLE RESTARTS==========");
        Serial.println("");
    } else {
        secondsCalibrator += (cycleCount*1.0)/1000.0;
    }
    
    // Web server
    server.handleClient();
    
    // Take a chill pill
    delay(cycleCount);
}
