#include "doctest/doctest.h"
#include "app.h"
#include "ui.h"
#include "guppyscreen.h"
#include "theme.h"

#include <fstream>
#include <filesystem>

TEST_CASE("unknown or invalid theme falls back to blue") {
  CHECK(Theme::index(nullptr) == 0);
  CHECK(Theme::index(42) == 0);
  CHECK(Theme::index("unknown") == 0);
  CHECK(Theme::index("green") == 2);
}

TEST_CASE("built-in theme changes live and is saved without theme files") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker, {{"theme", "green"}});
  app::wait_ready(moonraker);
  CHECK_FALSE(std::filesystem::exists(app::dir() + "/themes/green.json"));

  auto *extruder = ui::tile("T0");
  ui::tap_label("T0");
  CHECK(ui::has_bg(extruder, LV_PALETTE_GREEN));
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    GuppyScreen::refresh_theme(lv_color_hex(Theme::colors[3].primary));
  }
  CHECK(ui::has_bg(extruder, LV_PALETTE_PURPLE));
  ui::enter_on_numpad("");
  CHECK_FALSE(ui::has_bg(extruder, LV_PALETTE_PURPLE));

  ui::tap_tab(Nav::SETTINGS);
  ui::tap_label("System");
  auto *label = ui::find_label("Theme");
  REQUIRE(label != nullptr);
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    auto *dd = ui::find_in(lv_obj_get_parent(label), &lv_dropdown_class);
    REQUIRE(dd != nullptr);
    CHECK(lv_dropdown_get_selected(dd) == 2);
    lv_dropdown_set_selected(dd, 4);
    lv_event_send(dd, LV_EVENT_VALUE_CHANGED, nullptr);
    CHECK(lv_color_to32(lv_theme_get_color_primary(dd)) == lv_color_to32(lv_palette_main(LV_PALETTE_PINK)));
  }
  std::ifstream config(app::dir() + "/guppyconfig.json");
  CHECK(json::parse(config).at("theme") == "pink");
  CHECK_FALSE(std::filesystem::exists(app::dir() + "/themes/pink.json"));
  ui::tap_tab(Nav::HOME);
  ui::tap_label("T0");
  CHECK(ui::has_bg(extruder, LV_PALETTE_PINK));
}
