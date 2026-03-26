#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_NeoPixel.h>

WebServer server(80);

// ESP32 Hotspot
const char* AP_SSID = "ESP32_Robot";
const char* AP_PASSWORD = "12345678";

constexpr uint8_t SCREEN_WIDTH = 128;
constexpr uint8_t SCREEN_HEIGHT = 64;

// OLED
TwoWire I2C_1 = TwoWire(0);
TwoWire I2C_2 = TwoWire(1);

Adafruit_SSD1306 leftEye(SCREEN_WIDTH, SCREEN_HEIGHT, &I2C_1, -1);
Adafruit_SSD1306 rightEye(SCREEN_WIDTH, SCREEN_HEIGHT, &I2C_2, -1);

// Motor pins
constexpr uint8_t IN1 = 26;
constexpr uint8_t IN2 = 27;
constexpr uint8_t IN3 = 14;
constexpr uint8_t IN4 = 13;
constexpr uint8_t ENA = 25;
constexpr uint8_t ENB = 33;

// Mouth LED strip
constexpr uint8_t LED_PIN = 4;
constexpr uint8_t LED_COUNT = 4;
constexpr uint8_t BRIGHTNESS = 50;

Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

char currentCommand = 'S';
unsigned long lastCommandTime = 0;
constexpr unsigned long COMMAND_TIMEOUT = 700;

bool blinkState = false;
unsigned long blinkTimer = 0;
bool mouthEnabled = false;

unsigned long lastBlinkTime = 0;
unsigned long eyeStateChangedAt = 0;
bool eyeClosed = false;
constexpr unsigned long BLINK_INTERVAL = 4000;
constexpr unsigned long BLINK_DURATION = 180;

unsigned long stoppedSince = 0;
unsigned long lastRenderTime = 0;
constexpr unsigned long STOP_SAD_DELAY = 10000;
constexpr unsigned long RENDER_INTERVAL = 40;

bool oledReady = true;

enum FaceMode : uint8_t {
  FACE_NORMAL,
  FACE_HAPPY,
  FACE_NERVOUS,
  FACE_LOOK_LEFT,
  FACE_LOOK_RIGHT,
  FACE_SAD
};

bool isValidCommand(char cmd) {
  return cmd == 'F' || cmd == 'B' || cmd == 'L' || cmd == 'R' || cmd == 'S';
}

void setMotors(bool a1, bool a2, bool b1, bool b2, bool mouthOn) {
  digitalWrite(IN1, a1);
  digitalWrite(IN2, a2);
  digitalWrite(IN3, b1);
  digitalWrite(IN4, b2);

  mouthEnabled = mouthOn;

  if (!mouthOn) {
    blinkState = false;
  }
}

void moveForward()  { setMotors(HIGH, LOW, HIGH, LOW, true); }
void moveBackward() { setMotors(LOW, HIGH, LOW, HIGH, true); }
void moveLeft()     { setMotors(LOW, HIGH, HIGH, LOW, true); }
void moveRight()    { setMotors(HIGH, LOW, LOW, HIGH, true); }
void stopRobot()    { setMotors(LOW, LOW, LOW, LOW, false); }

void handleMovement() {
  switch (currentCommand) {
    case 'F': moveForward(); break;
    case 'B': moveBackward(); break;
    case 'L': moveLeft(); break;
    case 'R': moveRight(); break;
    default: stopRobot(); break;
  }
}

void safetyStop() {
  if (millis() - lastCommandTime >= COMMAND_TIMEOUT) {
    if (currentCommand != 'S') {
      currentCommand = 'S';
      stoppedSince = millis();
    }
  }
}

void handleBlink() {
  unsigned long now = millis();

  if (!eyeClosed && (now - lastBlinkTime >= BLINK_INTERVAL)) {
    eyeClosed = true;
    eyeStateChangedAt = now;
  } else if (eyeClosed && (now - eyeStateChangedAt >= BLINK_DURATION)) {
    eyeClosed = false;
    lastBlinkTime = now;
  }
}

