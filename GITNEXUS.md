# GitNexus

This repository pins GitNexus `1.6.9`. The current development machine uses
Node `22.15.0`; GitNexus `1.6.10+` requires Node `22.18.0` or newer and can
fail during analyzer identity hashing on Windows.

Use the repository wrapper so the CLI version and native build permissions stay
consistent. The wrapper also selects the `rosCar` index automatically when
other repositories are registered on the same machine:

```powershell
.\packaging\gitnexus.ps1 status
.\packaging\gitnexus.ps1 analyze
.\packaging\gitnexus.ps1 query "motor mapping cmd_vel differential drive" --content
.\packaging\gitnexus.ps1 context base_drive_node --content
```

`analyze` uses `.gitnexusignore` and keeps generated index data under
`.gitnexus/`. Build output, the portable runtime bundle, and temporary PDF
artifacts are excluded so incremental analysis only considers source files.
