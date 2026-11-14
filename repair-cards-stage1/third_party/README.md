# Included libraries

Dependencies are included so the project can build without downloads.

| Library | Pinned version | License / source |
|---|---|---|
| SQLite amalgamation | 3.50.4 | Public domain, https://sqlite.org/copyright.html |
| cpp-httplib | 0.28.0 | MIT, `HTTPLIB-LICENSE` |
| nlohmann/json | 3.12.0 | MIT, `JSON-LICENSE` |
| miniz | 3.0.2 | MIT, `MINIZ-LICENSE` |
| pugixml | 1.14 | MIT, `PUGIXML-LICENSE` |
| PicoSHA2 | 1.0.1 | MIT, `PICOSHA2-LICENSE` |
| Microsoft WebView2 SDK (native header and x64 loader) | 1.0.2903.40 | Microsoft BSD, `webview2/LICENSE.txt` |
| Nayuki QR Code generator (C++) | 1.8.0 | MIT, embedded in `qrcodegen/qrcodegen.cpp` and `.hpp` |

SQLite's generated `sqlite3.c` and `sqlite3.h` were obtained from the pinned
`WiseLibs/better-sqlite3` v12.4.1 tree, `deps/sqlite3/`. Only the SQLite amalgamation is used;
the Node.js addon and its code are not used. Files include SQLite's public-domain notice.

Sources:

- https://github.com/WiseLibs/better-sqlite3/tree/v12.4.1/deps/sqlite3
- https://github.com/yhirose/cpp-httplib/tree/v0.28.0
- https://github.com/nlohmann/json/tree/v3.12.0
- https://github.com/nayuki/QR-Code-generator/tree/v1.8.0/cpp

SHA-256 of included source files:

```text
90b7189566ea710ee18361b25d893f5cda3fcf43033223a16384762f53e99a85  sqlite3.c
abd1514e0351f79393d1be882830afdb40a8099e8257f311f0bfdf8486f11bea  sqlite3.h
9e5cafaa227dad5c10e837990a736cecaff03258f50d37067cedeee1dba5d344  httplib.h
aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63  json.hpp
1f3b3fcdac6954c32cf583ccd02ec9b5901f756a38c461acedc70be4a77d3757  qrcodegen/qrcodegen.cpp
b779c3b156cf7a57ce789d6fee4fc991ccc2913774d26c909d22bb8f26b2a793  qrcodegen/qrcodegen.hpp
```

Browser library versions, licenses and hashes: `../web/assets/README.md`.

Ready Windows EXEs are statically linked using LLVM-MinGW 20261104 (Clang 23.1.5).
Runtime license notices are included in `runtime-licenses/`; BarTender and Zebra drivers are not bundled.

Sources for 0.5 dependencies:

- https://github.com/richgel999/miniz/tree/3.0.2 (static build; miniz_export.h defines MINIZ_EXPORT empty, as for a static generated export header).
- https://github.com/zeux/pugixml/tree/v1.14/src
- https://github.com/okdshin/PicoSHA2/tree/v1.0.1
- https://www.nuget.org/packages/Microsoft.Web.WebView2/1.0.2903.40 (native WebView2.h, x64 WebView2Loader.dll and license). The header's EventToken.h include is changed to lowercase eventtoken.h for case-sensitive MinGW cross-builds; no interface definitions were altered. Loader binary is unmodified.

WebView2 Runtime is a separate Microsoft installation, not bundled as part of the SDK. Runtime and deployment: https://learn.microsoft.com/microsoft-edge/webview2/concepts/distribution

Own QR icon: resources/qr.ico. No external icon library is required.
