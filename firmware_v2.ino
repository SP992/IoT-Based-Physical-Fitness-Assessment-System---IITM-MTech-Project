#include <bluefruit.h>
#include <Wire.h>
#include <SPI.h>
#include <SdFat.h>
#include <VL53L0X.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <LSM6DS3.h>
#include <math.h>

// =============================
// DISPLAY / SD CONSTANTS
// =============================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET   -1
#define SD_CS_PIN     D0
const float g_const = 9.81f;
const float CAL_A   = 0.992805f;
const float CAL_B   = 2.809740f;

// =============================
// RULER DROP DSP PARAMETERS
// =============================
#define FS_IMU         200
#define WINDOW_SEC     3.0
#define HOP_SEC        0.5
#define WIN_SAMPLES    (int)(FS_IMU * WINDOW_SEC)
#define HOP_SAMPLES    (int)(FS_IMU * HOP_SEC)
#define FREEFALL_THR_G 0.1f

// =============================
// SECANT ALGORITHM PARAMETERS
// =============================
#define ANCHOR_PRE  7
#define ANCHOR_POST 10
#define SEG_PRE     30
#define SEG_POST    80

// =============================
// SIT & REACH ALGORITHM PARAMETERS
// =============================
#define SR_IDLE_WINDOW_MS     5000   // 5s stable → fix start point
#define SR_REACH_THRESHOLD_CM 7.0f  // must reduce by 7cm to count as reach
#define SR_IDLE_TOL_CM        2.0f  // ±2cm = "idle"
#define SR_FINAL_HOLD_MS      6000  // 6s stable → lock final distance
#define SR_LOG_INTERVAL_MS    500   // log ToF to SD every 500ms
#define SR_SHAKE_POS_THR      15.0f // +15g threshold for shake cancel
#define SR_SHAKE_NEG_THR     -15.0f // -15g threshold for shake cancel
#define SR_SHAKE_POS_COUNT    3     // need 3 positive crossings
#define SR_SHAKE_NEG_COUNT    3     // need 3 negative crossings

// =============================
// BLE
// =============================
BLEUart bleuart;

// =============================
// OBJECTS
// =============================
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
VL53L0X sensor;
SdFat32 sd;
File32  logFile;
LSM6DS3 imu(I2C_MODE, 0x6A);
bool imuAvailable = false;

// =============================
// MODE MANAGEMENT
// =============================
enum DeviceMode { MODE_RULER_DROP, MODE_SIT_REACH };
DeviceMode currentMode;
DeviceMode candidateMode;
unsigned long modeStableStart = 0;
const unsigned long MODE_HOLD_TIME = 5000;

// =============================
// BLE STATE
// =============================
bool   rdSessionActive = false;
bool   srSessionActive = false;
String playerID        = "";

// =============================
// RULER DROP BUFFERS (untouched)
// =============================
float    imuZBuf[WIN_SAMPLES];
float    tofBuf[WIN_SAMPLES];
uint16_t bufIdx      = 0;
uint16_t sampleCount = 0;
bool     windowHasFreeFall = false;

// =============================
// RULER DROP RESULTS (untouched)
// =============================
float  lastDropDistanceCM = -1.0f;
String currentFilename    = "";
String previousFilename   = "";

// =============================
// SIT & REACH STATE MACHINE
// =============================
enum SRPhase {
  SR_PHASE_IDLE,          // waiting to begin (online: waiting for session start)
  SR_PHASE_FIX_START,     // waiting for device to be stable 5s → lock startH
  SR_PHASE_WAIT_REACH,    // waiting for ≥7cm reduction from startH
  SR_PHASE_WAIT_FINAL,    // device reached, waiting for 6s stable → lock finalH
  SR_PHASE_DONE           // valid result displayed, waiting for auto-restart
};

SRPhase  srPhase          = SR_PHASE_IDLE;
float    srStartH         = 0.0f;   // locked start distance (cm)
float    srFinalH         = 0.0f;   // locked final distance (cm)
float    srLastResult     = -1.0f;  // last valid displacement (cm)

// --- Idle timer (used for both Phase 1 and Phase 3) ---
unsigned long srIdleTimerStart  = 0;
float         srIdleAnchorDist  = 0.0f;  // distance when idle timer started
bool          srIdleTimerActive = false;

// --- Dot countdown (Phase 3 display) ---
int           srDotsRemaining   = 6;
unsigned long srLastDotTick     = 0;

// --- SD logging timer ---
unsigned long srLastLogMs       = 0;

// --- Shake-cancel state ---
bool  srShakeActive     = false;  // true when cancel detection is armed
float srPrevAY          = 0.0f;   // previous Y-accel sample for edge detection
int   srShakePosCount   = 0;      // positive threshold crossings
int   srShakeNegCount   = 0;      // negative threshold crossings

