//------------------------------------------------------------
// Board info: Waveshare ESP32 S3 Zero
// Upload Set up
// - Tool -> Board -> 'Waveshare ESP32 S3 Zero'
// - USB CDC On Boot: Enabled
// * Make sure to match the board info to control the builtInLED
//------------------------------------------------------------
// [secret.h]
// Make sure fill up the secret infos
// 1. WIFI SSID/PASSWORD list, can be multiple
// 2. Firestore database API key
// 3. Client ID, 4 digit number, matches with the DB data
//------------------------------------------------------------

#include "secrets.h"

// WIFI
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
// JSON read
#include <ArduinoJson.h>


// RX TX setting
#include <HardwareSerial.h>
const int BAUD = 115200;
const int TXInterval = 200;
const int RXTimeout = 600;
const char* msgLine = "MSG:";
int lastMsgTime = 0;
bool msgActivate = false;
bool feedbackActivate = true;

const int RXPin = 7;
const int TXPin = 8;

HardwareSerial Uart(2);

String RXLine;
String line;
int lastTX = 0;
int counter = 0;


// Firestore Database
String idToken;
int tokenExpiryMs = 0;

// client info list
struct ClientInfo{
  String docPath;
  String Name;
};

ClientInfo currentClient;

#include <Wire.h>
// IMU (MPU6050) set
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#define I2C_SDA 2
#define I2C_SCL 1
Adafruit_MPU6050 mpu;
bool handshakeEnable = false;
String targetClientID = "";



static const uint32_t SAMPLE_MS = 20;

// Smoothing
static const float GRAV_ALPHA = 0.02f;
static const float MOVE_ALPHA = 0.15f;

// ---- Handshake detection tuning ----
static const float VERT_EMA_ALPHA = 0.25f;

// Thresholds (tune)
static float SHAKE_POS_THRESH = 1.0f;    // upward threshold
static float SHAKE_NEG_THRESH = -1.0f;   // downward threshold

// One full handshake cycle timing (NEG -> POS only)
static const uint32_t CYCLE_MIN_MS = 80;
static const uint32_t CYCLE_MAX_MS = 700;

// Stop: if no completed cycles for this long, stop
static const uint32_t STOP_GAP_MS = 800;

// Gravity expectation vector
static float gx = 0, gy = 0, gz = 0;

// Optional magnitude EMA (not required)
static float moveEma = 0;

// ---- Handshake runtime state ----
static bool handshaking = false;

// NEG->POS cycle arming
static bool negArmed = false;        // true after we see NEG edge, waiting for POS
static uint32_t negMs = 0;           // time of NEG edge
static uint32_t lastCycleMs = 0;     // time of last COMPLETED cycle (NEG->POS)


// ------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------

// TLS
// Insecure way to detect the firebase
// but, this project isn't public or official, so...
// for efficiency
static inline void makeInsecureTLS(WiFiClientSecure &client){
  client.setInsecure();
}

// Firestore Database Auth
bool firebaseSignIn(){
  if(WiFi.status() != WL_CONNECTED){
    return false;
  }
  WiFiClientSecure client;
  makeInsecureTLS(client);
  
  HTTPClient https;
  String url = String("https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=") + FIREBASE_API_KEY;

  if(!https.begin(client, url)){
    Serial.println("[AUTH] https.begin failed");
    return false;
  }

  // Targeting JSON shape
  https.addHeader("Content-Type", "application/json");

  StaticJsonDocument<256> req;
  req["email"] = FIREBASE_EMAIL;
  req["password"] = FIREBASE_PASSWORD;
  req["returnSecureToken"] = true;

  String body;
  serializeJson(req,body);

  int code = https.POST(body);
  String resp = https.getString();
  https.end();

  Serial.printf("[AUTH] HTTP %d\n", code);
  if(code != 200){
    Serial.println(resp);
    return false;
  }

  StaticJsonDocument<4096> doc;
  auto err = deserializeJson(doc, resp);
  if(err){
    Serial.print("[AUTH] JSON parse error: ");
    Serial.println(err.c_str());
    return false;
  }

  idToken = doc["idToken"].as<String>();
  int expiresInSec = doc["expiresIn"].as<int>();
  tokenExpiryMs = millis() + (uint32_t)(max(60, expiresInSec - 60)) * 1000UL;
  
  Serial.println("[AUTH] idToken OK");
  return true;
}

