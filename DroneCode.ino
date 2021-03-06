#include <I2Cdev.h>

#include <helper_3dmath.h>
#include <Servo.h>
#include <EEPROM.h>


/// I2Cdev and MPU6050 must be installed as libraries, or else the .cpp/.h files
// for both classes must be in the include path of your project
#include "I2Cdev.h"

#include "MPU6050_6Axis_MotionApps20.h"
#include "MPU6050.h" // not necessary if using MotionApps include file

// Arduino Wire library is required if I2Cdev I2CDEV_ARDUINO_WIRE implementation
// is used in I2Cdev.h
#if I2CDEV_IMPLEMENTATION == I2CDEV_ARDUINO_WIRE
    #include "Wire.h"
#endif



// ================================================================
// ===                        Switches                          ===
// ================================================================
//#define OUTPUT_READABLE_YAWPITCHROLL
//#define VERBOSE_SERIAL
//#define OUTPUT_YPR_DIFERENCE
#define MOTOR_SPEEDS
//#define MOTOR_SPEEDS1  //mSpeed \t ypr-targetYPR
//#define DEBUG_PD
//#define CALIBRATE


// ================================================================
// ===                      Description                       ===
// ================================================================
//Purpose of this sketch is to rotate a single DC motor at a constant speed
//then, have it speed up/slow down bassed on accl data


// ================================================================
// ===                      wiring                       ===
// ================================================================
//On the H-Bridge, tie the motor1Pin (2 on breadboard) to high and motor2Pin (7 on breadboard) to Low
//SDA is A4
//SCA is A5
const int mpuInteruptPin = 2;

const int motor0Pin = 10; //A
const int motor1Pin = 11; //B
const int motor2Pin = 6; //C
const int motor3Pin = 9; //D





#define LED_PIN 13 // (on board LED)
bool blinkState = false;


// ================================================================
// ===                      Error Codes                       ===
// ================================================================
/*
{1,1}      Failed to initialize MPU
{1,1,1} failed to initialize DMP
*/


// ================================================================
// ===                      Constants                           ===
// ================================================================
const int minThorttle = 900; //min throttle
const int maxThorttle = 2000; //max throttle

int mYPR[4][3];

const byte pastPoints = 6;
//theres an array thats float d[pastPoints][pastPoints]; grows by the square

const int maxSerialInputLength=15;  //not enforced in code, will silently overflow TODO: fix

const int armSpeed = 700;

const int hoverSpeed = 1450; //random untested value - should be where drone hovers
const int launchSpeed = 1550; //iffy tested exact testing needed -experimentally detirmed to be around here

const int minAcclValue = 0;  //experimentally detirmined
const int maxAcclValue = 100; //experimentally detirmened

const int maxYaw = 180;  
const int minYaw = -180;  

const int minPitch = -60; //TODO: find real Values
const int maxPitch = 60; //TODO: find real Values

const int minRoll = -70; //TODO: find real Values
const int maxRoll = 70; //TODO: find real Values


//TODO1:Find true value
const float PCorrectionMod[3] = {0.1, 1.0, 1.0}; //stabilization modifier (ie correction factor multiplied by this value), based on instantaenous ypr
const float DCorrectionMod[3] = {0.0, 1.0, 1.0};  //take the derivitive of ypr, this is weighting

//for test
float pCorrectionModMultiplier=1.2;
float dCorrectionModMultiplier=0.5;

const float calibrationPercision = 0.1;  //must be positive

/*
Your offsets:  -4592 -537  700 -1214 -18 0
-4550 -523  753 -1269 -23 -2
-4548 -526  751 -1267 -22 -2
Data is printed as: acelX acelY acelZ giroX giroY giroZ
*/





// class default I2C address is 0x68
// specific I2C addresses may be passed as a parameter here
// AD0 low = 0x68 (default for SparkFun breakout and InvenSense evaluation board)
// AD0 high = 0x69
MPU6050 mpu;
//MPU6050 mpu(0x69); // <-- use for AD0 high



// ================================================================
// ===                        EEPROM Addresses                  ===
// ================================================================
const int stdYawAddr = 0;
const int stdPitchAddr = 4;
const int stdRollAddr = 8;

// MPU control/status vars
bool dmpReady = false;  // set true if DMP init was successful
uint8_t mpuIntStatus;   // holds actual interrupt status byte from MPU
uint8_t devStatus;      // return status after each device operation (0 = success, !0 = error)
uint16_t packetSize;    // expected DMP packet size (default is 42 bytes)
uint16_t fifoCount;     // count of all bytes currently in FIFO
uint8_t fifoBuffer[64]; // FIFO storage buffer

