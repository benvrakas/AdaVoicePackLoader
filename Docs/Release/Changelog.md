# Changelog

## 1.1.4

- Fixes `/adapack` on a dedicated server, which 1.1.3 answered with "/adapack isn't ready yet": SML's command list only
  exists on the server, so the client now runs the command itself.

## 1.1.3

- `/adapack` now works for a player on a dedicated server: the command runs on your own game instead of going to the
  server (which doesn't have this mod and answered "Unknown command").
- The log warns at startup when an installed pack has no line with an event, since the game never plays such a pack by
  itself (only `/adapack play` does).

## 1.1.2 (untested in game)

- `general_status` now fires by itself at random about every 90 minutes (between 45 and 135), only when an installed
  pack has a line for it. An event can have its own timer: set `interval` (seconds) in `Resources/event_rules.json`.

## 1.1.1 (untested in game)

- Works better for a player on a dedicated server: deaths, damage and creature kills are also worked out from the
  replicated health, power fuse trips and factory status are read on the client, and dismantles near the player are
  seen when the buildable is removed. Duplicate firings of the same death are ignored.
- Logs `Factory count: N factories, M idle or blocked` every 5 minutes, to show what the production stall sees.

## 1.1.0 (untested in game)

- New events: `respawn`, `session_resume` (back in the game after 2+ minutes away), `creature_death`, `hazard_death`,
  `power_restored`, `production_building_placed` and `random` (periodic ping). The specific deaths fall back to
  `death`, and `production_building_placed` to `building_placed`, when a pack has no line for them.
- `production_stall` now fires (60%+ of at least 10 factories idle or blocked for a minute), and `research_skip` fires
  when a second MAM research is collected within a minute of the first.
- `Docs/Triggers.md` lists every event.

## 1.0.2 (untested in game)

- Fixes the crash at game launch in 1.0.0 and 1.0.1: the hooks on the game's virtual functions (player and creature
  damage and death, dismantling) weren't given a default object to resolve them through (SML assertion
  `SampleObjectInstance`).

## 1.0.1 (untested in game)

- Event names and their priority, cooldown and chance now live in `Resources/event_rules.json`, the loader's own file,
  instead of being copied into every pack. A pack can still override an event in its pack.json.

## 1.0.0 (untested in game)

- Loads ADA voice pack mods (`Resources/AdaPack/pack.json` + Wwise bank) and plays their lines in ADA's message box,
  on her own voice bus.
- Events: session resume, prolonged idle, random timer, player death and fall death, damage, low health, radiation,
  inventory full, item discard, nightfall, hostile enemies nearby, creature and Lizard Doggo kills, buildings placed,
  large demolitions, tripped power fuses, MAM research, milestones, space elevator, AWESOME Shop purchases, hard drives.
- Weighted line choice, a rare pool, per-event priority, cooldown and chance; death interrupts a quieter line.
- Chat command (single-player or host): `/adapack list`, `/adapack play <pack> <line>`, `/adapack trigger <event>`.
- Not wired yet: production stalls, research skip.
