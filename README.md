# ADA Voice Pack Loader

Plays ADA voice lines from separate pack mods, in ADA's own message box and voice, when things happen in the game.
Install this mod and any pack mods (each depends on this one). Only the player who wants the lines needs them.

- [Docs/Architecture.md](Docs/Architecture.md): how packs are loaded and played.
- [Docs/Triggers.md](Docs/Triggers.md): the events that start a line, and how to add one.
- Packs are made with `D:\SFMod\Tools\AdaPacks\adapack.py` (see its docstring).

Chat command (single-player or host): `/adapack list`, `/adapack play <pack> <line>`, `/adapack trigger <name> [arg]`.

The voice lines are AI-generated speech (Google Cloud Text-to-Speech).
