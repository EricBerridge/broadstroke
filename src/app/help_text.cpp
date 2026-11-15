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

- Left: what to paint with and how (medium, surface, size, effort, seed),
  Start, Save, and what the preview shows.
- Middle: the preview. It shows the photo, its scene, an imagined subject,
  or the artwork while it is being made.
- Right: the scene, which says what the photo shows, and the manners, which
  interpret it. Both are optional.

## The shortest way to an artwork

- Press "Open photo..." or drop a photo on the window.
- Choose a medium.
- Press Start and watch. Start reads Stop while the work is in progress.
- Press "Save artwork...".

## Finding out more

- Rest the pointer on a control for a short explanation.
- Press F1 for the help page of the part of the window you are in, or
  press Help.
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
  else. Without a scene it does nothing.
- Watercolor, Key: how light the picture is keyed, from 0.50 (dark) to 1.50
  (light). The default is 0.85. Below 1 the mid-tones are aimed darker than
  the photo's, above 1 lighter, toward the paper. The darks stay anchored.
- Pastel has no setting of its own.

## Start, Stop and Save

- Start makes the artwork. The preview shows the canvas as it is worked,
  and the status says what the artist is doing.
- Stop ends the work. What has been made so far stays and can be saved.
- "Save artwork..." writes PNG, JPEG or BMP, by the ending of the file
  name. The name offered is the photo's with the medium added.

## Show

What the preview shows: Photo; Scene, if the photo has one; Imagined
subject, if a manner is chosen and its subject has been made; Artwork, once
one has been started.
)help"},

    {"The scene", R"help(
A scene file tells the artist what the photo shows: a list of items (a
face, a tree, the sky) and which item each part of the picture belongs to.
It lies beside the photo and has the photo's name with `.scene.json` in
place of the ending. It is loaded when the photo is opened. Without one the
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

## Use the scene when painting

On by default. Off, the photo is painted as if it had no scene file. The
file itself is not changed.

## The list of items

One row for each item, in the order of the file. A part is set in below the
whole it belongs to. The colored square is the item's color in the scene
view.

## The scene view

Choose Scene under Show, or select an item. With nothing selected, every
item is tinted in its color. With an item selected, it and its parts keep
the photo's light and the rest is dimmed. Click an item in the scene view
to select it.

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

An item's name, the whole it is a part of, and the main subject cannot be
changed here. The scene file is plain text (JSON) and can be edited.
)help"},

    {"Observing a photo", R"help(
"Observe photo" writes the scene file of the open photo. It looks at the
photo with models that run on this machine. Nothing is sent anywhere.

- It takes a minute or two. The status shows the time and what the step
  last reported, and the button reads Stop while it runs.
- When it ends, the new scene is loaded and shown in the scene view. Look
  it over: a wrong importance, treatment or edge can be corrected in the
  list.
- If the photo has a scene file already, you are asked first. Observing
  again replaces the file, and corrections made to it are lost.
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
- Futurism: a thing that moves trails fading repeats of itself (echoing).
- Metaphysical: figures throw long shadows across the ground, as under a
  low sun (long shadow).
- Solid of triangles: one thing is rebuilt as a solid of flat triangles,
  each painted in one color (triangulated).

## Imagine

Choose a manner and press Imagine. A model on this machine chooses, for
each item, one of the manner's mechanisms and a strength. Then the changes
are carried out on the photo. The result is the imagined subject, which the
preview shows. A manner needs the photo's scene.

- If there is a choice for this manner already, Imagine carries it out
  without asking the model again.
- "Ask the model again" has the model choose anew. The choice shown is
  replaced, with anything you changed in it.

## Changing what the manner does

With a manner chosen, the list has a column named Change. Select an item
and set, in the row named Change below the list:

- the mechanism: one of the manner's, or "As it is" to leave the item
  alone;
- the strength: 1 slight, 2 clear, 3 extreme. For the solid of triangles it
  is how fine the triangles are: 1 coarse, 2 medium, 3 fine.

The change is saved to the manner's choice file at once. Press Imagine to
carry it out. Not every mechanism can be carried out on every item: a thing
may be too small, or cut off by the edge of the picture. The status says
what was done.

## Painting it

With a manner chosen, Start paints the imagined subject in place of the
photo. If the subject has not been made, or the choice was changed since it
was made, Start asks you to press Imagine first. Choose "As the photo is"
to paint the photo again.

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
The window reads and writes ordinary files beside the photo. For a photo
named `harbor.jpg`:

- `harbor.scene.json`: the scene file. Read when the photo is opened;
  written by "Observe photo" and by a correction in the list of items.
- `harbor.scene.items.png`, `harbor.scene.normals.png` and
  `harbor.scene.preview.jpg`: the layers of the scene and a preview,
  written by "Observe photo".
- `harbor.scene.observed.json`: the scene file as it was before its first
  correction.
- `harbor.interpret`, a folder: what Imagine makes. In it, `choices` holds
  one choice file for each manner and `subjects` the imagined subjects with
  scene files of their own.
- `harbor_oil.png`: the name offered when an oil painting of it is saved.
  With a manner it is `harbor_surrealism_oil.png`.

Nothing else is written, and the photo itself is never changed.

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
missing or wrong, pressing "Observe photo" or Imagine says what is missing,
and the rest of the window works.

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
- `--no-scene` paints without the scene file, and `--scene FILE` uses
  another one than the file beside the photo.
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
