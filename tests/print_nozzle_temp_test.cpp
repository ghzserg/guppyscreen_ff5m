// The print screen's nozzle temperature: "T<active tool>: temperature / target".
// The C5 Pro has a nozzle per tool (extruder, extruder1, ...); the AD5X has
// one nozzle (extruder) and T0-T3 are its filament channels.

#include "doctest/doctest.h"

#include "app.h"
#include "fake_moonraker.h"
#include "ui.h"

#include "guppyscreen.h"

#include <cstring>
#include <mutex>
#include <string>

namespace {

// The print screen's nozzle label ("T<n>: ..."), shown or not.
std::string nozzle_label() {
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  std::string text;
  ui::walk(lv_scr_act(), [&](lv_obj_t *o) {
    if (!text.empty() || !lv_obj_check_type(o, &lv_label_class)) return;
    const char *t = lv_label_get_text(o);
    if (t && t[0] == 'T' && t[1] >= '0' && t[1] <= '9' && t[2] == ':') text = t;
  });
  return text;
}

void switch_to_t2_and_heat(FakeMoonraker &moonraker, const char *heater) {
  moonraker.push_status({{"zmod_color", {{"active_tool_id", 2}}}});
  app::settle(moonraker);
  moonraker.push_status({{heater, {{"temperature", 230.0}, {"target", 230.0}}}});
  REQUIRE_MESSAGE(app::wait_until([] { return nozzle_label() == "T2: 230 / 230"; }), "label: " << nozzle_label());
}

}  // namespace

TEST_CASE("the AD5X print screen shows the active channel with its one nozzle's temperature") {
  FakeMoonraker moonraker(app::fixture("ad5x_basic.json"));
  app::start(moonraker, app::zmod_sensors("guppyconfig_ad5x.json", "AD5X"));
  app::wait_ready(moonraker);
  switch_to_t2_and_heat(moonraker, "extruder");
}

TEST_CASE("the C5 Pro print screen shows the active nozzle's temperature") {
  FakeMoonraker moonraker(app::fixture("c5pro_basic.json"));
  app::start(moonraker, app::zmod_sensors("guppyconfig_c5pro.json", "C5Pro"));
  app::wait_ready(moonraker);
  switch_to_t2_and_heat(moonraker, "extruder2");

  moonraker.push_status({{"extruder", {{"temperature", 180.0}, {"target", 180.0}}}});  // T0's: not shown
  app::settle(moonraker);
  CHECK(nozzle_label() == "T2: 230 / 230");
}
