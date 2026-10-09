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

  readSensor();

  bool snoozed = (int32_t)(snoozeUntilMs - millis()) > 0;
  int due = overdueIndex();
  if (due >= 0 && !snoozed && (millis() - lastChimeMs) > CHIME_REPEAT_MS) {
    chime();
    lastChimeMs = millis;
  }
  
  drawScreen(due, snoozed);
}