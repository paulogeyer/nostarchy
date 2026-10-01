# Nostarchy

Version 0.3. A small desktop Nostr client. C++20, Qt 6, and QML. It speaks NIP-01 over its own WebSocket client and signs with secp256k1.

## Build

Qt 6 (Core, Gui, Qml, Quick, QuickControls2, Network) and a C++20 compiler. secp256k1 is fetched at configure time.

```
cmake -S . -B build
cmake --build build
./build/nostarchy
```

`./build/nostarchy-check` runs the bech32, canonical JSON, and BIP-340 checks. `./build/nostarchy-check --net` also checks live relay events.

## Use

Generate a key or import an nsec. The secret stays in `~/.config/nostarchy/nostarchy.conf`.

- **Global** is a recent kind 1 feed from your relays.
- **Following** asks each followed account’s write relays (kind 10002) for their notes.
- Open a note to reply. Open a profile for that person’s notes, follows, and followers.
- **Delete** on your own note sends a kind 5 request (NIP-09). Relays may keep a copy. Kind 1 notes cannot be edited.
- **Store logs** appends client messages to `~/.local/state/nostarchy/nostarchy.log`.

Colors follow the current Omarchy theme when `~/.local/state/omarchy/current/theme/colors.toml` is present.

## Cache

Notes and profiles are stored in `~/.cache/nostarchy/nostarchy/feed.json`. The Following view also keeps a small `following.json` so those notes can show before relays answer. Avatar files live in `images/` next to that cache.

## Layout

`src/crypto` keys and bech32. `src/nostr` events and the relay pool. `src/net` the WebSocket client. `qml/Main.qml` the window.