// orientation/motion vars
Quaternion q;           // [w, x, y, z]         quaternion container
VectorInt16 aa;         // [x, y, z]            accel sensor measurements
VectorInt16 aaReal;     // [x, y, z]            gravity-free accel sensor measurements
VectorInt16 aaWorld;    // [x, y, z]            world-frame accel sensor measurements
VectorFloat gravity;    // [x, y, z]            gravity vector
float euler[3];         // [psi, theta, phi]    Euler angle container
float ypr[3];           // [yaw, pitch, roll]   yaw/pitch/roll container and gravity vector
float spYPR[3];       //base values when its flat, the SP
float flatYPR[3] = {-100.97,  0.20,  -0.18}; //put in EEPROM

float YPRerr[3][pastPoints];   //
float adjYPRerr[3];


bool newData = false;

String serialData;
bool newSerialData=false;

int throttle=700; //goes from 900 to 2000


Servo motor[4];
float mSpeed[4];

// ================================================================
// ===               INTERRUPT DETECTION ROUTINE                ===
// ================================================================

volatile bool mpuInterrupt = false;     // indicates whether MPU interrupt pin has gone high
void dmpDataReady() {
    mpuInterrupt = true;
}


void setup() { 
  //stupid arduino won't initialize 2D arrays
  //set motor multipliers (how a change efects motor speed). 
  int buffer[4][3] = { { 1, -1,  1}, 
                       {-1,  1,  1},
                       {-1,  1, -1},  
                       { 1, -1, -1}  }; 
           
           //fill array
    for (int x=0; x<4; x++){
     for (int y=0; y<3; y++){
       mYPR[x][y] = buffer[x][y];
     }
    }
        
        
  //actualy setup()
  serialData.reserve(maxSerialInputLength);
  Serial.begin(9600);
  setUpMPU();
  
  
  motor[0].attach(motor0Pin);    // attached to pin 
  motor[1].attach(motor1Pin);    // attached to pin 
  motor[2].attach(motor2Pin);    // attached to pin 
  motor[3].attach(motor3Pin);    // attached to pin 

  // configure LED for output
  pinMode(LED_PIN, OUTPUT);
  

  //9600 is magical. Havent tested to see if any other baud rate are not magical
  
     for(int i=0; i<4;i++) {
      motor[i].writeMicroseconds(armSpeed); //low throtle
   } 
   
   #ifdef VERBOSE_SERIAL
      Serial.println(F("Calibrating yaw pitch roll"));
   #endif
   #ifdef CALIBRATE
   //calibrate
  calibrateYPR(&flatYPR[0]);
  #else
 
        //EEPROM.get(stdYawAddr, flatYPR[0]);
        EEPROM.get(stdPitchAddr, flatYPR[1]);
        EEPROM.get(stdRollAddr, flatYPR[2]);
        getYawPitchRoll(&ypr[0]);
        flatYPR[0]=ypr[0]; //have updataed orientation
        Serial.print(F("Getting old target"));
 
  #endif

  //our target is flat
  for(int i=0; i<3;i++) {
    spYPR[i]=flatYPR[i];
  }

  
    for(int i=0; i<4;i++) {
      motor[i].writeMicroseconds(armSpeed); //low throtle
   } 
  
    // wait for ready
    Serial.println(millis()/1000.);
    Serial.println(F("Press any key to begin: "));
    digitalWrite(LED_PIN,HIGH);
    while (Serial.available() && Serial.read()); // empty buffer
    while (!Serial.available());                 // wait for data
    while (Serial.available() && Serial.read()); // empty buffer again  
    Serial.println(F("Begginiing"));
    spYPR[0]=ypr[0];
  //fill history
  for(int p=0;p<=pastPoints+3;) {
    
    newData = getYawPitchRoll(&ypr[0]);
    if(newData) {
    //shift data, and map it to -33,33
        for(int j=pastPoints-1; j>0; j--) {
           for(int i=0; i<3; i++) {
            YPRerr[i][j] = YPRerr[i][j-1];
           } 
        }
        
        for(int i=0; i<3; i++) {
           YPRerr[i][0] = ypr[i]-spYPR[i];
        }
      p++;
      spYPR[0]=ypr[0];
    }
  }    
}
 
 
void loop() {
  
    newData=getYawPitchRoll(&ypr[0]); //fill acceleration array, and return if we got new data
    
    
   
    
    
    if(newSerialData){
      inputHandler();
    }

    if(newData) {
      
      
      //shift data, and map it to -33,33
      for(int j=pastPoints-1; j>0; j--) {
         for(int i=0; i<3; i++) {
          YPRerr[i][j] = YPRerr[i][j-1];
         } 
      }
      
      for(int i=0; i<3; i++) {
         YPRerr[i][0] = ypr[i]-spYPR[i];
      }
      


      //put yaw through PD
      float axis[pastPoints];
      //no need to mess with yaw constants
      for(int i=0;i<1;i++) {
        //fill axis with all past yaws or pitches or rolls
        for (int j=0; j<pastPoints;j++) {
           axis[j] = YPRerr[i][j];
        }

        adjYPRerr[i]= getPComponent(YPRerr[i][0], PCorrectionMod[i] ) + getDComponent(&axis[0],i,pastPoints,  DCorrectionMod[i]);
      }
      //deal with pitch and roll
      for(int i=1;i<3;i++) {
        //fill axis with all past yaws or pitches or rolls
        for (int j=0; j<pastPoints;j++) {
           axis[j] = YPRerr[i][j];
        }

        adjYPRerr[i]= getPComponent(YPRerr[i][0], PCorrectionMod[i] * pCorrectionModMultiplier) + getDComponent(&axis[0],i,pastPoints,  DCorrectionMod[i]*dCorrectionModMultiplier);
      }


      mapYPR(&adjYPRerr[0]);


      
   for (int k=0; k<4;k++) {
      mSpeed[k] = combineYPR(adjYPRerr[0],  adjYPRerr[1], adjYPRerr[2],k); //from instantaneous
    }
    
    
    noInterrupts();
    spinRotor(motor[0], mSpeed[0]); //spin rotor A
    spinRotor(motor[1], mSpeed[1]); //spin rotor B
    spinRotor(motor[2], mSpeed[2]); //spin rotor C
    spinRotor(motor[3], mSpeed[3]);  //spin rotor D
    
     #ifdef OUTPUT_YPR_DIFERENCE
       for(int i=0;i<3; i++) {
         Serial.print(ypr[i]-spYPR[i]);
         Serial.print("\t");
       }   
       
    #endif
    
    #ifdef MOTOR_SPEEDS1
    
       for(int i=1;i<2; i++) {
         Serial.print((int) mSpeed[i]);
         Serial.print("\t");
       }  
       
       for(int i=2;i<3; i++) {
         Serial.print(ypr[i]-spYPR[i]);
         Serial.print("\t");
       }  
        
       
      Serial.print("\n");
    #endif
   
   #ifdef MOTOR_SPEEDS
    Serial.print(serialData);
    Serial.print("\t");
    Serial.println();
   #endif 
     #ifdef OUTPUT_YPR_DIFERENCE
    Serial.println();
   #endif 

   #ifdef DEBUG_PD
    Serial.print("\n");
   #endif 
 
    interrupts();
    }
}


