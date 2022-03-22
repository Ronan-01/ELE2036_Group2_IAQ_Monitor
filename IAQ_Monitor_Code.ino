#define BLYNK_PRINT Serial                                     //Allows Blynk status to be printed on the serial monitor (can be commented out to save space)
#define BLYNK_TEMPLATE_ID "TMPLk1M0r2sK"                       //Define Blynk template ID
#define BLYNK_DEVICE_NAME "Air Quality Monitor"                //Define Blynk device name
#define BLYNK_AUTH_TOKEN "b1Z3EQ78uGZvfQLnPps1dpk01nvEbe8u"    //Define Blynk authentication token

#define pb1 18                                                 //Push button 1 (pb1) is connected to pin 18

#include <Wire.h>                                              //Include I2C library
#include "bsec.h"                                              //Include Bosch Sensortec Environmental Cluster libary (higher-level signal processing for the BME680) 
#include <Adafruit_GFX.h>                                      //Include Adafruit graphics library for OLED display     
#include <Adafruit_SSD1306.h>                                  //Include Adafruit OLED driver library for monochrome 128x64 and 128x32 displays

#include <WiFi.h>                                              //Include WiFi libary
#include <WiFiClient.h>                                        //Include WiFiClient library
#include <BlynkSimpleEsp32.h>                                  //Include Blynk library for esp32


Bsec iaqSensor;                                                //Create an object of the class Bsec
Adafruit_SSD1306 display(128, 32, &Wire);                      //Constructor for I2C OLED is Adafruit_SSD1306(display_width, display_height, &Wire)

char auth[] = BLYNK_AUTH_TOKEN;                                //Declare an array of characters to store the Blynk authentication token
char ssid[] = "{Enter Wi-Fi SSID}";                            //Declare an array of characters to store the network ssid
char wifiPass[] = "{Enter Wi-Fi Password}";                    //Declare an array of characters to store the WiFi password

String sensorStatus;                                           //Declare string to store BME680 or Bsec error/warning messages
String qualityComment;                                         //Declare string to store air quality comment
volatile bool buttonPressed;                                   //Declare bool to change state when button is pressed - used to change data displayed on OLED
int displayON;                                                 //Declare int to flag whether the OLED should be ON or OFF 

unsigned long currentTime;                                     //Declare unsigned long to store the time since the last reset in ms
unsigned long calibrationDelay;                                //Declare unsigned long to store the BME680 calibration time in ms
bool calibrationFinished;                                      //Declare bool to store whether the BME680 has finished calibrating

BlynkTimer timer;                                              //Create a BlynkTimer object


//Prototype functions
void sendData();                                               //Function to send data to Blynk server
void displayData();                                            //Function to display data on the serial monitor and the OLED
void displayCalibrationData();                                 //Function to display data available during calibration on the serial monitor and the OLED
void buttonISR();                                              //Interrupt service routine to toggle buttonPressed each time pb1 is pressed
void checkIaqSensorStatus();                                   //Function to check BME680 status


BLYNK_WRITE(V7)                                                //Recieve data from virtual pin 7 of the Blynk datastream (string storing the state of OLED display switch) each time V7 changes
{
  displayON = param.asInt();                                   //Convert string to int and store in displayON
}


