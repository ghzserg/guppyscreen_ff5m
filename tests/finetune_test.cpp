// Fine Tune: when the printer reports a value changed, its frame turns yellow
// for a second, so the user sees which value their tap moved. A tap the
// printer hasn't answered yet lights nothing.

#include "doctest/doctest.h"

#include "app.h"
#include "fake_moonraker.h"
#include "fake_printer.h"
#include "ui.h"

#include "config.h"
#include "guppyscreen.h"
#include "nav.h"

#include <mutex>

namespace {

bool framed_yellow(lv_obj_t *value_label) {
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
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

LV_IMG_DECLARE(zoffset_thicker_img);
LV_IMG_DECLARE(zoffset_thinner_img);

namespace {
// Иконка кнопки с подписью `text`.
const void *icon_of(const char *text) {
  lv_obj_t *btn = ui::label_container(text);
  REQUIRE_MESSAGE(btn != nullptr, text << " not shown: " << ui::dump_screen());
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  lv_obj_t *img = ui::find_in(btn, &lv_imgbtn_class);
  REQUIRE(img != nullptr);
  return lv_imgbtn_get_src_middle(img, LV_IMGBTN_STATE_RELEASED);
}
}  // namespace

TEST_CASE("Fine Tune shows Z+ as a thicker first layer and Z- as a thinner one") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker);
  app::wait_ready(moonraker);

  ui::tap_label("Fine Tune");
  CHECK(icon_of("Z+") == &zoffset_thicker_img);
  CHECK(icon_of("Z-") == &zoffset_thinner_img);

  // invert_z_icon переставлял стрелки; толщина слоя от него не зависит.
  Config::get_instance()->set<bool>("/invert_z_icon", true);
  ui::tap_tab(Nav::HOME);
  ui::tap_label("Fine Tune");
  CHECK(icon_of("Z+") == &zoffset_thicker_img);
  CHECK(icon_of("Z-") == &zoffset_thinner_img);
}

TEST_CASE("Fine Tune says Multiplier Step and Z Reset and PA Reset") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker);
  app::wait_ready(moonraker);

  ui::tap_label("Fine Tune");
  CHECK_MESSAGE(ui::find_label("Multiplier Step") != nullptr, ui::dump_screen());
  CHECK(ui::find_label("Z Reset") != nullptr);
  CHECK(ui::find_label("PA Reset") != nullptr);
  CHECK(ui::find_label("Speed Reset") != nullptr);
  CHECK(ui::find_label("Multipler Step") == nullptr);
}