//Run after loop()
void serialEvent() {
  while (Serial.available() ) {
    // get the new byte:
    char inChar = (char) Serial.read(); 

    if(';'== inChar) {
      newSerialData = true;
      break;
    } else if ('`' == inChar) {
        serialData="";
    } else {
      // add it to the inputString:
      serialData += inChar;
    }
  }
}

void inputHandler() {
  String input=getSerialData(); //GOES FROM 90 TO 200
      
      if(input[0] == 'p') { //its not an int
        
        pCorrectionModMultiplier = ( (String) input[1]).toFloat();
        Serial.println(pCorrectionModMultiplier);
        
      } else if(input[0] == 'd') { //its not an int
        
        dCorrectionModMultiplier = ( (String) input[1]).toFloat();
        Serial.println(dCorrectionModMultiplier);
        
      } else if(input[0] == 'o') { //its not an int
        
        EEPROM.get(stdYawAddr, spYPR[0]);
        EEPROM.get(stdPitchAddr, spYPR[1]);
        EEPROM.get(stdRollAddr, spYPR[2]);
        Serial.print(F("Getting old target"));
        for(int i=0; i<3;i++) {
            Serial.print(spYPR[i]);
            Serial.print("\t");
          }
        
      } else if(input[0] == 'c') { //its not an int
        
        //calibrate
      
          calibrateYPR(&spYPR[0]);
          Serial.println(F("Press any key to begin: "));
          while (Serial.available() && Serial.read()); // empty buffer
          while (!Serial.available());                 // wait for data
          while (Serial.available() && Serial.read()); // empty buffer again
          throttle=minThorttle;
          /*  
          EEPROM.put(stdYawAddr, spYPR[0]);
          EEPROM.put(stdPitchAddr, spYPR[1]);
          EEPROM.put(stdRollAddr, spYPR[2]);
          */
          for(int i=0; i<3;i++) {
            Serial.print(spYPR[i]);
            Serial.print("\t");
          }
          Serial.println();
      } else if('k' == input[0]) {
        
        
      } else if (0 != input.toInt() ) {
        throttle= input.toInt()*10;
        Serial.println(throttle);
        
        if (throttle <= 400) {
          //do nothing, probably a mistaye
        } else if (throttle > 2000) {
          //shit no man
          throttle=700;
        }
      } else {
        Serial.println(input);
      }
}

