# T14FinishService v0.3.1 / package revision 4

T14FinishService is a background-only Qt 6/C++ service for the Manjaro T14. It has no GUI. Bash scripts, ChatGPT/Claude-assisted workflows, and other local tools can query it over localhost TCP port 45454.

The original Qt Creator template source files (`main.cpp`, `mainwindow.cpp`, `mainwindow.h`, and `mainwindow.ui`) remain in the project directory unchanged. The background executable is built from `service_main.cpp`, `finishservice.cpp`, and `finishservice.h`.

## Revision 4 fix

- Adds `t14-finish coding-state` for a simple AI-friendly decision: `KEEP_CODING`, `FINISH_CODING`, `STOP_SAFETY`, or `TIME_HIDDEN`.
- Adds `t14-finish coding-state --json` with the deadline, minutes remaining, battery, disk, weather/rain status, local sunset, reason, and next action.
- Uses a local astronomical sunset calculation for San Carlos / the Cowles Mountain area. Sunset calculation does not require the Internet and works for the 2026-2027 project period.
- Uses Open-Meteo only for the rain forecast. If the weather lookup fails, the service uses the normal non-rain schedule rather than inventing a rain forecast. This is the conservative choice for preserving travel/hiking time.
- Adds a 10-second weather request timeout so a network problem cannot hang the service indefinitely.
- Keeps the 55% battery and 10 GiB free-space safety gates.
- Preserves the WE6JBO context contract. If `time.visible` is false, the service will not expose or infer a clock time or deadline.
- Fixes the Qt/GCC `QTimeZone` vexing-parse build error found in revision 3.
- Records package revision 4 and ZIP milestone progress in `/home/we6jbo/.T14FinishService_backup/status.json` without erasing the 40-minute Git backup state.

## Schedule

Normal deadlines:

- Nov 11, 2026; Nov 23-27, 2026; Dec 21, 2026-Jan 1, 2027; Jan 18, 2027; Mar 29-Apr 5, 2027; May 31, 2027; Saturday; Sunday: sunset minus 250 minutes.
- Monday or Tuesday: sunset minus 225 minutes.
- Wednesday, Thursday, or Friday: sunset minus 235 minutes.

Rain overrides:

- Monday or Tuesday: 6:45 PM.
- Wednesday through Saturday: 4:45 PM.
- Sunday has no special rain override in the supplied rules, so Sunday continues to use sunset minus 250 minutes.

Note: sunset minus 250 minutes means 250 minutes before sunset.

## Install

```bash
cd ~/Downloads
unzip T14FinishService-v4.zip
cd T14FinishService_v4_package
./install.sh
```

The installer updates:

`/home/we6jbo/Projects/T14FinishService`

and backs up package-managed files first under:

`/home/we6jbo/.T14FinishService_backup/preinstall-YYYYMMDD-HHMMSS/`

## Verify

```bash
t14-finish ping
t14-finish status
t14-finish deadline
t14-finish coding-state
t14-finish coding-state --json
cat /home/we6jbo/.T14FinishService_backup/status.json
systemctl --user status t14-finish-service.service
systemctl --user status t14finish-git-backup.timer
ss -ltnp | grep 45454
```

## AI workflow

Before beginning another substantial feature, an AI coding workflow can run:

```bash
t14-finish coding-state
```

- `KEEP_CODING`: another coding task may be started.
- `FINISH_CODING`: stop adding features and switch to compile, test, debug, documentation, and save/backup work.
- `STOP_SAFETY`: stop writes until the battery and disk-space safety gates pass.
- `TIME_HIDDEN`: do not infer a deadline because the context policy hides time.

For machine-readable details:

```bash
t14-finish coding-state --json
```

## Git backup

The user-systemd timer checks every five minutes. A Git commit/push is attempted only after at least 2,400 seconds (40 minutes) have elapsed since the previous successful push. On the first backup it verifies `3751.txt` on `origin/master`; when absent remotely, the local project copy is staged and pushed. The backup helper requires at least 55% battery and 10 GiB free disk.

## Portable TG provenance

Stable project ID: `t14-finish-service-v1`

Embedded codes:

- TG564843
- TG333041
- TG323932
- TG610982
- TG148675

They remain in source metadata, `tg_context_snapshot.json`, status responses, and the best-effort `tg-register-project` invocation. Normal project operation does not require the private TG registry database.
