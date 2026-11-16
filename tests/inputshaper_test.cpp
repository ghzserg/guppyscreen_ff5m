// Input Shaper: the resonance graphs (800x480 PNGs, 1.5 MB each once decoded)
// are loaded only on request.

#include "doctest/doctest.h"

#include "app.h"
#include "fake_hw.h"
#include "fake_moonraker.h"
#include "png_writer.h"
#include "ui.h"

#include "guppyscreen.h"
#include "nav.h"

#include <functional>
#include <mutex>
#include <stdlib.h>
#include <sys/stat.h>

namespace {

// A config root with mod_data/resonances_X.png and _Y.png in it.
std::string config_root_with_graphs() {
  const char *tmp = getenv("TMPDIR");
  std::string dir = std::string(tmp && *tmp ? tmp : "/tmp") + "/guppy-config-XXXXXX";
  REQUIRE(mkdtemp(dir.data()) != NULL);
  mkdir((dir + "/mod_data").c_str(), 0755);
  std::vector<unsigned char> rgba(800 * 480 * 4, 200);  // as the printer's
  test_png::write(dir + "/mod_data/resonances_X.png", rgba, 800, 480);
  test_png::write(dir + "/mod_data/resonances_Y.png", rgba, 800, 480);
  return dir;
}

void start(FakeMoonraker &moonraker, const std::string &config_root) {
  if (!config_root.empty()) {
    moonraker.set_result("server.files.roots", json::array({
      {{"name", "config"}, {"path", config_root}, {"permissions", "rw"}},
      {{"name", "gcodes"}, {"path", "/data"}, {"permissions", "rw"}}}));
  }
  app::start(moonraker);
  app::wait_ready(moonraker);
}

// Images in the content area showing a file.
int file_images() {
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  int n = 0;
  ui::walk(Nav::get().content(), [&](lv_obj_t *o) {
    if (!lv_obj_check_type(o, &lv_img_class)) return;
    const void *src = lv_img_get_src(o);
    if (src != NULL && lv_img_src_get_type(src) == LV_IMG_SRC_FILE) n++;
  });
  return n;
}

void open_inputshaper() {
  ui::tap_tab(Nav::TUNE);
  ui::tap_label("Input Shaper");
  app::pump(100);
}

// The colour drawn just inside the top left corner of the graph box
// holding the first Load graphs button.
uint32_t graph_box_colour() {
  lv_obj_t *label = ui::find_label("Load graphs");
  REQUIRE(label != nullptr);
  app::pump(200);
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  lv_obj_t *box = lv_obj_get_parent(lv_obj_get_parent(label));
  lv_area_t a;
  lv_obj_get_coords(box, &a);
  lv_obj_invalidate(lv_scr_act());
  lv_refr_now(NULL);
  return lv_color_to32(fake_hw::pixel(a.x1 + 10, a.y1 + 10));
}

}  // namespace

TEST_CASE("Input Shaper graphs load only on Load graphs") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start(moonraker, config_root_with_graphs());
  CHECK(file_images() == 0);

  open_inputshaper();
  CHECK(file_images() == 0);
  REQUIRE_MESSAGE(ui::find_label("Load graphs") != nullptr, ui::dump_screen());
  ui::tap_label("Load graphs");
  CHECK(file_images() == 2);
  CHECK(ui::find_label("Load graphs") == nullptr);
}

TEST_CASE("leaving Input Shaper unloads the graphs") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start(moonraker, config_root_with_graphs());

  open_inputshaper();
  ui::tap_label("Load graphs");
  REQUIRE(file_images() == 2);
  ui::tap_tab(Nav::HOME);
  CHECK(file_images() == 0);

  open_inputshaper();
  CHECK(file_images() == 0);
  CHECK_MESSAGE(ui::find_label("Load graphs") != nullptr, ui::dump_screen());
}

TEST_CASE("Input Shaper without graph files has no Load graphs") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start(moonraker, "");

  open_inputshaper();
  CHECK_MESSAGE(ui::find_label("Load graphs") == nullptr, ui::dump_screen());
}

TEST_CASE("an unloaded graph leaves its box as it was") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start(moonraker, config_root_with_graphs());

  open_inputshaper();
  uint32_t before = graph_box_colour();
  ui::tap_label("Load graphs");
  ui::tap_tab(Nav::HOME);
  open_inputshaper();
  // Not LVGL's white "No data" box of an image without a source.
  CHECK(graph_box_colour() == before);
}