FaceMode getFaceMode() {
  switch (currentCommand) {
    case 'F': return FACE_HAPPY;
    case 'B': return FACE_NERVOUS;
    case 'L': return FACE_LOOK_LEFT;
    case 'R': return FACE_LOOK_RIGHT;
    default:
      if (stoppedSince != 0 && millis() - stoppedSince >= STOP_SAD_DELAY) {
        return FACE_SAD;
      }
      return FACE_NORMAL;
  }
}

void drawEyeShell(Adafruit_SSD1306 &display, int16_t yOffset = 0) {
  display.fillRoundRect(18, 14 + yOffset, 92, 38, 18, WHITE);
  display.fillRoundRect(24, 18 + yOffset, 80, 30, 14, BLACK);
}

void drawPupil(Adafruit_SSD1306 &display, int16_t x, int16_t y, uint8_t r = 8) {
  display.fillCircle(x, y, r, WHITE);
  display.fillCircle(x - 2, y - 2, r / 2, BLACK);
  display.fillCircle(x + 3, y - 3, 2, BLACK);
}

void drawEyeClosed(Adafruit_SSD1306 &display, int16_t tilt) {
  display.drawLine(24, 34 - tilt, 104, 34 + tilt, WHITE);
  display.drawLine(24, 35 - tilt, 104, 35 + tilt, WHITE);
  display.drawLine(26, 32 - tilt, 102, 32 + tilt, WHITE);
}

void drawNormalEye(Adafruit_SSD1306 &display, int16_t pupilX, int16_t pupilY) {
  drawEyeShell(display);
  drawPupil(display, pupilX, pupilY);
  display.drawLine(22, 16, 48, 12, WHITE);
  display.drawLine(80, 12, 106, 16, WHITE);
}

void drawNormalFace() {
  if (eyeClosed) {
    drawEyeClosed(leftEye, 0);
    drawEyeClosed(rightEye, 0);
    return;
  }

  int16_t breathe = (millis() / 220) % 2;
  drawNormalEye(leftEye, 64, 33 + breathe);
  drawNormalEye(rightEye, 64, 33 + breathe);
}

void drawHappyFace() {
  if (eyeClosed) {
    drawEyeClosed(leftEye, -1);
    drawEyeClosed(rightEye, 1);
    return;
  }

  int16_t bounce = ((millis() / 180) % 3) - 1;
  drawEyeShell(leftEye, bounce);
  drawEyeShell(rightEye, bounce);
  drawPupil(leftEye, 64, 34 + bounce, 7);
  drawPupil(rightEye, 64, 34 + bounce, 7);
}

void drawNervousFace() {
  if (eyeClosed) {
    drawEyeClosed(leftEye, -2);
    drawEyeClosed(rightEye, 2);
    return;
  }

  int16_t shakeX = ((millis() / 70) % 3) - 1;
  int16_t shakeY = ((millis() / 90) % 3) - 1;
  drawEyeShell(leftEye);
  drawEyeShell(rightEye);
  drawPupil(leftEye, 64 + shakeX, 34 + shakeY, 6);
  drawPupil(rightEye, 64 - shakeX, 34 + shakeY, 6);
}

void drawLookingFace(bool lookLeft) {
  if (eyeClosed) {
    int16_t tilt = lookLeft ? -1 : 1;
    drawEyeClosed(leftEye, tilt);
    drawEyeClosed(rightEye, tilt);
    return;
  }

  int16_t sideX = lookLeft ? 48 : 80;
  drawNormalEye(leftEye, sideX, 28);
  drawNormalEye(rightEye, sideX, 28);
}

void drawSadFace() {
  if (eyeClosed) {
    drawEyeClosed(leftEye, 1);
    drawEyeClosed(rightEye, -1);
    return;
  }

  drawEyeShell(leftEye, 2);
  drawEyeShell(rightEye, 2);
  drawPupil(leftEye, 64, 38, 7);
  drawPupil(rightEye, 64, 38, 7);
}

void renderFace(FaceMode mode) {
  if (!oledReady) {
    return;
  }

  leftEye.clearDisplay();
  rightEye.clearDisplay();

  switch (mode) {
    case FACE_HAPPY: drawHappyFace(); break;
    case FACE_NERVOUS: drawNervousFace(); break;
    case FACE_LOOK_LEFT: drawLookingFace(true); break;
    case FACE_LOOK_RIGHT: drawLookingFace(false); break;
    case FACE_SAD: drawSadFace(); break;
    default: drawNormalFace(); break;
  }

  leftEye.display();
  rightEye.display();
}

