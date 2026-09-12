# T14FinishService revision 11

Revision 11 adds an explicit integration note to every `t14-finish deadline` response. The note tells ChatGPT and other coding assistants that `t14-finish` / T14FinishService is an external timing helper only and is **not** part of whichever software project is currently being developed. It must not be copied into that project's source tree, ZIP/package, build configuration, documentation, or repository unless the user explicitly requests that integration. This is specifically intended for workflows where an install script runs `t14-finish deadline` and its terminal output is pasted into a new coding chat.

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

This package is revision 10 of the planned 19-revision workflow. Revision 13 is the next checkpoint, when development should begin shifting from adding features toward completion and validation.

## Revision 13: configurable MOTD and AI handoff

Revision 13 makes the external-helper message user-editable and adds an AI-facing configuration handoff.

The user-editable files live under:

```text
~/.config/t14finishservice/
  motd.txt
  ai-config.json
  programming-instructions.txt
```

`t14-finish motd` shows the current message and how to change it. Use `t14-finish motd edit`, `t14-finish motd set "..."`, or `t14-finish motd reset` to manage the message. The service reads `motd.txt` when it produces deadline output, so future ChatGPT/Claude prompts can carry the customized explanation that T14FinishService is external to the project being developed.

`t14-finish ai` prints the current editable configuration, MOTD, programming instructions, their file paths, and directions an AI can give the user for changing them. If `programming-instructions.txt` does not exist, the command creates a documented starter file automatically. `t14-finish ai-edit` opens that instructions file using `$VISUAL`, `$EDITOR`, or `nano`.

The `default_warning_minutes` value in `ai-config.json` controls the default lead time used by plain `t14-finish deadline`. Valid values remain 90 through 420 minutes. Explicit `--warning-minutes` arguments still override it for one command only.

Revision 13 is the planned project checkpoint: consider feature development substantially complete and concentrate increasingly on validation, reliability, build/install testing, recovery behavior, and documentation before the later GitHub/publication milestones.

## Revision 13 cutoff reminder

When `t14-finish deadline` is run after the coding deadline has been reached but before the 8:00 PM after-hours cutoff, the response now also says:

`Before creating the final build, add TG564843,TG333041,TG323932,TG610982,TG148675 to the project.`

This reminder appears only in the reached-deadline state. It does not appear in the early-warning state, the no-warning-yet state, or the 8:00 PM-and-later after-hours state.
