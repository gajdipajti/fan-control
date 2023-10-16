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
   * fan[A-F]?        - GET information about a single fan channel
   * pwm?             - GET all PWM outputs
   * pwm[A-F]?        - GET PWM output
   * pwm[A-F][0-255]  - SET PWM output manually
   * t?               - GET all measured temperature
   * tA?              - GET measured temperature
   * ts?              - GET temperature sources (example Dallas 1-wire or LM35)

  Optional Functions: [when extra circuit is present]
   * lcd[?,0-4] - GET/SET LCD Backlight; Update LCD
   * heart[0-1] - turn hearbeat LED ON/OFF
  Calibration Functions:
   * Ch[L,H][A-F]?        - GET LOW HIGH temperatue for a channel
   * Ch[L,H][A-F][0-100]  - SET LOW HIGH temperatue for a channel
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

// Variables and constants which might be checked in the GUI
const byte  swVersion = 001;
const float hwVersion = 1.0;
const char  boardSubVersion = 'A';
float time;

// Define PWM pins
const byte pwmA = 5;
const byte pwmB = 6;
const byte pwmC = 9;
const byte pwmD = 10;
const byte pwmE = 11;
// const byte pwmF = 3;     // Used as interrupt

// Define interrupt pins
const byte intC = 2;
const byte intD = 3;

// Define temperature and lcd pins
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
bool streamMode = false;        // enable stream mode

// Wait mode
short waitPeriod = 1000;
unsigned long startWait = 0;

void setup() {  // The initial setup, that will run every time when we connect to the Arduino via serial.
                // To disable the auto reset, change the switch position on the board.

  pinMode(LED_BUILTIN, OUTPUT); digitalWrite(LED_BUILTIN, HIGH);
  
  Serial.begin(115200);
  
  // Voltage channel outputs, it's not needed for analogWrite, but just in case.
  // https://www.arduino.cc/reference/en/language/functions/analog-io/analogwrite/
  pinMode(pwmA, OUTPUT); analogWrite(pwmA, LOW);
  pinMode(pwmB, OUTPUT); analogWrite(pwmB, LOW);
  pinMode(pwmC, OUTPUT); analogWrite(pwmC, LOW);
  pinMode(pwmD, OUTPUT); analogWrite(pwmD, LOW);
  pinMode(pwmE, OUTPUT); analogWrite(pwmE, LOW);
  // pinMode(pwmF, OUTPUT); analogWrite(pwmF, LOW);  // Not used

  // Temperature input
  pinMode(lm35, INPUT);

  // Copied the relevant part from here: http://playground.arduino.cc/Code/PwmFrequency
  TCCR1B = TCCR1B & 0b11111000 | 0x01;    // Change PWM Frequency for Timer2.  f=~31kHz
  // TCCR2B = TCCR2B & 0b11111000 | 0x01;    // Change PWM Frequency for Timer1.  f=~31kHz

  delay(1000);
  
  inputString.reserve(128);   // reserve 128 bytes for the inputString
  digitalWrite(LED_BUILTIN, LOW);
}

float lm35Temp() {
  // LM35DT: 10mV/°C -> (adc*(aref/1024))/10
  // Resolution: 0,49°C; Precision +/- 1,5°C
  // Range: 0°C - 100°C [0 - 205 ADC]
  float tmV = analogRead(lm35);
  return tmV*(500.00/1024.00);
}
void loop() {
  if (stringComplete) {
    // Do your thing
  } // There is no else
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
