/*
 * Minima main sketch
 *
 * FIXME: License?
 *
 * Copyright 2013 - Ashar Farhan
 *
 * Tuning Strategy and Method Modified by: Eldon R. Brown (ERB) - WA0UWH, Apr 25, 2014
 * "A Long line, to allow for left right scrole, for program construct alignment -------------------------------------------------------------------------------------------------------------------------
 */

#define __ASSERT_USE_STDERR
#include <assert.h>

/*
 * Wire is only used from the Si570 module but we need to list it here so that
 * the Arduino environment knows we need it.
 */
#include <Wire.h>
#include <LiquidCrystal.h>

#include <avr/io.h>
#include "Si570.h"
#include "debug.h"


//#define RADIONO_VERSION "0.4"
#define RADIONO_VERSION "0.4.erb" // Modifications by: Eldon R. Brown - WA0UWH

/*
 The 16x2 LCD is connected as follows:
    LCD's PIN   Raduino's PIN  PURPOSE      ATMEGA328's PIN
    4           13             Reset LCD    19
    6           12             Enable       18
    11          10             D4           17
    12          11             D5           16
    13           9             D6           15
    14           8             D7           14
*/

#define SI570_I2C_ADDRESS 0x55
//#define IF_FREQ   (0)  // FOR DEBUG ONLY
#define IF_FREQ   (19997000l) //this is for usb, we should probably have the USB and LSB frequencies separately
#define CW_TIMEOUT (600l) // in milliseconds, this is the parameter that determines how long the tx will hold between cw key downs

// When RUN_TESTS is 1, the Radiono will automatically do some software testing when it starts.
// Please note, that those are not hardware tests! - Comment this line to save some space.
//#define RUN_TESTS 1

unsigned long frequency = 14200000;
unsigned long vfoA=14200000L, vfoB=14200000L, ritA, ritB;
unsigned long cwTimeout = 0;

Si570 *vfo;
LiquidCrystal lcd(13, 12, 11, 10, 9, 8);

char b[20], c[20], printBuff[32];

/* tuning pot stuff */
unsigned char refreshDisplay = 0;
unsigned int stepSize = 100;

// Added by ERB
#define MAX_FREQ (30e6)

#define DEAD_ZONE (40)

#define CURSOR_MODE (1)
#define DIGIT_MODE (2)
#define MODE_SWITCH_TIME (2000U)

#define NO_CURSOR (0)
#define UNDERLINE (1)
#define UNDERLINE_ WINK (2)
#define BLOCK_BLINK (3)

int tuningDir = 0;
int tuningPosition = 0;
int freqUnStable = 1;
int tuneMode = CURSOR_MODE;
int tuningPositionDelta = 0;
int cursorDigitPosition=0;
int tuningPositionPrevious = 0;
int cursorCol, cursorRow, cursorMode;
int winkOn;
unsigned long modeSwitchTime;
unsigned long freqPrevious;



unsigned char locked = 0; //the tuning can be locked: wait until it goes into dead-zone before unlocking it

/* the digital controls */

#define LSB (2)
#define TX_RX (3)
#define CW_KEY (4)

#define BAND_HI (5)

#define FBUTTON (A3)
#define ANALOG_TUNING (A2)
#define ANALOG_KEYER (A1)


#define VFO_A 0
#define VFO_B 1

char inTx = 0;
char keyDown = 0;
char isLSB = 0;
char isRIT = 0;
char vfoActive = VFO_A;
/* modes */
unsigned char isManual = 1;
unsigned ritOn = 0;

/* dds ddschip(DDS9850, 5, 6, 7, 125000000LL); */

// ###############################################################################
/* display routines */
void printLine1(char const *c){
  if (strcmp(c, printBuff)){
    lcd.setCursor(0, 0);
    lcd.print(c);
    strcpy(printBuff, c);
  }
}

void printLine2(char const *c){
  lcd.setCursor(0, 1);
  lcd.print(c);
}

// ###############################################################################
void displayFrequency(unsigned long f){
  int mhz, khz, hz;

  mhz = f / 1000000l;
  khz = (f % 1000000l)/1000;
  hz = f % 1000l;
  sprintf(b, "[%02d.%03d.%03d]", mhz, khz, hz);
  printLine1(b);
}

void updateDisplay(){
  char const *vfoStatus[] = { "ERR", "RDY", "BIG", "SML"};
  int i;
   
  if (refreshDisplay) {
     
      refreshDisplay = false;
      cursorOff();
      sprintf(b, "%08ld", frequency);
      sprintf(c, "%s:%.2s.%.6s %s%s", vfoActive == VFO_A ? "A" : "B" , b,  b+2, ritOn ? "RIT" : "   ",
            tuningDir > 0 ? ">" : tuningDir < 0 ? "<": tuneMode == DIGIT_MODE ? "-" : "*");  // Added by ERB
      printLine1(c);
      
      sprintf(c, "%s %s %s", isLSB ? "LSB" : "USB", inTx ? " TX" : " RX", freqUnStable ? "    " : vfoStatus[vfo->status]);
      printLine2(c);
      
      refreshDisplay = false;
      setCursorCRM(11 - (cursorDigitPosition + (cursorDigitPosition>6) ), 0, tuneMode);
  }
  updateCursor();
}


