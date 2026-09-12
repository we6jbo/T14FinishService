# T14FinishService v1.0 / Package Revision 10

T14FinishService is a background-only Qt/C++ service for the Manjaro T14. Revision 10 keeps the existing battery, disk-space, San Carlos weather, sunset, persistent cache, schedule, coding-state, Git backup, weekly update-check, provenance, and milestone behavior while changing how human-facing deadline warnings work.

## Revision 10: stop-coding warning window

The actual coding cutoff is still calculated from the same schedule rules. Revision 10 changes only when the human-facing `deadline` command warns about it.

By default, T14FinishService does **not** display the stop-coding deadline until the cutoff is within 90 minutes. For example, if the calculated stop time is 2:48 PM:

```text
9:00 AM  -> no stop-coding warning yet
1:18 PM  -> warning window begins
2:00 PM  -> warning is displayed
2:48 PM  -> stop adding features immediately
```

Before the warning window, `t14-finish deadline` gives a short message indicating that no finish-up warning is needed yet. It intentionally does not print the future cutoff in that early message.

Inside the warning window it says, in substance:

```text
Warning: we cannot add any more features after 2:48 PM. Once it becomes 2:48 PM, we have to stop adding features and switch to compiling, testing, debugging, documenting, and saving.
```

Once the cutoff has been reached, it says that the cutoff has been reached and that no more features may be added.

## Adjustable warning lead time

The default warning lead time is 90 minutes. It can be changed for a particular request with:

```bash
t14-finish deadline --warning-minutes 180
```

or:

```bash
t14-finish deadline --warning-minutes=180
```

The accepted range is:

```text
minimum: 90 minutes (1 hour 30 minutes)
maximum: 420 minutes (7 hours)
```

Values outside that range are rejected. Changing `--warning-minutes` changes only how early the warning appears. It never changes the real stop time.

The warning text also tells ChatGPT that, when a coding task is unusually long, it may choose a larger warning window in the 90-420 minute range. This lets an assistant warn earlier when wrapping up a large feature will reasonably take longer, while preserving the same hard cutoff.

## Coding-state behavior

The machine-readable state commands continue to expose the actual state independently of whether the human warning is currently visible:

```bash
t14-finish coding-state
t14-finish coding-state --json
```

This is intentional. A coding assistant can continue to use the structured state for automation while the ordinary `deadline` output avoids distracting the user many hours before the cutoff.

At or after the calculated stop time, the state remains `FINISH_CODING`, meaning no new features should be started. At 8:00 PM or later, the existing `AFTER_HOURS` policy remains in effect and no new deadline is created until the next day.

## Persistent cache

The persistent cache remains outside the home directory:

```text
/var/cache/t14finishservice/weather.json
/var/cache/t14finishservice/sunset-365.json
```

Internet data is preferred when available. The cache is used when Internet data is unavailable or when the adaptive low-battery policy suppresses network activity. Sunset coverage continues to maintain at least 365 future dates while retaining historical entries. Weather retains as much future forecast information as the provider supplies and preserves older observations as history.

## Safety behavior

The disk rule remains a hard write-safety gate:

```text
At least 10 GiB free disk space
```

The 55% battery threshold remains adaptive rather than a blanket shutdown. Cheap local work such as cache reads and deadline calculations can continue below 55%, while higher-drain operations may be minimized or blocked according to measured battery behavior.

## Install

```bash
cd ~/Downloads
unzip T14FinishService-v10.zip
cd T14FinishService_v10_package
./install.sh
```

The project remains installed at:

```text
/home/we6jbo/Projects/T14FinishService
```

The persistent cache remains at:

```text
/var/cache/t14finishservice
```

## Verify

```bash
t14-finish ping
t14-finish status
t14-finish deadline
t14-finish deadline --warning-minutes 180
t14-finish coding-state
t14-finish coding-state --json
```

Invalid warning windows can be checked with:

```bash
t14-finish deadline --warning-minutes 89
t14-finish deadline --warning-minutes 421
```

Both should return an error saying the value must be from 90 through 420 minutes.

## Qt Creator templates

The original Qt Creator template files remain preserved:

```text
main.cpp
mainwindow.cpp
mainwindow.h
mainwindow.ui
```

The background service continues to build from `service_main.cpp`, `finishservice.cpp`, and `finishservice.h`.

## Provenance identifiers

The project continues to embed:

```text
TG564843
TG333041
TG323932
TG610982
TG148675
```

and retains `tg_context_snapshot.json` for portable provenance without requiring the private local registry.

## Project milestone

This package is revision 10 of the planned 19-revision workflow. Revision 12 is the next checkpoint, when development should begin shifting from adding features toward completion and validation.
