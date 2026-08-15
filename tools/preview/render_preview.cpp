// ---------------------------------------------------------------------------
//  render_preview.cpp — rasterise the real HUD renderer to PNGs.
//
//  This links src/hud_renderer.cpp itself against tools/preview/tft_shim.h, so
//  what it draws is by construction what the firmware draws. It replaces
//  tools/preview_hud.py, which was a second, hand-maintained implementation of
//  the same layout and had drifted two commits out of date.
//
//  Build + run via tools/preview.sh, which writes preview_<scene>.png.
// ---------------------------------------------------------------------------
#include "tft_shim.h"

#include <cstdio>
#include <map>
#include <functional>

#include "../../src/hud_renderer.h"
#include "../../src/hd2_model.h"

// --- PNG output (zlib stored-block deflate, so there is nothing to link) ----

static uint32_t crcTable[256];
static void initCrc() {
  for (uint32_t n = 0; n < 256; ++n) {
    uint32_t c = n;
    for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
    crcTable[n] = c;
  }
}
static uint32_t crc32(const uint8_t *b, size_t n, uint32_t c = 0xFFFFFFFFu) {
  for (size_t i = 0; i < n; ++i) c = crcTable[(c ^ b[i]) & 0xFF] ^ (c >> 8);
  return c;
}
static void be32(std::vector<uint8_t> &v, uint32_t x) {
  v.push_back(x >> 24); v.push_back(x >> 16); v.push_back(x >> 8); v.push_back(x);
}
static void chunk(FILE *f, const char *tag, const std::vector<uint8_t> &data) {
  std::vector<uint8_t> len; be32(len, (uint32_t)data.size());
  fwrite(len.data(), 1, 4, f);
  std::vector<uint8_t> body(tag, tag + 4);
  body.insert(body.end(), data.begin(), data.end());
  fwrite(body.data(), 1, body.size(), f);
  std::vector<uint8_t> c; be32(c, crc32(body.data(), body.size()) ^ 0xFFFFFFFFu);
  fwrite(c.data(), 1, 4, f);
}

// RGB565 -> RGB888, then a nearest-neighbour upscale so the 480x320 panel is
// legible on a desktop screen at the same 2x the old preview used.
static void writePng(const char *path, const std::vector<uint16_t> &px, int w, int h,
                     int scale) {
  initCrc();
  const int W = w * scale, H = h * scale;
  std::vector<uint8_t> raw;
  raw.reserve(size_t(H) * (1 + W * 3));
  for (int y = 0; y < H; ++y) {
    raw.push_back(0);  // filter: none
    for (int x = 0; x < W; ++x) {
      const uint16_t p = px[size_t(y / scale) * w + (x / scale)];
      const uint8_t r = (p >> 11) & 0x1F, g = (p >> 5) & 0x3F, b = p & 0x1F;
      raw.push_back((r << 3) | (r >> 2));
      raw.push_back((g << 2) | (g >> 4));
      raw.push_back((b << 3) | (b >> 2));
    }
  }
  // zlib stream, stored deflate blocks.
  std::vector<uint8_t> z{0x78, 0x01};
  uint32_t a = 1, bsum = 0;
  for (uint8_t c : raw) { a = (a + c) % 65521; bsum = (bsum + a) % 65521; }
  for (size_t off = 0; off < raw.size();) {
    const size_t n = std::min<size_t>(65535, raw.size() - off);
    z.push_back(off + n >= raw.size() ? 1 : 0);
    z.push_back(n & 0xFF); z.push_back(n >> 8);
    z.push_back(~n & 0xFF); z.push_back((~n >> 8) & 0xFF);
    z.insert(z.end(), raw.begin() + off, raw.begin() + off + n);
    off += n;
  }
  be32(z, (bsum << 16) | a);

  FILE *f = fopen(path, "wb");
  const uint8_t sig[8] = {137, 'P', 'N', 'G', 13, 10, 26, 10};
  fwrite(sig, 1, 8, f);
  std::vector<uint8_t> ihdr;
  be32(ihdr, W); be32(ihdr, H);
  ihdr.push_back(8); ihdr.push_back(2); ihdr.push_back(0);
  ihdr.push_back(0); ihdr.push_back(0);
  chunk(f, "IHDR", ihdr);
  chunk(f, "IDAT", z);
  chunk(f, "IEND", {});
  fclose(f);
}

