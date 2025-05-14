#include <LiquidCrystal.h>

// LCD 接腳：RS, E, D4, D5, D6, D7（LCD Keypad Shield 預設）
LiquidCrystal lcd(8, 9, 4, 5, 6, 7);

// 馬達控制腳位
const int stepPin = 3;
const int dirPin = 2;
const int enablePin = A1;


int rpm = 100;
int currentRpm = 5;
int maxRpm = 500;
int minRpm = 10;
int stepsPerRev = 200;
bool dirForward = false;
bool motorRunning = false;
bool motorEnable = false;
unsigned long lastCheckButton = 0;
unsigned long prevStepTime = 0;
unsigned long lastAdjustTime = 0;
int currentStepDelayUs = 0;

bool lastDirection = true;  // 儲存上一次方向

void updateDisplay() {
  lcd.clear();
  lcd.setCursor(12, 0);
  lcd.print(motorEnable ? "RUN" : "STOP");
  lcd.setCursor(0, 1);
  lcd.print("RPM:");
  lcd.print(rpm);
  // 顯示實際轉動物體的轉速, 馬達30齒, 轉盤25齒
  //int actualRpm = rpm * 1.2;
  //lcd.print(actualRpm);
  lcd.setCursor(10, 1);
  lcd.print(" Dir:");
  lcd.print(dirForward ? "R" : "L");
  lcd.setCursor(12, 0);
}

bool checkButton() {
  int key = analogRead(A0);

  if (motorRunning == false) {
    if (key < 50) { //RIGHT
      dirForward = true;
      goto keyin;
    }
    else if (key < 200) { //UP
      rpm += 10;
      if (rpm > maxRpm)
        rpm = maxRpm;
      goto keyin;
    }
    else if (key < 400) { //DOWN
      rpm -= 10;
      if (rpm < minRpm) 
        rpm = minRpm;
      goto keyin;
    }
    else if (key < 600) { //LEFT
      dirForward = false;
      goto keyin;
    }
    else if (key < 800) { //SELECT
      motorEnable = true;
      goto keyin;
    }
  }
  else {
    if ((key >= 600) && (key < 800)) { //SELECT
      motorEnable = false;
      //lcd.print("STOP");
    }
  }

  return false;

keyin:
  updateDisplay();
  return true;
}

void updateCurrentSpeed() {
  if (motorEnable == true) {
    if (millis() - lastAdjustTime >= 10)
      goto adjustTime;
  }
  else {
    if (millis() - lastAdjustTime >= 20)
      goto adjustTime;
  }

  return;

adjustTime:
  lastAdjustTime = millis();
  if (motorEnable == true) {
    if (currentRpm < rpm) {
      currentRpm++;
      goto update;
    }
    else if (currentRpm > rpm) {
      currentRpm--;
      goto update;
    }
  }
  else {
    if (currentRpm > 5) {
      currentRpm--;
      goto update;
    }
    else {
      motorRunning = false;
      updateDisplay();
    }
  }
  return;

update:
  if (currentRpm > 0) //馬達30齒, 轉盤25齒
    currentStepDelayUs = 60000000L / currentRpm / stepsPerRev / 1.2;

}

void setup() {
  pinMode(stepPin, OUTPUT);
  pinMode(dirPin, OUTPUT);
  pinMode(enablePin, OUTPUT);

  digitalWrite(enablePin, LOW);  // 預設關閉 ENA
  digitalWrite(dirPin, HIGH);
  digitalWrite(stepPin, HIGH);  

  lcd.begin(16, 2);
  updateDisplay();
}

void loop() {
  unsigned long now;

  now = micros();
  if (now >= lastCheckButton) {
    if (now - lastCheckButton < 300000)
      goto skipCheckButton;
  }
  else {
    if (now + lastCheckButton < 300000)
      goto skipCheckButton;
  }
  checkButton();
  lastCheckButton = now;

skipCheckButton:
  if (motorEnable == true) {
    if (motorRunning == false) {
      digitalWrite(enablePin, HIGH);
      delayMicroseconds(10);
      if (lastDirection != dirForward) {
        digitalWrite(dirPin, dirForward ? LOW : HIGH);
        delayMicroseconds(5);  // DIR 等待
        lastDirection = dirForward;
      }
      motorRunning = true;
    }
    goto motorControl;  
  }
  else {
    if (motorRunning == true)
      goto motorControl;
    else 
      digitalWrite(enablePin, LOW);  
  }
  return;

motorControl:
  updateCurrentSpeed();

  now = micros();
  if (now >= prevStepTime) {
    if (now - prevStepTime >= currentStepDelayUs) 
      goto setStep;
  }
  else {
    if (now + prevStepTime >= currentStepDelayUs)
      goto setStep;
  }
  return;

setStep:
  digitalWrite(stepPin, LOW);
  delayMicroseconds(5);
  digitalWrite(stepPin, HIGH);
  prevStepTime = now;
}
