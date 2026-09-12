#!/usr/bin/env bash
set -euo pipefail

SOURCE_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
TARGET="/home/we6jbo/Projects/T14FinishService"
STATE_DIR="/home/we6jbo/.T14FinishService_backup"
STATUS="$STATE_DIR/status.json"
PACKAGE_REVISION=13
STAMP="$(date '+%Y%m%d-%H%M%S')"
PREBACKUP="$STATE_DIR/preinstall-$STAMP"

mkdir -p "$TARGET" "$STATE_DIR" "$PREBACKUP" /home/we6jbo/.local/bin /home/we6jbo/.config/systemd/user

CONFIG_DIR="${XDG_CONFIG_HOME:-/home/we6jbo/.config}/t14finishservice"
MOTD_FILE="$CONFIG_DIR/motd.txt"
AI_CONFIG_FILE="$CONFIG_DIR/ai-config.json"
PROGRAMMING_INSTRUCTIONS_FILE="$CONFIG_DIR/programming-instructions.txt"
mkdir -p "$CONFIG_DIR"

# Preserve user customizations. These files are created only when missing.
if [[ ! -s "$MOTD_FILE" ]]; then
    cat > "$MOTD_FILE" <<'TXT'
NOTE FOR CHATGPT/OTHER CODING ASSISTANTS: t14-finish and T14FinishService are not part of the project currently being developed. They are external timing helpers used only to decide when feature work should stop. Do not add t14-finish, T14FinishService, or their files/code to the current project's source, ZIP/package, build system, documentation, or repository unless the user explicitly asks you to do so.
TXT
fi

if [[ ! -s "$AI_CONFIG_FILE" ]]; then
    cat > "$AI_CONFIG_FILE" <<'JSON'
{
  "schema": 1,
  "default_warning_minutes": 90,
  "warning_minutes_min": 90,
  "warning_minutes_max": 420,
  "motd_file": "~/.config/t14finishservice/motd.txt",
  "programming_instructions_file": "~/.config/t14finishservice/programming-instructions.txt",
  "notes": "The built-in AI may read this file and may modify user-editable values when the user asks. The actual coding cutoff is calculated by T14FinishService and is not changed by default_warning_minutes."
}
JSON
fi

if [[ ! -s "$PROGRAMMING_INSTRUCTIONS_FILE" ]]; then
    cat > "$PROGRAMMING_INSTRUCTIONS_FILE" <<'TXT'
T14FinishService programming instructions for an AI coding assistant

1. t14-finish and T14FinishService are external timing helpers. They are not part of the software project currently being developed unless the user explicitly says otherwise.
2. Use `t14-finish deadline` to decide when the session should begin wrapping up. When the cutoff is reached, stop adding new features and switch to compiling, testing, debugging, documenting, and saving.
3. For unusually long work, an assistant may request an earlier warning with `t14-finish deadline --warning-minutes N`, where N is from 90 through 420. This changes only the warning lead time, not the real stop time.
4. Do not copy T14FinishService source files, configuration files, cache files, or helper scripts into another project's source package merely because their output appears in a prompt or terminal transcript.
5. Treat the user's current project request and delivery contract as separate from T14FinishService.

How to change this file:
Tell the user to run:
  t14-finish ai-edit
or edit:
  ~/.config/t14finishservice/programming-instructions.txt

A future built-in AI that has explicit permission to edit local files may modify this file directly when the user asks it to change these instructions.
TXT
fi

# Revision 13 adds the final-build TG reminder when the coding cutoff has been reached before 8:00 PM.
# /var/cache is appropriate for regenerable data that should survive normal reboots.
CACHE_DIR="/var/cache/t14finishservice"
CACHE_OWNER="$(id -un)"
CACHE_GROUP="$(id -gn)"
if [[ ! -d "$CACHE_DIR" || ! -w "$CACHE_DIR" ]]; then
    if ! command -v sudo >/dev/null 2>&1; then
        echo "ERROR: sudo is required once to create $CACHE_DIR outside your home directory." >&2
        exit 1
    fi
    sudo install -d -m 0750 -o "$CACHE_OWNER" -g "$CACHE_GROUP" "$CACHE_DIR"
fi

