// ============================================================
//  Ursa Minor – Constellation Night-Light for a Child's Room
//  "Follow Your North Star"
// ============================================================
//  7 LEDs   → pins D2, D3, D4, D5, D6, D9, D10
//             PWM pins: D3, D5, D6, D9, D10
//  LDR      → A0          – ambient light
//  Board    → Arduino Nano Every (ATmega4809, megaAVR)
//  Power    → 1 x 9V Battery
// ============================================================
//  Features:
//    - Light-activated constellation (on at night, off during day)
//    - Visual calibration with cascade LED animation
//    - Breathing + twinkle animation for star LEDs
//    - Hidden Morse code message on a configurable star
//    - Power-saving sleep modes (standby / idle)
// ============================================================

// ============================================================
//  ALL THOSE STARS — Arduino Nano Every Sketch
//  Power-optimised build (no Serial, extended day sleep)
// ============================================================

#include <avr/sleep.h>
#include <avr/interrupt.h>

// ---------- PIN DEFINITIONS ----------

const int starPins[]    = {2, 3, 4, 5, 6, 9, 10};
const int NUM_STARS     = 7;

// PWM-capable star pins (for smooth breathing)
const int pwmStarPins[] = {3, 5, 6, 9, 10};
const int NUM_PWM       = 5;

// Sensor
const int PIN_PHOTO = A0;

// ---------- CALIBRATION ----------

// Delay between each LED lighting during the visual calibration cascade.
// Purely cosmetic — gives you time to see each star come on/off.
const unsigned long CALIBRATION_CASCADE_DELAY_MS = 2000;

// ---------- STAR BRIGHTNESS ----------

const int BREATHE_MIN = 20;   // PWM stars: dimmest point
const int BREATHE_MAX = 70;   // PWM stars: brightest point

// ---------- TWINKLE / FLICKER SETTINGS ----------

const unsigned long BREATHE_INTERVAL = 30;   // ms between PWM steps
const unsigned long FLICKER_CHECK    = 150;  // ms between flicker checks
const int           FLICKER_CHANCE   = 8;    // 1-in-N chance of flickering each check
const int           FLICKER_OFF_MS   = 50;   // how long a flicker lasts (ms)

// PWM star scintillation (realistic atmospheric twinkling)
const unsigned long SCINTILLATION_CHECK   = 80;  // ms between scintillation checks
const int           SCINTILLATION_CHANCE  = 3;   // 1-in-N chance per check per star
const int           SCINTILLATION_DIP_MIN = 10;  // min PWM dip from current brightness
const int           SCINTILLATION_DIP_MAX = 35;  // max PWM dip
const unsigned long SCINTILLATION_DUR_MIN = 30;  // min duration of a twinkle (ms)
const unsigned long SCINTILLATION_DUR_MAX = 120; // max duration

// ---------- HYSTERESIS ----------

// Extra margin above/below the threshold to prevent rapid on/off switching
// when the room light is near the boundary.
const int HYSTERESIS_MARGIN = 30;

// ---------- POWER SAVING ----------

// How often to check the light sensor while stars are ON (night mode).
// 5 seconds is responsive enough — the stars won't blink off the instant
// a light is switched on; there will be a brief delay, which is fine.
const unsigned long LIGHT_CHECK_NIGHT_MS = 5000;

// Day-mode sleep: the MCU does RTC-woken deep sleeps between light checks.
//
// RTC period: CYC32768 = 32768 ticks of the internal 32 kHz oscillator
//             = exactly 1 second per wake cycle.
//
// DAY_SLEEP_CYCLES: how many 1-second sleep cycles to do before checking
//                   whether the room has gone dark.
//
// Total day check interval = DAY_SLEEP_CYCLES × 1 s = 30 seconds.
// This means: if someone closes the curtains and turns off the room light
// during the day, the stars will come on within ~30 seconds.
// If you want faster response, lower DAY_SLEEP_CYCLES (minimum 1).
const int DAY_SLEEP_CYCLES = 30;

// Number of ADC samples to average for a light reading.
// Reduced from 6 to 3 to minimise ADC-on time during night checks.
const int LIGHT_SAMPLES = 3;

// ---------- HIDDEN MORSE MESSAGE ----------

// MORSE_STAR_INDEX : which entry in starPins[] blinks the message.
//   0 = pin D2 (good choice for Polaris in Ursa Minor).
// MORSE_MESSAGE    : text to blink. Supports A-Z, 0-9, spaces.
// MORSE_DOT_MS     : base unit length in ms.
//   Dash = 3×, letter gap = 3×, word gap = 7×.
// MORSE_PAUSE_MS   : pause after the full message before it repeats.

