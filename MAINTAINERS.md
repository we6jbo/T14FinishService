# T14FinishService Maintenance Guide

This file explains where each part of T14FinishService lives and what to edit when changing behavior. The original Qt Creator template files `main.cpp`, `mainwindow.cpp`, `mainwindow.h`, and `mainwindow.ui` are intentionally preserved unchanged and are not compiled into the background service.

## Runtime architecture

`service_main.cpp` is the executable entry point. With no arguments it starts the Qt `QCoreApplication` background service. With arguments it delegates to the installed `~/.local/bin/t14-finish` helper, so a Qt Creator build can be used as `./T14FinishService ai`, `./T14FinishService deadline`, and similar commands.

`finishservice.h` declares the service state structures and helper methods. `finishservice.cpp` implements localhost TCP port 45454, context loading, battery/disk policy, sunset calculation/cache, weather/cache, deadline rules, TG registration, and responses to socket commands. Keep protocol changes synchronized with `scripts/t14-finish`.

## User-facing helper scripts

`scripts/t14-finish` is the normal command users call. It manages the MOTD, AI configuration, AI programming instructions, and forwards runtime commands to the background service.

`scripts/t14finish-battery-monitor` records battery telemetry and computes ALLOW/MINIMIZE/BLOCK policy hints for potentially expensive features.

`scripts/t14finish-git-backup` handles the approximately 40-minute Git backup workflow. It must not commit unrelated files outside the T14FinishService repository.

`scripts/t14finish-version-check` performs the weekly j03.page version check.

`scripts/t14finish-9579-check` verifies that local `9579.txt` contains the expected historical quote and checks the public GitHub `master` branch for the same file/quote. It reports a notice only; it never pushes automatically.

## Persistent data

`~/.config/t14finishservice/` contains user-editable MOTD, AI configuration, and programming instructions. Installers preserve these files when they already exist.

`/var/cache/t14finishservice/` contains regenerable persistent weather and sunset cache data. Internet data is preferred; cache is used for offline/low-power fallback according to service policy.

`/home/we6jbo/.T14FinishService_backup/status.json` tracks package revision/milestones and Git-backup state. Do not erase unrelated keys when updating it.

## systemd

`systemd/t14-finish-service.service` starts the background service. Timer/service pairs run Git backup, version checks, and battery monitoring. After editing unit files, reinstall them and run `systemctl --user daemon-reload`.

## Build system

`CMakeLists.txt` compiles only the background-service sources with Qt Core and Qt Network. The original Qt Widgets template files remain in the tree for provenance/template preservation but are deliberately excluded.

## Publication marker files

`3751.txt` is the earlier Git-backup marker. `9579.txt` is the later publication marker requested for revision 14. Neither file changes runtime timing behavior.

## Safe modification checklist

1. Preserve the 10 GiB hard write-safety requirement.
2. Preserve adaptive low-battery behavior instead of reintroducing a blanket under-55% shutdown.
3. Honor `WE6JBO_CONTEXT_FILE` and the hidden-time policy.
4. Keep localhost-only networking unless explicitly redesigned.
5. Preserve user configuration files across installs.
6. Build with Qt Creator or CMake/Ninja, then test `t14-finish ping`, `deadline`, `coding-state --json`, and `ai`.
7. If changing a shell command exposed through the C++ executable, test both `t14-finish <command>` and `./T14FinishService <command>`.
