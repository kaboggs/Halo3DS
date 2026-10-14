# Halo3DS Remote Pilot integration for the dev branch

This adapter exposes the current game build to a paired PC QA tool. It is a
separate `RP1` protocol implementation, so the existing game port and other
game adapters do not share a wire identity or secret. No retail files, map
bytes, textures, or model data are embedded in this source. The local catalog
contains only names and IDs; 3D previews use the owner's installed maps.

The host is [Remote Pilot](https://github.com/kaboggs/RemotePilot), a separate
private repository with an MIT license. Kevin Boggs's additions in this
integration use the same MIT terms in the root `LICENSE-KEVIN-MIT.md`; the
upstream base license remains in `LICENSE.md`. The bundled Oxanium font and
its generated atlas retain the SIL Open Font License in
`port/n3ds/typography/Oxanium-OFL.txt`.

## Build and opt in

`remote_pilot_halo.c` and `remote_pilot_halo_transport.c` are included in the
normal unified build. Without an armed config file, the listener does not
start. To enable the paired link, create `sdmc:/halo-source/remote-pilot-upstream-dev.cfg`:

```ini
port=<private-LAN TCP port, 1024..65535>
token=<64 lowercase hexadecimal characters from 32 random bytes>
session=on
mode=reverse
```

To set up the separate portable Remote Pilot tool, run
`python port/n3ds/remote_pilot/install_preset.py --tool-root /path/to/RemotePilot`
from this source tree, then restart Remote Pilot. Select **Halo3DS Dev** and
choose **Prepare pairing**. Copy the generated config to the SD storage root
with the viewer's folder picker or download it and put it at the path above.
The tool keeps its token and cached address in its own ignored `local/` folder.
This game-specific RP1 preset is separate from the portable tool's RP2
reference adapter.

The PC adapter must hold the same token and connect to the device's current
private Wi-Fi IPv4 address. No address is compiled into the adapter. The
owner's `session=on` setting persists across boots, so the paired PC can
reconnect automatically. Set `session=off` on the SD card and restart the game
to disable the listener. Keep the token out of Git, reports, and screenshots.

The listener follows local DHCP address changes and rejects connections unless
both local and peer IPv4 addresses are in 10/8, 172.16/12, or 192.168/16.
The thread owns the socket and pairing
exchange; it does not wait on network I/O in the render loop. The active
full-resolution capture path writes a temporary RPIM file on the SD card. Live
preview frames stay in RAM. Captures and remote
input can affect frame timing while requested.

## Local dev link menu

Build with `--remote-pilot-dev-menu` to include **Bug It / PC Link**. This flag
is off in normal builds. From the main menu or pause menu, press **SELECT+X**;
the bottom screen also shows the shortcut and current status. The panel shows
Not armed, Waiting for PC, PC authenticated, Retrying, or Listener error.
It also shows the current private 3DS IPv4 address when network service is
available; use that with the PC receiver's explicit device-host option if
automatic private-LAN discovery misses the console.
Press **A** (or tap Retry) to reopen a valid listener. Press **Y** (or tap
Bug It) to ask the paired PC to save both screens and a diagnostic report;
the request is queued until the PC connects. **B** closes the panel.
When armed, a compact OFL-font link badge appears at the top-right of the
top screen, including during play. Press **X** or tap **HUD On/Off** in this
existing panel to toggle the badge. This preference is saved separately in
`sdmc:/halo-source/remote-pilot-upstream-dev-hud.cfg`; it never changes the pairing token
or listener setting. The touch-screen launch hint fades after a few seconds.
Retry never changes `session=off` to `session=on`. An unarmed session must be
enabled on the SD card before a new boot. The authenticated status
means a correctly paired PC was seen within the last eight seconds, not merely
that Wi-Fi is enabled.

The link menu uses the bundled OFL font. It contains no extracted game font.

## RP1 exchange

All lines are ASCII and end in `\n`. Each TCP connection handles one request;
the optional sequence in the shared RP1 design is therefore zero and omitted
from this adapter's HMAC input. The PC closes after receiving the body.

1. PC sends `RP1 HELLO halo3ds\n`.
2. Device generates a fresh 16-byte nonce and sends
   `RP1 CHALLENGE <32 lowercase hex nonce> <64 lowercase hex MAC>\n`.
   MAC = HMAC-SHA256(secret, nonce bytes + `server\n`).
3. PC sends `RP1 <64 lowercase hex MAC> <id> <kind> <payload>\n`, where
   `id` is 0..1000000000. MAC = HMAC-SHA256(secret, nonce bytes + the exact
   ASCII `<id> <kind> <payload>\n`). The device compares the MAC before
   dispatching any action.
4. Device sends `RP1 <type> <id> <byte-count>\n` followed by exactly that
   many body bytes, then `RP1 MAC <64 lowercase hex MAC>\n`. The response MAC
   is HMAC-SHA256(secret, nonce bytes + `response\n` + the exact response
   header bytes + body bytes). The PC verifies it before using any response.
   The body is absent for `none`. Unknown requests return a bounded `result`
   message.

| Request kind | Payload | Response | Purpose |
| --- | --- | --- | --- |
| `none` | `-` | `none` | Authenticated heartbeat and discovery |
| `meta` | `-` | `meta` JSON | Game ID, model family, capability flags |
| `perf` | `-` | `perf` JSON | Latest frame-stage snapshot |
| `log` | `tail` | `log` text | Up to 4095 recent bytes of the session log |
| `report` | `capture` | `report` text | Metadata, latest perf, recent log |
| `console` | `rp_preview` selection, up to 127 bytes | `result` text | Preview a validated installed item on the game thread; `ok`, `rejected`, or `timeout` |
| `input` | `keys=<hex>,frames=<1..120>` | `result` text | Hold allowed buttons for bounded frames |
| `pad` | `buttons=<4 lowercase hex>,lx=<int>,ly=<int>,rx=<int>,ry=<int>,lt=<int>,rt=<int>,frames=<int>` | `result` text | Replace a short-lived Xbox-style controller state |
| `touch` | `x=<0..319>,y=<0..239>,down=<0 or 1>` | `result` text | Press, drag, or release the real bottom LCD touch surface |
| `shot` | `capture` | `shot` RPIM | Both screens at native resolution |
| `stream` | `capture` | `stream` RPIM | Both screens downsampled by two, with optional mixed audio |

The paired PC may use the `console` action with `rp_preview map:<pc-map-id>`
or `rp_preview weapon:<class-id>:<pc-map-id>` or
`rp_preview vehicle:<class-id>:<pc-map-id>`. The game validates IDs against
its built-in PC map and model lists, requires the main menu and an installed
owner map, then opens that selection in its existing 3D preview menu. The PC
can request `shot` after the menu has drawn. This action never starts a match.
Campaign entries remain metadata; this menu previews installed PC multiplayer
maps and the model classes available in their assets.

The `input` mask uses the bit positions in `engine_input.h`. Remote injection
accepts A/B/X/Y, L/R/ZL/ZR, and the four D-pad buttons. It rejects START,
SELECT, touch, and unknown bits. Local input is ORed with the remote hold.
Use the separate `touch` request for bottom-screen UI. Coordinates are native
320×240 pixels. Send fresh `down=1` samples while held and `down=0` on release;
an unrefreshed hold expires after 900 ms. A physical finger takes priority and
cancels a simultaneous remote gesture. Listener restart or session shutdown
also clears the gesture. The normal game input path consumes the merged touch
sample, so menus, taps, and drags use their existing behavior. Valid requests
return `queued`; malformed requests return `unavailable`.
For `pad`, the hexadecimal `buttons` mask uses the standard browser Gamepad
API button positions 0–15: A, B, X, Y, LB, RB, LT, RT, View, Menu, left-stick
click, right-stick click, D-pad Up, Down, Left, Right. Stick axes are signed
integers from -1000 to 1000, with positive Y meaning up in the game. The PC
converts a browser stick's down-positive Y before sending. Triggers are
integers from 0 to 1000 and act as digital presses at 500; Gamepad button
bits 6 and 7 also count as trigger presses. `frames` is 1–30. Send a complete
state about every 100 ms while active, including zero values to release it.
The device replies with `RP1 result <id> 6\nqueued` on acceptance and
`RP1 result <id> 0\n` for a malformed pad payload. Do not retry a gameplay
action after a lost reply.

Each accepted pad state replaces the previous one. It expires after 350 ms or
when its frame count runs out, whichever comes first. Remote buttons are ORed
with physical buttons; each stick axis uses the input with greater magnitude,
so physical controls remain available. On a New 3DS, LT/RT map to ZL/ZR; on
an original 3DS they map to R/L, the game's existing grenade/fire controls.
LB/RB also map to L/R, so bumpers and triggers share those two actions on an
original model. Both virtual sticks remain available on either model, even
though the original model has no physical C-stick. Stick clicks map to
D-pad Down/Up; on New 3DS gameplay these are D-pad actions because the native
input ABI has no stick-click keys. Remote View/Menu map to SELECT/START, but
the adapter suppresses a remote START+SELECT exit chord, including one formed
with a physical button.
The `console` result reports whether the allowlisted preview request was
accepted. Arbitrary script evaluation is rejected. The pairing secret,
explicit owner opt in, and private-LAN restriction guard these control actions.

`meta` and `perf` use `schemaVersion: 1` and `gameId: "halo3ds"`. `perf` includes
monotonic `timestampMs`, frame/game tick, model (`original` or `new`), active
map basename, map source (`pc` or `xbox`), menu state, five measured work
stages, and total `workMs`. `frameMs` and `fps` appear after a prior frame is
available. Heap, linear, and application-free bytes appear when the periodic
memory query succeeds. Omitted values are unknown, not zero. The game samples
stage timings every frame and queries memory every 30 frames.

## RPIM screen body

The RPIM body is 16 little-endian header bytes followed by top and bottom
pixel buffers. Header offsets are: `0..3` magic `RPIM`; `4..5` top width;
`6..7` top height; `8..9` bottom width; `10..11` bottom height; `12..13`
pixel format `1` (RGB888) or `2` (RGB565); `14..15` orientation `1`
(portrait buffer).

`shot` uses RGB888 (format 1) at 240×400 and 240×320, for 518416 total bytes
including the header. `stream` uses little-endian RGB565 (format 2) at
120×200 and 120×160, for 86416 bytes. Older RGB888 stream packets remain
accepted by the PC receiver.

The game framebuffer stores BGR bytes. Full captures swap red and blue before
sending; live preview packs RGB565 from those bytes. Rotate each portrait buffer 90 degrees
counterclockwise to display a 400×240 or 320×240 image. A PC viewer can center
the lower screen under the upper screen in a 400×480 canvas.

Dev-menu builds append `RPAU`, a little-endian 16-bit sample count, a
little-endian 16-bit sample rate of 16364, and that many mono PCM16 samples
to each live packet. The source is NDSP's final stereo mix, averaged and
downsampled by two. The trailer is bounded to 8192 samples; old live packets
without it remain valid. Audio follows the requested preview rate and may skip
older samples after a long network gap.

## Catalog and current limits

[`catalog.json`](catalog.json) lists the ten campaign IDs, thirteen original
multiplayer map IDs, nineteen PC multiplayer map IDs, game modes, and the
weapon and vehicle preview classes used by the existing in-game 3D menu.
The catalog is descriptive; it does not claim a map is installed. Previews are
screenshots of the device's current menu selection, not a remote API for
selecting an arbitrary map or extracting assets.

The transport is compiled and host-tested. Its SHA-256 and HMAC implementation
is checked against Python's standard library with
`python port/n3ds/remote_pilot/test_sha256.py --clang PATH`. The unified 3DSX
reached the main menu in Azahar with a private copy of the owner's installed
maps. Local emulator pairing, metadata, performance, logs, reports, controller
input, stream, and full captures were exercised through the PC receiver;
screenshots succeeded both at the main menu and after profile navigation.
Hardware pairing, controller feel, capture timing, and frame impact still
need physical device verification before a release. The integration is
prepared separately for review against the upstream dev branch.

## Validation

From this source root, run
`python -m unittest port.n3ds.remote_pilot.preset.test_halo_protocol` and
`python port/n3ds/remote_pilot/test_sha256.py --clang /path/to/clang`.
For the preset lifecycle tests, add a separate portable Remote Pilot checkout
to `PYTHONPATH`, then run
`python -m unittest port.n3ds.remote_pilot.preset.test_preset_adapter`.
Run `python port/n3ds/tools/check_attribution.py` before committing.
