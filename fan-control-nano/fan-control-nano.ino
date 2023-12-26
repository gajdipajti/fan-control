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
   * PIN D12          Dallas 1-wire DS18B20
   * PIN A7  (ADC)    LM35
   * PIN A6  (ADC)    NTC Thermistor - NTC B57164K0472K000 Rn: 4k7 Ohm; K = 3950; Rs = 4k7 Ohm
   * PIN A0  (ADC)    AD22100KTZ
  Optional temperature pins:
   * PIN A1  (ADC) - tE
   * PIN A2  (ADC) - tD
   * PIN A3  (ADC) - tC

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
   * t[A,D,L,N]?        - GET measured temperature from selected source
   * ts?              - GET temperature source (example Dallas 1-wire)
   * tsa?             - GET all available temperature sources

  Optional Functions: [when extra circuit is present]
   * lcd[?,0-4] - GET/SET LCD Backlight; Update LCD
  Calibration Functions:
   * Ch[L,H,C][A-F]?        - GET LOW HIGH temperature for a channel
   * Ch[L,H,C][A-F][0-100]  - SET LOW HIGH temperature for a channel
   * p[L,H][A-F]?           - GET LOW HIGH PWM speed for a channel
   * p[L,H][A-F][0-255]     - SET LOW HIGH PWM for a channel
  Misc Func:
   * hrs?           - get board uptime [%d:%H:%M:%S]
   * ver?           - HW board version number
   * cbn?           - CODE build version number
   * ?              - Ping, Are You There?  + Flash a LED
   * stream         - flip ON/OFF stream mode
   * m?             - GET information for munin setup          
   * m!             - GET information for munin monitoring
*/
// Ref: http://www.gammon.com.au/tips
// #define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

#include <Wire.h> 
#include <uptime.h>
#include <OneWire.h>
#include <DallasTemperature.h>
// #include <LiquidCrystal_I2C.h>
// LiquidCrystal_I2C lcd(0x27,16,2);  // set the LCD address to 0x27 for a 16 chars and 2 line display

// Fan configuration - change this
// the compiler will work out the size
const bool fanmask[] = {true, true, true, true, true};
// Variables and constants which might be checked in the GUI
const byte  swVersion = 001;
const float hwVersion = 1.0;
const char  boardSubVersion = 'A';
float time;

// Define PWM pins Channel A-E
// the compiler will work out the size
const byte pwm[] = {5, 6, 9, 10, 11};
// const byte pwmF = 3;     // Used as interrupt

// Define 2D settings array for pwm and temperature
// ROWS: Channel A-E - the compiler will work out the size of the first dimension
// COLS: manual[0-1], current[0-255], LOW[0-255], HIGH[0-255], CRITICAL[255], ...
//                                         tLOW[0-85], tHIGH[0-85], tCRITICAL[85]
byte preset[][8] = {
  {0, 128, 30, 250, 255, 20, 30, 85},
  {0, 128, 30, 250, 255, 20, 30, 85},
  {0, 128, 30, 250, 255, 20, 60, 85},
  {0, 128, 30, 250, 255, 20, 60, 85},
  {0, 128, 30, 250, 255, 20, 60, 85}
};

// Define interrupt pins
const byte intC = 2;
const byte intD = 3;

// Store rpm values in volatile time variables
volatile unsigned long t_irpmC = 0;
volatile unsigned long tC = 0;
volatile unsigned long t_irpmD = 0;
volatile unsigned long tD = 0;
byte t0_corr = 0;

// Define temperature and lcd pins
char tempSource = 'D';      // The source of the temperature: 'N' - NTC, 'L' - LM35, 'D' - DS18B20, 'A' - AD22100KTZ, ...
const byte lm35 = A7;       // if an LM35 is connected
const byte ntc  = A6;       // if an NTC is connected
const byte ad22100 = A0;    // if an AD22100KTZ is connected
const byte OneWireBus = 4;  // Data wire for OneWire
bool dallasPresent = true;  // store dallas sensor status

// Define the thermistor
// Ref for external library: https://www.arduino.cc/reference/en/libraries/thermistor/
// NTC B57164K0472K000 R25: 4k7 Ohm; K = 3950; Rs = 4k7 Ohm
float R25_ntc = 4700.00;
float K_ntc = 3950.00;
float Rs_ntc = 4700.00;

