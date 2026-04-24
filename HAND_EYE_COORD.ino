#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_TinyUSB.h>
#include <bluefruit.h>

// ---------------- OLED ----------------
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ---------------- SHIFT REGISTER ----------------
#define LATCH_PIN  A3
#define CLOCK_PIN  A2
#define DATA_PIN   D6

// ---------------- INPUTS ----------------
#define LADDER_PIN A0
#define START_PIN  A1

// Button ladder values
int btnVal[6] = {3680, 1850, 1250, 950, 750, 650};
int tol = 50;

// Game constants
const unsigned long GAME_DURATION = 60000UL;
const unsigned long LED_WINDOW_MS = 1000UL;

// =============================
// GAME STATE MACHINE
// =============================
enum GameState {
  STATE_WAIT_SESSION,   // BLE connected, waiting for START_TEST_SESSION
  STATE_WAIT_START,     // session active (or offline), waiting for START button
  STATE_RUNNING,        // test in progress
  STATE_DONE            // test over, showing result
};
GameState gameState = STATE_WAIT_START;  // offline default

int  score         = 0;
int  currentLED    = 0;
unsigned long gameStartMs  = 0;
unsigned long ledStartMs   = 0;
bool          ledActive    = false;

// =============================
// BLE
// =============================
BLEUart bleuart;
bool   testSessionActive = false;
String playerID          = "";

// =============================
// FUNCTION PROTOTYPES
// =============================
void handleBLE();
void sendBLE(String msg);
void onBLEReceived(String cmd);
void pickNextLED();
void clearOutputs();
void sendByte(byte v);
void fullReset595();
bool startPressed();
int  readGameButton();
void updateDisplay();
void connect_callback(uint16_t conn_handle);
void disconnect_callback(uint16_t conn_handle, uint8_t reason);

// ---------------- SHIFT REGISTER ----------------
void sendByte(byte v) {
  digitalWrite(LATCH_PIN, LOW);
  shiftOut(DATA_PIN, CLOCK_PIN, MSBFIRST, v);
  digitalWrite(LATCH_PIN, HIGH);
}

void fullReset595() {
  for (int i = 0; i < 20; i++) {
    sendByte(0x00);
    delayMicroseconds(300);
  }
}

void clearOutputs() {
  sendByte(0x00);
}

// ---------------- BUTTONS ----------------
bool startPressed() {
  return analogRead(START_PIN) > 2900;
}

int readGameButton() {
  int v = analogRead(LADDER_PIN);
  delay(3);
  v = analogRead(LADDER_PIN);
  for (int i = 0; i < 6; i++) {
    if (v >= btnVal[i] - tol && v <= btnVal[i] + tol) return i + 1;
  }
  return 0;
}

// ---------------- SETUP ----------------
void setup() {
  Serial.begin(115200);
  analogReadResolution(12);
  delay(300);

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.display();

  pinMode(LATCH_PIN, OUTPUT);
  pinMode(CLOCK_PIN, OUTPUT);
  pinMode(DATA_PIN,  OUTPUT);
  digitalWrite(LATCH_PIN, HIGH);
  digitalWrite(CLOCK_PIN, LOW);
  digitalWrite(DATA_PIN,  LOW);

  fullReset595();
  clearOutputs();

  // ---- BLE ----
  Bluefruit.begin();
  Bluefruit.setConnLedInterval(250);
  Bluefruit.Periph.setConnectCallback(connect_callback);
  Bluefruit.Periph.setDisconnectCallback(disconnect_callback);
  Bluefruit.setName("HAND_EYE_COORD");
  Bluefruit.setTxPower(4);
  bleuart.begin();
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addName();
  Bluefruit.Advertising.addService(bleuart);
  Bluefruit.ScanResponse.addName();
  Bluefruit.ScanResponse.addService(bleuart);
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(160, 160);
  Bluefruit.Advertising.setFastTimeout(30);
  Bluefruit.Advertising.start(0);

  gameState = STATE_WAIT_START;
  Serial.println("System Ready");
}

