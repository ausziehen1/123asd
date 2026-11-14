CURRENT RELEASE: 0.6.0

Full guide: docs/USER_GUIDE.md.
Distribute ONE EXE from clients: Creator.exe, Reader.exe or Repair.exe.
The standalone EXE extracts DLLs into the user's LocalAppData cache.
Multiple instances are supported. Saves are queued and merged; forms refresh automatically.

Server: ready-windows/START-SERVER.cmd. Back up existing data before schema upgrade to 5.
The normal EXEs in ready-windows still need adjacent DLLs; use clients for one-file distribution.
Embedded forms need Microsoft WebView2 Runtime Evergreen x64 once per PC.