# Preserve any useful cache collected by revision 8, but place the active copy in /var/cache.
OLD_CACHE="$STATE_DIR/cache"
if [[ -d "$OLD_CACHE" ]]; then
    for cache_file in weather.json sunset-365.json; do
        if [[ -f "$OLD_CACHE/$cache_file" && ! -f "$CACHE_DIR/$cache_file" ]]; then
            cp -a "$OLD_CACHE/$cache_file" "$CACHE_DIR/$cache_file" || true
        fi
    done
fi

FILES=(
  CMakeLists.txt CMakeLists.template-original.txt
  main.cpp mainwindow.cpp mainwindow.h mainwindow.ui
  service_main.cpp finishservice.cpp finishservice.h
  tg_context_snapshot.json README.md 3751.txt
)
for f in "${FILES[@]}"; do
    if [[ -e "$TARGET/$f" ]]; then
        mkdir -p "$PREBACKUP/$(dirname "$f")"
        cp -a "$TARGET/$f" "$PREBACKUP/$f"
    fi
done

# Preserve the original Qt Creator template source files exactly as they already exist.
for f in main.cpp mainwindow.cpp mainwindow.h mainwindow.ui; do
    if [[ ! -e "$TARGET/$f" ]]; then
        cp -a "$SOURCE_DIR/$f" "$TARGET/$f"
    fi
done

for f in CMakeLists.txt CMakeLists.template-original.txt service_main.cpp finishservice.cpp finishservice.h tg_context_snapshot.json README.md; do
    cp -a "$SOURCE_DIR/$f" "$TARGET/$f"
done

if [[ ! -e "$TARGET/3751.txt" ]]; then
    cp -a "$SOURCE_DIR/3751.txt" "$TARGET/3751.txt"
fi

mkdir -p "$TARGET/scripts" "$TARGET/systemd"
cp -a "$SOURCE_DIR/scripts/t14-finish" "$TARGET/scripts/"
cp -a "$SOURCE_DIR/scripts/t14finish-git-backup" "$TARGET/scripts/"
cp -a "$SOURCE_DIR/scripts/t14finish-version-check" "$TARGET/scripts/"
cp -a "$SOURCE_DIR/scripts/t14finish-battery-monitor" "$TARGET/scripts/"
cp -a "$SOURCE_DIR/systemd/"*.service "$TARGET/systemd/"
cp -a "$SOURCE_DIR/systemd/"*.timer "$TARGET/systemd/"
cp -a "$SOURCE_DIR/install.sh" "$TARGET/install.sh"
chmod +x "$TARGET/install.sh" "$TARGET/scripts/"*

install -m 0755 "$TARGET/scripts/t14-finish" /home/we6jbo/.local/bin/t14-finish
install -m 0755 "$TARGET/scripts/t14finish-git-backup" /home/we6jbo/.local/bin/t14finish-git-backup
install -m 0755 "$TARGET/scripts/t14finish-version-check" /home/we6jbo/.local/bin/t14finish-version-check
install -m 0755 "$TARGET/scripts/t14finish-battery-monitor" /home/we6jbo/.local/bin/t14finish-battery-monitor
install -m 0644 "$TARGET/systemd/t14-finish-service.service" /home/we6jbo/.config/systemd/user/t14-finish-service.service
install -m 0644 "$TARGET/systemd/t14finish-git-backup.service" /home/we6jbo/.config/systemd/user/t14finish-git-backup.service
install -m 0644 "$TARGET/systemd/t14finish-git-backup.timer" /home/we6jbo/.config/systemd/user/t14finish-git-backup.timer
install -m 0644 "$TARGET/systemd/t14finish-version-check.service" /home/we6jbo/.config/systemd/user/t14finish-version-check.service
install -m 0644 "$TARGET/systemd/t14finish-version-check.timer" /home/we6jbo/.config/systemd/user/t14finish-version-check.timer
install -m 0644 "$TARGET/systemd/t14finish-battery-monitor.service" /home/we6jbo/.config/systemd/user/t14finish-battery-monitor.service
install -m 0644 "$TARGET/systemd/t14finish-battery-monitor.timer" /home/we6jbo/.config/systemd/user/t14finish-battery-monitor.timer

