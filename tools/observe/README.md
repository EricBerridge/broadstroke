# Observe

The observe step of scene understanding (spec 2.13): it looks at a photo
with models that run on this machine and writes the photo's scene file. It
is a Python program, not part of the build, and the engine never runs it.

```powershell
python observe.py photo.jpg [more photos ...]
```

The studio window starts it for the photo that is open there ("Observe
photo", spec 2.10). The window finds this folder and the Python
interpreter through `%LOCALAPPDATA%\Broadstroke\studio.json`;
`tools/studio.example.json` shows its two members. One photo takes about a
minute and a half.

With `--out DIR` it writes into that folder, and with `--name NAME` under
that name (the studio window uses both, to keep the files with its working
files and not beside the photo). Otherwise,
for each photo it writes, beside the photo, `<stem>.scene.json`, the item
layer `<stem>.scene.items.png`, the orientation layer
`<stem>.scene.normals.png`, and `<stem>.scene.preview.jpg` (the items
tinted over the photo, each with its importance in brackets). Look at the
preview; correct the JSON by hand if a name or an importance is wrong.
Running it again overwrites all four. For a batch of five photos it takes
about four minutes: two to list, box and outline the items, and two for
the check.

The check (spec 2.13): what the boxes left out is outlined from points,
and the list model takes a second look at each region by itself. A patch
under a wrong name is given to the right item; a region no item covered is
given to the item it shows or becomes a new item; a single part that
covers its whole is merged into it. What it did is printed and written
into the scene file as `check`. A new item's name comes from one look at
the region alone and is often wrong: read the notes and the preview.

```powershell
python observe.py --no-check photo.jpg [more photos ...]
python observe.py --check [--tell] photo.jpg [more photos ...]
```

`--no-check` writes the scene file without the check. `--check` runs only
the check, on a scene file that already exists; `--tell` prints what the
model said of every region.

```powershell
python observe.py --tidy photo.jpg [more photos ...]
```

runs no model: it removes the specks from the item layer of a scene file
that already exists (as `observe.py` itself now does) and rewrites the
preview.

```powershell
python observe.py --orientation photo.jpg [more photos ...]
```

runs only the orientation model: it adds the orientation layer to a scene
file that already exists and leaves its items as they are.

```powershell
python observe.py --add "pole" --at 0.05,0.37 photo.jpg
python observe.py --remove 17 photo.jpg
```

`--add` makes an item of a thing the scene file does not have as one: the
thing at the point, given as fractions of the photo's width and height,
outlined by SAM 2.1 (about fifteen seconds, most of it loading the model).
If only an item the scene already has is found there, a few places close
around the point are tried, and if none shows another thing nothing is
added. The item layer as it was is kept once as
`<stem>.scene.items.observed.png`; `--remove` takes an added item out
again and gives its pixels back from that copy, without running a model.
Both print one line that says what was done, and both work with `--out`
and `--name`. The studio window runs them from "Add item..." and "Remove
item" in the scene's editor.

## What it needs

- Python with `torch` (a CUDA build), `transformers`, `moge`, `scipy` and
  `Pillow`.
- MoGe-2 (`Ruicheng/moge-2-vitl-normal`), fetched into the folder named by
  `hf_home`.
- A llama.cpp `llama-server` build (the Vulkan build was used).
- Gemma 4 12B and Qwen3.5-4B as GGUF files, each with its `mmproj` file.
- SAM 2.1 Large (`facebook/sam2.1-hiera-large`), fetched by `transformers`
  into the folder named by `hf_home`.
- `observe.local.json` in this folder with the paths. Copy
  `observe.example.json` and edit it. It is not kept in git.

## Scripts

| Script | Does |
| --- | --- |
| `observe.py` | The observe step: list the items, box them, outline them, write the scene file |
| `describe.py` | Runs one vision-language model through llama-server: the list of items, and a box for each. Used by `observe.py`; run on its own it writes a model's answers for comparison (`--items-from` takes the lists from another model's results) |
| `segment.py` | Outlines from boxes with SAM 2.1. Used by `observe.py`; run on its own it writes overlays, with boxes from Grounding DINO or (`--boxes-from model`) from the language model |
| `show.py` | Prints the answers written by `describe.py` |
| `draw_boxes.py` | Draws a language model's boxes on the photo |
| `geometry.py` | Writes depth and surface orientation from MoGe-2 as pictures to judge by eye |

The last four, and the stand-alone modes of `describe.py` and `segment.py`,
are what the models in spec 2.13 were compared with.

Photos used for the comparison are public-domain images from Wikimedia
Commons. A scene file describes its photo, so scene files of private
photos stay out of git like the photos themselves.

The treatments listed in `describe.py` are a first guess. The engine acts
on `skin`, `hair`, `sky`, `cloud` and `foliage_mass` so far (spec 2.13,
spec 5).
