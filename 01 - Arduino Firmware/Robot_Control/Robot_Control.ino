#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>
#include <SoftwareSerial.h>

// --- Sensor Pins ---
const int leftTrigPin  = 7;
const int leftEchoPin  = 6;
const int rightTrigPin = 4;
const int rightEchoPin = 5;

// --- Variables ---
long leftDistance = 0;
long rightDistance = 0;

Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver();
SoftwareSerial BTSerial(2, 3);  // RX, TX

SoftwareSerial ESPSerial(8, 9);

// Servo port mapping (your wiring)
const int servoPorts[13] = {
 0, 1, 2,       // FL: Abductor, Hip, Knee
 3, 4, 5,       // BL: Abductor, Hip, Knee
 15, 14, 13,    // FR: Abductor, Hip, Knee
 12, 11, 10,    // BR: Abductor, Hip, Knee
 7   // Neck
};

// Min/max PWM tick values
int servoMins[13] = {
 160, 110, 120,  // Knee inward
 160, 110, 312,
 160, 85, 115,
 160, 90, 280,   // Knee inward
 150
};

int servoMaxs[13] = {
 480, 565, 290,
 490, 542, 510,  // Knee inward
 510, 540, 337,  // Knee inward
 490, 543, 490,
 450
};

char currentCommand = '0';
char previousCommand = '0';

bool alternateMode = false;
bool alternateMode2 = false; 
bool controlPosition = false;
bool objectDetected = false;
bool followModeActive = false;
bool walkPrepDone = false;
bool turnPrepDone = false;
bool gestureModeActive = false;

char lastWalkCommand = '0';  // tracks last walk button pressed
char lastTurnCommand = '0';  // tracks last turn button pressed

// Prep flags
bool walkPrepNeeded = true;
bool turnPrepNeeded = true;

// Current servo angles tracker
int currentAngle[13] = {90, 110, 90, 90, 70, 90, 90, 70, 90, 90, 110, 90, 90};
// Smoothed positions
float smoothedAngle[13] = {90, 110, 90, 90, 70, 90, 90, 70, 90, 90, 110, 90, 90};

int degreeToTick(int index, int degree) {
 degree = constrain(degree, 0, 180);
 return map(degree, 0, 180, servoMins[index], servoMaxs[index]);
}

void moveServo(int index, float angle) {
 int pulse = degreeToTick(index, angle);
 pwm.setPWM(servoPorts[index], 0, pulse);
}

void moveServoSmooth(int index, int targetAngle, int duration = 300) {
 int start = currentAngle[index];
 int steps = max(10, duration / 15);  // More steps for longer duration (adaptive)
  for (int i = 0; i <= steps; i++) {
   float t = (float)i / steps;
   float curve = (t < 0.5) ? 8 * pow(t, 4) : 1 - pow(-2 * t + 2, 4) / 2;
   int intermediateAngle = start + (targetAngle - start) * curve;
   moveServo(index, intermediateAngle);
   delay(duration / steps);
 }

 currentAngle[index] = targetAngle;
}

// Smoothly move two servos together over a duration (ms)
void moveTwoServosSmooth(int servoIndex1, int targetAngle1, int servoIndex2, int targetAngle2, int duration = 1000) {
 int startAngle1 = currentAngle[servoIndex1];
 int startAngle2 = currentAngle[servoIndex2];
 int steps = 20; // Higher for smoother motion

 for (int i = 0; i <= steps; i++) {
   float t = (float)i / steps;
   int intermediateAngle1 = startAngle1 + (targetAngle1 - startAngle1) * t;
   int intermediateAngle2 = startAngle2 + (targetAngle2 - startAngle2) * t;

   moveServo(servoIndex1, intermediateAngle1);
   moveServo(servoIndex2, intermediateAngle2);

   delay(duration / steps);
 }

 currentAngle[servoIndex1] = targetAngle1;
 currentAngle[servoIndex2] = targetAngle2;
}

// Smoothly move four servos together over a duration (ms)
void moveFourServosSmooth(
 int servoIndex1, int targetAngle1,
 int servoIndex2, int targetAngle2,
 int servoIndex3, int targetAngle3,
 int servoIndex4, int targetAngle4,
 int duration = 1000
) {
 int startAngle1 = currentAngle[servoIndex1];
 int startAngle2 = currentAngle[servoIndex2];
 int startAngle3 = currentAngle[servoIndex3];
 int startAngle4 = currentAngle[servoIndex4];
 int steps = 20; // Higher = smoother

 for (int i = 0; i <= steps; i++) {
   float t = (float)i / steps;
   int angle1 = startAngle1 + (targetAngle1 - startAngle1) * t;
   int angle2 = startAngle2 + (targetAngle2 - startAngle2) * t;
   int angle3 = startAngle3 + (targetAngle3 - startAngle3) * t;
   int angle4 = startAngle4 + (targetAngle4 - startAngle4) * t;

   moveServo(servoIndex1, angle1);
   moveServo(servoIndex2, angle2);
   moveServo(servoIndex3, angle3);
   moveServo(servoIndex4, angle4);


   delay(duration / steps);
 }

 currentAngle[servoIndex1] = targetAngle1;
 currentAngle[servoIndex2] = targetAngle2;
 currentAngle[servoIndex3] = targetAngle3;
 currentAngle[servoIndex4] = targetAngle4;
}

