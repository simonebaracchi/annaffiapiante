
#include <Wire.h>
#include <RTClib.h>
#include <avr/sleep.h>
#include <avr/power.h>
#include <avr/wdt.h>
#define DAY   (60*60*24UL)
RTC_DS1307 rtc;

#if 1
#define DEBUG
#endif

/******** Configurazione *******/

/* Intervallo tra due annaffiate, in secondi (o giorni) */
uint32_t waterInterval = 2; //(2 * DAY) - 300;

/* Quantita' di acqua da pompare, in microlitri */
uint32_t waterAmount = 8000;
/***************************/

/* How many microliters per second can we pump? */
/* My submersible pump on paper is capable of about 22ml/second,
 * the peristaltic pump is about 0.47ml/secondo
 */
#define MICROLITERS_PER_SECOND 470

int pinPump = 4;
int pinLed = 13;
int pinDebug = 10; /** Attacca +5V al pin 10 per attivare il debug (accende un led anzichè la pompa) **/
int pinClock = 7;
int pinPower = 12;
int debug = 0;
uint32_t waterLast = 0;



// watchdog interrupt
ISR (WDT_vect)
{
  wdt_disable();  // disable watchdog
}  // end of WDT_vect

void
sleep_some_time()
{
  // disable ADC
  byte old_ADCSRA = ADCSRA;
  ADCSRA = 0;
  power_all_disable();

  PCIFR  |= bit (PCIF0) | bit (PCIF1) | bit (PCIF2);   // clear any outstanding interrupts
  PCICR  = 0;

  TWCR = bit(TWEN) | bit(TWIE) | bit(TWEA) | bit(TWINT);
  digitalWrite (A4, LOW);
  digitalWrite (A5, LOW);

  // clear various "reset" flags
  MCUSR = 0;
  // allow changes, disable reset
  WDTCSR = bit (WDCE) | bit (WDE);
  // set interrupt mode and an interval
  WDTCSR = bit (WDIE) | bit (WDP3) | bit (WDP0);    // set WDIE, and 8 seconds delay
  wdt_reset();  // pat the dog

  set_sleep_mode (SLEEP_MODE_PWR_DOWN);
  noInterrupts ();           // timed sequence follows
  sleep_enable();

  // turn off brown-out enable in software
  MCUCR = bit (BODS) | bit (BODSE);
  MCUCR = bit (BODS);
  interrupts ();             // guarantees next instruction executed
  sleep_cpu ();

  // cancel sleep as a precaution
  sleep_disable();
  power_all_enable();
  ADCSRA = old_ADCSRA;
}

void
power_saving_init()
{
  for (byte i = 0; i <= A5; i++) {
    pinMode (i, OUTPUT);    // changed as per below
    digitalWrite (i, LOW);  //     ditto
  }

  //clock_prescale_set (clock_div_128); /* occhio, sballa i delay */

}

void printDate(const DateTime& dt) {
  Serial.print(dt.year(), DEC);
  Serial.print('/');
  Serial.print(dt.month(), DEC);
  Serial.print('/');
  Serial.print(dt.day(), DEC);
  Serial.print(' ');
  Serial.print(dt.hour(), DEC);
  Serial.print(':');
  Serial.print(dt.minute(), DEC);
  Serial.print(':');
  Serial.print(dt.second(), DEC);
}

void startingBlink()
{
  digitalWrite(pinPower, HIGH);
  delay(800);
  digitalWrite(pinPower, LOW);
  delay(800);
  digitalWrite(pinPower, HIGH);
  delay(800);
  digitalWrite(pinPower, LOW);
  delay(800);
}
void startedBlink()
{
  digitalWrite(pinPower, HIGH);
  delay(800);
  digitalWrite(pinPower, LOW);
  delay(800);
}

void wateringStartBlink()
{
  digitalWrite(pinPower, HIGH);
  digitalWrite(pinLed, HIGH);
  delay(15);
  digitalWrite(pinPower, LOW);
  digitalWrite(pinLed, LOW);
  delay(50);
  digitalWrite(pinPower, HIGH);
  digitalWrite(pinLed, HIGH);
  delay(15);
  digitalWrite(pinPower, LOW);
  digitalWrite(pinLed, LOW);
  delay(50);
}

void wateringEndBlink()
{
  digitalWrite(pinPower, HIGH);
  digitalWrite(pinLed, HIGH);
  delay(15);
  digitalWrite(pinPower, LOW);
  digitalWrite(pinLed, LOW);
  delay(50);
}

void lowBatteryBlink()
{
  digitalWrite(pinPower, HIGH);
  delay(15);
  digitalWrite(pinPower, LOW);
  delay(15);
}


const long InternalReferenceVoltage = 1700; /* I have no idea of what this is */
void getInputLevel(int16_t *raw, int16_t *millivolts) {
  // REFS0 : Selects AVcc external reference
  // MUX3 MUX2 MUX1 : Selects 1.1V (VBG)
  ADMUX = bit (REFS0) | bit (MUX3) | bit (MUX2) | bit (MUX1);
  ADCSRA |= bit( ADSC );  // start conversion
  while (ADCSRA & bit (ADSC))
  { }  // wait for conversion to complete
  *raw = ADC;
  *millivolts = (((InternalReferenceVoltage * 1024) / (*raw)) + 5);
}
void printInputPower()
{
  int16_t raw, millivolts;
  getInputLevel(&raw, &millivolts);
  char buf[20];
  sprintf(buf, "%d.%03dV (%d)", millivolts / 1000, millivolts % 1000, raw);
  Serial.print("Approx input: ");
  Serial.println(buf);
}

