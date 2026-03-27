// ============================================================
//  Ursa Minor – Constellation Night-Light for a Child's Room
//  "Follow Your North Star"
// ============================================================
//  7 LEDs   → pins D2, D3, D4, D5, D6, D9, D10
//             PWM pins: D3, D5, D6, D9, D10
//  LDR      → A0          – ambient light
//  Board    → Arduino Nano Every (ATmega4809, megaAVR)
//  Power    → 4×AA batteries (6 V)
// ============================================================
//  Features:
//    - Light-activated constellation (on at night, off during day)
//    - Visual calibration with cascade LED animation
//    - Breathing + twinkle animation for star LEDs
//    - Hidden Morse code message on a configurable star
//    - Power-saving sleep modes (standby / idle)
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

const unsigned long CALIBRATION_CASCADE_DELAY_MS = 2000;  // ms between each LED on/off during calibration

// ---------- STAR BRIGHTNESS ----------

const int BREATHE_MIN    = 20;   // PWM stars: dimmest point
const int BREATHE_MAX    = 90;   // PWM stars: brightest point (lowered to save power)

// ---------- TWINKLE / FLICKER SETTINGS ----------

const unsigned long BREATHE_INTERVAL = 30;    // ms between PWM steps
const unsigned long FLICKER_CHECK    = 150;   // ms between flicker checks
const int           FLICKER_CHANCE   = 8;     // 1-in-N chance of flickering each check
const int           FLICKER_OFF_MS   = 50;    // how long a flicker lasts (ms)

// PWM star scintillation (realistic star twinkling)
const unsigned long SCINTILLATION_CHECK   = 80;   // ms between scintillation checks
const int           SCINTILLATION_CHANCE  = 3;    // 1-in-N chance per check per star
const int           SCINTILLATION_DIP_MIN = 10;   // min PWM dip from current brightness
const int           SCINTILLATION_DIP_MAX = 35;   // max PWM dip
const unsigned long SCINTILLATION_DUR_MIN = 30;   // min duration of a twinkle (ms)
const unsigned long SCINTILLATION_DUR_MAX = 120;  // max duration

// ---------- HYSTERESIS ----------

const int HYSTERESIS_MARGIN = 30;   // analog units above/below threshold

// ---------- POWER SAVING ----------

const unsigned long LIGHT_CHECK_NIGHT   = 3000;   // check light every 3 s in night mode
const int           DAY_SLEEP_CYCLES    = 2;       // ~16 s between light checks in day mode

// ---------- HIDDEN MORSE MESSAGE ----------
//  Configure which star blinks the message and what the message is.
//  MORSE_STAR_INDEX: index into starPins[] (0 = first star, 6 = last).
//    For Ursa Minor, index 0 (pin D2) is a good choice for Polaris.
//    Change to any value 0-6 to pick a different star.
//  MORSE_MESSAGE: the text to blink. Supports A-Z, 0-9, and spaces.
//  MORSE_DOT_MS: base unit duration in ms (dot length).
//    Dash = 3x, letter gap = 3x, word gap = 7x.
//  MORSE_PAUSE_MS: pause before repeating the full message.

const int           MORSE_STAR_INDEX = 0;             // star index (0-6)
const char          MORSE_MESSAGE[]  = "DAD LOVES YOU";  // hidden message
const unsigned long MORSE_DOT_MS     = 300;           // dot duration (ms)
const unsigned long MORSE_PAUSE_MS   = 4000;          // pause after full message before repeat

// ---------- MORSE CODE LOOKUP TABLE ----------
//  Each letter is encoded as a string of '.' and '-'.
//  Index 0 = 'A', index 25 = 'Z'.

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

int  darknessThreshold  = 512;       // set during calibration
bool currentlyDark      = false;     // tracks room state with hysteresis
bool constellationOn    = false;     // are stars currently lit?

// Breathing state for each PWM star (pins 3, 5, 6, 9, 10)
int   breatheVal[5]  = {30, 55, 40, 65, 25};       // start at different phases
int   breatheDir[5]  = { 1, -1,  1, -1,  1};       // direction of each
float breatheSpeed[5] = {1.0, 1.3, 0.8, 1.1, 0.9}; // slightly different speeds

// Flicker state for non-PWM stars
unsigned long lastFlickerCheck = 0;

// Scintillation state per PWM star (realistic twinkling)
unsigned long scintStart[5]    = {0};    // when current twinkle started
unsigned long scintDuration[5] = {0};    // how long current twinkle lasts
int           scintDip[5]      = {0};    // how much to dip brightness
bool          scintActive[5]   = {false};
unsigned long lastScintCheck   = 0;