// Smoothly move four servos together over a duration (ms)
void moveFourServosSmooth2(
 int servoIndex1, int targetAngle1, int duration1,
 int servoIndex2, int targetAngle2, int duration2,
 int servoIndex3, int targetAngle3, int duration3,
 int servoIndex4, int targetAngle4, int duration4
) {
 int startAngle1 = currentAngle[servoIndex1];
 int startAngle2 = currentAngle[servoIndex2];
 int startAngle3 = currentAngle[servoIndex3];
 int startAngle4 = currentAngle[servoIndex4];

 // Find the servo with the longest duration so loop can cover all
 int maxDuration = max(max(duration1, duration2), max(duration3, duration4));
 int steps = 20; // Adjust for smoothness
 int totalSteps = max(10, maxDuration / (maxDuration / steps));

 for (int i = 0; i <= totalSteps; i++) {
   unsigned long elapsed = (unsigned long)i * (maxDuration / totalSteps);

   // Servo 1
   if (elapsed <= duration1) {
     float t1 = (float)elapsed / duration1;
     int angle1 = startAngle1 + (targetAngle1 - startAngle1) * t1;
     moveServo(servoIndex1, angle1);
   }

   // Servo 2
   if (elapsed <= duration2) {
     float t2 = (float)elapsed / duration2;
     int angle2 = startAngle2 + (targetAngle2 - startAngle2) * t2;
     moveServo(servoIndex2, angle2);
   }

   // Servo 3
   if (elapsed <= duration3) {
     float t3 = (float)elapsed / duration3;
     int angle3 = startAngle3 + (targetAngle3 - startAngle3) * t3;
     moveServo(servoIndex3, angle3);
   }

   // Servo 4
   if (elapsed <= duration4) {
     float t4 = (float)elapsed / duration4;
     int angle4 = startAngle4 + (targetAngle4 - startAngle4) * t4;
     moveServo(servoIndex4, angle4);
   }

   delay(maxDuration / totalSteps);
 }

 // Save final angles
 currentAngle[servoIndex1] = targetAngle1;
 currentAngle[servoIndex2] = targetAngle2;
 currentAngle[servoIndex3] = targetAngle3;
 currentAngle[servoIndex4] = targetAngle4;
}

// Smoothly move six servos together over a duration (ms)
void moveSixServosSmooth(
 int servoIndex1, int targetAngle1,
 int servoIndex2, int targetAngle2,
 int servoIndex3, int targetAngle3,
 int servoIndex4, int targetAngle4,
 int servoIndex5, int targetAngle5,
 int servoIndex6, int targetAngle6,
 int duration = 1000
) {
 int startAngle1 = currentAngle[servoIndex1];
 int startAngle2 = currentAngle[servoIndex2];
 int startAngle3 = currentAngle[servoIndex3];
 int startAngle4 = currentAngle[servoIndex4];
 int startAngle5 = currentAngle[servoIndex5];
 int startAngle6 = currentAngle[servoIndex6];
 int steps = 20; // Higher = smoother

 for (int i = 0; i <= steps; i++) {
   float t = (float)i / steps;
   int angle1 = startAngle1 + (targetAngle1 - startAngle1) * t;
   int angle2 = startAngle2 + (targetAngle2 - startAngle2) * t;
   int angle3 = startAngle3 + (targetAngle3 - startAngle3) * t;
   int angle4 = startAngle4 + (targetAngle4 - startAngle4) * t;
   int angle5 = startAngle5 + (targetAngle5 - startAngle5) * t;
   int angle6 = startAngle6 + (targetAngle6 - startAngle6) * t;

   moveServo(servoIndex1, angle1);
   moveServo(servoIndex2, angle2);
   moveServo(servoIndex3, angle3);
   moveServo(servoIndex4, angle4);
   moveServo(servoIndex5, angle5);
   moveServo(servoIndex6, angle6);

   delay(duration / steps);
 }

 currentAngle[servoIndex1] = targetAngle1;
 currentAngle[servoIndex2] = targetAngle2;
 currentAngle[servoIndex3] = targetAngle3;
 currentAngle[servoIndex4] = targetAngle4;
 currentAngle[servoIndex5] = targetAngle5;
 currentAngle[servoIndex6] = targetAngle6;
}

// Smoothly move six servos together over a duration (ms)
void moveSixServosSmooth2(
 int servoIndex1, int targetAngle1, int duration1,
 int servoIndex2, int targetAngle2, int duration2,
 int servoIndex3, int targetAngle3, int duration3,
 int servoIndex4, int targetAngle4, int duration4,
 int servoIndex5, int targetAngle5, int duration5,
 int servoIndex6, int targetAngle6, int duration6
) {
 int startAngle1 = currentAngle[servoIndex1];
 int startAngle2 = currentAngle[servoIndex2];
 int startAngle3 = currentAngle[servoIndex3];
 int startAngle4 = currentAngle[servoIndex4];
 int startAngle5 = currentAngle[servoIndex5];
 int startAngle6 = currentAngle[servoIndex6];

 // Find the servo with the longest duration
 int maxDuration = max(max(max(duration1, duration2), max(duration3, duration4)), max(duration5, duration6));

 int steps = 20; // Higher = smoother
 int totalSteps = max(10, steps);

 for (int i = 0; i <= totalSteps; i++) {
   unsigned long elapsed = (unsigned long)i * (maxDuration / totalSteps);

   if (elapsed <= duration1) {
     float t1 = (float)elapsed / duration1;
     int angle1 = startAngle1 + (targetAngle1 - startAngle1) * t1;
     moveServo(servoIndex1, angle1);
   }
   if (elapsed <= duration2) {
     float t2 = (float)elapsed / duration2;
     int angle2 = startAngle2 + (targetAngle2 - startAngle2) * t2;
     moveServo(servoIndex2, angle2);
   }
   if (elapsed <= duration3) {
     float t3 = (float)elapsed / duration3;
     int angle3 = startAngle3 + (targetAngle3 - startAngle3) * t3;
     moveServo(servoIndex3, angle3);
   }
   if (elapsed <= duration4) {
     float t4 = (float)elapsed / duration4;
     int angle4 = startAngle4 + (targetAngle4 - startAngle4) * t4;
     moveServo(servoIndex4, angle4);
   }
   if (elapsed <= duration5) {
     float t5 = (float)elapsed / duration5;
     int angle5 = startAngle5 + (targetAngle5 - startAngle5) * t5;
     moveServo(servoIndex5, angle5);
   }
   if (elapsed <= duration6) {
     float t6 = (float)elapsed / duration6;
     int angle6 = startAngle6 + (targetAngle6 - startAngle6) * t6;
     moveServo(servoIndex6, angle6);
   }

   delay(maxDuration / totalSteps);
 }

 // Save final angles
 currentAngle[servoIndex1] = targetAngle1;
 currentAngle[servoIndex2] = targetAngle2;
 currentAngle[servoIndex3] = targetAngle3;
 currentAngle[servoIndex4] = targetAngle4;
 currentAngle[servoIndex5] = targetAngle5;
 currentAngle[servoIndex6] = targetAngle6;
}

