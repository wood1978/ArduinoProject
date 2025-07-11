#include <EEPROM.h>
#include <LiquidCrystal_I2C.h>
#include <ModbusMaster.h>
#include <SoftwareSerial.h>

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

#define EEPROM_MAGIC_TIME 0xEF

#define WITHOUT_CD4069 1
#if WITHOUT_CD4069
#define MAX485_RE_NEG_PIN 4
#define MAX485_DE_PIN 3
#endif
#define MAX485_RX_PIN 5 // MAX485_RO
#define MAX485_TX_PIN 2 // MAX485_DI

#define RELAY_CONTROL_PIN 6

#define PRODUCE_RATE_MAX 3000

LiquidCrystal_I2C lcd(0x27,  20, 4); //SDA:A4, SCL:A5
SoftwareSerial rs485Serial(MAX485_RX_PIN, MAX485_TX_PIN);
ModbusMaster node;

unsigned long enableLowStart = 0;
unsigned long resetHighStart = 0;
unsigned long accumulatedSeconds = 0;

unsigned int eepromIndex = 0; //Loop all EEPROM by index
unsigned int alarm2Enable = 0;

bool pressureLow = false;
bool produceEnable = false;

const unsigned long SAVE_INTERVAL = 60000;
unsigned long lastSaveMillis = 0;

unsigned int produceRate = 0;
float pressure = 0, pressureAl1 = 0, pressureAl2 = 0;

void preTransmission() {
#if WITHOUT_CD4069
  digitalWrite(MAX485_RE_NEG_PIN, HIGH);
  digitalWrite(MAX485_DE_PIN, HIGH);
#endif
}

void postTransmission() {
#if WITHOUT_CD4069
  digitalWrite(MAX485_RE_NEG_PIN, LOW);
  digitalWrite(MAX485_DE_PIN, LOW);
#endif
}

