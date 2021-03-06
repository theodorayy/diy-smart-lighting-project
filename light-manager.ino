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
float lightMaximumWattage = 24.0;
// =====================================================


// =====================================================
// Ambient Light Sensor
#define lightSensorPin A0 //Ambient light sensor reading
float lightReading;
float ambientLightReading;
float ambientWithMaxLightReading;
float ambientWithLowLightReading;
float lightOnCutOff;
float lightOffCutOff;
float siriBrightnessRequest;
int currentColourTemp;
// =====================================================

// =====================================================
// Light Controls through IR Transmitter
IRsend irsend(4);  // An IR LED is controlled by GPIO pin 4 (D2)
// =====================================================


// =====================================================
// PIR Motion Sensor
int pirInputPin = D7;
int pirValue;
int pirInitialisingSeconds = 30; // how many seconds to initialise PIR

int pirTriggerCycle = 6; // PIR has this 'reset' period after making a reading... (so let program trigger every pirTriggerCycle seconds)
int timeElapsedSinceTriggerCycle = pirTriggerCycle;

int timeDeltaSinceLightOn = 0; // how many seconds elapsed since last motion detected and lights are on
int maxSecondsBeforeLightOff = 120;

// Grace periods
int gracePeriod = 0;
int definedGracePeriod = 0; // light ramp up timing

bool didMotionTriggerLightsOff = false;
// =====================================================

// =====================================================
// IR Receiver
uint16_t RECV_PIN = D4;
IRrecv irrecv(RECV_PIN);
decode_results results;
// =====================================================

