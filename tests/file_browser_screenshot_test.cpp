// Screenshots of the real LVGL file browser on the fake 800x480 display.
#include "doctest/doctest.h"
#include "app.h"
#include "fake_moonraker.h"
#include "fake_hw.h"
#include "png_writer.h"
#include "ui.h"
#include <cstdlib>
#include <vector>

TEST_CASE("file browser screenshot") {
  const char *out = std::getenv("GUPPY_FILES_SCREENSHOT");
  if (!out || !*out) return;

  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  moonraker.set_result("server.files.list", json::array({
    {{"path", "demo.gcode"}, {"modified", 1234567890}, {"size", 2000000}},
    {{"path", "test.gcode"}, {"modified", 1234567891}, {"size", 3000000}}
  }));
  moonraker.set_result("server.files.metadata", {
    {"modified", 1234567890}, {"size", 2000000}, {"estimated_time", 3600},
    {"filament_weight_total", 10}, {"thumbnails", json::array()}
  });
  app::start(moonraker);
  app::wait_ready(moonraker);
  ui::tap_tab(Nav::PRINT);
  REQUIRE(app::wait_until([] { return ui::table_has_row("demo.gcode"); }));
  ui::tap_table_row("demo.gcode");
  REQUIRE(app::wait_until([] { return ui::find_label("demo.gcode") != nullptr; }));
  app::settle(moonraker);
  app::redraw();

  std::vector<unsigned char> pixels;
  pixels.reserve(fake_hw::WIDTH * fake_hw::HEIGHT * 4);
  for (int y = 0; y < fake_hw::HEIGHT; ++y) {
    for (int x = 0; x < fake_hw::WIDTH; ++x) {
      uint32_t c = lv_color_to32(fake_hw::pixel(x, y));
      pixels.push_back((c >> 16) & 255);
      pixels.push_back((c >> 8) & 255);
      pixels.push_back(c & 255);
      pixels.push_back(255);
    }
  }
  test_png::write(out, pixels, fake_hw::WIDTH, fake_hw::HEIGHT);
}
