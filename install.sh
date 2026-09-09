#!/usr/bin/env bash
set -euo pipefail

SOURCE_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
TARGET="/home/we6jbo/Projects/T14FinishService"
STATE_DIR="/home/we6jbo/.T14FinishService_backup"
STATUS="$STATE_DIR/status.json"
PACKAGE_REVISION=4
STAMP="$(date '+%Y%m%d-%H%M%S')"
PREBACKUP="$STATE_DIR/preinstall-$STAMP"

mkdir -p "$TARGET" "$STATE_DIR" "$PREBACKUP" /home/we6jbo/.local/bin /home/we6jbo/.config/systemd/user

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
cp -a "$SOURCE_DIR/systemd/"*.service "$TARGET/systemd/"
cp -a "$SOURCE_DIR/systemd/"*.timer "$TARGET/systemd/"
cp -a "$SOURCE_DIR/install.sh" "$TARGET/install.sh"
chmod +x "$TARGET/install.sh" "$TARGET/scripts/"*

install -m 0755 "$TARGET/scripts/t14-finish" /home/we6jbo/.local/bin/t14-finish
install -m 0755 "$TARGET/scripts/t14finish-git-backup" /home/we6jbo/.local/bin/t14finish-git-backup
install -m 0644 "$TARGET/systemd/t14-finish-service.service" /home/we6jbo/.config/systemd/user/t14-finish-service.service
install -m 0644 "$TARGET/systemd/t14finish-git-backup.service" /home/we6jbo/.config/systemd/user/t14finish-git-backup.service
install -m 0644 "$TARGET/systemd/t14finish-git-backup.timer" /home/we6jbo/.config/systemd/user/t14finish-git-backup.timer

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
        "package":"T14FinishService-v4.zip",
        "install_commands":[
            "cd ~/Downloads",
            "unzip T14FinishService-v4.zip",
            "cd T14FinishService_v4_package",
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

/home/we6jbo/.local/bin/t14finish-git-backup || true

printf '\nInstalled T14FinishService package revision %s into %s\n' "$PACKAGE_REVISION" "$TARGET"
printf 'Pre-install backup: %s\n' "$PREBACKUP"
printf 'Package/status: cat %s\n' "$STATUS"
printf 'Test: t14-finish ping\n'
printf 'Deadline: t14-finish deadline\n'
printf 'AI state: t14-finish coding-state\n'
printf 'AI JSON: t14-finish coding-state --json\n'
printf '\nIf the service fails to start, check whether a Qt Creator test copy already owns port 45454:\n'
printf '  ss -ltnp | grep 45454\n'
