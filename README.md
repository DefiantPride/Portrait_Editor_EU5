# ACE Portrait Bridge

Edit any character's face in **Europa Universalis V** using the game's own
built-in portrait editor, and make the change stick.

Open a character, press a hotkey, their face loads into the editor. Change
whatever you like, press another hotkey, save the game. Done.

Works on every character in your save — rulers, courtiers, spouses, children,
randomly generated nobodies. Not just the scripted historical figures.

---

## Requirements

- **Europa Universalis V 1.3.11 "Pavia"**, launcher checksum **6210**.
  Other versions are refused at startup — see *Version lock* below.
- Windows x64.
- **Single-player only.** See *Before you use this*.

---

## Install

1. Download the release and unzip it anywhere.
2. That's it. Nothing goes in your mod folder.

Debug mode is required. Tell the loader where the game is, once:

```
ace_bridge_loader.exe --setup "D:\Steam\steamapps\common\Europa Universalis V\binaries\eu5.exe"
```

after which `ace_bridge_loader.exe --launch` starts the game with
`-debug_mode` and injects when it comes up. Or set `-debug_mode` yourself in
Steam's launch options and just run the loader with no arguments.

---

## Use

1. Start EU5 and load a save.
2. Run **`ace_bridge_loader.exe`**. It finds the game and loads the bridge.
   Do this once per game launch.
3. Open the character you want to change — click through to their
   **character panel**, not just the hover card.
4. Open the portrait editor: press `` ` `` for the console, type `pe`, Enter.
5. **`Ctrl+Shift+L`** — their face loads into the editor.
6. Edit however you like.
7. **`Ctrl+Shift+S`** — the character now has that face.
8. **Save the game.** Nothing is permanent until you do.
9. To see the new face in game: close and reopen their panel, or let a day
   pass.

### Hotkeys

| | |
|---|---|
| `Ctrl+Shift+L` | character → editor |
| `Ctrl+Shift+S` | editor → character |
| `Ctrl+Shift+R` | forget cached addresses (use after closing and reopening the editor) |

### The first press takes a moment

The first `Ctrl+Shift+L` of a session spends a few seconds locating the editor
and the character panel in memory. Everything after that is instant, because
both addresses are remembered and revalidated. If the editor is closed and
reopened it will find it again by itself.

---

## Is it working?

Everything is written to **`ace_portrait_bridge.log`**, next to `eu5.exe`.
A healthy session starts like this:

```
=== ACE Portrait Bridge ===
eu5.exe base = 00007FF7...
verifying build signatures...
  guard ok: CCharacter::GetDna prologue (encodes +0x148)
  guard ok: CDnaString::ToBase64String prologue (encodes +0x08)
ready.
```

and a transfer looks like this:

```
--- LOAD: character -> editor ---
  editor 000001F2...  102 genes
  character 0x34C at ...  102 genes
  816 bytes copied. gene0 {0 0} -> {242 15}
  expect Color Coordinates X 94.9% / Y 5.9%
```

The editor's **Color Coordinates** readout is the reliable check — compare it
against that last line. Don't judge by the face: the editor previews at its own
Age slider and a generic ethnicity template, so it never looks exactly like the
in-game portrait even when the genes are identical.

---

## Before you use this

**Back up your save folder.** `Documents\Paradox Interactive\Europa Universalis V\save games`

**Single-player, offline.** This loads code into the game process. It is not
part of the launcher checksum, so in multiplayer it is a desync risk and a bad
idea generally. Achievements are already off if you run mods.

**Your antivirus may flag the loader.** Injecting a DLL into another process is
also what malware does, so the technique gets flagged regardless of intent.
The full source is included — build it yourself if you would rather not trust
a binary. That is the honest recommendation.

---

## Version lock

Every memory offset in this tool was derived from one specific build of
`eu5.exe`. On any other build they would point at the wrong things.

So before doing anything, the bridge reads the first few bytes of two known
functions and checks they are what it expects. Those bytes literally encode the
offsets the tool depends on. If they do not match, it logs `BUILD MISMATCH`
and refuses to read or write anything at all.

**A game patch will break this**, and it will break loudly rather than
silently corrupting a character. Offsets have to be re-derived by hand
afterwards; `DEVELOPERS.md` explains how they were found.

---

## Troubleshooting

| Log message | What to do |
|---|---|
| `BUILD MISMATCH` | Your EU5 is not 1.3.11 / checksum 6210. Nothing was touched. |
| `portrait editor is not open` | Open it — console: `pe` |
| `no character panel open` | Open a character's full panel, not the hover card. |
| `character has no DNA yet` | View that character's portrait in game once, then retry. |
| `ungenerated DNA (hash 0)` | Same — the game hasn't generated their face yet. |
| `editor moved, rescanning` | Normal after reopening the editor. It recovers itself. |
| `gene count mismatch` | Shouldn't happen. Please report it with the log. |

**Loader says the game isn't running** — start EU5 and load a save first.

**Loader can't open the process** — run it as administrator.

**Hotkeys do nothing** — check the log exists and shows `ready.` If not, the
DLL never loaded. If it does, make sure both the character panel and the editor
are open.

**Duplicate lines in the log** — you injected twice. Restart the game.

---

## What it does not do

- It does not save your game for you.
- It does not redraw the portrait immediately. Reopen the panel or let a day
  pass.
- It does not change hair or beard style. Those come from the game's portrait
  modifiers, not from the face DNA — which is why pasted faces sometimes look
  bald in the editor.
- It calls no game functions. Every operation is a plain memory read or write.

---

## Building it yourself

Install the free **Build Tools for Visual Studio** with the C++ workload, open
an **x64 Native Tools Command Prompt for VS**, and run:

```
build.bat
```

You get `build\ace_portrait_bridge.dll` and `build\ace_bridge_loader.exe`.

---

## Copyright (c) 2026 murad

MIT — see Copyright (c) 2026 murad.

## Full documentation

**`USAGE.md`** is the detailed guide — every step, and every case where the
tool will not work. Read §5 before you use it on a courtier; the tool can pick
the wrong character in a specific, known situation.

`DEVELOPERS.md` covers the memory layout, why the design is what it is, and
what is verified versus assumed.
