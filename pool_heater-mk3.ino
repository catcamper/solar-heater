/*
 * Solar Pool Heater
 * 
 * 
 * Coded by: Benjamin Wells <bn@catcamper.com>
 *           Chris Wells <chris@cevanwells.com>
 * License: BSD-2-Clause
 */
#include "Fsm.h" // Finate State Machine
#include "pitches.h"
#include <OneWire.h> // temperature sensors
#include <DallasTemperature.h>
#include <Arduino.h> // OLED display
#include <U8g2lib.h>
#include <Wire.h>

 /*
  * Pin definitions
  */
#define BUZZER_PIN 5
#define POT_PIN A0
#define SENSOR_PIN 2
#define LED_PIN 4
#define PUMP_PIN 3

/*
 * Setup temperature sensors
 */
#define TEMPERATURE_PRECISION 9
OneWire oneWire(SENSOR_PIN);
DallasTemperature sensors(&oneWire);
DeviceAddress poolSensor = { 0x28, 0x44, 0x2D, 0x3C, 0x5F, 0x20, 0x01, 0x42 };
DeviceAddress coilSensor = { 0x28, 0x18, 0xC5, 0x02, 0x5F, 0x20, 0x01, 0xA8 };

/*
 * Setup OLED display
 */
U8G2_SSD1306_128X64_NONAME_1_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);
#define LINE1 13
#define LINE2 29
#define LINE3 45
#define LINE4 64

// initialize global variables
int poolTemp, poolSet, coilTemp, coilSet;
char statusBuffer[8];
int ledState, pumpState;

/*
 * FSM events
 * 
 * POOL_NOT_HEATED: pool is not warm enough
 * POOL_HEATED: pool is warm enough
 * COIL_NOT_PRIMED: still hot water in heater coils
 * COIL_PRIMED: heater coils filled with cold water
 * COIL_NOT_READY: water in heater coils not hot yet
 * COIL_READY: water in heater coils is hot
 */
#define POOL_NOT_HEATED 0
#define POOL_HEATED 1
#define COIL_NOT_PRIMED 2
#define COIL_PRIMED 3
#define COIL_NOT_READY 4
#define COIL_READY 5

/*
 * FSM states
 * 
 * check_pool_state: check to see if the pool is heated
 * wait_state: waiting for pool to cool
 * heat_state: waiting for the water in the heater coils to reach set point
 * small_heat_state: waiting for the water in the heater coils to finish heating
 * check_coil_state: check to see if the water in the heater coils is hot
 * check_primed_state: check to see if the heater coils are filled with cold water
 * pump_state: pump hot water from heater coils to pool / fill heater coils with cold water
 */
State check_pool_state(&check_pool_temp, NULL, NULL);
State wait_state(&enter_wait_state, NULL, NULL);
State heat_state(&enter_heat_state, NULL, NULL);
State small_heat_state(&enter_heat_state, NULL, NULL);
State check_coil_state(&check_coil_temp, NULL, NULL);
State check_primed_state(&check_coil_primed, NULL, NULL);
State pump_state(&enter_pump_state, NULL, &exit_pump_state);
Fsm fsm(&check_pool_state);

/*
 * FSM transitions
 * 
 * check_pool_temp: check pool thermocouple; trigger POOL_HEATED/NOT_HEATED
 * enter_wait_state: update display "WAIT..."
 * enter_heat_state: update display "HEAT..."
 * check_coil_temp: check coil thermocouple; trigger COIL_READY/NOT_READY
 * check_coil_primed: check heat/pool thermocouple; trigger COIL_PRIMED/NOT_PRIMED
 * enter_pump_state: update display "PUMP..."; start pump
 * exit_pump_state: stop pump
 */
void check_pool_temp() {
  Serial.println("check_pool_temp"); // BEN: debugging
  /*
   * is poolTemp >= poolSet
   * yes:
   *   trigger POOL_HEATED event
   * no:
   *   trigger POOL_NOT_HEATED event
   *   
   */
  if (poolTemp >= poolSet) {
    Serial.println("trigger POOL_HEATED");
    fsm.trigger(POOL_HEATED);
  } else {
    Serial.println("trigger POOL_NOT_HEATED");
    fsm.trigger(POOL_NOT_HEATED);
  }
}

void enter_wait_state() {
  Serial.println("enter_wait_state");
  /*
   * update status line on display to "WAIT..."
   */
   strcpy(statusBuffer, "WAIT...");
}

void enter_heat_state() {
  Serial.println("enter_heat_state");
  /*
   * update status line on display to "HEAT..."
   */
   strcpy(statusBuffer, "HEAT...");
}

void check_coil_temp() {
  Serial.println("check_coil_temp");
  /*
   * is coilTemp >= coilSet
   * yes:
   *   trigger COIL_READY event
   * no:
   *   trigger COIL_NOT_READY event
   *   
   */
  if (coilTemp >= coilSet) {
    Serial.println("trigger COIL_READY");
    fsm.trigger(COIL_READY);   
  } else {
    Serial.println("trigger COIL_NOT_READY");
    fsm.trigger(COIL_NOT_READY);
  }
} 

void check_coil_primed() {
  Serial.println("check_coil_primed");
  /*
   * is  coilTemp <= poolTemp + 2
   * yes:
   *   trigger COIL_PRIMED event
   * no:
   *   trigger COIL_NOT_PRIMED event
   */
  if (coilTemp <= poolTemp + 2) {
    Serial.println("trigger COIL_PRIMED");
    fsm.trigger(COIL_PRIMED);
  } else {
    Serial.println("trigger COIL_NOT_PRIMED");
    fsm.trigger(COIL_NOT_PRIMED);
  }

}

