// FakePrinter (tests/harness/fake_printer.*): the simulated printer the
// browser preview uses. These tests drive it through the real app: the
// print file list, a print from start to complete, and heaters that follow
// a G-code target and feed the main screen's temperature chart.

#include "doctest/doctest.h"

#include "app.h"
#include "fake_moonraker.h"
#include "fake_printer.h"
#include "ui.h"

#include "guppyscreen.h"
#include "state.h"

#include <functional>
#include <mutex>
#include <regex>
#include <string>
#include <vector>

namespace {

std::vector<PrintFile> test_files() {
  PrintFile cube;
  cube.path = "cube.gcode";
  cube.estimated_time = 1500;
  cube.object_height = 10;
  cube.layer_height = 0.2;
  PrintFile bracket;
  bracket.path = "parts/bracket.gcode";
  return {cube, bracket};
}

FakePrinter::Options fast() {
  FakePrinter::Options o;
  o.print_seconds = 1.5;
  o.heat_speed = 40;
  o.tick_ms = 50;
  return o;
}

std::string print_state() {
  return State::get_instance()
    ->get_data_copy("/printer_state/print_stats/state"_json_pointer)
    .get<std::string>();
}

// Every object under the active screen, depth first.
void walk(lv_obj_t *obj, const std::function<void(lv_obj_t *)> &fn) {
  fn(obj);
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(obj); i++) walk(lv_obj_get_child(obj, i), fn);
}

// A visible label like "37%" with 0 < value < 100. Not ui::find_label: the
// print status panel draws it on top of the (clickable) progress bar.
bool progress_between_0_and_100() {
  static const std::regex pct("^([1-9][0-9]?)%$");
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  bool found = false;
  walk(lv_scr_act(), [&](lv_obj_t *o) {
    if (!found && lv_obj_check_type(o, &lv_label_class) && lv_obj_is_visible(o)) {
      found = std::regex_match(std::string(lv_label_get_text(o)), pct);
    }
  });
  return found;
}

// The current temperature in the Extruder row: 215 from "Extruder", "215", "/", "220".
int extruder_shown() {
  for (const auto &t : ui::row_texts("Extruder")) {
    try {
      return std::stoi(t);
    } catch (...) {
    }
  }
  return -1;
}

}  // namespace

TEST_CASE("fake printer files are listed in the print panel") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  FakePrinter printer(moonraker, test_files(), fast());
  app::start(moonraker);
  app::wait_ready(moonraker);

  ui::tap_tab(Nav::PRINT);
  bool listed = app::wait_until([] { return ui::table_has_row("cube.gcode"); });
  CHECK_MESSAGE(listed, "screen: " << ui::dump_screen());
  CHECK(ui::table_has_row("parts"));  // a sub-directory
}

TEST_CASE("fake print runs from start to complete") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  FakePrinter printer(moonraker, test_files(), fast());
  app::start(moonraker);
  app::wait_ready(moonraker);

  ui::tap_tab(Nav::PRINT);
  REQUIRE(app::wait_until([] { return ui::table_has_row("cube.gcode"); }));
  ui::tap_table_row("cube.gcode");
  REQUIRE(app::wait_until([&] { return moonraker.request_count("server.files.metadata") > 0; }));
  app::settle(moonraker);
  ui::tap_label("Print");

  REQUIRE(app::wait_until([&] { return moonraker.request_count("printer.print.start") > 0; }));
  CHECK(moonraker.requests("printer.print.start")[0].value("filename", "") == "cube.gcode");

  bool printing = app::wait_until([] { return print_state() == "printing"; });
  CHECK_MESSAGE(printing, "state: " << print_state());
  // The print status panel comes to the front and shows the progress.
  bool status = app::wait_until([] { return ui::find_label("Pause") != nullptr; });
  CHECK_MESSAGE(status, "screen: " << ui::dump_screen());
  bool progress = app::wait_until(progress_between_0_and_100, 10000);
  CHECK_MESSAGE(progress, "screen: " << ui::dump_screen());

  bool complete = app::wait_until([] { return print_state() == "complete"; }, 10000);
  CHECK_MESSAGE(complete, "state: " << print_state());
}

TEST_CASE("heater gcode heats the fake extruder and the chart rises") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  FakePrinter::Options o = fast();
  o.heat_speed = 6;  // ~5 s to 200 °C: the chart takes a point every ~2 s
  FakePrinter printer(moonraker, test_files(), o);
  app::start(moonraker);
  app::wait_ready(moonraker);

  // Tap the extruder and enter 200 on the numpad: SET_HEATER_TEMPERATURE.
  ui::tap_label("Extruder");
  ui::enter_on_numpad("200");
  REQUIRE(app::wait_until([&] { return !moonraker.gcodes().empty(); }));
  CHECK(moonraker.gcodes().back() == "SET_HEATER_TEMPERATURE HEATER=extruder TARGET=200");

  bool hot = app::wait_until([] { return extruder_shown() >= 195; }, 20000);
  REQUIRE_MESSAGE(hot, "Extruder row: " << ui::join(ui::row_texts("Extruder")));
  CHECK(ui::join(ui::row_texts("Extruder")).find("200") != std::string::npos);  // the target

  // The main screen's chart: next to the temperature list the shown
  // "Extruder" label is in (other panels, e.g. PID, have charts of their own).
  lv_obj_t *label = ui::find_label("Extruder");
  REQUIRE(label != nullptr);
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  // The nearest ancestor holding a chart: the main screen's grid.
  lv_obj_t *main_cont = lv_obj_get_parent(label);
  auto has_chart = [](lv_obj_t *o) {
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(o); i++) {
      if (lv_obj_check_type(lv_obj_get_child(o, i), &lv_chart_class)) return true;
    }
    return false;
  };
  while (main_cont != nullptr && !has_chart(main_cont)) main_cont = lv_obj_get_parent(main_cont);
  REQUIRE(main_cont != nullptr);
  std::vector<lv_coord_t> best;
  walk(main_cont, [&](lv_obj_t *chart) {
    if (!lv_obj_check_type(chart, &lv_chart_class)) return;
    for (lv_chart_series_t *s = lv_chart_get_series_next(chart, NULL); s; s = lv_chart_get_series_next(chart, s)) {
    std::vector<lv_coord_t> pts;
    uint16_t n = lv_chart_get_point_count(chart);
    uint16_t start = lv_chart_get_x_start_point(chart, s);
    for (uint16_t i = 0; i < n; i++) {
      lv_coord_t v = s->y_points[(start + i) % n];
      if (v != LV_CHART_POINT_NONE) pts.push_back(v);
    }
    if (!pts.empty() && (best.empty() || pts.back() > best.back())) best = pts;
    }
  });
  INFO("hottest series: " << best.size() << " points");
  REQUIRE(best.size() >= 2);
  CHECK(best.front() < best.back());
  CHECK(best.back() >= 100);
}

TEST_CASE("print files with temperatures the fake heaters cannot reach are rejected") {
  // At full power the fake bed settles at 125 °C: a print would wait for it forever.
  CHECK_THROWS(FakePrinter::parse_files(json::array({{{"path", "a.gcode"}, {"bed_temp", 130}}})));
  CHECK_THROWS(FakePrinter::parse_files(json::array({{{"path", "a.gcode"}, {"extruder_temp", 400}}})));
  CHECK_THROWS(FakePrinter::parse_files(json::array({{{"path", "a.gcode"}, {"bed_temp", -5}}})));
  CHECK_NOTHROW(FakePrinter::parse_files(json::array({{{"path", "a.gcode"}, {"bed_temp", 110}, {"extruder_temp", 300}}})));
}
