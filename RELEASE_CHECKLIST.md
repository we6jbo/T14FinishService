# T14FinishService Release Candidate Checklist

Revision 15 marks feature development complete. From this point forward, changes should normally be limited to defects, documentation corrections, packaging fixes, and release engineering.

## Local validation

Run:

```bash
t14finish-release-check
```

The check verifies the project files, required provenance files/codes, helper scripts, systemd units, persistent cache location, user configuration, localhost service, command interface, build output, and Git repository state without modifying the repository.

## Required functional checks

```bash
t14-finish ping
t14-finish status
t14-finish deadline
t14-finish coding-state
t14-finish coding-state --json
t14-finish battery-policy --json
t14-finish ai
t14finish-9579-check
```

Also confirm that a low-battery state still permits cheap local/cache operations while expensive optional operations follow the learned battery policy.

## Release sequence

1. Resolve any FAIL results from `t14finish-release-check`.
2. Build successfully in Qt Creator and from CMake/Ninja.
3. Confirm the systemd user service survives a restart/re-login test.
4. Confirm Git status contains only intended release files.
5. Revision 16 begins GitHub publication/repository cleanup.
6. After GitHub, prepare Flatpak/Flathub and Snap packaging/submission.
7. Perform the final installed-machine acceptance test.
8. Complete the j03.page article, social posts, and Cowles Mountain spoken summary.

Store-review approval can occur later; the release workflow can verify that packages were built/submitted without claiming that a third-party store approved them immediately.
