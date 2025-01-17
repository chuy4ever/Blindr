#include <ESP8266WebServer.h>

// General config
#define DEBUG 1
// #define BMP280 1

#ifdef BMP280
  #include <Adafruit_BMP280.h>
  #include <Wire.h>
  Adafruit_BMP280 bmp;
#endif

// Wifi config
#define HTTP_REST_PORT 80
const char *ssid = "ssid";
const char *password = "pass";
const String deviceName = "Blindr";
ESP8266WebServer server(HTTP_REST_PORT);

// Blind rotator config
const int GEAR_RATIO = 2;               // Gear ratio of the blind gear to motor (2:1)
const int NUM_ENCODER_HOLES = 10;       // Number of holes in the encoder wheel (10 = 1 rotation)
const double NUM_WAND_ROTATIONS = 1.5;  // Number of wand rotations needed to move from one state to another

// Pinout
#define UP D6
#define DOWN D7
#define LED0 D5
#define LED1 D8
#define LED2 D4

// State constants
#define CLOSED_UP 0
#define OPEN 1
#define CLOSED_DOWN 2

// Can be open (blinds horizontal) or closed (blinds vertical)
// Closed can be up or down (concave up or concave down)
int currentState = OPEN;
int nextState = OPEN;

const int analogInPin = A0;   // Sensor pin
int sensorValue = 0;          // Value read from the sensor
bool wasLow = false;          // Was the sensor low last time we checked?

int ticks = 0;                // Number of ticks since last movement
int tickStop = 0;             // Number of ticks to stop motor at
void setup() {
  #ifdef DEBUG
    Serial.begin(115200);
  #endif
  pinMode(UP, OUTPUT);
  pinMode(DOWN, OUTPUT);
  pinMode(LED0, OUTPUT);
  pinMode(LED1, OUTPUT);
  pinMode(LED2, OUTPUT);

  #ifdef BMP280
    // Initialize BMP280
    if (!bmp.begin(BMP280_ADDRESS_ALT, BMP280_CHIPID)) {
      #ifdef DEBUG
        Serial.println("Could not find a valid BMP280 sensor, check wiring!");
      #endif
      while (1) delay(10);
    }

    bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,     // Operating Mode.
                    Adafruit_BMP280::SAMPLING_X2,     // Temp. oversampling = 2x
                    Adafruit_BMP280::SAMPLING_X16,    // Pressure oversampling = 16x
                    Adafruit_BMP280::FILTER_X16,      // Filtering = 16x
                    Adafruit_BMP280::STANDBY_MS_500); // Standby time = 500ms
  #endif

  // Webserver init
  #ifdef DEBUG
    Serial.println();
    Serial.println();
    Serial.print("Connecting to ");
    Serial.println(ssid);
  #endif

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    // Wait a bit and then try again
    delay(500);
    #ifdef DEBUG
        Serial.print(".");
    #endif
  }

  #ifdef DEBUG
    Serial.println("");
    Serial.println("WiFi connected");
  #endif

  // Set routes
  configureRouter();

  // Start the server
  server.begin();

  #ifdef DEBUG
    Serial.println("Server started");
    // Print the IP address
    Serial.print("Use this URL : ");
    Serial.print("http://");
    Serial.print(WiFi.localIP());
    Serial.println("/");
  #endif

}

void loop() {
  server.handleClient();

  // Check if we need to move the blinds/they are moving
  if (currentState != nextState) {
    // Count encoder ticks
    sensorValue = analogRead(analogInPin);
    // #ifdef DEBUG
    //   Serial.print("Sensor value: ");
    //   Serial.println(sensorValue);
    // #endif
    if (sensorValue > 500 && wasLow == true) {
      wasLow = false;
      ticks++;
      // #ifdef DEBUG
      //   Serial.print("Ticks: ");
      //   Serial.println(ticks);
      // #endif
    } else if (sensorValue < 500 && wasLow == false) {
      wasLow = true;
    }
    if (ticks >= tickStop) {
      // Finished transitioning to nextState
      changeLED(nextState, HIGH);
      Brake();
      delay(500);
      Stop();
      currentState = nextState;
    }
  }
}


void handleRoot() {
  server.send(200, "text/plain", "hello from esp8266!\r\n");
}

void configureRouter() {
  server.on("/", HTTP_GET, getState);
  server.on("/", HTTP_POST, changeState);
  #ifdef BMP280
    server.on("/temperature", HTTP_GET, getTemperature);
    server.on("/pressure", HTTP_GET, getPressure);
    // Get raw values (no units) for the server
    server.on("/temperatureRaw", HTTP_GET, getTemperatureRaw);
    server.on("/pressureRaw", HTTP_GET, getPressureRaw);
  #endif
}

// HTTP server helpers
String getStateText() {
  return getStateText(currentState);
}
String getStateText(int state) {
  return state == OPEN ? "open" : state == CLOSED_UP ? "closed-up" : "closed-down";
}

// REST route handlers
void getState() {
  server.send(200, "text/plain", getStateText());
}
void changeState() {
  String post_body = server.arg("plain");
  // Ensure the body is correct
  if (post_body != "0" && post_body != "1" && post_body != "2") {
    #ifdef DEBUG
      Serial.print("Invalid state ");
      Serial.print("'");
      Serial.print(post_body);
      Serial.println("'");
    #endif
    server.send(400, "text/plain", "Invalid state");
    return;
  }
  int newState = post_body.toInt();
  nextState = newState;
  #ifdef DEBUG
    Serial.print(getStateText(currentState));
    Serial.print(" -> ");
    Serial.println(getStateText(newState));
  #endif
  changeLED(currentState, LOW);
  moveTo(newState);
  String res = getStateText(currentState) + " -> " + getStateText(newState);
  server.send(200, "text/html", res);
}
#ifdef BMP280
  void getTemperature() {
    server.send(200, "text/plain", String(bmp.readTemperature()) + "°C");
  }
  void getPressure() {
    server.send(200, "text/plain", String(bmp.readPressure()) + "Pa");
  }
  void getTemperatureRaw() {
    server.send(200, "text/plain", String(bmp.readTemperature()));
  }
  void getPressureRaw() {
    server.send(200, "text/plain", String(bmp.readPressure()));
  }
#endif

// LED helpers
void changeLED(int state, bool ison) {
  if (state == 0) {
    digitalWrite(LED0, ison);
  } else if (state == 1) {
    digitalWrite(LED1, ison);
  } else {
    digitalWrite(LED2, ison);
  }
}

// Motor control helpers
void moveTo(int newState) {
  if (newState == currentState) {
    return;
  }
  int numRotations = currentState - newState;
  bool isForward = numRotations > 0;
  // Rotations always positive now that we have determined direction
  if (numRotations < 0) {
    numRotations = -numRotations;
  }
  // Start transition to newState
  ticks = 0;
  tickStop = numRotations * NUM_ENCODER_HOLES * GEAR_RATIO * NUM_WAND_ROTATIONS;
  #ifdef DEBUG
    Serial.print("ticks = ");
    Serial.print(ticks);
    Serial.print(" | tickStop = ");
    Serial.println(tickStop);
  #endif
  if (isForward) {
    Forward();
  } else {
    Backward();
  }
}
void Forward() {
  digitalWrite(UP, HIGH);
  digitalWrite(DOWN, LOW);
}
void Backward() {
  digitalWrite(UP, LOW);
  digitalWrite(DOWN, HIGH);
}
void Brake() {
  digitalWrite(UP, HIGH);
  digitalWrite(DOWN, HIGH);
}
void Stop() {
  digitalWrite(UP, LOW);
  digitalWrite(DOWN, LOW);
}