// =============================
// FUNCTION PROTOTYPES
// =============================
void   showMessage(String, String = "", int = 2000);
String getNextFileWithPrefix(String);
bool   createNewFile(String);
void   updateModeFromIMU(DeviceMode);
void   switchMode(DeviceMode);
void   handleSerialCommands();
void   handleBLE();
void   sendBLE(String);
void   onBLEReceived(String);
void   streamFileBLE(String, String, String);
float  runRulerDropAlgorithm(float*, float*, float&, float&, int&, int&);

// SR helpers
void   srBeginPhase(SRPhase);
void   srCheckShake(float ay);
void   srResetShakeCounters();
void   srInvalidate();
void   srWriteResult();
void   srDisplayPhase(float distCm);

// =============================
// SETUP
// =============================
void setup() {
  Serial.begin(115200);
  Wire.begin();
  Wire.setClock(400000);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { while (1); }
  display.setRotation(2);
  display.clearDisplay();
  display.display();

  showMessage("Booting...", "Please wait", 1500);

  if (!sd.begin(SD_CS_PIN, SD_SCK_MHZ(10))) {
    showMessage("SD Error!", "Check SD card", 5000);
    while (1);
  }

  if (!sensor.init()) {
    showMessage("TOF Error!", "Sensor missing", 5000);
    while (1);
  }
  sensor.setTimeout(10);
  sensor.startContinuous();

  if (imu.begin() == 0) {
    imuAvailable = true;
  } else {
    showMessage("IMU Error!", "Check hardware", 2000);
  }

  // BLE
  Bluefruit.begin();
  Bluefruit.setConnLedInterval(250);
  Bluefruit.Periph.setConnectCallback(connect_callback);
  Bluefruit.Periph.setDisconnectCallback(disconnect_callback);
  Bluefruit.setName("HILPCS");
  Bluefruit.setTxPower(4);
  bleuart.begin();
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addName();
  Bluefruit.Advertising.addService(bleuart);
  Bluefruit.ScanResponse.addName();
  Bluefruit.Advertising.addService(bleuart);
  Bluefruit.ScanResponse.addService(bleuart);
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(160, 160);
  Bluefruit.Advertising.setFastTimeout(30);
  Bluefruit.Advertising.start(0);

  // mode detection
  float az = 0, ay = 0;
  if (imuAvailable) {
    az = imu.readFloatAccelZ();
    ay = imu.readFloatAccelY();
  }
  if      (fabs(az) > 0.9f) currentMode = MODE_RULER_DROP;
  else if (fabs(ay) > 0.9f) currentMode = MODE_SIT_REACH;
  else {
    showMessage("Hold steady", "Adjust orientation", 3000);
    if (imuAvailable) {
      az = imu.readFloatAccelZ();
      ay = imu.readFloatAccelY();
    }
    currentMode = (fabs(ay) > 0.9f) ? MODE_SIT_REACH : MODE_RULER_DROP;
  }
  candidateMode = currentMode;

  if (currentMode == MODE_RULER_DROP)
    currentFilename = getNextFileWithPrefix("trial_");
  else
    currentFilename = getNextFileWithPrefix("SandR_");

  createNewFile(currentFilename);

  // If booting into SR mode offline, start immediately
  if (currentMode == MODE_SIT_REACH)
    srBeginPhase(SR_PHASE_FIX_START);

  showMessage("READY!", currentFilename, 2000);
}

