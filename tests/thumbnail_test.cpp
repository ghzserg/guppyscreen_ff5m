// Print panel file list: selecting a file shows its PNG thumbnail. LVGL
// decodes each PNG to ARGB8888 (300x300 -> 360 KB) and caches it; browsing
// must not keep the decoded thumbnails of files no longer shown.

#include "doctest/doctest.h"

#include "app.h"
#include "fake_moonraker.h"
#include "ui.h"

#include "guppyscreen.h"

#include <algorithm>
#include <cstdio>
#include <functional>
#include <cstring>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <vector>

#ifdef __SANITIZE_ADDRESS__
extern "C" size_t __sanitizer_get_current_allocated_bytes(void);  // ASan runtime
#endif

namespace {

constexpr int THUMB = 300;
constexpr int N_FILES = 4;
constexpr size_t DECODED = THUMB * THUMB * 4;

// Minimal PNG writer (RGBA 8-bit, uncompressed deflate blocks): LVGL's
// lodepng is built without its encoder.
void write_png(const std::string &path, const std::vector<unsigned char> &rgba, uint32_t w, uint32_t h) {
  auto crc32 = [](const std::vector<unsigned char> &d) {
    uint32_t c = 0xffffffffu;
    for (unsigned char b : d) {
      c ^= b;
      for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1u)));
    }
    return c ^ 0xffffffffu;
  };
  auto be32 = [](std::vector<unsigned char> &v, uint32_t x) {
    for (int i = 3; i >= 0; i--) v.push_back((x >> (8 * i)) & 0xff);
  };
  std::vector<unsigned char> out = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
  auto chunk = [&](const char *type, const std::vector<unsigned char> &data) {
    be32(out, data.size());
    std::vector<unsigned char> td(type, type + 4);
    td.insert(td.end(), data.begin(), data.end());
    out.insert(out.end(), td.begin(), td.end());
    be32(out, crc32(td));
  };
  std::vector<unsigned char> ihdr;
  be32(ihdr, w);
  be32(ihdr, h);
  ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});
  chunk("IHDR", ihdr);

  std::vector<unsigned char> raw;
  for (uint32_t y = 0; y < h; y++) {
    raw.push_back(0);  // filter: none
    raw.insert(raw.end(), rgba.begin() + y * w * 4, rgba.begin() + (y + 1) * w * 4);
  }
  std::vector<unsigned char> z = {0x78, 0x01};
  for (size_t pos = 0; pos < raw.size() || pos == 0; pos += 65535) {
    size_t n = std::min<size_t>(65535, raw.size() - pos);
    z.push_back(pos + n >= raw.size() ? 1 : 0);
    z.insert(z.end(), {(unsigned char)(n & 0xff), (unsigned char)(n >> 8),
                       (unsigned char)(~n & 0xff), (unsigned char)((~n >> 8) & 0xff)});
    z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
    if (pos + n >= raw.size()) break;
  }
  uint32_t a = 1, b = 0;
  for (unsigned char c : raw) { a = (a + c) % 65521; b = (b + a) % 65521; }
  be32(z, (b << 16) | a);
  chunk("IDAT", z);
  chunk("IEND", {});

  FILE *f = fopen(path.c_str(), "wb");
  REQUIRE(f != NULL);
  fwrite(out.data(), 1, out.size(), f);
  fclose(f);
}

std::string make_gcodes_dir() {
  const char *tmp = getenv("TMPDIR");
  std::string tmpl = std::string(tmp && *tmp ? tmp : "/tmp") + "/guppy-gcodes-XXXXXX";
  REQUIRE(mkdtemp(tmpl.data()) != NULL);
  mkdir((tmpl + "/.thumbs").c_str(), 0755);
  for (int i = 0; i < N_FILES; i++) {
    std::vector<unsigned char> px(DECODED);
    for (size_t p = 0; p < px.size(); p++) px[p] = (unsigned char)((p * (i + 3)) % 251);
    std::string path = tmpl + "/.thumbs/part" + std::to_string(i) + ".png";
    write_png(path, px, THUMB, THUMB);
  }
  return tmpl;
}

// The file panel's image: the only lv_img whose source is a .png file.
std::string shown_thumbnail() {
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  std::string found;
  std::function<void(lv_obj_t *)> walk = [&](lv_obj_t *o) {
    if (lv_obj_check_type(o, &lv_img_class) && lv_img_src_get_type(lv_img_get_src(o)) == LV_IMG_SRC_FILE) {
      const char *src = (const char *)lv_img_get_src(o);
      if (strstr(src, ".png") && lv_obj_is_visible(o)) found = src;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(o); i++) walk(lv_obj_get_child(o, i));
  };
  walk(lv_scr_act());
  return found;
}