//takes up to 15 chars
String getSerialData() {
  String data = serialData;
  serialData="";
  newSerialData=false;
  return data;
  
}




float getPComponent(float angle, float kP) {
  angle = angle* kP;
  #ifdef DEBUG_PD
    Serial.print(angle);
    Serial.print("\t");
  #endif
  return angle;
}

//old speed is an array of speeds where [0] is most current and [numOfPoints-1] is oldest
//predictAhead predicts how many points ahead there are
//TODO: make it so if we have extra points in our history, get a better taylor series
float getDComponent(float* oldValues, int axis, int numOfPoints, float kD) {
  // Taylor Series slope
  

     //get the derivatives 
     float d[numOfPoints][numOfPoints];
    //fill the base
    for(int i=0;i<numOfPoints-1;i++) {
     d[0][i]=oldValues[i]; 
    }
    
    for (int i=1;i<numOfPoints;i++) {
      for(int j=0; j<numOfPoints-i; j++) {
        d[i][j]=d[i-1][j]-d[i-1][j+1]; //technically divided by 1 
      }
    }

  float slope = d[1][0];
  for(int i=2; i<numOfPoints-1 ;i++) {
   slope = slope + d[i][0] /( (float) factorial(i) ); 
  }
  
  //float slope = oldValues[1]-oldValues[0];
 float adjSlope=slope*abs(slope)*kD;  //make the sloping bits steeper
  #ifdef DEBUG_PD

    Serial.print(adjSlope);
    Serial.print("\t");
  #endif
  return adjSlope;
  
}


int factorial(int n) {
 if(n>2) {
  return n*factorial(n-1);
 } else {
   return n;
 }
}

//take the (pitch actual - pitch desired) and the (roll actual - roll desired), return speed to change
float combineYPR(float yaw, float pitch, float roll, int motor) {
  
  float cor = (yaw*mYPR[motor][0] + mYPR[motor][1]*pitch + mYPR[motor][2]*roll);

  return cor;
  
}

void mapYPR(float* YPR) {
    //adjust values to -10 to 10 (so total is -30 or 30)
   //yaw
  YPR[0] = mapFloat(YPR[0], minYaw, maxYaw, -33.0, 33.0); 
  //pitch
  YPR[1] = mapFloat(YPR[1], minPitch, maxPitch, -33.0, 33.0);
 //roll 
  YPR[2] = mapFloat(YPR[2], minRoll, maxRoll, -33.0, 33.0);
}


 
// speed change is number, give max and min to make it releative, controls motorA
int spinRotor(Servo motor, float speedChange) {
  int spedeSent = ( (int) speedChange )+throttle;
  
  #ifdef MOTOR_SPEEDS
    Serial.print(spedeSent);
    Serial.print("\t");
  #endif
  
  motor.writeMicroseconds(spedeSent);
  return spedeSent;
}


void calibrateYPR(float* stdYPR) {
  if(throttle>900) {
    Serial.println(F("Cannot calibrate midflight"));
    return; 
  }
   //kill the motors
   for(int i=0; i<4;i++) {
      motor[i].writeMicroseconds(minThorttle); //low throtle
   } 
  Serial.println("Calibrating");
  bool calibrated=false;
  int trials;
  do{
      trials=16;
     do {
       filStdYPR(&stdYPR[0]);
       delay(100);
       trials=trials/2;
     } while( testStdYPR(&stdYPR[0],trials) && trials > 4 );
   calibrated = trials <= 4;
   }while(!calibrated);
   
}

