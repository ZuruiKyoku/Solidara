# Project state

Solidara's long-term goal is an offline Windows CAD tool for maker parts, with real parametric geometry and local ownership. The current deliverable is a working initial modeling slice, not complete Fusion 360 parity.

## Implemented and tested on Linux

- C++20 / Qt 6 Widgets application with native OCCT viewport, copper branding, dockable model browser, inspector and timeline, local panel settings and offline help.
- Exact XY-parallel rectangle/circle profiles, box/cylinder primitives, extrusion, world-Z through-cuts and all-edge fillets.
- Editable dimension expressions, named parameters and units (mm/cm/m/in), dependency checks, incremental shape reuse, feature rename/suppression/removal, transactional regeneration and undo/redo.
- Atomic JSON `.solidara` save/load preserving UUIDs and editable feature definitions. Invalid files and failed operations preserve the current document.
- STL mesh export with adjustable tolerance and exact STEP export. Core STEP read-back, binary/ASCII STL, exported bounds and volume round trips tested.
- Camera orbit/pan/zoom/fit, standard views, orientation cube, perspective, wireframe, geometry selection and highlight.
- Bounding dimensions, origin, volume, area and fixed 220 × 220 × 250 mm dimensional fit feedback.
- Background geometry and export operations; model and display geometry ownership isolated.

## Partial or absent functionality

Sketches are analytically dimensioned profile templates with fixed geometry, not freehand editable entities or a numerical constraint solver. There is no general constraint status or arbitrary closed-profile recognition. Circle attachment follows a body's upper bounding plane rather than a selected face. Fillets apply to all edges, not persistent selected edges. Each history exposes one final solid; independent body management and assemblies are absent.

Missing: arbitrary planes, line/arc/spline sketch tools, general constraint solving, topological naming, revolve/sweep/loft, union/intersection tools, shell/chamfer, patterns/transforms, mesh/STEP import UI, configurable printer profiles, multi-document editing, autosave/recovery, light theme, drawings, CAM and simulation. Export/regeneration cancellation is absent. Viewport copy and tessellation still run on the GUI thread and can pause interaction on complex models.

Windows build, runtime and portable packaging are configured but have not been executed in this Linux environment. VS 2022 uses `windows-release`; VS 2026 uses `windows-vs2026-release` and requires CMake 4.2+ plus the newer vcpkg tooling pin documented in the build guide. Preset parsing and the unchanged Linux configuration are validated locally; neither Windows compiler has been executed here. Linux uses Qt's X11 platform and OCCT's native X11 window binding; a native Wayland viewport is unsupported. No source license has been selected.

## Architecture and dependencies

See [architecture](docs/architecture/OVERVIEW.md), [design system](docs/design/DESIGN_SYSTEM.md), [build guide](docs/development/BUILD.md) and [dependency licensing](docs/development/DEPENDENCIES.md).

Validated toolchain: Debian 13 x64, GCC 14.2, CMake 3.31.6, Ninja 1.12.1, Qt 6.8.2, OCCT 7.8.1. Windows vcpkg baseline pins Qt 6.8.3 / OCCT 7.9.1. Rootless Linux dependencies are retained outside the checkout and scripts restore their environment paths.

The native file format is version 1 JSON, fixed-mm documents, with explicit rejection of unsupported future versions. Undo snapshots are in-memory only. No face/edge index is persisted as a durable identifier. Parameter changes currently invalidate all feature geometry; independent unchanged feature definitions reuse cached shapes.

## Validation

The C++ application builds with warnings enabled. CTest runs the core QtTest suite, viewport QtTest suite, end-to-end native GUI smoke, and Linux core/GUI tests with IPv4/IPv6 socket creation denied by a verified seccomp policy.

Core suite: 38 QtTest results (36 substantive cases, init and cleanup), 0 failures/skips. Covers expression parsing and units, cyclic/DAG parameters, closed profile geometry, primitives, extrusion/cut/fillet, upstream edits, cache/suppression, translated bounds, rollback, undo/redo, invalid projects, editable native round trips, STL triangle dimensions and STEP volume round trips.

GUI smoke: rectangle100×60 → extrusion20 → circular hole → all-edge fillet → inspector width120 → undo100 → redo120 → native save/reopen → STL/STEP export. Artifacts are generated in `build/linux-debug/smoke-test` and `smoke-test-offline`. These are Linux evidence only. Viewport suite: 7 QtTest results (5 scenarios, init and cleanup), 0 failures/skips. It exercises native rendering, mouse body/face selection, orbit/pan/zoom, orientation cube, model/display shape isolation and resizing inside a docked main window. Final CTest result: 5/5 suites passed. An additional installed-layout smoke run with packaged OCCT resources and blocked IPv4/IPv6 sockets also passed; Linux runtime libraries were supplied by the development environment, so this does not prove portable deployment. The committed screenshot and example project come from the passing GUI smoke run.

