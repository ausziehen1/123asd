# BarTender template

`TEST-original.btw` is the user's supplied `TEST.btw`, copied byte-for-byte.
SHA-256: `6f8e510d056f4a0b774a40f0faa0042aacb56b4cfce12623bbd122b413badfcf`.

The document identifies BarTender 10.1 SR3 build 2950 Enterprise Automation and Zebra S4M ZPL. The QR object «Штрих-код 1» contains static sample `MON003171`. No binary modification or automatic QRUrl binding is claimed.

Creator seeds `%LOCALAPPDATA%\RepairCards\labels\RepairCards.btw` only when absent. A different working `.btw` can be selected. In BarTender, bind the QR source to database field `QRUrl`, disable serialization/transforms, check automatic QR version/encoding and dimensions, and save the document.

Jobs contain UTF-8 BOM CSV with quoted headers `InternalID,ProductID,Platform,QRUrl`. Creator attaches the selected job using BarTender's supported CLI. Full instructions: [QR_PRINT_READER.md](../docs/QR_PRINT_READER.md).

The user's template is a project input, not covered by third-party library licenses. BarTender and printer drivers are installed separately.
