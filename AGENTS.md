# AGENTS.md

- `snake_case` everywhere
- class private members and instances prefixed `_`
- read .editorconfig
- network structs: `__attribute__((packed))`, first field is `type` always
- authoritative host/client. Host runs game logic and send state; client sends inputs
- network is a static class, raw byte transport, no message knowledge
- fixed custom MACs; role picks peer, toggle role only in `include/config.h`
- ESP-NOW limit: 250 bytes/msg. For frequent state use binary, not JSON
- to add a new message:
  - add `msg_type` value in `lib/protocol/src/protocol.h`
  - define packed struct with `type` first
  - register `net.on_message(type, cb)` in `src/main.cpp` for now
  - send with `object.send(&msg, sizeof(msg))`
