#include "doctest/doctest.h"
#include "app.h"
#include "fake_moonraker.h"
#include "screenshot.h"
#include "ui.h"

#include "guppyscreen.h"

#include <cstdlib>
#include <mutex>
#include <string>

TEST_CASE("Screenshot: Home and System") {
  const char *dir = std::getenv("GUPPY_SCREENSHOT_DIR");
  if (!dir || !*dir) return;

  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker);
  app::wait_ready(moonraker);

  ui::tap_tab(Nav::HOME);
  app::pump(1100);
  app::redraw();
  screenshot::save(std::string(dir) + "/home.ppm");

  ui::tap_tab(Nav::SETTINGS);
  ui::tap_label("System");
  app::redraw();
  screenshot::save(std::string(dir) + "/system.ppm");

  lv_obj_t *label = ui::find_label("Clock");
  if (!label) return;  // В базовом main переключателя ещё нет.

  lv_obj_t *toggle = nullptr;
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    toggle = ui::find_in(lv_obj_get_parent(label), &lv_switch_class);
  }
  REQUIRE(toggle != nullptr);
  ui::tap(toggle);

  ui::tap_tab(Nav::HOME);
  app::redraw();
  screenshot::save(std::string(dir) + "/home-disabled.ppm");

  moonraker.push_status({{"print_stats", {{"filename", "cube.gcode"}, {"state", "printing"}}}});
  app::settle(moonraker);
  ui::tap_tab(Nav::PRINT);
  app::redraw();
  screenshot::save(std::string(dir) + "/print-disabled.ppm");
}
