# Black video with DirectVobSub and a hidden tray icon

In build 960, the Win32 DirectVobSub filter reads uninitialized frame-resize settings when its legacy picture-extension getter fails. In the reported configuration, build 960 negotiates 1920×1080 video, subsequently expects 3840×2160, and rejects its own negotiated format when the Enhanced Video Renderer (EVR) asks again. This happens before the first PGS subtitle appears.

## Reproduction

The test used the alternate `Forced Sub Sample (PGS).mkv`, the Debug/LOCAL Zoom Player 23.0.0.2300 and LAV Filters 0.83.0.4-git. The file contains 1080p H.264 video, TrueHD audio and a default, forced English HDMV PGS track. Its first subtitle starts at 6.256 seconds. Each run started a new player on a private desktop. The filter settings were restored afterward.

| Filter and playback engine | Tray setting | Video output | Observed result |
| --- | --- | --- | --- |
| 960, internal Win32 | Icon allowed | NV12 | Video played and the forced subtitle appeared. |
| 960, internal Win32 | Hidden | NV12 | Startup returned `0x8004022A`. The clock stayed at zero before seeking. All five EVR captures were black, including the capture with subtitles disabled. |
| 960, external Win32 | Icon allowed / hidden | NV12 | Played with the icon allowed. The helper's PLAY command returned `0x8004022A` with the icon hidden. |
| 960, external x64 | Icon allowed / hidden | Default | Video played in both runs. |
| 960, internal and external Win32 | Hidden | YV12 | Two cold starts per engine played video and displayed the forced subtitle. |
| Pre-optimization 3.2.0.810, internal Win32 | Hidden | NV12 | Video and the forced subtitle played in the same v23 player. |

The archived Win32 releases 900, 910, 940 and 950 also returned the same startup error with the icon hidden. The 810 comparison used an archived filter. The exact DLL shipped with Zoom Player v22.5 and the reporter's original MKV were unavailable for comparison.

## Where it fails

Smart Play connected LAV Splitter's subtitle pin to DirectVobSub and the video path from LAV Video Decoder through DirectVobSub to EVR. Each connection succeeded. The failure also reproduced with a stream-copy remux containing only the video and PGS tracks and in a minimal DirectShow graph outside Zoom Player.

