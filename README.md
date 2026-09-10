# T14FinishService revision 7

T14FinishService is a background-only Qt/C++ service for the Manjaro T14. It supplies coding-session decisions to Bash scripts and AI coding tools through localhost port 45454.

## Revision 7: adaptive battery use

Revision 7 keeps the 55% battery check but changes its meaning. Battery below 55% no longer disables the whole service. Low-cost operations such as reading the local cache, reading the context file, calculating sunset/deadlines, and returning coding-state information remain available. Features that may use more CPU, network, or disk are monitored separately and can be minimized or deferred only after the T14 has collected enough battery-use evidence to show that the feature consumes materially more power than the machine's normal baseline.

A low-overhead user systemd timer samples battery telemetry every 10 minutes. Data is stored in:

`/home/we6jbo/.T14FinishService_backup/battery_usage.json`

A compact current summary is also stored under `battery_monitor` in:

`/home/we6jbo/.T14FinishService_backup/status.json`

The monitor reads battery capacity/status and, when exposed by Linux, instantaneous `power_now`; it falls back to `current_now * voltage_now` when needed. Learning uses discharging samples only so charging behavior does not distort the baseline.

The current learning rule requires at least five baseline samples and five samples for a feature before restricting it. Until enough evidence exists, the feature is allowed rather than being blocked merely because battery is below 55%. Known low-cost local/cache operations are always allowed. A feature is learned as `MINIMIZE` when its median draw is moderately above baseline, and `BLOCK` when it is substantially above baseline. These policies are only enforced below 55%; at or above 55% they resolve to `ALLOW`.

Weather keeps the cache-first behavior from revision 6. If learned telemetry says weather network refresh should be minimized while battery is low, the service first accepts a cached forecast up to 24 hours old. If the learned policy reaches BLOCK, it does not contact the weather service while below 55% and instead uses cached data when available. Sunset remains a local 365-day cache/calculation and does not require Internet access.

Git backups and the weekly j03.page version check now consult the learned battery policy instead of using a blanket 55% cutoff. They record their own battery-use samples around the network/disk activity.

The hard disk-space gate remains unchanged: if less than 10 GiB is free, T14FinishService does not perform normal state/cache writes or high-level work.

## Useful commands

```bash
t14-finish ping
t14-finish status
t14-finish deadline
t14-finish coding-state
t14-finish coding-state --json
t14-finish battery-policy --json
```

Direct battery helper commands:

```bash
t14finish-battery-monitor report
t14finish-battery-monitor policy weather_network
t14finish-battery-monitor policy git_backup
t14finish-battery-monitor policy version_check
```

Timer verification:

```bash
systemctl --user status t14finish-battery-monitor.timer
systemctl --user list-timers | grep t14finish-battery
```

## Existing behavior retained

The service still honors `WE6JBO_CONTEXT_FILE` (or `/home/we6jbo/.local/state/we6jbo-context/context.json`), the hidden-time policy, San Carlos/San Diego weather and sunset rules, the holiday/weekend/weekday/rain schedule, portable TG provenance metadata, 40-minute Git backup workflow, weekly j03.page update check, and package milestone tracking. Original Qt Creator template files remain preserved and are not compiled into the background-only service.

Package revision: 7 of the planned 19-revision development workflow.
