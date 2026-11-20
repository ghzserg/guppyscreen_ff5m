// Air circulation (zMod's AIR_CIRCULATION_* macros) on a printer with the
// fans they move, the AD5M Pro's internal and external fans and vent servo or
// the C5 Pro's chamber fans: three buttons in a column right of the fans.
// The calibration macros appear directly in Tune when available.

#include "doctest/doctest.h"

#include "app.h"
#include "fake_moonraker.h"
#include "ui.h"

#include "guppyscreen.h"

#include <fstream>
#include <mutex>

namespace {

const char *BUTTONS[] = {"Int", "Ext", "Stop"};  // short: a column at the right edge

// The AD5M Pro: Klipper has the vent servo and the two fans, zMod's config
// lists the Pro's fans.
void start_ad5m_pro(FakeMoonraker &moonraker) {
  json fixture = json::parse(std::ifstream(app::fixture("ad5m_basic.json")));
  json objects = fixture["printer.objects.list"];
  for (const char *o : {"servo my_servo", "fan_generic internal", "fan_generic external", "fan_generic chamber_fan",
                        "fan_generic fanM106"}) {
    objects["objects"].push_back(o);
  }
  moonraker.set_result("printer.objects.list", objects);
  json fan = {{"speed", 0.0}, {"rpm", nullptr}};
  json sub = fixture["printer.objects.subscribe"];
  sub["status"]["fan_generic internal"] = fan;
  sub["status"]["fan_generic external"] = fan;
  sub["status"]["fan_generic chamber_fan"] = fan;
  sub["status"]["fan_generic fanM106"] = fan;
  moonraker.set_result("printer.objects.subscribe", sub);
  app::start(moonraker, {{"printers", {{"FF5M", {{"fans", {
    {{"display_name", "Internal"}, {"id", "fan_generic internal"}},
    {{"display_name", "External"}, {"id", "fan_generic external"}},
    {{"display_name", "Chamber"}, {"id", "fan_generic chamber_fan"}},
    {{"display_name", "M106"}, {"id", "fan_generic fanM106"}}}}}}}}});
  app::wait_ready(moonraker);
}

void open_fans(FakeMoonraker &moonraker) {
  ui::tap_tab(Nav::HOME);
  ui::tap_label("Fans");
  app::settle(moonraker);
}

bool any_button_shown() {
  for (const char *b : BUTTONS) {
    if (ui::find_label(b) != nullptr) return true;
  }
  return false;
}

// On the open fan screen: the buttons in a column right of the row of `fan`,
// each sending its macro.
void check_column(FakeMoonraker &moonraker, const char *fan_name) {
  lv_obj_t *fan_row = ui::label_container(fan_name);
  REQUIRE_MESSAGE(fan_row != nullptr, fan_name << " not shown: " << ui::dump_screen());
  lv_area_t fan = ui::coords(fan_row);
  int left = -1, bottom = -1;
  for (const char *b : BUTTONS) {
    lv_obj_t *button = ui::label_container(b);
    REQUIRE_MESSAGE(button != nullptr, b << " not shown: " << ui::dump_screen());
    lv_area_t a = ui::coords(button);
    CHECK(a.x1 > fan.x2);  // right of the fans
    CHECK(a.x2 <= 799);
    if (left < 0) left = a.x1;
    CHECK(a.x1 == left);  // in one column
    CHECK(a.y1 > bottom);  // one under another
    bottom = a.y2;
  }

  const char *gcodes[] = {"AIR_CIRCULATION_INTERNAL", "AIR_CIRCULATION_EXTERNAL", "AIR_CIRCULATION_STOP"};
  for (int i = 0; i < 3; i++) {
    ui::tap_label(BUTTONS[i]);
    REQUIRE(app::wait_until([&] { return !moonraker.gcodes().empty() && moonraker.gcodes().back() == gcodes[i]; }));
  }
}

const char *CALIBRATIONS[] = {"Calibrate extruders", "Calibrate VFA"};

// The C5 Pro, with zMod's calibration macros if `calibrations` (their names
// in lower case if `lower_case`: Klipper keeps a macro's name as configured).
void start_c5pro(FakeMoonraker &moonraker, bool calibrations, bool lower_case = false) {
  if (calibrations) {
    json fixture = json::parse(std::ifstream(app::fixture("c5pro_basic.json")));
    json objects = fixture["printer.objects.list"];
    objects["objects"].push_back(lower_case ? "gcode_macro calibrate_extruders" : "gcode_macro CALIBRATE_EXTRUDERS");
    objects["objects"].push_back(lower_case ? "gcode_macro calibrate_vfa" : "gcode_macro CALIBRATE_VFA");
    moonraker.set_result("printer.objects.list", objects);
  }
  app::start(moonraker, app::zmod_sensors("guppyconfig_c5pro.json", "C5Pro"));
  app::wait_ready(moonraker);
}

void open_tune(FakeMoonraker &moonraker) {
  ui::tap_tab(Nav::TUNE);
  app::settle(moonraker);
}

}  // namespace

TEST_CASE("the AD5M Pro's fan screen has the air circulation buttons in a column right of the fans") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start_ad5m_pro(moonraker);
  open_fans(moonraker);
  check_column(moonraker, "Chamber");
}

TEST_CASE("the C5 Pro's fan screen has the air circulation buttons in a column right of the fans") {
  FakeMoonraker moonraker(app::fixture("c5pro_basic.json"));
  start_c5pro(moonraker, false);
  open_fans(moonraker);
  check_column(moonraker, "Chamber Fan");
}

