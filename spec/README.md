# Broadstroke specification

This folder is the specification of Broadstroke: what the application must
do and the reasons for its main design choices. Broadstroke turns a
photograph into an artwork by simulating an artist making it; the scope is
stated in section 1.

Building, running and the source layout are in the repository's
[README.md](../README.md). What has been done and what may come next is in
[PROJECT_PLAN.md](../PROJECT_PLAN.md).

## How to cite

Sections are numbered once for the whole specification, not per file. Cite
a section by its number ("spec 2.8"), never by file name: a section keeps
its number if it moves to another file. Numbers are never reused or
renumbered. A new section takes the next free number under its parent and
gets a row in the table below.

Each area's file ends with an acceptance list naming the tests and the study
that check it.

## Files

| File | Covers |
| --- | --- |
| [overview.md](overview.md) | Purpose, the pipeline, essential rationale, what is not built, open questions |
| [color.md](color.md) | Spectral pigments, film optics, the color-matching mixer |
| [surfaces-and-canvas.md](surfaces-and-canvas.md) | Paper, canvas and panel; the state of the artwork, its rendering and change tracking |
| [hand.md](hand.md) | Turning an intended stroke into the motion of a tool |
| [brush.md](brush.md) | The bristle brush and paint |
| [dry-media.md](dry-media.md) | Soft pastel, pastel pencil, stump, eraser |
| [watercolor.md](watercolor.md) | Water and pigment on paper |
| [artists.md](artists.md) | The closed-loop artists for each medium |
| [application.md](application.md) | The engine entry point, command line, files, studio window, profiler |
| [verification.md](verification.md) | Tests, studies, how a change is checked; determinism, parallel execution, arithmetic |
| [scene-understanding.md](scene-understanding.md) | What the photograph shows: the scene file, the observe step that writes it, importance |

## Sections

| Section | Title | File |
| --- | --- | --- |
| 1 | Purpose | [overview.md](overview.md) |
| 2.1 | Pipeline | [overview.md](overview.md) |
| 2.2 | Color | [color.md](color.md) |
| 2.3 | Surfaces | [surfaces-and-canvas.md](surfaces-and-canvas.md) |
| 2.4 | The canvas | [surfaces-and-canvas.md](surfaces-and-canvas.md) |
| 2.5 | The hand | [hand.md](hand.md) |
| 2.6 | Bristle brush and paint | [brush.md](brush.md) |
| 2.7 | Dry media | [dry-media.md](dry-media.md) |
| 2.8 | Watercolor | [watercolor.md](watercolor.md) |
| 2.9 | The artists | [artists.md](artists.md) |
| 2.10 | Application | [application.md](application.md) |
| 2.11 | Verification | [verification.md](verification.md) |
| 2.12 | Determinism, parallel execution and arithmetic | [verification.md](verification.md) |
| 2.13 | Scene understanding | [scene-understanding.md](scene-understanding.md) |
| 3 | Essential rationale | [overview.md](overview.md) |
| 4 | Status and what is not implemented | [overview.md](overview.md) |
| 5 | Open questions | [overview.md](overview.md) |

Section 2 ("Current specification") has no text of its own; it is the parent
of sections 2.1 onward.
