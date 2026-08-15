// Host-side stand-in for the Arduino core, for the preview build only.
//
// The renderer and model use Arduino's String, which is not std::string --
// it carries indexOf/toLowerCase/remove/startsWith and friends, and returns
// -1 rather than npos. This implements exactly the surface those two files
// touch, with Arduino's semantics, so the real sources compile unmodified.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <algorithm>
#include <cctype>

#define PROGMEM
#define pgm_read_word(addr) (*(const uint16_t *)(addr))
#define pgm_read_ptr(addr) (*(void *const *)(addr))
#define pgm_read_byte(addr) (*(const uint8_t *)(addr))

class String {
 public:
  String() = default;
  String(const char *s) : _s(s ? s : "") {}
  String(const std::string &s) : _s(s) {}
  String(char c) : _s(1, c) {}
  // Arduino offers a String(number) ctor for each integral width. They are
  // non-explicit there, and the renderer relies on that for `s += String(x)`.
  String(int v) { char b[24]; snprintf(b, sizeof b, "%d", v); _s = b; }
  String(unsigned v) { char b[24]; snprintf(b, sizeof b, "%u", v); _s = b; }
  String(long v) { char b[32]; snprintf(b, sizeof b, "%ld", v); _s = b; }
  String(unsigned long v) { char b[32]; snprintf(b, sizeof b, "%lu", v); _s = b; }
  String(long long v) { char b[32]; snprintf(b, sizeof b, "%lld", v); _s = b; }
  String(unsigned long long v) { char b[32]; snprintf(b, sizeof b, "%llu", v); _s = b; }
  // Arduino's fixed-decimal ctor. Used where a float has to go into a content
  // signature at a stable precision.
  String(double v, int dp = 2) { char b[40]; snprintf(b, sizeof b, "%.*f", dp, v); _s = b; }
  String(float v) { char b[32]; snprintf(b, sizeof b, "%.2f", (double)v); _s = b; }
  String(double v) { char b[32]; snprintf(b, sizeof b, "%.2f", v); _s = b; }

  void reserve(unsigned n) { _s.reserve(n); }

  const char *c_str() const { return _s.c_str(); }
  unsigned length() const { return (unsigned)_s.size(); }

  // Arduino returns -1 when absent, where std::string returns npos.
  int indexOf(const String &n) const {
    const size_t p = _s.find(n._s);
    return p == std::string::npos ? -1 : (int)p;
  }
  int indexOf(const char *n) const { return indexOf(String(n)); }

  void toLowerCase() {
    for (auto &c : _s) c = (char)std::tolower((unsigned char)c);
  }
  void toUpperCase() {
    for (auto &c : _s) c = (char)std::toupper((unsigned char)c);
  }

  // Arduino's remove(index) truncates from `index` to the end.
  void remove(unsigned index) {
    if (index < _s.size()) _s.erase(index);
  }
  void remove(unsigned index, unsigned count) {
    if (index < _s.size()) _s.erase(index, count);
  }

  // Arduino's substring is [begin, end), clamps `end` to the length, swaps the
  // two if they arrive backwards, and returns empty for a `begin` past the end
  // rather than throwing the way std::string::substr would. wrapText() in the
  // renderer relies on the clamping.
  String substring(unsigned begin, unsigned end) const {
    if (begin > end) std::swap(begin, end);
    if (begin >= _s.size()) return String();
    if (end > _s.size()) end = (unsigned)_s.size();
    return String(_s.substr(begin, end - begin));
  }
  String substring(unsigned begin) const { return substring(begin, (unsigned)_s.size()); }

  // In place, like Arduino's -- it returns void, not a trimmed copy.
  void trim() {
    size_t b = 0, e = _s.size();
    while (b < e && std::isspace((unsigned char)_s[b])) b++;
    while (e > b && std::isspace((unsigned char)_s[e - 1])) e--;
    _s = _s.substr(b, e - b);
  }

  bool startsWith(const String &p) const { return _s.rfind(p._s, 0) == 0; }
  bool endsWith(const String &p) const {
    return _s.size() >= p._s.size() &&
           _s.compare(_s.size() - p._s.size(), p._s.size(), p._s) == 0;
  }

  bool equalsIgnoreCase(const String &o) const {
    if (_s.size() != o._s.size()) return false;
    for (size_t i = 0; i < _s.size(); ++i)
      if (std::tolower((unsigned char)_s[i]) != std::tolower((unsigned char)o._s[i]))
        return false;
    return true;
  }

  // Exact overloads for char/const char*, so `out += ','` appends the
  // character rather than going through a numeric String ctor.
  String &operator+=(char c) { _s += c; return *this; }
  String &operator+=(const char *s) { _s += (s ? s : ""); return *this; }
  String &operator+=(const String &o) { _s += o._s; return *this; }
  friend String operator+(String a, const String &b) { a._s += b._s; return a; }
  bool operator==(const String &o) const { return _s == o._s; }
  bool operator!=(const String &o) const { return _s != o._s; }
  bool operator<(const String &o) const { return _s < o._s; }
  char operator[](unsigned i) const { return _s[i]; }

 private:
  std::string _s;
};

#define F(x) String(x)

// The renderer calls millis() to time the multi-target carousel. Previews are
// single-shot and must be reproducible, so this is a settable clock rather
// than a real one: render_preview.cpp pins it per scene.
inline uint32_t g_previewMillis = 0;
inline uint32_t millis() { return g_previewMillis; }

using std::max;
using std::min;
