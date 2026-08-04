#include "hd2_ota.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>

#include "config.h"

// ===========================================================================
//  Version comparison
// ===========================================================================

// Reads a leading "1.2.3" (optionally "v1.2.3") out of `s`. Anything after the
// patch number is ignored rather than rejected — see the contract in the
// header for which suffixes that has to cope with.
//
// `out` is written only on success. That matters: `git describe --always` on a
// repo with no tags yields a bare commit hash, and a hash that happens to
// start with digits ("60872d2") gets several components in before hitting the
// character that fails the parse. Leaving those behind would have the caller
// read that build as version 60872.0.0 — newer than every release that will
// ever be published, and permanently unupdatable.
static bool parseSemver(const char *s, uint32_t out[3]) {
  if (!s) return false;
  if (*s == 'v' || *s == 'V') s++;

  uint32_t v[3] = {0, 0, 0};
  for (int i = 0; i < 3; i++) {
    if (!isdigit((unsigned char)*s)) return false;
    uint32_t n = 0;
    while (isdigit((unsigned char)*s)) {
      // Clamp rather than wrap. A tag with a 12-digit component is nonsense,
      // but nonsense that silently overflows into a *small* number would read
      // as "older" and be invisible; clamped, it reads as "newer" and is at
      // worst a failed download.
      if (n < 100000) n = n * 10 + (uint32_t)(*s - '0');
      s++;
    }
    v[i] = n;
    if (i < 2) {
      if (*s != '.') return false;
      s++;
    }
  }

  out[0] = v[0];
  out[1] = v[1];
  out[2] = v[2];
  return true;
}

bool firmwareIsNewer(const char *candidate, const char *current) {
  uint32_t c[3];
  if (!parseSemver(candidate, c)) return false;

  // An unparseable local version sorts below everything. See the header. The
  // return value is deliberately ignored — parseSemver leaves `r` alone unless
  // it succeeds, so the 0.0.0 seed survives exactly that case.
  uint32_t r[3] = {0, 0, 0};
  parseSemver(current, r);

  for (int i = 0; i < 3; i++) {
    if (c[i] != r[i]) return c[i] > r[i];
  }
  return false;  // identical — not newer, so nothing to do.
}

// ===========================================================================
//  Release lookup
// ===========================================================================

bool HD2Ota::fetchLatestRelease(String &tag, String &assetUrl,
                                uint32_t &assetSize) {
  tag = "";
  assetUrl = "";
  assetSize = 0;

  // Stack-local for the same reason hd2_api.cpp keeps its client local: the
  // ~40KB of TLS working memory is released as soon as the request ends. This
  // never overlaps the API poll's connection — both run from loop(), one at a
  // time — so peak TLS usage is unchanged by adding this.
  WiFiClientSecure client;
  // Same trade as the HD2 API calls: no cert validation. The exposure here is
  // larger, though — a man-in-the-middle could serve an arbitrary image, not
  // just false Major Order data. See README "Security notes".
  client.setInsecure();
  client.setTimeout(kHttpTimeoutMs / 1000);

  HTTPClient http;
  http.setTimeout(kHttpTimeoutMs);
  http.setConnectTimeout(kHttpTimeoutMs);
  http.setReuse(false);

  const String url = "https://api.github.com/repos/" + String(HD2_OTA_REPO) +
                     "/releases/latest";
  if (!http.begin(client, url)) {
    _lastError = "begin() failed: " + url;
    return false;
  }

  // GitHub answers 403 to a request with no User-Agent. Sending the project's
  // own identifier also means this device is legible in their logs if it ever
  // misbehaves.
  http.setUserAgent(HD2_CLIENT_HEADER);
  http.addHeader("Accept", "application/vnd.github+json");
  http.addHeader("X-GitHub-Api-Version", "2022-11-28");

  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    // 404 is the normal answer for a repo that has no published release yet,
    // so this is not necessarily broken — the message says which it is.
    _lastError = (code == HTTP_CODE_NOT_FOUND)
                     ? String("no published release yet (404)")
                     : ("HTTP " + String(code) + " from releases API");
    http.end();
    return false;
  }

  const int len = http.getSize();
  if (len > (int)kOtaMaxReleaseJsonBytes) {
    _lastError = "release JSON is " + String(len) + " bytes; refusing to buffer";
    http.end();
    return false;
  }

  // Buffered rather than stream-parsed, matching HD2Api::getJson() — see the
  // note there about chunk framing leaking through getStream() on this core.
  const String body = http.getString();
  http.end();

  // The response carries the release notes, the author object, and a full
  // record per asset; the filter keeps all of that out of the document. Only
  // four fields are ever materialised.
  JsonDocument filter;
  filter["tag_name"] = true;
  filter["assets"][0]["name"] = true;
  filter["assets"][0]["size"] = true;
  filter["assets"][0]["browser_download_url"] = true;

  JsonDocument doc;
  const DeserializationError err =
      deserializeJson(doc, body, DeserializationOption::Filter(filter));
  if (err) {
    _lastError = String("JSON: ") + err.c_str();
    return false;
  }

  // /releases/latest excludes drafts and prereleases by definition, so a tag
  // pushed for a release candidate cannot reach a device unless it is
  // published as the latest full release. No filtering needed here.
  tag = doc["tag_name"] | "";
  if (tag.isEmpty()) {
    _lastError = F("release carried no tag_name");
    return false;
  }

  JsonArray assets = doc["assets"].as<JsonArray>();
  if (!assets.isNull()) {
    for (JsonVariant a : assets) {
      if (String(a["name"] | "") != kOtaAssetName) continue;
      assetUrl = a["browser_download_url"] | "";
      assetSize = a["size"] | 0U;
      break;
    }
  }
  return true;
}

