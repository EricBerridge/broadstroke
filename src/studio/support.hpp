// What the studio window needs that is not a window: the words of the scene
// file and of the manners, strings and paths, where the steps outside the
// engine are, and the pictures of the preview. Nothing here touches a
// control, so it can be read apart from the window.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/image.hpp"
#include "vision/scene.hpp"

namespace bs::studio {

inline constexpr wchar_t kAppTitle[] = L"Broadstroke";

inline constexpr int kMaxPreviewSide = 4096;  // larger photos are reduced for display only

inline constexpr int kSizes[] = {800, 1000, 1400, 2000, 2800};
inline constexpr int kDefaultSizeIndex = 2;

struct EffortChoice {
  const wchar_t* label;
  float value;
};
inline constexpr EffortChoice kEfforts[] = {{L"Quick study", 0.5f}, {L"Finished piece", 1.0f}, {L"Meticulous", 1.5f}};
inline constexpr int kDefaultEffortIndex = 1;

// The watercolor key on its slider: one position per 0.05, counted in
// hundredths so that the value handed to the engine is exact.
inline constexpr int kKeyStepPct = 5;

// The manners of spec 2.14 and the mechanisms each allows, as the interpret
// step names them (tools/interpret/vocab.py).
struct Manner {
  const char* id;
  const wchar_t* label;
  std::vector<const char*> mechanisms;
};
const std::vector<Manner>& manners();
inline constexpr char kAsItIs[] = "as it is";  // the mechanism of an item that is left alone

// Cubism (tools/interpret/imagine.py, facet): the number of planes the
// strongest strength among the faceted items gives unless the choice file
// states one, and the steps of how far apart the planes are, 1 to 3, of
// which the second is used unless the file states another.
inline constexpr int kFacetPlanes[] = {12, 18, 26};
inline constexpr const wchar_t* kFacetApart[] = {L"In place", L"Shifted", L"Dislocated"};
inline constexpr int kFacetApartDefault = 2;

// Where a solid of triangles ends (spec 2.14) is decided by the two
// materials (treatments) that meet there: raggedly or abruptly. By default
// a pair is ragged if either material is ragged by nature or the edges of
// both items are soft or lost; that is the interpret step's rule and its
// list (tools/interpret/facts.py, RAGGED and ragged_table). A choice file
// may state a pair to be the one or the other, as "a:b".
inline constexpr const char* kRaggedByNature[] = {"foliage_mass", "trunk_or_branches", "grass", "flowers", "hair",
                                                  "fur_or_feathers", "foam_or_spray", "cloud", "rock"};
inline constexpr char kNoMaterial[] = "none";  // what has no item, and an item whose treatment is not stated
// Materials that are thin things. A part made of them is not rebuilt with
// the item it belongs to, so it is something the item meets (facts.py, THIN).
inline constexpr const char* kThinMaterials[] = {"trunk_or_branches"};

// A pair of materials as it is compared: lower case, in alphabetical order,
// "a:b". The second form takes it as a file states it; empty if that is not
// a pair.
std::string pair_key(std::string a, std::string b);
std::string pair_key(const std::string& stated);

// A pair of materials that meet along the outline of what is rebuilt when
// an item becomes a solid of triangles (the item with its parts, but for
// thin ones), or between its parts.
struct Meeting {
  std::string key;                      // pair_key of the two
  std::string own, other;               // the item's material, and the one it meets
  std::vector<std::string> names;       // the items of that other material that it meets; none for no item
  bool ragged = false, abrupt = false;  // by default; both if it differs from stretch to stretch
};
// The pairs for the item at `item_index` of scene.items, with its parts.
std::vector<Meeting> meetings_of(const Scene& scene, int item_index);

// The words a scene file uses (spec 2.13). The treatments are the list the
// observe step chooses from (tools/observe/describe.py).
inline constexpr const char* kTreatments[] = {
    "skin",        "hair",         "fur_or_feathers", "fabric", "foliage_mass", "trunk_or_branches",
    "grass",       "flowers",      "sky",             "cloud",  "still_water",  "moving_water",
    "foam_or_spray", "rock",       "earth_or_sand",   "wood",   "metal",        "glass",
    "smooth_manufactured", "masonry", "food",         "other"};
inline constexpr const wchar_t* kImportanceNames[] = {L"Background", L"Supporting", L"Matters", L"Main subject"};
inline constexpr const wchar_t* kEdgeNames[] = {L"Hard", L"Soft", L"Lost"};

// ---------------------------------------------------------------------------
// Strings and paths

std::wstring widen(const std::string& utf8);
std::string to_utf8(const std::wstring& w);

// "cold_press" -> "cold press" (or "Cold press").
std::wstring pretty_name(const std::string& id, bool capitalize);

std::wstring file_name_of(const std::wstring& path);
std::wstring directory_of(const std::wstring& path);
std::wstring stem_of(const std::wstring& path);       // file name without its extension
std::wstring extension_of(const std::wstring& path);  // lower case, without the dot, or empty
std::wstring full_path(const std::wstring& path);

bool is_file(const std::wstring& path);
bool read_text(const std::wstring& path, std::string& out);
bool write_text(const std::wstring& path, const std::string& text);
// Whether file `a` was written after file `b`. False if either is missing.
bool written_after(const std::wstring& a, const std::wstring& b);

// ---------------------------------------------------------------------------
// About
//
// What Help > About says below the program's name: the version, whose the
// program is, and this computer's processor, memory and graphics adapters
// with their memory.

struct Computer {
  std::wstring processor;       // the name the processor gives itself; empty if it gives none
  int logical_processors = 0;
  uint64_t memory_bytes = 0;    // installed memory
  struct Graphics {
    std::wstring name;
    uint64_t memory_bytes = 0;  // the adapter's own memory; 0 if it only shares the computer's
  };
  std::vector<Graphics> graphics;  // hardware adapters, in the system's order
};

Computer this_computer();
std::wstring about_text(const char* version);

// ---------------------------------------------------------------------------
// The steps outside the engine
//
// Observe (spec 2.13) and interpret (spec 2.14) run models and are programs
// of their own, in Python. The window starts them and reads the files they
// write; it never runs a model itself. Where they are is machine-specific
// and comes from a small local file.

struct ToolsConfig {
  std::wstring python;  // the Python interpreter that has the tools' packages
  std::wstring tools;   // the folder that holds observe/ and interpret/
};

std::wstring tools_config_path();

// Reads the local file. On failure `problem` says what is missing and how to
// set it right, for a message; `brief`, if given, only what is missing, in
// one line, for a tooltip.
bool read_tools_config(ToolsConfig& config, std::wstring& problem, std::wstring* brief = nullptr);

// One program to run, and the folder it runs in.
struct ToolStep {
  std::wstring command, directory;
};

// ---------------------------------------------------------------------------
// Pictures for the preview

// An image ready for the screen: 32-bit BGRA, top-down, opaque.
struct Frame {
  int w = 0, h = 0;
  std::vector<uint8_t> bgra;
};

std::unique_ptr<Frame> make_frame(const Image& img);

// The display copy of a photo: reduced if it is larger than the preview can
// use. The artist still gets the full photo.
std::unique_ptr<Frame> make_display_frame(const Image& img);

// `src` reduced to w x h by averaging the pixels that fall on each new one.
// w and h must not exceed the frame's own size.
void reduce_frame(const Frame& src, int w, int h, uint8_t* bgra_out);

// The color an item is tinted with in the scene view and marked with in the
// list: hues spread by the golden ratio, so neighbors in the list differ.
struct Rgb {
  uint8_t r, g, b;
};
Rgb item_color(int row);

// The items tinted over the photo, for the screen only. With an item
// selected (its index in scene.items, or -1), it and its parts keep the
// photo's light and the rest is dimmed.
std::unique_ptr<Frame> make_scene_frame(const Frame& photo, const Scene& scene, int selected);

}  // namespace bs::studio
