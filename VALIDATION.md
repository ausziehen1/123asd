# Проверка R3.3.1

Результат: сборка и линковка ESP32-S3 native USB прошли успешно.
Физическая плата не подключалась.

- Arduino-ESP32 2.0.17; ESP-IDF из поставки этого SDK.
- Xtensa ESP32-S3 GCC 8.4.0, esp-2021r2-patch5.
- PlatformIO espressif32 6.12.0, PlatformIO Core 6.2.0.
- Целевая конфигурация: ESP32-S3-DevKitC-1-N8, 8 MB QIO, CPU 240 MHz.
- Native USB Mode 0 (TinyUSB), CDC On Boot 0.
- Arduino/Event core 0; движок Core 1, приоритет 4.
- RAM: 50 616 / 327 680 байт.
- Flash: 331 737 / 3 342 336 байт раздела приложения.
- 6 групп regression-тестов: PASS.
- 10 integration-сценариев самого скетча с заглушками: PASS.
- AddressSanitizer + UndefinedBehaviorSanitizer: без выявленных ошибок.
- PATTERN_CLOSE, PATTERN_LONG, MULTIPLIERS, report_descriptor равны исходным.

SHA-256 исходного вложения: `87cad1a502a3a849110705f26a9e0b3d548c7a9142f28d0511b6217cac3d2899`

SHA-256 исправленного скетча: `2d1cd113fb4cd455a6ea95dedb201f1350abd3e85529b262b68fcf6dbc950134`

SHA-256 EngineSafety.h: `cc03134d08e020279f87d0ae34153758b7ce04e359810cba6a3814be9e0db9aa`

SHA-256 собранного application binary: `525f9860fc968784095b3383d060645c7d01ca3ba34fe39764e4664c2b0f495f`

Двоичный файл не включён в исходный архив: сборку для прошивки нужно выполнять
под фактическую плату и её flash/PSRAM. Изменять исходные таблицы для этого не нужно.

Команды текущей облачной проверки:

```sh
bash /workspace/123asd/tests/run.sh
PLATFORMIO_CORE_DIR=/workspace/tooling/platformio /workspace/tooling/venv/bin/pio run -d /workspace/123asd -c /workspace/tooling/platformio.build.ini -j 4
```

Локальная конфигурация сборки использует официальные SDK и toolchain с GitHub,
проверенные по SHA-256 официального индекса Espressif, вместо недоступного
облачной сетевой политике реестра PlatformIO. Исходники SDK не изменялись;
локальные manifest/wrapper-файлы только подключают те же инструменты к PlatformIO.
В поставляемом проекте сохранён обычный переносимый platformio.ini.

Проверки на ПК не моделируют электрическую схему, реальный USB-хост, задержки
FreeRTOS, flash и interrupts. Порядок проверки на плате указан в README.md.

## Arduino IDE 1.8.19 compatibility regression

Arduino Builder 1.6.1 (upstream tag), Arduino ctags 5.8-arduino11, Linux host,
Arduino-ESP32 2.0.17, Xtensa ESP32-S3 GCC 8.4.0.

Baseline R3.3: воспроизведены ошибки пользователя на строках 814, 1050, 1072,
1131. R3.3.1: полная компиляция и линковка прошли, exit status 0.

FQBN: `esp32:esp32:esp32s3:PSRAM=disabled,FlashMode=qio,FlashSize=4M,LoopCore=1,EventsCore=1,USBMode=default,CDCOnBoot=default,MSCOnBoot=default,DFUOnBoot=default,UploadMode=default,PartitionScheme=huge_app,CPUFreq=240,UploadSpeed=921600,DebugLevel=none,EraseFlash=none,JTAGAdapter=default`

Flash: 327 685 / 3 145 728 bytes. RAM: 50 480 / 327 680 bytes.
Все прежние 6+10 групп/сценариев прошли; тест ранних прототипов тоже прошёл.
Содержимое перенесённых типов, траекторий, множителей и HID descriptor сверено
с предыдущей версией. Настройки и алгоритмы движения не изменены.

Команда проверки: `python3 /workspace/tooling/check_arduino_ide.py fixed`.
Сборщик построен из исходников официального тега; зависимости Go проверены
по исходному go.sum. В ctags переименован служебный макрос __unused__,
конфликтующий с современными glibc headers; логика разбора прототипов не менялась.
ESP32 SDK не патчился. Windows GUI и физическая плата не проверялись.
