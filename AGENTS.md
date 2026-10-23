# Agent reference

Read [docs/knowledge-graph.md](docs/knowledge-graph.md) before changing architecture, subtitle parsing, rendering, pixel formats, or build targets. It maps the solution projects, both runtime paths, and the source files behind each relationship.

The project is a Windows C++/DirectShow solution on the `xy_sub_filter_rc5` branch. The main DLL targets are `VSFilter`, `XySubFilter`, and `xy_sub_filter_consumer`; shared code is under `src/subtitles`, `src/subpic`, `src/dsutil`, and `src/filters/transform/basevideofilter`. Use `VSFilter.sln` and each `.vcxproj` to verify build dependencies. `build_vsfilter.sh` and `.github/workflows/build.yml` show the VS2019 CI build path.

When changing subtitle behavior, inspect both `CDirectVobSubFilter` and `XySubFilter`/`XySubFilterConsumer` paths and add or run focused tests where practical. `test/unit_test` contains Google Test coverage for rendering and blending; its `main.cpp` currently selects `test_overall.h`.

Keep the graph synchronized with source changes. Always use CRLF line endings when writing Pascal or Delphi `.pas`, `.dpr`, or `.dfm` files.

For playback benchmarks, use a **Debug (`LOCAL`) build** of `Z:\zpAI\zplayer.exe` and its helpers. Unsigned Release builds cannot be used for these tests. The default subtitle track in `Subtitles_CPU_killer_on_end_credits.mkv` is the test track. Zoom Player TCP request `1150` returns renderer statistics; see [test/benchmark/README.md](test/benchmark/README.md) for the collector and protocol details. FFmpeg tools are in `F:\Git\Clip Chopper\ffmpeg`. Do not install new software.
