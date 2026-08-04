// ---------------------------------------------------------------------------
//  hd2_ota.h — over-the-air firmware updates from GitHub Releases.
//
//  Owns the update check and the flash, nothing else. It never touches the
//  display or the HD2 model; the one thing it hands back to the caller is a
//  "I am about to block for a while" notice, so the HUD can say so.
//
//  Why GitHub Releases rather than a box on the LAN: the device lives on
//  someone's desk, and the thing that publishes firmware is a CI job. Pointing
//  it at a public release feed means an update needs no server at home, no
//  port forwarding, and works from any network the device is plugged into.
//
//    https://api.github.com/repos/<owner>/<repo>/releases/latest
//
//  Unauthenticated: the repo is public, and an unauthenticated caller gets 60
//  requests an hour against api.github.com, which is sixty times what this
//  needs. Shipping a token in a public firmware image would be worse than
//  pointless.
// ---------------------------------------------------------------------------
#pragma once

#include <Arduino.h>

// The repository to pull releases from, "owner/name". A fork has to change
// this or it will keep updating itself back to this project's builds.
// Guarded so platformio.ini wins, matching how config.h handles its knobs.
#ifndef HD2_OTA_REPO
#define HD2_OTA_REPO "codydoerfler/hd2-desk-monitor"
#endif

// How often to ask GitHub whether there is something newer.
//
// Deliberately twelve times slower than the HD2 API poll. Nothing about this
// device is time-critical about firmware freshness — a Major Order display
// that picks up a fix within the hour is doing fine — and each check costs a
// TLS handshake plus a few KB against a rate limit shared with every other
// device behind the same address. Below ~10 minutes there is no benefit left
// to buy, only heat.
static const uint32_t kOtaCheckIntervalS = 3600;  // 1 hour

// Delay from begin() to the first check. Boot is the busiest the device ever
// is — captive portal, NTP, first poll, first full repaint — and an immediate
// ~1.9MB download on top of that would stall the HUD before it has drawn
// anything. One minute is enough for the first poll to land.
static const uint32_t kOtaFirstCheckDelayS = 60;

// A release JSON document larger than this is treated as a failed check. The
// response is buffered whole before parsing, so without a bound this is an
// unbounded heap allocation on a part with no PSRAM.
//
// Sized off measurement, not a guess: a release of this project runs a few KB
// (one asset, generated notes), while the largest thing on github.com that
// was to hand — a cli/cli release, 22 assets and 11KB of notes — came to
// 50KB. 64KB clears that with room to spare and is still under a quarter of
// the free heap. The cost of tripping it is that OTA stops working until the
// release notes get shorter, which is a silly way to brick updates, so the
// bound is deliberately generous rather than tight.
static const size_t kOtaMaxReleaseJsonBytes = 64 * 1024;

// The release asset the device looks for. CI publishes exactly this name.
static const char *kOtaAssetName = "firmware.bin";

class HD2Ota {
 public:
  // Invoked immediately before the device blocks to download and flash. The
  // callback exists because that block is long (a couple of minutes on a slow
  // link) and otherwise indistinguishable from a hang: the caller uses it to
  // put a line on screen. It is a plain function pointer, not std::function,
  // to keep this off the heap.
  using Notice = void (*)(const char *message);
  void onUpdateStarting(Notice cb) { _notice = cb; }

  // Starts the check clock. Call once, at the end of setup().
  void begin();

  // Call every loop() iteration. Returns immediately unless a check is due,
  // so it is safe to call at the loop's full rate. `wifiUp` is passed in
  // rather than read here so the caller's single WiFi.status() read per
  // iteration is the only one.
  //
  // Returns only if no update was applied — a successful flash reboots into
  // the new image from inside this call and never comes back.
  void tick(bool wifiUp);

  // Human-readable reason for the most recent failure (for the serial log).
  const String &lastError() const { return _lastError; }

 private:
  // One GET against the releases API. Fills `tag` with the release's tag and,
  // if the release carries a kOtaAssetName asset, `assetUrl`/`assetSize` with
  // that asset's download URL and byte count. Returns false on transport
  // error, non-200, or malformed JSON.
  bool fetchLatestRelease(String &tag, String &assetUrl, uint32_t &assetSize);

  Notice _notice = nullptr;
  uint32_t _checkDueMs = 0;
  String _lastError;
};

// True if release tag `candidate` describes a firmware newer than `current`.
//
// Both are read as a leading MAJOR.MINOR.PATCH, tolerating a "v" prefix and
// ignoring anything after the patch number, so "v1.2.3", "1.2.3-rc1" and the
// "v1.2.3-4-gabc1234" that `git describe` produces off-tag all compare as
// 1.2.3. That last case is the useful one: a local build four commits past
// v1.2.3 will not be replaced by the v1.2.3 release (not strictly newer) but
// will accept v1.3.0.
//
// The two sides are treated asymmetrically, on purpose:
//   - `candidate` must parse. A tag that isn't a version number is never a
//     reason to overwrite a working image.
//   - `current` that doesn't parse is read as 0.0.0, so it loses to any real
//     release. That is the state of a build made before this repo had any
//     tags, where `git describe --always` yields a bare commit hash: there is
//     nothing to compare, and adopting a published release is the better of
//     the two guesses.
//
// Exposed at file scope so it can be exercised on the host.
bool firmwareIsNewer(const char *candidate, const char *current);