## Visual Studio entry point

`Solidara.sln` and `Solidara.vcxproj` now provide a portable repository-root entry point for VS 2026. Build/Run delegates to CMake through a PowerShell script, preparing an isolated pinned vcpkg checkout under local application data instead of requiring manual setup. Debug/Release, Rebuild and download-free Clean are supported. Application build definitions remain in CMake; generated files live under `build/visual-studio`.

Validation: the solution/project XML, project identity, source paths and configuration mappings were checked; PowerShell 7.5.4 on Linux passed 15 build-helper cases with one Windows-only Clean case explicitly skipped. The script uses Windows PowerShell 5.1-compatible syntax, with those checks added to Windows CI. The existing application rebuilt successfully and all 5 CTest suites passed. Windows SDK/MSVC execution and IDE F5 behavior cannot be verified on this Linux machine. Follow the Windows CI results and local Build Output rather than treating script checks as a successful Windows application build.

### Partial dependency checkout repair

A reported Windows build failure showed `checkout-index` attempting to fetch missing promised Git blobs, followed by `Could not resolve host: github.com`, while an ordinary DNS lookup succeeded. Upstream vcpkg-tool `2026-10-29` extracts port files with a Windows child environment containing only `GIT_INDEX_FILE`. Solidara's original partial-clone optimization exposed this path to deferred network requests.

The helper now creates complete clones and migrates older partial clones in place using `git fetch --refetch --no-filter origin`. It checks historical object availability before clearing the partial-clone settings, preserves the working tree and caches, and propagates fetch errors. This avoids network access during historical port extraction; initial source/package downloads still require connectivity. Validation on Linux/PowerShell 7.5.4: all 15 existing helper checks passed (one Windows-only Clean case skipped), and 7 real-Git regression cases passed. The regression reproduces failed historical checkout with an unavailable promisor remote, verifies failed migration preserves settings and local edits, then verifies all six historical port trees extract without connectivity after migration. Actual Windows compilation remains unverified here.

### Qt dependency manifest correction

The user's next Windows build log confirmed that partial-clone migration and historical port extraction succeeded. Dependency resolution then exposed an invalid `windeployqt` feature in the manifest: the pinned Qt 6.8.3#2 port builds that tool automatically on Windows. The invalid feature is removed, and PNG support is explicitly enabled for the application icon and screenshots. Local post-build deployment now selects Qt's configuration-aware imported tool so Debug builds use vcpkg's Debug paths and plugins. Windows CI also uses a complete dependency clone, matching the local helper's repair.

Validation: the pinned vcpkg tool reproduced the original manifest failure and resolved the corrected manifest into 20 Windows packages, including Qt 6.8.3#2 and OCCT 7.9.1. This was a dependency-plan-only dry run on Linux using an external overlay that preserves the Windows target settings while disabling Visual Studio environment/compiler detection. It did not compile or install Windows packages. Linux configure/build succeeded and all 5 CTest suites passed. Windows compilation and Debug deployment still require native validation.

## Development and delivery

The initial implementation was delivered on `feature/parametric-modeling` and merged into `main` through PRs #1 and #2. The checked-in Visual Studio solution is being delivered on `main` at the user's explicit request. The base checkout originally contained only README.md. Commits use repository-local identity ZuruiKyoku <30306568+ZuruiKyoku@users.noreply.github.com>. Build outputs and dependency archives are excluded from Git.

Implementation commit: `bdbe0eb` (`Add offline parametric CAD modeling workflow`). Both author and committer match the required identity.

The initial GitHub push was rejected with HTTP 403. After the connection was updated, commit `551fc26` was successfully pushed to `feature/parametric-modeling` and its remote SHA verified. GitHub API access still returned Forbidden when checking Actions, so remote CI results and PR creation remain unverified. No default branch or existing remote history was overwritten.

A Git bundle is provided outside the checkout at `/workspace/Solidara-delivery.bundle` for transfer. With appropriate GitHub write access, push `feature/parametric-modeling` and open a PR against main. Review the Windows CI results before claiming a Windows release.

The cloud environment draft has saved installation and startup instructions. Publication is a separate user action. After publication, the restored checkout was verified to retain the branch and implementation commits. Future snapshots should still be checked; retain a Git bundle or push work before relying on restoration.

## Next development priorities

1. Validate Windows CI and portable package on Windows 10/11, including graphics drivers, high DPI, non-ASCII paths and offline startup.
2. Add interactive sketch entity editing and integrate a proven geometric constraint solver with conflict/degree-of-freedom feedback.
3. Implement robust semantic/topological references for arbitrary face attachment and selected-edge fillets.
4. Add autosave/recovery, multiple documents and more detailed UI automation for modal creation/error paths.
5. Expand single-body features and then introduce explicit body/component ownership; avoid presenting speculative tool buttons as functional.
