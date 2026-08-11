# USAGE — ACE Portrait Bridge

Everything you need to run it, and every case where it will not work.

---

## 1. Requirements

| | |
|---|---|
| Game | Europa Universalis V **1.3.11 "Pavia"**, launcher checksum **6210** |
| OS | Windows x64 |
| Mode | **Single-player only** |
| Debug mode | **Required** |

Any other game version is refused at startup — see §7.

---

## 2. Debug mode

The tool needs it, for two reasons:

- The portrait editor is opened from the console (`pe`)
- The DEBUG hover box that shows a character's `ID:` only appears in debug mode,
  and that ID is how you confirm the right character was picked

Two ways to turn it on. **The loader can do it for you** — tell it once where
the game is:

```
ace_bridge_loader.exe --setup "D:\Steam\steamapps\common\Europa Universalis V\binaries\eu5.exe"
```

then from now on:

```
ace_bridge_loader.exe --launch
```

That starts the game with `-debug_mode` and injects once it is up.

**Or set it in Steam yourself:** right-click Europa Universalis V → Properties →
General → Launch Options → `-debug_mode`

> If `-debug_mode` does not enable the console on your build, try `-debug`.
> Paradox has used both across titles and I have not verified which EU5 accepts.

---

## 3. Normal session

1. Start the game (`--launch`, or manually with debug mode on) and load a save.
2. Run `ace_bridge_loader.exe`. **Once per game launch.**
3. Open the character you want — their **character panel**, not the hover card.
   Use the "Open Character Panel" button on the hover card.
