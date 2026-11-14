# Repair Cards — 0.5.0

[Полный архив: исходники и готовые Windows EXE/DLL](https://github.com/ausziehen1/123asd/raw/refs/heads/repair-cards-stage1/downloads/RepairCards-0.5.0-full.zip)

Отдельные комплекты:

- [Creator](https://github.com/ausziehen1/123asd/raw/refs/heads/repair-cards-stage1/downloads/RepairCards-0.5.0-creator-win64.zip)
- [Reader](https://github.com/ausziehen1/123asd/raw/refs/heads/repair-cards-stage1/downloads/RepairCards-0.5.0-reader-win64.zip)
- [Repair](https://github.com/ausziehen1/123asd/raw/refs/heads/repair-cards-stage1/downloads/RepairCards-0.5.0-repair-win64.zip)

[Обновление и пошаговый запуск](repair-cards-stage1/docs/CLIENTS.md)

В Creator — распределение кодов 1С группами «кол-во / код», плюс справа у последней строки и минус у предыдущих, предварительное назначение каждому номеру. Код сохраняется в SQLite, бланке и XLSX и передается BarTender как OneCCode. В макете один раз добавьте текстовый объект с этим источником; QRUrl остается прежним.

Reader показывает бланк во встроенном окне, Repair позволяет редактировать и сохранять. Все три клиента имеют QR-иконку и общую спокойную тему. Подтвержденный сервер скрывается, повторно показывается по нажатию «Сервер»; Enter открывает QR.

Клиентская логика находится в RepairClient.dll. Следующие локальные ZIP-патчи устанавливает Updater.exe с проверкой версии/ABI/хешей, резервной копией и откатом. Комплекты нужно распаковывать целиком. Для встроенных бланков один раз требуется Microsoft WebView2 Runtime Evergreen x64.

Перед первым обновлением сервера остановите приложения, сохраните data, конфиг и LocalAppData/RepairCards. Схема обновляется до 4, существующие номера и документы сохраняются. В полном ZIP папка ready-windows содержит все программы; ее конфиг использует ../data/repair.sqlite3. Проверьте свой фактический путь базы.

[Проверки и их границы](repair-cards-stage1/VALIDATION.md) · [Windows CI](https://github.com/ausziehen1/123asd/actions/workflows/repair-cards-windows.yml?query=branch%3Arepair-cards-stage1)

SHA-256: [полный архив](downloads/RepairCards-0.5.0-full.zip.sha256), [Creator](downloads/RepairCards-0.5.0-creator-win64.zip.sha256), [Reader](downloads/RepairCards-0.5.0-reader-win64.zip.sha256), [Repair](downloads/RepairCards-0.5.0-repair-win64.zip.sha256).

## Предыдущие версии

- [0.4.1](https://github.com/ausziehen1/123asd/raw/refs/heads/repair-cards-stage1/downloads/RepairCards-0.4.1-full.zip)
- [0.4](https://github.com/ausziehen1/123asd/raw/refs/heads/repair-cards-stage1/downloads/RepairCards-0.4-full.zip)
- [0.3](https://github.com/ausziehen1/123asd/raw/refs/heads/repair-cards-stage1/downloads/RepairCards-stage2-editor.zip)
- [0.2](https://github.com/ausziehen1/123asd/raw/refs/heads/repair-cards-stage1/downloads/RepairCards-stage2-creator.zip)
- [0.1](https://github.com/ausziehen1/123asd/raw/refs/heads/repair-cards-stage1/downloads/RepairCards-stage1.zip)
