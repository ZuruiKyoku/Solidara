# Dependencies and redistribution

The repository owner has not selected a project source license. This document records third-party dependencies; it does not grant a license to Solidara's source.

| Dependency | Development versions | Purpose | Licensing considerations |
| --- | --- | --- | --- |
| Qt Base | Debian 13: 6.8.2; pinned Windows manifest: 6.8.3 | Widgets, local JSON/files/settings, concurrent jobs, tests | Qt Base modules used here are available under LGPL-3.0/GPL/commercial terms. Dynamic linking does not eliminate notice, source availability, modification, or relinking obligations. Test tooling has its own applicable upstream notices. |
| Open CASCADE Technology | Debian 13: 7.8.1; Windows: 7.9.1 | Exact B-rep modeling, display, STEP and STL | LGPL-2.1 with OCCT's additional exception. Preserve the actual upstream LICENSE_LGPL_21.txt and OCCT_LGPL_EXCEPTION.txt terms and notices; assess distribution obligations. |
| FreeType | Resolved by Debian/vcpkg | OCCT text rendering | FreeType License or GPL-2.0; retain the selected license and attribution. |
| oneTBB | Debian system dependency | OCCT runtime parallelism | Apache-2.0; preserve notices. Windows manifest does not explicitly enable optional OCCT TBB support. |
| OpenGL and platform libraries | Operating system | Native graphics context and window integration | Platform/driver dependent; Windows OpenGL system components are not packaged. |
| CMake, Ninja, compiler | Build-time only | Build and packaging | Not runtime dependencies; preserve notices if redistributing tools. |

The Windows baseline is `ef7dbf94b9198bc58f45951adcf1f041fcbc5ea0` (vcpkg tag `2025.06.13`). The manifest explicitly enables Qt Widgets, Concurrent, OpenGL, thread support, Test, and the Windows deployment tool. It avoids unrelated SQL and web modules. `windeployqt` itself can depend on Qt Network; the application does not link a networking API or invoke an online service.

For Visual Studio 2026, the build guide pins the vcpkg tooling checkout to `8f363bdc11359ede4bc51e5eb32a2617538049c8`, which bootstraps tool release `2026-10-29` with VS 2026/v145 detection. This does not replace the manifest's library baseline. VS 2022 CI continues to use its existing tooling pin.

Transitive dependency versions and exact license texts are provided by the resolved package manager. Windows packaging includes every installed vcpkg package's `share/<package>/copyright` file under `licenses/`. This notice collection is useful evidence, not a substitute for satisfying full redistribution obligations or reviewing the release artifact. Keep modified library source and relinking information available where required.

The UI uses installed system fonts. No downloaded font binaries or external font services are required. Solidara's cube artwork is original to the product brief; no Autodesk assets are used.