bool testStdYPR(float *YPR, int trials) {
  //just to make sure its not crazy
  if( abs(YPR[0]+YPR[1]+YPR[2]) > abs(maxYaw+maxPitch+maxRoll)) {
    Serial.println("Crazy!");
    return false;
  }
      
  int trial = trials;
  
    float yDif=0;
    float pDif=0;
    float rDif=0;
    
     while(trial>0) {

     while( !getYawPitchRoll(&ypr[0]) ){delay(10);} //fill acceleration array with a new packet
       #ifdef OUTPUT_YPR_DIFERENCE
         for(int i=0;i<3; i++) {
           Serial.print(ypr[i]-YPR[i]);
           Serial.print("\t");
         }   
         Serial.println();
      #endif
      delay(200);

       
       yDif=ypr[0]-YPR[0];
       pDif=ypr[1]-YPR[1];
       rDif=ypr[2]-YPR[2];

       
        if( (abs(yDif) < calibrationPercision) &&  (abs(pDif) < calibrationPercision) && (abs(rDif) < calibrationPercision) ) {
         trial--; 
         delay(150); //if 100, it breaks, and ypr[i]-targetYPR[i] diverges signifigantly, then stabilizes at around //think s its because its too close to the speed of the MPU6050

        } else {
          Serial.println(F("OUT OF BOUNDS"));
         return false; //this isnt a good std, break out 
        }
     }
     

     return true;
  
}

void filStdYPR(float *targetYPR) {

  //give blank slate
  targetYPR[0]=0.0;
  targetYPR[1]=0.0;
  targetYPR[2]=0.0;
  
 //average of 20 values, weighted towards the newest values
 
 int i = 0;
 float tempYPR[3];
 while(i<20) {
   //if mpu has signaled its ready or theres a packet waiting
    while(!getYawPitchRoll(&tempYPR[0])) {delay(3);}
    
    if(abs(tempYPR[0]) < 10000.0) { //no overflow. shouldnt be over 10,000.0
      targetYPR[0] = (2.0*targetYPR[0]+tempYPR[0])/3.0;  
    }
    
    if(abs(tempYPR[1]) < 10000.0) { //to stop overflow - shouldnt be over 10,000.0
      targetYPR[1] = (2.0*targetYPR[1]+tempYPR[1])/3.0;  
    } 
    
    if(abs(tempYPR[2]) < 10000.0) { //to stop overflow on the float - shouldnt be over 10,000.0
      targetYPR[2] = (2.0*targetYPR[2]+tempYPR[2])/3.0;  
    } 
    
    i++;  
 }
 return;
}



void errorMPUInitializationFailure() {
  boolean error[2] = {1,1};
  blinkErrorCode(&error[0],2);
  return;
}
void errorDMPInitializationFailure() {
  boolean error[3] = {1,1,1};
  blinkErrorCode(&error[0],3);
  return;
}


void blinkErrorCode(boolean* errorCode, int len) {
  while (true) {
    int i=0;
    while(i<len) {
      digitalWrite(LED_PIN, errorCode[i]==1 ? (HIGH):(LOW));
      delay(500);
      i++; 
      digitalWrite(LED_PIN, LOW);
      delay(200);
    }
    delay(1000);
  }
  return;
}


//0 is yaw, 1 is pitch, 2 is roll
//ypr must be a array size 3
//returns true if new packet, else returns false
bool getYawPitchRoll(float *YPR) {
// if programming failed, don't try to do anything
    if (!dmpReady) return false;

    // if MPU interrupt or extra packet(s) not available, exit function
    if (!mpuInterrupt && fifoCount < packetSize) {
      return false;
    }

    
    mpuInterrupt = false;
    mpuIntStatus = mpu.getIntStatus();

    // get current FIFO count
    fifoCount = mpu.getFIFOCount();

    // check for overflow (this should never happen unless our code is too inefficient)
    if ((mpuIntStatus & 0x10) || fifoCount == 1024) {
        // reset so we can continue cleanly
        mpu.resetFIFO();
        //Serial.println(F("FIFO overflow!"));

    // otherwise, check for DMP data ready interrupt (this should happen frequently)
    } else if (mpuIntStatus & 0x02) {
        // wait for correct available data length, should be a VERY short wait
        while (fifoCount < packetSize) fifoCount = mpu.getFIFOCount();

        // read a packet from FIFO
        mpu.getFIFOBytes(fifoBuffer, packetSize);
        
        // track FIFO count here in case there is > 1 packet available
        // (this lets us immediately read more without waiting for an interrupt)
        fifoCount -= packetSize;


            mpu.dmpGetQuaternion(&q, fifoBuffer);
            mpu.dmpGetGravity(&gravity, &q);
            mpu.dmpGetYawPitchRoll(YPR, &q, &gravity);
            YPR[0]=YPR[0] * 180/M_PI;
            YPR[1]=YPR[1] * 180/M_PI;
            YPR[2]=YPR[2] * 180/M_PI;
            
        #ifdef OUTPUT_READABLE_YAWPITCHROLL
            // display Euler angles in degrees
            

            Serial.print("ypr\t");
            Serial.print(YPR[0]);
            Serial.print("\t");
            Serial.print(YPR[1]);
            Serial.print("\t");
            Serial.println(YPR[2]);
        #endif
    }

    return true;
}

