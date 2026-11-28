// Home's temperatures as tiles.

#include "doctest/doctest.h"

#include "app.h"
#include "fake_moonraker.h"
#include "fake_hw.h"
#include "ui.h"

#include "guppyscreen.h"
#include "sensor_container.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <mutex>

namespace {

// The shown label of the tile named `name` whose text is `text`.
lv_obj_t *tile_label(const std::string &name, const std::string &text) {
  lv_obj_t *name_label = ui::find_label(name);
  REQUIRE_MESSAGE(name_label != nullptr, name << " not shown: " << ui::dump_screen());
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  lv_obj_t *tile = lv_obj_get_parent(name_label);
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(tile); i++) {
    lv_obj_t *c = lv_obj_get_child(tile, i);
    if (c != name_label && lv_obj_check_type(c, &lv_label_class) && !lv_obj_has_flag(c, LV_OBJ_FLAG_HIDDEN)
        && text == lv_label_get_text(c)) {
      return c;
    }
  }
  return nullptr;
}

bool has_colour(lv_obj_t *label, lv_palette_t palette) {
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  return lv_color_to32(lv_obj_get_style_text_color(label, 0)) == lv_color_to32(lv_palette_main(palette));
}

}  // namespace

TEST_CASE("a sensor tile shows a set target in red and an unset one in grey") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker);
  app::wait_ready(moonraker);
  moonraker.push_status({{"extruder", {{"target", 200}}}});
  REQUIRE(app::wait_until([] { return tile_label("Extruder", "200") != nullptr; }));

  CHECK(has_colour(tile_label("Extruder", "200"), LV_PALETTE_RED));
  lv_obj_t *bed_target = tile_label("Bed", "0");
  REQUIRE(bed_target != nullptr);
  CHECK(has_colour(bed_target, LV_PALETTE_GREY));

  moonraker.push_status({{"extruder", {{"target", 0}}}});
  REQUIRE(app::wait_until([] { return tile_label("Extruder", "200") == nullptr; }));
  CHECK(has_colour(tile_label("Extruder", "0"), LV_PALETTE_GREY));
}

TEST_CASE("a tap on a C5 Pro tile opens the numpad for that nozzle") {
  FakeMoonraker moonraker(app::fixture("c5pro_basic.json"));
  app::start(moonraker, app::zmod_sensors("guppyconfig_c5pro.json", "C5Pro"));
  app::wait_ready(moonraker);

  ui::tap_label("T2");
  ui::enter_on_numpad("210");
  REQUIRE(app::wait_until([&] { return !moonraker.gcodes().empty(); }));
  CHECK(moonraker.gcodes().back() == "SET_HEATER_TEMPERATURE HEATER=extruder2 TARGET=210");
}

LV_IMG_DECLARE(thermometer_img);

// Lit pixels in the area of an icon object: its bounding box on screen.
struct Lit { int x1 = 1 << 20, y1 = 1 << 20, x2 = -1, y2 = -1; };
Lit lit_in(lv_obj_t *img) {
  lv_area_t a;
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    lv_obj_get_coords(img, &a);
  }
  app::redraw();
  Lit l;
  for (int y = a.y1; y <= a.y2; y++) {
    for (int x = a.x1; x <= a.x2; x++) {
      uint32_t c = lv_color_to32(fake_hw::pixel(x, y));
      if ((c & 0xff) > 0x80 && ((c >> 8) & 0xff) > 0x80 && ((c >> 16) & 0xff) > 0x80) {  // the white icon, not the card
        l.x1 = std::min(l.x1, x); l.y1 = std::min(l.y1, y);
        l.x2 = std::max(l.x2, x); l.y2 = std::max(l.y2, y);
      }
    }
  }
  return l;
}

