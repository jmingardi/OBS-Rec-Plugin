# SQLite amalgamation

This directory vendors the official SQLite 3.53.4 amalgamation (`3530400`) from
<https://www.sqlite.org/2026/sqlite-amalgamation-3530400.zip>.

- Official SHA3-256: `628a44cfe82c66aed1ccbbe85a562d2e33ebe64b3288981ed76285612227934e`
- Verified local SHA-256: `1e71ddf93849c6a6ecf58b827c0692073d2dd7ee40196158068f7b29f422e87d`
- SQLite is dedicated to the public domain: <https://www.sqlite.org/copyright.html>

The amalgamation is compiled directly into the plugin because the Qt bundle pinned by OBS Studio 32.2.2 does not
ship the `QSQLITE` driver.
