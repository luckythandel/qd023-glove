#include "ACB_Gloves.h"

Adafruit_MPU6050 mpu;

ACB_Gloves::ACB_Gloves()
{

}

void ACB_Gloves::init()
{
    Serial.begin(115200);
    while (!Serial)
        delay(10); // will pause Zero, Leonardo, etc until serial console opens

    // Try to initialize!
    while (!mpu.begin())
    {
        Serial.println("Failed to find MPU6050 chip");
        delay(10);
    }
    delay(100);
    Serial.println("MPU6050 Found!");

    // setupt motion detection
    mpu.setHighPassFilter(MPU6050_HIGHPASS_0_63_HZ); // Configure the cutoff frequency of the MPU6050 high-pass filter
    mpu.setMotionDetectionThreshold(1);              // Set the sensitivity threshold of motion detection
    mpu.setMotionDetectionDuration(20);              // Set the duration threshold for motion detection events.
    mpu.setInterruptPinLatch(true);                  // Keep it latched.  Will turn off when reinitialized.
    mpu.setInterruptPinPolarity(true);               // Configures the polarity of the interrupt pin
    mpu.setMotionInterrupt(true);                    // Enable motion detection interrupt.
    delay(100);
}

void ACB_Gloves::get_data()
{
    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);

    // Read the MPU6050 value
    AccelX = a.acceleration.x;
    AccelY = a.acceleration.y;
    AccelZ = a.acceleration.z;

    // Read the Rotate the potentiometer value
    potValue1 = analogRead(potentiometerPin1);
    potValue2 = analogRead(potentiometerPin2);
    potValue3 = analogRead(potentiometerPin3);
    potValue4 = analogRead(potentiometerPin4);
    potValue5 = analogRead(potentiometerPin5);

    delay(5);
}

String ACB_Gloves::get_command()
{
    if (AccelY < -5 && potValue1 < 200 && potValue2 < 200 && potValue3 < 200 & potValue4 < 200 && potValue5 < 200)
    {
        gloves_command = "command11";
    }

    else if (AccelY > 5 && potValue1 < 200 && potValue2 < 200 && potValue3 < 200 & potValue4 < 200 && potValue5 < 200)
    {
        gloves_command = "command12";
    }

    else if (AccelX < -5 && potValue1 < 200 && potValue2 < 200 && potValue3 < 200 & potValue4 < 200 && potValue5 < 200)
    {
        gloves_command = "command15";
    }

    else if (AccelX > 5 && potValue1 < 200 && potValue2 < 200 && potValue3 < 200 & potValue4 < 200 && potValue5 < 200)
    {
        gloves_command = "command16";
    }

    else if (AccelX < -8 && potValue1 > 200 && potValue2 > 200 && potValue3 > 200 & potValue4 > 200 && potValue5 > 200)
    {
        gloves_command = "command13";
    }

    else if (AccelX > 8 && potValue1 > 200 && potValue2 > 200 && potValue3 > 200 & potValue4 > 200 && potValue5 > 200)
    {
        gloves_command = "command14";
    }

    else if (AccelX > 5 && AccelY < -5 && potValue1 < 200 && potValue2 < 200 && potValue3 > 200 & potValue4 > 200 && potValue5 > 200)
    {
        gloves_command = "command17";
    }

    else if (AccelX > 5 && potValue1 < 200 && potValue2 < 200 && potValue3 > 200 & potValue4 > 200 && potValue5 > 200)
    {
        gloves_command = "command3";
    }
    else if (AccelX < -5 && potValue1 < 200 && potValue2 < 200 && potValue3 > 200 & potValue4 > 200 && potValue5 > 200)
    {
        gloves_command = "command4";
    }

    else if (AccelY < -5 && potValue1 > 200 && potValue2 < 200 && potValue3 > 200 & potValue4 > 200 && potValue5 > 200)
    {
        gloves_command = "command1";
    }
    else if (AccelY > 5 && potValue1 > 200 && potValue2 < 200 && potValue3 > 200 & potValue4 > 200 && potValue5 > 200)
    {
        gloves_command = "command2";
    }

    else if (AccelY < -5 && potValue1 > 200 && potValue2 < 200 && potValue3<200 & potValue4> 200 && potValue5 > 200)
    {
        gloves_command = "command5";
    }
    else if (AccelY > 5 && potValue1 > 200 && potValue2 < 200 && potValue3<200 & potValue4> 200 && potValue5 > 200)
    {
        gloves_command = "command6";
    }

    else if (AccelX < -5 && potValue1 > 200 && potValue2 < 200 && potValue3<200 & potValue4> 200 && potValue5 > 200)
    {
        gloves_command = "command9";
    }

    else if (AccelX > 5 && potValue1 > 200 && potValue2 < 200 && potValue3<200 & potValue4> 200 && potValue5 > 200)
    {
        gloves_command = "command10";
    }

    else if (AccelX < -5 && potValue1 > 200 && potValue2 < 200 && potValue3 < 200 & potValue4 < 200 && potValue5 < 200)
    {
        gloves_command = "command7";
    }

    else if (AccelX > 5 && potValue1 > 200 && potValue2 < 200 && potValue3 < 200 & potValue4 < 200 && potValue5 < 200)
    {
        gloves_command = "command8";
    }

    else
    {
        gloves_command = "command0";
    }

    return gloves_command;
}