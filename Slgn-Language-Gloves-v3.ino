#if defined(USE_DEFAULT_PARTITION) // ESP32
#undef USE_DEFAULT_PARTITION
#endif

#define USE_DEFAULT_PARTITION
#define CONFIG_ARDUINO_LOOP_STACK_SIZE 8192

// I2Cdev and MPU6050 must be installed as libraries
#include "I2Cdev.h"
#include "MPU6050.h"
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>

// Arduino Wire library is required if I2Cdev I2CDEV_ARDUINO_WIRE implementation
// is used in I2Cdev.h
#if I2CDEV_IMPLEMENTATION == I2CDEV_ARDUINO_WIRE
    #include "Wire.h"
#endif

// BLE UUIDs
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

// MPU6050 object
MPU6050 accelgyro;

// Sensor data variables
int16_t ax, ay, az;
int16_t gx, gy, gz;

// 5 flex sensor setup - corresponding to 5 fingers
const int FLEX_PINS[5] = {7, 1, 4, 6, 3};  // ESP32 pins
int flexValues[5] = {0, 0, 0, 0, 0};       // Store filtered readings for 5 flex sensors

// Filtering related variables
int flexHistory[5][5] = {0};        // Store last 5 readings for each sensor
int historyIndex[5] = {0};          // History index for each sensor
int lastStableValues[5] = {0};      // Last stable values

// MPU6050 filtering related variables
float accelHistory[3][5] = {0};     // Accelerometer history data [x,y,z]
int accelHistoryIndex = 0;
float filteredAccel[3] = {0};       // Filtered acceleration values

// Gesture recognition variables
String currentGesture = "None";
String lastGesture = "None";

// Gesture definitions
#define GESTURE_COUNT 7  
String gestureNames[GESTURE_COUNT] = {
    "Hello",  
    "I",     
    "D",     
    "E",     
    "OK",     
    "Thanks", 
    "You"     
};

// Finger state thresholds - Three-state recognition (small value = bent, large value = straight)
int FLEX_THRESHOLD_LOW = 800;   // Below this value means fully bent
int FLEX_THRESHOLD_HIGH = 1200; // Above this value means fully straight
// Between 800-1200 means approaching straight/half-bent state

// Direction state thresholds - Three-state recognition
const float DIRECTION_THRESHOLD_LOW = -5000;   // Below this value means negative direction
const float DIRECTION_THRESHOLD_HIGH = 5000;   // Above this value means positive direction
// Between -5000 to 5000 means middle direction

// Gesture pattern configuration - Use 2 for fully straight, 1 for approaching straight, 0 for fully bent
// Format: [Thumb, Index, Middle, Ring, Pinky]
const int GESTURE_PATTERNS[GESTURE_COUNT][5] = {
    // Hello - Wave hand (1452,1416,1695,1520,1572) - Mostly fully straight
    {2, 2, 2, 2, 2},  // All fingers fully straight
    
    // I - (1166,1290,1306,1048,854,957) - Mixed state
    {1, 2, 1, 1, 1},  // Thumb, index approaching straight, middle fully straight, ring, pinky fully bent
    
    // d - 1,1,1,1,1 All fingers approaching straight
    {1, 1, 1, 1, 1},  // All fingers approaching straight
    
    // e - 1,0,2,2,2 Thumb approaching straight, index fully bent, middle ring pinky fully straight
    {1, 0, 2, 2, 2},  // Thumb approaching straight, index fully bent, others fully straight
    
    // OK - 1,1,2,2,2 Thumb index approaching straight, middle ring pinky fully straight
    {1, 1, 2, 2, 2},  // Thumb index approaching straight, others fully straight

    // Thanks - 1,0,1,1,1 Thumb approaching straight, index fully bent, middle ring pinky approaching straight
    {1, 0, 1, 1, 1},  // Thumb approaching straight, index fully bent, others approaching straight
    
    // You - Reverse pattern of "I" gesture
    {1, 2, 1, 1, 1}   // All fingers approaching straight (different from "I" in direction)
};