// Smoothly move eight servos together over a duration (ms)
void moveEightServosSmooth(
 int servoIndex1, int targetAngle1,
 int servoIndex2, int targetAngle2,
 int servoIndex3, int targetAngle3,
 int servoIndex4, int targetAngle4,
 int servoIndex5, int targetAngle5,
 int servoIndex6, int targetAngle6,
 int servoIndex7, int targetAngle7,
 int servoIndex8, int targetAngle8,
 int duration = 1000
) {
 int startAngle1 = currentAngle[servoIndex1];
 int startAngle2 = currentAngle[servoIndex2];
 int startAngle3 = currentAngle[servoIndex3];
 int startAngle4 = currentAngle[servoIndex4];
 int startAngle5 = currentAngle[servoIndex5];
 int startAngle6 = currentAngle[servoIndex6];
 int startAngle7 = currentAngle[servoIndex7];
 int startAngle8 = currentAngle[servoIndex8];
 int steps = 20; // Higher = smoother

 for (int i = 0; i <= steps; i++) {
   float t = (float)i / steps;

   int angle1 = startAngle1 + (targetAngle1 - startAngle1) * t;
   int angle2 = startAngle2 + (targetAngle2 - startAngle2) * t;
   int angle3 = startAngle3 + (targetAngle3 - startAngle3) * t;
   int angle4 = startAngle4 + (targetAngle4 - startAngle4) * t;
   int angle5 = startAngle5 + (targetAngle5 - startAngle5) * t;
   int angle6 = startAngle6 + (targetAngle6 - startAngle6) * t;
   int angle7 = startAngle7 + (targetAngle7 - startAngle7) * t;
   int angle8 = startAngle8 + (targetAngle8 - startAngle8) * t;

   moveServo(servoIndex1, angle1);
   moveServo(servoIndex2, angle2);
   moveServo(servoIndex3, angle3);
   moveServo(servoIndex4, angle4);
   moveServo(servoIndex5, angle5);
   moveServo(servoIndex6, angle6);
   moveServo(servoIndex7, angle7);
   moveServo(servoIndex8, angle8);

   delay(duration / steps);
 }

 currentAngle[servoIndex1] = targetAngle1;
 currentAngle[servoIndex2] = targetAngle2;
 currentAngle[servoIndex3] = targetAngle3;
 currentAngle[servoIndex4] = targetAngle4;
 currentAngle[servoIndex5] = targetAngle5;
 currentAngle[servoIndex6] = targetAngle6;
 currentAngle[servoIndex7] = targetAngle7;
 currentAngle[servoIndex8] = targetAngle8;
}

// Smoothly move twelve servos together over a duration (ms)
void moveTwelveServosSmooth(
 int servoIndex1, int targetAngle1,
 int servoIndex2, int targetAngle2,
 int servoIndex3, int targetAngle3,
 int servoIndex4, int targetAngle4,
 int servoIndex5, int targetAngle5,
 int servoIndex6, int targetAngle6,
 int servoIndex7, int targetAngle7,
 int servoIndex8, int targetAngle8,
 int servoIndex9, int targetAngle9,
 int servoIndex10, int targetAngle10,
 int servoIndex11, int targetAngle11,
 int servoIndex12, int targetAngle12,
 int duration = 1000
) {
 int startAngle1 = currentAngle[servoIndex1];
 int startAngle2 = currentAngle[servoIndex2];
 int startAngle3 = currentAngle[servoIndex3];
 int startAngle4 = currentAngle[servoIndex4];
 int startAngle5 = currentAngle[servoIndex5];
 int startAngle6 = currentAngle[servoIndex6];
 int startAngle7 = currentAngle[servoIndex7];
 int startAngle8 = currentAngle[servoIndex8];
 int startAngle9 = currentAngle[servoIndex9];
 int startAngle10 = currentAngle[servoIndex10];
 int startAngle11 = currentAngle[servoIndex11];
 int startAngle12 = currentAngle[servoIndex12];

 int steps = 20; // Higher = smoother

 for (int i = 0; i <= steps; i++) {
   float t = (float)i / steps;

   int angle1 = startAngle1 + (targetAngle1 - startAngle1) * t;
   int angle2 = startAngle2 + (targetAngle2 - startAngle2) * t;
   int angle3 = startAngle3 + (targetAngle3 - startAngle3) * t;
   int angle4 = startAngle4 + (targetAngle4 - startAngle4) * t;
   int angle5 = startAngle5 + (targetAngle5 - startAngle5) * t;
   int angle6 = startAngle6 + (targetAngle6 - startAngle6) * t;
   int angle7 = startAngle7 + (targetAngle7 - startAngle7) * t;
   int angle8 = startAngle8 + (targetAngle8 - startAngle8) * t;
   int angle9 = startAngle9 + (targetAngle9 - startAngle9) * t;
   int angle10 = startAngle10 + (targetAngle10 - startAngle10) * t;
   int angle11 = startAngle11 + (targetAngle11 - startAngle11) * t;
   int angle12 = startAngle12 + (targetAngle12 - startAngle12) * t;

   moveServo(servoIndex1, angle1);
   moveServo(servoIndex2, angle2);
   moveServo(servoIndex3, angle3);
   moveServo(servoIndex4, angle4);
   moveServo(servoIndex5, angle5);
   moveServo(servoIndex6, angle6);
   moveServo(servoIndex7, angle7);
   moveServo(servoIndex8, angle8);
   moveServo(servoIndex9, angle9);
   moveServo(servoIndex10, angle10);
   moveServo(servoIndex11, angle11);
   moveServo(servoIndex12, angle12);

   delay(duration / steps);
 }

 // Save new angles as current
 currentAngle[servoIndex1] = targetAngle1;
 currentAngle[servoIndex2] = targetAngle2;
 currentAngle[servoIndex3] = targetAngle3;
 currentAngle[servoIndex4] = targetAngle4;
 currentAngle[servoIndex5] = targetAngle5;
 currentAngle[servoIndex6] = targetAngle6;
 currentAngle[servoIndex7] = targetAngle7;
 currentAngle[servoIndex8] = targetAngle8;
 currentAngle[servoIndex9] = targetAngle9;
 currentAngle[servoIndex10] = targetAngle10;
 currentAngle[servoIndex11] = targetAngle11;
 currentAngle[servoIndex12] = targetAngle12;
}