const int           MORSE_STAR_INDEX = 0;
const char          MORSE_MESSAGE[]  = "DAD LOVES YOU";
const unsigned long MORSE_DOT_MS    = 400;
const unsigned long MORSE_PAUSE_MS  = 3500;

// ---------- MORSE CODE LOOKUP TABLE ----------

const char* const morseAlpha[] = {
  ".-",    // A
  "-...",  // B
  "-.-.",  // C
  "-..",   // D
  ".",     // E
  "..-.",  // F
  "--.",   // G
  "....",  // H
  "..",    // I
  ".---",  // J
  "-.-",   // K
  ".-..",  // L
  "--",    // M
  "-.",    // N
  "---",   // O
  ".--.",  // P
  "--.-",  // Q
  ".-.",   // R
  "...",   // S
  "-",     // T
  "..-",   // U
  "...-",  // V
  ".--",   // W
  "-..-",  // X
  "-.--",  // Y
  "--..",  // Z
};

const char* const morseDigit[] = {
  "-----", // 0
  ".----", // 1
  "..---", // 2
  "...--", // 3
  "....-", // 4
  ".....", // 5
  "-....", // 6
  "--...", // 7
  "---..", // 8
  "----.", // 9
};

// ---------- STATE ----------

int  darknessThreshold = 512;
bool currentlyDark     = false;
bool constellationOn   = false;

// Breathing state for each PWM star
int   breatheVal[5]   = {30, 55, 40, 65, 25};
int   breatheDir[5]   = { 1, -1,  1, -1,  1};
float breatheSpeed[5] = {1.0, 1.3, 0.8, 1.1, 0.9};

// Flicker state
unsigned long lastFlickerCheck = 0;

// Scintillation state per PWM star
unsigned long scintStart[5]    = {0};
unsigned long scintDuration[5] = {0};
int           scintDip[5]      = {0};
bool          scintActive[5]   = {false};
unsigned long lastScintCheck   = 0;

// Timing
unsigned long lastBreathe    = 0;
unsigned long lastLightCheck = 0;

// RTC wake flag (set inside ISR)
volatile bool rtcWoke = false;

// Morse state machine
enum MorseState {
  M_ELEMENT_ON,
  M_ELEMENT_GAP,
  M_LETTER_GAP,
  M_WORD_GAP,
  M_MESSAGE_PAUSE
};

MorseState    morseState      = M_WORD_GAP;
int           morseMsgIdx     = 0;
int           morseSymIdx     = 0;
unsigned long morseTimer      = 0;
unsigned long morseDuration   = 0;
const char*   morseCurrentCode = NULL;
int           morsePin        = 0;
bool          morsePinIsPWM   = false;


// ============================================================
//  RTC PIT INTERRUPT
// ============================================================

ISR(RTC_PIT_vect) {
  RTC.PITINTFLAGS = RTC_PI_bm;
  rtcWoke = true;
}


// ============================================================
//  POWER MANAGEMENT
// ============================================================

// Configure the RTC Periodic Interrupt Timer.
// CYC32768 at 32 kHz internal oscillator = 1 second per interrupt.
// This fires once per second to wake the MCU from deep sleep.
void setupRTCWake() {
  while (RTC.STATUS  & RTC_CTRLABUSY_bm) { ; }
  RTC.CLKSEL = RTC_CLKSEL_INT32K_gc;
  while (RTC.PITSTATUS & RTC_CTRLBUSY_bm) { ; }
  // One-second wakeup period (was CYC8192 = 0.25 s)
  RTC.PITCTRLA  = RTC_PERIOD_CYC32768_gc | RTC_PITEN_bm;
  RTC.PITINTCTRL = RTC_PI_bm;
}

// Deep sleep (STANDBY).
// ADC is disabled before sleeping and re-enabled after to avoid
// the ADC drawing quiescent current while the MCU is idle.
void deepSleep() {
  ADC0.CTRLA &= ~ADC_ENABLE_bm;      // disable ADC to save power

  set_sleep_mode(SLEEP_MODE_STANDBY);
  sleep_enable();
  sei();
  sleep_cpu();                         // MCU halts here until RTC fires
  sleep_disable();

  ADC0.CTRLA |= ADC_ENABLE_bm;       // re-enable ADC
  delay(2);                            // brief settling time for ADC reference
}

