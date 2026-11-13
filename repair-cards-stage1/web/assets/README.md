# Browser libraries

All runtime files are served locally. Browser Internet access and Node.js are not required.

- x-data-spreadsheet **1.1.9**, MIT: https://www.npmjs.com/package/x-data-spreadsheet/v/1.1.9 ; license in `XSPREADSHEET-LICENSE`.
- ExcelJS **4.4.0**, MIT: https://www.npmjs.com/package/exceljs/v/4.4.0 ; license in `EXCELJS-LICENSE`.
- `locale-ru.js` is a Russian translation derived from the MIT-licensed x-data-spreadsheet English locale.
- `editor.js` contains the application integration: Excel conversion, versioned saving, read-only mode, safe text rendering and preservation of column widths during structural edits.

Vendor files were obtained from pinned npm versions via jsDelivr; the SVG icon sprite came from the same x-data-spreadsheet npm package. No vendor runtime source was modified.

SHA-256:

```text
7eae95f6d755ca85e613c4c1079d8462270ecda08232a95a1b33234c1dea823a  xspreadsheet.js
780df7cbf34c43d315acb51165248c2272559a2a189b87f03baaf735ee3399d9  xspreadsheet.css
adc9b1407233aa0fdf9690598812f660a1d8c3c6f69dd5af94f554480a651f96  58eaeb4e52248a5c75936c6f4c33a370.svg
7e49da68588e250dbb8bba190d2caa8ab3787cc0284bda1d8b2f805c4df742c9  exceljs.min.js
```