// =============================
// LOOP
// =============================
void loop() {

  handleSerialCommands();
  handleBLE();


  // static unsigned long lastLoopTime = 0;
  // static unsigned long loopCount = 0;
  // loopCount++;
  // if (millis() - lastLoopTime >= 1000) {
  //   Serial.print("Loop Hz: ");
  //   Serial.println(loopCount);
  //   loopCount = 0;
  //   lastLoopTime = millis();}

    
  // ===== 1. Read IMU =====
  float ax = 0, ay = 0, az = 1.0f;
  if (imuAvailable) {
    ax = imu.readFloatAccelX();
    ay = imu.readFloatAccelY();
    az = imu.readFloatAccelZ();
  }

  // ===== 2. Mode detection =====
  DeviceMode detected = currentMode;
  if      (fabs(az) > 0.9f) detected = MODE_RULER_DROP;
  else if (fabs(ay) > 0.9f) detected = MODE_SIT_REACH;
  updateModeFromIMU(detected);

  // ===== 3. Read ToF =====
  int distance_mm = sensor.readRangeContinuousMillimeters();
  if (sensor.timeoutOccurred()) return;
  float measured_cm = distance_mm / 10.0f;
  float distance_cm = (measured_cm - CAL_B) / CAL_A;
  if (distance_cm < 0) distance_cm = 0;

  // ===== 4. Ruler Drop Buffer (untouched) =====
  imuZBuf[bufIdx] = az;
  tofBuf[bufIdx]  = distance_cm;
  if (fabs(az) < FREEFALL_THR_G) windowHasFreeFall = true;
  bufIdx = (bufIdx + 1) % WIN_SAMPLES;
  if (sampleCount < WIN_SAMPLES) sampleCount++;

  // ===== 5. SIT & REACH STATE MACHINE =====
  if (currentMode == MODE_SIT_REACH) {

    // --- Shake cancel check (active in Phase 2, 3) ---
    if (srShakeActive && imuAvailable) {
      srCheckShake(ay);
    }

    switch (srPhase) {

      // --------------------------------------------------
      case SR_PHASE_IDLE:
        // Online: wait for START_SR_SESSION command (handled in BLE)
        // Offline: should not stay here — srBeginPhase called at boot/mode switch
        srDisplayPhase(distance_cm);
        break;

      // --------------------------------------------------
      case SR_PHASE_FIX_START: {
        // Check if distance is stable ±2cm from anchor
        if (!srIdleTimerActive) {
          // First sample — anchor here
          srIdleAnchorDist  = distance_cm;
          srIdleTimerStart  = millis();
          srIdleTimerActive = true;
        } else {
          if (fabs(distance_cm - srIdleAnchorDist) > SR_IDLE_TOL_CM) {
            // Moved — reset idle timer with new anchor
            srIdleAnchorDist  = distance_cm;
            srIdleTimerStart  = millis();
          } else if (millis() - srIdleTimerStart >= SR_IDLE_WINDOW_MS) {
            // Stable for 5s — lock start point
            srStartH = distance_cm;
            Serial.print("SR: startH locked = ");
            Serial.println(srStartH);
            srBeginPhase(SR_PHASE_WAIT_REACH);
          }
        }
        srDisplayPhase(distance_cm);
        break;
      }

      // --------------------------------------------------
      case SR_PHASE_WAIT_REACH: {
        // Wait until distance drops ≥7cm from startH
        float reduction = srStartH - distance_cm;
        if (reduction >= SR_REACH_THRESHOLD_CM) {
          Serial.println("SR: reach threshold crossed, entering Phase 3");
          srBeginPhase(SR_PHASE_WAIT_FINAL);
          // Fall through immediately into WAIT_FINAL this cycle
          // (handled next loop iteration is fine)
        }

        // SD logging at 500ms interval
        if (millis() - srLastLogMs >= SR_LOG_INTERVAL_MS) {
          srLastLogMs = millis();
          if (logFile.open(currentFilename.c_str(), O_WRITE | O_AT_END)) {
            logFile.print(millis());
            logFile.print(",");
            logFile.println(distance_cm, 2);
            logFile.close();
          }
        }
        srDisplayPhase(distance_cm);
        break;
      }

      // --------------------------------------------------
      case SR_PHASE_WAIT_FINAL: {
        // Check idle ±2cm; if stable for 6s → lock finalH
        if (!srIdleTimerActive) {
          srIdleAnchorDist  = distance_cm;
          srIdleTimerStart  = millis();
          srIdleTimerActive = true;
          srDotsRemaining   = 6;
          srLastDotTick     = millis();
        } else {
          if (fabs(distance_cm - srIdleAnchorDist) > SR_IDLE_TOL_CM) {
            // Moved — reset idle timer and dots
            srIdleAnchorDist  = distance_cm;
            srIdleTimerStart  = millis();
            srDotsRemaining   = 6;
            srLastDotTick     = millis();
          } else {
            // Stable — tick down dots every 1s
            if (srDotsRemaining > 0 && millis() - srLastDotTick >= 1000) {
              srDotsRemaining--;
              srLastDotTick = millis();
            }
            if (millis() - srIdleTimerStart >= SR_FINAL_HOLD_MS) {
              // Locked!
              srFinalH = distance_cm;
              Serial.print("SR: finalH locked = ");
              Serial.println(srFinalH);
              srWriteResult();
              srBeginPhase(SR_PHASE_DONE);
            }
          }
        }

        // SD logging at 500ms interval
        if (millis() - srLastLogMs >= SR_LOG_INTERVAL_MS) {
          srLastLogMs = millis();
          if (logFile.open(currentFilename.c_str(), O_WRITE | O_AT_END)) {
            logFile.print(millis());
            logFile.print(",");
            logFile.println(distance_cm, 2);
            logFile.close();
          }
        }
        srDisplayPhase(distance_cm);
        break;
      }

      // --------------------------------------------------
      case SR_PHASE_DONE:
        // Display result briefly, then auto-restart for next test
        srDisplayPhase(distance_cm);
        // Auto-restart after 3 seconds
        if (millis() - srIdleTimerStart >= 3000) {
          // Online: only restart if session still active
          if (!Bluefruit.connected() || srSessionActive) {
            previousFilename = currentFilename;
            currentFilename  = getNextFileWithPrefix("SandR_");
            createNewFile(currentFilename);
            srBeginPhase(SR_PHASE_FIX_START);
          }
          // Online + no session: go idle
          else {
            srBeginPhase(SR_PHASE_IDLE);
          }
        }
        break;
    }

  }

  // ===== 6. RULER DROP — Window hop (completely untouched) =====
  static uint16_t hopCounter = 0;
  hopCounter++;

  if (sampleCount == WIN_SAMPLES && hopCounter >= HOP_SAMPLES) {
    hopCounter = 0;

    if (windowHasFreeFall && currentMode == MODE_RULER_DROP) {

      bool shouldProcess = !Bluefruit.connected() || rdSessionActive;

      if (shouldProcess) {
        float startH = 0, catchH = 0;
        int   startI = 0, catchI = 0;

        float d = runRulerDropAlgorithm(imuZBuf, tofBuf,
                                         startH, catchH,
                                         startI, catchI);

        if (d > 0) {
          lastDropDistanceCM = d;
          float t_ms = sqrtf(2.0f * (d / 100.0f) / g_const) * 1000.0f;

          if (logFile.open(currentFilename.c_str(), O_WRITE | O_AT_END)) {
            logFile.println("---WINDOW---");
            for (int i = 0; i < WIN_SAMPLES; i++) {
              logFile.print(imuZBuf[i], 4);
              logFile.print(",");
              logFile.println(tofBuf[i], 2);
            }
            logFile.println("---END---");
            logFile.print("PLAYER_ID,"); logFile.println(playerID);
            logFile.print("START_IDX,"); logFile.println(startI);
            logFile.print("CATCH_IDX,"); logFile.println(catchI);
            logFile.print("START_CM,");  logFile.println(startH, 2);
            logFile.print("CATCH_CM,");  logFile.println(catchH, 2);
            logFile.print("DISP_CM,");   logFile.println(d, 2);
            logFile.print("REACT_MS,");  logFile.println(t_ms, 2);
            logFile.close();
          }

          if (Bluefruit.connected() && rdSessionActive) {
            char buf[128];
            snprintf(buf, sizeof(buf),
              "{\"type\":\"rd_result\",\"player\":\"%s\","
              "\"drop_cm\":%.1f,\"t_ms\":%.0f,"
              "\"start_cm\":%.1f,\"catch_cm\":%.1f}",
              playerID.c_str(), d, t_ms, startH, catchH);
            sendBLE(String(buf));
          }

          previousFilename = currentFilename;
          currentFilename  = getNextFileWithPrefix("trial_");
          createNewFile(currentFilename);

          Serial.print("Drop=");     Serial.print(d);
          Serial.print("cm  t=");    Serial.print(t_ms);
          Serial.print("ms  file="); Serial.println(previousFilename);
        }
      }
    }
    windowHasFreeFall = false;
  }

  // ===== 7. Display — Ruler Drop mode (untouched) =====
  if (currentMode == MODE_RULER_DROP) {
    display.clearDisplay();

    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("Ruler Drop");

    display.setTextSize(2);
    display.setCursor(0, 12);
    display.print(distance_cm, 1);
    display.println(" cm");

    if (lastDropDistanceCM > 0) {
      display.setTextSize(1);
      display.setCursor(0, 34);
      display.print("Last: ");
      display.print(lastDropDistanceCM, 1);
      display.print("cm ");
      display.print(sqrtf(2.0f*(lastDropDistanceCM/100.0f)/g_const)*1000.0f, 0);
      display.println("ms");
    }

    display.setTextSize(1);
    display.setCursor(0, 47);
    display.print(previousFilename != "" ? previousFilename : "no saves yet");

    display.setCursor(0, 57);
    display.print(">");
    display.print(currentFilename);

    if (Bluefruit.connected())              display.fillCircle(125, 61, 2, SSD1306_WHITE);
    if (rdSessionActive || srSessionActive) display.fillCircle(119, 61, 2, SSD1306_WHITE);

    display.display();
  }
  // SR display is handled inside srDisplayPhase() called from the state machine above
}