unsigned long loadLastEEPROMRecord() {
  unsigned int latestIndex = 0;
  unsigned long oldTime = 0, time;
  int addr;

  for (unsigned int i = 0; i < MAX_BLOCKS; i++) {
    addr = i * BLOCK_SIZE;
    
    if (EEPROM.read(addr + 4) == EEPROM_MAGIC_TIME) {
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

void displayUpdate(unsigned long seconds) {
  char buf[21], bufAl1[6], bufAl2[7];

  unsigned int hours = seconds / 3600UL;
  seconds %= 3600UL;

  unsigned int minutes = seconds / 60UL;
  seconds %= 60UL;

  snprintf(buf, sizeof(buf), "[Time = %05u:%02u:%02u]",
    hours, minutes, seconds);
  lcd.setCursor(0, 0);
  lcd.print(buf);

  memset(buf, ' ', 21);
  if (pressureLow == true) {
    if (produceRate < PRODUCE_RATE_MAX)
      produceRate += 1000;
    snprintf(buf, sizeof(buf), "H Produce %04uml/min", produceRate);
  }
  else {
    if (produceRate > 0)
      produceRate -= 1000;
    snprintf(buf, sizeof(buf), "H Stop    %04uml/min", produceRate);
  }
  lcd.setCursor(0, 1);
  lcd.print(buf);
  
  dtostrf(pressure, 5, 2, buf);
  if (alarm2Enable > 0) {
    if (alarm2Enable < 2)
      alarm2Enable++;
    else
      alarm2Enable = 1;
    snprintf(buf + 5, sizeof(buf) - 5, " bar %s", (alarm2Enable % 2) ? "! ALARM2 !" : " !ALARM2! ");
  }
  else
    snprintf(buf + 5, sizeof(buf) - 5, " bar           ");
  lcd.setCursor(0, 2);
  lcd.print(buf);

  dtostrf(pressureAl1, 5, 2, bufAl1);
  dtostrf(pressureAl2, 6, 2, bufAl2);
  snprintf(buf, sizeof(buf), "AL1:%s AL2:%s", bufAl1, bufAl2);
  
  lcd.setCursor(0, 3);
  lcd.print(buf);
}

void setup() {
  pinMode(RELAY_CONTROL_PIN, OUTPUT);
  digitalWrite(RELAY_CONTROL_PIN, HIGH);

#if WITHOUT_CD4069
  // Initialize control pins
  pinMode(MAX485_RE_NEG_PIN, OUTPUT);
  pinMode(MAX485_DE_PIN, OUTPUT);
  digitalWrite(MAX485_RE_NEG_PIN, LOW);
  digitalWrite(MAX485_DE_PIN, LOW);
#endif

  // Modbus communication runs at 9600 baud
  rs485Serial.begin(9600);
  // Modbus slave ID 1
  node.begin(1, rs485Serial);
  // Callbacks allow us to configure the RS485 transceiver correctly
  node.preTransmission(preTransmission);
  node.postTransmission(postTransmission);
  
#if DEBUG_LOG
  Serial.begin(9600);
  delay(500);
  DEBUG_PRINTLN("=== Start ===");
#endif

  lcd.init();
  lcd.backlight();
  
  // Read EEPROM
  accumulatedSeconds = loadLastEEPROMRecord();
  displayUpdate(accumulatedSeconds);
  DEBUG_PRINT("Accum seconds: ");
  DEBUG_PRINTLN(accumulatedSeconds);
}

void saveEEPROMRecord(unsigned long seconds) {
  int addr = eepromIndex * BLOCK_SIZE;

  for (int i = 0; i < 4; i++)
    EEPROM.update(addr + i, (seconds >> (8 * i)) & 0xFF);
  
  EEPROM.update(addr + 4, EEPROM_MAGIC_TIME);  // time magic

  DEBUG_PRINT("eepromIndex: ");
  DEBUG_PRINT(eepromIndex);
  DEBUG_PRINT(" Save time: ");
  DEBUG_PRINTLN(seconds);
  eepromIndex = (eepromIndex + 1) % MAX_BLOCKS;
}

bool readPressure() {
  uint8_t result;
  uint16_t buffer;
  float newPressure;

  result = node.readHoldingRegisters(0x0000, 1); // Read CV
  if (result == node.ku8MBSuccess) {
    buffer = node.getResponseBuffer(0);
    //DEBUG_PRINT("0x0000: ");
    //DEBUG_PRINTLN(buffer);
    newPressure = float(buffer / 100.00F);
    if (pressure != newPressure) {
      pressure = newPressure;
      if (pressure < 0)
        pressure = 0;
    }
  }

  result = node.readHoldingRegisters(0x0002, 1); // Read AL1 value
  if (result == node.ku8MBSuccess) {
    buffer = node.getResponseBuffer(0);
    //DEBUG_PRINT("0x0000: ");
    //DEBUG_PRINTLN(buffer);
    pressureAl1 = float(buffer / 100.00F);
  }


  result = node.readHoldingRegisters(0x0003, 1); // Read AL2 value
  if (result == node.ku8MBSuccess) {
    buffer = node.getResponseBuffer(0);
    //DEBUG_PRINT("0x0000: ");
    //DEBUG_PRINTLN(buffer);
    pressureAl2 = float(buffer / 100.00F);
  }

  result = node.readHoldingRegisters(0x0005, 1); // Read alarm
  if (result == node.ku8MBSuccess) {
    buffer = node.getResponseBuffer(0);
    //DEBUG_PRINT("0x0005: ");
    //DEBUG_PRINTLN(buffer);
    if (buffer & 0x02) {
      if (alarm2Enable == 0)
        alarm2Enable = 1;
    }
    else
      alarm2Enable = 0;

    if (buffer & 0x01)
      return true;
  }

  return false;
}

void loop() {
  unsigned long nowMillis;
  unsigned long totalSeconds;
  int state;
  bool storeData = false;

  state = readPressure();
  nowMillis = millis();
  if (state == HIGH) {
    if (!pressureLow) {
      enableLowStart = nowMillis;
      pressureLow = true;
      produceEnable = true;
      digitalWrite(RELAY_CONTROL_PIN, LOW);
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
    if (pressureLow) {
      if (nowMillis >= enableLowStart)
        accumulatedSeconds += (nowMillis - enableLowStart) / 1000;
      else
        accumulatedSeconds += (nowMillis + (0xFFFFFFFF - enableLowStart)) / 1000;
      pressureLow = false;
      storeData = true;
      produceEnable = false;
      digitalWrite(RELAY_CONTROL_PIN, HIGH);
      DEBUG_PRINT("High ");
    }
    totalSeconds = accumulatedSeconds;
  }

  if (storeData) {
    saveEEPROMRecord(totalSeconds);
    lastSaveMillis = nowMillis;
  }
  
  displayUpdate(totalSeconds);

  delay(100);    
}
