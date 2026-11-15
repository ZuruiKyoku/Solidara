# Architecture

## Model and geometry

`solidara_core` depends on Qt Core and OCCT, not Widgets. `Document` holds an ordered feature history and named length expressions. Every `Feature` has a UUID, kind, name, expression-valued dimensions, input UUIDs and suppression state. Input references must resolve to earlier features, so forward references and cycles are rejected. Duplicate UUIDs, missing dimensions, invalid units and unsupported operations fail explicitly.

`Model::apply` constructs candidate geometry before replacing the live document. OCCT builds exact planar faces and B-rep solids, performs Boolean cuts and fillets, then validates solids with `BRepCheck_Analyzer`. The final body is the last unsuppressed solid feature. Profiles remain separate history entries. Unchanged feature definitions reuse cached shapes if no dependency changed. Parameter changes currently invalidate all features, rather than only consumers of the changed parameter.

Input geometry is copied before kernel builders operate on it, isolating workers from live display geometry. Failed operations report the feature name and preserve document, geometry and undo state. Undo/redo uses snapshots of feature definitions (up to 100 edits) and regeneration, not serialization of transient pointers.

## Profiles and references

Rectangle profiles are analytically constructed closed wires with fixed horizontal/vertical sides and explicit width/depth. Circle profiles use exact center/radius definitions. This solves only those fixed templates; there is no general constraint graph, numerical constraint solver, freeform profile detection or mouse sketch editor.

Profiles lie on XY-parallel planes. A circle can reference a body's upper Z bounding plane plus an offset, which is recomputed after upstream edits. Cuts relocate a copy of the profile and extrude a tool through the entire body's Z bounds. This is a through-all cut, not a general face-normal operation. Fillets apply to all edges. Face or edge indices are never persisted. General topological naming and selected-edge fillets require future work.

## Expressions

The bounded recursive-descent evaluator supports arithmetic, parentheses, unary signs, scientific notation, parameter references and mm/cm/m/in conversion. It tracks length powers for explicit units and detects circular references. Named parameter values are memoized during evaluation. Bare scalar results become millimeters at a length boundary. Consequently `width = 100 mm; width + 2 mm` is valid while `width + 2` is rejected. Expressions that produce areas or inverse lengths cannot drive model dimensions.

## Persistence and exchange

The `.solidara` format is a UTF-8 JSON container with `format: solidara`, integer `version: 1`, `units: mm`, document name, parameters, and feature records. Geometry is rebuilt from definitions on load. UUIDs, dimensions and dependencies survive round trips. Unsupported versions are rejected, never silently downgraded; add explicit migration functions when a future schema is introduced.

Loading validates types, limits and geometry in a candidate before changing the document. Loading starts fresh undo history. `QSaveFile` atomically replaces native files. STL/STEP export writes kernel output to a temporary file, then atomically publishes the bytes. STL meshes a private copy at a requested chord tolerance. STEP read-back exists in the core, but imported features and import UI are not implemented.

Current limits: 500 features, 256 named parameters, 512 characters per expression, 64 expression/dependency nesting levels, 10 MB native input files, positive dimensions at least 1e-6 mm and coordinates/dimensions within ±1e6 mm.

## Presentation and concurrency

`MainWindow` owns document actions, tool dialogs, browser/history synchronization and an inspector. Mutations run on a candidate `Model` through Qt Concurrent. Controls are disabled while a task is running; the GUI thread receives a completed candidate only on success. Save/close prompts protect unsaved edits. Cancellation, autosave and multiple open documents are future work.

`Viewport` owns an OCCT AIS context, V3d view, OpenGL driver and platform window binding (X11 on Linux; WNT on Windows). It renders native B-reps and manages selection separately from camera gestures. It does not modify document definitions. Selection is transient. An AIS orientation cube and trihedron provide navigation cues. Persistent UI layout uses local QSettings.

All application resources are embedded. No Qt Network component or networking code is used. Offline tests block IPv4/IPv6 socket creation on Linux while exercising the application.

## Windows IDE entry point

The root `Solidara.sln`/`Solidara.vcxproj` pair is a maintained Makefile wrapper for Visual Studio 2026. Build commands run `scripts/build-windows.ps1`, which selects pinned dependency tooling and invokes the canonical CMake project in `build/visual-studio`. Debugger commands launch that build's configuration-specific executable. Generated inner project files and user settings remain ignored; only the portable outer entry point is tracked. C++ sources, libraries and runtime resources remain defined by CMake.