bool ensureFirebaseAuth(){
  if(WiFi.status() != WL_CONNECTED){
    return false;
  }
  if(idToken.length() == 0 || millis() > tokenExpiryMs){
    return firebaseSignIn();
  }
  return true;
}

String fsBase(){
  return String("https://firestore.googleapis.com/v1/projects/")
       + FIREBASE_PROJECT_ID
       + "/databases/(default)/documents/";
}

// docPath: "clients/0000/..."
// Dig into the client infos
String fsDocUrl(const String& docPath){
  return fsBase() + docPath;
}

int firestoreGetRaw(const String docPath, String &outResp){
  if(!ensureFirebaseAuth()){
    return -1;
  }
  WiFiClientSecure client;
  makeInsecureTLS(client);

  HTTPClient https;
  if(!https.begin(client, fsDocUrl(docPath))){
    return -1;
  }
  https.addHeader("Authorization", "Bearer " + idToken);

  int code = https.GET();
  outResp = https.getString();
  https.end();

  return code;
}

//// Assigned to RXTask();
// When receive other client's id, compare it with current client's data
// {currnetClient} > clientConnection > {received client ID} > State(String value)
// If there's no received client ID in 'clientConnection' collection,
// make a data and set 'State' as 0
// If there's already received client ID in 'clientConnection' collection,
// apply 'State' ++;
bool firestoreClientConnectionUpdate(String otherID){
  if(!ensureFirebaseAuth()){
    Serial.println("FirebaseAuth Failed");
    return false;
  }

  // Preventing self pinging
  if(otherID == String(FIREBASE_CLIENTID)){
    Serial.println("Received same ClientID");
    return false;
  }

  String docPath = String("clients/") + FIREBASE_CLIENTID + "/clientConnection/" + otherID;

  // Access to the 'docPath' to confirm is it exist or not
  String resp;
  int code = firestoreGetRaw(docPath, resp);
  Serial.print("Code: ");
  Serial.println(code);

  // When it's not exist, create one
  if(code == 404){
    WiFiClientSecure client;
    makeInsecureTLS(client);

    HTTPClient https;
    String url = fsDocUrl(docPath) + "?updateMask.fieldPaths=State";

    if(!https.begin(client, url)){
      Serial.println("HTTPS Begin Falied");
      return false;
    }

    https.addHeader("Authorization", "Bearer " + idToken);
    https.addHeader("Content-Type", "application/json");

    StaticJsonDocument<256> body;
    JsonObject fields = body.createNestedObject("fields");
    fields["State"]["stringValue"] = "0";

    String payload;
    serializeJson(body, payload);

    int c2 = https.PATCH(payload);
    String r2 = https.getString();
    https.end();

    if(c2 != 200){
      Serial.println(r2);
      return false;
    }
    return true;
  }

  // When it's exist, parse 'State' and increase
  if(code == 200){
    int current = 0;
    {
      DynamicJsonDocument doc(4096);
      auto err = deserializeJson(doc, resp);
      if(err){
        Serial.println("Failed deserializeJson 200");
        return false;
      }
      const char* s = doc["fields"]["State"]["stringValue"] | "0";
      current = atoi(s);
      if(current < 0){
        current = 0;
      }
    }
    int next = current + 1;
    
    WiFiClientSecure client;
    makeInsecureTLS(client);

    HTTPClient https;
    String url = fsDocUrl(docPath) + "?updateMask.fieldPaths=State";

    if(!https.begin(client,url)){
      Serial.println("Failed https.begin 200");
      return false;
    }

    https.addHeader("Authorization", "Bearer " + idToken);
    https.addHeader("Content-Type", "application/json");

    StaticJsonDocument<256> body;
    JsonObject fields = body.createNestedObject("fields");

    char buf[16];
    snprintf(buf, sizeof(buf), "%d", next);
    fields["State"]["stringValue"] = buf;

    String payload;
    serializeJson(body, payload);

    int c2 = https.PATCH(payload);
    String r2 = https.getString();
    https.end();

    if(c2 != 200){
        Serial.println(r2);
        return false;
    }
    return true;  
  }
  // other errors
  Serial.printf("[FS] GET %s -> HTTP %d\n", docPath.c_str(), code);
  Serial.println(resp);
  return false;
}