// Timing
unsigned long lastBreathe      = 0;
unsigned long lastLightCheck   = 0;

// RTC wake flag
volatile bool rtcWoke = false;

// ----- Morse state machine -----
//  States: ELEMENT_ON, ELEMENT_GAP, LETTER_GAP, WORD_GAP, MESSAGE_PAUSE
enum MorseState { M_ELEMENT_ON, M_ELEMENT_GAP, M_LETTER_GAP, M_WORD_GAP, M_MESSAGE_PAUSE };

MorseState   morseState       = M_WORD_GAP;    // start with initial pause
int          morseMsgIdx      = 0;              // index in MORSE_MESSAGE
int          morseSymIdx      = 0;              // index within current letter's morse string
unsigned long morseTimer      = 0;              // when current state started
unsigned long morseDuration   = 0;              // how long current state lasts
const char*  morseCurrentCode = NULL;            // pointer to current letter's code (".-" etc.)
int          morsePin         = 0;               // resolved pin for the Morse star
bool         morsePinIsPWM   = false;


// ============================================================
//  RTC INTERRUPT  (wakes the MCU from sleep)
// ============================================================

ISR(RTC_PIT_vect) {
  RTC.PITINTFLAGS = RTC_PI_bm;   // clear interrupt flag
  rtcWoke = true;
}


// ============================================================
//  POWER MANAGEMENT
// ============================================================

void setupRTCWake() {
  while (RTC.STATUS & RTC_CTRLABUSY_bm) { ; }
  RTC.CLKSEL = RTC_CLKSEL_INT32K_gc;
  while (RTC.PITSTATUS & RTC_CTRLBUSY_bm) { ; }
  RTC.PITCTRLA = RTC_PERIOD_CYC8192_gc | RTC_PITEN_bm;
  RTC.PITINTCTRL = RTC_PI_bm;
}

void deepSleep() {
  set_sleep_mode(SLEEP_MODE_STANDBY);
  sleep_enable();
  sei();
  sleep_cpu();
  sleep_disable();
  delay(2);
}

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
//  HELPER FUNCTIONS
// ============================================================

int readAverage(int pin, int samples) {
  long total = 0;
  for (int i = 0; i < samples; i++) {
    total += analogRead(pin);
    delay(3);
  }
  return (int)(total / samples);
}

bool isPWMPin(int pin) {
  return (pin == 3 || pin == 5 || pin == 6 || pin == 9 || pin == 10);
}


// ============================================================
//  CALIBRATION  (visual cascade + light sensor reading)
// ============================================================

void ledOn(int idx) {
  if (isPWMPin(starPins[idx])) {
    analogWrite(starPins[idx], 60);
  } else {
    digitalWrite(starPins[idx], HIGH);
  }
}

void ledOff(int idx) {
  if (isPWMPin(starPins[idx])) {
    analogWrite(starPins[idx], 0);
  } else {
    digitalWrite(starPins[idx], LOW);
  }
}

void allLEDsOn() {
  for (int i = 0; i < NUM_STARS; i++) ledOn(i);
}

void allLEDsOff() {
  for (int i = 0; i < NUM_STARS; i++) ledOff(i);
}

void blinkAllLEDs(int times, int onMs, int offMs) {
  for (int i = 0; i < times; i++) {
    allLEDsOn();
    delay(onMs);
    allLEDsOff();
    delay(offMs);
  }
}

