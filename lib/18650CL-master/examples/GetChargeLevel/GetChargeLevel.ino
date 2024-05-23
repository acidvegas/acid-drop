#include <Pangodream_18650_CL.h>

//#define ADC_PIN 34
//#define CONV_FACTOR 1.7
//#define READS 20

Pangodream_18650_CL BL;
/**
 * If you need to change default values you can use it as
 * Pangodream_18650_CL BL(ADC_PIN, CONV_FACTOR, READS);
 */

void setup() {
  Serial.begin(115200);
}

void loop() {
  Serial.print("Value from pin: ");
  Serial.println(analogRead(34));
  Serial.print("Average value from pin: ");
  Serial.println(BL.pinRead());
  Serial.print("Volts: ");
  Serial.println(BL.getBatteryVolts());
  Serial.print("Charge level: ");
  Serial.println(BL.getBatteryChargeLevel());
  Serial.println("");
  delay(1000);
}
