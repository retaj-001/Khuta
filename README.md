# Khuta Smart Horse Monitor

Firmware and Supabase backend for the Khuta smart horse monitor.

## Structure

- `Khuta_Main/`: Arduino IDE sketch and a safe secrets template.
- `Khuta_Tests/`: independent DS18B20, ADXL345, and NEO-6M test sketches.
- `supabase/migrations/`: replayable PostgreSQL schema, indexes, grants, RLS, and UTC configuration.
- `supabase/functions/ingest-reading/`: authenticated HTTPS ingestion function.

## Arduino

1. Copy `Khuta_Main/secrets.example.h` to `Khuta_Main/secrets.h` and fill the local values. Never commit `secrets.h`.
2. In Arduino IDE 2.3.10 select **ESP32 Dev Module**, Core **3.3.12**, and the actual USB port.
3. Install DallasTemperature 4.0.6, OneWire 2.3.8, Adafruit ADXL345 1.3.4, Adafruit Unified Sensor 1.1.15, Adafruit BusIO 1.17.4, and TinyGPSPlus 1.0.3.
4. Upload `Khuta_Main/Khuta_Main.ino` and open Serial Monitor at 115200.

The network task runs on the other ESP32 core, so HTTPS retries do not stop NMEA parsing. A stable `reading_id` makes retries idempotent. Missing GPS fields are JSON `null`; the firmware never invents a fix.

## Supabase

Apply migrations in filename order, seed the device with only a SHA-256 secret hash, then deploy `ingest-reading`. The plaintext device secret belongs only in the local ESP32 `secrets.h`.

The browser is read-only. Precise coordinates are removed by `latest_public_reading` and are returned only by `latest_authorized_reading` for an assigned authenticated user.

## Safety

Do not power the board from USB and the LiPo circuit at the same time. Inspect the battery, TP4056, and MT3608 before battery operation.
