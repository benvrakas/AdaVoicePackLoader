# Events

An event is the name a pack line is attached to. When the game does something that matches, the loader collects the
installed packs' lines for that event, drops the ones cooling down, picks one by weight, rolls the chances and queues it
in ADA's message box. The list below is generated from `Resources/event_rules.json`, the single source for names and
rules (a pack can override one in its pack.json `events`).

| Event | Fires when | Fires in game | Priority | Event cooldown | Chance |
|---|---|---|---|---|---|
| `death` | The player dies (any cause). | yes | 100 | 60 s | 100% |
| `fall_death` | The player dies from a fall; plays instead of death if it has a line, else death plays. | yes | 100 | 60 s | 100% |
| `power_failure` | A power circuit trips its fuse (single-player or the host). | yes | 70 | 120 s | 100% |
| `production_stall` | Most factories (60% or more of at least 10) are idle or blocked for a full minute (single-player or the host). | yes | 50 | 300 s | 50% |
| `building_placed` | The player or a friend finishes building something. | yes | 20 | 600 s | 10% |
| `building_dismantled` | A demolition: 4 or more buildings dismantled within 10 seconds. | yes | 20 | 900 s | 10% |
| `creature_kill` | The player kills a creature. | yes | 20 | 300 s | 15% |
| `doggo_kill` | The player kills a Lizard Doggo. | yes | 30 | 300 s | 50% |
| `research_complete` | A MAM research is finished and collected. | yes | 40 | 60 s | 100% |
| `research_skip` | A second MAM research is collected within a minute of the first (research overload). | yes | 90 | 60 s | 100% |
| `milestone` | A HUB milestone is delivered. | yes | 70 | 60 s | 100% |
| `space_elevator` | A Space Elevator phase is delivered. | yes | 90 | 60 s | 100% |
| `coupon` | An AWESOME Shop purchase. | yes | 35 | 120 s | 70% |
| `hard_drive` | A hard drive research is collected. | yes | 50 | 60 s | 100% |
| `damage` | The player takes damage. | yes | 10 | 600 s | 5% |
| `low_health` | The player drops below 25% health. | yes | 55 | 180 s | 100% |
| `radiation` | The player enters radiation. | yes | 50 | 180 s | 100% |
| `hostile_zone` | The player comes within 60 m of an enemy. | yes | 35 | 300 s | 50% |
| `nightfall` | Day turns to night. | yes | 20 | 600 s | 30% |
| `idle` | The player is idle for 10 minutes (use idle:<seconds> in a CSV events cell for another length). | yes | 15 | 600 s | 100% |
| `world_loaded` | The player resumes a saved game (20 seconds after loading). | yes | 30 | 0 s | 100% |
| `inventory_full` | The player inventory becomes full. | yes | 15 | 300 s | 50% |
| `item_discard` | The player puts something in the trash slot. | yes | 10 | 300 s | 20% |
| `general_status` | Fires by itself at random about every 90 minutes (between 45 and 135), only if a pack has a line for it; also by hand with /adapack trigger general_status. | yes | 5 | 1800 s | 100% |
| `respawn` | The player respawns after dying. | yes | 60 | 30 s | 100% |
| `session_resume` | The player returns to the game after 2 or more minutes in the background. | yes | 30 | 300 s | 100% |
| `creature_death` | The player dies to a creature; plays instead of death if it has a line, else death plays. | yes | 100 | 60 s | 100% |
| `hazard_death` | The player dies to an environmental hazard (radiation, gas, fire, drowning); falls back to death like creature_death. | yes | 100 | 60 s | 100% |
| `power_restored` | A tripped power fuse is reset. | yes | 40 | 120 s | 100% |
| `production_building_placed` | A production building (a machine) is finished; falls back to building_placed if it has no line. | yes | 25 | 600 s | 15% |
| `random` | A periodic ping at random times; write random:<seconds> for the average gap (very low frequency recommended). | yes | 5 | 0 s | 100% |

- **Priority:** 90 and up interrupts a quieter line that is playing (research overload, space elevator).
- **Event cooldown:** shortest time between two firings of the event. **Chance:** how often a firing plays anything.
- **Rare-pool lines** (`pool=rare`) also wait 30 minutes between plays and play a quarter as often.
- `idle:<seconds>` and `random:<seconds>` take a number in the CSV's events cell (`idle:300`).
- **Specific deaths:** `fall_death`, `creature_death` and `hazard_death` fall back to `death` when the pack has no line
  for them (or the specific event's cooldown or chance stops it), so `death` lines always cover every kind of death.
  `production_building_placed` falls back to `building_placed` the same way.
- Only one line plays at a time, and never while one of the game's own ADA messages is playing or the game is in the
  background.
- **On a dedicated server** the loader only runs on each player's own game. Player events, power fuses, kills near the
  player and dismantles near the player are read from state the game replicates to clients; `production_stall` counts
  only the factories near the client (the log's "Factory count" lines show what it sees). The game's own damage and
  death functions may not run on a client, so deaths, damage and kills are also worked out from replicated health.

`/adapack trigger <event> [argument]` fires an event by hand for testing; `/adapack list` and `/adapack play <pack> <line>`
list and play lines.

## Adding an event

Add it to `Resources/event_rules.json` (key, description, rule, a heading phrase if triggers files use prose), then detect
it: a hook in `UAdaGameEvents::RegisterHooks`, a delegate in `BindDelegates`, or a poll in `PollPlayer`/`PollWorld`, each
calling `Fire("<key>")`. Hooks on virtual functions need `SUBSCRIBE_METHOD_VIRTUAL_AFTER` with the class's default object.
