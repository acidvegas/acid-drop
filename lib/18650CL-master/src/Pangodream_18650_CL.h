/*
  MIT License
  
  Copyright (c) 2019 Pangodream
  
  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:
  
  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.
  
  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
*/

/* 
 *  18650 Ion-Li battery charge
 */
  
#ifndef Pangodream_18650_CL_h
#define Pangodream_18650_CL_h

#include "Arduino.h"

#define DEF_PIN 34
#define DEF_CONV_FACTOR 1.7
#define DEF_READS 20

/*
 * 18650 Ion-Li battery charge
 * Calculates charge level of an 18650 Ion-Li battery
 */
class Pangodream_18650_CL {    
  public:  
    
    /*
    * Constructor
    * @param addressPin, ADC pin number where the voltage divider is connected to
    */
    Pangodream_18650_CL(int addressPin);
    
    /*
    * Constructor
    * @param addressPin, ADC pin number where the voltage divider is connected to
    * @param convFactor, Convertion factor for analog read units to volts
    */
    Pangodream_18650_CL(int addressPin, double convFactor);
    
    /*
    * Constructor
    * @param addressPin, ADC pin number where the voltage divider is connected to
    * @param convFactor, Convertion factor for analog read units to volts
    * @param reads, Number of reads of analog pin to calculate an average value
    */
    Pangodream_18650_CL(int addressPin, double convFactor, int reads);
    /*
    * Constructor
    */
    Pangodream_18650_CL();    

    /*
     * Get the battery charge level (0-100)
     * @return The calculated battery charge level
     */
    int getBatteryChargeLevel();
    double getBatteryVolts();
    int getAnalogPin();
    int pinRead();
    double getConvFactor();
       
  private:

    int    _addressPin;               //!< ADC pin used, default is GPIO34 - ADC1_6
    int    _reads;                    //Number of reads of ADC pin to calculate an average value
    double _convFactor;               //!< Convertion factor to translate analog units to volts
    double _vs[101];                 //Array with voltage - charge definitions
    
    void   _initVoltsArray();
    int    _getChargeLevel(double volts);
    int    _analogRead(int pinNumber);
    double _analogReadToVolts(int readValue);
    
};

#endif