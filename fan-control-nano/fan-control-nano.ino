/*
  A 5 channel fan controller for 2, 3, and 4 wire fans.
  Code under GPLv3 license. Author: gajdost (gajdipajti@gmail.com)

Pin Assignment:

  Fan channels: 
   * PIN D5  (Timer0) PWM - ChA - 2 or 3 pin fans only
   * PIN D6  (Timer0) PWM - ChB - 2 or 3 pin fans only
   * PIN D9  (Timer1) PWM - ChC - configured for 4 pin fan
   * PIN D10 (Timer1) PWM - ChD - configured for 4 pin fan
   * PIN D11 (Timer2) PWM - ChE - 2, 3, or 4 pin fans
  Optional fan channel:
   * PIN D3  (Timer2) PWM - ChF - 2, 3, or 4 pin fans OR tachometer interrupt

  Detect interrupts:
   * PIN D2           Tachometer - Interrupt ChC
   * PIN D3  (Timer2) Tachometer - Interrupt ChD

  Temperature measurement (one is enough):
   * PIN D12          Dallas 1-wire
   * PIN A7  (ADC)    LM35
  Optional temperature pins:
   * PIN A0  (ADC) - tF
   * PIN A1  (ADC) - tE
   * PIN A2  (ADC) - tD
   * PIN A3  (ADC) - tC
   * PIN A6  (ADC) - tB

  Optional display pins:
   * PIN D4        - button press
   * PIN A4  (LCD) - reserved for I2C
   * PIN A5  (LCD) - reserved for I2C

  Heartbeat pin:
   * PIN D13 (LED) - heartbeat

Serial Commands:

  Main Functions:
   * fan?             - GET installed fans, all information
   * pwm?             - GET all PWM outputs
   * pwm[A-F]?        - GET PWM output
   * pwm[A-F]p        - SET PWM output to pilot mode
   * pwm[A-F][0-255]  - SET PWM output manually
   * rpm[C,D]?        - GET RPM output from 4-wire fans
   * t?               - GET measured temperature
   * t[D,L]?          - GET measured temperature from selected source
   * ts?              - GET temperature sources (example Dallas 1-wire or LM35)

  Optional Functions: [when extra circuit is present]
   * lcd[?,0-4] - GET/SET LCD Backlight; Update LCD
  Calibration Functions:
   * Ch[L,H,C][A-F]?        - GET LOW HIGH temperatue for a channel
   * Ch[L,H,C][A-F][0-100]  - SET LOW HIGH temperatue for a channel
   * p[L,H][A-F]?         - GET LOW HIGH PWM speed for a channel
   * p[L,H][A-F][0-255]   - SET LOW HIGH PWM for a channel
  Misc Func:
   * hrs?           - get board uptime [%d:%H:%M:%S]
   * ver?           - HW board version number
   * cbn?           - CODE build version number
   * ?              - Ping, Are You There?  + Flash a LED
   * stream         - flip ON/OFF stream mode
   * m?             - GET information for munin setup          
   * m!             - GET information for munin monitoring
*/
#include <Wire.h> 
#include <uptime.h>
#include <OneWire.h>
// #include <LiquidCrystal_I2C.h>
// LiquidCrystal_I2C lcd(0x27,16,2);  // set the LCD address to 0x27 for a 16 chars and 2 line display

// Fan configuration - change this
String fanconfiguration = "5ABCDE";
const bool fanmask[5] = {true, true, true, true, true};
// Variables and constants which might be checked in the GUI
const byte  swVersion = 001;
const float hwVersion = 1.0;
const char  boardSubVersion = 'A';
float time;

// Define PWM pins Channel A-E
const byte pwm[5] = {5, 6, 9, 10, 11};
// const byte pwmF = 3;     // Used as interrupt

// Define 2D settings array for pwm and temperature
// ROWS: Channel A-E
// COLS: manual[0-1], current[0-255], LOW[0-255], HIGH[0-255], CRITICAL[255], ...
//                                         tLOW[0-85], tHIGH[0-85], tCRITICAL[85]
byte preset[5][8] = {
  {0, 128, 30, 200, 255, 15, 60, 85},
  {0, 128, 30, 200, 255, 15, 60, 85},
  {0, 128, 30, 200, 255, 15, 60, 85},
  {0, 128, 30, 200, 255, 15, 60, 85},
  {0, 128, 30, 200, 255, 15, 60, 85}
};

// Define interrupt pins
const byte intC = 2;
const byte intD = 3;

// Store rpm values in vol
volatile unsigned int rpmC = 0;
volatile unsigned int rpmD = 0;

// Define temperature and lcd pins
char tempSource = 'L';      // The source of the temperature: 'L' - LM35, 'D' - DS18B20, ...
const byte lm35 = A7;       // LM35 connected
const byte ds18 = 12;       // DS18B20 sensor is connected
OneWire  ds(ds18);