void calibrateLight() {

  Serial.println(F(""));
  Serial.println(F("========================================"));
  Serial.println(F("   CALIBRATION - Light Sensor Setup"));
  Serial.println(F("========================================"));

  // ---- Phase 1: Bright / ambient light reading ----
  Serial.println(F("Phase 1: Keep room lights ON."));
  Serial.println(F("         LEDs cascading on..."));

  for (int i = 0; i < NUM_STARS; i++) {
    ledOn(i);
    Serial.print(F("   LED "));
    Serial.print(i + 1);
    Serial.print(F(" ON  (pin D"));
    Serial.print(starPins[i]);
    Serial.print(F(")  |  Light reading: "));
    Serial.println(analogRead(PIN_PHOTO));
    delay(CALIBRATION_CASCADE_DELAY_MS);
  }

  // All 7 LEDs are now on — sample the bright ambient level
  delay(500);
  Serial.println(F("   Sampling bright level..."));
  long brightTotal = 0;
  const int brightSamples = 20;
  for (int s = 0; s < brightSamples; s++) {
    int val = analogRead(PIN_PHOTO);
    brightTotal += val;
    Serial.print(F("     sample "));
    Serial.print(s + 1);
    Serial.print(F(": "));
    Serial.println(val);
    delay(50);
  }
  int brightLevel = (int)(brightTotal / brightSamples);

  Serial.print(F("   Bright level: "));
  Serial.println(brightLevel);

  // ---- Transition blink ----
  Serial.println(F("=> Blink: Now turn lights OFF / cover sensor!"));
  blinkAllLEDs(3, 300, 300);

  // ---- Phase 2: Dark reading ----
  Serial.println(F("Phase 2: Lights OFF / cover sensor."));
  Serial.println(F("         LEDs cascading off..."));

  allLEDsOn();
  delay(500);

  for (int i = NUM_STARS - 1; i >= 0; i--) {
    ledOff(i);
    Serial.print(F("   LED "));
    Serial.print(i + 1);
    Serial.print(F(" OFF (pin D"));
    Serial.print(starPins[i]);
    Serial.print(F(")  |  Light reading: "));
    Serial.println(analogRead(PIN_PHOTO));
    delay(CALIBRATION_CASCADE_DELAY_MS);
  }

  // All 7 LEDs are now off — sample the dark level
  delay(500);
  Serial.println(F("   Sampling dark level..."));
  long darkTotal = 0;
  const int darkSamples = 20;
  for (int s = 0; s < darkSamples; s++) {
    int val = analogRead(PIN_PHOTO);
    darkTotal += val;
    Serial.print(F("     sample "));
    Serial.print(s + 1);
    Serial.print(F(": "));
    Serial.println(val);
    delay(50);
  }
  int darkLevel = (int)(darkTotal / darkSamples);

  Serial.print(F("   Dark level: "));
  Serial.println(darkLevel);

  // ---- Compute threshold ----
  darknessThreshold = (darkLevel + brightLevel) / 2;

  Serial.println(F("----------------------------------------"));
  Serial.print(F("   Bright level (avg): "));
  Serial.println(brightLevel);
  Serial.print(F("   Dark  level (avg): "));
  Serial.println(darkLevel);
  Serial.print(F("   Threshold set to : "));
  Serial.println(darknessThreshold);
  Serial.print(F("   Stars ON  when light < "));
  Serial.println(darknessThreshold - HYSTERESIS_MARGIN);
  Serial.print(F("   Stars OFF when light > "));
  Serial.println(darknessThreshold + HYSTERESIS_MARGIN);
  Serial.println(F("========================================"));
  Serial.println(F("   CALIBRATION COMPLETE"));
  Serial.println(F("========================================"));
  Serial.println(F(""));

  // ---- Confirmation blink ----
  blinkAllLEDs(3, 200, 200);
  allLEDsOff();

  currentlyDark = false;
  constellationOn = false;
}


// ============================================================
//  CONSTELLATION CONTROL
// ============================================================

void starsOn() {
  for (int i = 0; i < NUM_STARS; i++) {
    // Morse star is managed by the Morse engine — turn it on initially
    if (!isPWMPin(starPins[i])) {
      digitalWrite(starPins[i], HIGH);
    }
  }
  constellationOn = true;
}

void starsOff() {
  for (int i = 0; i < NUM_STARS; i++) {
    if (isPWMPin(starPins[i])) {
      analogWrite(starPins[i], 0);
    } else {
      digitalWrite(starPins[i], LOW);
    }
  }
  constellationOn = false;
}

// Gentle breathing for PWM stars (skips the Morse star)
void updateBreathing() {
  if (millis() - lastBreathe < BREATHE_INTERVAL) return;
  lastBreathe = millis();

  for (int i = 0; i < NUM_PWM; i++) {
    // Skip if this PWM pin is the Morse star
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
    // Apply scintillation dip if a twinkle is active on this star
    if (scintActive[i]) {
      brightness -= scintDip[i];
      if (brightness < 0) brightness = 0;
    }
    analogWrite(pwmStarPins[i], brightness);
  }
}

// Random flicker for non-PWM stars (skips the Morse star)
void updateFlicker() {
  if (millis() - lastFlickerCheck < FLICKER_CHECK) return;
  lastFlickerCheck = millis();

  for (int i = 0; i < NUM_STARS; i++) {
    if (isPWMPin(starPins[i])) continue;
    if (i == MORSE_STAR_INDEX) continue;   // Morse star handled separately

    if (random(0, FLICKER_CHANCE) == 0) {
      digitalWrite(starPins[i], LOW);
      delay(random(20, FLICKER_OFF_MS));
      digitalWrite(starPins[i], HIGH);
    }
  }
}

