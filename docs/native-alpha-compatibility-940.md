# Native alpha compatibility in 3.2.1.940

## Behavior

Both filters expose **Fix out of range alpha in native renderer** under
Main > Rendering, below **Prefer libass rendering**. It is enabled by default
and saved as `General\MpcBeAlphaCompatibility` in each filter's existing
settings. Switching it updates the active subtitle renderer and invalidates
previously evaluated layouts and queued output. Setting the same value again
does not clear the queue.

The native engine takes the low eight bits of targets for `\alpha`, `\1a`,
`\2a`, `\3a`, and `\4a` before evaluating animation. This matches the requested
MPC-BE recovery behavior. For example, the sample's `F00` target becomes `00`
before interpolation. Disabling the option restores interpolation toward the
original numeric target followed by eight-bit conversion. Targets from `00`
through `FF` have identical behavior in either mode. Libass rendering is
unchanged.

The policy uses one bitwise AND per alpha tag, outside the global-alpha
component loop. It adds no work to pixel blending and keeps cached parsed
numbers immutable. Existing parsing, layout, draw reuse, and bitmap cache
optimizations remain in place.

The About page displays 3.2.1.940, includes **AI optimizations & fixes by Yaron
Gur**, and provides the **Latest releases** link to
<https://github.com/bLightZP/xy-VSFilter/releases>. The link uses a standard
owner-drawn button so it works in hosts without common-controls v6 and supports
keyboard activation.

## Regression results

Release builds succeeded for VSFilter, XySubFilter, and the consumer on x64 and
Win32 with the installed Visual Studio v145 toolset.

Both architectures passed 14 focused contracts, including:

- 3,600 alpha frame comparisons per architecture covering all five alpha tags,
  in-range and out-of-range targets, both hex spellings, transform boundaries,
  backward seeks, and changes to a populated renderer. Libass is checked with
  the option enabled and disabled.
- Actual-DLL Main-page checks for the exact label, enabled default, layout,
  Apply, saved registry value, and reloading the value in another filter.
- Actual-DLL About checks for version, credit, and release-link text.
- Native and libass queue contracts, including setting changes and unchanged
  values, together with the existing parsing, layout, reconstruction, draw
  reuse, and scalar-parameter contracts.

All 54 public fixture rows match 3.2.1.930 on each architecture. All 536 frames
of the extracted `brokensubs1.mkv` subtitle track satisfy these comparisons:

- Enabled native policy matches the reference with the 40 `F00` targets
  replaced by `00`.
- Disabled native policy matches 3.2.1.930 native output.
- Libass output matches 3.2.1.930 libass output.

Detailed logs and local binaries are in
`benchmark-results/alpha-940-20261031/`; `validation.json` records the results.
The test processes use isolated registry settings and do not register DLLs.

## Offline performance comparison

The end-credits stress track was rendered over all 2,428 frames in four
fresh processes per architecture, ordered 930 / 940 / 940 / 930. Both versions
used CPU 7, Above Normal priority, the same fonts and render settings, and
disabled diagnostics. Computer Use was inactive. Every frame hash matched
3.2.1.930 in every pass.

Median totals from two passes per version:

| Architecture | 930 thread CPU (s) | 940 thread CPU (s) | CPU change | 930 p99 frame (ms) | 940 p99 frame (ms) |
| --- | ---: | ---: | ---: | ---: | ---: |
| x64 | 25.633 | 24.594 | -4.1% | 39.669 | 38.493 |
| Win32 | 25.891 | 24.719 | -4.5% | 40.941 | 38.912 |

This comparison found no performance regression. The modest timing differences
are not evidence of a general speedup; scheduling and clock variation remain
possible. These are offline subtitle renders, not playback or madVR drop
measurements. Raw passes and wall/max timings are in `performance.json` in
the same local results directory.
