# Solidara design system

The native workspace uses compact desktop controls, dockable panels and a dominant 3D viewport. `resources/theme.qss` centralizes UI styling. The viewport's geometry/display colors live in its rendering implementation.

| Role | Color |
|---|---|
| Application background | `#15191E` |
| Viewport / input fields | `#1C2329` |
| Panels | `#242C33` |
| Raised controls | `#303942` |
| Borders | `#3B454D` |
| Main text | `#E8EAE8` |
| Secondary text | `#A8B3B9` |
| Disabled text | `#78868E` |
| Copper / primary action | `#D48D4B` |
| Pressed accent | `#B87136` |
| Secondary teal | `#45A7A0` |
| Selection | `#53BFC2` |

The cube logo is an original implementation of the supplied copper-line reference. SVG master, 512 px transparent PNG and Windows multi-resolution ICO are bundled locally. The icon's silhouette and internal edges define three faces.

UI typography prefers Inter with Segoe UI/system sans-serif fallbacks. Numerical controls prefer JetBrains Mono with Consolas/system monospace fallbacks. No third-party fonts are bundled yet. Labels use 12 px, with an 18 px brand mark. Layouts use 8 px spacing and 4 px adjustments, 1 px borders and 3 px control corners. Toolbars use compact text labels; a complete operation-icon family remains future work.

Copper identifies primary actions, hover borders and headings. Teal identifies selected geometry and feature entries. Keyboard focus uses a copper border. Disabled actions are muted. Tooltips explain actual behavior and limitations; offline help describes the complete initial workflow. Animations and decorative effects are omitted. Dark theme is implemented; an accessible light theme is planned.
