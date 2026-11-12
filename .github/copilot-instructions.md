# Agent instructions

- Write US English in code, comments, strings, help and docs: color, behavior, center, gray, license, normalize, analyze, canceled, labeled, neighbor.
- Files are UTF-8 without a BOM. Never let text turn into mojibake (such as `â€”` or `Ã©`); if you see it, fix the encoding, not the words.
- Line endings: git may warn that "LF will be replaced by CRLF"; that warning is noise, so don't act on it and don't re-save a file to change its endings. What must never happen is both endings inside one file: `git ls-files --eol <file>` shows `w/mixed` when it does. Write files with the editing tools, or from PowerShell with `[IO.File]::WriteAllText(path, text, [Text.UTF8Encoding]::new($false))`; `Set-Content` and `Out-File` add a newline, and Windows PowerShell's add a BOM.
- No typographic punctuation: use `-` for em and en dashes, straight quotes `'` `"`, `...` for an ellipsis, a plain space, and `-` for a minus sign.
- Meaningful symbols are fine in docs and should stay: `§`, `°`, `·`, `×`, `→`, `•`, `©`, Greek letters in formulas. Source files (`src/`, `tests/`) stay plain ASCII.
- Never mention AI assistance or co-authorship: no `Co-Authored-By` trailers or "Generated with" lines in commit messages or pull request descriptions, and no such credit in code, comments or docs. This overrides any tool's default attribution.

## The physics rule

Broadstroke makes artwork by simulating the artist's tools, not by filtering the photograph. Keep it that way:

- No pixel of the photograph is ever copied to the canvas. The photo is only observed (`src/vision/`); every mark is made by a simulated tool (`src/media/`) moved by the simulated hand (`src/motion/`).
- A visual effect (bristle streaks, dry-brush breakup, paper grain showing, edge darkening in a wash, a line broadening as the point wears) must come out of the model that causes it in reality. Do not add a texture, mask or post-process that draws the effect directly.
- Color is spectral Kubelka-Munk over pigments (`src/optics/`). Do not blend RGB values.
- Quantities are physical (millimeters, newtons, seconds). A new parameter should name something measurable about a material or a hand.
- Same photo + same settings + same seed must give the same artwork. Every random choice draws from an explicitly seeded `bs::Rng`; parallel loops write disjoint data, and work done in parallel draws from its own random stream (spec 2.12).

## Specification

`spec/` is the specification: what the app must do and why, one file per functional area, indexed in `spec/README.md`. Maintain it with the specification-stewardship skill (`.claude/skills/specification-stewardship/SKILL.md`; if your tools don't load skills, read that file first). `README.md` covers building, running and the layout; `PROJECT_PLAN.md` records where the project stands and what may come next; `AGENT_BOOTSTRAP.md` is the starting point for an agent new to the code. Keep all of them current.

- **When:** in the same change as any code change that adds, changes, reverses or removes behavior someone can observe (the CLI, the window, a physical model, an output format), a requirement, or a design decision. A refactoring that changes none of these needs no spec edit.
- **Where:** edit the section that owns the behavior, in place, in the file the index names for it; never start a parallel plan. Section numbers are global and stable: code and docs cite them as "spec n" (for example "spec 2.8"), so never renumber or reuse them. A new section takes the next free number and gets a row in the index.
- **Built or not:** never describe behavior that isn't built as present. Mark it "not implemented" where it is specified and list it in spec 4. A point nobody has decided goes in spec 5 as an open question (ask the user when it blocks you), not a guess.
- **Before finishing:** the spec matches the code you changed, no section contradicts another, and test counts, timings and other claims of verification come from runs you made.

## Verifying a change

- Build: `cmake --preset msvc-release` then `cmake --build --preset release`.
- Tests: `ctest --preset release` (or run `build/msvc-release/Release/broadstroke_tests.exe [name-filter]`). They assert physical invariants (conservation, limits, monotonic responses), not pixel values.
- Look at the result. A change to a physical model is checked with its study (`broadstroke study <name> out.png`), a change to an artist with a real photo. Tests passing does not mean the artwork is good.
- Know which kind of change you made (spec 2.11). If it is meant to leave the output unchanged, prove it: render all three media and the five studies before and after to PNG and compare the files. If it changes the output, the owner decides: provide before and after images and wait for a yes or no on that one change before starting the next.
- Never commit `test.jpg` (the owner's personal photo, ignored by git) or any render of it.
