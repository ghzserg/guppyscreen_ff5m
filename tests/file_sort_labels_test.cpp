#include "doctest/doctest.h"
#include "app.h"
#include "fake_moonraker.h"
#include "ui.h"

TEST_CASE("file browser sorting buttons retain icons and readable labels") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker);
  app::wait_ready(moonraker);
  ui::tap_tab(Nav::PRINT);
  CHECK(ui::find_label(LV_SYMBOL_REFRESH " Refresh") != nullptr);
  CHECK(ui::find_label(LV_SYMBOL_LIST " Changed") != nullptr);
  CHECK(ui::find_label(LV_SYMBOL_LIST " A-Z") != nullptr);
}
