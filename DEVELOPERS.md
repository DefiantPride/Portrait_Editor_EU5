# ACE Portrait Bridge — developer notes

User-facing instructions are in `README.md`. This file is the technical side:
why the design is what it is, and what is verified versus assumed.

Europa Universalis V **1.3.11 "Pavia"**, launcher checksum **6210**.

Two hotkeys that move portrait DNA between a campaign character and the game's
built-in portrait editor.

```
Ctrl+Shift+L    LOAD    selected character  ->  portrait editor
Ctrl+Shift+S    SAVE    portrait editor     ->  selected character
```

---

## Why this replaces the earlier tooling

The Lua scripts moved DNA as Base64 through a file, Notepad, and the editor's
own Paste button — because that was the only route verified at the time.

It turned out both endpoints are the same structure in memory:

```
CPortraitEditorWindow + 0x108   ->  CDnaString
CCharacter            + 0x148   ->  CDnaString
```

So the transfer is a memcpy of `gene_count * 8` bytes between two buffers. No
Base64, no clipboard, no files, and no game functions called. Verified in
Cheat Engine before this was written.

---

## Use

1. Load a save. Open a character's panel.
2. Open the portrait editor — console: `pe`
3. **Ctrl+Shift+L** — the editor now holds that character's DNA
4. Edit the face
5. **Ctrl+Shift+S** — the character now holds the edited DNA
6. **Save the game**

`ace_portrait_bridge.log`, written next to `eu5.exe`, records every action
including the gene 0 values before and after.

### Confirming it worked

The editor's **Color Coordinates** panel is a live readout of gene 0 as
percentages of 255. The log prints the values to expect after each transfer.
That is more reliable than judging by the face — the editor renders at its own
Age slider and `ethnicity_template`, so its preview never exactly matches the
in-game portrait even with identical genes.

---

## What it does not do

- **It does not save your game.** Changes live in memory until you save.
- **It does not redraw the in-game portrait.** Close and reopen the character's
  panel, or let a day pass. For the editor's own preview, try
  **Restart Portrait**.
- It calls no engine code, so it cannot trigger a refresh itself. Every write
  is plain data.

---

## Refusals

The bridge stops rather than guess:

| Message | Meaning |
|---|---|
| `BUILD MISMATCH` | Not 1.3.11/6210. Nothing read or written. |
| `portrait editor is not open` | Open it with `pe` first. |
| `no character panel open` | Open a character's panel. |
| `character has no DNA yet` | Open that character's portrait in game once. |
| `ungenerated DNA (hash 0)` | Allocated but never generated — see below. |
| `gene count mismatch` | Endpoints disagree on gene count. |
| `not a CDnaString` | Object at the offset failed its vtable check. |

The hash-zero refusal matters: the game's own getter returns a **shared global
default** for characters with no DNA. Without that check you would get the same
generic face for every such character and it would look like success.

---

## Version binding

`VerifyBuild()` checks two function prologues at startup:

- `CCharacter::GetDna` begins `48 8B 81 48 01 00 00` — `mov rax,[rcx+0x148]`,
  which literally encodes the DNA member offset
- `CDnaString::ToBase64String` contains `48 8B 41 08` — `mov rax,[rcx+8]`,
  the vector offset inside `CDnaString`

Neither function is ever called; only their bytes are read. If either fails to
match, the module logs and stops. **Do not remove this check** — with wrong
offsets it would read and write arbitrary memory.

---

## Safety

- Back up your save folder before first use.
- Single-player, offline. An injected DLL is not part of the launcher checksum
  and is a desync and terms-of-service risk in multiplayer.
- Achievements are already disabled with mods loaded.
- Every game-memory access is exception-guarded; a bad pointer fails cleanly
  rather than crashing.

---

## Build

See `README.md`. `build.bat` produces both the DLL and the loader.

---

## Known limitations

- **Selection heuristic.** Several `CCharacterLateralViewContext` objects are
  alive at once (main panel, spouse and children widgets, hover cards). The
  lowest-address one has tracked the open panel across every run observed, but
  this has not been verified across a game restart. If the wrong character is
  ever targeted, the log names the id so it is obvious.
- **Full-address-space scan** on every hotkey press. Takes a moment on a large
  save.
- **Reads on its own thread.** A transfer during an active animation could in
  principle tear; press the hotkey with the editor idle.