// --- scenes ----------------------------------------------------------------

static const time_t kNow = 1754060000;  // fixed, so previews are reproducible

// A defence in progress, matching the device's own DEFENSE screen.
static HudModel sceneDefense() {
  HudModel m;
  m.haveData = true; m.wifiUp = true; m.lastSuccess = kNow - 60;
  m.war.valid = true; m.war.totalKills = 393600000000ull;
  m.war.missionSuccessRate = 45; m.war.playerCount = 2;
  m.order.valid = true; m.order.id = 1;
  m.order.title = "DESERT OASIS";
  m.order.rewardAmount = 45;
  m.order.expiration = kNow + 12 * 3600 + 12 * 60;
  m.order.taskCount = 3;
  OrderTask &t = m.order.tasks[0];
  t.valid = true; t.taskType = kTaskTypeDefend; t.planetIndex = 64;
  t.planet.valid = true; t.planet.index = 64;
  t.planet.name = "FRONTERIA"; t.planet.sector = "UMLAUT";
  t.planet.biome = "DESERT"; t.planet.owner = "Humans";
  t.planet.liberation = 100.0f; t.planet.playerCount = 2;
  t.planet.observedAt = kNow - 60;
  m.order.tasks[1].valid = true; m.order.tasks[2].valid = true;
  return m;
}

// The same card with an active invasion, which is what lights the accent
// colour, the alert ribbon and the VICTORY IN: clock.
static HudModel sceneInvasion() {
  HudModel m = sceneDefense();
  OrderTask &t = m.order.tasks[0];
  t.planet.owner = "Humans";
  t.planet.event.active = true;
  t.planet.event.faction = "Automaton";
  t.planet.event.health = 380000; t.planet.event.maxHealth = 1000000;
  t.planet.event.endTime = kNow + 5 * 3600 + 41 * 60;
  t.planet.hazards[0] = kHazardTremor; t.planet.hazardCount = 1;
  return m;
}

static HudModel sceneLiberation() {
  HudModel m = sceneDefense();
  m.order.title = "STEEL VETERANS";
  OrderTask &t = m.order.tasks[0];
  t.taskType = kTaskTypeLiberate;
  t.planet.name = "YMIR"; t.planet.sector = "XZAR";
  t.planet.owner = "Automaton"; t.planet.liberation = 79.4f;
  t.planet.playerCount = 33400;
  return m;
}

static HudModel sceneIdle() {   // polled fine, but there is no Major Order
  HudModel m = sceneDefense();
  m.order = MajorOrder{};
  return m;
}

// No Major Order, but the campaigns feed found a live fight. This is what the
// idle screen is replaced by whenever that lookup succeeds. Only campaigns[0]
// is populated here — rendering campaigns[1..4] as additional preview scenes
// is out of scope; this exercises the single-campaign case (the pip row's
// `count > 1` gate off), which is what a real campaignCount == 1 poll draws.
static HudModel sceneCampaign() {
  HudModel m = sceneIdle();
  m.campaignCount = 1;
  PlanetInfo &p = m.campaigns[0];
  p.valid = true;
  p.index = 64;
  p.name = "GEMMA";
  p.sector = "Ursa";
  p.owner = "Terminids";
  p.biome = "Jungle";
  p.maxHealth = 1000000;
  p.health = 994405;      // 0.5595% liberated
  p.liberation = 0.5595f;
  p.playerCount = 4001;
  p.regenPerSecond = 2.78f;
  p.observedAt = 1785700000;
  // A previous sample an hour back, so the bar can quote a real %/h.
  RateSample &h = m.campaignHistory[0];
  h.valid = true;
  h.planetIndex = 64;
  h.at = 1785700000 - 3600;
  h.libPct = 0.5595f - 1.592f;
  return m;
}