void setUpMPU() {
     // join I2C bus (I2Cdev library doesn't do this automatically)
    Wire.begin();
    TWBR = 12; // set 400kHz mode @ 16MHz CPU or 200kHz mode @ 8MHz CPU  
    #ifdef VERBOSE_SERIAL    
      Serial.println(F("Initializing I2C devices..."));
    #endif
    mpu.initialize();

    // verify connection
    #ifdef VERBOSE_SERIAL    
      Serial.println(F("Testing device connections..."));
    #endif
    if (mpu.testConnection()) {
      #ifdef VERBOSE_SERIAL    
        Serial.println(F("MPU6050 connection successful"));
      #endif
    } else {
      Serial.println(F("MPU6050 connection failed"));
      errorMPUInitializationFailure();
    }

    // load and configure the DMP
    #ifdef VERBOSE_SERIAL    
      Serial.println(F("Initializing DMP..."));
    #endif
    
    devStatus = mpu.dmpInitialize();

    // supply your own gyro offsets here, scaled for min sensitivity
    mpu.setXAccelOffset(-4537);
    mpu.setYAccelOffset(-540);
    mpu.setZAccelOffset(738); // 1688 factory default for my test chip
    mpu.setXGyroOffset(-1257);
    mpu.setYGyroOffset(-22);
    mpu.setZGyroOffset(-2);
    


    // make sure it worked (returns 0 if so)
    if (devStatus == 0) {
        // turn on the DMP, now that it's ready
        #ifdef VERBOSE_SERIAL
          Serial.println(F("Enabling DMP..."));
        #endif
        mpu.setDMPEnabled(true);

        // enable Arduino interrupt detection
        #ifdef VERBOSE_SERIAL
          Serial.println(F("Enabling interrupt detection (Arduino external interrupt 2)..."));
        #endif
        attachInterrupt(0, dmpDataReady, RISING);
        mpuIntStatus = mpu.getIntStatus();

        // set our DMP Ready flag so the main loop() function knows it's okay to use it
        dmpReady = true;

        // get expected DMP packet size for later comparison
        packetSize = mpu.dmpGetFIFOPacketSize();
    } else {
        // ERROR!
        // 1 = initial memory load failed
        // 2 = DMP configuration updates failed
        // (if it's going to break, usually the code will be 1)
        Serial.print(F("DMP Initialization failed (code "));
        Serial.print(devStatus);
        Serial.println(F(")"));
        errorDMPInitializationFailure();
       
    }
}


bool isMPUStable() {
  float YPR[3];
  while(! getYawPitchRoll(&YPR[0])){}
  delay(1500);
   int trial = 8;
  
  //accumalate the error ic case its all going in 1 direction
    float yDif=0;
    float pDif=0;
    float rDif=0;
    
     while(trial>0) {

     while( !getYawPitchRoll(&ypr[0]) ){delay(10);} //fill acceleration array with a new packet
       #ifdef OUTPUT_YPR_DIFERENCE
         for(int i=0;i<3; i++) {
           Serial.print(ypr[i]-YPR[i]);
           Serial.print("\t");
         }   
         Serial.println();
      #endif
      delay(200);

       
       yDif=ypr[0]-YPR[0];
       pDif=yDif+ypr[1]-YPR[1];
       rDif=yDif+ ypr[2]-YPR[2];

       
        if( (abs(yDif)/(float)trial < 10) &&  (abs(pDif)/trial < 10) && (abs(rDif)/trial < 10) ) {
         trial--; 
         delay(150); //if 100, it breaks, and ypr[i]-targetYPR[i] diverges signifigantly, then stabilizes at around //think s its because its too close to the speed of the MPU6050

        } else {
          Serial.print(yDif);
         return false; //this isnt a good std, break out 
        }
     }
     
     
     Serial.print("\n \n");

     return true;
}

float mapFloat(float x, float in_min, float in_max, float out_min, float out_max)
{
 return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