// Idle sleep (used between animation frames at night).
// Less aggressive than STANDBY — timers and the ADC stay active so
// millis() keeps ticking and PWM outputs keep working.
void lightSleep(unsigned long ms) {
  set_sleep_mode(SLEEP_MODE_IDLE);
  sleep_enable();
  unsigned long start = millis();
  while (millis() - start < ms) {
    sleep_cpu();
  }
  sleep_disable();
}


// ============================================================
//  HELPERS
// ============================================================

// Read the light sensor LIGHT_SAMPLES times and return the average.
// Inter-sample delay kept short (3 ms) to minimise ADC-on time.
int readAverage(int pin) {
  long total = 0;
  for (int i = 0; i < LIGHT_SAMPLES; i++) {
    total += analogRead(pin);
    delay(3);
  }
  return (int)(total / LIGHT_SAMPLES);
}

bool isPWMPin(int pin) {
  return (pin == 3 || pin == 5 || pin == 6 || pin == 9 || pin == 10);
}


// ============================================================
//  CALIBRATION
// ============================================================

void ledOn(int idx) {
  if (isPWMPin(starPins[idx])) analogWrite(starPins[idx], 60);
  else                          digitalWrite(starPins[idx], HIGH);
}

void ledOff(int idx) {
  if (isPWMPin(starPins[idx])) analogWrite(starPins[idx], 0);
  else                          digitalWrite(starPins[idx], LOW);
}

void allLEDsOn()  { for (int i = 0; i < NUM_STARS; i++) ledOn(i);  }
void allLEDsOff() { for (int i = 0; i < NUM_STARS; i++) ledOff(i); }

void blinkAllLEDs(int times, int onMs, int offMs) {
  for (int i = 0; i < times; i++) {
    allLEDsOn();  delay(onMs);
    allLEDsOff(); delay(offMs);
  }
}

// Visual calibration — no serial output.
// Watch the LEDs and follow the implied two-phase sequence:
//   Phase 1 (cascade ON):  leave room lights ON.
//   Three blinks signal:   now turn room lights OFF (or cover the sensor).
//   Phase 2 (cascade OFF): room is dark.
//   Three blinks confirm:  calibration complete.
void calibrateLight() {

  // ---- Phase 1: sample bright (room lights ON) ----
  for (int i = 0; i < NUM_STARS; i++) {
    ledOn(i);
    delay(CALIBRATION_CASCADE_DELAY_MS);
  }

  delay(500);
  long brightTotal = 0;
  for (int s = 0; s < 20; s++) {
    brightTotal += analogRead(PIN_PHOTO);
    delay(50);
  }
  int brightLevel = (int)(brightTotal / 20);

  // ---- Signal: turn lights OFF now ----
  blinkAllLEDs(3, 300, 300);

  // ---- Phase 2: sample dark (room lights OFF) ----
  allLEDsOn();
  delay(500);

  for (int i = NUM_STARS - 1; i >= 0; i--) {
    ledOff(i);
    delay(CALIBRATION_CASCADE_DELAY_MS);
  }

  delay(500);
  long darkTotal = 0;
  for (int s = 0; s < 20; s++) {
    darkTotal += analogRead(PIN_PHOTO);
    delay(50);
  }
  int darkLevel = (int)(darkTotal / 20);

  // ---- Compute midpoint threshold ----
  darknessThreshold = (darkLevel + brightLevel) / 2;

  // ---- Confirmation blink ----
  blinkAllLEDs(3, 200, 200);
  allLEDsOff();

  currentlyDark   = false;
  constellationOn = false;
}


// ============================================================
//  CONSTELLATION CONTROL
// ============================================================

void starsOn() {
  for (int i = 0; i < NUM_STARS; i++) {
    if (!isPWMPin(starPins[i])) {
      digitalWrite(starPins[i], HIGH);
    }
  }
  constellationOn = true;
}

void starsOff() {
  for (int i = 0; i < NUM_STARS; i++) {
    if (isPWMPin(starPins[i])) analogWrite(starPins[i], 0);
    else                        digitalWrite(starPins[i], LOW);
  }
  constellationOn = false;
}

