# T14FinishService

A background-only Qt 6/C++ service for the Manjaro T14. It does not display a GUI.

The original Qt Creator template source files (`main.cpp`, `mainwindow.cpp`, `mainwindow.h`, and `mainwindow.ui`) are retained unchanged in the project directory. The service is built from `service_main.cpp`, `finishservice.cpp`, and `finishservice.h` instead. The original template CMake file is retained as `CMakeLists.template-original.txt`.

## Safety gates

Before normal deadline processing or TG registration, the service requires at least 10 GiB free and a readable battery level of at least 55%.

## Context

Reads `$WE6JBO_CONTEXT_FILE` when set, otherwise `/home/we6jbo/.local/state/we6jbo-context/context.json`. If `time.visible` is false, it does not return a current clock time or calculated deadline.

## Bash client

`~/.local/bin/t14-finish ping`

`~/.local/bin/t14-finish status`

`~/.local/bin/t14-finish deadline`

`~/.local/bin/t14-finish codes`

## GitHub backup helper

The installer creates `/home/we6jbo/.T14FinishService_backup/status.json` and enables a user timer that checks every five minutes. A Git commit/push is attempted only when at least 2,400 seconds (40 minutes) have passed since the prior successful push, or on the first run. The first run verifies `3751.txt` on `origin/master`; if it is absent remotely, the local project contains `3751.txt` so it is included in the first push.

The Git backup helper also requires >=10 GiB free disk and >=55% battery before writing its status or pushing.
