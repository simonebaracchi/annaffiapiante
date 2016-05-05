
#include <Wire.h>
#include <RTClib.h>
#define DAY   (60*60*24)

/***** configurazione ******/
/* in secondi */
int waterInterval = 10; //1 * DAY;
int waterAmount = 2;
/*****                ******/


RTC_DS1307 rtc;
int pinPump = 4;

void setup() {
  Serial.begin(57600);
  
  pinMode(pinPump, OUTPUT);  
  digitalWrite(pinPump, LOW);
  
  /* inizializzo RTC */
  {
    #ifdef AVR
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
   }
   Serial.println("Starting...");
}

void loop() {
  uint32_t time = rtc.now().unixtime();
  uint32_t waterLast = 0;
  int mustWater = 0;
  
  rtc.readnvram((uint8_t *)&waterLast, 4, 0);
  
  if(time - waterLast > waterInterval) {
    mustWater = 1;
  }
  
  if(mustWater) {
    Serial.println("Watering!");
    rtc.writenvram(0, (uint8_t *)&time, 4);  
    digitalWrite(pinPump, HIGH);
    delay(waterAmount * 1000);
    digitalWrite(pinPump, LOW);
  }
   
  delay(1000);
}
