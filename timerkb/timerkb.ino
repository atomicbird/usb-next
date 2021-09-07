#define PAUSE_DURATION 400 // Time, in milliseconds, between KB queries.
#define DEBUG

#ifdef DEBUG
#define debug(MSG) Serial.print(MSG)
#define debugf(MSG, MODE) Serial.print(MSG, MODE)
#define debugln(MSG) Serial.println(MSG)
#else
#define debug(MSG)
#define debugf(MSG, MODE)
#define debugln(MSG)
#endif

#define OUT_PIN 3
#define IN_PIN 2

// Macros and caches for reading data from the input pin
volatile uint8_t *input_pin_reg;
uint8_t input_pin_mask;
#define readbit() ((*input_pin_reg) & input_pin_mask)

// Sampling math
#define CLOCK_FREQ 16000000
#define KB_DATA_FREQ 19000
#define PULSE_WIDTH 1000000 / KB_DATA_FREQ
#define SAMPLES_PER_PULSE 9
#define KB_SAMPLE_FREQ CLOCK_FREQ / KB_DATA_FREQ / SAMPLES_PER_PULSE

const uint16_t t1_load = 0;
const uint16_t t1_comp = KB_SAMPLE_FREQ;

//#define KB_ENABLE

enum state{
           ready,
           query_inflight,
           query_sent,
           query_response_timeout,
           query_response_ready,
           query_response_complete,
           pause,
};

// Global state
volatile enum state currentState = ready;
volatile uint8_t currentKeycode = 0;
volatile uint8_t currentModifier = 0;
volatile uint32_t currentData = 0xFFFFFFFF;

volatile uint8_t highbit_counts[32];

volatile uint8_t currentResponseIndex = 0;

///////
// Protocol
///////

void sendKBQuery() {
  digitalWrite(OUT_PIN, LOW);
  delayMicroseconds(PULSE_WIDTH *5);
  digitalWrite(OUT_PIN, HIGH);
  delayMicroseconds(PULSE_WIDTH );
  digitalWrite(OUT_PIN, LOW);
  delayMicroseconds(PULSE_WIDTH *3);
  digitalWrite(OUT_PIN, HIGH);
}

void sendKBReset() {
  // reset the keyboard
  digitalWrite(OUT_PIN, LOW);
  delayMicroseconds(PULSE_WIDTH);
  digitalWrite(OUT_PIN, HIGH);
  delayMicroseconds(PULSE_WIDTH*4);
  digitalWrite(OUT_PIN, LOW);
  delayMicroseconds(PULSE_WIDTH);
  digitalWrite(OUT_PIN, HIGH);
  delayMicroseconds(PULSE_WIDTH*6);
  digitalWrite(OUT_PIN, LOW);
  delayMicroseconds(PULSE_WIDTH*10);
  digitalWrite(OUT_PIN, HIGH);
}
//

/////////
/// State transition handlers
////////
void handleReady() {
  // Disable interrupts while we configure things
  cli();

  // Set up interrupt for when we get a response back.
  enableResponseInterrupt();
  // Set up interrupt for when we timeout.
  enableTimeoutInterrupt();

  // Proceed to next state.
  currentState = query_sent;

  // Send the query.
  sendKBQuery();

  // Turn interrupts back on
  sei();
}

/////
// Start-of-response interrupt
/////
ISR(INT0_vect) {
  // Triggered when we get the falling edge of Pin INT0.
  // Reset data to zero.
  currentData = 0xFFFFFFFF;

  // Delay for half a pulse
  delayMicroseconds(PULSE_WIDTH*3 / 2);

  // We can turn off the timeout.
  disableTimeoutInterrupt();

  // Also, turn off the response interrupt.
  disableResponseInterrupt();

  // Now we can turn on the ticker for reading data.
  enableReaderInterrupt();

  // Mark us as reading data.
  currentState = query_response_ready;
}

