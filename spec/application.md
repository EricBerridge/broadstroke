This file covers: the command line, the studio window, the studies and the profiler. Index: [README.md](README.md).

### 2.10 Application (`src/app/`, `tools/`)

**Engine entry point** (`create_artwork`)

- Input: a photo, settings (medium, surface, long side in pixels, long
  side in millimeters, seed, effort, the watercolor key of spec 2.9, and
  whether oil blends the sky, spec 2.13) and optionally a scene (spec 2.13).
  Output: the lit image, the number of
  strokes, the time taken, the medium and surface used, the size, the
  artist's notes, and whether the work was stopped.
- The artwork has the photo's aspect ratio at the requested resolution,
  which is also the simulation resolution.
- Each medium has a customary surface and physical size, used when none is
  given: oil on `canvas` at 400 mm; watercolor on `cold_press` at 300 mm;
  pastel on `pastel_paper` at 400 mm.
- An unknown medium falls back to oil, and an unknown surface to the
  medium's customary one. The long side is limited to 160 to 6000 pixels
  and the effort to 0.3 to 2. An empty photo gives an empty artwork. A
  scene whose item layer does not have the photo's proportions is not used,
  and the notes say so.
- A progress callback receives the stage, the overall fraction, the stroke
  count and the canvas (which it may render). Returning false stops the
  work.

**Command line**

- `broadstroke paint <photo> [-m medium] [-s surface] [-o out] [--size px]
  [--mm long-side-mm] [--effort x] [--seed n] [--scene file] [--no-scene]
  [--key x] [--no-sky-blend] [--frames dir] [--frame-every n]`
  makes an artwork. The output defaults to `<photo>_<medium>.png` next to
  the photo. With `--frames`, progress frames are also saved every so many
  strokes.
- `--key` sets the watercolor key (spec 2.9) and `--no-sky-blend` switches
  off oil's blending of sky and cloud (spec 2.13). Each is ignored by the
  media it does not concern.
