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
// A decoded thumbnail: ARGB8888.
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

// Serves part0..part<N_FILES-1>.gcode, each with one THUMB-sized thumbnail.
void serve_files(FakeMoonraker &moonraker, const std::string &gcodes) {
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
}

// Zoom of every image showing a .png file (file list, print status, mini status).
std::vector<uint16_t> png_zooms() {
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  std::vector<uint16_t> zooms;
  std::function<void(lv_obj_t *)> walk = [&](lv_obj_t *o) {
    if (lv_obj_check_type(o, &lv_img_class) && lv_img_src_get_type(lv_img_get_src(o)) == LV_IMG_SRC_FILE
        && strstr((const char *)lv_img_get_src(o), ".png")) {
      zooms.push_back(lv_img_get_zoom(o));
    }
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(o); i++) walk(lv_obj_get_child(o, i));
  };
  walk(lv_scr_act());
  return zooms;
}

}  // namespace

TEST_CASE("browsing files shows thumbnails without keeping old ones decoded") {
  std::string gcodes = make_gcodes_dir();

  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  serve_files(moonraker, gcodes);

  app::start(moonraker);
  app::wait_ready(moonraker);

#ifdef __SANITIZE_ADDRESS__
  size_t heap0 = __sanitizer_get_current_allocated_bytes();
#endif

  ui::tap_tab(Nav::PRINT);
  for (int i = N_FILES - 1; i >= 0; i--) {
    std::string name = "part" + std::to_string(i);
    ui::tap_table_row(name + ".gcode");
    std::string want = "A:" + gcodes + "/.thumbs/" + name + ".png";
    bool shown = app::wait_until([&] { return shown_thumbnail() == want; });
    CHECK_MESSAGE(shown, "want " << want << ", shown: " << shown_thumbnail());
    app::redraw();  // draw it, which decodes the PNG
  }

#ifdef __SANITIZE_ADDRESS__
  long growth = (long)__sanitizer_get_current_allocated_bytes() - (long)heap0;
  MESSAGE("live heap growth after browsing " << N_FILES << " files: " << growth << " bytes ("
          << (double)growth / DECODED << " decoded thumbnails)");
  // The thumbnail on screen stays decoded; earlier ones must not.
  CHECK(growth < (long)(2 * DECODED));
#endif

  // Leaving the file list frees the last one too (its leave action).
  // Measured right after leaving, before any redraw: drawing the main
  // screen's icons would evict the entry from the cache anyway on a PC,
  // where decoding is fast and gives the entry little "life", but not
  // reliably on the printer.
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    Nav::get().activate(Nav::HOME);
#ifdef __SANITIZE_ADDRESS__
    growth = (long)__sanitizer_get_current_allocated_bytes() - (long)heap0;
    MESSAGE("live heap growth right after closing the file list: " << growth << " bytes");
    CHECK(growth < (long)(DECODED / 2));
#endif
  }
  CHECK(app::wait_until([] { return ui::find_label("Extrude") != nullptr; }));
}

// KUtils::get_thumbnail returned width 0 when the best thumbnail was the
// first listed (e.g. the only one), and the print status panel divided by it.
TEST_CASE("a print whose file has a single thumbnail shows it scaled") {
  std::string gcodes = make_gcodes_dir();
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  serve_files(moonraker, gcodes);
  app::start(moonraker);
  app::wait_ready(moonraker);

  moonraker.push_status({{"print_stats", {{"filename", "part0.gcode"}, {"state", "printing"}}}});
  REQUIRE(app::wait_until([&] { return moonraker.request_count("server.files.metadata") > 0; }));
  app::settle(moonraker);

  // Print status panel: 192 px wide, mini status on Home: 76 px, its whole
  // height above the progress bar.
  auto zooms = png_zooms();
  INFO("zooms: " << zooms.size());
  CHECK(std::find(zooms.begin(), zooms.end(), 192 * 256 / THUMB) != zooms.end());
  CHECK(std::find(zooms.begin(), zooms.end(), 76 * 256 / THUMB) != zooms.end());
}

TEST_CASE("a print whose thumbnail metadata says width 0 shows no thumbnail") {
  std::string gcodes = make_gcodes_dir();
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  serve_files(moonraker, gcodes);
  moonraker.set_handler("server.files.metadata", [](const json &params) {
    return json{{"filename", params.value("filename", "")}, {"size", 100000}, {"estimated_time", 600},
                {"thumbnails", json::array({{{"width", 0}, {"height", 0}, {"size", 1000},
                                             {"relative_path", ".thumbs/part0.png"}}})}};
  });
  app::start(moonraker);
  app::wait_ready(moonraker);

  moonraker.push_status({{"print_stats", {{"filename", "part0.gcode"}, {"state", "printing"}}}});
  REQUIRE(app::wait_until([&] { return moonraker.request_count("server.files.metadata") > 0; }));
  app::settle(moonraker);
  CHECK(png_zooms().empty());
}

TEST_CASE("a thumbnail with width 0 in the metadata doesn't hide the others") {
  std::string gcodes = make_gcodes_dir();
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  serve_files(moonraker, gcodes);
  // Width 0 is closer to the wanted 300 than 700 is.
  moonraker.set_handler("server.files.metadata", [](const json &params) {
    return json{{"filename", params.value("filename", "")}, {"size", 100000}, {"estimated_time", 600},
                {"thumbnails", json::array({
                  {{"width", 0}, {"height", 0}, {"size", 1000}, {"relative_path", ".thumbs/part0.png"}},
                  {{"width", 700}, {"height", 700}, {"size", 1000}, {"relative_path", ".thumbs/part0.png"}},
                })}};
  });
  app::start(moonraker);
  app::wait_ready(moonraker);

  moonraker.push_status({{"print_stats", {{"filename", "part0.gcode"}, {"state", "printing"}}}});
  REQUIRE(app::wait_until([&] { return moonraker.request_count("server.files.metadata") > 0; }));
  app::settle(moonraker);
  auto zooms = png_zooms();
  CHECK(std::find(zooms.begin(), zooms.end(), 192 * 256 / 700) != zooms.end());
}