// =============================
// LOOP — non-blocking, like HILPCS
// =============================
void loop() {

  // 1. Always handle BLE first
  handleBLE();

  // 2. State machine
  switch (gameState) {

    // ── Waiting for session (online, no session yet) ──────
    case STATE_WAIT_SESSION:
      // just show wait screen, BLE handler will flip to WAIT_START
      // when START_TEST_SESSION arrives
      updateDisplay();
      break;

    // ── Waiting for START button ──────────────────────────
    case STATE_WAIT_START:
      updateDisplay();
      if (startPressed()) {
        delay(300); // debounce
        // start the test
        score        = 0;
        gameStartMs  = millis();
        ledActive    = false;
        gameState    = STATE_RUNNING;
        Serial.println("TEST START");
        pickNextLED();
      }
      break;

    // ── Test running ──────────────────────────────────────
    case STATE_RUNNING: {
      unsigned long now = millis();

      // RESET button mid-test
      if (startPressed()) {
        clearOutputs();
        ledActive = false;
        delay(300);
        Serial.println("TEST RESET");
        // if online+session → go back to wait start
        // if offline → go back to wait start
        gameState = STATE_WAIT_START;
        break;
      }

      // session stopped mid-test (online only)
      if (Bluefruit.connected() && !testSessionActive) {
        clearOutputs();
        ledActive = false;
        Serial.println("TEST ABORTED — session stopped");
        gameState = STATE_WAIT_SESSION;
        break;
      }

      // time up
      // time up
      if (now - gameStartMs >= GAME_DURATION) {
        clearOutputs();
        ledActive = false;
        Serial.print("TEST OVER | Score: "); Serial.println(score);
        Serial.print("BLE connected: "); Serial.println(Bluefruit.connected());
        Serial.print("testSessionActive: "); Serial.println(testSessionActive);

        // send result over BLE if session active
        if (Bluefruit.connected() && testSessionActive) {
          char buf[100];
          snprintf(buf, sizeof(buf),
            "{\"type\":\"test_result\",\"player\":\"%s\",\"score\":%d}",
            playerID.c_str(), score);
          sendBLE(String(buf));
          Serial.println("Result sent over BLE");
        }
        gameState = STATE_DONE;
        break;
      }
      // LED window expired — no hit, pick next LED
      if (ledActive && now - ledStartMs >= LED_WINDOW_MS) {
        clearOutputs();
        ledActive = false;
        delay(150);
        pickNextLED();
      }

      // check button
      if (ledActive) {
        int btn = readGameButton();
        if (btn == currentLED) {
          score++;
          Serial.print("HIT | Score = "); Serial.println(score);
          clearOutputs();
          ledActive = false;
          delay(150);
          pickNextLED();
        }
      }

      updateDisplay();
      break;
    }

    // ── Test done — show result ───────────────────────────
    case STATE_DONE:
      updateDisplay();
      // START button → new test (if session active or offline)
      if (startPressed()) {
        delay(300);
        if (!Bluefruit.connected() || testSessionActive) {
          gameState = STATE_WAIT_START;
        } else {
          gameState = STATE_WAIT_SESSION;
        }
      }
      // session stopped at result screen
      if (Bluefruit.connected() && !testSessionActive) {
        gameState = STATE_WAIT_SESSION;
      }
      break;
  }
}

// =============================
// PICK NEXT LED
// =============================
void pickNextLED() {
  currentLED = random(1, 7);
  clearOutputs();
  delay(30);
  sendByte(1 << (currentLED + 1));
  ledStartMs = millis();
  ledActive  = true;
  Serial.print("LED "); Serial.println(currentLED);
}

