This file covers: giving the artists an understanding of what the photograph shows. Index: [README.md](README.md).

### 2.13 Scene understanding

**Status.** Built: the scene file and its reader, the observe step that
writes it, importance, work by item in watercolor and pastel, and one
treatment, skin in watercolor. Not built: work by item in oil and colored
pencil, the other treatments, stroke direction from the form, and the
depth and orientation layers. The points still undecided are in spec 5.

**Problem**

Without a scene the artists work the same way whatever the subject is. They
see lightness, edges, detail and flow (spec 2.1), not a face, a tree, a
wrist watch or sea foam, each of which an artist would draw differently.

**Concept**

- A separate observe step, outside the engine, looks at the photo with
  models that run on the local machine and writes a scene file next to the
  photo. No online service is used.
- The engine only reads the scene file. No model runs inside the engine,
  because model output is not reproducible bit for bit across graphics
  cards, backends or batch sizes.
- The scene file is input alongside the photo, the settings and the seed:
  the same four give the same artwork (spec 2.12). Without a scene file the
  artwork is byte-identical to what the engine makes without this feature.
- The scene file can be read and corrected by hand, so a wrong answer from a
  model is fixed in the file, not worked around in the engine.

**The scene file** (`src/vision/scene.*`)

- A photo's scene file is the photo's path with its extension replaced by
  `.scene.json`. It is UTF-8 JSON, with one image beside it.
- Members the engine reads:

  | Member | Meaning |
  | --- | --- |
  | `version` | 1 |
  | `items` | 1 to 255 items |
  | item `id` | a whole number from 1 to 255, used once |
  | item `name` | what the item is, in words |
  | item `parent` | the id of the item it is a part of, or 0; may be left out |
  | item `importance` | 0 background, 1 supporting, 2 matters to the picture, 3 the main subject and its key parts |
  | item `treatment` | how the item is to be worked, as one word; may be left out. Only `skin` is acted on so far |
  | `main_subject` | the id of the most important item, or 0; may be left out |
  | `item_layer` | the file name of the item layer, in the same folder |

- The item layer is an 8-bit grayscale PNG. Each pixel holds the id of the
  deepest item that covers it (a part before its whole), or 0 where no item
  does. It may have any resolution with the photo's proportions (within 2
  percent); the engine takes the nearest layer pixel for each canvas cell.
- Other members are ignored by the engine. The observe step also writes
  `photo`, `scene` (a sentence), `light_from`, and per item `edges` and
  `how_many`, for a person reading the file and for later uses.
- The file is untrusted input. It is refused, with a message that names the
  fault, if it is not valid JSON, is larger than 4 MB or nested deeper than
  32 levels, has another version, has an item with a missing or repeated id,
  no name, an importance outside 0 to 3, a treatment that is not text, or a
  parent that is not an item or leads back to itself, names a layer in another folder, has a layer that is
  not 8-bit grayscale or exceeds 120 megapixels, or has a layer pixel whose
  value no item has.
- Not yet in the file: depth, surface orientation, the order of items from
  front to back, and which pixels belong to which instance of an item.

**Importance** (`apply_importance` in `src/vision/analysis.cpp`)

- The detail map is the artists' measure of care: a higher value means a
  lower tolerance for error and finer work (spec 2.9). With a scene, the
  detail of each cell is scaled by a gain and raised to at least a floor,
  both set by the importance of the cell's item:

  | Importance | Gain | Floor |
  | --- | --- | --- |
  | 0 background | 0.40 | 0 |
  | 1 supporting | 0.75 | 0 |
  | 2 matters to the picture | 1 | 0.35 |
  | 3 main subject and its key parts | 1 | 0.70 |

- The floor is there because what matters most is often smooth: a cheek
  has little edge energy. The gain below 1 is there because a busy
  background would otherwise earn as much care as the subject.
- Cells that belong to no item keep the detail they had.
- Gain and floor are blurred over 0.6 percent of the long side, so care
  does not change abruptly at an item's outline.
- Nothing else the artist sees changes: color, lightness, edges and the
  flow field are as without a scene.

**Work by item** (watercolor and pastel; `paint_watercolor.cpp`,
`paint_dry.cpp`, `find_outlines` and `blur_within_items` in
`src/vision/analysis.cpp`)

Watercolor:

- With a scene the watercolor artist works one item at a time instead of
  across the sheet. Reason: the washes of one form then join while they
  are wet and their dry edges fall on the form's outline. Worked across the
  sheet, a wash covers part of a face and part of what is behind it, and
  its dry edge runs through the face.
- The items are taken in the order they are first met going down the
  sheet. Within an item the patches go row by row, turning at the end of
  each row so the next wash starts beside the last. Cells that belong to no
  item are worked as one more item.
- A patch that lies on several items gives each its own wash, judged at a
  point of the patch inside that item and from that item's cells only. An
  item that covers less than a twelfth of a patch is left to a pass with
  smaller patches.
- For each pass the subject is softened within each item separately, so
  the color wanted near an outline is the item's own and not a mixture with
  its neighbor's.
- A stroke belongs to its item. Within two widths of the mark from an
  outline, a stroke already running beside the outline turns along it; a
  stroke that meets an outline head-on ends there. The middle of the mark
  stops 0.3 of its width short of the outline, so its edge and not its
  middle meets the outline. The strip this leaves is worked by the next,
  smaller brush.
- Away from outlines the strokes run as they do without a scene.

Pastel:

- A hatched patch that lies on several items is worked once for each,
  judged at a point of the patch inside that item and from that item's
  cells only. An item that covers less than a twelfth of a patch is left to
  a pass with smaller patches. Reason for a share per item: if a patch were
  worked only for the item at its center, the parts of other items inside
  it would never get the broad strokes, and the paper would show there.