void walkPrep() {
 // Small caving-in / initial position for walking
 moveFourServosSmooth( // Move FL and BR legs up
     1, 122,    // FL Hip
     2, 50,     // FL Knee
     10, 122,   // BR Hip
     11, 50,    // BR Knee
     80         // Duration
   );
  
   moveSixServosSmooth(
     0, 98,     // FL Abductor
     1, 110,    // FL Hip
     2, 90,     // FL Knee
     9, 98,     // BR Abductor
     10, 110,   // BR Hip
     11, 90,    // BR Knee
     120        // Duration
   );
  
   moveFourServosSmooth( // Move FR and BL legs up
     4, 58,    // BL Hip
     5, 130,   // BL Knee
     7, 58,    // FR Hip
     8, 130,   // FR Knee
     80        // Duration
   );
  
   moveSixServosSmooth(
     3, 82,    // FL Abductor
     4, 70,    // FL Hip
     5, 90,    // FL Knee
     6, 82,    // BR Abductor
     7, 70,    // BR Hip
     8, 90,    // BR Knee
     120       // Duration
   );
  delay(50);
}

void turnPrep() {
 // Initial position for turning
 moveFourServosSmooth( // Move FL and BR legs up
     1, 122,    // FL Hip
     2, 50,     // FL Knee
     10, 122,   // BR Hip
     11, 50,    // BR Knee
     80         // Duration
   );
  
   moveSixServosSmooth(
     0, 90,     // FL Abductor
     1, 110,    // FL Hip
     2, 90,     // FL Knee
     9, 90,     // BR Abductor
     10, 110,   // BR Hip
     11, 90,    // BR Knee
     120        // Duration
   );
  
   moveFourServosSmooth( // Move FR and BL legs up
     4, 58,    // BL Hip
     5, 130,   // BL Knee
     7, 58,    // FR Hip
     8, 130,   // FR Knee
     80        // Duration
   );
  
   moveSixServosSmooth(
     3, 90,    // FL Abductor
     4, 70,    // FL Hip
     5, 90,    // FL Knee
     6, 90,    // BR Abductor
     7, 70,    // BR Hip
     8, 90,    // BR Knee
     120       // Duration
   );
}

void walkLoop(char dir) {
 if (dir == 'E') walkForward();
 else if (dir == 'G') walkBackward();
}

void turnLoop(char dir) {
 if (dir == 'F') turnRight();
 else if (dir == 'H') turnLeft();
}

void walkForward() {
 // Main repeating gait sequence
 moveFourServosSmooth( // Move FL and BR legs up
   1, 130,    // FL Hip
   2, 50,     // FL Knee
   10, 130,   // BR Hip
   11, 50,    // BR Knee
   80         // Duration
 );

 moveFourServosSmooth2( // Move FL and BR legs down and forward
   1, 102, 80,    // FL Hip
   2, 100, 160,   // FL Knee
   10, 116, 80,   // BR Hip
   11, 100, 160   // BR Knee
 );

 moveEightServosSmooth( // Move body forward
   1, 112,    // FL Hip
   2, 90,     // FL Knee
   4, 76,     // BL Hip
   5, 90,     // BL Knee
   7, 62,     // FR Hip
   8, 90,     // FR Knee
   10, 110,   // BR Hip
   11, 90,    // BR Knee
   80         // Duration
 );

 moveFourServosSmooth( // Move FR and BL legs up
   4, 50,    // BL Hip
   5, 130,   // BL Knee
   7, 50,    // FR Hip
   8, 130,   // FR Knee
   80        // Duration
 );

 moveFourServosSmooth2( // Move FR and BL legs down and forward
   4, 64, 80,    // BL Hip
   5, 76, 160,   // BL Knee
   7, 78, 80,    // FR Hip
   8, 84, 160    // FR Knee
 );

 moveEightServosSmooth( // Move body forward
   1, 118,    // FL Hip
   2, 90,     // FL Knee
   4, 70,     // BL Hip
   5, 90,     // BL Knee
   7, 68,     // FR Hip
   8, 90,     // FR Knee
   10, 104,   // BR Hip
   11, 90,    // BR Knee
   80         // Duration
 );

 delay(10);
}

void walkBackward() {
 // Main repeating gait sequence
 moveFourServosSmooth( // Move FL and BR legs up
   1, 130,    // FL Hip
   2, 50,     // FL Knee
   10, 130,   // BR Hip
   11, 50,    // BR Knee
   80         // Duration
 );

 moveFourServosSmooth2( // Move FL and BR legs down and forward
   1, 116, 80,     // FL Hip
   2, 95, 160,     // FL Knee
   10, 102, 80,    // BR Hip
   11, 105, 160    // BR Knee
 );

 moveEightServosSmooth( // Move body forward
   1, 110,    // FL Hip
   2, 90,     // FL Knee
   4, 66,     // BL Hip
   5, 90,     // BL Knee
   7, 72,     // FR Hip
   8, 90,     // FR Knee
   10, 112,   // BR Hip
   11, 90,    // BR Knee
   80         // Duration
 );

 moveFourServosSmooth( // Move FR and BL legs up
   4, 50,    // BL Hip
   5, 130,   // BL Knee
   7, 50,    // FR Hip
   8, 130,   // FR Knee
   80        // Duration
 );

 moveFourServosSmooth2( // Move FR and BL legs down and forward
   4, 78, 80,    // BL Hip
   5, 74, 160,   // BL Knee
   7, 64, 80,    // FR Hip
   8, 86, 160    // FR Knee
 );

 moveEightServosSmooth( // Move body forward
   1, 108,    // FL Hip
   2, 90,     // FL Knee
   4, 68,     // BL Hip
   5, 90,     // BL Knee
   7, 70,     // FR Hip
   8, 90,     // FR Knee
   10, 114,   // BR Hip
   11, 90,    // BR Knee
   80         // Duration
 );

 delay(10);
}

