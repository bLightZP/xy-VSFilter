# Optional libass rendering

The Main property page has a **Prefer libass rendering** checkbox in a Rendering
group below Loading. It defaults to unchecked. Both XySubFilter and VSFilter
persist this preference in their existing General registry section as
`PreferLibassRendering`; existing option identifiers and COM interfaces are preserved.

For ASS/SSA, the shared text renderer retains the original script, decoded to
UTF-8 without converting tags or styles through the native rendering engine.
Embedded tracks retain the codec-private header and timed packets. The backend
uses libass for parsing, shaping and rasterization, then composites its ordered
alpha masks into an owned frame using the existing color/output conventions.
Other subtitle formats continue through the native renderer. Libass and native
rendering have separate pixel baselines; cross-engine pixel identity is not expected.
Extended embedded ASS2 also retains the native renderer.

The libraries are statically linked into each filter binary. No separate libass
runtime DLL or installation is required. This project currently produces `.dll`
DirectShow filters; their filename extension does not affect the static linkage.

## Reproducible source build

Use an existing Python 3.12+ and the Visual Studio CMake component:

```
python tools/fetch_libass_sources.py
powershell -NoProfile -File tools/build_libass.ps1 -Platform x64 -Configuration Release
```

The fetcher verifies SHA256 hashes in
`src/thirdparty/libass-deps/sources.lock.json`. Source archives and extracted
trees are ignored; no system packages or build tools are installed. The normal
subtitles project builds the static dependencies before compiling. The filter
and console targets link those libraries. Repeat for Win32 and Debug as needed.

Pinned dependencies: libass 0.17.4, FreeType 2.14.1, HarfBuzz 11.5.1 and
FriBidi 1.0.16. Libass uses its portable C rasterizer in this build; NASM is not
required. System font lookup uses DirectWrite. Graph resource-bag font
attachments are supplied directly to libass; the console tool reads extracted
TTF/OTF files privately without installing fonts.

The upstream archives include their licenses: libass ISC, HarfBuzz's permissive
license, FreeType's FTL/GPL alternatives, and FriBidi LGPL-2.1-or-later. Preserve
these notices and the corresponding sources when distributing a build. The
filter is already distributed under GPL-2.0-or-later. Source URLs and exact
hashes are in the lock file; the CMake integration does not patch upstream code.

## Validation

`render_subtitles.exe --libass-contract` checks default selection, animation,
seeking, retained frame ownership, toggling back to native, equivalence between
external and embedded ASS, segment flush/re-send, and output formats. These are
functional checks; libass speed is not an acceptance criterion.

The offline performance workflow is described in `test/benchmark/README.md`.
No player is needed for native renderer comparisons. Debug Zoom Player remains
the integration test for playback and renderer drops when playback is authorized.

Completed build, correctness and native-performance results are in the
[2026-10-26 report](performance-20261026-libass-native.md). Libass performance is
not part of the acceptance criteria.