void updateBreathing() {
  if (millis() - lastBreathe < BREATHE_INTERVAL) return;
  lastBreathe = millis();

  for (int i = 0; i < NUM_PWM; i++) {
    if (pwmStarPins[i] == morsePin) continue;

    breatheVal[i] += (int)(breatheDir[i] * breatheSpeed[i]);

    if (breatheVal[i] >= BREATHE_MAX) {
      breatheVal[i] = BREATHE_MAX;
      breatheDir[i] = -1;
      breatheSpeed[i] = 0.6 + (random(0, 8) / 10.0);
    }
    if (breatheVal[i] <= BREATHE_MIN) {
      breatheVal[i] = BREATHE_MIN;
      breatheDir[i] = 1;
      breatheSpeed[i] = 0.6 + (random(0, 8) / 10.0);
    }

    int brightness = breatheVal[i];
    if (scintActive[i]) {
      brightness -= scintDip[i];
      if (brightness < 0) brightness = 0;
    }
    analogWrite(pwmStarPins[i], brightness);
  }
}

void updateFlicker() {
  if (millis() - lastFlickerCheck < FLICKER_CHECK) return;
  lastFlickerCheck = millis();

  for (int i = 0; i < NUM_STARS; i++) {
    if (isPWMPin(starPins[i])) continue;
    if (i == MORSE_STAR_INDEX)  continue;

    if (random(0, FLICKER_CHANCE) == 0) {
      digitalWrite(starPins[i], LOW);
      delay(random(20, FLICKER_OFF_MS));
      digitalWrite(starPins[i], HIGH);
    }
  }
}

void updateScintillation() {
  unsigned long now = millis();

  for (int i = 0; i < NUM_PWM; i++) {
    if (scintActive[i] && (now - scintStart[i] >= scintDuration[i])) {
      scintActive[i] = false;
    }
  }

  if (now - lastScintCheck < SCINTILLATION_CHECK) return;
  lastScintCheck = now;

  for (int i = 0; i < NUM_PWM; i++) {
    if (pwmStarPins[i] == morsePin) continue;
    if (scintActive[i])             continue;

    if (random(0, SCINTILLATION_CHANCE) == 0) {
      scintActive[i]   = true;
      scintStart[i]    = now;
      scintDip[i]      = random(SCINTILLATION_DIP_MIN, SCINTILLATION_DIP_MAX + 1);
      scintDuration[i] = random(SCINTILLATION_DUR_MIN, SCINTILLATION_DUR_MAX + 1);
    }
  }
}


// ============================================================
//  MORSE CODE ENGINE (non-blocking state machine)
// ============================================================

const char* getMorseCode(char c) {
  if (c >= 'A' && c <= 'Z') return morseAlpha[c - 'A'];
  if (c >= 'a' && c <= 'z') return morseAlpha[c - 'a'];
  if (c >= '0' && c <= '9') return morseDigit[c - '0'];
  return NULL;
}

void morseOn() {
  if (morsePinIsPWM) analogWrite(morsePin, BREATHE_MAX);
  else               digitalWrite(morsePin, HIGH);
}

void morseOff() {
  if (morsePinIsPWM) analogWrite(morsePin, 0);
  else               digitalWrite(morsePin, LOW);
}

void morseRest() {
  if (morsePinIsPWM) analogWrite(morsePin, BREATHE_MIN);
  else               digitalWrite(morsePin, HIGH);
}

void morseReset() {
  morseMsgIdx      = 0;
  morseSymIdx      = 0;
  morseState       = M_MESSAGE_PAUSE;
  morseTimer       = millis();
  morseDuration    = 1000;
  morseCurrentCode = NULL;
  morseRest();
}

void morseAdvanceChar() {
  morseMsgIdx++;

  if (morseMsgIdx >= (int)strlen(MORSE_MESSAGE)) {
    morseState    = M_MESSAGE_PAUSE;
    morseTimer    = millis();
    morseDuration = MORSE_PAUSE_MS;
    morseRest();
    return;
  }

  char c = MORSE_MESSAGE[morseMsgIdx];

  if (c == ' ') {
    morseState    = M_WORD_GAP;
    morseTimer    = millis();
    morseDuration = MORSE_DOT_MS * 7;
    morseRest();
    return;
  }

  morseCurrentCode = getMorseCode(c);
  if (morseCurrentCode == NULL) {
    morseAdvanceChar();
    return;
  }

  morseSymIdx   = 0;
  morseState    = M_LETTER_GAP;
  morseTimer    = millis();
  morseDuration = MORSE_DOT_MS * 3;
  morseRest();
}