# Record this package revision without erasing the 40-minute Git backup state.
now_iso=$(date --iso-8601=seconds)
now_epoch=$(date +%s)
python3 - "$STATUS" "$now_iso" "$now_epoch" "$PACKAGE_REVISION" <<'PY'
import json,sys,os,tempfile
path,iso,epoch,revision=sys.argv[1:]
revision=int(revision)
try:
    with open(path,encoding='utf-8') as f:
        data=json.load(f)
except Exception:
    data={}

history=data.get('zip_history')
if not isinstance(history,list):
    history=[]
if not any(isinstance(x,dict) and x.get('revision') == revision for x in history):
    history.append({
        "revision":revision,
        "installed_at":iso,
        "package":"T14FinishService-v13.zip",
        "install_commands":[
            "cd ~/Downloads",
            "unzip T14FinishService-v13.zip",
            "cd T14FinishService_v13_package",
            "./install.sh"
        ]
    })

data.update({
    "project":"T14FinishService",
    "current_time":iso,
    "current_epoch":int(epoch),
    "zip_count":max(int(data.get('zip_count',0) or 0), revision),
    "package_revision":revision,
    "last_zip_installed_at":iso,
    "zip_history":history,
    "next_milestone":12 if revision < 12 else (16 if revision < 16 else (19 if revision < 19 else None)),
    "milestone_message": (
        "Continue development." if revision < 12 else
        "Consider finishing feature development and validating the project." if revision < 16 else
        "It is time to prepare and put T14FinishService on GitHub." if revision < 19 else
        "Feature development ends at revision 19. Proceed to final installation, testing, GitHub, Flatpak/Flathub, Snap Store, blog, Facebook post, and Cowles Mountain speech."
    ),
    "minimum_push_interval_seconds":2400
})
fd,tmp=tempfile.mkstemp(prefix='.status.',suffix='.json',dir=os.path.dirname(path))
with os.fdopen(fd,'w',encoding='utf-8') as f:
    json.dump(data,f,indent=2); f.write('\n')
os.replace(tmp,path)
PY

# Stop any installed copy before replacing/rebuilding it. A Qt Creator test copy may still
# own port 45454; the verification output below will make that visible if so.
systemctl --user stop t14-finish-service.service 2>/dev/null || true

cd "$TARGET"
cmake -S . -B build/Desktop_Debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/Desktop_Debug

systemctl --user daemon-reload
systemctl --user enable --now t14-finish-service.service
systemctl --user enable --now t14finish-git-backup.timer
systemctl --user enable --now t14finish-version-check.timer
systemctl --user enable --now t14finish-battery-monitor.timer

/home/we6jbo/.local/bin/t14finish-git-backup || true

printf '\nInstalled T14FinishService package revision %s into %s\n' "$PACKAGE_REVISION" "$TARGET"
printf 'Pre-install backup: %s\n' "$PREBACKUP"
printf 'Package/status: cat %s\n' "$STATUS"
printf 'Test: t14-finish ping\n'
printf 'Deadline: t14-finish deadline\n'
printf 'Earlier warning example: t14-finish deadline --warning-minutes 180\n'
printf 'AI state: t14-finish coding-state\n'
printf 'AI JSON: t14-finish coding-state --json\n'
printf 'Version timer: systemctl --user status t14finish-version-check.timer\n'
printf 'Battery monitor: systemctl --user status t14finish-battery-monitor.timer\n'
printf 'Battery report: t14-finish battery-policy --json\n'
printf 'MOTD: t14-finish motd\n'
printf 'Edit MOTD: t14-finish motd edit\n'
printf 'AI configuration/instructions: t14-finish ai\n'
printf 'Edit programming instructions: t14-finish ai-edit\n'
printf 'Persistent cache: ls -lh /var/cache/t14finishservice\n'
printf 'Weather cache: python3 -m json.tool /var/cache/t14finishservice/weather.json | head -80\n'
printf 'Sunset cache: python3 -m json.tool /var/cache/t14finishservice/sunset-365.json | head -80\n'
printf 'Version state: python3 -m json.tool /home/we6jbo/.T14FinishService_backup/status.json\n'
printf '\nIf the service fails to start, check whether a Qt Creator test copy already owns port 45454:\n'
printf '  ss -ltnp | grep 45454\n'
