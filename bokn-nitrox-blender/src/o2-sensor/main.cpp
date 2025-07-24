#include <Arduino.h>
#include "TFT_eSPI.h" /* Please use the TFT library provided in the library. */
#include "img_logo.h"
#include "pin_config.h"

#define FONT_LARGE &Dialog_plain_100 // Key label font 2
#define SENSOR_SERIAL Serial1  // Change this to match your board's UART (e.g., Serial2 on STM32)

TFT_eSPI tft = TFT_eSPI();

const uint8_t rx1_pin = 17;
const uint8_t tx1_pin = 18;

void setup()
{

  pinMode(PIN_POWER_ON, OUTPUT);
  digitalWrite(PIN_POWER_ON, HIGH);

  pinMode(PIN_POTENTIOMETER, INPUT);

  Serial1.begin(9600, SERIAL_8N1, rx1_pin, tx1_pin);
  // Serial1.begin(9600); //, SERIAL_8N1, rx1_pin, tx1_pin);
  // Serial1.begin(9600);
  // sensorSerial.begin(9600, SERIAL_8N1,  MySerialRX, MySerialTX);

  Serial.begin(115200);
  Serial.println("Hello T-Display-S3");

  // Setup screen
  tft.begin();

  tft.setRotation(3);
  tft.setSwapBytes(true);
  tft.pushImage(0, 0, 320, 170, (uint16_t *)img_logo);

  delay(2000);

  ledcSetup(0, 2000, 8);
  ledcAttachPin(PIN_LCD_BL, 0);
  ledcWrite(0, 255);


    // tft_percent.setTextColor(TFT_RED);
    // tft_percent.drawString("ERR-1", 0, 0);    

  // tft.setTextColor(TFT_RED);
  // tft.drawString("ERR-1", 0, 0)
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_GREEN);


  tft.drawString("Connected...", 0, 0, 3);


}

bool verifyChecksum(const uint8_t* frame, uint8_t length);
void parseSensorData(const uint8_t* frame);
void printHexFrame(const uint8_t* frame, uint8_t length);

const uint8_t FRAME_LENGTH = 12;
uint8_t buffer[FRAME_LENGTH];
int idx = 0;
bool readingFrame = false;

void loop()
{

    // while (Serial1.available()) {
    //     Serial.write(Serial1.read());
    // }

      while (Serial1.available()) {
    uint8_t byteIn = Serial1.read();

    if (!readingFrame) {
      if (byteIn == 0x16) {
        readingFrame = true;
        idx = 0;
        buffer[idx++] = byteIn;
      }
    } else {
      buffer[idx++] = byteIn;
      if (idx == FRAME_LENGTH) {
        readingFrame = false;
        // printHexFrame(buffer, FRAME_LENGTH);
        verifyChecksum(buffer, FRAME_LENGTH);
        if (true) {
          parseSensorData(buffer);
        } else {
          Serial.println("Checksum failed!");
        }
      }
    }
  }

    delay(1);
}

void printHexFrame(const uint8_t* frame, uint8_t length) {
  Serial.print("Raw frame: ");
  for (uint8_t i = 0; i < length; i++) {
    if (frame[i] < 0x10) Serial.print("0"); // Leading zero for single-digit hex
    Serial.print(frame[i], HEX);
    Serial.print(" ");
  }
  Serial.println();
}

// String incomingData = "";

// long lastScreenUpdate = 0;
// long lastRead = 0;

// unsigned long timerDelay = 5000;
// unsigned long lastTime = 0;

// bool verifyChecksum(const uint8_t* frame, uint8_t length);
// void parseSensorData(const uint8_t* frame);

// const uint8_t FRAME_LENGTH = 12;
// uint8_t buffer[FRAME_LENGTH];
// int idx = 0;
// bool readingFrame = false;

// void loop() {
//   // Read from the sensor's UART
//   while (Serial1.available()) {
//     uint8_t byteIn = Serial1.read();

//     if (!readingFrame) {
//       if (byteIn == 0x16) {
//         readingFrame = true;
//         idx = 0;
//         buffer[idx++] = byteIn;
//       }
//     } else {
//       buffer[idx++] = byteIn;
//       if (idx == FRAME_LENGTH) {
//         readingFrame = false;
//         if (verifyChecksum(buffer, FRAME_LENGTH)) {
//           Serial.println("Parsing frame");
//           parseSensorData(buffer);
//         } else {
//           Serial.println("Checksum failed!");
//         }
//       }
//     }
//   }

//   delay(500);
//   Serial.println("Loop");
// }