void turnRight() {
 // Main repeating gait sequence
 moveFourServosSmooth( // Turn Body Right
   0, 80,    // FL Hip
   3, 80,    // BL Hip
   6, 80,    // BR Hip
   9, 80,    // FR Hip
   80        // Duration
 );

 moveFourServosSmooth( // Move FL and BR legs up
   1, 130,    // FL Hip
   2, 50,     // FL Knee
   10, 130,   // BR Hip
   11, 50,    // BR Knee
   80         // Duration
 );

 moveSixServosSmooth2( // Move FL and BR legs down in standPose position
   0, 90, 80,     // FL Abductor
   1, 110, 80,    // FL Hip
   2, 90, 160,    // FL Knee
   9, 90, 80,     // BR Abductor
   10, 110, 80,   // BR Hip
   11, 90, 160    // BR Knee
 );

 moveFourServosSmooth( // Move FR and BL legs up
   4, 50,    // BL Hip
   5, 130,   // BL Knee
   7, 50,    // FR Hip
   8, 130,   // FR Knee
   80        // Duration
 );

 moveSixServosSmooth2( // Move FR and BL legs down in standPose position
   3, 90, 80,    // BL Abductor
   4, 70, 80,    // BL Hip
   5, 90, 160,   // BL Knee
   6, 90, 80,    // FR Abductor
   7, 70, 80,    // FR Hip
   8, 90, 160    // FR Knee
 );

 delay(10);
}

void turnLeft() {
 // Main repeating gait sequence
 moveFourServosSmooth( // Turn Body Left
   0, 100,   // FL Hip
   3, 100,   // BL Hip
   6, 100,   // BR Hip
   9, 100,   // FR Hip
   80        // Duration
 );

 moveFourServosSmooth( // Move FR and BL legs up
   4, 50,    // BL Hip
   5, 130,   // BL Knee
   7, 50,    // FR Hip
   8, 130,   // FR Knee
   80        // Duration
 );

 moveSixServosSmooth2( // Move FR and BL legs down in standPose position
   3, 90, 80,    // BL Abductor
   4, 70, 80,    // BL Hip
   5, 85, 160,   // BL Knee
   6, 90, 80,    // FR Abductor
   7, 70, 80,    // FR Hip
   8, 95, 160    // FR Knee
 );

 moveFourServosSmooth( // Move FL and BR legs up
   1, 130,    // FL Hip
   2, 50,     // FL Knee
   10, 130,   // BR Hip
   11, 50,    // BR Knee
   80         // Duration
 );

 moveSixServosSmooth2( // Move FL and BR legs down in standPose position
   0, 90, 80,    // FL Abductor
   1, 110, 80,   // FL Hip
   2, 90, 160,   // FL Knee
   9, 90, 80,    // BR Abductor
   10, 110, 80,  // BR Hip
   11, 90, 160   // BR Knee
 );

 delay(10);
}

void runControlSequence() {
 Serial.println("Running controlPosition sequence...");
  standPose2();

 delay(50);
}

void runAlternateSequence() {
 Serial.println("Running alternateMode sequence...");
  standPose2();

 delay(50);
}

void runAlternateSequence2() {
 Serial.println("Running alternateMode2 sequence...");
  standPose2();

 delay(50);
}

void raiseBody() {
 moveFourServosSmooth(
   0, 90,   // FL Abductor
   3, 90,   // BL Abductor
   6, 90,   // FR Abductor
   9, 90,   // BR Abductor
   500      // Duration
 );

 moveEightServosSmooth(
   1, 100,   // FL Hip
   2, 130,   // FL Knee
   4, 80,    // BL Hip
   5, 60,    // BL Knee
   7, 80,    // FR Hip
   8, 60,    // FR Knee
   10, 100,  // BR Hip
   11, 120,  // BR Knee
   1000      // Duration
 );

 delay(50);
}

void lowerBody() {
 moveFourServosSmooth(
   0, 90,   // FL Abductor
   3, 90,   // BL Abductor
   6, 90,   // FR Abductor
   9, 90,   // BR Abductor
   500      // Duration
 );

 moveEightServosSmooth(
   1, 124,   // FL Hip
   2, 10,    // FL Knee
   4, 56,    // BL Hip
   5, 170,   // BL Knee
   7, 56,    // FR Hip
   8, 170,   // FR Knee
   10, 124,  // BR Hip
   11, 10,   // BR Knee
   1000      // Duration
 );

 delay(50);
}

void standUp() {
   moveFourServosSmooth(
     0, 30,    // FL Abductor
     3, 150,   // BL Abductor
     6, 150,   // FR Abductor
     9, 30,    // BR Abductor
     1000      // Duration
   );

   moveFourServosSmooth(
     1, 124,   // FL Hip
     4, 56,    // BL Hip
     7, 56,    // FR Hip
     10, 124,  // BR Hip
     1200      // Duration
   );

   moveFourServosSmooth(
     0, 90,    // FL Abductor
     3, 90,    // BL Abductor
     6, 90,    // FR Abductor
     9, 90,    // BR Abductor
     1200      // Duration
   );

 standPose2();

 delay(50);
}

void restPose() {
   standPose();

   moveEightServosSmooth(
     1, 124,   // FL Hip
     2, 5,     // FL Knee
     4, 56,    // BL Hip
     5, 175,   // BL Knee
     7, 56,    // FR Hip
     8, 175,   // FR Knee
     10, 124,  // BR Hip
     11, 5,    // BR Knee
     1000      // Duration
   );

   moveFourServosSmooth(
     0, 30,    // FL Abductor
     3, 150,   // BL Abductor
     6, 150,   // FR Abductor
     9, 30,    // BR Abductor
     1200      // Duration
   );

   moveFourServosSmooth(
     1, 145,   // FL Hip
     4, 34,    // BL Hip
     7, 34,    // FR Hip
     10, 146,  // BR Hip
     1200      // Duration
   );

   moveFourServosSmooth(
     0, 10,    // FL Abductor
     3, 170,   // BL Abductor
     6, 170,   // FR Abductor
     9, 10,    // BR Abductor
     1000      // Duration
   );

  delay(50);
}

