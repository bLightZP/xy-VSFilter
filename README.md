# xy-VSFilter v3.2.1.930 (2026-10-31)

Optimized Windows subtitle rendering with **VSFilter**, **XySubFilter**, and an optional **libass backend for ASS/SSA**. Builds are available for **32-bit (Win32) and 64-bit (x64)**.

[Download the latest release](https://github.com/bLightZP/xy-VSFilter/releases/latest) · [v3.2.1.930 release notes](docs/releases/v3.2.1.930.md) · [v3.2.1.920 release notes](docs/releases/v3.2.1.920.md) · [v3.2.1.910 release notes](docs/releases/v3.2.1.910.md)

Each architecture package includes the three filter DLLs and standalone rendering/streaming benchmark consoles. Matching PDB symbols, pinned dependency sources, build and validation records, and SHA256 checksums are separate release assets. Choose the architecture that matches your player/DirectShow graph.

Extract the package to a permanent folder and close your players. Run **RegisterFilters.cmd** to register its three filters, or **UnregisterFilters.cmd** to unregister them. The scripts request administrator permission when needed and select the registration tool for the package's architecture. Keep the folder in place while the filters are registered. Run either script with `/check` for a check that leaves registration unchanged.

| Component | Purpose |
| --- | --- |
| `VSFilter.dll` | DirectShow subtitle rendering and Avisynth subtitle filters. |
| `XySubFilter.dll` | Subtitle provider for compatible consumers such as madVR or MPC Video Renderer. |
| `xy_sub_filter_consumer.dll` | DirectShow video transform that receives and blends XySubFilter frames. |

Native rendering is the default. To use libass for ASS/SSA, enable **Prefer libass rendering** in the Main property page's Rendering group. Libass and its dependencies are statically linked into the filters. See [libass configuration and source builds](docs/libass-integration.md).

On the More property page, custom render-layout dimensions take effect on **Apply/OK**. Each dimension supports **16–12,800 pixels**. **Render To Original Video Size** uses the video's actual dimensions independently of the selected subtitle layout.

This is an [optimization fork](https://github.com/bLightZP/xy-VSFilter/) of [pinterf/xy-VSFilter](https://github.com/pinterf/xy-VSFilter/), based on [Cyberbeing's xy_sub_filter_rc5](https://github.com/Cyberbeing/xy-VSFilter/tree/xy_sub_filter_rc5) with pinterf's Avisynth additions. The active branch is `codex/sol-6.1-xhigh-v3.2.1.900`.

For development, see the [architecture graph](docs/knowledge-graph.md), [benchmark and regression-test instructions](test/benchmark/README.md), and [v3.2.1.910 performance comparison](docs/benchmarks/20261028-release-comparison.json).

# Useful links:
[Doom9 forum](https://forum.doom9.org/showthread.php?t=168282)

[Avisynth wiki](http://avisynth.nl/index.php/Xy-VSFilter)

# Info:

- VSFilter.DLL: Included Avisynth filters (name and parameter signature)
  
      VobSub "cs"
      TextSub "c[file]s[charset]i[fps]f[vfr]s"
      TextSubSwapUV "b"
      MaskSub "[file]s[width]i[height]i[fps]f[length]i[charset]i[vfr]s[pixel_type]s"

# Change log:

## v3.2.1.930 (2026-10-31)

- Fix madVR subtitle queue draining and failure to fill while paused by keeping consumer parameter updates from repeatedly clearing queued output.
- Keep future embedded subtitle packets from clearing the queue; limit late-packet invalidation to affected frames.
- Correct video timestamps and partial invalidation boundaries when subtitle delay or speed adjustment is active.
- Reset queue tracking after seeks and fix invalidation races involving in-flight frames, failed delivery and consumer reconnection.
- Safely fall back when consumer frame durations are zero or unavailable.

See the [full changelog](docs/releases/v3.2.1.930.md) and [release downloads](https://github.com/bLightZP/xy-VSFilter/releases/tag/v3.2.1.930).

## v3.2.1.920 (2026-10-30)

- Stage resolution edits until Apply/OK, validate custom dimensions before changing settings, and recover safely from invalid saved sizes.
- Correct **Render To Original Video Size** to use actual video dimensions separately from logical subtitle layout.
- Allocate combining buffers only when needed and size them to occupied subtitle bounds, preserving frames retained by the consumer.
- Check allocation sizes and blur work, propagate allocation/rendering failures safely, and clean up partial output.
- Refresh static and queued frames after settings changes; preserve alpha conventions and transparent gaps when combining bitmaps.
- Add layout, property-page, allocation-failure, retained-frame, and queue-clearing regression tests. Win32 and x64 passed **74 contract runs** in total, with matching output across **54 fixtures and 2,428 credits frames per architecture**.

See the [full changelog](docs/releases/v3.2.1.920.md) and [release downloads](https://github.com/bLightZP/xy-VSFilter/releases/tag/v3.2.1.920).

## v3.2.1.910 (2026-10-28)

- Add the optional **Prefer libass rendering** setting for ASS/SSA in both filter paths, with embedded subtitles, attached fonts, and style overrides. Statically link libass 0.17.4, FreeType 2.14.1, HarfBuzz 11.5.1, and FriBidi 1.0.16.
- Add **Win32 and x64 release packages**, matching symbols, dependency sources/licenses, and standalone benchmark consoles.
- Optimize native ASS command parsing and scalar-parameter reuse, compact tag storage, and reduce repeated text, style, and ownership allocations.
- Reuse eligible layouts, vector geometry, and draw descriptors; prepare upcoming events; reduce sorting/grouping work; and fuse single-color mask generation with SSE2 alpha blending.
- Correct cache-capacity, zero-capacity bypass, bitmap ownership/identity, and karaoke-key behavior; reduce edge-blur scratch memory.
- Expand synthetic rendering, ownership, grammar, cache, and streaming-packet tests and performance diagnostics.

See the [full changelog and performance results](docs/releases/v3.2.1.910.md) and [release downloads](https://github.com/bLightZP/xy-VSFilter/releases/tag/v3.2.1.910).

## v3.2.1.900 (20261023)

See the [optimization changelog](docs/releases/v3.2.1.900.md).

## v3.2.0.810 (20240123)

* Fix VsFilter crash on load/release (as external DirectVobSub in players). Regression since v804.

## v3.2.0.809 (20240102)

Updates, fixes and some backports from Cyberbeing/xy-VSFilter by TheOneric, Masaiki and Cyberbeing

* Gracefully handle negative Encoding values
  more on this: https://github.com/pinterf/xy-VSFilter/commit/102897189902658ec68ef691eb3dd8d89f099985
* Update CSRI name
* Use distinct CSRI name and update CSRI version:
  Adjust the name so users can distinguish Cyberbeing/xy-VSFilter
  from pinterf/xy-VSFilter if builds from both are placed into Aegisub’s csri directory
* fix: time shift when playing external sup files
* fix: close sup file and read stop time of sample
* Introduce LayoutRes{X,Y} script headers. See details here:
  https://github.com/pinterf/xy-VSFilter/commit/8a46df19666191f6fcaa44811123c34f165f358e

## v3.2.0.808 (20221116)
* Fix Possible unhandled exception in Masksub (https://github.com/pinterf/xy-VSFilter/issues/36)
* Fix: no more vertically flipped MaskSub result for RGB32
* remove latest scaling workaround
  More on that: https://github.com/pinterf/xy-VSFilter/pull/34

## v3.2.0.807 (20210522) - WIP
* fix: scale between subtitle and video on CWord (Masaiki) (Issue: frx and fry tags wrong scaling)

## v3.2.0.806 (20210412)

* proper WebVTT support fixes by adipose/MPC-HC

## v3.2.0.805 (20210412)

* WebVTT support, embedded subtitles as well.
  99% of it extracted from MPC-HC https://github.com/clsid2/mpc-hc, thanks to MPC-HC developers.
* Changed the Subtitles library timestamp to REFERENCE_TIME (64 bit) internally.
  For very-very long clips (use case: streaming) (slydiman)
* Fix: ASS subtitles: styles with space after their names would fail to work (MPC HC/BE + external XySubFilter)
  (report: MatiasMovie, solution by astiob)
* Various code syntax fixes (clang compiler)

## v3.2.0.804 (20210306 - pinterf)
* Add native YV16, YV24, P210 (YUV422P10) and P216 (YUV422P16) support.
  From TextSub Avisynth filter they even work.

## v3.2.0.803 (20210305 - pinterf)
* Source: new code base in new branch which finally works with Visual Studio 2019:\
  https://github.com/Cyberbeing/xy-VSFilter/tree/xy_sub_filter_rc5
* upstream:
  * (cyberbeing) Revert MPC-HC Tag Cache, restore xy Tag Cache
  
   Fixes a significant performance regression on certain SSA/ASS samples.
  * (jesec) subtitles, subpic: enable SSE2 unconditionally
     Target OS (10.0) required users to have SSE2
     It is meaningless to maintain backward compatibility
  * (jesec) Unicode conversion for file src/dsutil/DSUtil.cpp
     Remove support for DX7
  * (jesec) Rasterizer::Rasterize: use optimized memsetSSE2.
    Source: https://www.agner.org/optimize/
  * (cyberbeing) Add VS2019 environment
* Active branch changed to xy_sub_filter_rc5 on my (pinterf) side as well
* Avisynth plugin
  * re-add my changes from 2018
  * Add support for YV16
  * Throw error on unsupported formats
  * Avisynth plugin: keep frame properties when Avisynth+ interface is v8
* version set manually in src/filters/transform/vsfilter/version_in.h

## v3.2.0.802 (20181031)
* Change version numbering.
  
  Cyberbeing has released a patch for their original xy-VSFilter, their version stepped to v3.1.0.752
  
  Since I thought the project was full-dead, my version numbering kept - after a range skip - the original numbering.
  
  Now I'm changing my versioning scheme to 3.2.x.x to avoid possible future collision.
  
* Fix TextSub crash that could occur in rare circumstances (shssoichiro)

* Fix for SSA/ASS repositioning becoming permanently disabled after typesetting was displayed\
  (madshi -> Cyberbeing v3.1.0.752 7th October 2018)
  
* Add BT.2020 Support\
  (madshi -> Cyberbeing v3.1.0.751 27th September 2018)

## v3.1.0.801 (20180904)
* fix random crash in x64 build (address truncated to 32 bits, Rasterizer::Draw and Rasterizer::FillSolidRect were affected)
* XySubFilter.DLL now compiled with v141_xp toolset instead of v141 (like VSFilter.DLL)
* From upstream (madshi, Cyberbeing)
  * Fix a possible infinite loop in the Real Text subtitle parser on 64-bit
  * Fix external SRT subtitles with italic tag not being flagged as repositionable
  * Disable repositioning for SSA/ASS subtitles with default position level
  * Add setting to allow repositioning of SSA/ASS dialog

- v3.1.0.800 (20180411)
* Bring source to XySubFilter-3.1.0.746 level:\
  Apply file differences between Cyberbeing XySubFilter-3.1.0.746 (source from the release pack) \
  and xy-VSFilter 3.0.0.306 mod.
* make the source VS2017 compatible again
* make VSFilter work again as an Avisynth plugin
* Versioning: 3.1.0 prefix is kept, build number goes from 800 and up for my mod manually in src/filters/transform/vsfilter/version_in.h

## v3.0.0.400 (20180405) (work in February 2018)
* Get the last known xy-vsfilter version (3.0.0.306) which was still working as the Avisynth plugin.\
  https://github.com/Cyberbeing/xy-VSFilter/releases/tag/3.0.0.306\
  Plan: have a native 10/16 bit compatible Avisynth TextSub filter.\
* xy-vsfilter 3.0.0.306 rebuilt with Visual Studio 2017
* Avisynth+ headers
* Native support for YUV420P10 and YUV420P16 formats in Avisynth filter TextSub and probably VobSub.
  Internal conversion to and from P010 and P016 with SSE2 support
* Still doesn't work in any MT mode (I think): automatically registers itself as MT_SERIALIZED to prevent troubles
* Avisynth filter MaskSub: new pixel_type (default: "RGB32") parameter. Can be set to "YUV420P10" or "YUV420P16"
* Versioning: 3.0.0 prefix kept, build number goes from 400 and up for my mod manually in src/filters/transform/vsfilter/version_in.h