// =====================================================
// SIT & REACH HELPERS
// =====================================================

// Called whenever we transition to a new SR phase
void srBeginPhase(SRPhase newPhase) {
  srPhase           = newPhase;
  srIdleTimerActive = false;
  srIdleTimerStart  = millis();  // also used as "phase entered" timestamp in DONE

  switch (newPhase) {
    case SR_PHASE_IDLE:
      srShakeActive   = false;
      srResetShakeCounters();
      break;

    case SR_PHASE_FIX_START:
      srShakeActive   = false;    // shake not active before start is fixed
      srResetShakeCounters();
      srLastLogMs     = 0;        // reset log timer
      break;

    case SR_PHASE_WAIT_REACH:
      srShakeActive   = true;     // arm shake-cancel from here
      srResetShakeCounters();
      srLastLogMs     = millis(); // start logging timer
      break;

    case SR_PHASE_WAIT_FINAL:
      srShakeActive   = true;     // still armed
      srDotsRemaining = 6;
      srLastDotTick   = millis();
      srLastLogMs     = millis();
      break;

    case SR_PHASE_DONE:
      srShakeActive   = false;    // valid result — disable cancel
      srResetShakeCounters();
      srIdleTimerStart = millis(); // reuse as "done entered" for 3s auto-restart
      break;
  }
}

