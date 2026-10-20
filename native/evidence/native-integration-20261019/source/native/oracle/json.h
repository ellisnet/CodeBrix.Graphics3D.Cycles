// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Minimal strict JSON reader for oracle scene files. Numbers keep their source
// text so that floats are rounded once, directly from decimal, like C literals.
#pragma once
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace oracle {

struct Json {
  enum Kind { Null, Boolean, Number, String, Array, Object } kind = Null;
  bool boolean = false;
  std::string text;  // string value, or the number's source text
  std::vector<Json> items;
  std::vector<std::pair<std::string, Json>> fields;

  const Json *find(const std::string &key) const {
    for (auto &[name, value] : fields) if (name == key) return &value;
    return nullptr;
  }
  const Json &at(const std::string &key) const {
    auto value = find(key);
    if (!value) throw std::runtime_error("Missing JSON field: " + key);
    return *value;
  }
  bool is(Kind wanted) const { return kind == wanted; }
  const std::string &string() const {
    if (kind != String) throw std::runtime_error("Expected a JSON string");
    return text;
  }
  float as_float() const {
    if (kind != Number) throw std::runtime_error("Expected a JSON number");
    // strtof rounds correctly; floating-point from_chars needs macOS 26 libc++.
    // The text already matched the JSON grammar and the C locale is never changed.
    char *end = nullptr;
    const float value = std::strtof(text.c_str(), &end);
    if (end != text.c_str() + text.size() || !std::isfinite(value)) throw std::runtime_error("Number is not a finite float: " + text);
    return value;
  }
  int64_t as_int() const {
    int64_t value = 0;
    if (kind == Boolean) return boolean ? 1 : 0;
    if (kind != Number) throw std::runtime_error("Expected a JSON integer");
    auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc() || end != text.data() + text.size()) throw std::runtime_error("Number is not an integer: " + text);
    return value;
  }
};

class JsonReader {
  const std::string &source;
  size_t position = 0;
  [[noreturn]] void fail(const char *message) const {
    throw std::runtime_error(std::string("JSON error at byte ") + std::to_string(position) + ": " + message);
  }
  void space() { while (position < source.size() && (source[position] == ' ' || source[position] == '\n' || source[position] == '\r' || source[position] == '\t')) ++position; }
  bool take(char c) { space(); if (position < source.size() && source[position] == c) { ++position; return true; } return false; }
  void expect(char c) { if (!take(c)) fail((std::string("expected '") + c + "'").c_str()); }
  bool literal(const char *word) {
    const std::string value(word);
    if (source.compare(position, value.size(), value) != 0) return false;
    position += value.size(); return true;
  }
  static void utf8(std::string &out, uint32_t code) {
    if (code < 0x80) out += char(code);
    else if (code < 0x800) { out += char(0xc0 | (code >> 6)); out += char(0x80 | (code & 0x3f)); }
    else if (code < 0x10000) { out += char(0xe0 | (code >> 12)); out += char(0x80 | ((code >> 6) & 0x3f)); out += char(0x80 | (code & 0x3f)); }
    else { out += char(0xf0 | (code >> 18)); out += char(0x80 | ((code >> 12) & 0x3f)); out += char(0x80 | ((code >> 6) & 0x3f)); out += char(0x80 | (code & 0x3f)); }
  }
  uint32_t hex4() {
    if (position + 4 > source.size()) fail("truncated \\u escape");
    uint32_t code = 0;
    for (int i = 0; i < 4; ++i) {
      char c = source[position++]; code <<= 4;
      if (c >= '0' && c <= '9') code |= uint32_t(c - '0');
      else if (c >= 'a' && c <= 'f') code |= uint32_t(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') code |= uint32_t(c - 'A' + 10);
      else fail("invalid \\u escape");
    }
    return code;
  }
  std::string string_value() {
    expect('"'); std::string out;
    while (true) {
      if (position >= source.size()) fail("unterminated string");
      char c = source[position++];
      if (c == '"') return out;
      if (static_cast<unsigned char>(c) < 0x20) fail("control character in string");
      if (c != '\\') { out += c; continue; }
      if (position >= source.size()) fail("unterminated escape");
      switch (char e = source[position++]) {
        case '"': case '\\': case '/': out += e; break;
        case 'b': out += '\b'; break; case 'f': out += '\f'; break;
        case 'n': out += '\n'; break; case 'r': out += '\r'; break; case 't': out += '\t'; break;
        case 'u': {
          uint32_t code = hex4();
          if (code >= 0xd800 && code < 0xdc00) {
            if (!literal("\\u")) fail("unpaired surrogate");
            uint32_t low = hex4(); if (low < 0xdc00 || low >= 0xe000) fail("invalid surrogate pair");
            code = 0x10000 + ((code - 0xd800) << 10) + (low - 0xdc00);
          } else if (code >= 0xdc00 && code < 0xe000) fail("unpaired surrogate");
          utf8(out, code); break;
        }
        default: fail("invalid escape");
      }
    }
  }
  std::string number_text() {
    size_t start = position;
    if (position < source.size() && source[position] == '-') ++position;
    auto digits = [&] { size_t first = position; while (position < source.size() && source[position] >= '0' && source[position] <= '9') ++position; return position > first; };
    if (position < source.size() && source[position] == '0') ++position; else if (!digits()) fail("invalid number");
    if (position < source.size() && source[position] == '.') { ++position; if (!digits()) fail("invalid fraction"); }
    if (position < source.size() && (source[position] == 'e' || source[position] == 'E')) {
      ++position; if (position < source.size() && (source[position] == '+' || source[position] == '-')) ++position;
      if (!digits()) fail("invalid exponent");
    }
    return source.substr(start, position - start);
  }
  Json value(int depth) {
    if (depth > 64) fail("nesting too deep");
    space(); Json out;
    if (position >= source.size()) fail("unexpected end");
    char c = source[position];
    if (c == '{') {
      ++position; out.kind = Json::Object;
      if (take('}')) return out;
      do {
        space(); auto key = string_value();
        if (out.find(key)) fail(("duplicate key " + key).c_str());
        expect(':'); out.fields.emplace_back(std::move(key), value(depth + 1));
      } while (take(','));
      expect('}');
    } else if (c == '[') {
      ++position; out.kind = Json::Array;
      if (take(']')) return out;
      do out.items.push_back(value(depth + 1)); while (take(','));
      expect(']');
    } else if (c == '"') { out.kind = Json::String; out.text = string_value(); }
    else if (literal("true")) { out.kind = Json::Boolean; out.boolean = true; }
    else if (literal("false")) { out.kind = Json::Boolean; }
    else if (literal("null")) {}
    else { out.kind = Json::Number; out.text = number_text(); }
    return out;
  }
 public:
  explicit JsonReader(const std::string &text) : source(text) {}
  Json document() {
    auto result = value(0); space();
    if (position != source.size()) fail("trailing content");
    return result;
  }
};

inline std::string json_quote(const std::string &value) {
  std::string out = "\"";
  for (unsigned char c : value) {
    if (c == '"' || c == '\\') { out += '\\'; out += char(c); }
    else if (c < 0x20) { char buffer[8]; std::snprintf(buffer, sizeof(buffer), "\\u%04x", c); out += buffer; }
    else out += char(c);
  }
  return out + "\"";
}

}  // namespace oracle