// A4 -> LCD SDA
// A5 -> LCD SCL
// A8 is conncected to the internal temperature sensor.

// For LCD
// bool lcdState = HIGH;

// For serial communication.
String inputString = "";        // a string to hold incoming data
bool stringComplete = false;    // whether the string is complete
bool automode = true;           // enable auto mode

// Wait mode
short waitPeriod = 1000;
unsigned long startWait = 0;

void setup() {  // The initial setup, that will run every time when we connect to the Arduino via serial.
                // To disable the auto reset, change the switch position on the board.

  pinMode(LED_BUILTIN, OUTPUT); digitalWrite(LED_BUILTIN, HIGH);
  
  Serial.begin(115200);
  
  // Voltage channel outputs, it's not needed for analogWrite, but just in case.
  // https://www.arduino.cc/reference/en/language/functions/analog-io/analogwrite/
  pinMode(pwm[0], OUTPUT); analogWrite(pwm[0], LOW);
  pinMode(pwm[1], OUTPUT); analogWrite(pwm[1], LOW);
  pinMode(pwm[2], OUTPUT); analogWrite(pwm[2], LOW);
  pinMode(pwm[3], OUTPUT); analogWrite(pwm[3], LOW);
  pinMode(pwm[4], OUTPUT); analogWrite(pwm[4], LOW);
  // pinMode(pwmF, OUTPUT); analogWrite(pwmF, LOW);  // Not used

  // Temperature input
  analogReference(INTERNAL);    // set AREF to 1.1 V
  pinMode(lm35, INPUT);

  // Copied the relevant part from here: http://playground.arduino.cc/Code/PwmFrequency
  TCCR1B = TCCR1B & 0b11111000 | 0x01;    // Change PWM Frequency for Timer1.  f=~31kHz
  // TCCR2B = TCCR2B & 0b11111000 | 0x01;    // Change PWM Frequency for Timer2.  f=~31kHz

  delay(1000);
  
  inputString.reserve(128);   // reserve 128 bytes for the inputString
  digitalWrite(LED_BUILTIN, LOW);
}

float lm35Temp() {
  // LM35DZ: 10mV/°C -> (adc*(aref/1024))/10
  // Current settings for AREF = 1100 mV
  // Resolution: 0,11°C; Precision +/- 1 °C
  // Range: 0°C - 110°C [0 - 1023 ADC]
  float tmV = analogRead(lm35);
  return tmV*(110.00/1024.00);
}

float ds18Temp() {
  // TODO
  return 0.00;
}

float getTemperature(char Source) {
  // NOTE: Implement here other sources.
  switch (Source) {
    case 'L':
      return lm35Temp(); break;
    case 'D':
      return ds18Temp(); break;
    default:
      break;
  }
}