// A count-style objective: progress measured against the task's own goal
// rather than a planet's health. Numbers are lifted verbatim from live order
// 3979642198 (2026-08-15), whose eradicate task sat at 277,438,986 of
// 1,250,000,000 on Senge 23 — the order that exposed the bug this scene
// exists to keep fixed. The planet resolves here, which is the healthy case.
static HudModel sceneCount() {
  HudModel m = sceneDefense();
  m.order.title = "VOID CORRIDOR";
  OrderTask &t = m.order.tasks[0];
  t.taskType = kTaskTypeEradicate;
  t.planetIndex = 280;
  t.progress = 277438986ull;
  t.goal = 1250000000ull;
  t.complete = false;
  t.planet.index = 280;
  t.planet.name = "SENGE 23"; t.planet.sector = "UNKNOWN";
  t.planet.biome = "HIGHLANDS"; t.planet.owner = "Terminids";
  t.planet.playerCount = 18240;
  // An earlier reading, an hour back, so the bar can quote a real %/h. This
  // rides on countPct/countAt rather than the planet-derived fields — the
  // whole point of keeping them separate is that a count objective's rate
  // survives a planet lookup the API cannot answer.
  RateSample &h = m.history[0];
  h.haveCount = true;
  h.countAt = kNow - 60 - 3600;
  h.countPct = 22.195f - 0.412f;
  return m;
}

// The same family of objective with its planet missing — /planets/{index}
// 404s for indices absent from the community API's static table, which is the
// normal state of a planet added to the war days before the table catches up.
// This is the exact state the reported bug was photographed in, and the card
// must still show real progress: no artwork and no stat strip, but a live bar,
// an honest percentage, and the planet index instead of "UNKNOWN".
static HudModel sceneExtraction() {
  HudModel m = sceneCount();
  OrderTask &t = m.order.tasks[0];
  t.taskType = kTaskTypeExtract;
  t.planetIndex = 278;
  t.progress = 7362236ull;
  t.goal = 35000000ull;
  t.planet = PlanetInfo{};  // the 404: no record at all, not a blank one
  RateSample &h = m.history[0];
  h.countPct = 21.035f - 0.503f;
  return m;
}

// A three-target order whose targets are not all the same kind: a liberation,
// then a count, then a defence. Rendered by shootAdvance() rather than shoot()
// — see main() — so what lands in the PNG is the *second* page reached by a
// carousel step, not a first page painted onto a cleared screen.
//
// The mixture is the point. Everything that has to follow the carousel has to
// visibly differ between page 0 and page 1: the pip row, the objective-type
// word, and the stat strip's column count (a liberation quotes PUSH/REGEN, a
// count has no such pair and quotes an ETA instead).
static HudModel sceneCarousel() {
  HudModel m = sceneLiberation();
  m.order.title = "STEEL VETERANS";
  m.order.taskCount = 3;

  OrderTask &count = m.order.tasks[1];
  count.valid = true; count.taskType = kTaskTypeEradicate;
  count.planetIndex = 280;
  count.progress = 277438986ull; count.goal = 1250000000ull;
  count.planet.valid = true; count.planet.index = 280;
  count.planet.name = "SENGE 23"; count.planet.sector = "MERIDIA";
  count.planet.biome = "HIGHLANDS"; count.planet.owner = "Terminids";
  count.planet.playerCount = 18240;
  count.planet.observedAt = kNow - 60;
  RateSample &h = m.history[1];
  h.haveCount = true;
  h.countAt = kNow - 60 - 3600;
  h.countPct = 22.195f - 0.412f;

  OrderTask &def = m.order.tasks[2];
  def.valid = true; def.taskType = kTaskTypeDefend; def.planetIndex = 64;
  return m;
}

static HudModel sceneStale() {  // link down, last good data still on screen
  HudModel m = sceneInvasion();
  m.stale = true; m.wifiUp = false; m.lastSuccess = kNow - 3600;
  return m;
}

// --- event overlays --------------------------------------------------------
//
// The three full-screen announcements. They ignore everything in the model
// except `overlay` and `overlaySubject`, so the underlying scene is only here
// to prove that: whatever is behind them, none of it should reach the panel.
//
// Briefing text is a real one in shape and length -- High Command writes long,
// and a preview using a short placeholder would not exercise the wrap or show
// where the four-line cap actually bites.
static MajorOrder overlaySubject() {
  MajorOrder o;
  o.valid = true;
  o.id = 3979642198;
  o.title = "OPERATION SWIFT DISASSEMBLY";
  o.briefing =
      "Automaton forces have massed in the Umlaut sector and are staging for a "
      "push on the Cyberstan corridor. High Command requires the immediate "
      "eradication of one billion two hundred and fifty million Terminids to "
      "deny them the flank. Deploy at once, Helldivers.";
  o.rewardAmount = 45;
  o.expiration = kNow + 3 * 24 * 3600;
  o.taskCount = 4;
  for (int i = 0; i < 4; i++) o.tasks[i].valid = true;
  return o;
}

