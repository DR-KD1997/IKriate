# Drawpile in IKriate

Vendored from [Drawpile](https://drawpile.net) 2.3.1 (GPL-3.0-or-later, see `LICENSE.txt`
and `AUTHORS`): the canvas engine (`src/drawdance`, C), the protocol and networking
libraries (`src/libshared`, `src/libclient`) and the built-in server (`src/libserver`), plus
zstd 1.5.7 (`zstd/`, BSD) that Drawpile's session format needs. Protocol `dp:4.25.1`, which
talks to Drawpile 2.3 servers and clients.

IKriate uses it for online sessions (`libs/ui/ikriate/online`) and for the Drawpile file
formats (`IkDrawpileFiles`, `plugins/impex/drawpile`). Everything is built as static
libraries with hidden symbols and linked into kritaui, so nothing clashes with Krita's own
libmypaint, SQLite or curve classes.

## Left out

- Drawpile's desktop application, dedicated server, browser build and its build system
  (`CMakeLists.txt` here replaces the top-level one).
- Tests, benchmarks, translations, code generators (`protogen`, `generators`) and the
  optional Rust parts (`src/drawdance/rust`).
- The KArchive and libzip zip back-ends (IKriate provides a QuaZip one).

## Changed

| File | Change |
|---|---|
| `src/*/CMakeLists.txt`, `src/cmake-config/CMakeLists.txt` | Test, i18n and generator subdirectories removed; configuration values fixed (no git version, no install rules). |
| `src/drawdance/libimpex/dpimpex/zip_archive_quazip.cpp` (new), `libimpex/CMakeLists.txt` | `DP_zip_*` implemented with QuaZip, which Krita already uses. |
| `src/libserver/client.{h,cpp}`, `thinserverclient.{h,cpp}`, `src/libclient/net/server.cpp` | WebSocket code behind `DP_HAVE_WEBSOCKETS` (defined only when Qt WebSockets is found), so sessions use plain TCP without that module. |
| `src/libclient/utils/kis_cubic_curve.{h,cpp}` and its users (`brushes/brush.*`, `tools/toolcontroller.*`, `view/canvascontrollerbase.*`, `config/config.h`) | `KisCubicCurve` renamed `DpCubicCurve`: Drawpile carries an older copy of Krita's class, and two definitions of one class in one program crash. |
| `src/drawdance/libcommon/dpcommon/curve.c` | Two comments follow the rename (no code change). |
| `src/libclient/CMakeLists.txt` | The parental-controls stub on every platform (the Windows version needs COM's Parental Controls API, which llvm-mingw may lack; IKriate filters NSFM sessions itself). |

To update: replace `src/` and `zstd/` with the new release's, delete what is listed under
*Left out*, re-apply the changes above (each is small), and check `protocol.yaml` for the
protocol version the wrapper's `CMakeLists.txt` passes on.