void enter_pump_state() {
  Serial.println("enter_pump_state");
  /*
   * update status line on display to "PUMP..."
   * turn on pump
   */ 
   ledState = HIGH;
   pumpState = HIGH;
   strcpy(statusBuffer, "PUMP...");
}

void exit_pump_state() {
  Serial.println("exit_pump_state");
  /*
   * turn off pump
   */
   ledState = LOW;
   pumpState = LOW;
}

void makeGoodBeep() {
  tone(BUZZER_PIN, NOTE_C3, 500);
  delay(600);
  tone(BUZZER_PIN, NOTE_G4, 500);
}

void makeBadBeep() {
  tone(BUZZER_PIN, NOTE_B2, 500);
  delay(600);
  tone(BUZZER_PIN, NOTE_E2, 500);
  delay(600);
  tone(BUZZER_PIN, NOTE_G1, 1000);
}

// helper functions
int calcStart(char *str) {
  int strWidth = u8g2.getStrWidth(str);
  int scrWidth = u8g2.getDisplayWidth();
  
  return ((scrWidth - strWidth) / 2);
}

void setup() {
  Serial.begin(9600);
  /*
   * Add transitions to FSM
   * 
   * check_pool_state->wait_state: POOL_HEATED event
   * wait_state->check_pool_state: 10mins (600000 ms)
   * check_pool_state->coil_state: POOL_NOT_HEATED event
   * coil_state->check_coil_state: 20mins (1200000 ms)
   * check_coil_state->small_heat_state: COIL_NOT_READY event
   * small_heat_state->check_coil_state: 2mins (120000 ms)
   * check_coil_state->check_primed_state: COIL_READY event
   * check_primed_state->pump_state: COIL_NOT_PRIMED event
   * pump_state->check_primed_state: 2secs (2000 ms)
   * check_primed_state->wait_state: COIL_PRIMED event
   */
  fsm.add_transition(&check_pool_state, &wait_state, POOL_HEATED, &makeGoodBeep);
  //fsm.add_timed_transition(&wait_state, &check_pool_state, 600000, NULL);
  fsm.add_timed_transition(&wait_state, &check_pool_state, 6000, NULL); //BEN: missing
  fsm.add_transition(&check_pool_state, &heat_state, POOL_NOT_HEATED, &makeGoodBeep);
  //fsm.add_timed_transition(&coil_state, &check_coil_state, 1200000, &makeGoodBeep);
  fsm.add_timed_transition(&heat_state, &check_coil_state, 12000, &makeGoodBeep);
  fsm.add_transition(&check_coil_state, &small_heat_state, COIL_NOT_READY, NULL);
  //fsm.add_timed_transition(&small_heat_state, &check_coil_state, 120000, NULL);
  fsm.add_timed_transition(&small_heat_state, &check_coil_state, 6000, NULL);
  fsm.add_transition(&check_coil_state, &check_primed_state, COIL_READY, &makeGoodBeep);
  fsm.add_transition(&check_primed_state, &pump_state, COIL_NOT_PRIMED, &makeGoodBeep);
  //fsm.add_timed_transition(&pump_state, &check_primed_state, 2000, NULL);
  fsm.add_timed_transition(&pump_state, &check_primed_state, 2000, NULL);
  fsm.add_transition(&check_primed_state, &wait_state, COIL_PRIMED, &makeGoodBeep);

  // initialize thermocouples
  sensors.begin();
  sensors.setResolution(poolSensor, TEMPERATURE_PRECISION);
  sensors.setResolution(coilSensor, TEMPERATURE_PRECISION);
  
  // initialize setpoint/analog pin
  coilSet = 90;
  pinMode(POT_PIN, INPUT);

  //initialize OLED display
  u8g2.begin();
  u8g2.setFlipMode(1);
  u8g2.setFont(u8g2_font_profont17_mr);

  // initialize LED
  ledState = LOW;
  pumpState = LOW;
  pinMode(LED_PIN, OUTPUT);
  pinMode(PUMP_PIN, OUTPUT);
}

void loop() {
  sensors.requestTemperatures();
  // you add 0.5 to make it round instead of truncating
  poolTemp = sensors.getTempF(poolSensor) + 0.5;
  coilTemp = sensors.getTempF(coilSensor) + 0.5;
  //poolTemp = 70;
  //coilTemp = 90;

  // read POT_PIN and set poolSet
  poolSet = map(analogRead(POT_PIN), 1023, 0, 50, 90);

  // run page loop for display
  u8g2.firstPage();
  do {
    // draw main title and lines
    u8g2.drawStr(calcStart("SOLAR HEATER"), LINE1, "SOLAR HEATER");
    u8g2.drawLine(0, 14, 128, 14);
    u8g2.drawLine(0, 49, 128, 49);

    // poolTemp and poolSet
    char tempBuffer[8];
    u8g2.drawStr(calcStart("pool: xxx/xxx"), LINE2, "pool: ");
    u8g2.setCursor(60, LINE2);
    sprintf(tempBuffer, "%3d/%3d", poolTemp, poolSet);
    u8g2.print(tempBuffer);

    // coilTemp and coilSet
    u8g2.drawStr(calcStart("coil: xxx/xxx"), LINE3, "coil: ");
    u8g2.setCursor(60, LINE3);
    sprintf(tempBuffer, "%3d/%3d", coilTemp, coilSet);
    u8g2.print(tempBuffer);

    // system status
    u8g2.drawStr(calcStart(statusBuffer), LINE4, statusBuffer);
    
  } while (u8g2.nextPage());

  // enable LED according to ledState
  digitalWrite(LED_PIN, ledState);
  digitalWrite(PUMP_PIN, pumpState);
  
  // run FSM
  fsm.run_machine();
}
