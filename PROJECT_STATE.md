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

Windows build, runtime and portable packaging are configured but have not been executed in this Linux environment. Linux uses Qt's X11 platform and OCCT's native X11 window binding; a native Wayland viewport is unsupported. No source license has been selected.

## Architecture and dependencies

See [architecture](docs/architecture/OVERVIEW.md), [design system](docs/design/DESIGN_SYSTEM.md), [build guide](docs/development/BUILD.md) and [dependency licensing](docs/development/DEPENDENCIES.md).

Validated toolchain: Debian 13 x64, GCC 14.2, CMake 3.31.6, Ninja 1.12.1, Qt 6.8.2, OCCT 7.8.1. Windows vcpkg baseline pins Qt 6.8.3 / OCCT 7.9.1. Rootless Linux dependencies are retained outside the checkout and scripts restore their environment paths.

The native file format is version 1 JSON, fixed-mm documents, with explicit rejection of unsupported future versions. Undo snapshots are in-memory only. No face/edge index is persisted as a durable identifier. Parameter changes currently invalidate all feature geometry; independent unchanged feature definitions reuse cached shapes.

## Validation

The C++ application builds with warnings enabled. CTest runs the core QtTest suite, viewport QtTest suite, end-to-end native GUI smoke, and Linux core/GUI tests with IPv4/IPv6 socket creation denied by a verified seccomp policy.

Core suite: 38 QtTest results (36 substantive cases, init and cleanup), 0 failures/skips. Covers expression parsing and units, cyclic/DAG parameters, closed profile geometry, primitives, extrusion/cut/fillet, upstream edits, cache/suppression, translated bounds, rollback, undo/redo, invalid projects, editable native round trips, STL triangle dimensions and STEP volume round trips.

GUI smoke: rectangle100×60 → extrusion20 → circular hole → all-edge fillet → inspector width120 → undo100 → redo120 → native save/reopen → STL/STEP export. Artifacts are generated in `build/linux-debug/smoke-test` and `smoke-test-offline`. These are Linux evidence only. Viewport suite: 7 QtTest results (5 scenarios, init and cleanup), 0 failures/skips. It exercises native rendering, mouse body/face selection, orbit/pan/zoom, orientation cube, model/display shape isolation and resizing inside a docked main window. Final CTest result: 5/5 suites passed. An additional installed-layout smoke run with packaged OCCT resources and blocked IPv4/IPv6 sockets also passed; Linux runtime libraries were supplied by the development environment, so this does not prove portable deployment. The committed screenshot and example project come from the passing GUI smoke run.

## Development and delivery

Branch: `feature/parametric-modeling`. The base checkout originally contained only README.md. Commits use repository-local identity ZuruiKyoku <30306568+ZuruiKyoku@users.noreply.github.com>. Build outputs and dependency archives are excluded from Git.

Implementation commit: `bdbe0eb` (`Add offline parametric CAD modeling workflow`). Both author and committer match the required identity.

GitHub read access through the existing Git proxy was verified. The actual push of `feature/parametric-modeling` was rejected with HTTP 403: permission to ZuruiKyoku/Solidara.git denied. GitHub API access also returns Forbidden, so no pull request was created and remote CI was not triggered. The implementation and delivery documentation are committed locally; no default branch or remote history was changed.

A Git bundle is provided outside the checkout at `/workspace/Solidara-delivery.bundle` for transfer. With appropriate GitHub write access, push `feature/parametric-modeling` and open a PR against main. Review the Windows CI results before claiming a Windows release.

The cloud environment draft has saved installation and startup instructions. Publication is a separate user action. A restored future task has not been verified, and local-only commit restoration through environment snapshots is not guaranteed; retain the Git bundle or push the branch before relying on snapshot restoration.

## Next development priorities

1. Validate Windows CI and portable package on Windows 10/11, including graphics drivers, high DPI, non-ASCII paths and offline startup.
2. Add interactive sketch entity editing and integrate a proven geometric constraint solver with conflict/degree-of-freedom feedback.
3. Implement robust semantic/topological references for arbitrary face attachment and selected-edge fillets.
4. Add autosave/recovery, multiple documents and more detailed UI automation for modal creation/error paths.
5. Expand single-body features and then introduce explicit body/component ownership; avoid presenting speculative tool buttons as functional.
