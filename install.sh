#!/usr/bin/env bash
set -euo pipefail

SOURCE_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
TARGET="/home/we6jbo/Projects/T14FinishService"
STATE_DIR="/home/we6jbo/.T14FinishService_backup"
STAMP="$(date '+%Y%m%d-%H%M%S')"
PREBACKUP="$STATE_DIR/preinstall-$STAMP"

mkdir -p "$TARGET" "$STATE_DIR" "$PREBACKUP" /home/we6jbo/.local/bin /home/we6jbo/.config/systemd/user

# Preserve the user's current project files before replacing package-managed files.
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

# Keep the original Qt Creator template source files exactly as supplied if they already exist.
for f in main.cpp mainwindow.cpp mainwindow.h mainwindow.ui; do
    if [[ ! -e "$TARGET/$f" ]]; then
        cp -a "$SOURCE_DIR/$f" "$TARGET/$f"
    fi
done

# Install the background-service additions and build definition.
for f in CMakeLists.txt CMakeLists.template-original.txt service_main.cpp finishservice.cpp finishservice.h tg_context_snapshot.json README.md; do
    cp -a "$SOURCE_DIR/$f" "$TARGET/$f"
done

# Never replace an existing 3751.txt; create it only when missing.
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

# Initialize status.json with the current local time. Preserve a prior last-push timestamp if one exists.
now_iso=$(date --iso-8601=seconds)
now_epoch=$(date +%s)
python3 - "$STATE_DIR/status.json" "$now_iso" "$now_epoch" <<'PY'
import json,sys,os,tempfile
path,iso,epoch=sys.argv[1:]
try:
    with open(path,encoding='utf-8') as f:
        old=json.load(f)
except Exception:
    old={}
data={
  "current_time": iso,
  "current_epoch": int(epoch),
  "last_push_time": old.get("last_push_time"),
  "last_push_epoch": int(old.get("last_push_epoch",0) or 0),
  "push_due": int(epoch)-int(old.get("last_push_epoch",0) or 0) >= 2400 if int(old.get("last_push_epoch",0) or 0) else True,
  "minimum_push_interval_seconds": 2400
}
fd,tmp=tempfile.mkstemp(prefix='.status.',suffix='.json',dir=os.path.dirname(path))
with os.fdopen(fd,'w',encoding='utf-8') as f:
    json.dump(data,f,indent=2); f.write('\n')
os.replace(tmp,path)
PY

cd "$TARGET"
cmake -S . -B build/Desktop_Debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/Desktop_Debug

systemctl --user daemon-reload
systemctl --user enable --now t14-finish-service.service
systemctl --user enable --now t14finish-git-backup.timer

# Run the backup checker once now; it will only push if due.
/home/we6jbo/.local/bin/t14finish-git-backup || true

printf '\nInstalled T14FinishService into %s\n' "$TARGET"
printf 'Pre-install backup: %s\n' "$PREBACKUP"
printf 'Test with: t14-finish ping\n'
printf 'Deadline:  t14-finish deadline\n'
printf 'Backup status: cat %s/status.json\n' "$STATE_DIR"
