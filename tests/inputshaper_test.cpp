// Проверяем автоматическую загрузку графиков из корня config Moonraker.

#include "doctest/doctest.h"
#include "app.h"
#include "fake_moonraker.h"
#include "png_writer.h"
#include "ui.h"

#include "guppyscreen.h"
#include "nav.h"

#include <cstdlib>
#include <mutex>
#include <set>
#include <string>
#include <sys/stat.h>
#include <vector>

TEST_CASE("Input Shaper opens graphs automatically from Moonraker config root") {
  const char *tmp = getenv("TMPDIR");
  std::string root = std::string(tmp && *tmp ? tmp : "/tmp") + "/guppy-shaper-XXXXXX";
  REQUIRE(mkdtemp(root.data()) != NULL);
  REQUIRE(mkdir((root + "/mod_data").c_str(), 0755) == 0);
  std::vector<unsigned char> rgba(8 * 8 * 4, 200);
  test_png::write(root + "/mod_data/resonances_X.png", rgba, 8, 8);
  test_png::write(root + "/mod_data/resonances_Y.png", rgba, 8, 8);

  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  moonraker.set_result("server.files.roots", json::array({
      {{"name", "config"}, {"path", root}, {"permissions", "rw"}},
      {{"name", "gcodes"}, {"path", "/tmp"}, {"permissions", "rw"}},
  }));
  app::start(moonraker);
  app::wait_ready(moonraker);
  ui::tap_tab(Nav::TUNE);
  ui::tap_label("Input Shaper");

  std::set<std::string> sources;
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    ui::walk(Nav::get().content(), [&](lv_obj_t *o) {
      if (!lv_obj_check_type(o, &lv_img_class)) return;
      const void *src = lv_img_get_src(o);
      if (src && lv_img_src_get_type(src) == LV_IMG_SRC_FILE)
        sources.insert((const char *)src);
    });
  }
  CHECK(sources.count("A:" + root + "/mod_data/resonances_X.png") == 1);
  CHECK(sources.count("A:" + root + "/mod_data/resonances_Y.png") == 1);
}
