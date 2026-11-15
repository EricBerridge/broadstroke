// A small JSON reader for the engine's own input files (the scene file,
// spec 2.13). It reads a whole document into a tree. The writer is for the
// application, which saves a scene file corrected in the studio window; the
// engine writes no JSON.
#pragma once

#include <utility>

#include "core/common.hpp"

namespace bs {

struct Json {
  enum class Type { Null, Bool, Number, String, Array, Object };
  Type type = Type::Null;
  bool boolean = false;
  double number = 0.0;
  std::string string;
  std::vector<Json> array;
  std::vector<std::pair<std::string, Json>> object;  // members in file order

  bool is_number() const { return type == Type::Number; }
  bool is_string() const { return type == Type::String; }
  bool is_array() const { return type == Type::Array; }
  bool is_object() const { return type == Type::Object; }
  // The member of an object with this key, or null if there is none (or
  // this is not an object).
  const Json* get(const std::string& key) const;
  // The member of an object with this key, added at the end (as null) if
  // there is none. A value that is not an object becomes an empty one first.
  Json& member(const std::string& key);

  static Json of(double v);
  static Json of(bool v);
  static Json of(const std::string& v);
};

// Parses a JSON document. Input is untrusted: nesting is limited, and
// anything after the document other than white space is an error. Returns
// false and sets *error (with the byte offset) on failure.
bool parse_json(const std::string& text, Json& out, std::string* error = nullptr);

// Writes a document as text: one member or element per line, indented by one
// space per level, text beyond ASCII as it is (UTF-8), a newline at the end.
// This is the layout the observe step writes (spec 2.13), so a file saved
// again differs from it only where a value changed. A whole number is written
// without a fraction. Reading the text back gives the same tree.
std::string write_json(const Json& doc);

}  // namespace bs
