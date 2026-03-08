/*
 * Created by ArduinoGetStarted.com
 *
 * This example code is in the public domain
 *
 * Tutorial page: https://arduinogetstarted.com/tutorials/arduino-dht11
 */

 #include "DHT.h"
 #define DHT11_PIN 47
 
 DHT dht11(DHT11_PIN, DHT11);
 

 float surf_temp(){
  delay(2000);
  float tempC = dht11.readTemperature();
  return tempC;

 }
 void setup() {
   Serial.begin(115200);
   dht11.begin(); // initialize the sensor
 }
 
 void loop() {
   float x= surf_temp();
   Serial.println(x);
 }
 