// Configure OneWire
OneWire oneWire(OneWireBus);
DallasTemperature sensors(&oneWire);
DeviceAddress insideThermometer;

// A4 -> LCD SDA
// A5 -> LCD SCL
// A8 is connected to the internal temperature sensor.

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

  // Attach interrupts
  // Ref: https://www.arduino.cc/reference/en/language/functions/external-interrupts/attachinterrupt/
  pinMode(intC, INPUT_PULLUP);
  pinMode(intD, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(intC), tachC, FALLING);
  attachInterrupt(digitalPinToInterrupt(intD), tachD, FALLING);

  // Temperature input
  // https://www.arduino.cc/reference/en/language/functions/analog-io/analogreference/
  // analogReference(INTERNAL);    // set AREF to 1.1 V
  pinMode(ntc,  INPUT);
  pinMode(lm35, INPUT);
  pinMode(ad22100, INPUT);

  // Copied the relevant part from here: http://playground.arduino.cc/Code/PwmFrequency
  // Also refs: 
  // * https://forum.arduino.cc/t/varying-the-pwm-frequency-for-timer-0-or-timer-2/16679/3
  // * https://docs.arduino.cc/tutorials/generic/secrets-of-arduino-pwm
  t0_corr = 6;                            // Correction for Timer0 change
  TCCR0B = TCCR0B & 0b11111000 | 0x01;    // The Arduino uses Timer 0 internally for the millis() and delay() functions
  TCCR1B = TCCR1B & 0b11111000 | 0x01;    // Change PWM Frequency for Timer1.  f=~31kHz
  // TCCR2B = TCCR2B & 0b11111000 | 0x01;    // Change PWM Frequency for Timer2.  f=~31kHz

  // delay(1000);
  // For Serial Communication
  inputString.reserve(128);   // reserve 128 bytes for the inputString

  // OneWire setup;
  if (!sensors.getAddress(insideThermometer, 0)) {
    Serial.println("Unable to find address for Device 0");
    dallasPresent = false;
  }
  sensors.setResolution(insideThermometer, 10);

  digitalWrite(LED_BUILTIN, LOW);
}

float ntcTemp() {
    float ADCntc = analogRead(ntc);
    // Calculate the measured resistance using the reference series resistance.
    float RT_ntc = Rs_ntc / ((1024.00/ADCntc) - 1.00);
    // Calculate the 1/T from the Steinhart equation. Note: T0 is in Kelvin.
    float recT = 1.00/(273.00+25.00) + log(RT_ntc/R25_ntc)/K_ntc;
    // Convert to Celsius.
    return 1.00/recT - 273.00; 
}

float ad22100Temp() {
  // AD22100KTZ: V_out = (V_ref/5V) * (1,375 V + 0,0225 V/°C * T)
  //             T = (V_out–1,375V)/0,0225 V/°C
  //             T = (adc*(V_ref/1024) – 1,375 V) /0,0225 V/°C

  // If AREF = 5000 mV
  // Resolution: 0.217°C;
  // Range: -50°C - 150°C [51 - 973 ADC] 

  float adc = analogRead(ad22100);
  float V_out = adc*(5000.00/1024.00);
  return (V_out - 1.375)/0.0225;
}

float lm35Temp() {
  // LM35DZ: 10mV/°C -> (adc*(aref/1024))/10
  
  // Settings for AREF = 1100 mV
  // Resolution: 0.11°C; Precision +/- 1 °C
  // Range: 0°C - 110°C [0 - 1023 ADC]
  
  // Settings for AREF = 5000 mV
  // Resolution: 0.49°C; Precision +/- 1 °C
  // Range: 0°C - 110°C [0 - 225 ADC]

  float tmV = analogRead(lm35);
  return tmV*(500.00/1024.00);
}

float ds18Temp() {
  if (dallasPresent) {
    // call sensors.requestTemperatures() to issue a global temperature 
    // request to all devices on the bus
    sensors.requestTemperatures(); // Send the command to get temperatures
    // printTemperature(insideThermometer); // Use a simple function to print out the data
    float tempC = sensors.getTempC(insideThermometer);
    return tempC;
  } else {
    return 80.00;   // In case of problem the output should be
  }
}

float getTemperature(char Source) {
  // NOTE: Implement here other sources.
  switch (Source) {
    case 'N':
      return ntcTemp();  break;
    case 'L':
      return lm35Temp(); break;
    case 'D':
      return ds18Temp(); break;
    case 'A':
      return ad22100Temp(); break;
    default:
      break;
  }
}

