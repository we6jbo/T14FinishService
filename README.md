# T14FinishService v0.9 / Package Revision 9

T14FinishService is a background-only Qt/C++ service for the Manjaro T14. It keeps the existing battery, disk-space, weather, sunset, schedule, coding-state, Git backup, weekly version-check, provenance, and project-milestone behavior from earlier revisions.

## Revision 9: persistent offline cache outside the home directory

Revision 9 moves the active weather/sunset cache outside `/home/we6jbo/` to:

```text
/var/cache/t14finishservice/
```

The installer creates that directory with ownership for the current user. Because `/var/cache` is outside the home directory and normally survives ordinary reboots, the cached information remains available after restarting the T14.

The active cache files are:

```text
/var/cache/t14finishservice/weather.json
/var/cache/t14finishservice/sunset-365.json
```

The installer will copy the old revision-8 cache into `/var/cache/t14finishservice/` if useful files exist and the new cache does not already contain them. The old home-directory cache is not used by revision 9.

## Internet-first behavior

Revision 9 changes the policy from cache-first to Internet-first.

When T14FinishService needs weather or sunset information it normally requests current data from the Internet first. A successful online result is used immediately and also saved to the persistent cache for possible later offline use.

The persistent cache is read only when Internet data is unavailable, times out, returns invalid data, or when the adaptive low-battery policy intentionally suppresses a network request. If no cached sunset exists for an offline date, T14FinishService can still calculate sunset locally using its astronomical calculation.

## Sunset retention

At service startup, T14FinishService maintains at least 365 days of future sunset values in `sunset-365.json`. Existing entries are retained rather than discarded. That means the file gradually contains historical sunset values as time passes while continuing to maintain a forward-looking year of sunset information.

Online forecast sunsets are also persisted in `weather.json` alongside the weather forecast dates returned by the provider.

## Weather retention

When an online forecast succeeds, every forecast date returned by the weather provider is stored in `weather.json`, not only the current day. T14FinishService keeps older entries, so successful forecasts become historical cached records across reboots.

Weather providers do not provide a dependable 365-day future weather forecast. Revision 9 therefore caches as much future weather information as the provider returns and grows historical weather coverage over time rather than fabricating long-range forecasts.

## Existing safety policy

The disk rule remains a hard write-safety gate:

```text
At least 10 GiB free disk space
```

The 55% battery threshold remains adaptive rather than a blanket shutdown. Low-cost operations such as reading local files, reading cache data, and calculating deadlines can continue below 55%. Network, Git, build, and other potentially higher-drain features can be minimized or blocked according to the learned battery policy.

## Install

```bash
cd ~/Downloads
unzip T14FinishService-v9.zip
cd T14FinishService_v9_package
./install.sh
```

The installer may ask for your sudo password once so it can create:

```text
/var/cache/t14finishservice
```

The project itself remains installed at:

```text
/home/we6jbo/Projects/T14FinishService
```

## Verify

```bash
t14-finish ping
t14-finish status
t14-finish deadline
t14-finish coding-state --json
```

Check the persistent cache:

```bash
ls -lh /var/cache/t14finishservice
python3 -m json.tool /var/cache/t14finishservice/sunset-365.json | head -80
python3 -m json.tool /var/cache/t14finishservice/weather.json | head -80
```

After the service has started successfully, `sunset-365.json` should contain at least 365 forward sunset entries. `weather.json` will appear after a successful online weather request.

## Offline test

After the cache has been populated, disconnect the network temporarily and run:

```bash
t14-finish deadline
t14-finish coding-state --json
```

The service should use cached weather/sunset information when that date is present. If cached weather is unavailable, it uses the existing conservative non-rain rule; sunset can still fall back to the local astronomical calculation.

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

This package is revision 9 of the planned 19-revision development workflow. Revision 12 is the next checkpoint, when feature development should begin shifting toward completion and validation.