// WIFI Connection
void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  static bool started = false;
  static unsigned long lastAttempt = 0;

  if (millis() - lastAttempt < 5000) return;
  lastAttempt = millis();

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);

  if (!started) {
    Serial.println("Scanning WiFi...");
    int n = WiFi.scanNetworks();

    if (n <= 0) {
      Serial.println("No WiFi networks found");
      return;
    }

    for (int i = 0; i < n; i++) {
      String found = WiFi.SSID(i);

      for (int j = 0; j < WIFI_COUNT; j++) {
        if (found == WIFI_SSIDS[j]) {
          Serial.print("Connecting to ");
          Serial.println(WIFI_SSIDS[j]);

          WiFi.begin(WIFI_SSIDS[j], WIFI_PASSWORDS[j]);
          started = true;
          Serial.print("WIFI connected with: ");
          Serial.println(WIFI_SSIDS[j]);
          return;
        }
      }
    }
    Serial.println("No known WiFi detected");
  }
}

// RX TX 
// Action after receive the msg
void onMsgLine(String line){

  line = FIREBASE_CLIENTID_2;

  targetClientID = line;

  Serial.print("Receive, ");
  Serial.println(line);

  handshakeEnable = true;

  if(feedbackActivate){
    feedbackActivate = false;
    rgbLedWrite(RGB_BUILTIN, 0, 0,RGB_BRIGHTNESS);
  }

  // // Action
  // // Firestore: create(State=0) if missing, else State++
  // bool tryUpdate = firestoreClientConnectionUpdate(line);

  // if(tryUpdate){
  //   rgbLedWrite(RGB_BUILTIN, 0, 0,RGB_BRIGHTNESS);
  // }else{
  //   rgbLedWrite(RGB_BUILTIN, RGB_BRIGHTNESS, 0, 0);
  // }
  // delay(500);
}

// Action after lost the msg
void onMsgLost() {
  Serial.println("Connection lost");
  digitalWrite(RGB_BUILTIN, LOW);
  delay(1000);
  handshakeEnable = false;
  feedbackActivate = true;
}

void TXTask(){
  int now  = millis();
  if(now - lastTX < TXInterval){
    return;
  }
  lastTX = now;
  Uart.print('\r');
  Uart.print(msgLine);
  Uart.print(FIREBASE_CLIENTID);
  Uart.print('\n');
}

void RXTask(){
  while (Uart.available()>0){
    char c = (char)Uart.read();
    if(c == '\r'){
      continue;
    }

    if (c == '\n') {
      RXLine.trim();
      
      if(RXLine.startsWith(msgLine)){
        String cmd = RXLine.substring(4);
        cmd.trim();
        Serial.println(cmd);

        lastMsgTime = millis();
        if(!msgActivate){
          msgActivate = true;
        }        
        onMsgLine(cmd);
      }
      RXLine = "";
      continue;
    }else{
      RXLine += c;

    }
  }
}

void msgLostTask(){
  if(!msgActivate){
    return;
  }
  int now = millis();
  if(now - lastMsgTime > RXTimeout){
    msgActivate = false;
    onMsgLost();
  }
}

