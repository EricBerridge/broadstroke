# Broadstroke - Project plan

The specification (what the application must do, and why) is in
[spec/](spec/README.md). This file records where the project stands and what
may come next. It does not restate requirements; it points to the sections
that hold them.

## Where it stands

- The engine was rewritten from scratch as a CPU-only C++20 simulation
  (spec 1, 2.1, 3). The earlier GPU implementation is gone.
- Four media work end to end: oil, watercolor, colored pencil and soft
  pastel (spec 2.6 to 2.9). Graphite pencil, charcoal and wax crayon were
  removed in November 2026 (spec 4).
- There is a command-line tool, a Windows studio window, five physics
  studies and a sampling profiler (spec 2.10).
- A performance pass in November 2026 brought the test photo at 1400 px
  from 149 s to under a minute for oil, from 170 s to under a minute for
  watercolor, and from 49 s and 29 s to about 10 s and 8 s for pastel and
  colored pencil. Current timings are in the README.

## What may come next

Nothing is scheduled. Candidates, none of them decided:

- The items listed as not implemented in spec 4.
- The open questions in spec 5.
- Further speed for oil, whose brush runs on one core: simulating the
  bristle geometry eight at a time was considered and not started.
- Pastel's fingertip blending pass runs on one core and is now a visible
  share of a pastel drawing's time.
