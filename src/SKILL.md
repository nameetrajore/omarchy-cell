---
name: cell
description: Inspect laptop battery health and power consumption with Cell, or explicitly collect local battery history.
---

Use `cell status --json` for current measurements (no history write).
Missing numeric measurements are -1. Power is a positive magnitude in watts;
`state` determines charging/discharging. Health is full/design capacity × 100.

`cell collect --json` records every 30 seconds until stopped and prints JSON lines.
Only run this persistent collector when requested; never enable autostart implicitly.
The development checkout has `bin/record enable|disable` for an opt-in systemd user
service. Check `systemctl --user is-active cell.service` before starting an extra
collector. Recording continues with the GUI closed when this service is enabled,
starts at login, and pauses during system sleep.
`cell open` also records while the window is open. Both use the same local SQLite database
under Qt's application data directory (`~/.local/share/cell/cell/history.sqlite`
with default XDG settings). Duplicate device/30-second buckets are ignored.

Use `cell report --json` for a read-only, bounded summary: `battery`, recorded
`context`, `analysis`, recent graph points and five discharge sessions. Check
`stale` before interpreting history. `analysis.status` is `warming`, `no_matches`,
`ready` or `stale`. Missing legacy context is not backfilled.

New samples retain your user-owned process names and CPU shares, plus aggregate
CPU, backlight brightness, profile and display connections. No titles, domains,
command lines or executable paths. Process CPU shares are not watts.

Comparisons require at least three five-minute windows from earlier discharge
sessions with the same profile/displays and mean CPU/brightness within 10 pp.
Correlations and profile averages are unadjusted associations, not causes or
promises of battery savings. Never claim this controls for GPU or all workloads.

Session energy integrates whole-battery power, not per-process consumption.
Charging, device changes, or gaps over 90 seconds split sessions.
Do not claim exact application-level watts, diagnose battery failure from a single
health value, or change charge thresholds or power settings without approval.
