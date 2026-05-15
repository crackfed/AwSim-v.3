# AwSim v3.0

A Win32 GUI automation tool built on pixel-measured BMP overlays.  
Written in C99, no external dependencies beyond the Windows API.

---

## Files

| File | Description |
|---|---|
| `AwSim.c` | Full application source |
| `AwSim.rc` | Resource script (links the app icon) |
| `AwSim.ico` | Application icon |

Runtime assets (place in the same directory as the compiled EXE):

| File | Size | Contents |
|---|---|---|
| `AwSimUI-1.bmp` | 580 × 70 | Header section |
| `AwSimUI-2.bmp` | 580 × 135 | Engines section |
| `AwSimUI-3.bmp` | 580 × 135 | Boxgrid section |
| `AwSimUI-4.bmp` | 580 × 135 | Targets section |

---

## Building

Requires **MinGW-w64** (gcc + windres on PATH).

```bat
windres AwSim.rc -O coff -o AwSim.res
gcc -O2 -o AwSim.exe AwSim.c AwSim.res -luser32 -lgdi32 -mwindows
```

---

## Window layout

The UI is a borderless `WS_POPUP` window made of four stacked BMP sections.  
The `< >` buttons in the header cycle through six view states:

| State | Sections visible | Height |
|---|---|---|
| 0 | Header + Engines + Boxgrid + Targets | 477 px |
| 1 | Header + Engines + Boxgrid | 342 px |
| 2 | Header + Engines | 207 px |
| 3 | Header + Boxgrid | 207 px |
| 4 | Header + Targets | 207 px |
| 5 | Header only | 72 px |

---

## Engine variables

A 13-character string stored in `_AwSim.ini` under `_EngineVars`:

```
. 1 0 0 0 0 0 0 0 0 0 0 3
│ └─────┬─────┘ └──┬──┘ └┬┘ │
│    CR 1-4     FS 5-8  EP  MPC
└── mode (. = dot  # = number)
```

- **CR 1–4**: mutually exclusive run-order (0 = off, 1–5 = order)  
- **FS 5–8**: independently orderable (0 = off)  
- **EP 9–11**: mirror FS 6–8  
- **MPC**: moves-per-cycle (1–5)

---

## Engine #1 — AwSim

Selects 2 boxes (MPC = 1) or 3 boxes randomly from the active (checked) grid cells each cycle.

**First cycle only:** sends `A X X` to reset the game cursor.

**Each Move** within a cycle:
1. Click destination box  
2. Click origin box  
3. Press `A`  
4. Click origin box (receives the swapped piece)  
5. Press `A`  
6. Wait 3333 ms  

Move sequence per MPC value:

| MPC | Moves |
|---|---|
| 1 | Box1 → Box2 |
| 2 | Box1 → Box2 · Box2 → Box3 |
| 3 | + Box3 → Box1 |
| 4 | + Box1 → Box2 |
| 5 | + Box2 → Box3 |

After all moves: double-click the active Target (Refresh), then start the next cycle.

---

## Stop conditions (while running)

| Action | Effect |
|---|---|
| Right-click anywhere | Stop automation |
| Move cursor to x ≤ 3 | Exit program |

---

## Configuration

Settings are auto-saved to `_AwSim.ini` in the EXE directory on every meaningful action and on exit.

---

## License

MIT