- A hatch line ends where its middle would leave the item. The mark may
  lap over the outline by half its width. Held further back, the marks
  left the paper bare along every outline.
- Near an outline the fingertip rubs along it, not across, and stays on its
  own side: pastel is not dragged from one item into the next. A rubbing
  stroke is shortened to the stretch inside the item and left out if less
  than a quarter of it remains.
- The closing pass (spec 2.9) also closes what is left open between the
  marks of two items along their outline.
- The order of work is as without a scene.

Oil and colored pencil do not work by item. Oil holds its outlines without
it, because later opaque layers correct the earlier ones.

**The observe step** (`tools/observe/observe.py`, Python)

- For each photo it writes the scene file, the item layer
  (`<stem>.scene.items.png`) and a preview of the items tinted over the
  photo (`<stem>.scene.preview.jpg`), beside the photo. Running it again
  overwrites them.
- A vision-language model names the items, relates them (parent and child),
  says which is the main subject, and picks each item's treatment and
  attributes from fixed lists. A second vision-language model gives a rough
  box around each instance of each item.
- A segmentation model turns each box into an outline. The box only says
  where to look; no boundary in the scene file is a box. This placed the
  names of parts (face, hair; watch case, dial, strap) on the right shapes,
  where a separate detector searching for each name did not.
- The item layer is composed with wholes first and parts over them, and
  within a level the larger area first.
- Specks are then removed from the layer. The segmentation model leaves
  stray dots of one item inside another, and an artist who works by item
  would work around each as an outline. A connected patch is a speck if it
  covers less than 0.04 percent of the picture and less than a twentieth of
  the largest patch of its own item (so each of many small birds is kept);
  it takes the item of the nearest cell that is kept. `observe.py --tidy`
  does this to scene files that already exist, without running a model.
- The photo is looked at with its long side reduced to at most 2048 pixels,
  and the item layer is written at that size.
- The models and their locations are named in a config file on the machine
  (`observe.local.json`, not in git; `observe.example.json` shows the keys).
- Below the part level (clusters of leaves, crests of waves) nothing is
  taken from a model. The intent is that the existing tone and flow
  analysis finds those shapes inside the item's region, steered by the
  item's treatment (not built).

**Models**

| Part | Model | License |
| --- | --- | --- |
| List of items, treatments, main subject | Gemma 4 12B | Apache-2.0 |
| A box for each instance | Qwen3.5-4B | Apache-2.0 |
| Outline from each box | SAM 2.1 Large | Apache-2.0 |
| Depth and surface orientation (tried, not yet used) | MoGe-2 | MIT |
| Parts of a face, if the boxes do not give them (not tried) | SegFace | MIT |
| Outlines from names, held in reserve (not tried) | SAM 3 | Meta's SAM License; the download is gated |
| Parts of a body, held in reserve (not tried) | Sapiens2 | Meta's Sapiens2 License |

- The language models run with llama.cpp through its server, with the
  answer constrained by a JSON schema. The Vulkan build was used.
- Chosen on five photographs. Set aside: Grounding DINO for the boxes (it
  put the names of parts on the wrong shapes), Gemma's own boxes (too far
  off), a Qwen model for the list (it repeats itself in a busy scene),
  Qwen3.5-9B (no better than the 4B at over twice the size).
- SAM 3 may be tried if the large undefined areas of a picture (sky, water,
  background) cannot be outlined well from boxes.
- Sapiens2 may be tried if outlines of body parts turn out to be needed.
  Its license forbids biometric processing without defining it and lets
  Meta audit use at its own discretion; if it is used, only its
  segmentation output is, not its keypoints.

**Treatments** (one built: skin in watercolor)

- The artists never act on an item's name. Each item maps to one of a small
  fixed set of treatments (on the order of twenty: skin, hair, foliage
  mass, branches, still water, broken water, foam, sky, cloud, metal, fabric
  and so on), so that the open-ended variety of subjects does not become an
  open-ended variety of code paths.
- A treatment is a few numbers an artist already has a use for: the
  smallest mark, how hard the edges are, where stroke direction comes
  from, and the order of working.
- Skin in watercolor is worked wet in wet. In the second washes, the
  shapes and the details, the artist first goes over the strokes of a wash
  with clean water and then lays the color into the damp place. The first
  washes are broad and wet already; the fine details stay on dry paper, so
  the eyes and the line of the mouth keep their edge. Reason: painted on
  dry paper, the folds of a face dried as hard dark strokes across it.
- Known limit: the folds are softer but still read as lines. They stand
  out mainly because the watercolor's skin is much paler than the
  photograph's.

**Rules**

- A layer informs the artist's decisions and never clips or masks what a
  tool deposits. The artist keeps to a shape by steering and stopping the
  stroke (spec 2.1).
- Tests use scenes written by hand. No model runs in a test.

**Acceptance** (`tests/test_scene.cpp`): the JSON reader reads documents
and refuses malformed and deeply nested ones; a scene file is read, and
each fault listed above is refused, and an item's treatment is read; with
a scene the care is higher on the
main subject and lower on the background than where no item is, where it
is unchanged, and nothing but the care changes; the same scene gives the
same artwork and a scene changes the artwork; an empty scene, or one
without the photo's proportions, is not used; outlines are found where
items meet, with the distance to them and their direction; a subject
softened within its items keeps each item's color up to its outline; in
watercolor a pale form beside a dark one stays pale up to the outline with
a scene, and is darkened there without one; in pastel a narrow pale form
in a dark surrounding keeps its tone worked by item, and no paper is left
bare along its outline; in watercolor the skin treatment reaches the
artist and a dark fold on skin is still painted. The
observe step has no automated test; it is judged by its preview.
