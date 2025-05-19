#include <Wire.h>
#include <LiquidCrystal.h>
#include <EEPROM.h>

// -------- 編譯選項 --------
#define DEBUG_LOG 1

#if DEBUG_LOG
#define DEBUG_PRINT(x) Serial.print(x)
#define DEBUG_PRINTLN(x) Serial.println(x)
#else
#define DEBUG_PRINT(x)
#define DEBUG_PRINTLN(x)
#endif

// -------------------------
// LCD1602 keypad 接腳：RS, E, D4, D5, D6, D7（LCD Keypad Shield 預設）
LiquidCrystal lcd(8, 9, 4, 5, 6, 7);

const int inputPin = 2;

const byte MAGIC = 0xAC;

const int TIME1_ADDR = 0;
const int TIME2_ADDR = 5;

unsigned long highStart = 0;
unsigned long accumulatedSeconds = 0;
bool wasHigh = false;

const unsigned long SAVE_INTERVAL = 60000;
unsigned long lastSaveMillis = 0;

void setup() {
  pinMode(inputPin, INPUT);

#if DEBUG_LOG
  Serial.begin(9600);
  delay(500);
  Serial.println("=== Start ===");
#endif

  lcd.begin(16, 2);

  // 讀取 EEPROM
  accumulatedSeconds = loadLastValidRecord(TIME1_ADDR, TIME2_ADDR);

  displayUpdate(accumulatedSeconds);

  DEBUG_PRINT("Accum seconds: ");
  DEBUG_PRINTLN(accumulatedSeconds);
}

void loop() {
  int state = digitalRead(inputPin);
  unsigned long nowMillis = millis();
  unsigned long totalSeconds;
  bool storeData = false;

  // Input 高電位時開始累計
  if (state == HIGH) {
    if (!wasHigh) {
      highStart = nowMillis;
      wasHigh = true;
      DEBUG_PRINTLN("HIGH start");
    }

    totalSeconds = accumulatedSeconds + (nowMillis - highStart) / 1000;
    displayUpdate(totalSeconds);

    if (nowMillis >= lastSaveMillis) {
      if (nowMillis - lastSaveMillis >= SAVE_INTERVAL)
        storeData = true;
    }
    else {
      if (0xFFFFFFFF - lastSaveMillis + nowMillis >= SAVE_INTERVAL)
        storeData = true;
    }
  } else {
    if (wasHigh) {
      accumulatedSeconds += (nowMillis - highStart) / 1000;
      wasHigh = false;
      totalSeconds = accumulatedSeconds;
      storeData = true;
      DEBUG_PRINT("LOW ");
    }
  }

  if (storeData) {
    saveRecordWithDoubleBackup(TIME1_ADDR, TIME2_ADDR, totalSeconds);
    lastSaveMillis = nowMillis;
    DEBUG_PRINT("Saved seconds: ");
    DEBUG_PRINTLN(totalSeconds);
  }

  delay(100);
}

void displayUpdate(unsigned long seconds) {
  char buf[17];

  unsigned int hours = seconds / 3600UL;
  seconds %= 3600UL;

  unsigned int minutes = seconds / 60UL;
  seconds %= 60UL;

  snprintf(buf, sizeof(buf), "Time %05u:%02u:%02u",
           hours, minutes, seconds);

  lcd.setCursor(0, 0);
  lcd.print(buf);

  DEBUG_PRINT("Display: ");
  DEBUG_PRINTLN(buf);
}

// -------- EEPROM 備援寫入與讀取 --------

void saveRecordWithDoubleBackup(int addr1, int addr2, unsigned long value) {
  static bool toggle = false;
  int addr = toggle ? addr1 : addr2;
  toggle = !toggle;

  for (int i = 0; i < 4; i++)
    EEPROM.update(addr + i, (value >> (8 * i)) & 0xFF);

  EEPROM.update(addr + 4, MAGIC);  // 最後寫 magic byte
}

unsigned long readRecord(int addr) {
  unsigned long value = 0;

  if (EEPROM.read(addr + 4) != MAGIC) return 0;

  for (int i = 0; i < 4; i++)
    value |= ((unsigned long)EEPROM.read(addr + i)) << (8 * i);

  return value;
}

unsigned long loadLastValidRecord(int addr1, int addr2) {
  unsigned long v1 = readRecord(addr1);
  unsigned long v2 = readRecord(addr2);
  DEBUG_PRINT("EEPROM A: ");
  DEBUG_PRINTLN(v1);
  DEBUG_PRINT("EEPROM B: ");
  DEBUG_PRINTLN(v2);
  return max(v1, v2);
}