// =============================
// DISPLAY
// =============================
void updateDisplay() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  switch (gameState) {

    case STATE_WAIT_SESSION:
      display.setTextSize(1);
      display.setCursor(0, 10); display.println("BLE Connected");
      display.setCursor(0, 28); display.println("Start session");
      display.setCursor(0, 40); display.println("in the App");
      break;

    case STATE_WAIT_START:
      display.setTextSize(1);
      display.setCursor(10, 20); display.println("Reaction Test");
      display.setCursor(10, 40); display.println("Press START");
      break;

    case STATE_RUNNING: {
      int timeLeft = (GAME_DURATION - (millis() - gameStartMs)) / 1000;
      display.setTextSize(1);
      display.setCursor(0, 0);
      display.print("Time: "); display.print(timeLeft); display.println("s");
      display.setCursor(0, 12);
      display.print("Score: "); display.println(score);
      display.setCursor(0, 26);
      display.print("LED: "); display.println(currentLED);
      break;
    }

    case STATE_DONE:
      display.setTextSize(2);
      display.setCursor(10, 10); display.println("TEST OVER");
      display.setTextSize(1);
      display.setCursor(10, 40);
      display.print("Score: "); display.println(score);
      display.setCursor(10, 55);
      if (Bluefruit.connected() && testSessionActive)
        display.println("Press START again");
      else if (Bluefruit.connected() && !testSessionActive)
        display.println("Session stopped");
      else
        display.println("Press START");
      break;
  }

  // BLE dots — bottom right, same as HILPCS
  if (Bluefruit.connected())   display.fillCircle(125, 61, 2, SSD1306_WHITE);
  if (testSessionActive)       display.fillCircle(119, 61, 2, SSD1306_WHITE);

  display.display();
}

// =============================
// BLE HANDLER — same pattern as HILPCS
// =============================
void handleBLE() {
  static bool wasConnected = false;
  bool isConnected = Bluefruit.connected();

  if (wasConnected && !isConnected) {
    testSessionActive = false;
    playerID          = "";
    // if mid-test, reset cleanly
    if (gameState == STATE_RUNNING) {
      clearOutputs();
      ledActive = false;
    }
    gameState = STATE_WAIT_START; // go offline mode
    Serial.println("BLE disconnected — session cleared");
  }
  wasConnected = isConnected;

  if (!bleuart.available()) return;
  String cmd = bleuart.readStringUntil('\n');
  cmd.trim();
  if (cmd.length() > 0) onBLEReceived(cmd);
}

void onBLEReceived(String cmd) {
  if (cmd.startsWith("START_TEST_SESSION,")) {
    playerID          = cmd.substring(19);
    testSessionActive = true;
    gameState         = STATE_WAIT_START;
    sendBLE("{\"status\":\"test_session_started\",\"player\":\"" + playerID + "\"}");
    Serial.println("Test session started: " + playerID);
  }
  else if (cmd == "STOP_TEST_SESSION") {
    testSessionActive = false;
    if (gameState == STATE_RUNNING) {
      clearOutputs();
      ledActive = false;
    }
    gameState = STATE_WAIT_SESSION;
    sendBLE("{\"status\":\"test_session_stopped\",\"player\":\"" + playerID + "\"}");
    Serial.println("Test session stopped: " + playerID);
    playerID = "";
  }
}

// =============================
// SEND BLE — same as HILPCS
// =============================



void sendBLE(String msg) {
  if (!Bluefruit.connected()) return;
  Serial.print("SENDING: "); Serial.println(msg);  // <-- add this
  int len = msg.length();
  int i   = 0;
  while (i < len) {
    int chunkSize = min(20, len - i);
    bleuart.write((const uint8_t*)msg.c_str() + i, chunkSize);
    bleuart.flush();
    i += chunkSize;
    delay(100);
  }
  bleuart.write((const uint8_t*)"\r\n", 2);
  bleuart.flush();
}

// =============================
// BLE CALLBACKS
// =============================
void connect_callback(uint16_t conn_handle) {
  // when BLE connects, go to wait session if no session active
  if (!testSessionActive) gameState = STATE_WAIT_SESSION;
  Serial.println("BLE Connected!");
}

void disconnect_callback(uint16_t conn_handle, uint8_t reason) {
  Serial.print("BLE Disconnected, reason = 0x");
  Serial.println(reason, HEX);
  Bluefruit.Advertising.start(0);
}
