# AviSynthMinus

**English** | [简体中文](README.zh-CN.md) | [日本語](README.ja.md)

AviSynthMinus is an audio and video frameserver forked from AviSynth+. It uses scripts to arrange editing, filtering, and format conversion, supplying processed audio and video to applications such as encoders and players on demand, without first generating a complete intermediate file.

The project follows a release strategy with separate stable and mainline tracks, allowing conservative fixes to ship promptly while new features and larger internal improvements continue to develop. We value automated testing and aim to preserve compatibility with existing AviSynth scripts and plugins while continuing to follow and incorporate relevant upstream improvements.

## Why this fork?

AviSynthMinus grew out of a different approach to development and releases. Upstream AviSynth+ development primarily proceeds on a single main branch, where fixes, new features, and larger internal changes accumulate together. When official releases are far apart, users also have to wait longer to receive fixes through an official release. We want to separate stable maintenance from mainline development so that verified fixes can ship independently while larger changes continue through development and testing.

This fork will continue to follow AviSynth+ development and incorporate suitable fixes and improvements. For features already under development upstream, we prefer to integrate them once completed, minimizing the risk of incompatible implementations. We also welcome upstream adoption of this project's fixes and improvements, with their source and contributor attribution preserved.

## Release channels

AviSynthMinus plans to maintain two release tracks, distinguished by the minor version (`minor`) in `major.minor.patch`:

| Channel | Minor version | Purpose |
|---|---|---|
| Stable | Even, such as `0.2.x` | Focuses on fixes and conservative improvements, for users who prefer fewer behavioral changes. |
| Mainline | Odd, such as `0.1.x` and `0.3.x` | Introduces new features and larger internal changes, for users willing to try new developments and provide feedback. |

Release channel and prerelease status are separate designations: a mainline release is not necessarily a prerelease, and a stable release may have release candidates for testing. See each release's notes for its status, changes, and known issues.

We favor small, relatively frequent releases so that useful fixes reach users sooner. There is no fixed release schedule; timing depends on the readiness of changes and the maintainer's available time.

## Download and installation