void enableResponseInterrupt() {
  // Enable interrupts for INT0.
  EIMSK |= (1 << INT0);
}

void configureResponseInterrupt() {
  // Enable interrupts on the falling edge of Pin INT0
  EICRA |=  (1 << ISC01);
  EICRA &= ~(1 << ISC00);
}

void disableResponseInterrupt() {
  EIMSK &= ~(1 << INT0);
}

/////
// Timeout interrupt (TODO)
/////
void enableTimeoutInterrupt() {}
void configureTimeoutInterrupt() {}
void disableTimeoutInterrupt() {}

/////
// Reader interrupt
/////
ISR(TIMER1_COMPA_vect) {
  TCNT1 = 0;

  static uint8_t nSampled = 0;

  if (readbit()) {
    highbit_counts[currentResponseIndex]++;
  }
  nSampled++;

  if (nSampled >= SAMPLES_PER_PULSE) {
    nSampled = 0;
    currentResponseIndex++;
  }

  if (currentResponseIndex >= 32) {
    currentResponseIndex = 0;
    nSampled = 0;
    disableReaderInterrupt();
    currentState = query_response_complete;
  }
}

void enableReaderInterrupt() {
  // Reset to zero
  TCNT1 = 0;
  // Enable register A comparison interrupts
  TIMSK1 |= (1 << OCIE1A);
}

void configureReaderInterrupt() {
  // Set timer to defaults in case arduino has done anything funky
  TCCR1A = 0;

  // No prescaling
  TCCR1B &= ~(1 << CS12);
  TCCR1B &= ~(1 << CS11);
  TCCR1B |= (1 << CS10);

  // Set the trigger threshold
  OCR1A = t1_comp;
}

void disableReaderInterrupt() {
  // Disable interrupts
  TIMSK1 &= ~(1 << OCIE1A);
  // Clear any active interrupts
}


void handleQueryResponseTimeout() {
  // Send a reset
  sendKBReset();
  currentState = pause;
}

void handlePause() {
  delay(PAUSE_DURATION);
  currentState = ready;
}

void handleQueryResponse() {
  // Send current key over USB.
  debug("current data: ");
  for (uint8_t i = 0; i < 32; i++) {
    debug(highbit_counts[i]);
    debug(" ");
    if (i % 4 == 3) {
      debug(" ");
    }
    highbit_counts[i] = 0;
  }
  debugln();
  // sendKey(currentKeycode, currentModifier);
  currentState = pause;
}

void sendKey(uint8_t keycode, uint8_t modifier) {
  #ifdef KB_ENABLE
  #endif

  debug("sending key code=");
  debugf(keycode, HEX);
  debug(" mod=");
  debugf(modifier, HEX);
  debugln();
}

void loop() {
  switch (currentState) {
  case ready:
    handleReady();
    break;
  case query_sent:
    // No-op. Waiting for a timeout or ready state.
    break;
  case query_response_timeout:
    debugln("timeout");
    handleQueryResponseTimeout();
    break;
  case query_response_ready:
    // No-op. Wait for response complete.
    break;
  case query_response_complete:
    handleQueryResponse();
    break;
  case pause:
    handlePause();
    break;
  }
}
void setup() {
  pinMode(OUT_PIN, OUTPUT);
  pinMode(IN_PIN, INPUT);

  input_pin_reg = portInputRegister(digitalPinToPort(IN_PIN));
  input_pin_mask = digitalPinToBitMask(IN_PIN);

  configureResponseInterrupt();
  configureReaderInterrupt();
#ifdef DEBUG
  while (!Serial)
  Serial.begin(57600);
  Serial.println("NeXT keyboard initialized!");
#endif

  debug("Pulse width: "); debugln(PULSE_WIDTH);
  debug("Samples per pulse: "); debugln(SAMPLES_PER_PULSE);
  debug("Sample Frequency: "); debugln(KB_SAMPLE_FREQ);
}
