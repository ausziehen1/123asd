# Included libraries

Dependencies are included so the project can build without downloads.

| Library | Pinned version | License / source |
|---|---|---|
| SQLite amalgamation | 3.50.4 | Public domain, https://sqlite.org/copyright.html |
| cpp-httplib | 0.28.0 | MIT, `HTTPLIB-LICENSE` |
| nlohmann/json | 3.12.0 | MIT, `JSON-LICENSE` |

SQLite's generated `sqlite3.c` and `sqlite3.h` were obtained from the pinned
`WiseLibs/better-sqlite3` v12.4.1 tree, `deps/sqlite3/`. Only the SQLite amalgamation is used;
the Node.js addon and its code are not used. Files include SQLite's public-domain notice.

Sources:

- https://github.com/WiseLibs/better-sqlite3/tree/v12.4.1/deps/sqlite3
- https://github.com/yhirose/cpp-httplib/tree/v0.28.0
- https://github.com/nlohmann/json/tree/v3.12.0

SHA-256 of included source files:

```text
90b7189566ea710ee18361b25d893f5cda3fcf43033223a16384762f53e99a85  sqlite3.c
abd1514e0351f79393d1be882830afdb40a8099e8257f311f0bfdf8486f11bea  sqlite3.h
9e5cafaa227dad5c10e837990a736cecaff03258f50d37067cedeee1dba5d344  httplib.h
aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63  json.hpp
```

Browser library versions, licenses and hashes: `../web/assets/README.md`.