void sitDown() {
 moveTwelveServosSmooth(
   0, 90,    // FL Abductor
   1, 100,   // FL Hip
   2, 150,   // FL Knee
   3, 90,    // BL Abductor
   4, 56,    // BL Hip
   5, 170,   // BL Knee
   6, 90,    // FR Abductor
   7, 80,    // FR Hip
   8, 40,    // FR Knee
   9, 90,    // BR Abductor
   10, 124,  // BR Hip
   11, 10,   // BR Knee
   1000      // Duration
 );

 delay(50);
}

void handShake() {
   moveEightServosSmooth(
     1, 100,   // FL Hip
     2, 150,   // FL Knee
     4, 56,    // BL Hip
     5, 170,   // BL Knee
     7, 80,    // FR Hip
     8, 40,    // FR Knee
     10, 124,  // BR Hip
     11, 10,   // BR Knee
     1000      // Duration
   );

   delay(50);

   moveFourServosSmooth(
     0, 110,   // FL Hip
     3, 70,    // BL Hip
     6, 110,   // BR Hip
     9, 70,    // FR Hip
     800       // Duration
   );

   // Lift FR foot: move hip backward and bend knee to lift foot
   moveTwoServosSmooth(7, 60, 8, 130, 400);  // Hip backward, knee bends (foot lifts)
    moveTwoServosSmooth(7, 130, 8, 80, 800);  // Hip forward, knee forward

 delay(50);
}

void headTiltRight() {
 moveTwelveServosSmooth(
   0, 100,   // FL Abductor
   1, 100,   // FL Hip
   2, 130,   // FL Knee
   3, 80,    // BL Abductor
   4, 80,    // BL Hip
   5, 60,    // BL Knee
   6, 100,   // FR Abductor
   7, 60,    // FR Hip
   8, 120,   // FR Knee
   9, 80,    // BR Abductor
   10, 120,  // BR Hip
   11, 60,   // BR Knee
   1000      // Duration
 );
}

void headTiltLeft() {
 moveTwelveServosSmooth(
   0, 80,    // FL Abductor
   1, 120,   // FL Hip
   2, 50,    // FL Knee
   3, 100,   // BL Abductor
   4, 60,    // BL Hip
   5, 120,   // BL Knee
   6, 80,    // FR Abductor
   7, 80,    // FR Hip
   8, 60,    // FR Knee
   9, 100,   // BR Abductor
   10, 100,  // BR Hip
   11, 120,  // BR Knee
   1000      // Duration
 );
}

void followMode() {
 standPose();

 delay(50);
}

void kickLeg() {
 moveTwelveServosSmooth(
     0, 90,    // FL Abductor
     1, 110,   // FL Hip
     2, 90,    // FL Knee
     3, 90,    // BL Abductor
     4, 70,    // BL Hip
     5, 90,    // BL Knee
     6, 90,    // FR Abductor
     7, 70,    // FR Hip
     8, 90,    // FR Knee
     9, 90,    // BR Abductor
     10, 110,  // BR Hip
     11, 90,   // BR Knee
     1000      // Duration
   );

 moveFourServosSmooth(
   0, 110,   // FL Abductor
   3, 70,    // BL Abductor
   6, 110,   // FR Abductor
   9, 70,    // BR Abductor
   600       // Duration
 );

 // Lift FR foot
 moveTwoServosSmooth(7, 58, 8, 130, 100);  // Hip backward, knee bends (foot lifts)

 // Kick with FR leg
 moveTwoServosSmooth(7, 90, 8, 110, 150);  // Hip forward, knee extends

 moveTwoServosSmooth(7, 58, 8, 130, 400);

 moveTwoServosSmooth(7, 70, 8, 90, 100);
  moveFourServosSmooth(
   0, 90,    // FL Abductor
   3, 90,    // BL Abductor
   6, 90,    // FR Abductor
   9, 90,    // BR Abductor
   500       // Duration
 );

 delay(50);
}

void stretchPose() {
 moveTwelveServosSmooth(
   0, 90,     // FL Abductor
   1, 124,    // FL Hip
   2, 10,     // FL Knee
   3, 90,     // BL Abductor
   4, 80,     // BL Hip
   5, 60,     // BL Knee
   6, 90,     // FR Abductor
   7, 56,     // FR Hip
   8, 170,    // FR Knee
   9, 90,     // BR Abductor
   10, 100,   // BR Hip
   11, 120,   // BR Knee
   1500       // Duration
 );

 moveEightServosSmooth(
   1, 104,   // FL Hip
   2, 10,    // FL Knee
   4, 60,    // BL Hip
   5, 60,    // BL Knee
   7, 76,    // FR Hip
   8, 170,   // FR Knee
   10, 120,  // BR Hip
   11, 120,  // BR Knee
   1000      // Duration
 );

 delay(50);
}

void standPose() {  
   moveFourServosSmooth( // Move FL and BR legs up
     1, 122,    // FL Hip
     2, 50,     // FL Knee
     10, 122,   // BR Hip
     11, 50,    // BR Knee
     80         // Duration
   );
  
   moveSixServosSmooth2(
     0, 90, 80,      // FL Abductor
     1, 110, 80,     // FL Hip
     2, 90, 160,     // FL Knee
     9, 90, 80,      // BR Abductor
     10, 110, 80,    // BR Hip
     11, 90, 160     // BR Knee
   );
  
   moveFourServosSmooth( // Move FR and BL legs up
     4, 58,    // BL Hip
     5, 130,   // BL Knee
     7, 58,    // FR Hip
     8, 130,   // FR Knee
     80        // Duration
   );
  
   moveSixServosSmooth2(
     3, 90, 80,    // FL Abductor
     4, 70, 80,    // FL Hip
     5, 90, 160,   // FL Knee
     6, 90, 80,    // BR Abductor
     7, 70, 80,    // BR Hip
     8, 90, 160    // BR Knee
   );

   moveServoSmooth(12, 90, 2000);

 delay(50);
}

