#include <Wire.h>

// Define the ESP32 pins used for the I2C bus
#define SDA_PIN 21
#define SCL_PIN 22

void setup() {
  // Start serial communication with the PC for debugging/output
  Serial.begin(115200);

  // Initialize the I2C bus with the selected SDA and SCL pins
  Wire.begin(SDA_PIN, SCL_PIN);

  // Set the I2C clock frequency to 100 kHz
  // This is a safe and standard speed for a first communication test
  Wire.setClock(100000);

  // Print a message to indicate that the scan is starting
  Serial.println("Scan I2C CN0349...");
}

void loop() {
  // Variable used to store the I2C transmission status
  byte error;

  // Counter for the number of detected I2C devices
  int count = 0;

  // Scan all possible 7-bit I2C addresses from 1 to 126
  for (byte address = 1; address < 127; address++) {
    // Start a transmission to the current address
    Wire.beginTransmission(address);

    // End the transmission and store the returned status
    // If error == 0, a device responded at this address
    error = Wire.endTransmission();

    // If a device is found, print its aACddress
    if (error == 0) {
      Serial.print("Peripherique trouve a l'adresse : 0x");

      // Add a leading zero for addresses below 0x10
      if (address < 16) Serial.print("0");

      // Print the address in hexadecimal format
      Serial.println(address, HEX);

      // Increase the detected device count
      count++;
    }
  }

  // If no devices were found, print a message
  if (count == 0) {
    Serial.println("Aucun peripherique I2C detecte.");
  } else {
    // Otherwise, indicate that the scan is complete
    Serial.println("Scan termine.");
  }

  // Wait 5 seconds before repeating the scan
  delay(5000);
}