void updateMorse() {
  unsigned long now = millis();
  if (now - morseTimer < morseDuration) return;

  switch (morseState) {

    case M_MESSAGE_PAUSE:
      morseMsgIdx = 0;
      morseSymIdx = 0;
      {
        char c = MORSE_MESSAGE[0];
        if (c == ' ') {
          morseState    = M_WORD_GAP;
          morseTimer    = now;
          morseDuration = MORSE_DOT_MS * 7;
          morseRest();
          return;
        }
        morseCurrentCode = getMorseCode(c);
        if (morseCurrentCode == NULL) {
          morseAdvanceChar();
          return;
        }
      }
      // fall through to start first element

    case M_ELEMENT_GAP:
    case M_LETTER_GAP:
    case M_WORD_GAP:
      if (morseCurrentCode == NULL || morseCurrentCode[morseSymIdx] == '\0') {
        morseAdvanceChar();
        return;
      }
      {
        char sym = morseCurrentCode[morseSymIdx];
        morseOn();
        morseState    = M_ELEMENT_ON;
        morseTimer    = now;
        morseDuration = (sym == '-') ? MORSE_DOT_MS * 3 : MORSE_DOT_MS;
      }
      break;

    case M_ELEMENT_ON:
      morseOff();
      morseSymIdx++;

      if (morseCurrentCode[morseSymIdx] == '\0') {
        morseState    = M_LETTER_GAP;
        morseTimer    = now;
        morseDuration = MORSE_DOT_MS * 3;
      } else {
        morseState    = M_ELEMENT_GAP;
        morseTimer    = now;
        morseDuration = MORSE_DOT_MS;
      }
      break;
  }
}


// ============================================================
//  LIGHT DETECTION WITH HYSTERESIS
// ============================================================

bool isRoomDark() {
  int light = readAverage(PIN_PHOTO);

  if (currentlyDark) {
    if (light > darknessThreshold + HYSTERESIS_MARGIN) {
      currentlyDark = false;
    }
  } else {
    if (light < darknessThreshold - HYSTERESIS_MARGIN) {
      currentlyDark = true;
    }
  }

  return currentlyDark;
}


// ============================================================
//  SETUP
// ============================================================

void setup() {
  // No Serial — completely disabled to save power.
  // The UART peripheral stays uninitialised and draws no current.

  randomSeed(analogRead(A2));

  morsePin      = starPins[MORSE_STAR_INDEX];
  morsePinIsPWM = isPWMPin(morsePin);

  for (int i = 0; i < NUM_STARS; i++) {
    pinMode(starPins[i], OUTPUT);
    digitalWrite(starPins[i], LOW);
  }

  delay(1500);
  calibrateLight();

  // RTC PIT now fires every 1 second (CYC32768).
  setupRTCWake();
}


// ============================================================
//  MAIN LOOP
// ============================================================

void loop() {

  bool dark = isRoomDark();

  if (dark) {
    // ---- NIGHT MODE: constellation ON ----

    if (!constellationOn) {
      starsOn();
      morseReset();
      lastLightCheck = millis();
    }

    updateBreathing();
    updateFlicker();
    updateScintillation();
    updateMorse();

    // Check if room has brightened — every LIGHT_CHECK_NIGHT_MS (5 s).
    // Only call isRoomDark() here; the result updates currentlyDark
    // so the top of the next loop() will handle turning stars off.
    if (millis() - lastLightCheck > LIGHT_CHECK_NIGHT_MS) {
      lastLightCheck = millis();
      isRoomDark();   // updates currentlyDark via hysteresis
    }

    // IDLE sleep between animation frames — keeps timers/PWM running.
    lightSleep(20);
  }
  else {
    // ---- DAY MODE: deep sleep ----
    // Stars are off. MCU sleeps for DAY_SLEEP_CYCLES × 1 second
    // (= 30 seconds) before checking the sensor again.
    // Each deepSleep() disables the ADC, sleeps until the 1-second
    // RTC tick, then re-enables the ADC.

    if (constellationOn) {
      starsOff();
    }

    for (int i = 0; i < DAY_SLEEP_CYCLES; i++) {
      rtcWoke = false;
      deepSleep();          // ~1 second of standby sleep per cycle
      // Early exit: check every cycle so we don't wait a full
      // 30 seconds if the room suddenly gets very dark.
      // (This adds one brief ADC read per second in day mode
      // but catches rapid darkness events faster if desired.
      // To remove the early exit and check only every 30 seconds,
      // delete the four lines below.)
      if (isRoomDark()) break;
    }
  }
}