void checkInputPower()
{
  int16_t raw, millivolts;
  getInputLevel(&raw, &millivolts);
  if (millivolts < 4500) {
    lowBatteryBlink();
  }
}

void checkIfCounterShouldBeReset(uint32_t last) {
  // Change this to the timestamp of the last watering
  // if you want to reset the watering timer
  if (last == 1456444029) {
    uint32_t dummy = 0;
    Serial.println("Looks like you want to reset the watering timer.");
    rtc.writenvram(0, (uint8_t *)&dummy, 4);
    Serial.println("If that is the case, please shut me down in 10...");
    for (int i = 9; i > 0; i--) {
      Serial.print(i, DEC);
      Serial.print("... ");
      delay(1000);
    }
    rtc.writenvram(0, (uint8_t *)&last, 4);
    Serial.println("OK, no reset then!");
  }
}

void setup() {
  Serial.begin(57600);
  Serial.println("Hello, world!");
  power_saving_init();
  startingBlink();
  pinMode(pinDebug, INPUT);
  digitalWrite(pinDebug, LOW);
  debug = digitalRead(pinDebug) == HIGH;
  if (debug)
    Serial.println("Debugging is ENABLED");
  else
    Serial.println("Debugging is disabled");

  printInputPower();

  pinMode(pinPump, OUTPUT);
  digitalWrite(pinPump, LOW);

  pinMode(pinLed, OUTPUT);
  digitalWrite(pinLed, LOW);

  pinMode(pinPower, OUTPUT);
  digitalWrite(pinPower, LOW);

  pinMode(pinClock, OUTPUT);
  digitalWrite(pinClock, HIGH);

  if (debug)
    pinPump = pinLed;

  /* inizializzo RTC */
  {
#ifdef AVR
    Serial.println("*******************");
    Serial.println("Initializing AVR...");
    Wire.begin();
#else
    Serial.println("Initializing NO AVR...");
    Wire1.begin(); // Shield I2C pins connect to alt I2C bus on Arduino Due
#endif
    Serial.println("Initializing RTC...");
    rtc.begin();

    Serial.println("Checking RTC...");
    if (! rtc.isrunning()) {
      Serial.println("RTC is NOT running!");
      // following line sets the RTC to the date & time this sketch was compiled
      rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
      // This line sets the RTC with an explicit date & time, for example to set
      // January 21, 2014 at 3am you would call:
      // rtc.adjust(DateTime(2014, 1, 21, 3, 0, 0));
    }

    DateTime datetime = rtc.now();
    Serial.print("Current time: ");
    printDate(datetime);
    Serial.println("");
    Serial.println("*******************");
  }


  digitalWrite(pinClock, LOW);
  Serial.println("Starting...");
  Serial.end();


  delay(15);
  startedBlink();
}

void loop() {
  digitalWrite(pinClock, HIGH);
  DateTime datetime = rtc.now();

  uint32_t timeNow = datetime.unixtime();
  int mustWater = 0;

  if (waterLast == 0) {
    rtc.readnvram((uint8_t *)&waterLast, 4, 0);
    Serial.begin(57600);

    Serial.print("Read last watering from clock: ");
    printDate(DateTime(waterLast));
    Serial.print(" (");
    Serial.print(waterLast, DEC);
    Serial.println(")");
    checkIfCounterShouldBeReset(waterLast);
    if (waterLast > timeNow) {
      Serial.println("Last watering is in the future... am I time traveling?");
      waterLast = 1;
    }
    Serial.end();
  }

  /* Controllo se e' passato abbastanza tempo dall'ultima annaffiatura.
   * Annaffio al massimo ogni 10 secondi ... come sicurezza.
   */
  if (timeNow - waterLast > max(10, max(waterInterval, 20))) {
    mustWater = 1;
  }
  /* Evita di innaffiare durante il giorno e la notte */
  if (datetime.hour() < 17 || datetime.hour() > 24) {
    mustWater = 0;
  }

  
    Serial.begin(57600);
    Serial.print("Watering???");
    Serial.print(mustWater, DEC);
    Serial.println("");
    Serial.end();


  if (mustWater) {
    uint32_t amountInSeconds = (waterAmount) / MICROLITERS_PER_SECOND;
    Serial.begin(57600);
    Serial.print("Watering! ul=");
    Serial.print(waterAmount, DEC);
    Serial.print(", ms=");
    Serial.print(amountInSeconds, DEC);
    Serial.print(", time=");
    printDate(datetime);
    Serial.print(" (");
    Serial.print(timeNow, DEC);
    Serial.print(")");
    Serial.println("");
    printInputPower();
    wateringStartBlink();
    Serial.end();

    waterLast = timeNow;
    rtc.writenvram(0, (uint8_t *)&waterLast, 4);
    digitalWrite(pinPump, HIGH);
    delay(amountInSeconds * 1000);
    digitalWrite(pinPump, LOW);
    wateringEndBlink();
  }
  digitalWrite(pinClock, LOW);

  checkInputPower();

  sleep_some_time();
}
