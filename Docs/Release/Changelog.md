# Changelog

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
