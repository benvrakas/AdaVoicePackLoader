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
| `production_stall` | A factory stops producing or a belt backs up. | not yet | 50 | 300 s | 50% |
| `building_placed` | The player or a friend finishes building something. | yes | 20 | 600 s | 10% |
| `building_dismantled` | A demolition: 4 or more buildings dismantled within 10 seconds. | yes | 20 | 900 s | 10% |
| `creature_kill` | The player kills a creature. | yes | 20 | 300 s | 15% |
| `doggo_kill` | The player kills a Lizard Doggo. | yes | 30 | 300 s | 50% |
| `research_complete` | A MAM research is finished and collected. | yes | 40 | 60 s | 100% |
| `research_skip` | Research skipped or overloaded. | not yet | 90 | 60 s | 100% |
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
| `general_status` | Never fires by itself; only /adapack trigger general_status or a random timer. | yes | 5 | 1800 s | 5% |

- **Priority:** 90 and up interrupts a quieter line that is playing (death, research skip, space elevator).
- **Event cooldown:** shortest time between two firings of the event. **Chance:** how often a firing plays anything.
- **Rare-pool lines** (`pool=rare`) also wait 30 minutes between plays and play a quarter as often.
- `idle:<seconds>` and `random:<seconds>` take a number in the CSV's events cell (`idle:300`).
- Only one line plays at a time, and never while one of the game's own ADA messages is playing or the game is in the
  background. World events (power, buildings, kills) are detected on the machine running the factory.

`/adapack trigger <event> [argument]` fires an event by hand for testing; `/adapack list` and `/adapack play <pack> <line>`
list and play lines.

## Adding an event

Add it to `Resources/event_rules.json` (key, description, rule, a heading phrase if triggers files use prose), then detect
it: a hook in `UAdaGameEvents::RegisterHooks`, a delegate in `BindDelegates`, or a poll in `PollPlayer`/`PollWorld`, each
calling `Fire("<key>")`. Hooks on virtual functions need `SUBSCRIBE_METHOD_VIRTUAL_AFTER` with the class's default object.