// Tap the file table row whose text ends with `name`.
void tap_file_row(const std::string &name) {
  lv_point_t p{};
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    lv_obj_t *table = nullptr;
    std::function<void(lv_obj_t *)> walk = [&](lv_obj_t *o) {
      if (lv_obj_check_type(o, &lv_table_class) && lv_obj_is_visible(o)) {
        for (uint16_t r = 0; r < lv_table_get_row_cnt(o); r++) {
          const char *v = lv_table_get_cell_value(o, r, 0);
          if (v && strstr(v, name.c_str())) table = o;
        }
      }
      for (uint32_t i = 0; i < lv_obj_get_child_cnt(o); i++) walk(lv_obj_get_child(o, i));
    };
    walk(lv_scr_act());
    REQUIRE_MESSAGE(table != nullptr, "no file row " << name);
    lv_obj_update_layout(table);
    lv_table_t *t = (lv_table_t *)table;
    lv_area_t a;
    lv_obj_get_content_coords(table, &a);
    lv_coord_t y = a.y1 - lv_obj_get_scroll_y(table);
    for (uint16_t r = 0; r < t->row_cnt; r++) {
      const char *v = lv_table_get_cell_value(table, r, 0);
      if (v && strstr(v, name.c_str())) {
        p = {(lv_coord_t)(a.x1 + 20), (lv_coord_t)(y + t->row_h[r] / 2)};
        break;
      }
      y += t->row_h[r];
    }
  }
  ui::tap_at(p.x, p.y);
}

}  // namespace

TEST_CASE("browsing files shows thumbnails without keeping old ones decoded") {
  std::string gcodes = make_gcodes_dir();

  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  moonraker.set_result("server.files.roots", json::array({
    {{"name", "config"}, {"path", "/opt/config"}, {"permissions", "rw"}},
    {{"name", "gcodes"}, {"path", gcodes}, {"permissions", "rw"}},
  }));
  json files = json::array();
  for (int i = 0; i < N_FILES; i++) {
    files.push_back({{"path", "part" + std::to_string(i) + ".gcode"}, {"modified", 1000 + i}, {"size", 100000}});
  }
  moonraker.set_result("server.files.list", files);
  moonraker.set_handler("server.files.metadata", [](const json &params) {
    std::string fname = params.value("filename", "");
    std::string stem = fname.substr(0, fname.find('.'));
    return json{{"filename", fname}, {"size", 100000}, {"modified", 1000}, {"estimated_time", 600},
                {"filament_weight_total", 5.0},
                {"thumbnails", json::array({{{"width", THUMB}, {"height", THUMB}, {"size", 1000},
                                             {"relative_path", ".thumbs/" + stem + ".png"}}})}};
  });

  app::start(moonraker);
  app::wait_ready(moonraker);

#ifdef __SANITIZE_ADDRESS__
  size_t heap0 = __sanitizer_get_current_allocated_bytes();
#endif

  ui::tap_label("Print");
  for (int i = N_FILES - 1; i >= 0; i--) {
    std::string name = "part" + std::to_string(i);
    tap_file_row(name + ".gcode");
    std::string want = "A:" + gcodes + "/.thumbs/" + name + ".png";
    bool shown = app::wait_until([&] { return shown_thumbnail() == want; });
    CHECK_MESSAGE(shown, "want " << want << ", shown: " << shown_thumbnail());
    app::pump(200);  // draw it, which decodes the PNG
  }

#ifdef __SANITIZE_ADDRESS__
  long growth = (long)__sanitizer_get_current_allocated_bytes() - (long)heap0;
  MESSAGE("live heap growth after browsing " << N_FILES << " files: " << growth << " bytes ("
          << (double)growth / DECODED << " decoded thumbnails)");
  // The thumbnail on screen stays decoded; earlier ones must not.
  CHECK(growth < (long)(2 * DECODED));
#endif

  // Closing the file list frees the last one too. Measured right after the
  // Back handler, before any redraw: drawing the main screen's icons would
  // evict the entry from the cache anyway on a PC, where decoding is fast
  // and gives the entry little "life", but not reliably on the printer.
  lv_obj_t *back = ui::find_label("Back");
  REQUIRE(back != nullptr);
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    lv_event_send(lv_obj_get_parent(back), LV_EVENT_SHORT_CLICKED, NULL);
#ifdef __SANITIZE_ADDRESS__
    growth = (long)__sanitizer_get_current_allocated_bytes() - (long)heap0;
    MESSAGE("live heap growth right after closing the file list: " << growth << " bytes");
    CHECK(growth < (long)(DECODED / 2));
#endif
  }
  CHECK(app::wait_until([] { return ui::find_label("Print") != nullptr; }));
}