// =====================================================
// WIFI Setup
IPAddress ip(192, 168, 1, 218);
IPAddress dns(192, 168, 1, 1);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);
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
                      "Welcome to the ER Light Manager." \
                    "</p>" \
                    "</div>" \
                   "</section>" \
                   "<nav class=\"level my-5\">"
                        "<div class=\"level-item has-text-centered\">"
                            "<div>"
                                "<p class=\"heading\">Initialised Ambient Reading</p>"
                                "<p class=\"title\">" + String(ambientLightReading) + "%</p>"
                            "</div>"
                        "</div>"
                        "<div class=\"level-item has-text-centered\">"
                            "<div>"
                                "<p class=\"heading\">Current Ambient Light</p>"
                                "<p class=\"title\">" + String(lightReading) + "%</p>"
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
                                "<p class=\"heading\">Last motion detected</p>"
                                "<p class=\"title\">" + String(timeDeltaSinceLightOn) + "s ago</p>"
                            "</div>"
                        "</div>"
                        "<div class=\"level-item has-text-centered\">"
                            "<div>"
                                "<a class=\"button is-gray is-light\" href=\"/\">Refresh &#10227;</a>" \
                            "</div>"
                        "</div>"
                    "</nav>"
                    "<div class=\"columns\">"\
                        "<div class=\"column is-8 is-offset-2 buttons has-addons is-centered\">"\
                            "<div class=\"tile is-ancestor\">"\
                                "<div class=\"tile is-parent\">"\
                                    "<a href=\"lm?fixtureOnOff=true\" class=\"tile is-child notification has-text-centered is-danger is-light\">"\
                                        "<p class=\"title\">&#x23FB;</p>"\
                                        "<p class=\"subtitle\">Turn on/off</p>"\
                                    "</a>"\
                                "</div>"\
                            "</div>"\
                            "<div class=\"tile is-ancestor\">"\
                                "<div class=\"tile is-parent\">"\
                                    "<a href=\"lm?auto=true\" class=\"tile is-child notification has-text-centered is-success is-light\">"\
                                        "<p class=\"title\">Auto</p>"\
                                        "<p class=\"subtitle\">Set light controls to automatic.</p>"\
                                    "</a>"\
                                "</div>"\
                            "</div>"\
                            "<div class=\"tile is-ancestor\">"\
                                "<div class=\"tile is-parent\">"\
                                    "<a href=\"lm?code=33454215\" class=\"tile is-child notification has-text-centered has-background-grey-lighter has-text-dark\">"\
                                        "<p class=\"title\">&#128261;</p>"\
                                        "<p class=\"subtitle\">Decrease brightness</p>"\
                                    "</a>"\
                                "</div>"\
                                "<div class=\"tile is-parent\">"\
                                    "<a href=\"lm?code=33441975\" class=\"tile is-child notification has-text-centered is-light\">"\
                                        "<p class=\"title\">&#128262;</p>"\
                                        "<p class=\"subtitle\">Increase brightness</p>"\
                                    "</a>"\
                                "</div>"\
                            "</div>"\
                            "<div class=\"tile is-ancestor\">"\
                                "<div class=\"tile is-parent\">"\
                                    "<a href=\"lm?code=33439935\" class=\"tile is-child notification has-text-centered is-light is-warning\">"\
                                        "<p class=\"title\">2700K</p>"\
                                        "<p class=\"subtitle\">Warmer, softer light</p>"\
                                    "</a>"\
                                "</div>"\
                                "<div class=\"tile is-parent\">"\
                                    "<a href=\"lm?code=33472575\" class=\"tile is-child notification has-text-centered is-light is-info\">"\
                                        "<p class=\"title\">6500K</p>"\
                                        "<p class=\"subtitle\">Cooler, active light</p>"\
                                    "</a>"\
                                "</div>"\
                            "</div>"\
                            "<div class=\"tile is-ancestor\">"\
                                "<div class=\"tile is-parent\">"\
                                    "<a href=\"lm?code=33448095\" class=\"tile is-child notification has-text-centered is-light is-success\">"\
                                        "<p class=\"title\">&#128260;</p>"\
                                        "<p class=\"subtitle\">Change colour temperature</p>"\
                                    "</a>"\
                                "</div>"\
                                "<div class=\"tile is-parent\">"\
                                    "<a href=\"lm?code=33464415\" class=\"tile is-child notification has-text-centered is-light is-warning\">"\
                                        "<p class=\"title\">&#128161;</p>"\
                                        "<p class=\"subtitle\">Neutral lighting</p>"\
                                    "</a>"\
                                "</div>"\
                            "</div>"\
                            "<div class=\"tile is-ancestor\">"\
                                "<div class=\"tile is-parent\">"\
                                    "<a href=\"lm?reset=true\" class=\"tile is-child notification has-text-centered is-danger\">"\
                                        "<p class=\"title\">RESET</p>"\
                                    "</a>"\
                                "</div>"\
                            "</div>"\
                        "</div>"\
                    "</div>"\
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
            didMotionTriggerLightsOff = false;
            timeElapsedSinceTriggerCycle = pirTriggerCycle;
            Serial.println("Set to auto mode.");
        } else if (server.argName(i) == "reset") {
            initialiseLighting();
            isManualMode = false;
        } else if (server.argName(i) == "fixtureOnOff") {
            isManualMode = true;
            triggerFixtureOn(!lightDidTurnOn);
        }
    }
    server.sendHeader("Location", String("/"), true);
    server.send(302, "text/plain", "");
}

