# 181. The game's debug camera

The retail game still makes the developers' debug camera, and still draws from it when it is switched on. Only the
code that switched it on and moved it was cut. The port puts that code back:

| Setting | Knob | What it does |
|---|---|---|
| Settings > Controls > Camera > **Free camera (offline)**, off by default | `DES_FREECAM=1` | the debug camera, switched with Cross and L3 |

It is read once, at start, so the row says "Restart to apply". It works with the debug menus (`DES_DBGMENU`, docs/180)
or without them.

## 1. No cheating

With the knob set, the session is offline, as with the debug menus (docs/180 section 1):
- before anything connects, the network knobs are cleared and the runtime is locked offline;
- so there is no online play, no co-op and no Pantheon records, and a free camera can't look around corners at another
  player;
- the Network tab says why: "Off: the free camera is on".

The camera doesn't change the saves, so they aren't copied. With `DES_DBGMENU` as well, the debug menu's save copy
applies.

## 2. Using it

**Hold Cross and press L3** to go to the next mode:

| Mode | The view | The game |
|---|---|---|
| 1 | the free camera | time stopped. Hold Square to let frames through |
| 2 | the free camera | time runs. The character takes no input |
| 3 | stays where the free camera is | you play. The character walks away from the view |
| 0 (off) | the game's camera | as normal |

While the camera is free (modes 1 and 2):

| Control | What it does |
|---|---|
| Left stick | moves |
| Right stick | turns |
| R2 / L2 | up / down (L2 not tried yet) |
| L1 / R1 | slower / faster (not tried yet) |

L3 counts only with no shoulder button held. A new area starts with the camera off.

## 3. How it works

The frame step (0x00144160) asks for pad device 0 at 0x0014422C, once a frame. The port's instruction hook there
(`des_dbgmenu_hook`, `free_camera` in `port/des_dbgmenu.cpp`) does the rest. Addresses are BLUS30443's; the region
tables have the other discs'.

**The cameras object:** the frame step's +0x34. It is made by 0x0021D280 at each area's load:

| Offset | What it is |
|---|---|
| +0x8 | the game's camera |
| +0xC | the camera drawn from in mode 3 |
| +0x204 | the debug camera (a camera's 4x4 matrix is at +0x10) |
| +0x1FC | the debug camera's mode, 0 at the start |

While the mode isn't 0, the frame step (0x001446FC) draws from the debug camera, and the cameras' update (0x00219CD8)
leaves the game's camera alone. That is the game's own code.

**Each frame, the hook:**
- reads the game's pad state (pad device +4):
  - +0x120 bit 9 is Cross held;
  - +0x11E is 0x0A on the frame L3 is pressed, with no shoulder button;
  - +0x122 bit 4 is Square;
- on Cross and L3, adds 1 to the mode, wrapping after 3;
- in mode 0, puts the game camera's matrix on the debug camera, so the free camera starts from the view;
- in modes 1 and 2:
  - moves the debug camera with the game's own `DbgCamControls` (0x0012B2F8: camera, pad device, 0, 0);
  - sets the frame step's +0x44 to 1, which keeps the step on its debug-camera path;
- in mode 1, sets the frame step's pause flags (+0x58, +0x59) unless Square is held. It clears them again when it
  leaves mode 1, and leaves them alone otherwise;
- in mode 3, puts the debug camera's matrix on +0xC.

## 4. Tested

On the US disc (BLUS30443), player build, in the tutorial:
- **The modes:** all four in turn, with both Cross and L3 together, and Cross held first;
- **Mode 1:**
  - two shots a second apart differ in 0.7% of their pixels, against 7.4% in mode 2;
  - holding Square lets the frames through;
- **Mode 2:**
  - the left stick moved the camera 27 units in 2 seconds;
  - R2 raised it 45 units in 1.5 seconds;
  - the right stick turned it;
- **Mode 3:** the view stayed while the character walked on;
- **Together with `DES_DBGMENU`:** both worked;
- **Offline:** with `PS3_NET_ONLINE` and a local server set, the network was off and locked.

**Not run yet:**
- the EU, JP and Asia discs. Their addresses were found with `tools/region/region_table.py find`;
- L1, R1 and L2;
- an area change with the camera on;
- cutscenes, menus and boss fights with the camera on.

In mode 1 a few things still move (0.7% of the pixels), probably particles.