TEST_CASE("the head sensor tile shows a thermometer as big as the other icons") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker, {{"printers", {{"FF5M", {{"monitored_sensors", {
    {{"id", "extruder"}, {"display_name", "Extruder"}, {"controllable", true}, {"color", "red"}},
    {{"id", "temperature_sensor head"}, {"display_name", "Head"}, {"controllable", false}, {"color", "green"}}}}}}}}});
  app::wait_ready(moonraker);

  lv_obj_t *head = ui::find_label("Head");
  lv_obj_t *extruder = ui::find_label("Extruder");
  REQUIRE(head != nullptr);
  REQUIRE(extruder != nullptr);
  lv_obj_t *head_img, *extruder_img;
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    head_img = lv_obj_get_child(lv_obj_get_parent(head), 0);
    extruder_img = lv_obj_get_child(lv_obj_get_parent(extruder), 0);
    CHECK(lv_img_get_src(head_img) == &thermometer_img);
  }
  // Drawn, and about as tall as the extruder icon.
  Lit h = lit_in(head_img), e = lit_in(extruder_img);
  INFO("head " << h.x1 << "," << h.y1 << "-" << h.x2 << "," << h.y2
       << " extruder " << e.x1 << "," << e.y1 << "-" << e.x2 << "," << e.y2);
  CHECK(e.y2 > e.y1);
  CHECK(h.y2 > h.y1);
  CHECK(std::abs((h.y2 - h.y1) - (e.y2 - e.y1)) <= 6);
}

LV_FONT_DECLARE(notosemi_20);
LV_FONT_DECLARE(notobold_32_numpad);

TEST_CASE("the temperature numpad shows the value as big as the tiles and big bold keys") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker);
  app::wait_ready(moonraker);
  lv_obj_t *tile_value = tile_label("Extruder", "31");  // the fixture's extruder temperature
  REQUIRE(tile_value != nullptr);

  ui::tap_label("Extruder");
  lv_obj_t *input = ui::find_shown(&lv_textarea_class);
  REQUIRE_MESSAGE(input != nullptr, "numpad did not open");
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  CHECK(lv_obj_get_style_text_font(input, LV_PART_MAIN) == lv_obj_get_style_text_font(tile_value, LV_PART_MAIN));
  lv_obj_t *kb = lv_obj_get_child(lv_obj_get_parent(input), 1);
  REQUIRE(lv_obj_check_type(kb, &lv_keyboard_class));
  CHECK(lv_obj_get_style_text_font(kb, LV_PART_ITEMS) == &notobold_32_numpad);  // twice the theme's 16 px, bold
}

TEST_CASE("a sensor tile shows the target above the current temperature") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker);
  app::wait_ready(moonraker);
  moonraker.push_status({{"extruder", {{"target", 200}}}});
  REQUIRE(app::wait_until([] { return tile_label("Extruder", "200") != nullptr; }));

  lv_obj_t *target = tile_label("Extruder", "200");
  lv_obj_t *current = tile_label("Extruder", "31");  // the fixture's extruder temperature
  REQUIRE(current != nullptr);
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  lv_area_t t, c;
  lv_obj_get_coords(target, &t);
  lv_obj_get_coords(current, &c);
  CHECK((t.y1 + t.y2) / 2 < c.y1);  // the line boxes overlap by their empty margins
  CHECK(lv_obj_get_style_text_font(target, LV_PART_MAIN) == SensorContainer::temperature_font());
  CHECK(lv_obj_get_style_text_font(current, LV_PART_MAIN) == SensorContainer::temperature_font());
}

namespace {

// The colour of the stripe under the tile named `name` (also its chart line).
uint32_t stripe(const std::string &name) {
  lv_obj_t *name_label = ui::find_label(name);
  REQUIRE_MESSAGE(name_label != nullptr, name << " not shown: " << ui::dump_screen());
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  return lv_color_to32(lv_obj_get_style_border_color(lv_obj_get_parent(name_label), 0));
}

uint32_t palette(lv_palette_t p) { return lv_color_to32(lv_palette_main(p)); }

void start_with_sensors(FakeMoonraker &moonraker, json sensors) {
  app::start(moonraker, {{"printers", {{"FF5M", {{"monitored_sensors", sensors}}}}}});
  app::wait_ready(moonraker);
}

json sensor(const std::string &id, const std::string &name, const std::string &color) {
  return {{"id", id}, {"display_name", name}, {"controllable", false}, {"color", color}};
}

}  // namespace

