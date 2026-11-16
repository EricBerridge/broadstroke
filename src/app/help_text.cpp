#include "app/help_text.hpp"

#include <cstddef>

namespace bs {
namespace {

const HelpPage kPages[] = {
    {"Getting started", R"help(
Broadstroke turns a photograph into an artwork by simulating an artist
making it: a hand moves a brush or a pastel stick over a simulated sheet,
stroke by stroke. The photograph is only looked at. Nothing of it is copied
onto the canvas.

## The window

- Top: the menus. File opens a photo and saves the artwork, View chooses
  what the preview shows, Help has this guide.
- Left: what to paint (the photo using its scene, the photo alone, or an
  interpretation), what to paint with and how (medium, surface, size,
  effort, seed), Start, and what the preview shows.
- Right: the preview. It shows the photo, its scene, an imagined subject,
  or the artwork while it is being made.
- "Edit scene..." and "Edit changes..." put a list of the scene's items in
  the place of the painting controls. "Back to painting" returns.

## The shortest way to an artwork

- Choose File > Open photo, or drop a photo on the window.
- Leave "What to paint" as it is: the photo, using its scene if it has one.
- Choose a medium.
- Press Start and watch. Start reads Stop while the work is in progress.
- Choose File > Save artwork.

## Keys

- Ctrl+O opens a photo, Ctrl+S saves the artwork.
- F5 starts the work, and stops it while it is in progress.
- Ctrl+1 to Ctrl+4 show the photo, the scene, the imagined subject and the
  artwork in the preview.
- Alt with F, V or H opens a menu. F1 opens the help.

## Finding out more

- Rest the pointer on a control for a short explanation.
- A control that cannot be used now is gray, and its explanation begins
  with the reason. A menu item says the reason where its shortcut would
  be.
- Press F1 for the help page of the part of the window you are in, or
  choose Help > Guide.
- Help > About says which version this is, and what processor, memory and
  graphics this computer has.
- This help window does not block the studio window. Keep it open beside
  your work, and close it with Esc.
)help"},

    {"Painting", R"help(
## Medium

- Oil: bristle brushes and paint, layered from a block-in to accents.
- Watercolor: transparent washes that flow and dry on paper, worked from
  light to dark.
- Pastel: soft pastel sticks on toned, toothy paper.

## Surface

What the artwork is made on. "(default for medium)" is canvas for oil,
cold-pressed paper for watercolor and pastel paper for pastel.

## Size

The long side of the artwork in pixels. It is also the resolution of the
simulation, so a larger size takes longer. The sheet keeps its physical
size (400 mm for oil and pastel, 300 mm for watercolor): more pixels give a
finer picture of the same sheet, not a larger sheet.

## Effort

How far the artist takes the piece: a quick study, a finished piece, or
meticulous work. More effort is more and finer work, and more time.

## Seed

A whole number that fixes every chance decision the artist makes. The same
photo, scene file, settings and seed give exactly the same artwork. Another
seed gives another realization of the same picture.

## The medium's own setting

- Oil, "Blend the sky and clouds": with a scene, the sky and the clouds are
  gone over with a clean, soft brush after each broad layer, so that they
  melt into a gradation. Switched off, the sky is painted like everything
  else. Without a scene it does nothing, so the box is gray while the
  scene is not used.
- Watercolor, Key: how light the picture is keyed, from 0.50 (dark) to 1.50
  (light). The default is 0.85. Below 1 the mid-tones are aimed darker than
  the photo's, above 1 lighter, toward the paper. The darks stay anchored.
- Pastel has no setting of its own.

## Start, Stop and Save

- Start makes the artwork. The preview shows the canvas as it is worked,
  and the status says what the artist is doing.
- Stop ends the work. What has been made so far stays and can be saved.
- F5 does what the button does.
- File > Save artwork (Ctrl+S) writes PNG, JPEG or BMP, by the ending of
  the file name. The name offered is the photo's with the medium added.

## Show

What the preview shows: Photo; Scene, if the photo has one; Imagined
subject, if a manner is chosen and its subject has been made; Artwork, once
one has been started. The View menu has the same four, with Ctrl+1 to
Ctrl+4; one that has nothing to show yet is disabled there.
)help"},

    {"The scene", R"help(
A scene file tells the artist what the photo shows: a list of items (a
face, a tree, the sky) and which item each part of the picture belongs to.
The window keeps it with the photo's other working files (see "Files"), not
beside the photo. It is loaded when the photo is opened. Without one the
photo is painted the same way whatever it shows.

## What the scene changes

- Importance: the main subject and what matters get finer work, the
  background broader work.
- In watercolor and pastel each item is worked on its own, and strokes end
  at its outline.
- In oil, strokes follow the form of a surface where the photo's colors
  give no direction.
- Treatments the painters act on: skin in watercolor (worked wet in wet),
  hair in pastel (hatched along its strands), sky and cloud in watercolor
  (laid wet in wet) and in oil (blended), and masses of foliage in
  watercolor (worked in clusters).
- Edges: in watercolor and pastel the outline between two soft-edged items
  is worked softly.

## Using the scene, or not

Under "What to paint":

- "The photo, using its scene" is chosen when a photo that has a scene is
  opened. The line below it says how many items the scene has and which is
  the main subject. If the photo has no scene, the line says "Not observed
  yet" and offers "Observe photo".
- "The photo alone" paints the photo as if it had no scene file. The file
  itself is not changed.

## Edit scene

"Edit scene..." shows the scene's items in the place of the painting
controls, and the scene view in the preview. "Back to painting" returns.

- The list has one row for each item, in the order of the file. A part is
  set in below the whole it belongs to. The colored square is the item's
  color in the scene view.
- With nothing selected, every item is tinted in its color. With an item
  selected, it and its parts keep the photo's light and the rest is dimmed.
- Select an item in the list, or click it in the picture.
- "Observe again" writes the scene anew (see "Observing a photo").

The scene view can also be chosen under Show, or with Ctrl+2.

## Correcting an item

Select the item, then change:

- Importance: Background, Supporting, Matters, or Main subject.
- Treatment: what the item is, from a fixed list of words.
- Edges: Hard (a crisp outline), Soft (a gradual one), or Lost (the item
  merges into its surroundings; lost items that meet are worked as one).

A change is saved to the scene file at once, so the file always says what
will be painted. The first time, the file as it was is kept beside it with
`.scene.observed.json` at the end. To undo all corrections, put that file
in the place of the scene file.

A subject imagined in a manner before the correction does not have it: its
line then says "Out of date: the scene was edited", and "Imagine again"
makes it anew.

An item's name, the whole it is a part of, and the main subject cannot be
changed here. The scene file is plain text (JSON) and can be edited.
)help"},

    {"Observing a photo", R"help(
"Observe photo" writes the scene file of the open photo. It looks at the
photo with models that run on this machine. Nothing is sent anywhere. The
button is under "What to paint", in the line of the scene, while the photo
has no scene.

- It takes a minute or two. The line shows the time, the status what the
  step last reported, and the button reads Stop while it runs.
- When it ends, the new scene is loaded and shown in the scene view, and
  "The photo, using its scene" is chosen. Look the scene over with "Edit
  scene...": a wrong importance, treatment or edge can be corrected there.
- A photo that has a scene is observed again with "Observe again" in the
  scene's editor. You are asked first: observing again replaces the file,
  and corrections made to it are lost.
- Observing the same photo again can give a somewhat different scene: the
  models' answers are not exactly repeatable. Painting from a given scene
  file is.

## What it writes

Beside the photo: the scene file, the item layer (`.scene.items.png`), the
orientation layer (`.scene.normals.png`) and a preview
(`.scene.preview.jpg`).

## Before the first use

The step has to be set up once. See "Setting up Observe and Imagine".
)help"},

    {"Manners", R"help(
By default the photo is painted as it is. A manner interprets it: something
is done to a thing in the picture because of what it is. A watch melts; a
tree stands on stilts. This is an investigation in progress. It was tried
in oil only; the window lets you paint an interpretation in any medium.

## The manners

- Surrealism: a few things behave as in a dream. Its mechanisms are
  melting, on stilts, levitating, giant and repeated.
- Cubism: the main subject is broken into flat, angular planes (faceted).
  The cuts follow the form: a plane is a piece of surface that faces one
  way.
- Futurism: a thing that moves trails fading repeats of itself (echoing).
- Metaphysical: figures throw long shadows across the ground, as under a
  low sun (long shadow).
- Solid of triangles: one thing is rebuilt as a solid of flat triangles,
  each painted in one color (triangulated).

## Imagine

Under "What to paint", choose "An interpretation" and a manner, and press
Imagine. A model on this machine chooses, for each item, one of the
manner's mechanisms and a strength. Then the changes are carried out on the
photo. The result is the imagined subject, which the preview shows. A
manner needs the photo's scene; without one the button reads "Observe
photo".

The line above the button says where the subject stands:

- "Not imagined yet": press Imagine.
- "Ready": Start paints it.
- "Out of date": what the manner changes, or the scene, was edited after
  the subject was made. Press "Imagine again".

If there is a choice for this manner already, Imagine carries it out
without asking the model again.

## Changing what the manner does

"Edit changes..." shows the scene's items with what the manner does to
each, in the place of the painting controls. Select an item, in the list
or by a click in the picture, and set below the list:

- the mechanism: one of the manner's, or "As it is" to leave the item
  alone;
- the strength: 1 slight, 2 clear, 3 extreme. For the solid of triangles it
  is how fine the triangles are: 1 coarse, 2 medium, 3 fine. Cubism has
  none for an item; see below.

Cubism cuts all the items you leave faceted into planes together, so its
three settings are for the manner as a whole, under "The planes":

- Planes: how many. Few give large slabs, many give fine facets.
- Apart: how far each plane is turned and moved. In place, each plane
  shows the subject where it is. Shifted, the planes slip a little along
  the cuts. Dislocated, what they show slides apart and a face is broken.
- Flatness: how far each plane is drawn toward one flat tone. Slight
  keeps the planes about as rich in contrast as what is left as it is.
  Strong makes them flat, and pale beside what is left as it is.

For an item that the solid of triangles rebuilds, "Where the solid ends"
lists the materials it meets, one row for each, with the items of that
material in brackets. The solid ends raggedly where finer triangles run
into the surroundings (leaves against the sky), or abruptly, cut at the
outline (a watch on a wrist). "By default" follows what the two materials
are; choose Ragged or Abrupt to say otherwise. What you choose holds
wherever the two materials meet in this picture.

A change waits. "Imagine with these changes" saves the choice and carries
it out; "Discard" drops the changes that wait. If you leave while changes
wait, you are asked which of the two you want.

- Not every mechanism can be carried out on every item: a thing may be too
  small, or cut off by the edge of the picture. The status says what was
  done.
- "Ask the model again" has the model choose anew. The choice shown is
  replaced, with anything you changed in it.

## Painting it

With "An interpretation" chosen, Start paints the imagined subject in place
of the photo, with the subject's own scene. Start is disabled until the
subject is ready. Choose one of the two answers above it to paint the photo
again. Opening another photo chooses the photo, too.

## The mechanisms

- Melting: the thing sags and runs down over the edge of what it sits on.
- On stilts: what holds it up is stretched very long.
- Levitating: it hangs in the air above where it stood, its shadow below.
- Giant: it is far larger than it should be.
- Repeated: it appears many times over the open space of the picture.
- Echoing: it is followed by fading repeats of itself.
- Faceted: it is broken into flat planes that are shifted against each
  other.
- Long shadow: a low sun throws its long shadow across the ground.
- Triangulated: it is rebuilt as a solid of flat triangles.
)help"},

    {"Files", R"help(
The window writes nothing beside your photo. Its working files for a photo
are ordinary files in a folder of that photo's own, under
`%LOCALAPPDATA%\Broadstroke\photos`. File > Show working files opens it.
The folder is found by what the photo holds, not by its name or place, so
a photo keeps its scene when you rename or move it. A photo that has been
edited counts as a new one. For a photo named `harbor.jpg` the folder holds:

- `harbor.scene.json`: the scene file. Read when the photo is opened;
  written by "Observe photo" and by a correction in the scene's editor.
- `harbor.scene.items.png`, `harbor.scene.normals.png` and
  `harbor.scene.preview.jpg`: the layers of the scene and a preview,
  written by "Observe photo".
- `harbor.scene.observed.json`: the scene file as it was before its first
  correction.
- `interpret`, a folder: what Imagine makes. In it, `choices` holds one
  choice file for each manner and `subjects` the imagined subjects with
  scene files of their own.

An artwork is saved where you say. The name offered is `harbor_oil.png`
for an oil painting, with a manner `harbor_surrealism_oil.png`, and the
folder offered is the photo's. Nothing else is written, and the photo
itself is never changed. File > Show photo in folder opens the photo's
folder with the photo selected.

## The same input gives the same artwork

The photo, the scene file, the settings and the seed decide the artwork
completely. Painting again with the same four gives the same file, here and
with `broadstroke paint` on the command line.
)help"},

    {"Setting up Observe and Imagine", R"help(
"Observe photo" and Imagine are programs of their own, written in Python,
in the `tools` folder of Broadstroke. They run models on this machine. The
window starts them and reads the files they write. Painting does not need
them.

## What the window needs

A file named `studio.json` in the folder `%LOCALAPPDATA%\Broadstroke`, with
two entries:

- `python`: the Python interpreter that has the packages of the tools.
- `tools`: the `tools` folder of Broadstroke.

`tools/studio.example.json` is a file to copy and edit. If the file is
missing or wrong, the buttons that start the steps are gray and their
explanation says what is missing; the rest of the window works. The window
looks again when a photo is opened and each time it comes to the front, so
it need not be started again after the file is set right.

## What the steps need

The models and the packages are listed in `tools/observe/README.md` and
`tools/interpret/README.md`. Where the models are is written in
`tools/observe/observe.local.json`.

## If a step fails

The window shows the last lines the step printed. Check the paths in
`studio.json` and in `tools/observe/observe.local.json` first.
)help"},

    {"The command line", R"help(
Whatever the window paints can also be made from a terminal, and a few
things only there.

- `broadstroke paint photo.jpg -m oil` makes an artwork. `-s`, `--size`,
  `--effort` and `--seed` are the window's surface, size, effort and seed,
  and `-o` names the file to write.
- `--key X` is the watercolor key, and `--no-sky-blend` switches off the
  blending of the sky in oil.
- The command line looks for a scene file beside the photo, where the
  window does not keep one: give it the window's with `--scene FILE` (the
  file is among the working files). `--no-scene` paints without one.
- Only on the command line: `--mm`, the physical size of the sheet, and
  `--frames DIR`, which saves pictures of the work in progress.
- `broadstroke list` lists the media, the surfaces and the studies.
- `broadstroke gui photo.jpg` opens this window on a photo.
)help"},
};

static_assert(sizeof(kPages) / sizeof(kPages[0]) == static_cast<size_t>(HelpTopic::Count),
              "one page for each help topic");

}  // namespace

