// What the application adds around the engine (spec 2.10): writing JSON,
// and saving a scene file corrected in the studio window.
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "check.hpp"
#include "app/help_text.hpp"
#include "app/scene_edit.hpp"
#include "core/json.hpp"
#include "stb_image_write.h"

using namespace bs;

namespace {

struct TempDir {
  std::filesystem::path dir;
  TempDir() : dir(std::filesystem::temp_directory_path() / "broadstroke_test_app") {
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
  }
  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
  }
  std::string path(const char* name) const { return (dir / name).string(); }
  void write(const char* name, const std::string& text) const {
    std::ofstream out(dir / name, std::ios::binary);
    out << text;
  }
  std::string read(const char* name) const {
    std::ifstream in(dir / name, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  }
};

// A scene file in the layout the observe step writes, with members the
// engine does not read.
const char* kObserved =
    "{\n"
    " \"version\": 1,\n"
    " \"photo\": \"caf\xC3\xA9.jpg\",\n"
    " \"scene\": \"A woman says \\\"hello\\\" at a market.\",\n"
    " \"main_subject\": 2,\n"
    " \"item_layer\": \"items.png\",\n"
    " \"items\": [\n"
    "  {\n"
    "   \"id\": 1,\n"
    "   \"name\": \"woman\",\n"
    "   \"parent\": 0,\n"
    "   \"treatment\": \"fabric\",\n"
    "   \"importance\": 3,\n"
    "   \"edges\": \"hard\",\n"
    "   \"how_many\": \"one\"\n"
    "  },\n"
    "  {\n"
    "   \"id\": 2,\n"
    "   \"name\": \"face\",\n"
    "   \"parent\": 1,\n"
    "   \"importance\": 3\n"
    "  }\n"
    " ],\n"
    " \"check\": [\n"
    "  \"1.5% of the picture had no item\"\n"
    " ],\n"
    " \"empty\": [],\n"
    " \"ratio\": 0.25\n"
    "}\n";

}  // namespace

TEST(json_is_written_as_it_was_read) {
  Json doc;
  std::string err;
  CHECK(parse_json(kObserved, doc, &err));
  // The observe step's layout comes back byte for byte.
  CHECK(write_json(doc) == kObserved);

  // Whatever is written reads back as the same values.
  Json odd;
  odd.member("text") = Json::of(std::string("tab\there \"quoted\" back\\slash\nline \x01 \xC3\xA9"));
  odd.member("whole") = Json::of(-42.0);
  odd.member("fraction") = Json::of(0.1);
  odd.member("large") = Json::of(1e300);
  odd.member("yes") = Json::of(true);
  odd.member("nothing");
  odd.member("list").type = Json::Type::Array;
  odd.member("list").array.push_back(Json::of(1.0));
  odd.member("list").array.push_back(Json{});
  Json back;
  CHECK(parse_json(write_json(odd), back, &err));
  CHECK(back.get("text") && back.get("text")->string == odd.get("text")->string);
  CHECK(back.get("whole")->number == -42.0 && back.get("fraction")->number == 0.1 && back.get("large")->number == 1e300);
  CHECK(back.get("yes")->boolean && back.get("nothing")->type == Json::Type::Null);
  CHECK(back.get("list")->array.size() == 2 && back.get("list")->array[1].type == Json::Type::Null);
  CHECK(write_json(back) == write_json(odd));
  // member() finds what is there and adds what is not.
  CHECK(&odd.member("whole") == odd.get("whole") && odd.object.size() == 7);
}

TEST(help_pages_are_well_formed) {
  // The markup: headings, paragraphs whose lines are joined, list items
  // with lines that continue them.
  std::vector<HelpBlock> b = parse_help("\none line\nand the next\n\n## Head\n\n- first item\n  goes on\n- second\nafter\n");
  CHECK(b.size() == 5);
  if (b.size() == 5) {
    CHECK(b[0].kind == HelpBlock::Kind::Paragraph && b[0].text == "one line and the next");
    CHECK(b[1].kind == HelpBlock::Kind::Heading && b[1].text == "Head");
    CHECK(b[2].kind == HelpBlock::Kind::Item && b[2].text == "first item goes on");
    CHECK(b[3].kind == HelpBlock::Kind::Item && b[3].text == "second");
    CHECK(b[4].kind == HelpBlock::Kind::Paragraph && b[4].text == "after");
  }
  CHECK(parse_help("").empty());

  // Every topic has a page that can be shown.
  for (int t = 0; t < static_cast<int>(HelpTopic::Count); ++t) {
    const HelpPage& page = help_page(static_cast<HelpTopic>(t));
    const std::string title = page.title, body = page.body;
    CHECK(!title.empty());
    bool plain = true;
    for (char c : title + body) plain = plain && (c == '\n' || (c >= 0x20 && c < 0x7F));
    CHECK(plain);  // ASCII, as the window shows it
    const std::vector<HelpBlock> blocks = parse_help(body);
    CHECK(blocks.size() >= 3);
    for (const HelpBlock& block : blocks) {
      CHECK(!block.text.empty());
      // Backticks come in pairs within one piece.
      CHECK(std::count(block.text.begin(), block.text.end(), '`') % 2 == 0);
    }
    if (!plain || blocks.size() < 3) std::printf("    page %d: %s\n", t, page.title);
  }
}