// Realistic star scintillation for PWM stars (skips the Morse star)
// Simulates atmospheric twinkling by applying random brief brightness dips
void updateScintillation() {
  unsigned long now = millis();

  // End any expired twinkles
  for (int i = 0; i < NUM_PWM; i++) {
    if (scintActive[i] && (now - scintStart[i] >= scintDuration[i])) {
      scintActive[i] = false;
    }
  }

  // Periodically start new twinkles
  if (now - lastScintCheck < SCINTILLATION_CHECK) return;
  lastScintCheck = now;

  for (int i = 0; i < NUM_PWM; i++) {
    if (pwmStarPins[i] == morsePin) continue;  // skip Morse star
    if (scintActive[i]) continue;               // already twinkling

    if (random(0, SCINTILLATION_CHANCE) == 0) {
      scintActive[i]   = true;
      scintStart[i]    = now;
      scintDip[i]      = random(SCINTILLATION_DIP_MIN, SCINTILLATION_DIP_MAX + 1);
      scintDuration[i] = random(SCINTILLATION_DUR_MIN, SCINTILLATION_DUR_MAX + 1);
    }
  }
}


// ============================================================
//  MORSE CODE ENGINE  (non-blocking state machine)
// ============================================================
//  Runs each loop() iteration. Controls the Morse star LED
//  independently of the breathing/flicker animations.
//
//  Timing (ITU standard):
//    dot      = 1 unit    (MORSE_DOT_MS)
//    dash     = 3 units
//    gap between elements within a letter = 1 unit
//    gap between letters = 3 units
//    gap between words   = 7 units
// ============================================================

// Get the Morse code string for a character, or NULL if unsupported
const char* getMorseCode(char c) {
  if (c >= 'A' && c <= 'Z') return morseAlpha[c - 'A'];
  if (c >= 'a' && c <= 'z') return morseAlpha[c - 'a'];
  if (c >= '0' && c <= '9') return morseDigit[c - '0'];
  return NULL;  // space or unsupported — handled as word gap
}

// Turn the Morse star LED on
void morseOn() {
  if (morsePinIsPWM) {
    analogWrite(morsePin, BREATHE_MAX);  // bright for visibility
  } else {
    digitalWrite(morsePin, HIGH);
  }
}

// Turn the Morse star LED off
void morseOff() {
  if (morsePinIsPWM) {
    analogWrite(morsePin, 0);
  } else {
    digitalWrite(morsePin, LOW);
  }
}

// Set the Morse LED to a dim "resting" glow (so it's part of the constellation)
void morseRest() {
  if (morsePinIsPWM) {
    analogWrite(morsePin, BREATHE_MIN);
  } else {
    digitalWrite(morsePin, HIGH);   // non-PWM: just on
  }
}

// Initialize / reset the Morse state machine
void morseReset() {
  morseMsgIdx    = 0;
  morseSymIdx    = 0;
  morseState     = M_MESSAGE_PAUSE;
  morseTimer     = millis();
  morseDuration  = 1000;  // brief pause before first play
  morseCurrentCode = NULL;

  morseRest();  // start with dim glow

  Serial.print(F("MORSE | Message: \""));
  Serial.print(MORSE_MESSAGE);
  Serial.print(F("\" on star "));
  Serial.print(MORSE_STAR_INDEX + 1);
  Serial.print(F(" (pin D"));
  Serial.print(morsePin);
  Serial.println(F(")"));
}

// Advance to the next character in the message
void morseAdvanceChar() {
  morseMsgIdx++;

  // End of message?
  if (morseMsgIdx >= (int)strlen(MORSE_MESSAGE)) {
    morseState    = M_MESSAGE_PAUSE;
    morseTimer    = millis();
    morseDuration = MORSE_PAUSE_MS;
    morseRest();
    Serial.println(F("MORSE | Message complete, pausing..."));
    return;
  }

  char c = MORSE_MESSAGE[morseMsgIdx];

  // Space = word gap
  if (c == ' ') {
    morseState    = M_WORD_GAP;
    morseTimer    = millis();
    morseDuration = MORSE_DOT_MS * 7;
    morseRest();
    return;
  }

  // Get code for next letter
  morseCurrentCode = getMorseCode(c);
  if (morseCurrentCode == NULL) {
    // Unsupported char — skip it
    morseAdvanceChar();
    return;
  }

  morseSymIdx = 0;

  // Inter-letter gap before starting this letter
  morseState    = M_LETTER_GAP;
  morseTimer    = millis();
  morseDuration = MORSE_DOT_MS * 3;
  morseRest();
}

