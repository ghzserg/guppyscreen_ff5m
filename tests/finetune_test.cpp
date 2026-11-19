// Fine Tune: when the printer reports a value changed, its frame turns yellow
// for a second, so the user sees which value their tap moved. A tap the
// printer hasn't answered yet lights nothing.

#include "doctest/doctest.h"

#include "app.h"
#include "fake_moonraker.h"
#include "fake_printer.h"
#include "ui.h"


namespace {

bool framed_yellow(lv_obj_t *value_label) {
  lv_obj_t *frame = lv_obj_get_parent(value_label);
  lv_color_t c = lv_obj_get_style_border_color(frame, LV_PART_MAIN);
  return c.full == lv_palette_main(LV_PALETTE_YELLOW).full;
}

FakePrinter::Options fast() {
  FakePrinter::Options o;
  o.tick_ms = 50;
  return o;
}

// Opens Fine Tune with the speed at 150% (the flow stays at 100%).
void open_fine_tune(FakeMoonraker &moonraker) {
  app::start(moonraker);
  app::wait_ready(moonraker);
  moonraker.push_status({{"gcode_move", {{"speed_factor", 1.5}}}});
  ui::tap_label("Fine Tune");
  REQUIRE_MESSAGE(app::wait_until([] { return ui::find_label("150%") != nullptr; }), ui::dump_screen());
  REQUIRE(ui::find_label("100%") != nullptr);
  // The 150% may have come as a change and lit the frame: wait it out.
  lv_obj_t *speed = ui::find_label("150%");
  REQUIRE(app::wait_until([&] { return !framed_yellow(speed); }, 3000));
}

}  // namespace

TEST_CASE("Fine Tune frames the value the printer changed in yellow for a second") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  FakePrinter printer(moonraker, {}, fast());
  open_fine_tune(moonraker);
  lv_obj_t *flow = ui::find_label("100%");

  ui::tap_label("Speed+");
  REQUIRE_MESSAGE(app::wait_until([] { return ui::find_label("151%") != nullptr; }), ui::dump_screen());
  lv_obj_t *speed = ui::find_label("151%");
  CHECK(framed_yellow(speed));
  CHECK_FALSE(framed_yellow(flow));

  CHECK(app::wait_until([&] { return !framed_yellow(speed); }, 3000));

  ui::tap_label("Z+");
  CHECK_MESSAGE(app::wait_until([] {
    lv_obj_t *z = ui::find_label("0.005", ui::Match::Contains);
    return z != nullptr && framed_yellow(z);
  }), ui::dump_screen());
}

TEST_CASE("Fine Tune lights nothing until the printer answers") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));  // takes G-code, reports no change
  open_fine_tune(moonraker);
  lv_obj_t *speed = ui::find_label("150%");

  ui::tap_label("Speed+");
  REQUIRE(app::wait_until([&] { return moonraker.gcodes().size() > 0; }));
  app::pump(300);
  CHECK_FALSE(framed_yellow(speed));

  moonraker.push_status({{"gcode_move", {{"speed_factor", 1.51}}}});
  CHECK(app::wait_until([&] { return framed_yellow(speed); }));
}
