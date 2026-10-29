# Проверка R3.3

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

SHA-256 исправленного скетча: `2537d2d771e5fed42b33b5fc6d73d30524a5a3e69af79ab7063d4f5095c34fda`

SHA-256 EngineSafety.h: `6055be3d189f2ef69e70ad9a8c35aaa2d21e2bab40522145f317230e34df6f5a`

SHA-256 собранного application binary: `13041395e6451921d8a9fbc40526bbb8bb60546450f4681b8d77e4483f7e64d5`

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