TEST(scene_corrections_are_saved_to_the_file) {
  TempDir tmp;
  std::vector<uint8_t> px(40 * 30, 1);
  for (size_t i = 0; i < px.size(); ++i) px[i] = (i % 40) < 20 ? 1 : 2;
  stbi_write_png(tmp.path("items.png").c_str(), 40, 30, 1, px.data(), 40);
  tmp.write("p.scene.json", kObserved);
  const std::string path = tmp.path("p.scene.json");
  CHECK(observed_copy_path("C:/a/p.scene.json") == "C:/a/p.scene.observed.json");

  std::string err;
  Scene sc = load_scene(path, &err);
  CHECK(!sc.empty() && sc.items.size() == 2);

  // Saved unchanged, the file differs only by the mark that it was saved
  // here, and the file as it was is kept beside it.
  bool kept = false;
  CHECK(save_scene_items(path, sc.items, &kept, &err));
  CHECK(kept);
  CHECK(tmp.read("p.scene.observed.json") == kObserved);
  std::string expected = kObserved;
  expected.replace(expected.rfind("\n}"), 2, ",\n \"edited\": true\n}");
  CHECK(tmp.read("p.scene.json") == expected);

  // A correction is read back by the engine's reader; what the engine does
  // not read is still there.
  sc.items[0].importance = 1;
  sc.items[0].treatment = "skin";
  sc.items[1].edges = Edges::Soft;
  sc.items[1].treatment = "hair";
  CHECK(save_scene_items(path, sc.items, &kept, &err));
  CHECK(!kept);  // the copy is made once
  CHECK(tmp.read("p.scene.observed.json") == kObserved);
  Scene again = load_scene(path, &err);
  CHECK(!again.empty() && again.main_subject == 2);
  CHECK(again.find(1)->importance == 1 && again.find(1)->treatment == "skin" && again.find(1)->edges == Edges::Hard);
  CHECK(again.find(2)->importance == 3 && again.find(2)->treatment == "hair" && again.find(2)->edges == Edges::Soft);
  CHECK(again.find(1)->name == "woman" && again.find(2)->parent == 1);
  const std::string text = tmp.read("p.scene.json");
  CHECK(text.find("\"how_many\": \"one\"") != std::string::npos);
  CHECK(text.find("1.5% of the picture had no item") != std::string::npos);
  CHECK(text.find("caf\xC3\xA9.jpg") != std::string::npos);
  CHECK(text.find("\"ratio\": 0.25") != std::string::npos);

  // An item the file does not have changes nothing; a file that cannot be
  // read is reported and nothing is written.
  std::vector<SceneItem> stranger = {{9, "stranger", 0, 2}};
  CHECK(save_scene_items(path, stranger, &kept, &err));
  CHECK(tmp.read("p.scene.json") == text);
  // Whether an item is brought out is stated in the file, and taken back
  // by leaving it out again.
  std::vector<SceneItem> chosen = again.items;
  chosen[0].bring_out = 1;
  chosen[1].bring_out = 0;
  CHECK(save_scene_items(path, chosen, &kept, &err));
  Scene said = load_scene(path, &err);
  CHECK(!said.empty() && said.find(1)->bring_out == 1 && said.find(2)->bring_out == 0);
  CHECK(said.brought_out()[1] && !said.brought_out()[2]);
  CHECK(save_scene_items(path, again.items, &kept, &err));
  CHECK(tmp.read("p.scene.json") == text);
  // Likewise that an item is to be erased: stated only where it is.
  chosen = again.items;
  chosen[1].erase = true;
  CHECK(save_scene_items(path, chosen, &kept, &err));
  said = load_scene(path, &err);
  CHECK(!said.empty() && !said.find(1)->erase && said.find(2)->erase);
  CHECK(tmp.read("p.scene.json").find("\"erase\": true") != std::string::npos);
  CHECK(save_scene_items(path, again.items, &kept, &err));
  CHECK(tmp.read("p.scene.json") == text);
  CHECK(!save_scene_items(tmp.path("absent.scene.json"), sc.items, &kept, &err) && !err.empty());
  CHECK(!std::filesystem::exists(tmp.dir / "absent.scene.json"));
  tmp.write("bad.scene.json", "{ nonsense");
  CHECK(!save_scene_items(tmp.path("bad.scene.json"), sc.items, &kept, &err));
  CHECK(tmp.read("bad.scene.json") == "{ nonsense");
}