// Direction pattern configuration - Use 2 for positive direction, 1 for middle direction, 0 for negative direction
// Format: [X-axis, Y-axis, Z-axis]
const int DIRECTION_PATTERNS[GESTURE_COUNT][3] = {
    // Hello - Usually has specific movement direction when waving
    {1, 2, 1},  // X middle, Y positive, Z middle
    
    // I - Relatively static
    {2, 2, 2},  // X positive, Y middle, Z negative
    
    // d - Specific direction
    {1, 2, 2},  
    
    // e - Specific direction  
    {0, 2, 1},  // X negative, Y positive, Z middle
    
    // OK - 1,2,2 X middle, Y positive, Z positive
    {1, 2, 2},  // X middle, Y positive, Z positive

    // Thanks - Specific direction
    {2, 0, 1},  // X positive, Y negative, Z middle
    
    // You - Reverse direction of "I" gesture
    {1, 1, 2}   // X negative, Y middle, Z negative (reverse of "I")
};

// Gesture recognition sensitivity (number of fingers that need to match)
const int MATCH_THRESHOLD = 4;  // 4 out of 5 fingers need to match
const int DIRECTION_MATCH_THRESHOLD = 2;  // 2 out of 3 axes need to match

#define OUTPUT_READABLE_ACCELGYRO
#define LED_PIN 2  // Built-in LED on ESP32 dev board is usually GPIO2

// I2C pin configuration
#define I2C_SDA 8  // Hardware I2C1 SDA
#define I2C_SCL 9  // Hardware I2C1 SCL
bool blinkState = false;

// BLE related variables
BLECharacteristic *pCharacteristic;
bool bleConnected = false;

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      bleConnected = true;
      Serial.println("BLE device connected");
    }

    void onDisconnect(BLEServer* pServer) {
      bleConnected = false;
      Serial.println("BLE device disconnected");
      // Restart advertising for reconnection
      BLEDevice::startAdvertising();
      Serial.println("Waiting for device connection...");
    }
};

class MyCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    String value = pCharacteristic->getValue();  
    
    if (value.length() > 0) {
      Serial.println("*********");
      Serial.print("Received BLE data: ");
      Serial.print(value);
      Serial.println();
      Serial.println("*********");
    }
  }
};

void setup() {
    // Initialize serial communication
    Serial.begin(115200);  
    
    // Initialize BLE
    BLEDevice::init("MyESP32-Glove");
    BLEServer *pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());

    BLEService *pService = pServer->createService(SERVICE_UUID);

    pCharacteristic = pService->createCharacteristic(
        CHARACTERISTIC_UUID,
        BLECharacteristic::PROPERTY_READ |
        BLECharacteristic::PROPERTY_WRITE |
        BLECharacteristic::PROPERTY_NOTIFY
    );

    pCharacteristic->setCallbacks(new MyCallbacks());
    pCharacteristic->setValue("Hello from Data Glove");
    pService->start();

    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06);
    pAdvertising->setMinPreferred(0x12);
    BLEDevice::startAdvertising();
    
    // Wait for serial connection
    while (!Serial) {
        delay(10);
    }
    
    Serial.println("=== Data Glove System Starting ===");
    Serial.println("1- Download and install BLE scanner app");
    Serial.println("2- Scan and connect to 'MyESP32-Glove'");
    Serial.println("3- Check data in CUSTOM SERVICE");
    Serial.println("4- Receive sensor data in real-time");
    
    // Initialize custom I2C pins
    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(400000);  // Set I2C clock to 400kHz
    
    // Initialize device
    Serial.println("Initializing I2C devices...");
    accelgyro.initialize();

    // Verify connection
    Serial.println("Testing device connections...");
    Serial.println(accelgyro.testConnection() ? "MPU6050 connection successful" : "MPU6050 connection failed");

    // Initialize 5 flex sensors
    Serial.println("Initializing 5 flex sensors...");
    for(int i = 0; i < 5; i++) {
        pinMode(FLEX_PINS[i], INPUT);
        Serial.print("Flex sensor ");
        Serial.print(i);
        Serial.print(" on pin GPIO");
        Serial.println(FLEX_PINS[i]);
    }
    
    // Configure MPU6050
    accelgyro.setSleepEnabled(false);
    accelgyro.setWakeCycleEnabled(false);
    accelgyro.setStandbyXAccelEnabled(false);
    accelgyro.setStandbyYAccelEnabled(false);
    accelgyro.setStandbyZAccelEnabled(false);
    accelgyro.setStandbyXGyroEnabled(false);
    accelgyro.setStandbyYGyroEnabled(false);
    accelgyro.setStandbyZGyroEnabled(false);
    
    // Configure ADC precision
    analogReadResolution(12);  // ESP32 default 12-bit ADC (0-4095)
    
    // Initialize filter history data
    for(int i = 0; i < 5; i++) {
        int initialValue = analogRead(FLEX_PINS[i]);
        for(int j = 0; j < 5; j++) {
            flexHistory[i][j] = initialValue;
        }
        lastStableValues[i] = initialValue;
        flexValues[i] = initialValue;
    }
    
    // Initialize accelerometer history data
    for(int i = 0; i < 5; i++) {
        accelHistory[0][i] = 0;
        accelHistory[1][i] = 0;
        accelHistory[2][i] = 0;
    }
    
    Serial.println("Finger mapping: [Thumb, Index, Middle, Ring, Pinky]");
    Serial.println("Direction mapping: [X-axis, Y-axis, Z-axis]");
    Serial.println("==============================================");
    Serial.println("Supported gestures: Hello, I, You, D, E, OK, Thanks");
    Serial.println("Three-state recognition: 0=Fully Bent, 1=Approaching Straight, 2=Fully Straight");
    Serial.println("Direction recognition: 0=Negative, 1=Middle, 2=Positive");
    Serial.println("Thresholds: Fully Bent<" + String(FLEX_THRESHOLD_LOW) + " <Approaching Straight< " + String(FLEX_THRESHOLD_HIGH) + " <Fully Straight");
    Serial.println("Direction thresholds: Negative<" + String(DIRECTION_THRESHOLD_LOW) + " <Middle< " + String(DIRECTION_THRESHOLD_HIGH) + " <Positive");
    Serial.println("Sensitivity: " + String(MATCH_THRESHOLD) + "/5 fingers match, " + String(DIRECTION_MATCH_THRESHOLD) + "/3 directions match");
    Serial.println("Enter 'c' for calibration mode");
    Serial.println("==============================================");

    // Configure LED pin
    pinMode(LED_PIN, OUTPUT);
    Serial.println("System initialization completed!");
}

