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
- `broadstroke gui [photo]` starts the studio window and waits until it is
  closed. The window is a program of its own, `broadstroke_studio.exe` in
  the folder `studio\` beside `broadstroke.exe`, and can also be started
  directly, with the photo as its only argument.
- `broadstroke study <pigments|surfaces|brush|dry|watercolor> <out>` renders
  a scripted scene that shows one physical model at a time.
- `broadstroke list` lists media, surfaces and studies.
- An unknown medium, surface or option on the command line is an error, with
  a non-zero exit code.

**Files**

- Photos are untrusted input: the header is validated and the pixel count
  capped (120 megapixels) before decoding.
- Output is PNG, JPEG or BMP, chosen by the file extension.
- In the studio window, paths may contain non-ASCII characters, also the
  path given to `broadstroke gui`.

**Studio window** (Windows, `src/studio/`)

The window gathers a photo, a scene and settings and calls the engine, as
the command line does: for the same input the artwork it saves is the file
the command line writes, byte for byte. It exposes the settings that matter
most and leaves the rest to the command line and to the files.

It is built on WinUI 3 (C++/WinRT, Windows App SDK), with these decisions:

- A program of its own, linked with the same engine library as the command
  line and built with the same compiler settings. Reason: the command line
  stays a plain console program that needs nothing of the Windows App SDK.
- Unpackaged: it runs from its folder, takes a photo on its command line
  and starts the steps outside the engine as ordinary programs. Reason:
  nothing the window does needs a package identity, and a packaged program
  is installed, signed, and may have its file access redirected.
- Self-contained: the files of the Windows App SDK are copied beside the
  program when it is built, from the version the project names. Reason:
  nothing has to be installed to run it, and an update of Windows cannot
  change the window.
- It follows the light or dark app setting of Windows. The preview keeps
  its dark ground in both.

- A menu bar, and below it two parts: a panel on the left and the
  preview. The panel is the painting controls or, in their place, the
  editor of the scene's items.
- Menus: File (Open photo..., Save artwork..., Show photo in folder,
  Exit), View (Photo, Scene, Imagined subject, Artwork) and Help (Guide,
  About Broadstroke). A command is in one place, a menu or a button. The
  view is the exception: it is in the View menu and in the "Show" box,
  which say the same. Reason: the box says what is on screen, the menu
  gives the keys.
- A photo is opened with File > Open photo or by dropping a file on the
  window. The window's title carries the open photo's name. Show photo in
  folder opens the photo's folder in Explorer with the photo selected.
  Exit closes the window as its close button does.
- About shows the version (`src/app/version.hpp`), the copyright, and
  this computer's processor, installed memory, and graphics adapters with
  their own memory.
- Shortcuts: Ctrl+O and Ctrl+S open and save, F5 starts and stops,
  Ctrl+1 to Ctrl+4 choose the view, F1 opens the help, and Alt with F, V
  or H opens a menu. A shortcut does what its item or button would do at
  that moment, nothing where that is disabled, and nothing while a dialog
  is open. They belong to the window, not to the menu items. Reason: an
  item is not part of the window until its menu has been opened.
- Painting controls: what to paint (below), medium, surface, size,
  effort, seed, start and stop. One row holds what only the chosen medium
  has: for oil a check box for blending the sky and clouds, for
  watercolor a slider for the key (0.5 to 1.5 in steps of 0.05). Pastel
  has none. In a window too small for them the controls above Start
  scroll; Start, the view and the status stay in place.
- A choice of what the preview shows: the photo, the scene (if the photo
  has one), the imagined subject (if a manner is chosen and its subject is
  made), the artwork (once one is started). The "Show" box lists the views
  there is something to show for; the View menu lists all four and
  disables the others. The preview shows the canvas as it is being
  worked, lit as the finished piece will be.
- The preview's picture is fitted to the preview, keeping its proportions,
  and fitted again when the window is resized or moved to a monitor of
  another scale. A picture larger than its place is reduced to the pixels
  it is shown on by averaging. Reason: left to the framework, a large
  picture is shown through a coarse filter.
- The scene view is drawn for the screen only: each item tinted in a color
  of its own over the photo, with outlines; with an item selected, it and
  its parts keep the photo's light and the rest is dimmed. A click on an
  item in the scene view selects it.

What to paint (spec 2.13, 2.14):

- One choice of three: the photo using its scene; the photo alone, as
  with `--no-scene`; an interpretation in a manner (surrealism, cubism,
  futurism, the metaphysical manner, the solid of triangles). Reason:
  whether the scene is used and whether a manner is applied were two
  controls for one question, and what one needed of the other was not to
  be seen.
- Opening a photo loads its scene file if there is one beside it, and
  chooses the photo: using its scene if it has one, alone if not. An
  interpretation is chosen anew for each photo. Reason: painting the
  photo as it is stays the default. A scene file that cannot be used is
  reported, and the photo is opened without it. With the photo chosen,
  nothing of an interpretation is read or used.
- What an answer needs first is said in a line below it, beside the one
  button that does it:
  - the scene: that the photo is not observed yet, with "Observe photo";
    or the number of items and the main subject, with "Edit scene...";
  - the imagined subject, under the manner: not imagined yet, with
    "Imagine"; ready; out of date, and whether the changes or the scene
    were edited, with "Imagine again"; or that a manner needs the photo's
    scene, with "Observe photo". Beside it, "Edit changes...".
  While a step runs, its line shows the time and its button reads "Stop".
- "The photo, using its scene" cannot be chosen while the photo has no
  scene. A photo observed for the first time is painted with its scene.
- With an interpretation chosen, Start paints the imagined subject with
  its own scene file, in any medium, exactly as the command line paints
  that file. Start is disabled until the subject is made and up to date.
- The subject is out of date if the manner's choice file or the photo's
  scene file was written after it. Reason: the subject carries a scene of
  its own, made from the photo's scene as it was.

The editor of the items:

- "Edit scene..." and "Edit changes..." show the editor in the place of
  the painting controls, wider than they are, and the scene view in the
  preview. "Back to painting" returns. It is a view of the main window,
  not a dialog. Reason: an item is selected by a click in the picture,
  and a correction of the scene has nothing to confirm.
- The items are listed in the file's order, a part set in below its
  whole, each with the color it has in the scene view. Selecting an item
  shows it in the scene view.
- The scene's form lists each item's importance, treatment and edges,
  and the selected item's can be changed. The treatments offered are the
  observe step's list, and whatever else the file says. A change is
  saved to the scene file at once, so the file always says what will be
  painted. Nothing else in the file changes, and its layout stays the
  observe step's. The first time a file is changed, the file as it was
  is kept beside it as `<stem>.scene.observed.json` and the file is
  marked `"edited": true`. If the file cannot be written the change is
  not made.
- The form for what the manner changes lists each item's mechanism and
  strength, and the selected item's can be changed: the mechanism to one
  of the manner's or "as it is", the strength from 1 to 3 (for the solid
  of triangles coarse, medium, fine). A change waits in the window.
  "Imagine with these changes" writes the choice file, marked
  `"edited_by_hand": true`, and starts the interpret step; "Discard"
  drops the changes that wait. Leaving the editor, opening another photo
  or closing the window while changes wait asks whether to imagine with
  them, discard them, or stay. Reason: the choice file and the subject
  stay in step, and several changes cost one run of the step. The window
  does not check that the mechanism can be carried out on the item; the
  interpret step reports what it did.
- "Observe photo", and "Observe again" in the scene's form, start the
  observe step for the open photo and then load the scene file it wrote.
  If the photo has a scene file, the user is asked first, because
  corrections to it are lost.
- "Imagine" starts the interpret step for the open photo and the chosen
  manner: the model's choice of a mechanism per item, unless there is a
  choice file for that manner already, and then the imagined subject made
  from the choice. "Ask the model again", in the form for the changes,
  has the model choose again; the user is asked first, because the choice
  shown is replaced, with the changes that wait.
- The files of an interpretation are kept beside the photo in
  `<stem>.interpret\`: `choices\` holds one choice file per manner and
  `subjects\` the imagined subjects with their scene files.
- What the window has to say or ask is shown in a dialog inside the
  window. A message waits for the one before it; a question that replaces
  something offers the replacement and "Cancel", with "Cancel" the default.
- Left to the files and the command line: an item's name and parent, the
  main subject, another scene file than the one beside the photo, the
  observe step's partial modes, painting an imagined subject without its
  scene, and of the interpret step the number of triangles, which pairs
  of materials meet raggedly or abruptly, the fill model, and its seed.

Tooltips and help (`src/app/help_text.cpp`, `src/studio/HelpWindow.*`):

- Every control that needs explaining has a tooltip of a sentence or two,
  shown also on its label and while the control is disabled. Reason: a
  disabled control is where a user most wants to know what it is for. The
  tooltip belongs to a host around the control and its label, because a
  disabled control passes the pointer through to what is behind it.
- A menu item that needs explaining has a tooltip too, shown beside the
  menu. It does not show while the item is disabled.
- A control that the state does not permit is disabled, and says why:
  its tooltip begins with the reason, before what the control is for. A
  disabled menu item shows the reason, in two or three words, in the
  place of its shortcut. Reason: nothing is refused only after it was
  pressed, and a user sees what to do first. If several reasons apply,
  one is given: what is running, then that no photo is open, then what
  the control itself lacks. A control is enabled exactly when there is no
  reason against it; the two are decided in one place
  (`MainWindow::update_controls`).
- The reasons: the artwork is being made, or a step is running (with
  what stops it); no photo is open; the photo has no scene; nothing is
  selected in the list; an interpretation's subject is not imagined yet,
  or is out of date; no artwork has been made; no changes wait; a step
  or the artwork is being stopped; the steps outside the engine are not
  set up; for oil's blending of the sky, the scene is not used (the
  setting does nothing then, spec 2.13, and keeps its value).
- The help is a set of pages written for a user of the window: getting
  started, painting, the scene, observing a photo, manners, files, setting
  up the steps outside the engine, the command line. The pages are part of
  the program; no file has to be beside it.
- Help > Guide opens the first page. F1 opens the page of the part of
  the window that has the keyboard focus, and the first page while no
  photo is open.
- The help window lists the topics beside the open page. It belongs to the
  studio window and stays above it, but it is not modal: both windows take
  input, so a page can stay open beside the work it explains. Esc or its
  close button hides it; it comes back at the same place.
- A change to what a control does changes its tooltip and its page in the
  same change.

The steps outside the engine (`src/app/process_win32.cpp`):

- Observe and interpret are the Python programs of `tools/`. The window
  starts them as programs of their own, without a console window, and
  reads the files they write. No model runs in the window or in the
  engine.
- Where they are comes from a local file,
  `%LOCALAPPDATA%\Broadstroke\studio.json` (or the file named by the
  environment variable `BROADSTROKE_STUDIO_CONFIG`), with two members:
  `python`, the interpreter that has the tools' packages, and `tools`, the
  folder that holds `observe/` and `interpret/`
  (`tools/studio.example.json`). Without it the buttons that start a
  step are disabled, with what is missing as the reason, and the rest of
  the window works. The window looks for it when it starts, when a photo
  is opened and each time it comes to the front, so that setting the
  steps up takes effect without a restart. If the file has gone missing
  since, pressing a button says what is missing.
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
  call queued on its dispatcher that carries no data; wake-ups are
  coalesced so the queue cannot flood. A step outside the engine runs on a thread of its own and
  reports the same way.
- Closing the window while work is in progress stops the work, or the
  step, and waits for it.

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
- The `Profile` configuration builds the application with release
  optimization and symbols for it.
- Sampling slows the program, so the report is for shares of time; absolute
  times come from an unprofiled run.