// Check Y-axis acceleration for shake-cancel gesture
// Called every loop iteration when srShakeActive == true
void srCheckShake(float ay) {
  // Detect rising edge past +15g
  if (srPrevAY < SR_SHAKE_POS_THR && ay >= SR_SHAKE_POS_THR) {
    srShakePosCount++;
    Serial.print("SR shake +cross #"); Serial.println(srShakePosCount);
  }
  // Detect falling edge past -15g
  if (srPrevAY > SR_SHAKE_NEG_THR && ay <= SR_SHAKE_NEG_THR) {
    srShakeNegCount++;
    Serial.print("SR shake -cross #"); Serial.println(srShakeNegCount);
  }
  srPrevAY = ay;

  if (srShakePosCount >= SR_SHAKE_POS_COUNT &&
      srShakeNegCount >= SR_SHAKE_NEG_COUNT) {
    Serial.println("SR: shake invalidate triggered");
    srInvalidate();
  }
}

void srResetShakeCounters() {
  srShakePosCount = 0;
  srShakeNegCount = 0;
  srPrevAY        = 0.0f;
}

// Erase current file contents, write fresh header, restart Phase 1
void srInvalidate() {
  // Truncate and rewrite header
  if (logFile.open(currentFilename.c_str(), O_WRITE | O_TRUNC)) {
    logFile.println("# Sit & Reach");
    logFile.println("time_ms,distance_cm");
    logFile.close();
  }
  Serial.println("SR: file invalidated, restarting");
  srBeginPhase(SR_PHASE_FIX_START);
}

// Called when finalH is locked — append summary, send BLE if session active
void srWriteResult() {
  float disp = srStartH - srFinalH;
  srLastResult = disp;

  // Append result summary to SD file
  if (logFile.open(currentFilename.c_str(), O_WRITE | O_AT_END)) {
    logFile.println("---RESULT---");
    logFile.print("PLAYER_ID,"); logFile.println(playerID);
    logFile.print("START_CM,");  logFile.println(srStartH, 2);
    logFile.print("FINAL_CM,");  logFile.println(srFinalH, 2);
    logFile.print("DISP_CM,");   logFile.println(disp, 2);
    logFile.println("---END---");
    logFile.close();
  }

  Serial.print("SR result: start="); Serial.print(srStartH);
  Serial.print(" final=");           Serial.print(srFinalH);
  Serial.print(" disp=");            Serial.println(disp);

  // Send JSON over BLE if session active
  if (Bluefruit.connected() && srSessionActive) {
    char buf[160];
    snprintf(buf, sizeof(buf),
      "{\"type\":\"sr_result\",\"player\":\"%s\","
      "\"start_cm\":%.1f,\"final_cm\":%.1f,\"disp_cm\":%.1f,"
      "\"file\":\"%s\"}",
      playerID.c_str(), srStartH, srFinalH, disp,
      currentFilename.c_str());
    sendBLE(String(buf));
  }
}

// Draw SR-specific display for each phase
void srDisplayPhase(float distCm) {
  display.clearDisplay();
  // Top-right: last result
  if (srLastResult > 0) {
    display.setTextSize(1);
    display.setCursor(80, 0);
    display.print(srLastResult, 1);
    display.println("cm");
  }
  // Top bar — mode label
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Sit & Reach");

  // Current distance
  display.setTextSize(2);
  display.setCursor(0, 12);
  display.print(distCm, 1);
  display.println(" cm");

  display.setTextSize(1);

  switch (srPhase) {

    case SR_PHASE_IDLE:
      display.setCursor(0, 34);
      display.println("Waiting for");
      display.setCursor(0, 44);
      display.println("session...");
      break;

    case SR_PHASE_FIX_START: {
      display.setCursor(0, 34);
      display.println("Hold steady...");
      // Show a small progress bar for the 5s idle timer
      if (srIdleTimerActive) {
        unsigned long elapsed = millis() - srIdleTimerStart;
        int barW = (int)((float)elapsed / SR_IDLE_WINDOW_MS * 80.0f);
        if (barW > 80) barW = 80;
        display.drawRect(0, 45, 80, 6, SSD1306_WHITE);
        display.fillRect(0, 45, barW, 6, SSD1306_WHITE);
      }
      break;
    }

    case SR_PHASE_WAIT_REACH:
      display.setCursor(0, 34);
      display.print("Start: ");
      display.print(srStartH, 1);
      display.println("cm");
      display.setCursor(0, 44);
      display.println("Reach forward!");
      break;

    case SR_PHASE_WAIT_FINAL: {
      display.setCursor(0, 34);
      display.print("Start: ");
      display.print(srStartH, 1);
      display.println("cm");
      // Draw dots — filled = remaining, empty = elapsed
      int dotX = 0;
      for (int i = 0; i < 6; i++) {
        if (i < srDotsRemaining)
          display.fillCircle(dotX + 3, 54, 3, SSD1306_WHITE);
        else
          display.drawCircle(dotX + 3, 54, 3, SSD1306_WHITE);
        dotX += 10;
      }
      break;
    }

    case SR_PHASE_DONE:
      display.setCursor(0, 26);
      display.print("St:");
      display.print(srStartH, 1);
      display.print(" Fn:");
      display.println(srFinalH, 1);
      display.setCursor(0, 36);
      display.print("Disp: ");
      display.print(srLastResult, 1);
      display.println(" cm");
      display.setCursor(0, 46);
      display.print(previousFilename != "" ? previousFilename : currentFilename);
      break;
  }

  // BLE dots — bottom right (same as ruler drop)
  if (Bluefruit.connected())              display.fillCircle(125, 61, 2, SSD1306_WHITE);
  if (rdSessionActive || srSessionActive) display.fillCircle(119, 61, 2, SSD1306_WHITE);

  display.display();
}