// ------------------------------------------------------------------
// ------------------------------------------------------------------
// Handshake Detection
void handshakeDetect(){
  static uint32_t lastMs = 0;
  uint32_t now = millis();
  if (now - lastMs < SAMPLE_MS) return;
  lastMs = now;

  // if (!isHandshakeDetectionEnabled()) {
  if(!handshakeEnable){
    if (handshaking) onHandshakeStop();
    resetHandshakeState();
    return;
  }

  sensors_event_t a, g, t;
  mpu.getEvent(&a, &g, &t);

  float ax = a.acceleration.x;
  float ay = a.acceleration.y;
  float az = a.acceleration.z;

  // Gravity estimate
  gx = gx * (1.0f - GRAV_ALPHA) + ax * GRAV_ALPHA;
  gy = gy * (1.0f - GRAV_ALPHA) + ay * GRAV_ALPHA;
  gz = gz * (1.0f - GRAV_ALPHA) + az * GRAV_ALPHA;

  // Linear acceleration
  float lx = ax - gx;
  float ly = ay - gy;
  float lz = az - gz;

  // Optional magnitude EMA
  float move = sqrtf(lx*lx + ly*ly + lz*lz);
  moveEma = moveEma * (1.0f - MOVE_ALPHA) + move * MOVE_ALPHA;

  // Normalize gravity direction
  float g2 = gx*gx + gy*gy + gz*gz;
  float invg = safeInvSqrt(g2);
  float ux = gx * invg;
  float uy = gy * invg;
  float uz = gz * invg;

  // Vertical component of linear acceleration
  float vert = lx*ux + ly*uy + lz*uz;

  // Smooth
  static float vertEma = 0;
  vertEma = vertEma * (1.0f - VERT_EMA_ALPHA) + vert * VERT_EMA_ALPHA;

  // Detect edges
  bool negEdge = (vertEma < SHAKE_NEG_THRESH);
  bool posEdge = (vertEma > SHAKE_POS_THRESH);

  // If we armed on NEG but waited too long for POS, reset
  if (negArmed && negMs != 0 && (now - negMs) > CYCLE_MAX_MS) {
    resetCycleArming();
  }

  // 1) Arm when we see NEG edge (downward)
  if (!negArmed) {
    if (negEdge) {
      negArmed = true;
      negMs = now;
    }
  }
  // 2) Complete cycle only when we see POS edge after NEG
  else {
    if (posEdge) {
      uint32_t cycleDt = now - negMs;

      if (cycleDt >= CYCLE_MIN_MS && cycleDt <= CYCLE_MAX_MS) {
        // First completed cycle -> start session here
        if (!handshaking) {
          handshaking = true;
          onHandshakeStart();
        }

        lastCycleMs = now;
        // onHandshakeCycle(vertEma, cycleDt);
        onHandshakeCycle();
      }

      // Re-arm for next handshake (must see NEG again)
      resetCycleArming();
    }
  }

  // STOP: no completed cycles for STOP_GAP_MS
  if (handshaking) {
    if (lastCycleMs != 0 && (now - lastCycleMs) > STOP_GAP_MS) {
      onHandshakeStop();
      resetHandshakeState();
    }
  }
}


void onHandshakeStart() {
  Serial.println("Handshake START");
}

void onHandshakeStop() {
  Serial.println("Handshake STOP");
}

// When it detects the handshake 'cycle'
// -> Cycle means, the handshake has to be Up+Down or Down+Up
// void onHandshakeCycle(float vertEma, uint32_t cycleDtMs) {
void onHandshakeCycle() {
  Serial.println("Handshaked");
  rgbLedWrite(RGB_BUILTIN, 0, RGB_BRIGHTNESS, 0);

  // Action
  // Firestore: create(State=0) if missing, else State++
  bool tryUpdate = firestoreClientConnectionUpdate(targetClientID);

  if(!tryUpdate){
    rgbLedWrite(RGB_BUILTIN, RGB_BRIGHTNESS, 0, 0);
  }
  delay(500);
}

static float safeInvSqrt(float x) {
  if (x <= 1e-6f) return 0.0f;
  return 1.0f / sqrtf(x);
}

static void resetCycleArming() {
  negArmed = false;
  negMs = 0;
}

static void resetHandshakeState() {
  handshaking = false;
  lastCycleMs = 0;
  resetCycleArming();
}


void setup() {
  Serial.begin(115200);
  digitalWrite(RGB_BUILTIN, LOW);  

  delay(200);

  Uart.begin(BAUD, SERIAL_8N1, RXPin, TXPin);


  Serial.println("ESP32 ready");
  Serial.print("Current Client ID is: ");
  Serial.println(FIREBASE_CLIENTID);

  connectWiFi();

  pinMode(I2C_SDA, INPUT_PULLUP);
  pinMode(I2C_SCL, INPUT_PULLUP);

  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(100000);
  delay(300);

  if (!mpu.begin(0x68, &Wire)) {
    Serial.println("MPU6050 not found!");
    while (true) delay(10);
  }

  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
  delay(200);
  digitalWrite(RGB_BUILTIN, HIGH);  


}

void loop() {
  // WIFI reconnect attempt
  if (WiFi.status() != WL_CONNECTED) {
    static uint32_t lastTry = 0;
    if (millis() - lastTry > 5000) {
      lastTry = millis();
      connectWiFi();
    }
  }

  // Looping RX TX task
  TXTask();
  RXTask();
  msgLostTask();

  // Handshake detection
  handshakeDetect();

}