void handleSiriCommands() {
    String jsonMessage;
    String stateValue;
    String siriRequest;
    for (uint8_t i = 0; i < server.args(); i++) {
        if (server.argName(i) == "setControl") {
            isManualMode = true;
            runLightReading();
            Serial.println("Handling via Siri now...");
            handleIR(server.arg(i));
        } else if (server.argName(i) == "setAuto") {
            if (server.arg(i) == "false") {
                isManualMode = true;
            } else {
                isManualMode = false;
                didMotionTriggerLightsOff = false;
                timeElapsedSinceTriggerCycle = pirTriggerCycle;
            }
            runLightReading();
            Serial.println("Handling auto mode settings via Siri now...");
        } else if (server.argName(i) == "desiredBrightness") {
            isManualMode = true;
            runLightReading();
            siriBrightnessRequest = server.arg(i).toInt();
            Serial.println("Handling brightness via Siri now...");
            handleBrightness(server.arg(i));
        } else if (server.argName(i) == "setColourTemp") {
            isManualMode = true;
            Serial.println("Handling colour temperature via Siri now...");
            handleColourTemp(server.arg(i));
        } else if (server.argName(i) == "getStatus") {
            runLightReading();
            // STATUS LIST
            // power
            // currentBrightness
            siriRequest = server.arg(i);
            String isManualString;
            if (isManualMode) {
                isManualString = "true";
            } else {
                isManualString = "false";
            }
            if (server.arg(i) == "power") {
                stateValue = lightDidTurnOn;
                jsonMessage = "{\"fixture\": \"light\", \"request\": \"" + siriRequest + "\", \"is_manual\": \"" + isManualString + "\"\"state\": " + stateValue + "}";
            } else if (server.arg(i) == "currentBrightness") {
                stateValue = lightReading;
                jsonMessage = "{\"fixture\": \"light\", \"request\": \"" + siriRequest + "\", \"is_manual\": \"" + isManualString + "\", \"state\": " + stateValue + "}";
            } else if (server.arg(i) == "continuousPoll") {
                stateValue = lightDidTurnOn;
                float colourTempInKelvin = 1000000.0 / currentColourTemp;
                jsonMessage = "{\"fixture\": \"light\", \"request\": \"" + siriRequest + "\", \"is_manual\": \"" + isManualString + "\", \"state\": " + stateValue + ", \"last_motion_detected_seconds\": " + timeDeltaSinceLightOn + ", \"brightness\": " + lightReading + ", \"colour_temp_in_kelvin\":" + colourTempInKelvin +"}";
            }
        }
    }
    server.send(200, "application/json", jsonMessage);
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
    Serial.println(wordCommand);
    if (wordCommand == "onOff") {
        // always needed by triggerFixtureOn()
        transmitIR("33456255");
    } else if (wordCommand == "on") {
        Serial.println(wordCommand);
        Serial.println("turning on");
        triggerFixtureOn(true);
    } else if (wordCommand == "off") {
        Serial.println(wordCommand);
        Serial.println("turning off");
        triggerFixtureOn(false);
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
    } else if (wordCommand == "setMood") {
        Serial.println("Setting mood");
    } else if (wordCommand == "setAuto") {
        isManualMode = false;
        Serial.println("Set to auto mode.");
    }
}

void handleBrightness(String desiredLevel) {

    int request = desiredLevel.toInt();

    int iterator = (request - lightReading) / 10;
    int maxIterator = abs(iterator);
  
    for (int i = 0; i < maxIterator; i++) {
          if (iterator < 0) {
              handleIR("decreaseBrightness");
          } else if (iterator > 0) {
              handleIR("increaseBrightness");
          }
    }
    
    
}

void handleColourTemp(String stringTemp) {
    //set colour neutral first
    handleIR("eveningLight");

    int colourTemp = stringTemp.toInt();
    Serial.print("Temp is: ");
    Serial.println(stringTemp);

    if (colourTemp < 230) {
        smoothColourTempModifier("decrease");
        smoothColourTempModifier("decrease");
    } else if (colourTemp >= 230 && colourTemp < 320) {
        smoothColourTempModifier("decrease");
    } else if (colourTemp >= 320 && colourTemp < 410) {
        smoothColourTempModifier("increase");
    } else if (colourTemp >= 410) {
        smoothColourTempModifier("increase");
        smoothColourTempModifier("increase");
    }
    runLightReading();
    handleBrightness(String(siriBrightnessRequest,0));
    
    currentColourTemp = colourTemp;
}

void smoothColourTempModifier(String incrementDecrement) {
    handleIR(incrementDecrement + "ColourTemp");
//    handleIR(incrementDecrement + "Brightness");
//    delay(200);
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
    int sunsetMinute = 6;

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
        currentColourTemp = 180;
        if (state != "sunrise") {
          Serial.println("Setting sunrise");
          // turn on the lights      
          triggerFixtureOn(true);
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
              float difference = (ambientWithMaxLightReading - ambientLightReading) * (acceptableWattage / lightMaximumWattage);
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
        currentColourTemp = 400;
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
            triggerFixtureOn(false);
            state = "sleep";
        }
    }
    return state;
}

