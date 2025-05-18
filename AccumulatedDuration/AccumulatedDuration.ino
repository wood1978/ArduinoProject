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
const int resetButtonPin = 3;

const byte MAGIC = 0xAB;

const int TIME1_ADDR = 0;
const int TIME2_ADDR = 5;
const int BOOT1_ADDR = 10;
const int BOOT2_ADDR = 15;

unsigned long highStart = 0;
unsigned long accumulatedSeconds = 0;
bool wasHigh = false;
unsigned long bootCount = 0;

const unsigned long SAVE_INTERVAL = 5000;
unsigned long lastSaveMillis = 0;

void setup() {
  pinMode(inputPin, INPUT);
  pinMode(resetButtonPin, INPUT_PULLUP);

#if DEBUG_LOG
  Serial.begin(9600);
  delay(500);
  Serial.println("=== Start ===");
#endif

  lcd.begin(16, 2);
  //lcd.backlight();

  // 讀取 EEPROM
  accumulatedSeconds = loadLastValidRecord(TIME1_ADDR, TIME2_ADDR);
  bootCount = loadLastValidRecord(BOOT1_ADDR, BOOT2_ADDR) + 1;
  saveRecordWithDoubleBackup(BOOT1_ADDR, BOOT2_ADDR, bootCount);

  lcd.setCursor(0, 0);
  lcd.print("Boot: ");
  lcd.print(bootCount);

  displayFormattedTime(accumulatedSeconds);

  DEBUG_PRINT("Boot count: ");
  DEBUG_PRINTLN(bootCount);
  DEBUG_PRINT("Accum seconds: ");
  DEBUG_PRINTLN(accumulatedSeconds);
}

void loop() {
  int state = digitalRead(inputPin);
  bool resetPressed = digitalRead(resetButtonPin) == LOW;
  unsigned long nowMillis = millis();

  // reset
  if (resetPressed) {
    accumulatedSeconds = 0;
    saveRecordWithDoubleBackup(TIME1_ADDR, TIME2_ADDR, accumulatedSeconds);
    displayFormattedTime(accumulatedSeconds);
    DEBUG_PRINTLN("RESET pressed: counter cleared");
    delay(1000);
  }

  // Input 高電位時開始累計
  if (state == HIGH) {
    if (!wasHigh) {
      highStart = nowMillis;
      wasHigh = true;
      DEBUG_PRINTLN("HIGH start");
    }

    unsigned long totalSeconds = accumulatedSeconds + (nowMillis - highStart) / 1000;
    displayFormattedTime(totalSeconds);

    if (nowMillis - lastSaveMillis >= SAVE_INTERVAL) {
      saveRecordWithDoubleBackup(TIME1_ADDR, TIME2_ADDR, totalSeconds);
      lastSaveMillis = nowMillis;
      DEBUG_PRINT("Saved seconds: ");
      DEBUG_PRINTLN(totalSeconds);
    }

  } else {
    if (wasHigh) {
      accumulatedSeconds += (nowMillis - highStart) / 1000;
      wasHigh = false;
      DEBUG_PRINT("LOW - total updated: ");
      DEBUG_PRINTLN(accumulatedSeconds);
    }
  }

  delay(100);
}

// 顯示格式：YYDD HH:MM:SS
void displayFormattedTime(unsigned long seconds) {
  unsigned int years = seconds / 31536000UL;
  seconds %= 31536000UL;

  unsigned int days = seconds / 86400UL;
  seconds %= 86400UL;

  unsigned int hours = seconds / 3600UL;
  seconds %= 3600UL;

  unsigned int minutes = seconds / 60UL;
  seconds %= 60UL;

  char buf[17];
  snprintf(buf, sizeof(buf), "%02uY%02uD %02u:%02u:%02u",
           years, days, hours, minutes, seconds);

  lcd.setCursor(0, 1);
  lcd.print("                ");
  lcd.setCursor(0, 1);
  lcd.print(buf);

  DEBUG_PRINT("Display: ");
  DEBUG_PRINTLN(buf);
}

// -------- EEPROM 備援寫入與讀取 --------

void saveRecordWithDoubleBackup(int addr1, int addr2, unsigned long value) {
  static bool toggle = false;
  int addr = toggle ? addr1 : addr2;
  toggle = !toggle;

  for (int i = 0; i < 4; i++) {
    EEPROM.update(addr + i, (value >> (8 * i)) & 0xFF);
  }
  EEPROM.update(addr + 4, MAGIC);  // 最後寫 magic byte
}

unsigned long readRecord(int addr) {
  if (EEPROM.read(addr + 4) != MAGIC) return 0;
  unsigned long value = 0;
  for (int i = 0; i < 4; i++) {
    value |= ((unsigned long)EEPROM.read(addr + i)) << (8 * i);
  }
  return value;
}

unsigned long loadLastValidRecord(int addr1, int addr2) {
  unsigned long v1 = readRecord(addr1);
  unsigned long v2 = readRecord(addr2);
  DEBUG_PRINT("EEPROM A: "); DEBUG_PRINTLN(v1);
  DEBUG_PRINT("EEPROM B: "); DEBUG_PRINTLN(v2);
  return max(v1, v2);
}