// -------------------------------------------------------------------------------
void setCursorCRM(int col, int row, int mode) {
  // mode 0 = underline
  // mode 1 = underline wink
  // move 2 = block blink
  // else   = no cursor
  cursorCol = col;
  cursorRow = row;
  cursorMode = mode;
}


// -------------------------------------------------------------------------------
void cursorOff() {
  lcd.noBlink();
  lcd.noCursor();
}


// -------------------------------------------------------------------------------
void updateCursor() {
  
  lcd.setCursor(cursorCol, cursorRow); // Postion Curesor
  
  // Set Cursor Display Mode, Wink On and OFF for DigitMode, Solid for CursorMode
  if (cursorMode == CURSOR_MODE) {
      if (millis() & 0x0200 ) { // Update only once in a while
        lcd.noBlink();
        lcd.cursor();
      }
  }
  else if (cursorMode == DIGIT_MODE) { // Winks Underline Cursor
      if (millis() & 0x0200 ) { // Update only once in a while
        if (winkOn == false) {
          lcd.cursor();
          winkOn = true;
        }
      } else {
        if (winkOn == true) {
          lcd.noCursor();
          winkOn = false;
        }
      }
  }
  else if (cursorMode == BLOCK_BLINK) {
      if (millis() & 0x0200 ) { // Update only once in a while
        lcd.blink();
        lcd.noCursor();
      }
  }
  else {
      if (millis() & 0x0200 ) { // Update only once in a while
        cursorOff();
      }
  }
 
}

// ###############################################################################
void setup() {
  // Initialize the Serial port so that we can use it for debugging
  Serial.begin(115200);
  debug("Radiono starting - Version: %s", RADIONO_VERSION);
  lcd.begin(16, 2);

#ifdef RUN_TESTS
  run_tests();
#endif

  printBuff[0] = 0;
  printLine1("Raduino ");
  lcd.print(RADIONO_VERSION);
  
  // Print just the File Name, Added by ERB
  //printLine2("F: ");
  //char *pch = strrchr(__FILE__,'/')+1;
  //lcd.print(pch);
  delay(3000);
  

  // The library automatically reads the factory calibration settings of your Si570
  // but it needs to know for what frequency it was calibrated for.
  // Looks like most HAM Si570 are calibrated for 56.320 Mhz.
  // If yours was calibrated for another frequency, you need to change that here
  vfo = new Si570(SI570_I2C_ADDRESS, 56320000);

  if (vfo->status == SI570_ERROR) {
    // The Si570 is unreachable. Show an error for 3 seconds and continue.
    printLine2("Si570 comm error");
    delay(3000);
  }
  printLine2("                ");  // Added: ERB
  

  // This will print some debugging info to the serial console.
  vfo->debugSi570();

  //set the initial frequency
  vfo->setFrequency(26150000L);

  //set up the pins
  pinMode(LSB, OUTPUT);
  pinMode(TX_RX, INPUT);
  pinMode(CW_KEY, OUTPUT);

  //set the side-tone off, put the transceiver to receive mode
  digitalWrite(CW_KEY, 0);
  digitalWrite(TX_RX, 1); //old way to enable the built-in pull-ups
  digitalWrite(FBUTTON, 1);
}

void setSideband(){
  if (frequency >= 10000000L)
  {
    isLSB = 0;
    digitalWrite(LSB, 0);
  }
  else{
    digitalWrite(LSB, 1);
    isLSB = 1;
  }
  
}

// ###############################################################################
void setBandswitch(){
  if (frequency >= 15000000L)
  {
    digitalWrite(BAND_HI, 1);
  }
  else {
    digitalWrite(BAND_HI, 0);
  }
}

// ###############################################################################
void readTuningPot(){
    tuningPosition = analogRead(2);
}



// ###############################################################################
// An Alternate Tuning Strategy or Method
// This method somewhat emulates a normal Radio Tuning Dial
// Digit can be changed when Underline Cursor is blinking, else Cursor can be Moved
// Author: Eldon R. Brown - WA0UWH, Apr 25, 2014
void checkTuning() {
  
  if (!freqUnStable) {
    //we are Stable, so, Set to Non-lock
    locked = 0;
  }
 
  // Compute tuningDaltaPosition from tuningPosition
  tuningPositionDelta = tuningPosition - tuningPositionPrevious;
  
  if (tuneMode == CURSOR_MODE) {  // Check for Mode ########
    changeCursorPositionMode();
  } else { // Check Digit Change Mode ######################
    changeDigitMode();
  }
}  


