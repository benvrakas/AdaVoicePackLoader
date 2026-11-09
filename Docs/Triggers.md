# Triggers

A pack line's `trigger` column (in `D:\SFMod\Tools\AdaPacks\packs\<id>\lines.csv`) names the event that makes it play.
`name` or `name:argument` (the argument is a number). A blank trigger means the line only plays from `/adapack play`.

When a trigger fires, the loader collects the lines of every installed pack with that trigger, drops those cooling down
(`cooldown`) or already played (`once`), picks one at random, rolls its `chance`, and queues it in ADA's message box.
Only one line plays at a time; a line is refused (and the refusal logged once) while another line or one of the game's
own ADA messages is playing, while the game is in the background, or within the pack's `minGapSeconds` of the last line.

## Built in

| Trigger | Fires |
|---|---|
| `world_loaded` | 20 s after the player's pawn exists in a world, once per world load |
| `idle:<seconds>` | the player hasn't moved (more than 1 m) or turned (more than 5 degrees) for that long, outside menus; once per idle stretch for each distinct number |
| `random:<seconds>` | at random times, on average that many seconds apart (between 0.5x and 1.5x); each distinct number has its own countdown |

`/adapack trigger <name> [argument]` fires a trigger by hand for testing.

## Adding a trigger

A trigger is a call to `UAdaVoicePlayer::Fire(Name, Arg)` from the place that detects the event: a polled check in
`UAdaVoicePlayer::PollTriggers`, or an SML hook in `FAdaVoicePackLoaderModule::StartupModule`. Add the row to the table
above when you add one. Events that happen to the player must be detected on the player's own machine (the mod is
client-side).