4. Open the portrait editor: console (`` ` ``) → `pe` → Enter.
5. **`Ctrl+Shift+L`** — their face loads into the editor.
6. Edit it.
7. **`Ctrl+Shift+S`** — the character gets that face.
8. **Save the game.**
9. To see it in game: close and reopen their panel, or close the editor, or let
   a day pass.

### Several characters at once

Changes accumulate in memory, so one save covers all of them:

```
character A panel → Ctrl+Shift+L → edit → Ctrl+Shift+S
character B panel → Ctrl+Shift+L → edit → Ctrl+Shift+S
character C panel → Ctrl+Shift+L → edit → Ctrl+Shift+S
save once
```

Press `Ctrl+Shift+S` **before** loading the next character. `Ctrl+Shift+L`
overwrites the editor, so an unsaved edit is gone at that point.

### Hotkeys

| | |
|---|---|
| `Ctrl+Shift+L` | character → editor |
| `Ctrl+Shift+S` | editor → character |
| `Ctrl+Shift+R` | forget the cached editor address |

---

## 4. Confirming it did the right thing

Everything goes to **`ace_portrait_bridge.log`**, next to `eu5.exe`.

```
--- LOAD: character -> editor ---
  scan: 3125 ms, 9692 MB  editor=found  contexts=2 (rejected 0)
    context[0] 000002D8116E8B60  id 0x1E8D   <-- chosen
    context[1] 000002D7F31DE5B0  id 0x1E8D
  2 of 2 contexts agree on character 0x1E8D
  816 bytes copied. gene0 {0 0} -> {100 48}
  expect Color Coordinates X 39.2% / Y 18.8%
```

**Two things to check.**

The **`id`** against the `ID:` in the game's DEBUG hover box. The log is in hex,
the game shows decimal — `0x1E8D` is 7821. This matters; see §5.

The **Color Coordinates** panel in the editor against that last line. That is
the reliable signal. Do not judge by the face: the editor previews at its own
Age slider and a generic ethnicity template, so it never looks quite like the
in-game portrait even with identical genes.

---

## 5. Restrictions and known failure cases

### It can pick the wrong character

**This is the significant one.** The tool finds the open panel by scanning for
context objects and taking the character id that the most of them agree on.
Every portrait widget on screen has one of these — spouses, children, hover
cards, the top bar.

A **ruler's** portrait is rendered persistently all over the UI, so the ruler
usually has the most widgets alive. Editing a **courtier** while their ruler is
also on screen can therefore select the ruler instead.

Observed: opening Gertrud von Cassel (ID 21519) picked Wilhelm von Hessen
(ID 7781), the ruler of that county, 4 contexts to 3.

**Workaround:** let a day tick in game between characters. That lets stale
widget contexts get recycled so the count reflects what is actually open.

**Always check the id in the log before you save.** If it is wrong, do not press
`Ctrl+Shift+S`.

### Version lock

Offsets came from one specific build. Any other build is refused outright —
see §7. A game patch breaks the tool until the offsets are re-derived by hand.

### Multiplayer

Do not. An injected DLL is not part of the launcher checksum; it is a desync
risk and outside what Paradox's terms contemplate. Achievements are already off
with mods loaded.

### Characters with no DNA yet

If the game has never drawn a character's portrait, they have no DNA and the
tool refuses rather than handing you a shared default face. View their portrait
in game once, then retry.

### What it does not change

- **Hair and beard style.** Those come from the game's portrait modifiers, not
  from face DNA. This is why pasted faces sometimes look bald in the editor.
- **Age.** The editor previews at its own Age slider.
- **Children's faces.** They are generated from parents at birth. Editing a
  parent afterwards does not retroactively change children.

### Timing

- The portrait does not redraw instantly — reopen the panel, close the editor,
  or let a day pass.
- Nothing is permanent until you **save the game**. A crash before saving loses
  the edits, like any other unsaved state.
- Each transfer spends ~3 seconds locating the panel. Not a hang.

### Antivirus

The loader injects a DLL into another process, which is also what malware does,
so it may get flagged regardless of intent. **The full source is included —
building it yourself is the honest recommendation.**

---

## 6. Before you start

**Back up your save folder.**
`Documents\Paradox Interactive\Europa Universalis V\save games`

This writes into the game's memory. It has been used extensively without
incident, but it is one person's testing on one machine, and a bad write to a
live process is not a recoverable class of mistake.

---

## 7. Version lock, in detail

Every memory offset was derived from one build of `eu5.exe`. On a different
build they point somewhere else entirely.

So before doing anything, the bridge reads the opening bytes of two known
functions and checks they are what it expects:

- `CCharacter::GetDna` begins `48 8B 81 48 01 00 00` — `mov rax,[rcx+0x148]`,
  which literally encodes the DNA member offset the tool depends on
- `CDnaString::ToBase64String` contains `48 8B 41 08` — `mov rax,[rcx+8]`, the
  vector offset inside `CDnaString`

Neither function is ever called; only their bytes are read. If either does not
match, it logs `BUILD MISMATCH` and refuses to read or write anything.

It breaks loudly rather than silently corrupting a character. That is deliberate.

---

## 8. Troubleshooting

| Symptom | Cause |
|---|---|
| `BUILD MISMATCH` | Not 1.3.11 / checksum 6210. Nothing was touched. |
| `portrait editor is not open` | Open it — console: `pe` |
| `no character panel open` | Open the full panel, not the hover card. |
| `character has no DNA yet` | View that character's portrait in game once. |
| `ungenerated DNA (hash 0)` | Same. |
| `editor moved, rescanning` | Normal after reopening the editor. Self-heals. |
| Wrong character loaded | See §5. Let a day pass, check the id in the log. |
| Duplicate lines in the log | Injected twice. Restart the game. |
| Loader: game not running | Start it, or use `--launch`. |
| Loader: cannot open process | Run the loader as administrator. |
| Hotkeys do nothing | Check the log shows `ready.` If not, the DLL never loaded. |

**After rebuilding the DLL, always restart the game.** Windows will not unload
the old copy while the game runs, so injecting again leaves two bridges running
at once — both polling the hotkeys, both writing.

---

## 9. Building

Install the free **Build Tools for Visual Studio** with the C++ workload, open
an **x64 Native Tools Command Prompt for VS**, and run:

```
build.bat
```

Produces `build\ace_portrait_bridge.dll` and `build\ace_bridge_loader.exe`.

`DEVELOPERS.md` covers the memory layout and what is verified versus assumed.