// AUX Tuning Function =================================

// Check Cursor Change Position Mode ------------------- 
void changeCursorPositionMode() {
   
  if (cursorDigitPosition > 0) { 
   // Check to see if it is Idle and time to switch Modes
    if (millis() > modeSwitchTime) {
      tuneMode = DIGIT_MODE; // Change Modes
      refreshDisplay = true;
      modeSwitchTime = millis() + 2 * MODE_SWITCH_TIME; // Init Timer for other Mode
      return;
    }
  }
  
  // Check and Position Cursor for which Digit should be changed
  if (abs(tuningPositionDelta) > DEAD_ZONE) { 
    cursorDigitPosition += (tuningPositionDelta < 0 ? +1 : -1);
    cursorDigitPosition = min(cursorDigitPosition, 7);
    cursorDigitPosition = max(cursorDigitPosition, 0); // Zero indicates cursor is Parked off to the Right of LSD
    freqPrevious = frequency;
    tuningPositionPrevious = tuningPosition; // Set up for the next Iteration
    freqUnStable = false;  // Set Freq is NOT UnStable, it is Stable
    tuningDir = 0;
    refreshDisplay = true;
    modeSwitchTime = millis() + MODE_SWITCH_TIME; // Stay in current mode by reseting Timer
  }
}


// Check for Digit Change Mode --------------------------
void changeDigitMode() {
  long deltaFreq;
  unsigned long newFreq;
  
  tuningDir = 0;  // Set Default Tuning Directon to neather Right nor Left
  
  // Check to see if it is Idle and time to switch Modes
  if (millis() > modeSwitchTime) {
    tuneMode = CURSOR_MODE; // Change Modes
    refreshDisplay = true;
    modeSwitchTime = millis() + MODE_SWITCH_TIME; // Init Timer for other Mode
    return;
  }
  
  // Count Down to Freq Stable, i.e. Freq has not changed recently
  freqUnStable = max(--freqUnStable, 0);
  
  // Check to see if Automatic Digit Change Action is Required, if SO, force the change
  if (tuningPosition < DEAD_ZONE * 2) { // We must be at the Low end of the Tuning POT
    tuningPositionDelta = -DEAD_ZONE;
    delay(100);
    if (tuningPosition > DEAD_ZONE / 8 ) delay(100);
  }
  if (tuningPosition > 1024 - DEAD_ZONE * 2) { // We must be at the High end of the Tuning POT
    tuningPositionDelta = DEAD_ZONE; 
    delay(100);
    if (tuningPosition < 1024 - DEAD_ZONE / 8) delay(100);
  }

  // Check to see if Digit Change Action is Required, Otherwise Do Nothing via RETURN 
  if (abs(tuningPositionDelta) < DEAD_ZONE) return;

  refreshDisplay = true;
  modeSwitchTime = millis() + 2 * MODE_SWITCH_TIME; // Stay in current mode by reseting Timer
  
  tuningDir = tuningPositionDelta < 0 ? -1 : tuningPositionDelta > 0 ? +1 : 0;  
  if (!tuningDir) return;  // If Neather Direction, Abort
  
  if (cursorDigitPosition < 1) return; // Move Along, nothing to do here
  
  
  // Compute deltaFreq based on current Cursor Position Digit
  deltaFreq = tuningDir;
  for (int i = cursorDigitPosition; i > 1; i-- ) deltaFreq *= 10;
  
  newFreq = freqPrevious + deltaFreq;  
  if (newFreq != frequency) {
      // Update frequency if within range of limits, Avoiding Nagative underRoll of UnSigned Long, and over run MAX_FREQ  
      if (!(newFreq > MAX_FREQ * 2) && !(newFreq > MAX_FREQ)) {
        frequency = newFreq;
        freqUnStable = 25; // Set to UnStable (non-zero) Because Freq has been changed
      }
      tuningPositionPrevious = tuningPosition; // Set up for the next Iteration
  }
  freqPrevious = frequency;
}


// ###############################################################################
void checkTX(){
  
  if (freqUnStable) return;

  //we don't check for ptt when transmitting cw
  if (cwTimeout > 0)
    return;
    
  if (digitalRead(TX_RX) == 0 && inTx == 0){
    refreshDisplay++;
    inTx = 1;
  }

  if (digitalRead(TX_RX) == 1 && inTx == 1){
    refreshDisplay++;
    inTx = 0;
  }
}


// ###############################################################################
/*CW is generated by keying the bias of a side-tone oscillator.
nonzero cwTimeout denotes that we are in cw transmit mode.
*/

