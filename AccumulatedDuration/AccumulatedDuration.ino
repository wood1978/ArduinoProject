#include <LiquidCrystal_I2C.h>
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
#define EEPROM_SIZE 1020
#define BLOCK_SIZE 5
#define MAX_BLOCKS (EEPROM_SIZE / BLOCK_SIZE)

#define MAGIC_TIME 0xAB

#define ZERO_BAR_ADC_VALUE 196
#define ADC_OFFSET -9

LiquidCrystal_I2C lcd(0x27,  16, 2); //SDA:A4, SCL:A5

unsigned int eepromIndex = 0;  // 動態輪轉位置

const int enablePin = 2;

unsigned long enableLowStart = 0;
unsigned long resetHighStart = 0;
unsigned long accumulatedSeconds = 0;
bool wasLow = false;

const unsigned long SAVE_INTERVAL = 60000;
const unsigned long RESET_INTERVAL = 10000;
unsigned long lastSaveMillis = 0;

float pressure = 0;

void setup() {
  pinMode(enablePin, INPUT);

#if DEBUG_LOG
  Serial.begin(9600);
  delay(500);
  Serial.println("=== Start ===");
#endif

  lcd.init();
  lcd.backlight();

  // 讀取 EEPROM
  accumulatedSeconds = loadLastEEPROMRecord();

  displayUpdate(accumulatedSeconds);

  DEBUG_PRINT("Accum seconds: ");
  DEBUG_PRINTLN(accumulatedSeconds);
}

void loop() {
  int state = digitalRead(enablePin);
  unsigned long nowMillis = millis();
  unsigned long totalSeconds;
  bool storeData = false;
  
  // Input 低電位時開始累計
  if (state == LOW) {
    if (!wasLow) {
      enableLowStart = nowMillis;
      wasLow = true;
      DEBUG_PRINTLN("LOW start");
    }

    if (nowMillis >= enableLowStart)
      totalSeconds = accumulatedSeconds + (nowMillis - enableLowStart) / 1000;
    else
      totalSeconds = accumulatedSeconds + (nowMillis + (0xFFFFFFFF - enableLowStart)) / 1000;

    if (nowMillis >= lastSaveMillis) {
      if (nowMillis - lastSaveMillis >= SAVE_INTERVAL)
        storeData = true;
    }
    else {
      if (0xFFFFFFFF - lastSaveMillis + nowMillis >= SAVE_INTERVAL)
        storeData = true;
    }
  } else {
    if (wasLow) {
      if (nowMillis >= enableLowStart)
        accumulatedSeconds += (nowMillis - enableLowStart) / 1000;
      else
        accumulatedSeconds += (nowMillis + (0xFFFFFFFF - enableLowStart)) / 1000;
      wasLow = false;
      storeData = true;
      DEBUG_PRINT("High ");
    }
    totalSeconds = accumulatedSeconds;
  }

  if (storeData) {
    saveEEPROMRecord(totalSeconds);
    lastSaveMillis = nowMillis;
  }

  readADC();
  
  displayUpdate(totalSeconds);

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

  lcd.setCursor(0, 1);

  memset(buf, ' ', 17);
  dtostrf(pressure, 5, 1, buf);
  snprintf(buf + 5, sizeof(buf) - 5, " bar H %s", wasLow ? "Prod" : "Stop");
  lcd.setCursor(0, 1);
  lcd.print(buf);

  //DEBUG_PRINT("Display: ");
  //DEBUG_PRINTLN(buf);
}

// -------- EEPROM 備援寫入與讀取 --------
void saveEEPROMRecord(unsigned long seconds) {
  int addr = eepromIndex * BLOCK_SIZE;

  for (int i = 0; i < 4; i++)
    EEPROM.update(addr + i, (seconds >> (8 * i)) & 0xFF);
  
  EEPROM.update(addr + 4, MAGIC_TIME);  // time magic

  DEBUG_PRINT("eepromIndex: ");
  DEBUG_PRINT(eepromIndex);
  DEBUG_PRINT(" Save time: ");
  DEBUG_PRINTLN(seconds);
  eepromIndex = (eepromIndex + 1) % MAX_BLOCKS;
  
}

unsigned long loadLastEEPROMRecord() {
  unsigned int latestIndex = 0;
  unsigned long oldTime = 0, time;
  int addr;

  for (unsigned int i = 0; i < MAX_BLOCKS; i++) {
    addr = i * BLOCK_SIZE;
    
    if (EEPROM.read(addr + 4) == MAGIC_TIME) {
      time = 0;
      for (int j = 0; j < 4; j++)
        time |= ((unsigned long)EEPROM.read(addr + j)) << (8 * j);
      
      if (time >= oldTime) {
        oldTime = time;
        latestIndex = i;
      }
    }
  }

  DEBUG_PRINT("Load latestIndex: ");
  DEBUG_PRINT(latestIndex);
  DEBUG_PRINT(" time: ");
  DEBUG_PRINTLN(oldTime);

  eepromIndex = (latestIndex + 1) % MAX_BLOCKS;
  
  return oldTime;
}

void readADC() {
  int adcValue = analogRead(A0) - ZERO_BAR_ADC_VALUE + ADC_OFFSET;
  float newPressure;
  newPressure = adcValue / 56.975467;
  //DEBUG_PRINT("adcValue: ");
  //DEBUG_PRINTLN(adcValue);
  if (pressure != newPressure) {
    pressure = newPressure;
    if (pressure < 0)
      pressure = 0;
  }
}