void loop() {
    // Check serial input
    if(Serial.available()) {
        char command = Serial.read();
        if(command == 'c' || command == 'C') {
            autoCalibrateThreshold();
        }
    }

    // Read 5 flex sensors with filtering
    for(int i = 0; i < 5; i++) {
        int rawValue = analogRead(FLEX_PINS[i]);
        flexValues[i] = stabilizedRead(i, rawValue);
    }

    // Read raw accelerometer/gyroscope data and filter
    accelgyro.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);
    filterAccelerometer();
    
    // Gesture recognition
    detectGesture();
    
    #ifdef OUTPUT_READABLE_ACCELGYRO
        // Display all sensor data to serial
        printSensorData();
    #endif

    // Send sensor data via BLE
    sendSensorDataViaBLE();

    // LED blink to indicate activity
    blinkState = !blinkState;
    digitalWrite(LED_PIN, blinkState);
    
    delay(100); // Appropriate delay
}

// Accelerometer filter function
void filterAccelerometer() {
    // Update history data
    accelHistory[0][accelHistoryIndex] = ax;
    accelHistory[1][accelHistoryIndex] = ay;
    accelHistory[2][accelHistoryIndex] = az;
    
    accelHistoryIndex = (accelHistoryIndex + 1) % 5;
    
    // Calculate moving average
    for(int i = 0; i < 3; i++) {
        float sum = 0;
        for(int j = 0; j < 5; j++) {
            sum += accelHistory[i][j];
        }
        filteredAccel[i] = sum / 5;
    }
}

// Get direction states: 0=negative, 1=middle, 2=positive
void getDirectionStates(int states[3]) {
    states[0] = getAxisState(filteredAccel[0]); // X-axis
    states[1] = getAxisState(filteredAccel[1]); // Y-axis
    states[2] = getAxisState(filteredAccel[2]); // Z-axis
}

// Get single axis state
int getAxisState(float value) {
    if(value < DIRECTION_THRESHOLD_LOW) {
        return 0;  // Negative direction
    } else if(value > DIRECTION_THRESHOLD_HIGH) {
        return 2;  // Positive direction
    } else {
        return 1;  // Middle direction
    }
}

