# Build and run

Solidara requires C++20, CMake 3.24 or newer, Qt 6.4 or newer (Core, Gui, Widgets, Concurrent, Test), and Open CASCADE 7.8 or newer. The supported development configurations currently target OCCT 7.x. The application uses OpenGL; a graphics driver is required for the viewport. The core tests require no display server. Viewport and application smoke tests require a native OpenGL-capable display; CTest automatically uses `xvfb-run` when available on Linux.

## Linux

On Debian 13, install the build dependencies:

```sh
sudo apt-get update
sudo apt-get install cmake ninja-build g++ qt6-base-dev \
  libocct-data-exchange-dev libocct-visualization-dev libtbb-dev libfontconfig-dev \
  xvfb xauth libgl1-mesa-dri fonts-dejavu-core
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug
./build/linux-debug/Solidara
```

The cloud workspace also supports installation without root on Debian 13:

```sh
bash scripts/bootstrap-linux.sh
source scripts/env-linux.sh
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug
./build/linux-debug/Solidara
```

The bootstrap verifies Debian package signatures/checksums using APT, extracts dependencies outside the checkout, and prepares their local CMake/runtime paths. Source the environment script in each new shell. It is a development environment, not a Linux application distribution. Dependency versions come from Debian 13 package metadata; the installed package versions are recorded by the bootstrap, not independently locked by the application manifest.

For a virtual software-rendered display, install `xvfb xauth libgl1-mesa-dri fonts-dejavu-core`, then run the application with `xvfb-run -a -s '-screen 0 1440x900x24' ./build/linux-debug/Solidara`. This validates Linux rendering only.

## Windows 10/11 x64

Choose the preset matching your installed Visual Studio version. Both require **Desktop development with C++**, an x64 MSVC compiler, a Windows SDK and Git. Setting `VCPKG_ROOT` only selects an existing vcpkg installation; it does not install vcpkg.

### Visual Studio 2026

The simplest entry point is the checked-in **`Solidara.sln` at the repository root**. Open it in Visual Studio 2026, select Release/x64 and use Ctrl+F5. The Solidara project invokes `scripts/build-windows.ps1`, which locates CMake 4.2+ from Visual Studio or PATH, prepares an isolated pinned vcpkg checkout under `%LOCALAPPDATA%/Solidara/Tools`, then configures and builds the executable. Install Git for Windows and the Desktop development with C++ workload with a Windows SDK and C++ CMake tools before the first build. Restart Visual Studio after installing command-line tools so it sees the updated PATH.

`Solidara.vcxproj` is a maintained Makefile project that delegates compilation to CMake. It does not contain absolute machine paths or duplicate dependency/linker settings. Debug and Release launch the actual executable under `build/visual-studio/<configuration>/Solidara.exe`. Source items are explicitly listed for safe IDE editing; add new files to the appropriate CMake target and the outer project's source list. CMake configures the inner solution in a different directory, so it cannot recurse into the launcher solution.

Build configures dependencies on demand, Rebuild cleans application targets and rebuilds, and Clean only cleans an existing build without cloning tools or installing dependencies. New managed vcpkg checkouts include complete Git history and file contents. Existing partial checkouts created by earlier Solidara versions are completed in place with an unfiltered refetch before vcpkg runs; this one-time download can be substantial. The helper verifies that historical objects are present before disabling the partial-clone filter. It preserves the working tree and package caches, and leaves the partial-clone settings intact if fetching fails. Git 2.36+ is needed for this migration (`--refetch`); current Git for Windows is recommended. Shared file locks prevent overlapping changes to the same build directory or tool checkout; close other builds if the lock cannot be acquired. The project disables the outer Visual Studio vcpkg integration and uses its own tooling root, keeping existing vcpkg installations untouched. The child PowerShell process uses a process-scoped execution-policy override to run the repository's build script; it does not change machine or user policy.

The solution entry point does not require manually running the commands below. They remain available for command-line users and generate a separate solution under `build/windows-vs2026-release`.

Use CMake **4.2 or newer**; this is the first version with the `Visual Studio 18 2026` generator. The `windows-vs2026-release` preset uses a separate build directory from VS 2022. The commands below install a pinned vcpkg checkout whose 2026-10-29 tool release recognizes VS 2026 and its v145 toolset. The project's `vcpkg.json` continues to pin the dependency baseline to Qt 6.8.3 / OCCT 7.9.1; tooling and library versions are independent.

From PowerShell in the Solidara checkout, install vcpkg if the target directory does not already exist:

