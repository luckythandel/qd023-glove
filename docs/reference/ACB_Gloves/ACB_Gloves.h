#ifndef ACB_Gloves_H
#define ACB_Gloves_H

#include <Arduino.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

extern Adafruit_MPU6050 mpu;  // Mpu6050 object

class ACB_Gloves
{
	public:
		ACB_Gloves();
        
        void init();
        void get_data();
        String get_command();

        int potValue1 = 0;     // Potentiometer simulation value
        int potValue2 = 0;
        int potValue3 = 0;
        int potValue4 = 0;
        int potValue5 = 0;
        const int potentiometerPin1 = 36;  // Potentiometer PIN
        const int potentiometerPin2 = 39;
        const int potentiometerPin3 = 34;
        const int potentiometerPin4 = 35;
        const int potentiometerPin5 = 32;
        float AccelX, AccelY, AccelZ;  // Mpu6050 acceleration

        String gloves_command = "";

	private:
        
};

#endif