// Get direction state description
String getDirectionDescription(int state) {
    switch(state) {
        case 0: return "Negative";
        case 1: return "Middle";
        case 2: return "Positive";
        default: return "Unknown";
    }
}

// Send sensor data via BLE
void sendSensorDataViaBLE() {
    if (bleConnected) {
        // Create sensor data string
        String sensorData = "Flex[";
        for(int i = 0; i < 5; i++) {
            sensorData += String(flexValues[i]);
            if(i < 4) sensorData += ",";
        }
        sensorData += "] States[";
        
        // Add finger states
        int fingerStates[5];
        getFingerStates(fingerStates);
        for(int i = 0; i < 5; i++) {
            sensorData += String(fingerStates[i]);
            if(i < 4) sensorData += ",";
        }
        
        sensorData += "] Accel[";
        sensorData += String(filteredAccel[0]) + ",";
        sensorData += String(filteredAccel[1]) + ",";
        sensorData += String(filteredAccel[2]);
        
        sensorData += "] Direction[";
        int directionStates[3];
        getDirectionStates(directionStates);
        for(int i = 0; i < 3; i++) {
            sensorData += String(directionStates[i]);
            if(i < 2) sensorData += ",";
        }
        
        sensorData += "] Gesture[";
        sensorData += currentGesture;
        sensorData += "]";
        
        // Send data via BLE
        pCharacteristic->setValue(sensorData.c_str());
        pCharacteristic->notify();
    }
}

// Get finger states: 0=fully bent, 1=approaching straight, 2=fully straight
void getFingerStates(int states[5]) {
    for(int i = 0; i < 5; i++) {
        if(flexValues[i] < FLEX_THRESHOLD_LOW) {
            states[i] = 0;  // Fully bent
        } else if(flexValues[i] > FLEX_THRESHOLD_HIGH) {
            states[i] = 2;  // Fully straight
        } else {
            states[i] = 1;  // Approaching straight/half-bent
        }
    }
}

// Get finger state description
String getStateDescription(int state) {
    switch(state) {
        case 0: return "Fully Bent";
        case 1: return "Approaching Straight";
        case 2: return "Fully Straight";
        default: return "Unknown";
    }
}

// Three-state gesture recognition function (combining fingers and direction)
void detectGesture() {
    int bestMatchGesture = -1;
    int bestMatchCount = 0;
    int bestMatchScore = 0;
    
    // Get current finger states
    int currentFingerStates[5];
    getFingerStates(currentFingerStates);
    
    // Get current direction states
    int currentDirectionStates[3];
    getDirectionStates(currentDirectionStates);
    
    // Check matching for each gesture
    for(int gesture = 0; gesture < GESTURE_COUNT; gesture++) {
        int fingerMatchCount = 0;
        int directionMatchCount = 0;
        int totalMatchScore = 0;
        
        // Check finger state matching
        for(int finger = 0; finger < 5; finger++) {
            if(currentFingerStates[finger] == GESTURE_PATTERNS[gesture][finger]) {
                fingerMatchCount++;
                totalMatchScore += 2;  // Perfect match gets 2 points
            } else if(abs(currentFingerStates[finger] - GESTURE_PATTERNS[gesture][finger]) == 1) {
                totalMatchScore += 1;  // One level difference gets 1 point (approximate match)
            }
        }
        
        // Check direction state matching
        for(int axis = 0; axis < 3; axis++) {
            if(currentDirectionStates[axis] == DIRECTION_PATTERNS[gesture][axis]) {
                directionMatchCount++;
                totalMatchScore += 2;  // Direction match also gets 2 points
            } else if(abs(currentDirectionStates[axis] - DIRECTION_PATTERNS[gesture][axis]) == 1) {
                totalMatchScore += 1;  // Approximate direction match gets 1 point
            }
        }
        
        // If this gesture has better matching, update best match
        if(fingerMatchCount >= MATCH_THRESHOLD && directionMatchCount >= DIRECTION_MATCH_THRESHOLD) {
            if(totalMatchScore > bestMatchScore || 
               (totalMatchScore == bestMatchScore && fingerMatchCount > bestMatchCount)) {
                bestMatchScore = totalMatchScore;
                bestMatchCount = fingerMatchCount;
                bestMatchGesture = gesture;
            }
        }
    }
    
    // Update current gesture
    if(bestMatchGesture != -1) {
        String confidence = "";
        if(bestMatchScore >= 12) {
            confidence = "High Confidence";
        } else if(bestMatchScore >= 8) {
            confidence = "Medium Confidence";
        } else {
            confidence = "Low Confidence";
        }
        currentGesture = gestureNames[bestMatchGesture] + "(" + String(bestMatchCount) + "/5," + confidence + ")";
    } else {
        currentGesture = "None";
    }
    
    // Only print when gesture changes
    if(currentGesture != lastGesture) {
        Serial.print("Gesture Recognized: ");
        Serial.println(currentGesture);
        lastGesture = currentGesture;
    }
}

