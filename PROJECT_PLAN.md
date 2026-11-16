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
- There is a command-line tool, a Windows studio window (a program of its
  own on WinUI 3, started by `broadstroke gui`), five physics studies and
  a sampling profiler (spec 2.10).
- The build is an MSBuild solution, `broadstroke.slnx` (README). CMake was
  removed on 2026-11-15, the owner's decision, and with it the untested
  route to other compilers and platforms (spec 4).
- A performance pass in November 2026 brought the test photo at 1400 px
  from 149 s to under a minute for oil, from 170 s to under a minute for
  watercolor, and from 49 s to about 10 s for pastel. Current timings are
  in the README.

## Intended work

What the owner has said he wants, in his order. Each entry points to
where its state is recorded.

1. **The user interface** (spec 2.10). Built: in the studio window, one
   choice of what to paint (the photo using its scene, the photo alone,
   an interpretation in a manner), the scene view, the scene's items with
   their importance, treatment and edges (editable and saved to the scene
   file), the watercolor key and oil's sky blending (also on the command
   line), "Observe photo", "Imagine", the model's choice per item
   (editable) and painting of the imagined subject. The owner's
   decisions: the window starts the Python tools as programs of their
   own; scene edits go to the file, with a copy of the file as observed;
   an interpretation may be painted in every medium, to be judged later;
   the window writes nothing beside a photo and keeps its working files
   under `%LOCALAPPDATA%\Broadstroke\photos`, found by the photo's
   content (2026-11-15; the files of the eight photos that had them
   beside them were moved there by hand, and the code does not look
   beside a photo).
   Also built at his request: tooltips on the controls, and a help window
   of topics that does not block the studio window. Built on 2026-11-15
   and awaiting his look: choosing the ragged and abrupt pairs of the
   solid of triangles in the window, per item, among the materials it
   meets. The watercolor key slider keeps its range of 0.5 to 1.5
   (decided 2026-11-15): measured, the whole range changes the picture
   about evenly; the owner finds 0.5 too dark but wants it within reach.

   The window is being rebuilt on a new foundation, in steps the
   owner approves one at a time (started 2026-11-15). None of them may
   change the picture.
   1. Done: the build is an MSBuild solution (`broadstroke.slnx`) in place
      of CMake, with the same compiler settings; the renders are
      byte-identical to those before.
   2. Done (2026-11-15): the window in WinUI 3 (C++/WinRT, Windows App SDK
      2.6.5), doing everything spec 2.10 says. His decisions: a program of
      its own (`broadstroke_studio.exe`) that `broadstroke gui` starts;
      unpackaged; self-contained; it follows the app theme of Windows, and
      a settings menu for such things comes later. The engine and the tests
      did not change; of the command line only how `gui` starts the
      window. The window as it was before is removed.
   3. Done (2026-11-15): a menu bar. File
      has open, save, show the photo in its folder and exit; View has the
      four views beside the "Show" box, which stays; Help has the guide
      and About (version, copyright, the computer's processor, memory and
      graphics). Shortcuts for these and F5 for Start; the photo's name in
      the window's title. His decisions: nothing else moves to a menu for
      now; no theme setting yet; a version number, begun at 0.1.0
      (`src/app/version.hpp`), in place of a build date.
   4. Done (2026-11-15): the flow of the window, which he found
      confusing. In place of the scene panel: one
      choice of what to paint; Observe and Imagine shown as a line of
      status with one button each; the items and what a manner changes
      edited in a view of the main window that takes the place of the
      painting controls, not a dialog and not a wizard. Scene edits are
      saved at once; changes to what a manner does wait for a button that
      imagines with them, or are discarded. His decisions: a scene edit
      marks the imagined subject out of date; opening a photo chooses the
      photo, so an interpretation is chosen anew each time; an imagined
      subject is painted without its scene only from the command line.
   5. Done (2026-11-15), and the owner has looked at all five steps: a
      control that the state does not permit is disabled and says why, in
      place of a
      message after the click. Its tooltip begins with the reason; a
      disabled menu item, which shows no tooltip, has the reason in the
      place of its shortcut. The buttons that start the steps outside the
      engine are disabled while the steps are not set up, and oil's sky
      blending while the scene is not used.
2. **The solid of triangles** (spec 2.14). The outline decided where two
   materials meet (sheets of round 6) is accepted (2026-11-15). The
   defaults for which pairs of materials are ragged stay as they are; he
   wanted the means to override them, which the choice file and the
   window now have.