void setup() 
{
  Serial.begin(115200);                                                //Begin serial communication at a baud rate of 115200
  pinMode(pb1, INPUT_PULLUP);                                          //Set pb1 as an input and enable internal pullup resistor
  attachInterrupt(digitalPinToInterrupt(pb1), buttonISR, RISING);      //Call buttonISR every time there is a rising edge on pin 32 (each time pb1 is pressed)

  displayON = 1;                                                       //Initialise displayON to 1 (OLED display ON)
  buttonPressed = true;                                                //Initialise buttonPressed to true (first page of data on OLED)

  Blynk.begin(auth, ssid, wifiPass);                                   //Connect ESP32 to WiFi network and connect to Blynk server
  timer.setInterval(3000L, sendData);                                  //Call sendData function every 3 seconds (low power sample rate of Bsec library)

  calibrationDelay = 1800000;                                          //1800000 ms (30 minute) calibration delay                    
  currentTime = 0;                                                     //Initialise current time to 0 ms
  calibrationFinished = false;                                         //Initially the BME680 is not calibrated
   
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);                           //Initialise OLED with the I2C addrss 0x3C and generate the high voltage from the 3.3v line internally 
  
  display.display();                                                   //Display buffer contents on OLED (library initialises this with an Adafruit splash screen) - can be removed to save space
  delay(100);                                                          //wait 0.1 seconds
  display.clearDisplay();                                              //Clear OLED display (all pixels OFF)
  display.display();                                                   //Show the cleared display                                                      
  display.setTextSize(1);                                              //Set text size to normal 1:1 pixel scale
  display.setTextColor(WHITE);                                         //Set text colour to white

  iaqSensor.begin(BME680_I2C_ADDR_SECONDARY, Wire);                    //Begin I2C communication with the BME680 sensor
  
  checkIaqSensorStatus();                                              //Call the function to check BME680 status and report any errors/warnings
  
  bsec_virtual_sensor_t sensorList[] = {                               //Array to store the parameters extracted from the BME680 readings
    BSEC_OUTPUT_RAW_TEMPERATURE,                                               
    BSEC_OUTPUT_RAW_PRESSURE,                                                
    BSEC_OUTPUT_RAW_HUMIDITY,                                                  
    BSEC_OUTPUT_RAW_GAS,                                                         
    BSEC_OUTPUT_IAQ,                                                      
    BSEC_OUTPUT_STATIC_IAQ,                                                       
    BSEC_OUTPUT_CO2_EQUIVALENT,                                                        
    BSEC_OUTPUT_BREATH_VOC_EQUIVALENT,                                                      
    BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE,                            
    BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY,                             
  };                                                                            
                                                                              
  iaqSensor.updateSubscription(sensorList, 10, BSEC_SAMPLE_RATE_LP);   //Update the value of the sensor list elements at the Bsec low power sample rate (once every 3 seconds)
  checkIaqSensorStatus();                                              //Call the function to check BME680 status and report any errors/warnings
}                                                                             
                                                                                
                                                                                
void loop()                                                                       
{                                                                                    
  Blynk.run();                                                         //Blynk routine responsible for keeping the connection to the Blynk server alive
  timer.run();                                                         //Run the timer to call the sendData function every 3 seconds
  
  if(!iaqSensor.run())                                                 //If no data is available
  { 
    checkIaqSensorStatus();                                            //Call the function to check BME680 status and report any errors/warnings
    return;
  }
}


void sendData()                                                          //Function to send data to Blynk server
{
  currentTime = millis();                                                //Update value of current time in ms
  
  if(currentTime >= calibrationDelay)                                    //If it has been more than 30 mins since the program started
  { 
    calibrationFinished = true;                                          //Set calibrationFinished to true - this will stay true even if currentTime overflows
  }

  if(calibrationFinished)                                                //If the calibration is finished
  {
    if(iaqSensor.staticIaq < 51)                                         //Check if the indoor air quality index is less than 51
    {
      qualityComment = "IAQ is Good! :-)";                               //Good indoor air quality according to the BME680 datasheet
    }
    else if(iaqSensor.staticIaq < 101 && iaqSensor.staticIaq >= 51)      //Check if the IAQ index is more than or equal to 51 but less than 101
    {
      qualityComment = "IAQ is Average";                                 //Average indoor air quality according to the BME680 datasheet
    }
    else if(iaqSensor.staticIaq < 151 && iaqSensor.staticIaq >= 101)     //Check if the IAQ index is more than or equal to 101 but less than 151
    {
      qualityComment = "IAQ is a Little Bad :-(";                        //Little bad indoor air quality according to the BME680 datasheet
    }
    else if(iaqSensor.staticIaq < 201 && iaqSensor.staticIaq >= 151)     //Check if the IAQ index is more than or equal to 151 but less than 201
    {
      qualityComment = "IAQ is Bad - Open a window";                     //Bad indoor air quality according to the BME680 datasheet
    }
    else if(iaqSensor.staticIaq < 301 && iaqSensor.staticIaq >= 201)     //Check if the IAQ index is more than or equal to 201 but less than 301
    {
      qualityComment = "IAQ is Worse than Bad - Increase Ventillation";  //Worse indoor air quality according to the BME680 datasheet
    }
    else if(iaqSensor.staticIaq <= 500 && iaqSensor.staticIaq >= 301)    //Check if the IAQ index is more than or equal to 301 but less than 500     
    {
      qualityComment = "IAQ is Very Bad - Take Immediate Action";        //Very bad indoor air quality according to the BME680 datasheet
    }                                                                         
                                                                              
    Blynk.virtualWrite(V0, iaqSensor.temperature);                       //Read temperature value and send to virtual pin 0 of the Blynk server datastream
    Blynk.virtualWrite(V1, iaqSensor.pressure/100.0);                    //Read pressure value and send to virtual pin 1 of the Blynk server datastream
    Blynk.virtualWrite(V2, iaqSensor.humidity);                          //Read humidity value and send to virtual pin 2 of the Blynk server datastream
    Blynk.virtualWrite(V3, iaqSensor.co2Equivalent);                     //Read equivalent carbon dioxide value and send to virtual pin 3 of the Blynk server datastream
    Blynk.virtualWrite(V4, iaqSensor.breathVocEquivalent);               //Read equivalent breath VOC value and send to virtual pin 4 of the Blynk server datastream
    Blynk.virtualWrite(V5, iaqSensor.staticIaq);                         //Read indoor air quality index value and send to virtual pin 5 of the Blynk server datastream
    Blynk.virtualWrite(V6, qualityComment);                              //Send qualityComment to virtual pin 6 of the Blynk server datastream
  
    displayData();                                                       //Call function to display data on the serial monitor and the OLED 
  }
  else                                                                   //If the BME680 sensor is still calibrating
  {
    qualityComment = "Waiting for sensor calibration.";
                                                                              
    Blynk.virtualWrite(V0, iaqSensor.temperature);                       //Read temperature value and send to virtual pin 0 of the Blynk server datastream
    Blynk.virtualWrite(V1, iaqSensor.pressure/100.0);                    //Read pressure value and send to virtual pin 1 of the Blynk server datastream
    Blynk.virtualWrite(V2, iaqSensor.humidity);                          //Read humidity value and send to virtual pin 2 of the Blynk server datastream
    Blynk.virtualWrite(V6, qualityComment);                              //Send qualityComment to virtual pin 6 of the Blynk server datastream
  
    displayCalibrationData();                                            //Call function to display data available during calibration on the serial monitor and the OLED 
  }
}