In that graph, EVR's own pause call succeeded. Pausing DirectVobSub returned `VFW_E_TYPE_NOT_ACCEPTED`. Calling `Commit()` directly on the allocator supplied by the EVR input pin returned the identical HRESULT even before the graph ran. DirectVobSub activates its connected output through `CBaseOutputPin::Active()`, which returns the allocator's HRESULT to the graph. The working and failing NV12 connections had identical captured format blocks. Correcting the advertised sample size, changing the plane count to one and filling the source and target rectangles, individually and together, still produced the rejection. [DirectShow pin activation](../src/filters/BaseClasses/amfilter.cpp) · [Microsoft HRESULT definitions](https://learn.microsoft.com/en-us/windows/win32/directshow/error-and-success-codes)

EVR asks the upstream DirectVobSub output pin whether it still accepts the video type. In a failing run, the filter's own `IPin::QueryAccept` already returned `S_FALSE` immediately after connection. EVR's input pin returned `S_OK` for the identical type. The filter's connected-output check requires the proposed height to match its cached height. [Connected output type check](../src/filters/transform/basevideofilter/BaseVideoFilter.cpp)

## Why 810 played and 960 failed

The historical change was commit `ea48f73a` in 2013. It moved `get_ExtendPicture` to individual option lookups. The previous DirectVobSub option table enabled the getter and the getter explicitly set the resolution doubler to zero. The replacement table left the individual picture-extension options commented out for DirectVobSub. The first lookup therefore returns `E_INVALIDARG` before writing any outputs. In the affected builds, the caller, `CDirectVobSubFilter::AdjustFrameSize`, ignores that result and reads its five uninitialized local integers. [Historical change](https://github.com/bLightZP/xy-VSFilter/commit/ea48f73a4a57a560284aef6977e7717cf7bde25f) · [DirectVobSub option table](../src/filters/transform/vsfilter/IDirectVobSubXy.h) · [Getter](../src/filters/transform/vsfilter/DirectVobSub.cpp)

The output-connection code optionally creates the tray helper, then reapplies the input media type. That recalculates the frame size. A stale resize value can select the 2× branch. Toggling the tray changes the calls preceding that calculation. Rebuilding the same source can also change what is left on the stack. [Connection and frame-size calculation](../src/filters/transform/vsfilter/DirectVobSubFilter.cpp)

Each row below used a fresh 32-bit process, the same DLL staging path and the same 1080p NV12 graph. The cached sizes were read from the running filter before and after connecting EVR. Direct calls on both published builds prefilled all five getter outputs with distinct values. Both returned `0x80070057` and left all five values untouched.

| Win32 DLL | Tray setting | Cached size before → after EVR connection | DirectVobSub accepts connected type | Playback |
| --- | --- | --- | --- | --- |
| Archived 810 (linker 14.38) | Hidden | 1920×1080 → 1920×1080 | Yes | Played |
| Unchanged 810 tag rebuilt with v145 (linker 14.51) | Hidden | 1920×1080 → 3840×2160 | No | Black |
| Published 960 (linker 14.51) | Hidden | 1920×1080 → 3840×2160 | No | Black |
| Same published 960 | Icon allowed | 1920×1080 → 1920×1080 | Yes | Played |
| Private 960 control | Hidden | 1920×1080 → 3840×2160 | No | Black |
| Matched private 960 with the getter result checked | Hidden | 1920×1080 → 1920×1080 | Yes | Played |

The unchanged 810 rebuild reproduces the failure without any of the later rendering optimizations. The paired 960 builds isolate the failed-getter check. Both used the same compiler, static libraries, project, source path and tray behavior.

The same pair was tested off-screen in Zoom Player v23 with Smart Play, LAV, EVR, NV12 and the tray hidden. The unmodified internal Win32 control produced black frames, and the external Win32 control rejected PLAY. Both engines played video with the corrected DLL. Their eight-second frame captures showed the forced subtitle in the independently decoded PGS glyph region. Mean luma there was 189.7 with subtitles on and 114.4–114.8 with subtitles disabled. The loaded test DLL hashes were checked for both successful runs. The player and helpers were private copies verified against the original Debug/LOCAL build manifest.

## Fix in 3.2.1.970

`CDirectVobSubFilter::AdjustFrameSize` returns immediately when the legacy settings lookup fails. The caller keeps the dimensions it negotiated with the decoder. The DirectVobSub filter contract now connects a 1920×1080 NV12 source and sink with the tray hidden, then checks the connected dimensions and requires the output pin to accept its own media type. [Frame-size calculation](../src/filters/transform/vsfilter/DirectVobSubFilter.cpp) · [Regression test](../test/benchmark/render_subtitles.cpp)

The exact Win32 release DLLs were compared in the same LAV → DirectVobSub → EVR graph with the icon hidden. Build 960 doubled its cached dimensions, returned `S_FALSE` for its own format and failed to start with `VFW_E_TYPE_NOT_ACCEPTED`. Build 970 preserved 1920×1080, returned `S_OK` for that format and advanced playback. The lightweight regression uses a null video sink and also passes on 960 on this machine. The real EVR graph provides the failing control for the stack-dependent bug.

The 970 release also passed six cold starts in Debug/LOCAL Zoom Player with the supplied alternate PGS sample. Each of the three engines (internal Win32, external Win32 and external x64) played through the first forced cue with the icon hidden and again with it shown. Every run verified the loaded DirectVobSub DLL against the release SHA-256. In the independently decoded subtitle mask, mean luma at the eight-second cue was 189.7 with subtitles enabled and 114.4–114.9 with subtitles disabled. The tests used a private desktop and restored the original settings.

## Workaround in 3.2.1.960

Leave **Hide tray icon** unchecked and restart the player. To keep the icon hidden, open **DirectVobSub Properties → Colors → Output**, leave only **YV12** checked, then restart Zoom Player. This second setting affects other videos opened with that filter. Restore the previous output selections after testing.
