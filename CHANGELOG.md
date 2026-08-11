# Changelog

## v2.8.0

First public release.

- `--setup` / `--launch`: the loader can start the game with `-debug_mode`,
  so debug mode no longer has to be configured in Steam by hand.
- `USAGE.md`: full documentation, including every known restriction.

### How it got here

- **v2.7.0** — stopped caching panel contexts. A cached context stays valid
  after you navigate to another character while still naming the previous one,
  so the editor silently loaded the wrong face.
- **v2.6.0** — enumerate memory regions first, then scan them top-down, with an
  early exit once two contexts agree. 204 s to 3.1 s.
- **v2.5.1** — re-read candidates after 50 ms and discard any that moved. A
  stack slot briefly holding the vtable pointer had been passing validation.
- **v2.4.1** — validate every scan candidate. A qword equal to a vtable address
  is not proof of an object.
- **v2.3.2** — confirm injection with a module snapshot, not the remote thread's
  exit code, which truncates a 64-bit `HMODULE` to 32 bits.
- **v2.3.1** — the loader waits for Enter, so double-clicking does not flash the
  window away.
- **v2.3.0** — standalone loader; Cheat Engine no longer required.
- **v2.0.0** — direct memcpy between `editor+0x108` and `character+0x148`,
  replacing the Base64 + clipboard route entirely.
