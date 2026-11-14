# Repair Cards 0.6.0

**[Скачать полный проект: готовый сервер, однофайловые клиенты и исходники](https://github.com/ausziehen1/123asd/raw/refs/heads/repair-cards-stage1/downloads/RepairCards-0.6.0-full.zip)**

**[Полная пошаговая инструкция](repair-cards-stage1/docs/USER_GUIDE.md)**: установка, перенос существующей базы, совместная работа, BarTender, история, резервирование, будущие обновления и откат.

Пользователям передается **один** файл нужного приложения:

- [Creator.exe](https://github.com/ausziehen1/123asd/raw/refs/heads/repair-cards-stage1/downloads/0.6.0/Creator.exe) — регистрация, редактирование, коды 1С, QR/BarTender.
- [Reader.exe](https://github.com/ausziehen1/123asd/raw/refs/heads/repair-cards-stage1/downloads/0.6.0/Reader.exe) — встроенный просмотр.
- [Repair.exe](https://github.com/ausziehen1/123asd/raw/refs/heads/repair-cards-stage1/downloads/0.6.0/Repair.exe) — встроенное редактирование и сохранение.
- [Все три однофайловых клиента и инструкция одним ZIP](https://github.com/ausziehen1/123asd/raw/refs/heads/repair-cards-stage1/downloads/RepairCards-0.6.0-clients.zip).

EXE сам размещает DLL/Updater/справку в LocalAppData. Можно запускать несколько экземпляров каждого приложения на одном или нескольких ПК. Для встроенного бланка один раз нужен Microsoft WebView2 Runtime Evergreen x64; Creator для печати использует установленный BarTender и драйвер принтера. Обычные маленькие EXE в ready-windows предназначены для комплекта с соседними DLL; для раздачи одного файла берите clients или ссылки выше.

SQLite остается на локальном диске одного сервера. Сохранения новых клиентов идут через FIFO-очередь: разные ячейки объединяются, одинаковая получает последнее значение. При пересекающихся изменениях структуры применяется поздний лист целиком. Предыдущие версии доступны через «История». Бланки получают обновления примерно раз в секунду, списки Creator — раз в три секунды; активный ввод не прерывается.

Перед переходом с 0.5 остановите старый сервер, сохраните data/конфиг/LocalAppData RepairCards и следуйте разделу 3 инструкции. Схема обновляется до 5. Для новой очереди и живого обновления замените **все клиенты**. Не запускайте пустую базу вместо прежней.

Сервер запускается ready-windows/START-SERVER.cmd; свой конфиг — config.local.json. Для живой резервной копии включен repair_backup.exe (SQLite Backup API + integrity_check). Для будущих обновлений можно заменить один EXE либо установить локальный ZIP-патч DLL через меню приложения/параметр --update. Параметр --files открывает каталог соответствующего комплекта.

[Реальный DLL-патч обычного комплекта 0.5.0 → 0.6.0](https://github.com/ausziehen1/123asd/raw/refs/heads/repair-cards-stage1/downloads/RepairCards-0.5.0-to-0.6.0-client-patch.zip) применяется старым Updater **после обновления сервера и web**. Однофайловое распространение требует новых EXE; сам DLL-патч маленькие старые EXE в однофайловые не превращает.

[Проверки и их границы](repair-cards-stage1/VALIDATION.md) · [Windows CI](https://github.com/ausziehen1/123asd/actions/workflows/repair-cards-windows.yml?query=branch%3Arepair-cards-stage1).

Контрольные суммы SHA-256: [полный ZIP](downloads/RepairCards-0.6.0-full.zip.sha256), [клиенты ZIP](downloads/RepairCards-0.6.0-clients.zip.sha256), [Creator](downloads/0.6.0/Creator.exe.sha256), [Reader](downloads/0.6.0/Reader.exe.sha256), [Repair](downloads/0.6.0/Repair.exe.sha256), [DLL-патч](downloads/RepairCards-0.5.0-to-0.6.0-client-patch.zip.sha256).

## Предыдущие версии

- [0.5.0](https://github.com/ausziehen1/123asd/raw/refs/heads/repair-cards-stage1/downloads/RepairCards-0.5.0-full.zip)
- [0.4.1](https://github.com/ausziehen1/123asd/raw/refs/heads/repair-cards-stage1/downloads/RepairCards-0.4.1-full.zip)
- [0.4](https://github.com/ausziehen1/123asd/raw/refs/heads/repair-cards-stage1/downloads/RepairCards-0.4-full.zip)
- [0.3](https://github.com/ausziehen1/123asd/raw/refs/heads/repair-cards-stage1/downloads/RepairCards-stage2-editor.zip)
- [0.2](https://github.com/ausziehen1/123asd/raw/refs/heads/repair-cards-stage1/downloads/RepairCards-stage2-creator.zip)
- [0.1](https://github.com/ausziehen1/123asd/raw/refs/heads/repair-cards-stage1/downloads/RepairCards-stage1.zip)