void displayData()                                                                                     //Function to display data on the serial monitor and the OLED 
{                                                                               
  Serial.println(String("\nTemperature = ") + iaqSensor.temperature + String(" °C"));                  //Display temperature value on the serial monitor
  Serial.println(String("Pressure = ") + (iaqSensor.pressure/100.0) + String(" hPa"));                 //Display pressure value on the serial monitor
  Serial.println(String("Humidity = ") + iaqSensor.humidity + String(" %"));                           //Display relative humidity value on the serial monitor
  Serial.println(String("Breath VOC Equivalent = ") + iaqSensor.breathVocEquivalent + String(" ppm")); //Display equivalent breath VOC value on the serial monitor
  Serial.println(String("CO2 Equivalent = ") + iaqSensor.co2Equivalent + String(" ppm"));              //Display equivalent carbon dioxide value on the serial monitor
  Serial.println(String("IAQ = ") + iaqSensor.staticIaq);                                              //Display indoor air quality index value on the serial monitor
  Serial.println(qualityComment);                                                                      //Display qualityComment on the serial monitor

  display.setCursor(0,0);                                                                              //Set the cursor to the top left of the OLED display (row 0, column 0)
  display.clearDisplay();                                                                              //Clear OLED display (all pixels OFF)

  if(buttonPressed && displayON == 1)                                                                  //If buttonPressed is true and displayON is 1, display first page of data on OLED
  {                                                                                                       
    display.println(String("Temperature: ") + iaqSensor.temperature + String(" *C"));                    
    display.println(String("Pressure: ") + (iaqSensor.pressure/100.0) + String(" hPa"));                     
    display.println(String("Humidity: ") + iaqSensor.humidity + String(" %"));                           
    display.println(String("VOC: ") + iaqSensor.breathVocEquivalent + String(" ppm"));                      
  }                                                                                                          
  else if(displayON == 1)                                                                              //If buttonPressed is false and displayON is 1, display second page of data on OLED
  {                                                                                                           
    display.println(String("eCO2: ") + iaqSensor.co2Equivalent + String(" ppm"));                       
    display.println(String("IAQ: ") + iaqSensor.staticIaq);                                               
    display.println(qualityComment);                                                                     
  }                                                                                                         
  display.display();                                                                                   //Update the OLED display with the new data
}             