// =============================
// BLE HANDLER
// =============================
void handleBLE() {
  static bool wasConnected = false;
  bool isConnected = Bluefruit.connected();

  if (wasConnected && !isConnected) {
    rdSessionActive = false;
    // If SR session drops mid-test, invalidate cleanly
    if (srSessionActive && currentMode == MODE_SIT_REACH) {
      srInvalidate();
      srBeginPhase(SR_PHASE_IDLE);
    }
    srSessionActive = false;
    playerID        = "";
    Serial.println("BLE disconnected — sessions closed");
  }
  wasConnected = isConnected;

  if (!bleuart.available()) return;
  String cmd = bleuart.readStringUntil('\n');
  cmd.trim();
  if (cmd.length() > 0) onBLEReceived(cmd);
}

void onBLEReceived(String cmd) {

  // ---- RULER DROP SESSION (untouched) ----
  if (cmd.startsWith("START_RD_SESSION,")) {
    if (currentMode != MODE_RULER_DROP) {
      sendBLE("{\"status\":\"error\",\"msg\":\"not in ruler drop mode\"}");
      return;
    }
    playerID        = cmd.substring(17);
    rdSessionActive = true;
    lastDropDistanceCM = -1.0f;
    sendBLE("{\"status\":\"rd_session_started\",\"player\":\"" + playerID + "\"}");
    Serial.println("RD session started: " + playerID);
  }

  else if (cmd == "STOP_RD_SESSION") {
    if (currentMode != MODE_RULER_DROP) {
      sendBLE("{\"status\":\"error\",\"msg\":\"not in ruler drop mode\"}");
      return;
    }
    rdSessionActive = false;
    sendBLE("{\"status\":\"rd_session_stopped\",\"player\":\"" + playerID + "\"}");
    Serial.println("RD session stopped: " + playerID);
    playerID = "";
  }

  // ---- SIT & REACH SESSION ----
  else if (cmd.startsWith("START_SR_SESSION,")) {
    if (currentMode != MODE_SIT_REACH) {
      sendBLE("{\"status\":\"error\",\"msg\":\"not in sit and reach mode\"}");
      return;
    }
    playerID        = cmd.substring(17);
    srSessionActive = true;
    // Online: new file + begin Phase 1 immediately on session start
    previousFilename = currentFilename;
    currentFilename  = getNextFileWithPrefix("SandR_");
    createNewFile(currentFilename);
    srBeginPhase(SR_PHASE_FIX_START);
    sendBLE("{\"status\":\"sr_session_started\",\"player\":\"" + playerID +
            "\",\"file\":\"" + currentFilename + "\"}");
    Serial.println("SR session started: " + playerID);
  }

  else if (cmd == "STOP_SR_SESSION") {
    if (currentMode != MODE_SIT_REACH) {
      sendBLE("{\"status\":\"error\",\"msg\":\"not in sit and reach mode\"}");
      return;
    }
    // If stopped mid-test (not DONE), erase file and send nothing
    if (srPhase != SR_PHASE_DONE) {
      // Truncate file — invalid incomplete test
      if (logFile.open(currentFilename.c_str(), O_WRITE | O_TRUNC)) {
        logFile.close();
      }
      sd.remove(currentFilename.c_str());
      sendBLE("{\"status\":\"sr_session_stopped\",\"player\":\"" + playerID +
              "\",\"saved\":false}");
    } else {
      sendBLE("{\"status\":\"sr_session_stopped\",\"player\":\"" + playerID +
              "\",\"saved\":true}");
    }
    srSessionActive = false;
    srBeginPhase(SR_PHASE_IDLE);
    currentFilename = getNextFileWithPrefix("SandR_");
    createNewFile(currentFilename);
    Serial.println("SR session stopped: " + playerID);
    playerID = "";
  }

  // ---- FILE UTILITIES (untouched) ----
  else if (cmd == "LIST_FILES") {
    File32 root = sd.open("/");
    File32 entry;
    String list = "{\"status\":\"files\",\"list\":[";
    bool first = true;
    while (entry.openNext(&root, O_RDONLY)) {
      char name[32];
      entry.getName(name, sizeof(name));
      if (!first) list += ",";
      list += "\"" + String(name) + "\"";
      first = false;
      entry.close();
    }
    root.close();
    list += "]}";
    sendBLE(list);
  }

  else if (cmd.startsWith("GET_FILE,")) {
    String fname = cmd.substring(9);
    streamFileBLE("file_data", playerID, fname);
  }

  else if (cmd.startsWith("DELETE_FILE,")) {
    String fname = cmd.substring(12);
    if (sd.remove(fname.c_str()))
      sendBLE("{\"status\":\"deleted\",\"file\":\"" + fname + "\"}");
    else
      sendBLE("{\"status\":\"error\",\"msg\":\"delete failed\"}");
  }
}

