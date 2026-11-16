// The LED screen: a slider per light. The AD5M's light is Klipper's
// "led chamber_led" (zMod's LED_ON / LED_OFF macros set it).

#include "doctest/doctest.h"

#include "app.h"
#include "fake_moonraker.h"
#include "ui.h"

#include <fstream>

namespace {

void start_with_chamber_led(FakeMoonraker &moonraker) {
  json fixture = json::parse(std::ifstream(app::fixture("ad5m_basic.json")));
  json objects = fixture["printer.objects.list"];
  objects["objects"].push_back("led chamber_led");
  moonraker.set_result("printer.objects.list", objects);
  app::start(moonraker);
  app::wait_ready(moonraker);
}

}  // namespace

TEST_CASE("the LED screen shows a slider for the chamber light") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start_with_chamber_led(moonraker);
  ui::tap_label("LED");
  app::settle(moonraker);

  CHECK_MESSAGE(ui::find_shown(&lv_slider_class) != nullptr, ui::dump_screen());
}