void standPose2() {
   moveTwelveServosSmooth(
     0, 90,    // FL Abductor
     1, 110,   // FL Hip
     2, 90,    // FL Knee
     3, 90,    // BL Abductor
     4, 70,    // BL Hip
     5, 90,    // BL Knee
     6, 90,    // FR Abductor
     7, 70,    // FR Hip
     8, 90,    // FR Knee
     9, 90,    // BR Abductor
     10, 110,  // BR Hip
     11, 90,   // BR Knee
     1000      // Duration
   );
  
   moveFourServosSmooth( // Move FL and BR legs up
     1, 122,    // FL Hip
     2, 50,     // FL Knee
     10, 122,   // BR Hip
     11, 50,    // BR Knee
     80         // Duration
   );
  
   moveSixServosSmooth2(
     0, 90, 80,     // FL Abductor
     1, 110, 80,    // FL Hip
     2, 90, 160,    // FL Knee
     9, 90, 80,     // BR Abductor
     10, 110, 80,   // BR Hip
     11, 90, 160    // BR Knee
   );
  
   moveFourServosSmooth( // Move FR and BL legs up
     4, 58,    // BL Hip
     5, 130,   // BL Knee
     7, 58,    // FR Hip
     8, 130,   // FR Knee
     80        // Duration
   );

   moveSixServosSmooth2(
     3, 90, 80,    // FL Abductor
     4, 70, 80,    // FL Hip
     5, 90, 160,   // FL Knee
     6, 90, 80,    // BR Abductor
     7, 70, 80,    // BR Hip
     8, 90, 160    // BR Knee
   );

 delay(50);
}

void lookUp() {
  moveServoSmooth(12, 120, 400);  // tilt up
}

void lookDown() {
  moveServoSmooth(12, 60, 400);   // tilt down
}

void lookCenter() {
  moveServoSmooth(12, 90, 400);  // neutral
}

// --- Follow sequence indicator ---
void runFollowSequence() {
 Serial.println("Running follow start sequence...");

 sitDown();
 delay(1000);

 moveTwelveServosSmooth(
     0, 90,    // FL Abductor
     1, 110,   // FL Hip
     2, 90,    // FL Knee
     3, 90,    // BL Abductor
     4, 70,    // BL Hip
     5, 90,    // BL Knee
     6, 90,    // FR Abductor
     7, 70,    // FR Hip
     8, 90,    // FR Knee
     9, 90,    // BR Abductor
     10, 110,  // BR Hip
     11, 90,   // BR Knee
     1000      // Duration
   );

   delay(50);

 Serial.println("Follow mode engaged.");
}

void gestureFollow() {
 headTiltRight();
 delay(50);

 headTiltLeft();
 delay(50);

 moveTwelveServosSmooth(
     0, 90,    // FL Abductor
     1, 110,   // FL Hip
     2, 90,    // FL Knee
     3, 90,    // BL Abductor
     4, 70,    // BL Hip
     5, 90,    // BL Knee
     6, 90,    // FR Abductor
     7, 70,    // FR Hip
     8, 90,    // FR Knee
     9, 90,    // BR Abductor
     10, 110,  // BR Hip
     11, 90,   // BR Knee
     600       // Duration
   );

 delay(50);
}


void handleGesture() {
 if (!gestureModeActive) return; // only run if gesture mode is active

 // Read sensors
 long leftDistance  = readUltrasonicDistance(leftTrigPin, leftEchoPin);
 long rightDistance = readUltrasonicDistance(rightTrigPin, rightEchoPin);

 // Interpret gestures
 if (leftDistance < 50 && rightDistance >= 50) {
   if (lastTurnCommand != 'h') {
     turnPrep();
     lastTurnCommand = 'h';
     lastWalkCommand = '0';
   }
   turnLoop('h');
 }
 else if (rightDistance < 50 && leftDistance >= 50) {
   if (lastTurnCommand != 'f') {
     turnPrep();
     lastTurnCommand = 'f';
     lastWalkCommand = '0';
   }
   turnLoop('f');
 }
 else if (leftDistance < 50 && rightDistance < 50) {
   if (lastWalkCommand != 'g') {
     walkPrep();
     lastWalkCommand = 'g';
     lastTurnCommand = '0';
   }
   walkLoop('g');
 }
 else if (leftDistance >= 50 && leftDistance <= 100 &&
          rightDistance >= 50 && rightDistance <= 100) {
   if (lastWalkCommand != 'e') {
     walkPrep();
     lastWalkCommand = 'e';
     lastTurnCommand = '0';
   }
   walkLoop('e');
 }
 else {
   // No gesture detected
   lastWalkCommand = '0';
   lastTurnCommand = '0';
 }
}

// === Arduino Setup & Bluetooth Control ===
void setup() {
  // Serial for debugging
  Serial.begin(9600);


  // Bluetooth serial
  BTSerial.begin(9600);


  // PCA9685 for servos
  pwm.begin();
  pwm.setPWMFreq(50);
  delay(10);


  // Ultrasonic sensor pins
  pinMode(leftTrigPin, OUTPUT);
  pinMode(leftEchoPin, INPUT);
  pinMode(rightTrigPin, OUTPUT);
  pinMode(rightEchoPin, INPUT);


  // ADD THIS (ESP32 signal pin)
  pinMode(8, INPUT_PULLUP);


  // Start robot in stand pose
  standPose();
}


// --- Function to read distance ---
long readUltrasonicDistance(int trigPin, int echoPin) {
 digitalWrite(trigPin, LOW);
 delayMicroseconds(2);
 digitalWrite(trigPin, HIGH);
 delayMicroseconds(10);
 digitalWrite(trigPin, LOW);


 long duration = pulseIn(echoPin, HIGH, 30000); // 30ms timeout
 long distance = duration * 0.034 / 2;          // Convert to cm


 if (duration == 0) distance = 999;            // no reading
 return distance;
}


