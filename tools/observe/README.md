# Observe

The observe step of scene understanding (spec 2.13): it looks at a photo
with models that run on this machine and writes the photo's scene file. It
is a Python program, not part of the build, and the engine never runs it.

```powershell
python observe.py photo.jpg [more photos ...]
```

For each photo it writes, beside the photo, `<stem>.scene.json`, the item
layer `<stem>.scene.items.png`, the orientation layer
`<stem>.scene.normals.png`, and `<stem>.scene.preview.jpg` (the items
tinted over the photo, each with its importance in brackets). Look at the
preview; correct the JSON by hand if a name or an importance is wrong.
Running it again overwrites all four. For a batch of five photos it takes
about two minutes, most of it loading the models and listing the items.

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
on `skin` only so far (spec 2.13, spec 5).
