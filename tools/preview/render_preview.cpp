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
// idle screen is replaced by whenever that lookup succeeds.
static HudModel sceneCampaign() {
  HudModel m = sceneIdle();
  m.campaign.valid = true;
  m.campaign.index = 64;
  m.campaign.name = "GEMMA";
  m.campaign.sector = "Ursa";
  m.campaign.owner = "Terminids";
  m.campaign.biome = "Jungle";
  m.campaign.maxHealth = 1000000;
  m.campaign.health = 994405;      // 0.5595% liberated
  m.campaign.liberation = 0.5595f;
  m.campaign.playerCount = 4001;
  m.campaign.regenPerSecond = 2.78f;
  m.campaign.observedAt = 1785700000;
  // A previous sample an hour back, so the bar can quote a real %/h.
  m.campaignHistory.valid = true;
  m.campaignHistory.planetIndex = 64;
  m.campaignHistory.at = 1785700000 - 3600;
  m.campaignHistory.libPct = 0.5595f - 1.592f;
  return m;
}

static HudModel sceneStale() {  // link down, last good data still on screen
  HudModel m = sceneInvasion();
  m.stale = true; m.wifiUp = false; m.lastSuccess = kNow - 3600;
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
      {"stale", sceneStale},
  };

  auto shoot = [&](const String &name, const HudModel &m) {
    hud.invalidate();
    hud.update(m, kNow);
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
    auto it = scenes.find(want);
    if (it == scenes.end()) { fprintf(stderr, "unknown scene: %s\n", argv[1]); return 1; }
    shoot(want, it->second());
    return 0;
  }

  shootBoot();
  for (auto &kv : scenes) shoot(kv.first, kv.second());
  return 0;
}
