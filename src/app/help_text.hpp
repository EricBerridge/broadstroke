// The help of the studio window (spec 2.10): what a user of the window needs
// to know, as pages of plain text. The window shows them in its help window;
// nothing here depends on the platform.
//
// A page's body is written in a small markup:
//   "## Heading"   a heading
//   "- item"       a list item; lines that follow, set in by two spaces,
//                  continue it
//   a blank line   ends a paragraph; the lines of a paragraph are joined
//   `text`         text shown as typed (a file name, a command)
//
// Keep the pages current with the window: a change to what a control does
// changes its page and its tooltip in the same change.
#pragma once

#include <string>
#include <vector>

namespace bs {

enum class HelpTopic : int {
  Start,
  Painting,
  Scene,
  Observe,
  Manners,
  Files,
  Setup,
  CommandLine,
  Count,
};

struct HelpPage {
  const char* title;
  const char* body;
};

// The page of a topic. Plain ASCII.
const HelpPage& help_page(HelpTopic topic);

// One piece of a page's body, as it is to be shown. The text still holds
// the backticks around what is shown as typed.
struct HelpBlock {
  enum class Kind { Heading, Paragraph, Item };
  Kind kind = Kind::Paragraph;
  std::string text;
};

// A body in the markup above as the pieces it is made of, in order.
std::vector<HelpBlock> parse_help(const std::string& body);

}  // namespace bs