// Stable read function (moving average filter + dead zone control)
int stabilizedRead(int sensorIndex, int newValue) {
    // Update history data
    flexHistory[sensorIndex][historyIndex[sensorIndex]] = newValue;
    historyIndex[sensorIndex] = (historyIndex[sensorIndex] + 1) % 5;
    
    // Calculate moving average
    long sum = 0;
    for(int i = 0; i < 5; i++) {
        sum += flexHistory[sensorIndex][i];
    }
    int average = sum / 5;
    
    // Dead zone control: only update stable value when change exceeds threshold
    if(abs(average - lastStableValues[sensorIndex]) > 15) {
        lastStableValues[sensorIndex] = average;
    }
    
    return lastStableValues[sensorIndex];
}

// Print sensor data to serial
void printSensorData() {
    static unsigned long lastDebugTime = 0;
    unsigned long currentTime = millis();
    
    // Print detailed debug information every 3 seconds
    if(currentTime - lastDebugTime > 3000) {
        debugGesturePatterns();
        lastDebugTime = currentTime;
    }
    
    // Get finger states
    int fingerStates[5];
    getFingerStates(fingerStates);
    
    // Get direction states
    int directionStates[3];
    getDirectionStates(directionStates);
    
    Serial.print("Flex[");
    for(int i = 0; i < 5; i++) {
        Serial.print(flexValues[i]);
        Serial.print("(");
        Serial.print(fingerStates[i]);
        Serial.print(")");
        if(i < 4) Serial.print(",");
    }
    Serial.print("] | ");
    
    Serial.print("Accel[");
    Serial.print(filteredAccel[0]); Serial.print(",");
    Serial.print(filteredAccel[1]); Serial.print(",");
    Serial.print(filteredAccel[2]);
    Serial.print("] | ");
    
    Serial.print("Direction[");
    for(int i = 0; i < 3; i++) {
        Serial.print(directionStates[i]);
        Serial.print("(");
        Serial.print(getDirectionDescription(directionStates[i]));
        Serial.print(")");
        if(i < 2) Serial.print(",");
    }
    Serial.print("] | ");
    
    Serial.print("Gesture[");
    Serial.print(currentGesture);
    Serial.println("]");
}