void tachC() {
  unsigned long time=micros();    // Store current microseconds.
  // Calculate the time difference to last call. This might not be safe.
  t_irpmC = time - tC;
  tC = time;                      // Store last call time.
}

void tachD() {
  unsigned long time=micros();    // Store current microseconds.
  // Calculate the time difference to last call. This might not be safe.
  t_irpmD = time - tD;
  tD = time;                      // Store last call time.
}

unsigned long toRPM(unsigned long irpm, byte correction) {
  // Calculate actual Rounds Per Minute
  unsigned long rpm = 60000000/irpm;
  return (rpm>1 && irpm > 0) ? rpm << correction : 0;
}

void loop() {
  if (stringComplete) {
    Serial.println(inputString);
    if (inputString.startsWith("t")) {
      switch(inputString.charAt(1)) {
        case '?':
          Serial.println(getTemperature(tempSource)); break;
        case 's':
          Serial.println(tempSource); break;  // Display source
        case 'N':
          Serial.println(ntcTemp());  break;  // Only for debugging
        case 'L':
          Serial.println(lm35Temp()); break;  // Only for debugging
        case 'D':
          Serial.println(ds18Temp()); break;  // Only for debugging
        case 'A':
          Serial.println(ad22100Temp()); break;  // Only for debugging
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
            Serial.print(preset[0][1]); Serial.print(";");
            Serial.print(preset[1][1]); Serial.print(";");
            Serial.print(preset[2][1]); Serial.print(";");
            Serial.print(preset[3][1]); Serial.print(";");
            Serial.print(preset[4][1]); Serial.println(";"); break;
          default:
            Serial.print("Syntax Error: "); Serial.println(inputString);
            break;
        }
      } else if (inputString.endsWith("p")) {
        // Set to pilot mode from manual to auto.
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

          Serial.println(toRPM(t_irpmC, t0_corr)); break;
        case 'D':
          Serial.println(toRPM(t_irpmD, t0_corr)); break;
        case '?':
          Serial.print(toRPM(t_irpmC, t0_corr)); Serial.print("; ");
          Serial.println(toRPM(t_irpmD, t0_corr)); break;
        default:
          Serial.print("Syntax Error: "); Serial.println(inputString);
          break;
      } 
    } else if (inputString.startsWith("fan?")) {
      // Print information about the fans
      Serial.println("fanmask"); // Change this to the bit array.
    } else if (inputString.startsWith("?")) {
      // Are you there?
      digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
      Serial.println("OK");
    } else if (inputString.startsWith("hrs?")) {
      if (t0_corr == 0) {
        // Uptime
        uptime::calculateUptime();
        Serial.print(uptime::getDays());      Serial.print(":");
        Serial.print(uptime::getHours());     Serial.print(":");
        Serial.print(uptime::getMinutes());   Serial.print(":");
        Serial.println(uptime::getSeconds());
      } else {
        Serial.println("00:00:00:00");          // Timer0 source is not good.
      }
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
          case 'H':
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
          case 'H':
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
          case 'H':
            Serial.println(preset[value][6]); break;
          default:
            Serial.print("Syntax Error: "); Serial.println(inputString);
            break;
        }
      } else {
        // SET LOW or HIGH pwm settings.
        // But constrain LOW between 0 and HIGH, and constrain HIGH between LOW and CRITICAL.
        switch(inputString.charAt(2)) {
          case 'L':
            preset[value][5] = constrain(byte(inputString.substring(4).toInt()), 0, preset[value][6]);
            Serial.print(preset[value][5]); 
            Serial.println(" OK"); break;
          case 'H':
            preset[value][6] = constrain(byte(inputString.substring(4).toInt()), preset[value][5], preset[value][7]);
            Serial.print(preset[value][6]);
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
    // To preserve the 0.25°C precision, the temperature is multiplied by 4.
    int currentTemp = int(getTemperature(tempSource)*4);
    for (int idx = 0; idx < sizeof(fanmask); idx++) {
      if (fanmask[idx] && (preset[idx][0] == 0)) {
        // Multiply the temperature ranges by 4 also.
        preset[idx][1] = map(currentTemp, preset[idx][5]*4, preset[idx][6]*4, preset[idx][2], preset[idx][3]);
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
    } else { inputString += inChar; }           // otherwise add it to the inputString                             
  }
}
