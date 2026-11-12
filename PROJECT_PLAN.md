# Broadstroke - Project plan

The specification (what the application must do, and why) is in
[spec/](spec/README.md). This file records where the project stands and what
may come next. It does not restate requirements; it points to the sections
that hold them.

## Where it stands

- The engine was rewritten from scratch as a CPU-only C++20 simulation
  (spec 1, 2.1, 3). The earlier GPU implementation is gone.
- Three media work end to end: oil, watercolor and soft pastel (spec 2.6
  to 2.9). Graphite pencil, charcoal, wax crayon and, last, colored pencil
  were removed in November 2026 (spec 4): the owner judged their results
  not worth keeping.
- There is a command-line tool, a Windows studio window, five physics
  studies and a sampling profiler (spec 2.10).
- A performance pass in November 2026 brought the test photo at 1400 px
  from 149 s to under a minute for oil, from 170 s to under a minute for
  watercolor, and from 49 s to about 10 s for pastel. Current timings are
  in the README.

## What may come next

**Scene understanding** (spec 2.13) is the current area of work, on the
branch `feature/scene-understanding`. The order:

1. Done: a trial of the candidate models on five public-domain
   photographs. It chose Gemma 4 12B for the list of items, Qwen3.5-4B for
   the boxes and SAM 2.1 for the outlines; spec 2.13 records what was set
   aside and why.
2. Done: the scene file, the observe step that writes it
   (`tools/observe/`), and its first use, importance, so that the main
   subject gets finer work than the background. The owner kept it as a
   start; on its own its effect is small.
3. Strokes that know their region. Done for watercolor and approved: the
   sheet is worked item by item, and strokes follow and end at the items'
   outlines. Done for pastel and approved: each item in a patch is judged
   and worked on its own and its marks end at its outline. Oil was looked
   at and left alone: it already holds its outlines. Done and approved for
   oil: stroke direction from the form, from an orientation layer in the
   scene file (MoGe-2). It was tried in watercolor and pastel and not kept
   there: no gain. Done and approved for watercolor and pastel: edge
   quality, so that outlines between soft-edged items (cloud, foam, hair,
   background) are worked softly. Working from far to near was built with a depth layer and
   dropped: it made no visible difference in watercolor, pastel or oil
   (spec 2.13). This step is finished.
4. Techniques per treatment, one medium and one treatment at a time. Done
   and approved: skin in watercolor is worked wet in wet. It softens the
   folds of a face but they still read as lines. A tone closer to the
   photograph's is not the remedy: the skin is no paler than the rest of
   the picture is keyed. A trial with the eyes and mouth drawn in by hand
   as items of their own helped pastel, did nothing for oil and did not
   work in watercolor; faces are left as they are (spec 2.13). Done and
   approved: hair in pastel is hatched along its strands. Done and approved: sky and cloud are laid wet in wet in
   watercolor. Done and approved: in oil, sky and cloud are blended with a
   clean soft brush after each broad layer; it can be switched off in the
   settings, for a later choice in the window or on the command line.
   Done and approved: masses of foliage are worked in clusters in
   watercolor. A pastel counterpart of the sky treatment (the sky rubbed
   smooth and left out of the layers of small marks) and a fabric
   treatment in watercolor (broad washes of cloth laid wet in wet) were
   built and not kept. The owner then closed this step: the treatments
   have given what they can. Two changes that are not
   treatments were tried before it and not kept (a lighter touch where less
   than the mark's width is wanted; each pass seeing the subject softened
   to the width of its mark).

Alongside: outlines for the large undefined areas of a picture (sky,
background), and how the observe step is shipped (spec 5).

Each step from 2 on changes the picture when a scene file is present, so
each is approved from before and after images (spec 2.11). Without a scene
file these steps leave the output unchanged; this is checked each time by
rendering the media at two sizes and the five studies before and
after and comparing the files.

Not part of scene understanding, done in the same period (spec 2.9):

- Colored pencil was removed completely, on the owner's decision.

- Pastel accents are drawn only where the drawing lacks a dark. Before, a
  line with the darkest pastel pencil was drawn along every strong edge,
  which put black lines across faces.
- Pastel has a closing pass: the artist goes over the whole sheet with the
  pastel pencils and closes the small spots where the paper showed
  through. Pastel takes about 15 s instead of 10 s for it.

Nothing else is scheduled. Candidates, none of them decided:

- Further uses of the scene file and of local models (spec 2.13, spec 5).

- The other items listed as not implemented in spec 4.
- The open questions in spec 5.
- Further speed for oil, whose brush runs on one core: simulating the
  bristle geometry eight at a time was considered and not started.
- Pastel's fingertip blending pass runs on one core and is now a visible
  share of a pastel drawing's time.
