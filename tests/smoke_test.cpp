// Smoke tests: the app starts against a basic AD5M, shows live data and
// sends commands. Each TEST_CASE runs in its own process (tests/run_tests.sh).
// Test case names must not contain commas.

#include "doctest/doctest.h"

#include "app.h"
#include "fake_moonraker.h"
#include "ui.h"

#include <algorithm>

static bool contains(const std::vector<std::string> &v, const std::string &s) {
  return std::find(v.begin(), v.end(), s) != v.end();
}

TEST_CASE("startup shows heater temperatures from Klipper") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker);
  app::wait_ready(moonraker);

  // Fixture: extruder 31.2 / 0, bed 27.8 / 0. The UI shows whole degrees.
  auto extruder = ui::row_texts("Extruder");
  INFO("Extruder row: " << ui::join(extruder) << "; screen: " << ui::dump_screen());
  CHECK(contains(extruder, "31"));

  auto bed = ui::row_texts("Bed");
  INFO("Bed row: " << ui::join(bed));
  CHECK(contains(bed, "27"));
}

TEST_CASE("status update changes the shown temperature and target") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker);
  app::wait_ready(moonraker);

  moonraker.push_status({{"extruder", {{"temperature", 215.4}, {"target", 220.0}}}});

  bool updated = app::wait_until([] {
    auto row = ui::row_texts("Extruder");
    return contains(row, "215") && contains(row, "220");
  });
  CHECK_MESSAGE(updated, "Extruder row: " << ui::join(ui::row_texts("Extruder")));
}

TEST_CASE("homing panel sends G28") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker);
  app::wait_ready(moonraker);

  ui::tap_label("Homing");
  REQUIRE_MESSAGE(app::wait_until([] { return ui::find_label("G28") != nullptr; }),
                  "homing panel did not open; screen: " << ui::dump_screen());

  ui::tap_label("G28");
  bool sent = app::wait_until([&] { return contains(moonraker.gcodes(), "G28"); });
  CHECK_MESSAGE(sent, "G-codes sent: " << ui::join(moonraker.gcodes()));
}