// zMod's AD5M Pro config lists four fans.
TEST_CASE("the AD5M Pro's four fans fit the fan screen without scrolling") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start_ad5m_pro(moonraker);
  open_fans(moonraker);

  lv_obj_t *last = ui::label_container("External");  // the last row: M106, Chamber, then Klipper's order
  REQUIRE_MESSAGE(last != nullptr, "the last fan is not shown: " << ui::dump_screen());
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  lv_obj_update_layout(lv_scr_act());
  lv_area_t a;
  lv_obj_get_coords(last, &a);
  INFO("last row " << a.y1 << ".." << a.y2);
  CHECK(a.y2 < lv_obj_get_height(lv_scr_act()));
  CHECK_FALSE(lv_obj_has_flag(lv_obj_get_parent(last), LV_OBJ_FLAG_SCROLLABLE));  // no scrollbar
}

TEST_CASE("the AD5M Pro has no Pro button or unavailable calibrations") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start_ad5m_pro(moonraker);
  open_tune(moonraker);
  CHECK(ui::find_label("PID") != nullptr);
  CHECK(ui::find_label("Pro") == nullptr);
  for (const char *c : CALIBRATIONS) CHECK(ui::find_label(c) == nullptr);
}

TEST_CASE("the AD5M has no unsupported circulation buttons") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker);
  app::wait_ready(moonraker);
  open_fans(moonraker);
  CHECK_FALSE(any_button_shown());
  open_tune(moonraker);
  CHECK(ui::find_label("Pro") == nullptr);
  for (const char *c : CALIBRATIONS) CHECK(ui::find_label(c) == nullptr);
}

TEST_CASE("the C5 Pro without calibration macros hides their Tune buttons") {
  FakeMoonraker moonraker(app::fixture("c5pro_basic.json"));
  start_c5pro(moonraker, false);
  open_tune(moonraker);
  CHECK(ui::find_label("PID") != nullptr);
  CHECK(ui::find_label("Pro") == nullptr);
  for (const char *c : CALIBRATIONS) CHECK(ui::find_label(c) == nullptr);
}

TEST_CASE("the C5 Pro's Tune screen has both calibrations beside PID") {
  FakeMoonraker moonraker(app::fixture("c5pro_basic.json"));
  start_c5pro(moonraker, true);
  open_tune(moonraker);
  lv_obj_t *pid = ui::label_container("PID");
  lv_obj_t *extruders = ui::label_container(CALIBRATIONS[0]);
  lv_obj_t *vfa = ui::label_container(CALIBRATIONS[1]);
  REQUIRE(pid != nullptr);
  REQUIRE(extruders != nullptr);
  REQUIRE(vfa != nullptr);
  lv_area_t p = ui::coords(pid), e = ui::coords(extruders), v = ui::coords(vfa);
  CHECK(p.y1 == e.y1);
  CHECK(e.y1 == v.y1);
  CHECK(p.x1 < e.x1);
  CHECK(e.x1 < v.x1);
  CHECK(ui::find_label("Pro") == nullptr);

  ui::tap_label(CALIBRATIONS[0]);
  REQUIRE(app::wait_until([&] { return !moonraker.gcodes().empty() && moonraker.gcodes().back() == "CALIBRATE_EXTRUDERS"; }));
  ui::tap_label(CALIBRATIONS[1]);
  REQUIRE(app::wait_until([&] { return !moonraker.gcodes().empty() && moonraker.gcodes().back() == "CALIBRATE_VFA"; }));
}

TEST_CASE("the C5 Pro's Tune calibrations accept lower-case macro names") {
  FakeMoonraker moonraker(app::fixture("c5pro_basic.json"));
  start_c5pro(moonraker, true, true);
  open_tune(moonraker);
  CHECK_MESSAGE(ui::find_label(CALIBRATIONS[0]) != nullptr, ui::dump_screen());
  CHECK(ui::find_label(CALIBRATIONS[1]) != nullptr);
}

// zMod's C5 Pro config lists five fans: more than fit, the list scrolls.
TEST_CASE("the C5 Pro's fan rows do not overlap and scroll") {
  FakeMoonraker moonraker(app::fixture("c5pro_basic.json"));
  start_c5pro(moonraker, false);
  open_fans(moonraker);

  lv_obj_t *first = ui::label_container("Chamber Fan");
  REQUIRE_MESSAGE(first != nullptr, ui::dump_screen());
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  lv_obj_update_layout(lv_scr_act());
  lv_obj_t *list = lv_obj_get_parent(first);
  CHECK(lv_obj_get_child_cnt(list) > 4);
  lv_coord_t bottom = -10000;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(list); i++) {
    lv_area_t a;
    lv_obj_get_coords(lv_obj_get_child(list, i), &a);
    INFO("row " << i << ": " << a.y1 << ".." << a.y2);
    CHECK(a.y1 >= bottom);
    bottom = a.y2;
  }
  CHECK(lv_obj_has_flag(list, LV_OBJ_FLAG_SCROLLABLE));
}

namespace {

// The fan screen's row names, top to bottom.
std::vector<std::string> fan_rows(const char *first) {
  lv_obj_t *row = ui::label_container(first);
  REQUIRE_MESSAGE(row != nullptr, first << " not shown: " << ui::dump_screen());
  std::vector<std::string> names;
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  lv_obj_t *list = lv_obj_get_parent(row);
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(list); i++) {
    lv_obj_t *r = lv_obj_get_child(list, i);
    names.push_back(lv_label_get_text(lv_obj_get_child(r, 0)));  // the row's title
  }
  return names;
}

}  // namespace

// M106 (the part fan) first, then the chamber, the rest as Klipper lists them.
TEST_CASE("the fan screen lists M106 first then the chamber then Klipper's order") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start_ad5m_pro(moonraker);  // Klipper: internal, external, chamber_fan, fanM106
  open_fans(moonraker);
  CHECK(fan_rows("M106") == std::vector<std::string>{"M106", "Chamber", "Internal", "External"});
}
