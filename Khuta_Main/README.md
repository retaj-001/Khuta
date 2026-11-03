# Khuta Smart Horse Monitor

المشروع النهائي لـ ESP32. يقرأ DS18B20 وADXL345 وNEO-6M، ويضع اتصال HTTPS في مهمة مستقلة حتى تستمر معالجة NMEA.

1. انسخ `secrets.example.h` إلى `secrets.h` واملأ Wi-Fi ورابط Edge Function ومفتاح الجهاز وشهادة الجذر.
2. في Arduino IDE 2.3.10 اختر ESP32 Dev Module وCore 3.3.12 والمنفذ الفعلي (كان COM5 في آخر فحص).
3. ثبّت DallasTemperature 4.0.6 وOneWire 2.3.8 وAdafruit ADXL345 1.3.4 وAdafruit Unified Sensor 1.1.15 وAdafruit BusIO 1.17.4 وTinyGPSPlus 1.0.3.
4. ارفع `Khuta_Main.ino` وافتح Serial Monitor بسرعة 115200.

لا توصل USB والبطارية معًا. الحقول غير المتاحة تُرسل `null` ولا تُنشأ إحداثيات عند غياب GPS Fix.
