// A Moonraker message larger than 64 kB (on zMod the printer.objects.subscribe
// reply is ~500 kB, mostly configfile) must not break the messages after it.
// guppyscreen trims malloc and libhv drops its receive buffer after such a
// message.

#include "doctest/doctest.h"

#include "app.h"
#include "fake_moonraker.h"
#include "ui.h"

#include <algorithm>
#include <fstream>

namespace {

bool contains(const std::vector<std::string> &v, const std::string &s) {
  return std::find(v.begin(), v.end(), s) != v.end();
}

}  // namespace

TEST_CASE("large subscribe reply is followed by live updates") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));

  std::ifstream f(app::fixture("ad5m_basic.json"));
  json sub = json::parse(f)["printer.objects.subscribe"];
  // 300 macros of ~1 kB each, like a zMod config.
  json &config = sub["status"]["configfile"]["config"];
  for (int i = 0; i < 300; i++) {
    config["gcode_macro BIG_" + std::to_string(i)] = {{"gcode", std::string(1000, 'G')}};
  }
  REQUIRE(sub.dump().size() > 300 * 1000);
  moonraker.set_result("printer.objects.subscribe", sub);

  app::start(moonraker);
  app::wait_ready(moonraker);

  auto extruder = ui::row_texts("Extruder");
  CHECK_MESSAGE(contains(extruder, "31"), "Extruder row: " << ui::join(extruder));

  // Small messages after the big one, then another big one.
  for (int i = 0; i < 3; i++) {
    double t = 200.0 + i;
    moonraker.push_status({{"extruder", {{"temperature", t}}}});
    std::string want = std::to_string((int)t);
    bool updated = app::wait_until([&] { return contains(ui::row_texts("Extruder"), want); });
    CHECK_MESSAGE(updated, "want " << want << "; Extruder row: " << ui::join(ui::row_texts("Extruder")));
  }

  moonraker.push_status({{"configfile", sub["status"]["configfile"]}, {"heater_bed", {{"temperature", 55.0}}}});
  bool bed = app::wait_until([] { return contains(ui::row_texts("Bed"), "55"); });
  CHECK_MESSAGE(bed, "Bed row: " << ui::join(ui::row_texts("Bed")));

  moonraker.push_status({{"extruder", {{"temperature", 210.0}}}});
  bool after = app::wait_until([] { return contains(ui::row_texts("Extruder"), "210"); });
  CHECK_MESSAGE(after, "Extruder row: " << ui::join(ui::row_texts("Extruder")));
}