void streamFileBLE(String type, String pid, String fname) {
  File32 f = sd.open(fname.c_str(), O_RDONLY);
  if (!f) {
    sendBLE("{\"status\":\"error\",\"msg\":\"file not found\"}");
    return;
  }
  sendBLE("{\"status\":\"stream_start\",\"type\":\"" + type +
          "\",\"player\":\"" + pid +
          "\",\"file\":\"" + fname + "\"}");
  delay(50);

  char chunk[101];
  int  idx = 0;
  while (f.available()) {
    chunk[idx++] = f.read();
    if (idx == 100) {
      chunk[idx] = '\0';
      bleuart.print(chunk);
      idx = 0;
      delay(20);
    }
  }
  if (idx > 0) {
    chunk[idx] = '\0';
    bleuart.print(chunk);
  }
  f.close();
  delay(50);
  sendBLE("{\"status\":\"stream_end\",\"type\":\"" + type +
          "\",\"player\":\"" + pid +
          "\",\"file\":\"" + fname + "\"}");
}

void sendBLE(String msg) {
  if (!Bluefruit.connected()) return;
  int len = msg.length();
  int i = 0;
  while (i < len) {
    int chunkSize = min(20, len - i);
    bleuart.write((const uint8_t*)msg.c_str() + i, chunkSize);
    bleuart.flush();
    i += chunkSize;
    delay(20);         //change this to 100 if json file not chunking properly
  }
  bleuart.write((const uint8_t*)"\r\n", 2);
  bleuart.flush();
}

// =============================
// ALGORITHM — secant version (untouched)
// =============================
float runRulerDropAlgorithm(float *az_g, float *tof_cm,
                             float &startH, float &catchH,
                             int   &startI, int   &catchI)
{
  int anchor = 1;
  float max_dimu = -9999.0f;
  for (int i = 1; i < WIN_SAMPLES; i++) {
    float d = az_g[i] - az_g[i - 1];
    if (d > max_dimu) { max_dimu = d; anchor = i; }
  }

  int inner_start = max(1,             anchor - ANCHOR_PRE);
  int inner_end   = min(WIN_SAMPLES-1, anchor + ANCHOR_POST);
  int seg_start   = max(3,             anchor - SEG_PRE);
  int seg_end     = min(WIN_SAMPLES-4, anchor + SEG_POST);

  int   n     = inner_end - inner_start + 1;
  float sumx  = 0, sumy = 0, sumxy = 0, sumx2 = 0;
  for (int i = inner_start; i <= inner_end; i++) {
    float x = (float)i, y = tof_cm[i];
    sumx += x; sumy += y; sumxy += x*y; sumx2 += x*x;
  }
  float denom = (float)n * sumx2 - sumx * sumx;
  if (fabs(denom) < 1e-6f) return -1.0f;
  float slope     = ((float)n * sumxy - sumx * sumy) / denom;
  float intercept = (sumy - slope * sumx) / (float)n;

  int start_idx = seg_start;
  for (int i = anchor - 1; i >= seg_start; i--) {
    float r0 = tof_cm[i]   - (slope*(float)i       + intercept);
    float r1 = tof_cm[i+1] - (slope*(float)(i + 1) + intercept);
    if (r0 * r1 <= 0.0f) { start_idx = i; break; }
  }

  int end_idx = seg_end;
  for (int i = seg_end; i > anchor; i--) {
    float r0 = tof_cm[i]   - (slope*(float)i       + intercept);
    float r1 = tof_cm[i-1] - (slope*(float)(i - 1) + intercept);
    if (r0 * r1 <= 0.0f) { end_idx = i; break; }
  }

  float p0 = tof_cm[start_idx-1], p1 = tof_cm[start_idx-2], p2 = tof_cm[start_idx-3];
  if (p0>p1){float t=p0;p0=p1;p1=t;} if(p1>p2){float t=p1;p1=p2;p2=t;} if(p0>p1){float t=p0;p0=p1;p1=t;}
  startH = p1;

  float q0 = tof_cm[end_idx+1], q1 = tof_cm[end_idx+2], q2 = tof_cm[end_idx+3];
  if (q0>q1){float t=q0;q0=q1;q1=t;} if(q1>q2){float t=q1;q1=q2;q2=t;} if(q0>q1){float t=q0;q0=q1;q1=t;}
  catchH = q1;

  startI = start_idx;
  catchI = end_idx;

  float disp = catchH - startH;
  if (disp < 0) disp = -disp;
  if (disp < 1.0f || disp > 40.0f) return -1.0f;
  return disp;
}

