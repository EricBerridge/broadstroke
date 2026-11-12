#include "core/json.hpp"

#include <charconv>

namespace bs {
namespace {

constexpr int kMaxDepth = 32;

struct Parser {
  const std::string& s;
  size_t i = 0;
  std::string err;

  bool fail(const char* what) {
    if (err.empty()) err = std::string(what) + " at byte " + std::to_string(i);
    return false;
  }
  void skip_space() {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
  }
  bool literal(const char* word) {
    size_t n = std::char_traits<char>::length(word);
    if (s.compare(i, n, word) != 0) return fail("unexpected text");
    i += n;
    return true;
  }
  bool hex4(unsigned& out) {
    if (i + 4 > s.size()) return fail("truncated \\u escape");
    out = 0;
    for (int k = 0; k < 4; ++k) {
      char c = s[i++];
      unsigned d;
      if (c >= '0' && c <= '9') d = static_cast<unsigned>(c - '0');
      else if (c >= 'a' && c <= 'f') d = static_cast<unsigned>(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') d = static_cast<unsigned>(c - 'A' + 10);
      else return fail("bad \\u escape");
      out = out * 16 + d;
    }
    return true;
  }
  static void append_utf8(std::string& o, unsigned cp) {
    if (cp < 0x80) {
      o += static_cast<char>(cp);
    } else if (cp < 0x800) {
      o += static_cast<char>(0xC0 | (cp >> 6));
      o += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
      o += static_cast<char>(0xE0 | (cp >> 12));
      o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      o += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
      o += static_cast<char>(0xF0 | (cp >> 18));
      o += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
      o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      o += static_cast<char>(0x80 | (cp & 0x3F));
    }
  }
  bool string(std::string& out) {
    ++i;  // opening quote
    out.clear();
    while (true) {
      if (i >= s.size()) return fail("unterminated string");
      char c = s[i++];
      if (c == '"') return true;
      if (static_cast<unsigned char>(c) < 0x20) return fail("control character in string");
      if (c != '\\') {
        out += c;
        continue;
      }
      if (i >= s.size()) return fail("unterminated string");
      char e = s[i++];
      switch (e) {
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case '/': out += '/'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case 'u': {
          unsigned cp;
          if (!hex4(cp)) return false;
          if (cp >= 0xD800 && cp <= 0xDBFF) {
            // A surrogate pair encodes one character beyond U+FFFF.
            unsigned lo;
            if (s.compare(i, 2, "\\u") != 0) return fail("unpaired surrogate");
            i += 2;
            if (!hex4(lo)) return false;
            if (lo < 0xDC00 || lo > 0xDFFF) return fail("unpaired surrogate");
            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
          } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
            return fail("unpaired surrogate");
          }
          append_utf8(out, cp);
          break;
        }
        default: return fail("bad escape");
      }
    }
  }
  bool number(Json& out) {
    size_t start = i;
    if (i < s.size() && s[i] == '-') ++i;
    while (i < s.size() && ((s[i] >= '0' && s[i] <= '9') || s[i] == '.' || s[i] == 'e' || s[i] == 'E' ||
                            s[i] == '+' || s[i] == '-'))
      ++i;
    // from_chars does not depend on the locale.
    auto res = std::from_chars(s.data() + start, s.data() + i, out.number);
    if (res.ec != std::errc() || res.ptr != s.data() + i) {
      i = start;
      return fail("bad number");
    }
    out.type = Json::Type::Number;
    return true;
  }
  bool value(Json& out, int depth) {
    if (depth > kMaxDepth) return fail("nested too deeply");
    skip_space();
    if (i >= s.size()) return fail("unexpected end");
    char c = s[i];
    if (c == '{') {
      out.type = Json::Type::Object;
      ++i;
      skip_space();
      if (i < s.size() && s[i] == '}') { ++i; return true; }
      while (true) {
        skip_space();
        if (i >= s.size() || s[i] != '"') return fail("expected a member name");
        std::string key;
        if (!string(key)) return false;
        skip_space();
        if (i >= s.size() || s[i] != ':') return fail("expected ':'");
        ++i;
        Json member;
        if (!value(member, depth + 1)) return false;
        out.object.emplace_back(std::move(key), std::move(member));
        skip_space();
        if (i < s.size() && s[i] == ',') { ++i; continue; }
        if (i < s.size() && s[i] == '}') { ++i; return true; }
        return fail("expected ',' or '}'");
      }
    }
    if (c == '[') {
      out.type = Json::Type::Array;
      ++i;
      skip_space();
      if (i < s.size() && s[i] == ']') { ++i; return true; }
      while (true) {
        Json element;
        if (!value(element, depth + 1)) return false;
        out.array.push_back(std::move(element));
        skip_space();
        if (i < s.size() && s[i] == ',') { ++i; continue; }
        if (i < s.size() && s[i] == ']') { ++i; return true; }
        return fail("expected ',' or ']'");
      }
    }
    if (c == '"') {
      out.type = Json::Type::String;
      return string(out.string);
    }
    if (c == 't') { out.type = Json::Type::Bool; out.boolean = true; return literal("true"); }
    if (c == 'f') { out.type = Json::Type::Bool; out.boolean = false; return literal("false"); }
    if (c == 'n') { out.type = Json::Type::Null; return literal("null"); }
    if (c == '-' || (c >= '0' && c <= '9')) return number(out);
    return fail("unexpected character");
  }
};

}  // namespace

const Json* Json::get(const std::string& key) const {
  if (type != Type::Object) return nullptr;
  for (const auto& member : object)
    if (member.first == key) return &member.second;
  return nullptr;
}

bool parse_json(const std::string& text, Json& out, std::string* error) {
  Parser p{text};
  // A byte order mark is not JSON, but editors add one.
  if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) p.i = 3;
  Json doc;
  bool ok = p.value(doc, 0);
  if (ok) {
    p.skip_space();
    if (p.i != text.size()) ok = p.fail("text after the document");
  }
  if (!ok) {
    if (error) *error = p.err;
    return false;
  }
  out = std::move(doc);
  return true;
}

}  // namespace bs
