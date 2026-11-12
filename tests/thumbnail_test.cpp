// Print panel file list: selecting a file shows its PNG thumbnail. LVGL
// decodes each PNG (ARGB8888: 300x300 -> 360 KB) and caches it; browsing
// must not keep the decoded thumbnails of files no longer shown.

#include "doctest/doctest.h"

#include "app.h"
#include "fake_moonraker.h"
#include "ui.h"

#include "guppyscreen.h"
#include "png_writer.h"

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
// A decoded thumbnail: ARGB8888 (smaller than PngDecoder::MIN_PIXELS).
constexpr size_t DECODED = THUMB * THUMB * 4;

std::string make_gcodes_dir() {
  const char *tmp = getenv("TMPDIR");
  std::string tmpl = std::string(tmp && *tmp ? tmp : "/tmp") + "/guppy-gcodes-XXXXXX";
  REQUIRE(mkdtemp(tmpl.data()) != NULL);
  mkdir((tmpl + "/.thumbs").c_str(), 0755);
  for (int i = 0; i < N_FILES; i++) {
    std::vector<unsigned char> px(THUMB * THUMB * 4);
    for (size_t p = 0; p < px.size(); p++) px[p] = (unsigned char)((p * (i + 3)) % 251);
    std::string path = tmpl + "/.thumbs/part" + std::to_string(i) + ".png";
    test_png::write(path, px, THUMB, THUMB);
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