static HudModel sceneNewOrder() {
  HudModel m = sceneDefense();
  m.overlay = kOverlayNewOrder;
  m.overlaySubject = overlaySubject();
  return m;
}

static HudModel sceneSuccess() {
  HudModel m = sceneDefense();
  m.overlay = kOverlaySuccess;
  m.overlaySubject = overlaySubject();
  for (int i = 0; i < m.overlaySubject.taskCount; i++)
    m.overlaySubject.tasks[i].complete = true;
  return m;
}

// A near miss rather than a rout: three of four met is the case where the
// objective count is worth printing at all, and the one most likely to be
// misread if the layout crowds it.
static HudModel sceneFailure() {
  HudModel m = sceneDefense();
  m.overlay = kOverlayFailure;
  m.overlaySubject = overlaySubject();
  for (int i = 0; i < 3; i++) m.overlaySubject.tasks[i].complete = true;
  return m;
}

int main(int argc, char **argv) {
  const int scale = 2;
  HUDRenderer hud;
  hud.begin();
  TFT_eSPI &tft = hud.canvas();

  std::map<String, std::function<HudModel()>> scenes{
      {"defense", sceneDefense},   {"invasion", sceneInvasion},
      {"liberation", sceneLiberation}, {"idle", sceneIdle},
      {"campaign", sceneCampaign},
      {"count", sceneCount},       {"extraction", sceneExtraction},
      {"stale", sceneStale},
      {"neworder", sceneNewOrder}, {"success", sceneSuccess},
      {"failure", sceneFailure},
  };

  auto shoot = [&](const String &name, const HudModel &m) {
    hud.invalidate();
    hud.update(m, kNow);
    const String out = "preview_" + name + ".png";
    writePng(out.c_str(), tft.pixels(), tft.width(), tft.height(), scale);
    printf("wrote %s\n", out.c_str());
  };

  // Every scene above is shot through invalidate(), i.e. a full repaint onto a
  // cleared screen — which is exactly the path a running device almost never
  // takes. The carousel advancing between two targets of one order repaints
  // the card and nothing else, and that is where the header row's pips were
  // found stuck: they were only ever painted by the full-body path.
  //
  // So this one deliberately does not invalidate. It paints page 0, advances
  // the way a swipe or the dwell timer would, and paints again — a frame the
  // renderer reached incrementally. The pip row and the objective-type word
  // beside it have to have moved between the two halves.
  auto shootAdvance = [&](const String &name, const HudModel &m) {
    hud.invalidate();
    hud.update(m, kNow);          // page 0, from scratch
    hud.advancePage(m, 1);
    hud.update(m, kNow);          // page 1, incrementally
    const String out = "preview_" + name + ".png";
    writePng(out.c_str(), tft.pixels(), tft.width(), tft.height(), scale);
    printf("wrote %s\n", out.c_str());
  };

  // The boot screen takes no model, so it is handled on its own.
  auto shootBoot = [&]() {
    hud.showBoot("Contacting High Command...");
    writePng("preview_boot.png", tft.pixels(), tft.width(), tft.height(), scale);
    printf("wrote preview_boot.png\n");
  };

  if (argc > 1) {
    const String want = argv[1];
    if (want == "boot") { shootBoot(); return 0; }
    if (want == "carousel") { shootAdvance(want, sceneCarousel()); return 0; }
    auto it = scenes.find(want);
    if (it == scenes.end()) { fprintf(stderr, "unknown scene: %s\n", argv[1]); return 1; }
    shoot(want, it->second());
    return 0;
  }

  shootBoot();
  for (auto &kv : scenes) shoot(kv.first, kv.second());
  shootAdvance("carousel", sceneCarousel());
  return 0;
}