// zMod's AD5X config gives Head "white", which used to fall back to orange,
// the colour of Weight.
TEST_CASE("a sensor coloured white is white") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start_with_sensors(moonraker, {sensor("temperature_sensor head", "Head", "white"),
                                 sensor("temperature_sensor weightValue", "Weight", "orange")});
  CHECK(stripe("Head") == lv_color_to32(lv_color_white()));
  CHECK(stripe("Weight") == palette(LV_PALETTE_ORANGE));
}

TEST_CASE("a sensor whose colour another sensor has gets the first free contrasting one") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start_with_sensors(moonraker, {sensor("extruder", "Extruder", "red"), sensor("heater_bed", "Bed", "red"),
                                 sensor("temperature_sensor head", "Head", "blue")});
  CHECK(stripe("Extruder") == palette(LV_PALETTE_RED));
  CHECK(stripe("Head") == palette(LV_PALETTE_BLUE));  // set explicitly: kept
  CHECK(stripe("Bed") == palette(LV_PALETTE_GREEN));  // red and blue taken
}

TEST_CASE("a sensor with an unknown colour gets the first free contrasting one") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start_with_sensors(moonraker, {sensor("extruder", "Extruder", "red"), sensor("heater_bed", "Bed", "mauve")});
  CHECK(stripe("Bed") == palette(LV_PALETTE_BLUE));
}

TEST_CASE("the C5 Pro nozzles keep the colours of the config") {
  FakeMoonraker moonraker(app::fixture("c5pro_basic.json"));
  json zmod = json::parse(std::ifstream(app::fixture("guppyconfig_c5pro.json")));
  start_with_sensors(moonraker, zmod["printers"]["C5Pro"]["monitored_sensors"]);
  CHECK(stripe("T0") == palette(LV_PALETTE_RED));
  CHECK(stripe("T1") == palette(LV_PALETTE_ORANGE));
  CHECK(stripe("T2") == palette(LV_PALETTE_YELLOW));
  CHECK(stripe("T3") == palette(LV_PALETTE_GREEN));
}