```powershell
New-Item -ItemType Directory -Force C:/dev | Out-Null
git clone https://github.com/microsoft/vcpkg.git C:/dev/solidara-vcpkg
```

If this dedicated vcpkg checkout already exists, reuse it. Run the following commands one at a time and stop if one fails. Keep any local changes in that checkout before switching its revision; do not force the checkout.

```powershell
git -C C:/dev/solidara-vcpkg fetch origin
git -C C:/dev/solidara-vcpkg checkout 8f363bdc11359ede4bc51e5eb32a2617538049c8
& C:/dev/solidara-vcpkg/bootstrap-vcpkg.bat -disableMetrics
$env:VCPKG_ROOT = 'C:/dev/solidara-vcpkg'
Test-Path "$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --preset windows-vs2026-release
cmake --build --preset windows-vs2026-release
ctest --preset windows-vs2026-release
./build/windows-vs2026-release/Release/Solidara.exe
```

`Test-Path` must report `True`. If it reports `False`, correct `VCPKG_ROOT` or finish installing vcpkg before configuring. To work in the IDE, run `cmake --open build/windows-vs2026-release` after configuration succeeds. Select Release/x64, set the Solidara executable as the startup project, and use Ctrl+F5. CMake may generate `.slnx` instead of `.sln` with recent VS 2026 toolsets, so `cmake --open` avoids assuming a solution extension.

To package this configuration:

```powershell
cpack --config build/windows-vs2026-release/CPackConfig.cmake -C Release -B build/packages
```

The preset and upstream tool support have been checked on Linux; actual compilation and execution with VS 2026 remain unverified. The existing Windows CI job uses VS 2022.

### Visual Studio 2022

Install Visual Studio 2022 with **Desktop development with C++**, a Windows SDK, Git, and CMake 3.24+. Run these commands in PowerShell from the repository:

```powershell
git clone https://github.com/microsoft/vcpkg.git C:/dev/vcpkg
git -C C:/dev/vcpkg checkout ef7dbf94b9198bc58f45951adcf1f041fcbc5ea0
C:/dev/vcpkg/bootstrap-vcpkg.bat -disableMetrics
$env:VCPKG_ROOT = 'C:/dev/vcpkg'
cmake --preset windows-release
cmake --build --preset windows-release
ctest --preset windows-release
./build/windows-release/Release/Solidara.exe
cpack --config build/windows-release/CPackConfig.cmake -C Release -B build/packages
```

The manifest pins vcpkg's 2025.06.13 baseline to Qt 6.8.3 and OCCT 7.9.1. Initial dependency compilation is substantial; reuse the vcpkg binary cache. Development downloads require internet access. Application modeling, files, help, and export use only local resources.

CTest runs geometry/persistence tests, real viewport interaction tests, and a complete application smoke workflow. On Linux it also reruns core and application smoke tests under a seccomp filter that denies IPv4/IPv6 socket creation while preserving local Unix sockets for X11; the filter verifies itself before executing the tests. Smoke artifacts are written under the build directory in `smoke-test/` and `smoke-test-offline/`.

The Windows GitHub workflow performs configure, compilation, core/viewport/application tests, and portable ZIP packaging. Packaging resolves runtime DLLs, invokes `windeployqt` for platform plugins and MSVC runtime deployment, copies OCCT runtime resources, and collects dependency copyright notices. The ZIP runs from `bin/Solidara.exe`; it needs no installer. Packaging fails on unresolved DLL dependencies. Windows build, GUI behavior, and clean-machine portable execution must be verified on Windows before declaring a release; Linux test results cannot establish these.

Project source licensing has not been selected. Review the third-party redistribution obligations in DEPENDENCIES.md before publishing binary releases.

### Dependency checkout reports DNS failure

If `checkout-index` fails with `Could not resolve host: github.com` and `could not fetch ... from promisor remote`, update Solidara on `main` and Build again. The original launcher created a partial vcpkg clone. The pinned vcpkg tool extracts historical port files using a Git child environment containing only `GIT_INDEX_FILE` on Windows, so deferred blob downloads can lose normal Windows network/proxy settings. The helper now completes those downloads in the parent environment first; no manual cache deletion or project relocation is needed.

This change does not bypass network policy or provide an offline first installation. If the helper's ordinary Git fetch fails too, test `git ls-remote https://github.com/microsoft/vcpkg.git HEAD` in normal PowerShell and resolve that connection error before retrying. Normal application use remains offline after dependencies have been built.
