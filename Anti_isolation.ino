#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>

#define PIN_DHT    D1
#define PIN_BTN_1  D2
#define PIN_BTN_2  D3
#define PIN_BTN_3  D4
#define PIN_SCL    D5
#define PIN_SDA    D6
#define PIN_BUZZER D10

#define SECONDS_PER_DAY   10UL
#define SNOOZE_MS         60000UL
#define CHIME_REPEAT_MS   15000UL
#define DEBOUNCE_MS       30

#define MPU_ADDR          0x68
#define TILT_DEBUG        1

#define TILT_LR_AXIS      ay
#define TILT_LR_SIGN      1
#define FLIP_SIGN         1

#define TILT_ON_G         0.45f
#define TILT_OFF_G        0.20f
#define FACE_DOWN_G       0.75f
#define FLIP_HOLD_MS      600

Adafruit_SSD1306 display(128, 64, &Wire, -1);
DHT dht(PIN_DHT, DHT11);

struct Contact {
  const char* name;
  uint16_t remindAfterDays;
  uint32_t lastContactMs;
};

Contact contacts[] = {
  {"Mom",      3,  0},
  {"Dad",      7,  0},
  {"Liam",     14, 0},
};
const int NUM_CONTACTS = sizeof(contacts) / sizeof(contacts[0]);

struct Button {
  uint8_t pin;
  bool lastState;
  uint32_t lastChangeMs;
};

Button btnNext   = {PIN_BTN_1, HIGH, 0};
Button btnDone   = {PIN_BTN_2, HIGH, 0};
Button btnSnooze = {PIN_BTN_3, HIGH, 0};

bool wasPressed(Button &b) {
  bool now = digitalRead(b.pin);
  if (now != b.lastState && (millis() - b.lastChangeMs) > DEBOUNCE_MS) {
    b.lastChangeMs = millis();
    b.lastState = now;
    if (now == LOW) return true;
  }
  return false;
}

int selected = 0;
uint32_t snoozeUntilMs = 0;
uint32_t lastChimeMs = 0;
uint32_t lastDhtMs = 0;
float tempC = NAN;
float humidity = NAN;

bool mpu0k = false;
float ax = 0, ay = 0, az = 0;
bool tiltArmed = true;
uint32_t faceDownSinceMs = 0;
bool flipHandled = false;

void beep (int freq, int ms) {
  ledcWriteTone(PIN_BUZZER, freq);
  delay(ms);
  ledWriteTone(PIN_BUZZER, 0);
}

void chime () {
  beep (880, 120); delay (40);
  beep (1175, 120); delay (40);
  beep(1568, 250);
}

bool mpuInit() {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x68);
  Wire.write(0x00);
  if (Wire.endTransmission() != 0) return false;

  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x1C);
  Wire.write(0x00);
  return Wire.endTransmission() == 0;
}

int16_t read16() {
  uint8_t hi = Wire.read();
  uint8_t lo = Wire.read();
  return (int16_t)(hi << 8 | lo)
}

bool readAccel() {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);
  if(Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)6) != 6) return false;
  ax = read16() / 16384.0f;
  ay = read16() / 16384.0f;
  az = read16() / 16384.0f;
  return true;
}

void snooze() {
  snoozeUntilMs = millis() + SNOOZE_MS;
  beep(660, 80);
}

void changeContact(int dir) {
  selected = (selected + dir + NUM_CONTACTS) % NUM_CONTACTS;
}

void updateTilt() {
  if (!mpu0k) return;

  static uint32_t lastReadMs = 0;
  if (millis() - lastReadMs < 20) return;
  lastReadMs = millis
  if (!readAccel()) return;

#if TILT_DEBUG
  static uint32_t lastPrintMs = 0;
  if(millis() - lastPrintMs > 250) {
    lastPrintMs = millis();
    Serial.printf("ax=%.2f ay=%.2f, az=%.2f\n" ax, ay, az);
  }
#endif

  bool faceDown = (FLIP_SIGN * az) < -FACE_DOWN_G;
  if (faceDown) {
    if (faceDownSinceMs == 0) faceDownSinceMs = millis();
    if (!flipHandled && (millis() - faceDownSinceMs) > FLIP_HOLD_MS) {
      snooze();
      flipHandled = true;
    }
  } else {
    faceDownSinceMs = 0;
    flipHandled = false;
  }
  
  if (!faceDown) {
    float lr = TILT_LR_SIGN * TILT_LR_AXIS;
    if (tiltArmed) {
      if (lr > TILT_ON_G) {
        changeContact(+1);
        tiltArmed = false;
        beep(2000, 15);
      } else if (lr < -TILT_ON_G) {
        changeContact(-1);
        tiltArmed = false;
        beep(2000, 15);
      }
    } else if (fabsf(lr) < TILT_OFF_G) {
      tiltArmed = true;
    }
  }
}

uint32_t daysSince(const Contact &c) {
  return (millis() - c.lastContactMs) / (1000UL * SECONDS_PER_DAY)
}

int overdueIndex() {
  for (int i = 0;  i< NUM_CONTACTS; i++) {
    if (daysSince(contacts[i]) >= contacts[i].remindAfterDays) return i;
  }
  return -1
}

void readSensor() {
  if(millis() - lastDhtMs < 2000) return;
  lastDhtMs = millis();
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (!isnan(t)) tempC = t;
  if (!isnan(h)) humidity = h;
}

void drawScreen(int dueIdx, bool snoozed) {
  const Contact &c = contacts[selected];
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(0, 0);
  display.print(c.name);
  display.setTextSize(1);
  display.setCursor(0, 20);
  display.printf("Last: %lu days ago", (unsigned long)daysSince(c));
  display.setCursor(0, 30);
  display.printf("Remind every %d days", (int)c.remindAfterDays);

  display.setCursor(0, 42);
  if(isnan(tempC) || isnan(humidity)) {
    display.print("Temp: --");
  } else {
    display.print ("%.0fC  %.0f%% RH", tempC, humidity);
  }

  display.setCursor(0, 54);
  if (dueIdx >= 0)  {
    if (snoozed) display.print ("Snoozed...");
    else display.printf("Call %s!", contacts[dueIdx].name);
  }
  display.display();
}

void setup() {
  Serial.begin(115200)

  pinMode(PIN_BTN_1, INPUT_PULLUP);
  pinMode(PIN_BTN_2, INPUT_PULLUP);
  pinMode(PIN_BTN_3, INPUT_PULLUP);

  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(400000);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED not found, check SDA/SCL wiring");
  }

  mpu0k = mpuInit();
  if (!mpu0k) Serial.println("MPU6050 not found at 0x68 - tilt disabled");

  dht.begin();
  ledcAttach(PIN_BUZZER, 2000, 8);

  for (int i = 0; i < NUM_CONTACTS; i++) contacts[i].lastContactMs = millis();

  beep(1568, 100)
}

void loop() {
  if (wasPressed(btnNext)) {
    selected = (selected + 1) % NUM_CONTACTS;
  }
  if(wasPressed(btnDone)) {
    contacts [selected].lastContactMs = millis();
    beep (1568, 80);
  }
  if(wasPressed(btnSnooze)) {
    snoozeUntilMs = millis() + SNOOZE_MS;
    beep (660, 80);
  }

  updateTilt();

  readSensor();

  bool snoozed = (int32_t)(snoozeUntilMs - millis()) > 0;
  int due = overdueIndex();
  if (due >= 0 && !snoozed && (millis() - lastChimeMs) > CHIME_REPEAT_MS) {
    chime();
    lastChimeMs = millis;
  }
  
  drawScreen(due, snoozed);
}