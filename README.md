# Khuta Smart Horse Monitor

## Local setup

1. Copy `secrets.example.h` to `secrets.h`.
2. Fill in Wi-Fi and Firebase device credentials. Never commit `secrets.h`.
3. In Arduino IDE 2.3.10 select **ESP32 Dev Module** and Core **3.3.12**.
4. Install these libraries: FirebaseClient 2.2.13, DallasTemperature 4.0.6, OneWire 2.3.8, Adafruit ADXL345 1.3.4, Adafruit Unified Sensor 1.1.15, Adafruit BusIO 1.17.4, and TinyGPSPlus 1.0.3.
5. Open `Khuta_Main.ino`, select the ESP32 COM port, then Upload.
6. Open Serial Monitor at 115200 baud.

The firmware reads the sensors without blocking GPS reception and schedules a Firebase upload about every two seconds. Missing GPS fields are sent as JSON `null`; Firebase Realtime Database represents null object members as absent children, and the dashboard treats absent fields as unavailable.