TEST_CASE("C5 Pro uses filament colors only for stripes and hides an empty nozzle stripe") {
  FakeMoonraker moonraker(app::fixture("c5pro_basic.json"));
  app::start(moonraker, app::zmod_sensors("guppyconfig_c5pro.json", "C5Pro"));
  app::wait_ready(moonraker);

  moonraker.push_status({{"zmod_color", {{"slots", json::array({
    {{"ID", "1"}, {"HEX", "12AB34"}, {"hasFilament", true}},
    {{"ID", "2"}, {"HEX", "FF0000"}, {"hasFilament", false}},
    {{"ID", "3"}, {"HEX", "0000FF"}, {"hasFilament", true}},
    {{"ID", "4"}, {"HEX", "00FF00"}, {"hasFilament", true}}
  })}}}});
  REQUIRE(app::wait_until([] { return stripe("T0") == lv_color_to32(lv_color_hex(0x12AB34)); }));
  lv_obj_t *t0 = ui::find_label("T0");
  REQUIRE(t0 != nullptr);
  CHECK(has_colour(t0, LV_PALETTE_RED));
  CHECK(stripe("T1") == palette(LV_PALETTE_GREY));
  lv_obj_t *name = ui::find_label("T1");
  REQUIRE(name != nullptr);
  CHECK(has_colour(name, LV_PALETTE_ORANGE));
  lv_obj_t *chart = ui::find_shown(&lv_chart_class);
  REQUIRE(chart != nullptr);
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    CHECK(lv_obj_get_style_border_opa(lv_obj_get_parent(name), LV_PART_MAIN) == LV_OPA_TRANSP);
    bool red_line = false, orange_line = false;
    for (lv_chart_series_t *s = lv_chart_get_series_next(chart, NULL); s; s = lv_chart_get_series_next(chart, s)) {
      const auto color = lv_color_to32(s->color);
      red_line |= color == palette(LV_PALETTE_RED);
      orange_line |= color == palette(LV_PALETTE_ORANGE);
      CHECK(color != lv_color_to32(lv_color_hex(0x12AB34)));
      CHECK(color != palette(LV_PALETTE_GREY));
    }
    CHECK(red_line);
    CHECK(orange_line);
  }

  ui::tap_tab(Nav::SETTINGS);
  ui::tap_label("System");
  lv_obj_t *label = ui::find_label("Filament colors");
  REQUIRE(label != nullptr);
  lv_obj_t *toggle;
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    toggle = ui::find_in(lv_obj_get_parent(label), &lv_switch_class);
    REQUIRE(toggle != nullptr);
    CHECK(lv_obj_has_state(toggle, LV_STATE_CHECKED));
  }
  ui::tap(toggle);
  ui::tap_tab(Nav::HOME);
  REQUIRE(app::wait_until([] { return stripe("T0") == palette(LV_PALETTE_RED); }));
  CHECK(stripe("T1") == palette(LV_PALETTE_ORANGE));
  for (const auto &entry : {std::pair<const char *, lv_palette_t>{"T0", LV_PALETTE_RED},
                            {"T1", LV_PALETTE_ORANGE}, {"T2", LV_PALETTE_YELLOW},
                            {"T3", LV_PALETTE_GREEN}, {"Bed", LV_PALETTE_BLUE},
                            {"Chamber", LV_PALETTE_PURPLE}}) {
    CAPTURE(entry.first);
    lv_obj_t *label = ui::find_label(entry.first);
    REQUIRE(label != nullptr);
    CHECK(has_colour(label, entry.second));
    CHECK(stripe(entry.first) == palette(entry.second));
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    bool found = false;
    for (lv_chart_series_t *s = lv_chart_get_series_next(chart, NULL); s; s = lv_chart_get_series_next(chart, s))
      found |= lv_color_to32(s->color) == palette(entry.second);
    CHECK(found);
  }
  ui::tap_tab(Nav::SETTINGS);
  ui::tap_label("System");
  ui::tap(toggle);
  ui::tap_tab(Nav::HOME);
  REQUIRE(app::wait_until([] { return stripe("T0") == lv_color_to32(lv_color_hex(0x12AB34)); }));
  lv_obj_t *restored = ui::find_label("T0");
  REQUIRE(restored != nullptr);
  CHECK(has_colour(restored, LV_PALETTE_RED));
  CHECK(has_colour(name, LV_PALETTE_ORANGE));
}

namespace {

// The tile of the sensor named `name`.
lv_obj_t *tile_of(const std::string &name) {
  lv_obj_t *name_label = ui::find_label(name);
  REQUIRE_MESSAGE(name_label != nullptr, name << " not shown: " << ui::dump_screen());
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  return lv_obj_get_parent(name_label);
}

bool numpad_open() { return ui::find_shown(&lv_textarea_class) != nullptr; }

}  // namespace

TEST_CASE("Home's tile being set is blue while the numpad is open") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker);
  app::wait_ready(moonraker);
  lv_obj_t *extruder = tile_of("Extruder");
  lv_obj_t *bed = tile_of("Bed");
  CHECK_FALSE(ui::has_bg(extruder, LV_PALETTE_BLUE));

  ui::tap_label("Extruder");
  REQUIRE(numpad_open());
  CHECK(ui::has_bg(extruder, LV_PALETTE_BLUE));
  CHECK_FALSE(ui::has_bg(bed, LV_PALETTE_BLUE));

  ui::tap_label("Bed");  // another tile while it is open: the numpad is for that one
  REQUIRE(numpad_open());
  CHECK_FALSE(ui::has_bg(extruder, LV_PALETTE_BLUE));
  CHECK(ui::has_bg(bed, LV_PALETTE_BLUE));

  ui::enter_on_numpad("60");
  CHECK_FALSE(numpad_open());
  CHECK_FALSE(ui::has_bg(bed, LV_PALETTE_BLUE));

  ui::tap_label("Bed");
  ui::enter_on_numpad("");  // OK with nothing entered closes it too
  CHECK_FALSE(numpad_open());
  CHECK_FALSE(ui::has_bg(bed, LV_PALETTE_BLUE));
}
