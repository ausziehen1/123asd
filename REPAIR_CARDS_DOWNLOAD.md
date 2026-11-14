# Карточки ремонта — версия 0.4.1

[Исправленный Креатор 0.4.1 для Windows x64](https://github.com/ausziehen1/123asd/raw/refs/heads/repair-cards-stage1/downloads/RepairCards-0.4.1-creator-win64.zip) — для текущего исправления достаточно заменить Креатор; совместим с сервером 0.4.

[Полный ZIP: исходники и готовые Windows EXE](https://github.com/ausziehen1/123asd/raw/refs/heads/repair-cards-stage1/downloads/RepairCards-0.4.1-full.zip)

[Отдельный Ридер для Windows x64](https://github.com/ausziehen1/123asd/raw/refs/heads/repair-cards-stage1/downloads/RepairCards-0.4.1-reader-win64.zip)

[Обновление, первый QR, макет, предпросмотр и печать](repair-cards-stage1/docs/QR_PRINT_READER.md)

В 0.4.1 исправлено формирование командной строки BarTender: параметры передаются без внешних кавычек, пути заключены в кавычки после знака равенства. Добавлены /MAX, проверка видимого окна и журнал bartender-launch.log. Работа самого BarTender проверяется на пользовательском ПК.

Креатор регистрирует партии и открывает редактор Excel-бланка. Добавлены QR выбранных записей или последней партии, отдельный Ридер для просмотра и передача заданий в BarTender 10.1. Положение и оформление элементов меняются в рабочем .btw; точный предпросмотр, выбор принтера и свойства драйвера доступны в BarTender.

Ваш TEST.btw включен без изменений. Один раз нужно связать QR-объект с полем QRUrl и сохранить рабочий макет по инструкции. Печать запускается вручную из диалога BarTender.

Перед обновлением остановите сервер и Креатор, сохраните копию data и своего конфига. Содержимое RepairCards-stage1 из архива обновляет существующий проект; рабочая база не включена. Готовые EXE находятся в ready-windows, исходники можно собирать в CLion + MinGW64. Схема SQLite обновляется автоматически, сохраняя карточки.

Проверены кросс-сборка трех Windows EXE, четыре группы C++ тестов, API, независимое распознавание QR и браузерный редактор. Реальный запуск новых окон в Windows, BarTender 10.1 и физическая Zebra S4M проверяются на пользовательском ПК — это не заявлено как уже выполненная проверка.

Контрольные суммы:

- [Полный архив SHA-256](downloads/RepairCards-0.4.1-full.zip.sha256)
- [Ридер SHA-256](downloads/RepairCards-0.4.1-reader-win64.zip.sha256)

- [Креатор SHA-256](downloads/RepairCards-0.4.1-creator-win64.zip.sha256)

## Предыдущие версии

- [0.4 — QR и BarTender](https://github.com/ausziehen1/123asd/raw/refs/heads/repair-cards-stage1/downloads/RepairCards-0.4-full.zip)

- [0.3 — редактор](https://github.com/ausziehen1/123asd/raw/refs/heads/repair-cards-stage1/downloads/RepairCards-stage2-editor.zip)
- [0.2 — Креатор](https://github.com/ausziehen1/123asd/raw/refs/heads/repair-cards-stage1/downloads/RepairCards-stage2-creator.zip)
- [0.1 — сервер и SQLite](https://github.com/ausziehen1/123asd/raw/refs/heads/repair-cards-stage1/downloads/RepairCards-stage1.zip)

[Исходники текущей версии](repair-cards-stage1).