3. **Cubism** (spec 2.14). Kept, to be refined as part of this work. Its
   first form lacks cuts that follow the form. Round 7 (2026-11-15) has
   them, as planes joined from a mesh by the way they face. He found it
   an improvement: real cubist cuts take up lines that are already in
   the scene and exaggerate them. It is the default now (2026-11-15);
   straight cuts remain as an option of the interpret step. Round 8
   tried the number of planes and how far apart they are; at his word
   both are settings of the manner in the window, the second with three
   steps that show (in place, shifted, dislocated). Round 9, after he
   found the faceted parts washed out beside what was left as it is:
   how flat the planes are drawn is a third setting, and its default
   keeps the faceted and the unfaceted alike in color and contrast.
   Still missing: a
   plane that shows another side of the subject. Not tried: cuts placed
   along the strong lines the picture already has, and another
   arrangement by a new seed.
4. **Surrealism** (spec 2.14). Its mechanisms are kept, for use in
   targeted ways; not refined further for now. Known gaps: what is
   painted in behind a large moved thing is too smooth, and repeats of a
   small thing are too small to read.

Done by the owner from the studio window (2026-11-15): the observe step
with its check, and interpretations, on photos of his own, painted in
oil, watercolor and pastel. He found them good.

Queued, not to be started until he says: refining surrealism (item 4),
and the checks of the window that were not driven (the light theme,
monitors of different DPI, an observe step run to its end, selecting an
item by a click in the picture, the window at its smallest size).

## What was done, and what else may come

**Scene understanding** (spec 2.13) was built in November 2026 and merged
to `main` on 2026-11-14. The order:

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
   folds of a face but they still read as lines. A darker tone for skin alone is not the remedy: the skin was no paler
   than the rest of the picture. A trial with the eyes and mouth drawn in by hand
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
- Watercolor is keyed darker (0.85 instead of 1.25), so that its tones
  land near the photograph's instead of about 0.1 lighter. This changes
  watercolors made without a scene file too. Approved.

- Pastel accents are drawn only where the drawing lacks a dark. Before, a
  line with the darkest pastel pencil was drawn along every strong edge,
  which put black lines across faces.
- Pastel has a closing pass: the artist goes over the whole sheet with the
  pastel pencils and closes the small spots where the paper showed
  through. Pastel takes about 15 s instead of 10 s for it.

Added to the observe step in November 2026 and kept by the owner:
outlines for what the boxes leave out, and a second look by the list model
that checks the list of items against the outlines (spec 2.13). The
paintings show no gain from it; its value is a scene file that needs less
correcting by hand. The scene files written with it for the five trial
photographs are in `outputs/scene_trial/photos_v2/` and are the ones used
from here on; the ones the earlier sheets were made with are untouched in
`outputs/scene_trial/photos/`.

**Interpretation** (spec 2.14) is under investigation; its trial was
merged to `main` with scene understanding: painting the subject in an
artistic manner (surrealism, cubism
and so on) instead of as the photograph shows it. The faithful artwork
stays the default. Nothing of it is in the engine, and no change to the
engine has been needed: the trial in `tools/interpret/` prepares an
imagined subject that the engine paints like any photo. How to run it is
in [tools/interpret/README.md](tools/interpret/README.md). In oil only,
in this order:

1. Six moods as key and color with a phrase per item. The owner: a watch
   made to melt is a genuine artistic mechanism; changes of tone and
   color read as filters and are not pursued.
2. The mechanisms that stand out in the art movements, collected and
   reduced to building blocks (spec 2.14).
3. Surrealism (melting, stilts, levitation, giant scale, rows of
   repeats), cubist facets, long shadows in the manner of de Chirico,
   fading repeats of a moving thing (futurism). The owner's verdicts are
   under "Intended work"; his two rules for surrealism are in spec 2.14.
4. The solid of triangles, the owner's idea of 2026-11-14. A first form
   (the picture's detail on each triangle, relit as one plane, with a
   black wireframe) was not what he wanted. In the second each triangle
   is one color and there is no wireframe; the brushwork is the
   painter's. The owner: "a very cool and innovative effect". Then, at
   his request, its outline decided where two materials meet: ragged
   (finer triangles that run into the surroundings) or abrupt.

Beyond "Intended work" nothing is scheduled. Candidates, none of them
decided:

- Further uses of the scene file and of local models (spec 2.13, spec 5):
  edge quality measured along each outline, focal zones for the features
  of a face, importance per instance. None is started.
- The mechanisms of spec 2.14 that have not been tried.
- How the steps outside the engine (observe, interpret) are shipped to a
  user (spec 5). Running the models from a C++ program instead of Python
  was weighed on 2026-11-15 and put off; if it is taken up, observe comes
  first and interpret after its manners have settled.
- The other items listed as not implemented in spec 4.
- The open questions in spec 5.
- Further speed for oil, whose brush runs on one core: simulating the
  bristle geometry eight at a time was considered and not started.
- Pastel's fingertip blending pass runs on one core and is now a visible
  share of a pastel drawing's time.