- The photo's scene file (spec 2.13) is used if it exists beside the photo.
  `--scene` names another, and `--no-scene` uses none. A scene file that is
  named or found but cannot be used (unreadable, refused, or not of the
  photo's proportions) is an error and nothing is painted.
- `broadstroke gui [photo]` opens the studio window.
- `broadstroke study <pigments|surfaces|brush|dry|watercolor> <out>` renders
  a scripted scene that shows one physical model at a time.
- `broadstroke list` lists media, surfaces and studies.
- An unknown medium, surface or option on the command line is an error, with
  a non-zero exit code.

**Files**

- Photos are untrusted input: the header is validated and the pixel count
  capped (120 megapixels) before decoding.
- Output is PNG, JPEG or BMP, chosen by the file extension.
- In the studio window, paths may contain non-ASCII characters.

**Studio window** (Windows, `gui_win32.cpp`)

The window gathers a photo, a scene and settings and calls the engine, as
the command line does: for the same input the artwork it saves is the file
the command line writes, byte for byte. It exposes the settings that matter
most and leaves the rest to the command line and to the files.

- Three parts: painting controls on the left, the preview in the middle,
  the scene panel on the right.
- Painting controls: open a photo (a dialog, or a file dropped on the
  window), help, medium, surface, size, effort, seed, start and stop, save
  the artwork. One row holds what only the chosen medium has: for oil a check
  box for blending the sky and clouds, for watercolor a slider for the key
  (0.5 to 1.5 in steps of 0.05). Pastel has none.
- A choice of what the preview shows: the photo, the scene (if the photo
  has one), the imagined subject (if a manner is chosen and its subject is
  made), the artwork (once one is started). The preview shows the canvas
  as it is being worked, lit as the finished piece will be.
- The preview's picture is fitted to the preview, keeping its proportions.
  When the window is resized the picture is fitted again, and every
  control that moved or changed size is drawn again as a whole. Reason:
  Windows keeps the pixels of a child that is moved; the preview then
  showed its old picture with strips of the new one, and controls that
  had passed over each other were left half drawn.
- The scene view is drawn for the screen only: each item tinted in a color
  of its own over the photo, with outlines; with an item selected, it and
  its parts keep the photo's light and the rest is dimmed. A click on an
  item in the scene view selects it.

Scene panel (spec 2.13):

- Opening a photo loads its scene file if there is one beside it; the
  panel shows the file's name, the number of items and the main subject. A
  scene file that cannot be used is reported, and the photo is opened
  without it.
- "Use the scene when painting" is on by default; off, the photo is
  painted without its scene, as with `--no-scene`.
- The items are listed in the file's order, a part set in below its whole,
  each with the color it has in the scene view, its importance, treatment
  and edges. Selecting an item shows it in the scene view.
- The selected item's importance, treatment and edges can be changed. The
  treatments offered are the observe step's list, and whatever else the
  file says. A change is saved to the scene file at once, so the file
  always says what will be painted. Nothing else in the file changes, and
  its layout stays the observe step's. The first time a file is changed,
  the file as it was is kept beside it as `<stem>.scene.observed.json` and
  the file is marked `"edited": true`. If the file cannot be written the
  change is not made.
- "Observe photo" starts the observe step for the open photo and then
  loads the scene file it wrote. If the photo has a scene file, the user is
  asked first, because corrections to it are lost.
- Left to the file and the command line: an item's name and parent, the
  main subject, another scene file than the one beside the photo, and the
  observe step's partial modes.

Interpretation in the scene panel (spec 2.14):

- A list of manners, with "As the photo is" first and chosen by default:
  surrealism, cubism, futurism, the metaphysical manner, the solid of
  triangles. With "As the photo is" the window paints the photo, and
  nothing of an interpretation is read or used.
- "Imagine" starts the interpret step for the open photo and the chosen
  manner: the model's choice of a mechanism per item, unless there is a
  choice file for that manner already, and then the imagined subject made
  from the choice. "Ask the model again" has the model choose again; the
  user is asked first, because the choice shown is replaced. A manner
  needs the photo's scene.
- The files are kept beside the photo in `<stem>.interpret\`: `choices\`
  holds one choice file per manner and `subjects\` the imagined subjects
  with their scene files.
- With a manner chosen, the list of items has a column for what the manner
  does to each, and the selected item's mechanism (one of the manner's, or
  "as it is") and strength (1 to 3; for the solid of triangles coarse,
  medium, fine) can be changed. A change is saved to the choice file at
  once and marks it `"edited_by_hand": true`. The window does not check
  that the mechanism can be carried out on the item; the interpret step
  reports what it did.
- With a manner chosen, Start paints the imagined subject with its own
  scene file, in any medium, exactly as the command line paints that file.
  If the subject has not been made, or its choice was changed after it was
  made, Start says so and paints nothing.
- Left to the command line of the interpret step: the number of triangles,
  which pairs of materials meet raggedly or abruptly, the fill model, and
  its seed.

Tooltips and help (`help_text.cpp`, `help_win32.cpp`):

- Every control that needs explaining has a tooltip of a sentence or two,
  shown also on its label and while the control is disabled. Reason: a
  disabled control is where a user most wants to know what it is for.
- The help is a set of pages written for a user of the window: getting
  started, painting, the scene, observing a photo, manners, files, setting
  up the steps outside the engine, the command line. The pages are part of
  the program; no file has to be beside it.
- The Help button opens the first page. F1 opens the page of the part of
  the window that has the keyboard focus.
- The help window lists the topics beside the open page. It belongs to the
  studio window and stays above it, but it is not modal: both windows take
  input, so a page can stay open beside the work it explains. Esc or its
  close button hides it; it comes back at the same place.
- A change to what a control does changes its tooltip and its page in the
  same change.

The steps outside the engine (`process_win32.cpp`):

- Observe and interpret are the Python programs of `tools/`. The window
  starts them as programs of their own, without a console window, and
  reads the files they write. No model runs in the window or in the
  engine.
- Where they are comes from a local file,
  `%LOCALAPPDATA%\Broadstroke\studio.json` (or the file named by the
  environment variable `BROADSTROKE_STUDIO_CONFIG`), with two members:
  `python`, the interpreter that has the tools' packages, and `tools`, the
  folder that holds `observe/` and `interpret/`
  (`tools/studio.example.json`). Without it, pressing either button says
  what is missing, and the rest of the window works.
- While a step runs, the status shows the time and the newest line the
  step printed, the bar only shows that it runs, and the button that
  started it reads "Stop". Stopping ends the step with every program it
  started (the model server). Painting and editing wait until it ends.
- When a step ends, its files are read again whether it succeeded or not.
  A step that fails is reported with its last lines.

Threads:

- The artwork is made on a worker thread; the window stays responsive.
  The worker never touches a window. It leaves its latest progress, frame
  and finally the artwork in a shared mailbox and wakes the window with a
  message that carries no data; wake-ups are coalesced so the message queue
  cannot flood. A step outside the engine runs on a thread of its own and
  reports the same way.
- Closing the window while work is in progress stops the work, or the
  step, and waits for it.
- On other platforms `gui` reports that the window is not available.

**Acceptance** (`tests/test_app.cpp`, and by driving the window): a JSON
document in the observe step's layout is written back byte for byte, and
what is written reads back as the same values; a scene file saved
unchanged differs only by its mark, with the file as it was kept beside
it once; a correction is read back by the engine's reader and what the
engine does not read is still in the file; a file that cannot be read is
reported and nothing is written; the help's markup is read as headings,
paragraphs and list items, and every topic has a page of plain text that
can be shown. The window itself has no automated test; spec 4 says what
has been driven.

**Profiler** (Windows, `tools/profiler/`)

- `broadstroke_profiler <report> <interval-ms> <exe> [args]` runs a program,
  samples its threads and writes a text report of where the main thread's
  wall time went: a call tree, self and inclusive time per function with
  inlined functions listed separately, and the hottest source lines. It
  needs no elevation.
- The `msvc-profile` preset builds the application with release
  optimization and symbols for it.
- Sampling slows the program, so the report is for shares of time; absolute
  times come from an unprofiled run.
