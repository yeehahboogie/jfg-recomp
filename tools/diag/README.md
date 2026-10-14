# Diagnostic tools (development only)

Used to find the graphics bugs in Task 14/15 by comparing the port with ares. None of them touch game data in the repo.

| Tool | What it does |
|---|---|
| `dlwalk.py RDRAM DL_ADDR TRIS LOG` | Walks a JFG (DKR-family) display list in a big-endian RDRAM image with GLideN64's semantics (segments, counted lists, matrix slots, billboards). Writes clip-space triangles, a decoded log, and `LOG.seq` (every command's w0/w1). |
| `rasterize.py TRIS OUT.png FIRST COUNT` | Renders those triangles with near-plane clipping and a z-buffer (vertex colours only, no textures/blending). |
| `gdbmem.py HOST:PORT ADDR LEN OUT [CHUNK]` | Reads emulator memory through a GDB stub (ares: Settings > DebugServer), pausing the CPU for a consistent snapshot. |
| `vpad.py guid` / `vpad.py run SCRIPT` | A uinput virtual gamepad that replays a `JFG_INPUT_SCRIPT`-format script, for driving ares without touching keyboard focus. `guid` prints the SDL3 GUID and the ares `VirtualPad1` binding strings. |

Port-side counterparts are env vars (see the handoff doc): `JFG_DEBUG_RDRAM` dumps the port's RDRAM in the same byte order, `JFG_INPUT_SCRIPT` drives the port.

Typical comparison: run both with the same input script, dump RDRAM from each at the same moment, find the graphics task (`OSTask` with ucode 0x0009F3D0; its `data_ptr` is the display list), walk and render both, and diff the `.seq` files.