void triggerFixtureOn(bool didTriggerOn) {

    Serial.println("");
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
    didMotionTriggerLightsOff = false;
    Serial.println("Setting up your light fixture...");

    // MAX Light Initialisation
    handleIR("eveningLight");
    handleIR("onOff"); // turns off
    delay(500);
    handleIR("changeColourTemp");
    delay(2500);
    ambientWithMaxLightReading = analogRead(lightSensorPin) * 0.0976;
    delay(500);
    // Turn on and set to lowest brightness
    for (int i = 0; i < 10; i++) {
        handleIR("decreaseBrightness");
    }
    delay(1000);
    ambientWithLowLightReading = analogRead(lightSensorPin) * 0.0976;
    delay(2000);
    
    // turn off to confirm lighting is OFF and set lightDidTurnOn state to FALSE
    handleIR("onOff");
    delay(1000);
    ambientLightReading = analogRead(lightSensorPin) * 0.0976;

    while (ambientLightReading >= (ambientWithLowLightReading * 0.9)) {
        handleIR("onOff");
        delay(1000);
        ambientLightReading = analogRead(lightSensorPin) * 0.0976;
    }
    // since we can't keep track of states,
    // use a cutoff with a buffer range for the ambient light when the fixture is turned off
    lightOnCutOff = ambientLightReading + ((ambientWithLowLightReading - ambientLightReading) * 0.015);
    lightOffCutOff = ambientLightReading + ((ambientWithLowLightReading - ambientLightReading) * 0.2);

    Serial.print("Ambient light with fixture off: ");
    Serial.println(ambientLightReading);

    Serial.print("Ambient light with lowest brightness: ");
    Serial.println(ambientWithLowLightReading);

    Serial.print("Ambient light with full brightness: ");
    Serial.println(ambientWithMaxLightReading);
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
            triggerFixtureOn(true);
            didMotionTriggerLightsOff = false;
        } else if (!lightDidTurnOn && gracePeriod > 0) {
            gracePeriod--;
        }
    } else {
        Serial.println("No motion detected");
        
        //  set a counter if no motion detected
        if (lightDidTurnOn) {
            timeDeltaSinceLightOn += pirTriggerCycle;
            
            Serial.print("Motion not detected! Time Delta Since Lights On: ");
            Serial.println(timeDeltaSinceLightOn);
        }
        
        if (lightDidTurnOn && (timeDeltaSinceLightOn >= maxSecondsBeforeLightOff)) {
            Serial.print("It's been ");
            Serial.print(timeDeltaSinceLightOn);
            Serial.print(" seconds since the light has been turned on with no motion detected, turning off now...");
            triggerFixtureOn(false);
            didMotionTriggerLightsOff = true;
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
    WiFi.config(ip, dns, gateway, subnet);
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
    server.on("/siri", handleSiriCommands);

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

    // Do a little dance to demonstrate it's working ;)
    handleIR("eveningLight");
    delay(50);
    handleIR("changeColourTemp");
    delay(50);
    handleIR("eveningLight");

    // Sensor initialisation
    pinMode(lightSensorPin, INPUT);
    pinMode(pirInputPin, INPUT);
    Serial.println("Initialising motion sensor...");

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
    
    // Light readings run only every 1 second
    if (secondsCalibrator >= 1) {
        runLightReading(); 
        if (!isManualMode) {
            // run motion detector service
            if (state != "sleep") {
                if (!didMotionTriggerLightsOff && timeElapsedSinceTriggerCycle > 0) {
                    timeElapsedSinceTriggerCycle--;
                    Serial.println("Time elapsed since trigger cycle:");
                    Serial.println(timeElapsedSinceTriggerCycle);
                } else {
                    runMotionDetector();
                    timeElapsedSinceTriggerCycle = pirTriggerCycle;
                }
            }
    
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
