# Shipping Runtime and Hazel Fundamentals - August 30, 2026

## Outcome

IC_2DE now produces a standalone Windows player target rather than treating the development testbed as the shipped game. The architecture was informed by [Hazel's application and project split](https://github.com/TheCherno/Hazel/tree/1feb70572fa87fa1c4ba784a2cfeada5b4a500db), but no Hazel implementation or assets were copied.

## Runtime foundation

- `RuntimeProject::load()` owns a strict, versioned `key=value` manifest format.
- Asset and start-scene paths must be relative, cannot traverse with `..`, and are resolved from the manifest directory.
- `ic2de_testbed` keeps development controls and diagnostics.
- `IC_2DE.exe` is a separate shipping entry point with project-relative startup.
- `windows-shipping` excludes tests, the testbed, overlays, the grid, and development controls.
- MSVC's static runtime keeps redistributable compiler DLLs out of the package requirements.

## Package contract

`tools/package.ps1 -RunSmoke` builds and tests Release, builds the shipping preset, installs only the `Runtime` component, creates a ZIP, and launches the installed executable from its package directory. The runtime folder contains only:

- `IC_2DE.exe`;
- `IC_2DE.runtime`;
- `Content/test_area.scene`;
- `RUNTIME_README.txt`;
- `THIRD_PARTY_NOTICES.txt`.

The smoke writes `shipping-smoke.png` after archive creation, so the diagnostic image is evidence beside the working folder rather than shipped content.

## Verified before the physics checkpoint

- Eight of eight Release suites passed.
- The installed executable loaded `Content/test_area.scene`, rendered through the RTX 2080 Ti/OpenGL path, completed exactly 300 fixed ticks, validated collision/elevation/trigger state and texture lifetime, and exited zero.
- The minimal ZIP was 401,149 bytes with SHA-256 `BA61C080A0F018B749144F556F6FF9358E4B949D7E4B616DAC3D8D38EA520F7B`.

The package is rebuilt and smoke-tested at later checkpoints, so the current archive hash is expected to change as engine code changes.

After Physics2D landed, the package was rebuilt and revalidated:

- nine of nine Release suites passed;
- the installed folder and a fresh ZIP extraction both completed the 300-tick shipped smoke with replay hash `7074030210802259671`;
- the archive contains exactly the five runtime entries listed above;
- `dumpbin /dependents` reports only `WINMM.dll`, `KERNEL32.dll`, `USER32.dll`, `GDI32.dll`, and `SHELL32.dll`;
- after the kinematic-release correction, the current ZIP is 496,967 bytes with SHA-256 `15993CD5589C7541D4928577E7A1F3EED70991579F542F6459D9088EE48EC7CE`.

After authored scene construction replaced the hard-coded application setup:

- ten of ten Release suites passed;
- the package loaded and validated scene schema 2 before opening the graphics window;
- the installed runtime constructed 14 entities and 10 physics bodies from `Content/test_area.scene`;
- the same 300-tick shipped smoke retained replay hash `7074030210802259671` and passed collision, elevation, contact, trigger, dynamic-prop, and texture-lifetime checks;
- the five-entry ZIP is 544,951 bytes with SHA-256 `CD3A6A54F22DABB20AC37D21BC0DBBD0A13C01BB1D59F0E32B5F651D434828B3`.

After the deterministic runtime-animation foundation landed:

- eleven of eleven Release suites passed;
- the package validated scene schema 3 and instantiated two animated entities;
- the shipped route observed an authored locomotion frame event while preserving replay hash `7074030210802259671`;
- all three authored scene textures were released before graphics shutdown;
- the five-entry ZIP is 565,792 bytes with SHA-256 `CE7316F543A661387526125BF4BAEBD87D64240DED4159FA9989509518EDC4BA`.

## Remaining external check

Run the ZIP on a separate clean Windows 10/11 machine with current graphics drivers. The local package test proves relocation inside this development machine, not the absence of every machine-specific dependency.
