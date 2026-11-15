# Repository conventions

Read PROJECT_STATE.md before changing the implementation. Solidara is an offline native C++20 / Qt 6 / Open CASCADE application. Preserve functioning CAD operations and describe incomplete work explicitly.

- Keep geometry, parametric history, and persistence in the core, independent of Widgets. UI commands modify the model through transactional edits. Viewport owns only rendering and interaction.
- Use PascalCase types, camelCase members/functions, four-space indentation, RAII, and useful comments explaining non-obvious decisions.
- Never store regenerated face/edge indices as durable references. Use feature UUIDs and semantic geometry until a persistent naming system exists.
- Use the existing checkout; do not create a worktree unless requested.
- Use feature/short-description branches, concise imperative commit subjects, and focused PR descriptions covering behavior, validation, and limitations.
- Configure repository-local Git identity before commits: ZuruiKyoku <30306568+ZuruiKyoku@users.noreply.github.com>. Verify author and committer metadata. Preserve required platform attribution.
- Run the core tests and build the GUI after relevant changes. Test failure handling, file round trips, and real geometric measurements rather than merely checking non-null shapes.
- Keep PROJECT_STATE.md, build documentation, and offline help consistent with actual capabilities. Do not claim Windows validation from Linux results.
- Runtime must make no network requests. Downloads are permitted only for dependency installation.
- The owner has not selected a source license; do not invent one.