void checkCW(){
  
  if (freqUnStable) return;

  if (keyDown == 0 && analogRead(ANALOG_KEYER) < 50){
    //switch to transmit mode if we are not already in it
    if (inTx == 0){
      //put the  TX_RX line to transmit
      pinMode(TX_RX, OUTPUT);
      digitalWrite(TX_RX, 0);
      //give the relays a few ms to settle the T/R relays
      delay(50);
    }
    inTx = 1;
    keyDown = 1;
    digitalWrite(CW_KEY, 1); //start the side-tone
    refreshDisplay++;
  }

  //reset the timer as long as the key is down
  if (keyDown == 1){
     cwTimeout = CW_TIMEOUT + millis();
  }

  //if we have a keyup
  if (keyDown == 1 && analogRead(ANALOG_KEYER) > 150){
    keyDown = 0;
    digitalWrite(CW_KEY, 0);
    cwTimeout = millis() + CW_TIMEOUT;
  }

  //if we have keyuup for a longish time while in cw tx mode
  if (inTx == 1 && cwTimeout < millis()){
    //move the radio back to receive
    digitalWrite(TX_RX, 1);
    //set the TX_RX pin back to input mode
    pinMode(TX_RX, INPUT);
    digitalWrite(TX_RX, 1); //pull-up!
    inTx = 0;
    cwTimeout = 0;
    refreshDisplay++;
  }
}

int btnDown(){
  if (analogRead(FBUTTON) < 300)
    return 1;
  else
    return 0;
}

void checkButton(){
  int i, t1, t2;
  //only if the button is pressed
  if (!btnDown())
    return;

  //if the btn is down while tuning pot is not centered, then lock the tuning
  //and return
  if (freqUnStable) {
    if (locked)
      locked = 0;
    else
      locked = 1;
    return;
  }

  t1 = t2 = i = 0;

  while (t1 < 30 && btnDown() == 1){
    delay(50);
    t1++;
  }

  while (t2 < 10 && btnDown() == 0){
    delay(50);
    t2++;
  }

  //if the press is momentary and there is no secondary press
  if (t1 < 10 && t2 > 6){
    ritOn = !ritOn;
    refreshDisplay++;
  }
  //there has been a double press
  else if (t1 < 10 && t2 <= 6) {
    if (vfoActive == VFO_B){
      vfoActive = VFO_A;
      vfoB = frequency;
      frequency = vfoA;
    }
    else{
      vfoActive = VFO_B;
      vfoA = frequency;
      frequency = vfoB;
    }
    ritOn = 0;
    refreshDisplay++;
    updateDisplay();
    cursorOff();
    printLine2("VFO swap! ");
    refreshDisplay++;
  }
  else if (t1 > 10){
    vfoA = vfoB = frequency;
    ritOn = 0;
    refreshDisplay++;
    updateDisplay();
    cursorOff();
    printLine2("VFOs reset!");
    refreshDisplay++;
  }

  while (btnDown() == 1){
     delay(50);
  }
}

// ###############################################################################
// ###############################################################################
void loop(){
  readTuningPot();
  checkTuning();

  //the order of testing first for cw and then for ptt is important.
  checkCW();
  checkTX();
  checkButton();

  vfo->setFrequency(frequency + IF_FREQ);

  setSideband();
  setBandswitch();

  updateDisplay();
  
}

#ifdef RUN_TESTS

bool run_tests() {
  /* Those tests check that the Si570 libary is able to understand the
   * register values provided and do the required math with them.
   */
  // Testing for thomas - si570
  {
    uint8_t registers[] = { 0xe1, 0xc2, 0xb5, 0x7c, 0x77, 0x70 };
    vfo = new Si570(registers, 56320000);
    assert(vfo->getFreqXtal() == 114347712);
    delete(vfo);
  }

  // Testing Jerry - si570
  {
    uint8_t registers[] = { 0xe1, 0xc2, 0xb6, 0x36, 0xbf, 0x42 };
    vfo = new Si570(registers, 56320000);
    assert(vfo->getFreqXtal() == 114227856);
    delete(vfo);
  }

  Serial.println("Tests successful!");
  return true;
}

// ###############################################################################
// ###############################################################################
// handle diagnostic informations given by assertion and abort program execution:
void __assert(const char *__func, const char *__file, int __lineno, const char *__sexp) {
  debug("ASSERT FAILED - %s (%s:%i): %s", __func, __file, __lineno, __sexp);
  Serial.flush();
  // Show something on the screen
  lcd.setCursor(0, 0);
  lcd.print("OOPS ");
  lcd.print(__file);
  lcd.setCursor(0, 1);
  lcd.print("Line: ");
  lcd.print(__lineno);
  // abort program execution.
  abort();
}

#endif