void displayCalibrationData()                                                                          //Function to display data available during calibration on the serial monitor and the OLED 
{                                                                               
  Serial.println(String("\nTemperature = ") + iaqSensor.temperature + String(" °C"));                  //Display temperature value on the serial monitor
  Serial.println(String("Pressure = ") + (iaqSensor.pressure/100.0) + String(" hPa"));                 //Display pressure value on the serial monitor
  Serial.println(String("Humidity = ") + iaqSensor.humidity + String(" %"));                           //Display relative humidity value on the serial monitor
  Serial.println("Breath VOC Equivalent = Calibrating");                                               //Equivalent breath VOC value is not accurate until the BME680 has finished calibrating
  Serial.println("CO2 Equivalent = Calibrating");                                                      //Equivalent carbon dioxide value is not accurate until the BME680 has finished calibrating
  Serial.println("IAQ = Calibrating");                                                                 //Indoor air quality index value is not accurate until the BME680 has finished calibrating
  Serial.println(qualityComment);                                                                      //Display qualityComment on the serial monitor

  display.setCursor(0,0);                                                                              //Set the cursor to the top left of the OLED display (row 0, column 0)
  display.clearDisplay();                                                                              //Clear OLED display (all pixels OFF)

  if(buttonPressed && displayON == 1)                                                                  //If buttonPressed is true and displayON is 1, display first page of data on OLED
  {                                                                                                       
    display.println(String("Temperature: ") + iaqSensor.temperature + String(" *C"));                    
    display.println(String("Pressure: ") + (iaqSensor.pressure/100.0) + String(" hPa"));                     
    display.println(String("Humidity: ") + iaqSensor.humidity + String(" %"));                           
    display.println("VOC: -");                      
  }                                                                                                          
  else if(displayON == 1)                                                                              //If buttonPressed is false and displayON is 1, display second page of data on OLED
  {                                                                                                           
    display.println("eCO2: -");                       
    display.println("IAQ: -");                                               
    display.println(qualityComment);                                                                     
  }                                                                                                         
  display.display();                                                                                   //Update the OLED display with the new data
}
                                                                                
                                                                                  
void buttonISR()                                                         //Interrupt service routine to toggle buttonPressed every time pb1 is pressed
{
  buttonPressed = !buttonPressed;                                        //Toggle buttonPressed
  timer.setInterval(3000L, sendData);                                    //Reset the timer to call sendData function every 3 seconds so that the OLED display is updated immediatly (don't have to wait 3 seconds)
}


void checkIaqSensorStatus()                                              //Function to check BME680 status and report any errors/warnings
{
  if(iaqSensor.status != BSEC_OK)                                        //If the status of iaqSensor is not BSEC_OK
  {                                                                           
    if(iaqSensor.status < BSEC_OK)                                       //If the status of iaqSensor is less than BSEC_OK 
    {                                                                            
      sensorStatus = "BSEC error code : " + String(iaqSensor.status);    //Update sensorStatus to include the Bsec error       
      Serial.println(sensorStatus);                                      //Print Bsec error code to the serial monitor
      display.setCursor(0,0);                                            //Set the cursor to the top left of the OLED display (row 0, column 0)                                          
      display.println(sensorStatus);                                     //Display Bsec error code on the OLED display     
      display.display();                                                 //Update the OLED display with the new data   
      for (;;)  delay(10);                                               //Hault the program (delay forever)        
    }                                                                     
    else                                                                 //If the status of iaqSensor is more than BSEC_OK 
    {                                                                  
      sensorStatus = "BSEC warning code : " + String(iaqSensor.status);  //Update sensorStatus to include the Bsec warning 
      Serial.println(sensorStatus);                                      //Print Bsec warning code to the serial monitor
    }                                                                            
  }                                                                        
                                                                                    
  if(iaqSensor.bme680Status != BME680_OK)                                        //If the status of iaqSensor is not BME680_OK        
  {
    if(iaqSensor.bme680Status < BME680_OK)                                       //If the status of iaqSensor is less than BME680_OK 
    {
      sensorStatus = "BME680 error code : " + String(iaqSensor.bme680Status);    //Update sensorStatus to include the BME680 error 
      Serial.println(sensorStatus);                                              //Print BME680 error code to the serial monitor  
      display.setCursor(0,0);                                                    //Set the cursor to the top left of the OLED display (row 0, column 0)
      display.println(sensorStatus);                                             //Display BME680 error code on the OLED display 
      display.display();                                                         //Update the OLED display with the new data 
      for (;;)  delay(10);                                                       //Hault the program (delay forever)  
    } 
    else                                                                         //If the status of iaqSensor is more than BME680_OK
    {
      sensorStatus = "BME680 warning code : " + String(iaqSensor.bme680Status);  //Update sensorStatus to include the BME680 warning 
      Serial.println(sensorStatus);                                              //Print BME680 warning code to the serial monitor
    }
  }
}