void loop() {
  if (stringComplete) {
    if (inputString.startsWith("t")) {
      switch(inputString.charAt(1)) {
        case '?':
          Serial.println(getTemperature(tempSource)); break;
        case 's':
          Serial.println(tempSource); break;  // Display source
        case 'L':
          Serial.println(lm35Temp()); break;  // Only for debugging
        case 'D':
          Serial.println(ds18Temp()); break;  // Only for debugging
        default:
          Serial.print("Syntax Error: "); Serial.println(inputString);
          break;
      }
    } else if (inputString.startsWith("pwm")) {
      if (inputString.endsWith("?")) {
        // Read current pwm settings from array.
        switch(inputString.charAt(3)) {
          case 'A':
            Serial.println(preset[0][1]); break;
          case 'B':
            Serial.println(preset[1][1]); break;
          case 'C':
            Serial.println(preset[2][1]); break;
          case 'D':
            Serial.println(preset[3][1]); break;
          case 'E':
            Serial.println(preset[4][1]); break;
          case '?':
            Serial.print(preset[0][1]); Serial.print('; ');
            Serial.print(preset[1][1]); Serial.print('; ');
            Serial.print(preset[2][1]); Serial.print('; ');
            Serial.print(preset[3][1]); Serial.print('; ');
            Serial.println(preset[4][1]); break;
          default:
            Serial.print("Syntax Error: "); Serial.println(inputString);
            break;
        }
      } else if (inputString.endsWith("p")) {
        // Set to pilot mode from manual.
        int value = constrain(inputString.charAt(3),65,69)-65;
        preset[value][0] = 0;
        Serial.println("OK");
      } else {
        // Manually set the pwm value. Set to manual mode.
        int value = constrain(inputString.charAt(3),65,69)-65;
        preset[value][1] = constrain(byte(inputString.substring(4).toInt()),0,255);
        preset[value][0] = 1;
        analogWrite(pwm[value],inputString.substring(4).toInt());
        Serial.println("OK");
      }
    } else if (inputString.startsWith("rpm")) {
      switch(inputString.charAt(3)) {
        case 'C':
          Serial.println(rpmC); break;
        case 'D':
          Serial.println(rpmD); break;
        case '?':
          Serial.print(rpmC); Serial.print('; ');
          Serial.println(rpmD); break;
        default:
          Serial.print("Syntax Error: "); Serial.println(inputString);
          break;
      } 
    } else if (inputString.startsWith("fan?")) {
      // Print information about the fans
      Serial.println(fanconfiguration);
    } else if (inputString.startsWith("?")) {
      // Are you there?
      digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
      Serial.println("OK");
    } else if (inputString.startsWith("hrs?")) {
      // Uptime
      uptime::calculateUptime();
      Serial.print(uptime::getDays());      Serial.print(":");
      Serial.print(uptime::getHours());     Serial.print(":");
      Serial.print(uptime::getMinutes());   Serial.print(":");
      Serial.println(uptime::getSeconds());

    } else if (inputString.startsWith("cbn?")) { Serial.println(swVersion);  // Code Build Number
    } else if (inputString.startsWith("ver?")) {
      Serial.print(hwVersion, 1); Serial.println(boardSubVersion);            // Version Number + Board SubVersion
    } else if (inputString.startsWith("p")) {
      int value = constrain(inputString.charAt(2),65,69)-65;
      if (inputString.endsWith("?")) {
        // GET LOW or HIGH pwm settings.
        switch(inputString.charAt(1)) {
          case 'L':
            Serial.println(preset[value][2]); break;
          case 'M':
            Serial.println(preset[value][3]); break;
          default:
            Serial.print("Syntax Error: "); Serial.println(inputString);
            break;
        }
      } else {
        // SET LOW or HIGH pwm settings.
        // But constrain LOW between 0 and HIGH, and constrain HIGH between LOW and CRITICAL.
        switch(inputString.charAt(1)) {
          case 'L':
            preset[value][2] = constrain(byte(inputString.substring(3).toInt()), 0, preset[value][3]);
            Serial.print(preset[value][2]); 
            Serial.println(" OK"); break;
          case 'M':
            preset[value][3] = constrain(byte(inputString.substring(3).toInt()), preset[value][2], preset[value][4]);
            Serial.print(preset[value][3]); 
            Serial.println(" OK"); break;
          default:
            Serial.print("Syntax Error: "); Serial.println(inputString);
            break;
        }     
      }
    } else if (inputString.startsWith("Ch")) {
      int value = constrain(inputString.charAt(3),65,69)-65;
      if (inputString.endsWith("?")) {
        // GET LOW or HIGH pwm settings.
        switch(inputString.charAt(2)) {
          case 'L':
            Serial.println(preset[value][5]); break;
          case 'M':
            Serial.println(preset[value][6]); break;
          default:
            Serial.print("Syntax Error: "); Serial.println(inputString);
            break;
        }
      } else {
        // SET LOW or HIGH pwm settings.
        // But constrain LOW between 0 and HIGH, and constrain HIGH between LOW and CRITICAL.
        switch(inputString.charAt(1)) {
          case 'L':
            preset[value][5] = constrain(byte(inputString.substring(4).toInt()), 0, preset[value][6]);
            Serial.print(preset[value][5]); 
            Serial.println(" OK"); break;
          case 'M':
            preset[value][3] = constrain(byte(inputString.substring(4).toInt()), preset[value][5], preset[value][7]);
            Serial.println(preset[value][6]);
            Serial.println(" OK"); break;
          default:
            Serial.print("Syntax Error: "); Serial.println(inputString);
            break;
        }     
      }
    }
    // Do your thing
  } else if (automode) {
    // GET temperature in float, convert to int
    // UPDATE pwm if not in manual
    int currentTemp = int(getTemperature(tempSource));
    for (int idx = 0; idx < fanconfiguration.charAt(0); idx++) {
      if (fanmask[idx] && (preset[idx][0] == 0)) {
        preset[idx][1] = map(currentTemp, preset[idx][5], preset[idx][6], preset[idx][2], preset[idx][3]);
        analogWrite(pwm[idx], preset[idx][1]);
      }
    }
  }
  inputString = ""; stringComplete = false;      //clear the string
  //serialEvent();  // Workaround for ATTiny or ATMega chips without hardware serial
}

void serialEvent() {
 // SerialEvent occurs whenever a new data comes in the hardware serial RX. This routine is run between each
 // time loop() runs, so using delay inside loop can delay response. Multiple bytes of data may be available.
  while (Serial.available()) {
    char inChar = (char)Serial.read();          // get the new byte
    if (inChar == '\r') {                       // if the incoming character is a carriage return (ASCII 13),
      stringComplete = true;                    // set a flag so the main loop can do something about it.
    } else { inputString += inChar; }           // otherwies add it to the inputString                             
  }
}