// Main Morse update — call every loop() iteration
void updateMorse() {
  unsigned long now = millis();
  if (now - morseTimer < morseDuration) return;  // still in current state

  switch (morseState) {

    case M_MESSAGE_PAUSE:
      // Pause is over — start from the beginning
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
      // Fall through to start first element
      // no break

    case M_ELEMENT_GAP:
    case M_LETTER_GAP:
    case M_WORD_GAP:
      // Start the next element (dot or dash)
      if (morseCurrentCode == NULL || morseCurrentCode[morseSymIdx] == '\0') {
        // No more elements in this letter — advance to next char
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
      // Element just finished — turn off, then gap
      morseOff();
      morseSymIdx++;

      if (morseCurrentCode[morseSymIdx] == '\0') {
        // Last element of this letter — advance to next character
        morseAdvanceChar();
      } else {
        // Intra-character gap (1 dot unit)
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
  int light = readAverage(PIN_PHOTO, 6);

  Serial.print(F("LDR: "));
  Serial.print(light);
  Serial.print(F("  (threshold: "));
  Serial.print(darknessThreshold);
  Serial.print(F(", ON<"));
  Serial.print(darknessThreshold - HYSTERESIS_MARGIN);
  Serial.print(F(", OFF>"));
  Serial.print(darknessThreshold + HYSTERESIS_MARGIN);
  Serial.print(F(")  -> "));

  if (currentlyDark) {
    if (light > darknessThreshold + HYSTERESIS_MARGIN) {
      currentlyDark = false;
      Serial.println(F("BRIGHT (was dark)"));
    } else {
      Serial.println(F("DARK"));
    }
  } else {
    if (light < darknessThreshold - HYSTERESIS_MARGIN) {
      currentlyDark = true;
      Serial.println(F("DARK (was bright)"));
    } else {
      Serial.println(F("BRIGHT"));
    }
  }

  return currentlyDark;
}


// ============================================================
//  SETUP
// ============================================================

void setup() {
  Serial.begin(9600);

  randomSeed(analogRead(A2));

  // Resolve the Morse star pin
  morsePin      = starPins[MORSE_STAR_INDEX];
  morsePinIsPWM = isPWMPin(morsePin);

  // Configure star pins
  for (int i = 0; i < NUM_STARS; i++) {
    pinMode(starPins[i], OUTPUT);
    digitalWrite(starPins[i], LOW);
  }

  delay(1500);

  // Run guided calibration
  calibrateLight();

  // Configure RTC PIT for power-saving periodic wake-up
  setupRTCWake();

  Serial.println(F("Power-saving mode active."));
  Serial.println(F("Day: standby sleep (~8 s RTC wake cycles)"));
  Serial.println(F("Night: idle sleep between frames"));
  Serial.print(F("Morse star: index "));
  Serial.print(MORSE_STAR_INDEX);
  Serial.print(F(" (pin D"));
  Serial.print(morsePin);
  Serial.print(F(", PWM: "));
  Serial.print(morsePinIsPWM ? F("yes") : F("no"));
  Serial.println(F(")"));
}


// ============================================================
//  MAIN LOOP
// ============================================================

void loop() {

  bool dark = isRoomDark();

  if (dark) {
    // ---- NIGHT MODE: constellation ON ----

    if (!constellationOn) {
      Serial.println(F("NIGHT | Stars ON"));
      starsOn();
      morseReset();             // start (or restart) the Morse message
      lastLightCheck = millis();
    }

    // Animate the stars (Morse star excluded from breathing/flicker)
    updateBreathing();
    updateFlicker();
    updateScintillation();

    // Blink the hidden Morse message on the designated star
    updateMorse();

    // Periodically check if lights came back on
    if (millis() - lastLightCheck > LIGHT_CHECK_NIGHT) {
      lastLightCheck = millis();
    }

    // IDLE sleep between animation frames
    lightSleep(20);
  }
  else {
    // ---- DAY MODE: deep sleep ----

    if (constellationOn) {
      starsOff();
      Serial.println(F("DAY   | Lights detected - constellation OFF"));
      Serial.flush();
    }

    for (int i = 0; i < DAY_SLEEP_CYCLES; i++) {
      rtcWoke = false;
      deepSleep();
      if (isRoomDark()) break;
    }
  }
}