void updateFace() {
  if (!oledReady) {
    return;
  }

  unsigned long now = millis();
  if (now - lastRenderTime < RENDER_INTERVAL) {
    return;
  }

  lastRenderTime = now;
  renderFace(getFaceMode());
}

void turnOffMouthStrip() {
  for (int i = 0; i < LED_COUNT; i++) {
    strip.setPixelColor(i, 0);
  }
  strip.show();
  blinkState = false;
}

void updateMouthStrip() {
  if (!mouthEnabled) {
    turnOffMouthStrip();
    return;
  }

  if (millis() - blinkTimer >= 500) {
    blinkTimer = millis();
    blinkState = !blinkState;

    for (int i = 0; i < LED_COUNT; i++) {
      strip.setPixelColor(i, blinkState ? strip.Color(255, 0, 0) : 0);
    }
    strip.show();
  }
}

void handleRoot() {
  String page =
    "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<style>"
    "body{font-family:Arial;text-align:center;background:#111;color:#fff;margin-top:30px;}"
    "button{width:110px;height:70px;font-size:24px;margin:8px;border:none;border-radius:14px;}"
    ".f{background:#2ecc71;} .b{background:#e67e22;} .l,.r{background:#3498db;} .s{background:#e74c3c;}"
    "</style></head><body>"
    "<h2>ESP32 Robot Hotspot Control</h2>"
    "<p><button class='f' onclick=\"sendCmd('F')\">Forward</button></p>"
    "<p>"
    "<button class='l' onclick=\"sendCmd('L')\">Left</button>"
    "<button class='s' onclick=\"sendCmd('S')\">Stop</button>"
    "<button class='r' onclick=\"sendCmd('R')\">Right</button>"
    "</p>"
    "<p><button class='b' onclick=\"sendCmd('B')\">Back</button></p>"
    "<script>"
    "function sendCmd(c){fetch('/cmd?move='+c).catch(()=>{});}"
    "</script>"
    "</body></html>";

  server.send(200, "text/html", page);
}

void handleCommand() {
  if (!server.hasArg("move")) {
    server.send(400, "text/plain", "Missing move");
    return;
  }

  char cmd = server.arg("move")[0];
  if (!isValidCommand(cmd)) {
    server.send(400, "text/plain", "Invalid command");
    return;
  }

  currentCommand = cmd;
  lastCommandTime = millis();

  if (cmd == 'S') {
    stoppedSince = millis();
  } else {
    stoppedSince = 0;
  }

  Serial.print("Hotspot command: ");
  Serial.println(cmd);

  server.send(200, "text/plain", "OK");
}

void setup() {
  Serial.begin(115200);

  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);
  pinMode(ENA, OUTPUT);
  pinMode(ENB, OUTPUT);

  digitalWrite(ENA, HIGH);
  digitalWrite(ENB, HIGH);

  strip.begin();
  strip.setBrightness(BRIGHTNESS);
  strip.clear();
  strip.show();

  stopRobot();

  I2C_1.begin(21, 22);
  I2C_2.begin(18, 19);

  bool leftOk = leftEye.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  bool rightOk = rightEye.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  oledReady = leftOk && rightOk;

  unsigned long now = millis();
  lastCommandTime = now;
  lastBlinkTime = now;
  eyeStateChangedAt = now;
  stoppedSince = now;

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);

  Serial.println();
  Serial.print("ESP32 Hotspot Name: ");
  Serial.println(AP_SSID);
  Serial.print("ESP32 Hotspot Password: ");
  Serial.println(AP_PASSWORD);
  Serial.print("Open this IP in phone browser: ");
  Serial.println(WiFi.softAPIP());

  server.on("/", handleRoot);
  server.on("/cmd", handleCommand);
  server.begin();

  updateFace();
  Serial.println("ESP32 Robot Hotspot Ready");
}

void loop() {
  server.handleClient();
  safetyStop();
  handleMovement();
  handleBlink();
  updateFace();
  updateMouthStrip();
}