bool verifyChecksum(const uint8_t* frame, uint8_t length) {
  uint8_t sum = 0;
  for (uint8_t i = 0; i < length - 1; i++) {
    sum += frame[i];
  }
  // Serial.print("checksum: value [");
  // Serial.print(frame[12], HEX);
  // Serial.print("] sum [");
  // Serial.print(sum, HEX);
  // Serial.println("]");

  return sum == frame[length - 1];
}

// Parses a string like "O2=93.4%; FLOW=5.2L"
void parseSensorData(const uint8_t* frame) {
  uint16_t concRaw = (frame[3] << 8) | frame[4];
  uint16_t flowRaw = (frame[5] << 8) | frame[6];
  uint16_t tempRaw = (frame[7] << 8) | frame[8];

  float concentration = concRaw / 10.0;
  float flow = flowRaw / 10.0;
  float temperature = tempRaw / 10.0;

  Serial.print("O2: ");
  Serial.print(concentration);
  Serial.print(" %, Flow: ");
  Serial.print(flow);
  Serial.print(" L/min, Temp: ");
  Serial.print(temperature);
  Serial.println(" °C");
}

// void loop() {
//   typedef unsigned char u8;
//   typedef unsigned int u16;
//   int inByte;
//   u8 temp;
//   u8 i, j, o2[12];
//   u16 o2c, o2f, o2t; //Define oxygen concentration, flow rate and temperature
 
//   //When character arrive over the serial port ..
//   if (Serial1.available()) {
//     // wait a bit for the entire message to arrive
//     delay(100);
//     // read all the available characters
//     while (Serial1.available() > 0) {
//       Serial.print(".");
//       inByte = Serial1.read();
//       //---Receiving part---
//       if ((o2[0] == 0x16) && (o2[1] == 0x09) && (o2[2] == 0x01)) //Determine if the first two bytes are received correctly, I is the global variable
//       {
//         o2[i] = inByte;      
//         i++;
//       }
//       else           //If one of the first three bytes received is incorrect, the first two bytes will be judged
//       {
//         if ((o2[0] == 0x16) && (o2[1] == 0x09))
//         {
//           if ( inByte == 0x01)  
//           {
//             o2[2] =  inByte;  
//             i++;
//           }
//           else                                
//           {
//             i = 0;                
//             for (j = 0; j < 12; j++)          
//             {
//               o2[j] = 0;
//             }
//           }
//         }
//         else    
//         {
//           if (o2[0] == 0x16)
//           {
//             if ( inByte == 0x09)
//             {
//               o2[1] =  inByte;
//               i++;
//             }
//             else        
//             {
//               i = 0;                
//               for (j = 0; j < 12; j++)          
//               {
//                 o2[j] = 0;
//               }
//             }
//           }
//           else    
//           {
//             if ( inByte == 0x16)
//             {
//               o2[0] =  inByte;
//               i++;
//             }
//             else        
//             {
//               i = 0;                
//               for (j = 0; j < 12; j++)        
//               {
//                 o2[j] = 0;
//               }
//             }
//           }
//         }
//       }
//      //---Receiving part---
 
 
 
//       if (i == 12)   //Data received complete, start calibration
//       {
//         temp = 0;
//         for (j = 0; j < 12; j++)
//         {
//           temp += o2[j];
//         }
//         if (temp == 0)     //Check passed, calculate oxygen concentration, flow, temperature value
//         {
//           o2c = o2[3] * 256 + o2[4];     //Oxygen concentration
//           o2f = o2[5] * 256 + o2[6];     //Oxygen flow value
//           o2t = o2[7] * 256 + o2[8];     //Oxygen temperature
//         }
 
       
 
//         i = 0;                            
//         for (j = 0; j < 12; j++)           //Initialize array
//         {
//           o2[j] = 0;
//         }
//       }
//     }
//   }
// Serial.println();
// //--O2
// Serial.print("O2 : ");
// Serial.print(o2c/100);
// Serial.print(o2c/10%10);
// Serial.print(".");
// Serial.print(o2c%10);
// Serial.println("%");
 
// //--Flow
// Serial.print("Flow : ");
// Serial.print(o2f/10%10);
// Serial.print(".");
// Serial.print(o2f%10);
// Serial.println("L/min");
 
// //--Temperature
// Serial.print("Temp : ");
// Serial.print(o2t/100);
// Serial.print(o2t/10%10);
// Serial.print(".");
// Serial.print(o2t%10);
// Serial.println("Celcius");
// delay(2000);
// }


