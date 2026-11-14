# Repair Cards 0.5.0

Локальный C++ сервер + SQLite и три Windows-клиента: **Creator**, **Reader**, **Repair**. Для существующего проекта начните с **[обновления и первого запуска](docs/CLIENTS.md)**.

- Creator регистрирует диапазоны, распределяет коды 1С по группам «кол-во / код 1С», готовит QR и открывает BarTender.
- «+» справа у последней группы добавляет строку, «−» удаляет предыдущие. Сумма количеств должна совпасть с выбранными номерами; справа показано назначение каждому Unique ID / Table ID.
- Код 1С сохраняется в SQLite, ячейке бланка и XLSX вместе. При вставке строк/столбцов привязка перемещается. Пустой код и ведущие нули поддерживаются.
- Reader открывает бланк во встроенном окне для просмотра; Repair — для редактирования и сохранения. Creator также открывает редактор внутри приложения.
- Все три клиента используют RepairClient.dll. Локальные ZIP-патчи устанавливает Updater.exe с проверкой версий, хешей, резервной копией и откатом.
- QR-иконка включена в EXE, окна и ярлыки. Общая спокойная тема задается в theme.json.

В полном архиве есть исходники, все библиотеки и **ready-windows** с готовыми EXE и двумя клиентскими DLL. Компилятор — CLion/MinGW64; Qt, Python и Node.js для запуска не нужны. Для встроенного бланка один раз нужен [WebView2 Runtime Evergreen x64](https://developer.microsoft.com/microsoft-edge/webview2/#download-section).

## Обновление

Остановите сервер и клиенты, сохраните копию `data`, своих конфигов и `%LOCALAPPDATA%\RepairCards`. Обновите файлы проекта по [инструкции](docs/CLIENTS.md), затем запустите новый `ready-windows/repair_server.exe`. Его конфиг использует `../data/repair.sqlite3`; проверьте свой фактический путь. Схема автоматически обновляется до 4, учетные номера и бланки сохраняются.

После сервера запустите **Creator.exe**, **Reader.exe** или **Repair.exe**. Важны DLL и остальные файлы рядом с EXE: распределяйте комплект клиента целиком. Старый сервер 0.4 после миграции требует резервной копии старой базы.

## BarTender

Рабочий .btw пользователя хранится в LocalAppData и сохраняется при обновлении. После формирования QR один раз добавьте в нем текстовый объект с источником **OneCCode**. QR-объект остается связан с **QRUrl**. Макет, точный предпросмотр, выбор Zebra S4M и параметры печати доступны в BarTender 10.1. Исходный TEST-original.btw включен без программных изменений. Подробности: [инструкция](docs/CLIENTS.md#3-один-раз-добавить-код-1с-в-рабочий-макет-bartender).

Учетных записей по требованиям нет. Режимы клиентов определяют их интерфейс; серверный API доступен в сети без авторизации.

## Разработка и проверки

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
python tests/api_test.py build/repair_server
```

Дополнительные тесты требуют Pillow, zxing-cpp 2.3.0, Playwright, openpyxl и установленный Chromium только в среде разработки:

```sh
python tests/qr_test.py build/repair_server build/labels_tests
python tests/editor_test.py build/repair_server /usr/bin/chromium
python tests/one_c_browser_test.py build/repair_server /usr/bin/chromium
```

Windows-цели: repair_server, repair_creator, repair_reader, repair_repair, repair_updater, repair_client. Последняя создает RepairClient.dll; ABI запускающих EXE — 1. Создание локальных патчей: scripts/make_patch.py и [документация](docs/CLIENTS.md#для-разработчика-патчей).

[API](docs/API.md) · [Требования](docs/REQUIREMENTS.md) · [Проверки](VALIDATION.md) · [Сторонние библиотеки](third_party/README.md).