void loop() {
  char incoming = '0';


  // --- READ FROM ESP32 (DIGITAL SIGNAL TEST) ---
  static bool lastPinState = HIGH;   // idle HIGH (INPUT_PULLUP)
  bool currentPinState = digitalRead(8);


  static int pulseCount = 0;
  static unsigned long lastPulseTime = 0;
  static unsigned long startTime = millis();


  // --- CLEAN EDGE DETECTION (ANTI-NOISE) ---
  if (millis() > startTime + 1000 && currentPinState == LOW && lastPinState == HIGH) {
   
    delay(5);
   
    if (digitalRead(8) == LOW && millis() - lastPulseTime > 150) {
      pulseCount++;
      lastPulseTime = millis();
    }
  }


  lastPinState = currentPinState;


  // --- INTERPRET PULSE COUNT (WITH END MARKER) ---
  if (pulseCount >= 4 && millis() - lastPulseTime > 500) {


    int commandPulses = pulseCount - 4;


    switch (commandPulses) {
      case 1: incoming = 'e'; break;
      case 2: incoming = 'f'; break;
      case 3: incoming = 'h'; break;
      case 4: incoming = 'g'; break;
      case 5: incoming = 'u'; break;
      case 6: incoming = 'j'; break;
      case 7: incoming = 's'; break;
      case 8: incoming = 'x'; break;
      case 9: incoming = 'r'; break;
      case 10: incoming = 'l'; break;
      case 11: incoming = 'm'; break;
      case 12: incoming = 'i'; break;
      case 13: incoming = 'o'; break;
    }


    pulseCount = 0;
  }


  // --- READ BLUETOOTH INPUT ---
  if (incoming == '0' && BTSerial.available()) {
    char raw = BTSerial.read();


    if ((raw == 'E' || raw == 'G' || raw == 'F' || raw == 'H') && BTSerial.peek() == tolower(raw)) {
      raw = BTSerial.read();
    }


    if (raw == 'E' || raw == 'G' || raw == 'F' || raw == 'H') incoming = tolower(raw);
    else incoming = raw;
  }


  // --- CANCEL ALL MODES ---
  if (incoming == 'a') {
    alternateMode = false;
    alternateMode2 = false;
    controlPosition = false;
    followModeActive = false;
    gestureModeActive = false;
    currentCommand = '0';
    previousCommand = '0';
    walkPrepNeeded = true;
    turnPrepNeeded = true;
    lastWalkCommand = '0';
    lastTurnCommand = '0';
    standPose();
    return;
  }


  // --- MODE TOGGLES ---
  if (incoming == 'c') {
    controlPosition = !controlPosition;
    if (controlPosition) runControlSequence();
    return;
  }


  if (incoming == 'd') {
    alternateMode = true;
    alternateMode2 = false;
    runAlternateSequence();
    return;
  }


  if (incoming == 'b') {
    alternateMode2 = true;
    alternateMode = false;
    runAlternateSequence2();
    return;
  }


  // --- SENSOR READ ---
  long leftDistance = readUltrasonicDistance(leftTrigPin, leftEchoPin);
  long rightDistance = readUltrasonicDistance(rightTrigPin, rightEchoPin);


  if (leftDistance > 150) leftDistance = 150;
  if (rightDistance > 150) rightDistance = 150;


  // --- OBSTACLE STOP ---
  if (!gestureModeActive) {
    if (!objectDetected && (leftDistance <= 30 || rightDistance <= 30)) {
      alternateMode = false;
      alternateMode2 = false;
      controlPosition = false;
      currentCommand = '0';
      walkPrepNeeded = true;
      turnPrepNeeded = true;
      standPose();
      objectDetected = true;
      return;
    }


    if (objectDetected && leftDistance > 30 && rightDistance > 30) {
      objectDetected = false;
    }
  }


  // --- NORMAL CONTROL ---
  previousCommand = currentCommand;
  currentCommand = incoming;


  if (currentCommand != '0') {


    if (objectDetected && currentCommand == 'e') {
      currentCommand = '0';
      return;
    }


    if (controlPosition) {
      if (currentCommand != previousCommand) {
        switch (currentCommand) {
          case 'e': standUp(); break;
          case 'g': restPose(); break;
          case 'f':
            followMode();
            runFollowSequence();
            followModeActive = true;
            break;
        }
      }
    }


    else if (alternateMode) {
      if (currentCommand != previousCommand) {
        switch (currentCommand) {
          case 'e': raiseBody(); break;
          case 'g': lowerBody(); break;
          case 'f': sitDown(); break;
          case 'h': handShake(); break;
        }
      }
    }


    else if (alternateMode2) {
      if (currentCommand != previousCommand) {
        switch (currentCommand) {
          case 'e': kickLeg(); break;
          case 'g': stretchPose(); break;
          case 'f': headTiltRight(); break;
          case 'h': headTiltLeft(); break;
        }
      }
    }


    else {
      switch (currentCommand) {


        case 'e':
        case 'g':
          if (lastWalkCommand != currentCommand) {
            walkPrep();
          }
          walkLoop(currentCommand);
          walkPrepNeeded = false;


          lastWalkCommand = currentCommand;
          lastTurnCommand = '0';
          break;


        case 'f':
        case 'h':
          if (lastTurnCommand != currentCommand) {
            turnPrep();
          }
          turnLoop(currentCommand);
          turnPrepNeeded = false;


          lastTurnCommand = currentCommand;
          lastWalkCommand = '0';
          break;


        default:
          if (currentCommand != previousCommand) {
            switch (currentCommand) {
              case 's': standPose(); break;
              case 'l': headTiltLeft(); delay(2000); standPose2(); break;
              case 'r': headTiltRight(); delay(2000); standPose2(); break;
              case 'k': handShake(); delay(2000); standPose2(); break;
              case 'x': sitDown(); delay(2000); standPose2(); break;
              case 'u': raiseBody(); delay(2000); standPose2(); break;
              case 'j': lowerBody(); delay(2000); standPose2(); break;
              case 'i': lookUp(); break;
              case 'm': lookDown(); break;
              case 'o': lookCenter(); break;
            }


            lastWalkCommand = '0';
            lastTurnCommand = '0';
            walkPrepNeeded = true;
            turnPrepNeeded = true;
          }
          break;
      }
    }
  }


  delay(10);
}