Choose a version from [Releases](https://github.com/HomeOfAviSynthPlusEvolution/AviSynthMinus/releases) and read its changes, installation requirements, and known issues.

Windows users can choose a mod installer or a files-only package. The current mod installer requires x64 Windows 10 or later and an existing official AviSynth+ 3.7.5 installation. It replaces the installed x86 and/or x64 cores, preserves existing plugins and other components, and does not add architectures that are not already installed.

The mod installer backs up the cores it replaces and checks whether they can be restored during uninstallation. Close applications using AviSynth before installing or uninstalling the mod. Uninstall the mod before upgrading or uninstalling official AviSynth+.

Files-only packages are intended for manual deployment or users who manage their own runtime environment. Available architectures and runtime dependencies for Windows, Linux, and macOS are specified by the downloads and notes for each release. Choose the architecture that matches the application loading AviSynth.

## Compatibility and platforms

AviSynthMinus aims to preserve compatibility with existing AviSynth scripts and plugins, and to maintain API and ABI compatibility with AviSynth+ wherever practical. Compatibility does not mean freezing the implementation: when improving architecture and behavior, we assess the impact on existing usage and explicitly document necessary breaking changes and migration steps in release notes.

The project targets Windows, Linux, and macOS, but the extent of validation may differ across operating systems and processor architectures. A successful automated build shows that the code compiles in that environment; it does not mean every script, plugin, and host application has been tested at runtime. Plugins and host applications also have their own platform, architecture, and runtime requirements.

Compatibility reports are welcome. Please include the AviSynthMinus version, operating system and architecture, host application and relevant plugin versions, and a minimal script that reproduces the issue. If the same usage behaves differently in AviSynth+, include the version and results used for comparison.

The minimum supported macOS version is macOS 15.0 (Sequoia), for both Intel x86_64 and Apple Silicon arm64. Earlier macOS versions are outside the project's support scope.

The former `<avs/filesystem.h>` compatibility header has been removed from the SDK. Source code that included it should include `<filesystem>` and use `std::filesystem` directly.

### Expr expressions

`Expr` and `IrisExpr` use the [Iris engine](third_party/iris/README.md). `Expr` preserves the legacy argument names, types, and positions. Its `optAvx2`, `optSingleMode`, `optSSE2`, and `optVectorC` flags are accepted but ignored; they no longer restrict CPU instructions or processing width.

The new `backend`, `optimize`, and `lut_max_mb` arguments follow the legacy arguments. Use `backend` to select execution; omitting it uses the build default. `IrisExpr` provides the interface without the legacy flags. See the Iris documentation for expressions and backend options.

## Quick start

After installation, create a plain text file named `version.avs` containing:

```avs
Version()
```

Open it in a player, editor, or encoder that supports loading AviSynth scripts. It should generate a video displaying version information, allowing you to check which core version the host application actually loaded. This example requires no external media files or additional plugins.

An `.avs` file describes audio and video processing steps and is loaded and executed by a host application. To process your own media, select a source filter or plugin appropriate for the input format, then add processing steps such as editing and resizing.

## Building and testing

Initialize the pinned submodules before configuring, including after checking out a different parent revision:

```sh
git submodule update --init --recursive
```

The independent [Audio](third_party/audio_convert/README.md), [Video](third_party/video_convert/README.md), and [Composite](third_party/composite/README.md) modules supply computational kernels and are linked statically into the core. The host retains script interfaces, frame management, and CPU policy. See the [Composite adapter](modules/composite/README.md) for integrated filter behavior and numerical allowances.

The project requires CMake 3.8 or newer and a C++17 compiler and standard library with `std::filesystem` support. Run the following commands from the repository root. They build only the core library, without compiling the bundled external plugins; the resulting core can still load compatible plugins normally.

Iris is enabled by default and linked statically into the core. The default configuration requires LLVM 20–23 and automatically downloads and builds SLEEF; the default backend is `sleef`. If CMake cannot locate LLVM, pass `-DLLVM_DIR=<LLVM installation>/lib/cmake/llvm` when configuring.

Use `IRIS_LLVM` and `IRIS_SLEEF` to change the dependency configuration. Missing requested dependencies cause a configuration error. `ENABLE_IRIS=OFF` removes both `Expr` and `IrisExpr`.

**Windows:**

Windows XP defaults to scalar Iris; Windows ARM64 uses LLVM without SLEEF. Windows builds link LLVM statically into `avisynth.dll`; no separate LLVM DLL is deployed. Modern Windows x86 and x64 use LLVM with SLEEF by default. The LLVM component libraries must match the core’s C/C++ runtime configuration (`/MD` for the standard Release build).

Example using Visual Studio 2026 for x64:

```powershell
cmake -S . -B build/core -G "Visual Studio 18 2026" -A x64 -DBUILD_SHARED_LIBS=ON -DENABLE_TESTS=OFF -DENABLE_PLUGINS=OFF
cmake --build build/core --config Release --parallel
```

You can also build with clang-cl on Windows. In some processing scenarios, clang-cl builds may perform better than MSVC builds. Results depend on compiler versions, build options, and the processing involved, so compare them using your actual workloads.

**Linux / macOS (requires Ninja and an appropriate C++ toolchain):**

Linux uses the system LLVM runtime. Windows and macOS CI download checksum-pinned [static LLVM SDKs](https://github.com/HomeOfAviSynthPlusEvolution/llvm-static-builds/releases/tag/llvm-23.1.1-r1); macOS embeds LLVM in the core and targets macOS 15.0 or newer.

```sh
cmake -S . -B build/core -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON -DENABLE_TESTS=OFF -DENABLE_PLUGINS=OFF
cmake --build build/core --parallel
```

**Running tests:**

The repository provides test presets for Visual Studio 2026 x64. The test configuration uses a static core library and downloads GoogleTest and xxHash through Git during initial configuration, so Git and access to the dependency repositories are required.

```powershell
cmake --preset msvc-release
cmake --build --preset msvc-release --parallel
ctest --preset msvc-release
```

With other toolchains, configure tests in a separate build directory using `-DENABLE_TESTS=ON -DBUILD_SHARED_LIBS=OFF -DENABLE_PLUGINS=OFF`, then run them through CTest. Tests cover core and scripting behavior, audio and video conversion, filters, and regression cases; coverage continues to evolve with development.

## Development and contributions

AviSynthMinus is currently led by its maintainer, who is responsible for technical direction, decisions about changes, and final releases. Bug reports, suggestions, code contributions, and technical discussions are welcome. For new features, compatibility changes, or substantial architectural changes, we recommend discussing the goals and approach in an issue first.

This project uses AI-assisted development, including implementation, test writing, and code review. The maintainer provides guidance, evaluates changes, and remains responsible for what is accepted and released. Contributions should clearly explain the problem, the implementation approach, and how the change was validated. Please also disclose how AI was involved when contributing with AI assistance.

Use [GitHub Issues](https://github.com/HomeOfAviSynthPlusEvolution/AviSynthMinus/issues) to report problems, including a reproducible example wherever possible. When submitting a fix, we encourage you to include regression tests that reproduce the original problem and verify the correction. Technical disagreements can be discussed openly; the maintainer makes the final decisions on project direction and merging changes.

## Documentation, acknowledgments, and license

The bundled [user documentation](distrib/docs/english/source/avisynthdoc/index.rst) and [plugin development resources](distrib/docs/english/source/avisynthdoc/FilterSDK) are largely inherited from AviSynth+ and are currently primarily in English. Some material may not yet reflect changes in this fork, so read it alongside the relevant release notes.

AviSynthMinus builds on the longstanding work of AviSynth, AviSynth+, and their contributors. The automated tests were ported from [AviSynthPlus-UT](https://github.com/HomeOfAviSynthPlusEvolution/AviSynthPlus-UT) and continue to be expanded here. Thanks to upstream developers, plugin authors, and everyone who tests, reports issues, and contributes improvements.

Thanks to [SB.SB](https://sb.sb) for sponsoring the LLM subscription used in this project's development.

See the repository's [license information](distrib/docs/english/source/avisynthdoc/license.rst) and [full GPL text](distrib/gpl.txt) for licensing terms. Some interfaces include additional licensing exceptions, and third-party components may use different licenses; refer to the declarations in the respective files.