const HelpPage& help_page(HelpTopic topic) {
  int i = static_cast<int>(topic);
  if (i < 0 || i >= static_cast<int>(HelpTopic::Count)) i = 0;
  return kPages[i];
}

std::vector<HelpBlock> parse_help(const std::string& body) {
  std::vector<HelpBlock> blocks;
  bool open = false;  // the last block still takes lines
  size_t at = 0;
  while (at <= body.size()) {
    size_t end = body.find('\n', at);
    if (end == std::string::npos) end = body.size();
    std::string line = body.substr(at, end - at);
    at = end + 1;
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();

    if (line.empty()) {
      open = false;
    } else if (line.compare(0, 3, "## ") == 0) {
      blocks.push_back({HelpBlock::Kind::Heading, line.substr(3)});
      open = false;
    } else if (line.compare(0, 2, "- ") == 0) {
      blocks.push_back({HelpBlock::Kind::Item, line.substr(2)});
      open = true;
    } else {
      const size_t first = line.find_first_not_of(' ');
      const std::string text = line.substr(first);
      // A line set in continues a list item; any other line continues a
      // paragraph or starts one.
      const bool continues = open && (blocks.back().kind == HelpBlock::Kind::Item ? first >= 2 : first == 0);
      if (continues) {
        blocks.back().text += " " + text;
      } else {
        blocks.push_back({HelpBlock::Kind::Paragraph, text});
        open = true;
      }
    }
  }
  return blocks;
}

}  // namespace bs