// Debug function: display current finger states and all gesture patterns
void debugGesturePatterns() {
    int currentFingerStates[5];
    getFingerStates(currentFingerStates);
    
    int currentDirectionStates[3];
    getDirectionStates(currentDirectionStates);
    
    Serial.println("=== Gesture Pattern Debug ===");
    Serial.print("Current Finger States: [");
    for(int i = 0; i < 5; i++) {
        Serial.print(currentFingerStates[i]);
        Serial.print("(");
        Serial.print(getStateDescription(currentFingerStates[i]));
        Serial.print(")");
        if(i < 4) Serial.print(",");
    }
    Serial.println("]");
    
    Serial.print("Current Direction States: [");
    for(int i = 0; i < 3; i++) {
        Serial.print(currentDirectionStates[i]);
        Serial.print("(");
        Serial.print(getDirectionDescription(currentDirectionStates[i]));
        Serial.print(")");
        if(i < 2) Serial.print(",");
    }
    Serial.println("]");
    
    Serial.print("Current Sensor Values - Flex: [");
    for(int i = 0; i < 5; i++) {
        Serial.print(flexValues[i]);
        if(i < 4) Serial.print(",");
    }
    Serial.print("] Accel: [");
    Serial.print(filteredAccel[0]); Serial.print(",");
    Serial.print(filteredAccel[1]); Serial.print(",");
    Serial.print(filteredAccel[2]); Serial.println("]");
    
    for(int gesture = 0; gesture < GESTURE_COUNT; gesture++) {
        int fingerMatchCount = 0;
        int directionMatchCount = 0;
        int totalScore = 0;
        
        Serial.print(gestureNames[gesture] + ": Fingers[");
        for(int finger = 0; finger < 5; finger++) {
            Serial.print(GESTURE_PATTERNS[gesture][finger]);
            if(finger < 4) Serial.print(",");
        }
        Serial.print("] Direction[");
        for(int axis = 0; axis < 3; axis++) {
            Serial.print(DIRECTION_PATTERNS[gesture][axis]);
            if(axis < 2) Serial.print(",");
        }
        Serial.print("] -> ");
        
        // Calculate matching score
        for(int finger = 0; finger < 5; finger++) {
            if(currentFingerStates[finger] == GESTURE_PATTERNS[gesture][finger]) {
                fingerMatchCount++;
                totalScore += 2;
            } else if(abs(currentFingerStates[finger] - GESTURE_PATTERNS[gesture][finger]) == 1) {
                totalScore += 1;
            }
        }
        
        for(int axis = 0; axis < 3; axis++) {
            if(currentDirectionStates[axis] == DIRECTION_PATTERNS[gesture][axis]) {
                directionMatchCount++;
                totalScore += 2;
            } else if(abs(currentDirectionStates[axis] - DIRECTION_PATTERNS[gesture][axis]) == 1) {
                totalScore += 1;
            }
        }
        
        Serial.print(fingerMatchCount);
        Serial.print("/5 fingers,");
        Serial.print(directionMatchCount);
        Serial.print("/3 directions,Total score:");
        Serial.print(totalScore);
        Serial.println("/16");
    }
    Serial.println("===================");
}

// Automatic threshold calibration
void autoCalibrateThreshold() {
    Serial.println("Starting three-state auto-calibration...");
    
    // Calibrate fully bent state
    Serial.println("Please keep hand fully clenched, then press any key");
    while(!Serial.available()) { delay(100); }
    Serial.read();
    
    int bentValues[5];
    int bentSum = 0;
    for(int i = 0; i < 5; i++) {
        bentValues[i] = analogRead(FLEX_PINS[i]);
        bentSum += bentValues[i];
        delay(100);
    }
    
    // Calibrate half-bent state
    Serial.println("Please keep fingers half-bent, then press any key");
    while(!Serial.available()) { delay(100); }
    Serial.read();
    
    int halfBentValues[5];
    int halfBentSum = 0;
    for(int i = 0; i < 5; i++) {
        halfBentValues[i] = analogRead(FLEX_PINS[i]);
        halfBentSum += halfBentValues[i];
        delay(100);
    }
    
    // Calibrate fully straight state
    Serial.println("Please keep hand fully straight, then press any key");
    while(!Serial.available()) { delay(100); }
    Serial.read();
    
    int straightValues[5];
    int straightSum = 0;
    for(int i = 0; i < 5; i++) {
        straightValues[i] = analogRead(FLEX_PINS[i]);
        straightSum += straightValues[i];
        delay(100);
    }
    
    // Calculate thresholds (Note: small value = bent, large value = straight)
    int bentAvg = bentSum / 5;        // Fully bent - small value
    int halfBentAvg = halfBentSum / 5; // Half-bent - middle value
    int straightAvg = straightSum / 5; // Fully straight - large value
    
    FLEX_THRESHOLD_LOW = (bentAvg + halfBentAvg) / 2;
    FLEX_THRESHOLD_HIGH = (halfBentAvg + straightAvg) / 2;
    
    Serial.println("Three-state calibration completed!");
    Serial.print("Fully bent average: "); Serial.println(bentAvg);
    Serial.print("Half-bent average: "); Serial.println(halfBentAvg);
    Serial.print("Fully straight average: "); Serial.println(straightAvg);
    Serial.print("New thresholds - Fully Bent<"); Serial.print(FLEX_THRESHOLD_LOW);
    Serial.print(" <Approaching Straight< "); Serial.print(FLEX_THRESHOLD_HIGH);
    Serial.println(" <Fully Straight");
}