// =============================
// MODE LOGIC (untouched)
// =============================
void updateModeFromIMU(DeviceMode detected) {
  if (detected != currentMode) {
    if (detected != candidateMode) {
      candidateMode   = detected;
      modeStableStart = millis();
    } else if (millis() - modeStableStart > MODE_HOLD_TIME) {
      switchMode(detected);
    }
  }
}

void switchMode(DeviceMode newMode) {
  if (Bluefruit.connected()) {
    if (currentMode == MODE_SIT_REACH && srSessionActive) {
      // Invalidate any in-progress SR test before switching
      if (srPhase != SR_PHASE_DONE) {
        if (logFile.open(currentFilename.c_str(), O_WRITE | O_TRUNC)) logFile.close();
        sd.remove(currentFilename.c_str());
      }
      sendBLE("{\"status\":\"sr_session_stopped\",\"reason\":\"mode_switch\"}");
      srSessionActive = false;
    }
    if (currentMode == MODE_RULER_DROP && rdSessionActive) {
      sendBLE("{\"status\":\"rd_session_stopped\",\"reason\":\"mode_switch\"}");
      rdSessionActive = false;
    }
    playerID = "";
  }

  showMessage("Switching...", "", 1500);
  currentMode      = newMode;
  previousFilename = currentFilename;

  if (currentMode == MODE_RULER_DROP) {
    currentFilename = getNextFileWithPrefix("trial_");
    srBeginPhase(SR_PHASE_IDLE);  // reset SR state cleanly
  } else {
    currentFilename = getNextFileWithPrefix("SandR_");
  }

  createNewFile(currentFilename);
  lastDropDistanceCM = -1.0f;

  // If switching into SR mode offline, begin immediately
  if (currentMode == MODE_SIT_REACH && !Bluefruit.connected())
    srBeginPhase(SR_PHASE_FIX_START);

  showMessage(currentMode == MODE_RULER_DROP ? "Ruler Drop" : "Sit & Reach",
              currentFilename, 2000);
}

// =============================
// FILE UTILITIES (untouched)
// =============================
String getNextFileWithPrefix(String prefix) {
  File32 root, entry;
  int maxNum = 0;
  root.open("/");
  while (entry.openNext(&root, O_RDONLY)) {
    char name[32];
    entry.getName(name, sizeof(name));
    String f = String(name);
    if (f.startsWith(prefix) && f.endsWith(".txt")) {
      int n = f.substring(prefix.length(), f.length() - 4).toInt();
      if (n > maxNum) maxNum = n;
    }
    entry.close();
  }
  root.close();
  return prefix + String(maxNum + 1) + ".txt";
}

bool createNewFile(String fname) {
  logFile.open(fname.c_str(), O_CREAT | O_TRUNC | O_WRITE);
  if (currentMode == MODE_RULER_DROP)
    logFile.println("# Ruler Drop Trial");
  else {
    logFile.println("# Sit & Reach");
    logFile.println("time_ms,distance_cm");
  }
  logFile.close();
  return true;
}

void showMessage(String l1, String l2, int d) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 10); display.println(l1);
  display.setCursor(0, 30); display.println(l2);
  display.display();
  delay(d);
}

// =============================
// SERIAL COMMANDS (untouched)
// =============================
void handleSerialCommands() {
  if (!Serial.available()) return;
  String cmd = Serial.readStringUntil('\n');
  cmd.trim();

  if (cmd == "LIST") {
    File32 root = sd.open("/");
    File32 entry;
    Serial.println("FILES:");
    while (entry.openNext(&root, O_RDONLY)) {
      char name[32];
      entry.getName(name, sizeof(name));
      Serial.println(name);
      entry.close();
    }
    root.close();
    Serial.println("END_FILES");

  } else if (cmd.startsWith("GET ")) {
    String fname = cmd.substring(4);
    File32 f = sd.open(fname.c_str(), O_RDONLY);
    if (f) {
      Serial.println("START");
      while (f.available()) Serial.write(f.read());
      Serial.println("\nEND");
      f.close();
    } else {
      Serial.println("ERROR: File not found");
    }

  } else if (cmd.startsWith("DELETE ")) {
    String fname = cmd.substring(7);
    Serial.println(sd.remove(fname.c_str()) ? "DELETED" : "FAILED");
  }
}

void connect_callback(uint16_t conn_handle) {
  Serial.println("Connected!");
}

void disconnect_callback(uint16_t conn_handle, uint8_t reason) {
  Serial.print("Disconnected, reason = 0x");
  Serial.println(reason, HEX);
  Bluefruit.Advertising.start(0);
}
