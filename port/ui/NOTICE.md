# The wrapper UI: third-party notices

The start screen, in-game menu and settings (`port/ui`, docs/32) are built from the software and fonts below. The
About tab of the settings lists the same.

| What | Used for | Licence | Where |
| --- | --- | --- | --- |
| [Dusklight](https://github.com/TwilitRealm/dusklight) | The interface code (`src/dusk/ui`) and stylesheets (`res/rml`), adapted by way of FFCCEOT-Recomp. Each adapted file says so in its header. | CC0 1.0 (public domain) | `port/ui/src`, `port/ui/res/rml` |
| FFCCEOT-Recomp | The same interface as adapted there (D3D/SDL host, settings, embedding), and its problem reports (data folder, log, crash reports and dialog, report an issue, zip writer); by the author of this port | The author's own, or Dusklight's (CC0) | `port/ui/src`, `port/cmake/EmbedFiles.cmake` |
| [RmlUi](https://github.com/mikke89/RmlUi) 6.3 | The HTML/CSS interface library, with its D3D11 renderer and Win32 helpers (`Backends/`) | MIT (below) | Downloaded at configure time (FetchContent), compiled in, with one fix of ours (`port/ui/cmake/rmlui_fixes.cmake`) |
| [FreeType](https://freetype.org) 2.14.3 | Font rendering for RmlUi | FreeType Licence (FTL) | Downloaded at configure time (FetchContent), compiled in |
| [Cinzel](https://github.com/NDISCOVER/Cinzel) | Headings | SIL Open Font Licence 1.1 | `res/fonts/Cinzel.ttf`, licence in `res/fonts/OFL-Cinzel.txt` |
| [Alegreya Sans](https://github.com/huertatipografica/Alegreya-Sans) | Body text | SIL Open Font Licence 1.1 | `res/fonts/AlegreyaSans-*.ttf`, licence in `res/fonts/OFL-AlegreyaSans.txt` |
| [Old Standard TT](https://fonts.google.com/specimen/Old+Standard+TT) | The DeS UI look's text | SIL Open Font Licence 1.1 | `res/fonts/OldStandard-*.ttf`, licence in `res/fonts/OFL-OldStandard.txt` |
| [Material Symbols Rounded](https://github.com/google/material-design-icons) | Icons | Apache-2.0 | `res/fonts/MaterialSymbolsRounded-Regular.ttf`, licence in `res/fonts/LICENSE-Apache-2.0.txt` |
| [Mbed TLS](https://www.trustedfirmware.org/projects/mbed-tls/) 3.6 | TLS to the online server, in the runtime (ps3recomp) | Apache-2.0 | ps3recomp's `third_party/mbedtls`, compiled in; licence in `licenses/mbedtls` of the download |
| [SMAA](https://www.iryoku.com/smaa/) (reference implementation, iryoku/smaa) | The SMAA anti-aliasing setting (`RSX_AA=2`), in the runtime (ps3recomp) | MIT | ps3recomp's `third_party/smaa`, compiled in; licence in `licenses/smaa` of the download |
| [SDL](https://www.libsdl.org) 3.4.20 | The controllers (on Windows and Linux), and on Linux also the window, the keyboard and mouse, and the sound, in the runtime (ps3recomp) | zlib | ps3recomp's `cmake/sdl3_dep.cmake` builds it from its pinned source, compiled in; licence in `licenses/sdl3` of the download |
| [libusb](https://libusb.info) 1.0.30 | The Switch 2 controllers over USB, which SDL opens through it (Windows) | LGPL 2.1 or later | `libusb-1.0.dll` beside the game, unchanged; licence in `licenses/libusb`, source on this project's `source-libusb-1.0.30` release |
| [FFmpeg](https://ffmpeg.org) 8.1.3 | The game's movies (libavcodec, libavutil) | LGPL 2.1 or later | DLLs (Linux: shared libraries) beside the game, unchanged; licence in `licenses/ffmpeg`, source on this project's `third-party-sources` release |
| [vkd3d](https://gitlab.winehq.org/wine/vkd3d) 2.1 | The D3D12 renderer on Vulkan (Linux) | LGPL 2.1 or later | Shared libraries beside the game (Linux), unchanged; licence in `licenses/vkd3d`, source on this project's `third-party-sources` release |
| [glslang](https://github.com/KhronosGroup/glslang), [SPIRV-Tools](https://github.com/KhronosGroup/SPIRV-Tools), [SPIRV-Headers](https://github.com/KhronosGroup/SPIRV-Headers) | Compiling the game's shaders for the Vulkan renderer, in the runtime (ps3recomp) | BSD-3-Clause and others (glslang), Apache-2.0 (SPIRV-Tools), MIT (SPIRV-Headers) | Compiled in; licences in `licenses/glslang`, `licenses/spirv-tools` and `licenses/spirv-headers` |
| [volk](https://github.com/zeux/volk), [Vulkan-Headers](https://github.com/KhronosGroup/Vulkan-Headers) | Loading and calling Vulkan, in the runtime (ps3recomp) | MIT (volk), Apache-2.0 or MIT (Vulkan-Headers) | Compiled in; licences in `licenses/volk` and `licenses/vulkan-headers` |

Crash reports use Windows' own dbghelp (stack and minidump) and report screenshots its Imaging Component (PNG); both
are part of Windows, nothing is added.

Japanese, Chinese and Korean text (the trophy names of the Japanese and Asian discs) is drawn with a font already on
the PC: Yu Gothic, Malgun Gothic or Microsoft JhengHei on Windows, Noto Sans CJK (or Droid Sans Fallback, WenQuanYi)
on Linux. No font for it is bundled (`src/text_fonts.cpp`).

The menus read the controllers through the runtime (SDL 3, above). On Windows the keyboard and mouse come through
the game window's messages; on Linux through SDL 3.

No art from Dusklight, FFCCEOT-Recomp or the game is used. The fonts and stylesheets are compiled into the executable.

## FreeType

Portions of this software are copyright © 2025 The FreeType Project (www.freetype.org). All rights reserved.

## RmlUi

```
MIT License

Copyright (c) 2008-2014 CodePoint Ltd, Shift Technology Ltd, and contributors
Copyright (c) 2019-2026 The RmlUi Team, and contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```