// ===========================================================================
//  Check + apply
// ===========================================================================

void HD2Ota::begin() { _checkDueMs = millis() + kOtaFirstCheckDelayS * 1000UL; }

void HD2Ota::tick(bool wifiUp) {
  if ((int32_t)(millis() - _checkDueMs) < 0) return;

  // No network: return without consuming the interval, so the check happens as
  // soon as WiFi is back rather than up to an hour later. Silent by design —
  // the header already shows OFFLINE, and an outage would otherwise write a
  // line to the serial log five times a second.
  if (!wifiUp) return;

  // Charged before the work, not after, so that however long the check takes
  // (or however it fails) the next one is a clean interval away and this can
  // never turn into a tight retry loop against GitHub's rate limit.
  _checkDueMs = millis() + kOtaCheckIntervalS * 1000UL;

  String tag, assetUrl;
  uint32_t assetSize = 0;
  if (!fetchLatestRelease(tag, assetUrl, assetSize)) {
    Serial.printf("[ota] check failed: %s — retrying in %us\n",
                  _lastError.c_str(), (unsigned)kOtaCheckIntervalS);
    return;
  }

  if (!firmwareIsNewer(tag.c_str(), HD2_FW_VERSION)) {
    Serial.printf("[ota] running %s, latest release %s — up to date\n",
                  HD2_FW_VERSION, tag.c_str());
    return;
  }

  if (assetUrl.isEmpty()) {
    // A release exists but CI did not attach a binary to it, e.g. a tag pushed
    // by hand or a build that failed after the release was created. Nothing to
    // do but say so; the next successful build fixes it.
    Serial.printf("[ota] release %s has no %s asset — skipping\n", tag.c_str(),
                  kOtaAssetName);
    return;
  }

  // Pre-flight the size. HTTPUpdate makes this check itself and fails with
  // "Not Enough space", but only after downloading the headers — and the
  // interesting numbers (how much too big, against which slot) are in its
  // debug log, not its error string. Catching it here costs one comparison and
  // turns a recurring mystery failure into a line that says what to fix: the
  // partition table is out of room, see platformio.ini.
  const uint32_t freeSpace = ESP.getFreeSketchSpace();
  if (assetSize > 0 && assetSize > freeSpace) {
    Serial.printf(
        "[ota] release %s is %u bytes but the inactive app slot holds %u — "
        "the image has outgrown the partition table; skipping\n",
        tag.c_str(), (unsigned)assetSize, (unsigned)freeSpace);
    return;
  }

  Serial.printf("[ota] updating %s -> %s (%u bytes, %u free)\n", HD2_FW_VERSION,
                tag.c_str(), (unsigned)assetSize, (unsigned)freeSpace);
  if (_notice) _notice("Installing firmware update...");

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(kHttpTimeoutMs / 1000);

  // Not optional: browser_download_url answers 302 to a githubusercontent
  // host, and HTTPUpdate defaults to not following redirects, so without this
  // every update fails with "Wrong HTTP Code". STRICT is enough — the request
  // is a GET, which RFC 2616 lets a client redirect without asking.
  httpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  // Default, restated because it is the whole contract of the call below: on
  // success this reboots into the freshly written slot and does not return.
  // Nothing after the update() call runs in the success case.
  httpUpdate.rebootOnUpdate(true);

  const t_httpUpdate_return result = httpUpdate.update(client, assetUrl);
  switch (result) {
    case HTTP_UPDATE_FAILED:
      // Covers the whole failure surface — out of space, bad image header, a
      // truncated download, a 403 — behind one printable string. The old image
      // is untouched and still running; the boot pointer only moves once a
      // complete image has been written and verified.
      Serial.printf("[ota] update failed (%d): %s — staying on %s\n",
                    httpUpdate.getLastError(),
                    httpUpdate.getLastErrorString().c_str(), HD2_FW_VERSION);
      break;
    case HTTP_UPDATE_NO_UPDATES:
      // The server said there was nothing to send. Shouldn't happen given the
      // version check above, so it is worth a line rather than a silent pass.
      Serial.println(F("[ota] server reported no update available"));
      break;
    case HTTP_UPDATE_OK:
      // Unreachable with rebootOnUpdate(true): the reboot happens inside
      // update(). Handled so the enum is covered and so this is a visible
      // clue if that default is ever changed.
      Serial.println(F("[ota] update ok"));
      break;
  }

  // Reaching here means the panel is still showing whatever the notice
  // callback drew. Restoring the HUD is the caller's job — it owns the
  // renderer — and main.cpp does it by invalidating inside that callback.
}
