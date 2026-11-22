// The Print tab of the nav bar: the file list when idle, the print status
// screen while printing or paused.

#include "doctest/doctest.h"

#include "app.h"
#include "fake_hw.h"
#include "fake_moonraker.h"
#include "fake_printer.h"
#include "ink.h"
#include "ui.h"

#include "guppyscreen.h"
#include "nav.h"
#include "sensor_container.h"
#include "state.h"
#include "utils.h"

#include <cstdlib>
#include <ctime>
#include <fstream>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

LV_IMG_DECLARE(fan);

namespace {

std::vector<PrintFile> test_files() {
  PrintFile cube;
  cube.path = "cube.gcode";
  cube.estimated_time = 1500;
  cube.object_height = 10;
  cube.layer_height = 0.2;
  return {cube};
}

FakePrinter::Options fast(double print_seconds) {
  FakePrinter::Options o;
  o.print_seconds = print_seconds;
  o.heat_speed = 40;
  o.tick_ms = 50;
  return o;
}

std::string print_state() {
  return State::get_instance()
    ->get_data_copy("/printer_state/print_stats/state"_json_pointer)
    .get<std::string>();
}

Nav::Tab active_tab() {
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  return Nav::get().active();
}

// The file list's table holds cube.gcode and a tap on it would reach it.
bool file_list_shown() {
  if (!ui::table_has_row("cube.gcode")) return false;
  lv_obj_t *table = ui::find_shown(&lv_table_class);
  return table != nullptr;
}
bool print_screen_shown() { return ui::find_label("Pause") != nullptr || ui::find_label("Resume") != nullptr; }

// Pick cube.gcode in the Print tab and press Print, as a user would.
void start_print(FakeMoonraker &moonraker) {
  ui::tap_tab(Nav::PRINT);
  REQUIRE(app::wait_until(file_list_shown));
  ui::tap_table_row("cube.gcode");
  REQUIRE(app::wait_until([&] { return moonraker.request_count("server.files.metadata") > 0; }));
  app::settle(moonraker);
  ui::tap_label("Print");
  REQUIRE(app::wait_until([] { return print_state() == "printing"; }));
}

// Without FakePrinter: the print state comes only from what the test pushes.
void push_print(FakeMoonraker &moonraker, const json &print_stats) {
  moonraker.push_status({{"print_stats", print_stats}});
  app::pump(200);
}

void start_plain(FakeMoonraker &moonraker) {
  app::start(moonraker, {{"prompt_emergency_stop", false}});
  app::wait_ready(moonraker);
  push_print(moonraker, {{"filename", "cube.gcode"}, {"state", "printing"}});
  REQUIRE_MESSAGE(app::wait_until(print_screen_shown), ui::dump_screen());
}

}  // namespace

TEST_CASE("Print tab opens the file list when idle") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  FakePrinter printer(moonraker, test_files(), fast(1.5));
  app::start(moonraker);
  app::wait_ready(moonraker);

  CHECK(ui::is_shown(Nav::get().tab_button(Nav::PRINT)));
  CHECK_MESSAGE(ui::find_label("Print") == nullptr, "Home has no Print button: " << ui::dump_screen());
  ui::tap_tab(Nav::PRINT);
  CHECK_MESSAGE(app::wait_until(file_list_shown), ui::dump_screen());
  CHECK(active_tab() == Nav::PRINT);
  CHECK(ui::find_label("Status") == nullptr);
}

TEST_CASE("a print start shows the print screen") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  FakePrinter printer(moonraker, test_files(), fast(30));
  app::start(moonraker);
  app::wait_ready(moonraker);

  start_print(moonraker);
  CHECK_MESSAGE(app::wait_until(print_screen_shown), ui::dump_screen());
  CHECK(active_tab() == Nav::PRINT);
  CHECK(!file_list_shown());
  CHECK(ui::find_label("Back") == nullptr);
  CHECK(ui::find_label("Stop") == nullptr);
}

TEST_CASE("Print tab returns to the file list when the print completes") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  FakePrinter printer(moonraker, test_files(), fast(1.5));
  app::start(moonraker);
  app::wait_ready(moonraker);

  start_print(moonraker);
  REQUIRE(app::wait_until([] { return print_state() == "complete"; }, 10000));
  CHECK_MESSAGE(app::wait_until(file_list_shown), ui::dump_screen());
  CHECK(active_tab() == Nav::PRINT);
}

TEST_CASE("a completed print closes Fine Tune over the print screen") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  FakePrinter printer(moonraker, test_files(), fast(3));
  app::start(moonraker);
  app::wait_ready(moonraker);

  start_print(moonraker);
  REQUIRE(app::wait_until(print_screen_shown));
  ui::tap_label("Fine Tune");
  REQUIRE_MESSAGE(app::wait_until([] { return ui::find_label("Speed Reset") != nullptr; }), ui::dump_screen());
  REQUIRE(app::wait_until([] { return print_state() == "complete"; }, 10000));
  CHECK_MESSAGE(app::wait_until(file_list_shown), ui::dump_screen());
  CHECK(ui::find_label("Speed Reset") == nullptr);
}

TEST_CASE("pause and resume keep the open screen") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start_plain(moonraker);

  ui::tap_tab(Nav::HOME);
  ui::tap_label("Homing");
  REQUIRE(ui::find_label("Motor off") != nullptr);
  push_print(moonraker, {{"state", "paused"}});
  push_print(moonraker, {{"state", "printing"}});
  CHECK_MESSAGE(ui::find_label("Motor off") != nullptr, ui::dump_screen());
  CHECK(active_tab() == Nav::HOME);
}

TEST_CASE("Resume blocks repeated clicks until printing resumes or 20 seconds pass") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  moonraker.set_result("printer.print.resume", "ok");
  start_plain(moonraker);
  push_print(moonraker, {{"state", "paused"}});
  auto *button = ui::label_container("Resume");
  REQUIRE(button != nullptr);
  ui::tap_label("Resume");
  REQUIRE(app::wait_until([&] { return moonraker.request_count("printer.print.resume") == 1; }));

  lv_timer_t *cooldown = nullptr;
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    CHECK(lv_obj_has_state(button, LV_STATE_DISABLED));
    CHECK(lv_obj_has_state(lv_obj_get_child(button, 0), LV_STATE_DISABLED));
    CHECK(lv_obj_has_state(lv_obj_get_child(button, 1), LV_STATE_DISABLED));
    for (auto *timer = lv_timer_get_next(nullptr); timer; timer = lv_timer_get_next(timer)) {
      if (timer->period == 20000 && timer->repeat_count == 1) {
        REQUIRE(cooldown == nullptr);
        cooldown = timer;
      }
    }
    REQUIRE(cooldown != nullptr);
    lv_event_send(button, LV_EVENT_SHORT_CLICKED, nullptr);
  }
  ui::tap(button);
  push_print(moonraker, {{"state", "paused"}});
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    CHECK(lv_obj_has_state(button, LV_STATE_DISABLED));
    // Имитируем истечение 20 секунд без ожидания в тесте.
    lv_timer_ready(cooldown);
  }
  app::redraw();
  CHECK(moonraker.request_count("printer.print.resume") == 1);
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    CHECK_FALSE(lv_obj_has_state(button, LV_STATE_DISABLED));
  }
  ui::tap_label("Resume");
  REQUIRE(app::wait_until([&] { return moonraker.request_count("printer.print.resume") == 2; }));

  push_print(moonraker, {{"state", "printing"}});
  push_print(moonraker, {{"state", "paused"}});
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    CHECK_FALSE(lv_obj_has_state(button, LV_STATE_DISABLED));
    for (auto *timer = lv_timer_get_next(nullptr); timer; timer = lv_timer_get_next(timer)) {
      CHECK_FALSE((timer->period == 20000 && timer->repeat_count == 1));
    }
  }
  ui::tap_label("Resume");
  REQUIRE(app::wait_until([&] { return moonraker.request_count("printer.print.resume") == 3; }));

  push_print(moonraker, {{"state", "cancelled"}});
  push_print(moonraker, {{"state", "printing"}, {"filename", "next.gcode"}});
  push_print(moonraker, {{"state", "paused"}});
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    CHECK_FALSE(lv_obj_has_state(button, LV_STATE_DISABLED));
  }
  ui::tap_label("Resume");
  REQUIRE(app::wait_until([&] { return moonraker.request_count("printer.print.resume") == 4; }));
}

TEST_CASE("Cancel keeps the print screen until Klipper reports it") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start_plain(moonraker);

  ui::tap_label("Cancel");
  REQUIRE(app::wait_until([&] { return moonraker.request_count("printer.print.cancel") == 1; }));
  app::pump(200);
  CHECK_MESSAGE(print_screen_shown(), ui::dump_screen());
  CHECK(active_tab() == Nav::PRINT);

  push_print(moonraker, {{"state", "cancelled"}});
  CHECK(!print_screen_shown());
  CHECK(active_tab() == Nav::PRINT);  // the file list now
}

TEST_CASE("empty filename does not activate the Print tab") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start_plain(moonraker);

  ui::tap_tab(Nav::TUNE);
  push_print(moonraker, {{"filename", ""}});
  CHECK(active_tab() == Nav::TUNE);
}

TEST_CASE("Fine Tune from the print screen is left by the Print tab") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start_plain(moonraker);

  ui::tap_label("Fine Tune");
  REQUIRE_MESSAGE(ui::find_label("Speed Reset") != nullptr, ui::dump_screen());
  ui::tap_tab(Nav::PRINT);
  CHECK(ui::find_label("Speed Reset") == nullptr);
  CHECK_MESSAGE(print_screen_shown(), ui::dump_screen());
}

TEST_CASE("tapping the fan in print status opens fan controls") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start_plain(moonraker);

  lv_obj_t *fan_button = nullptr;
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    ui::walk(Nav::get().root(Nav::PRINT), [&](lv_obj_t *obj) {
      if (lv_obj_check_type(obj, &lv_img_class) && lv_img_get_src(obj) == &fan) {
        fan_button = lv_obj_get_parent(obj);
      }
    });
  }
  REQUIRE(fan_button != nullptr);

  ui::tap(fan_button);
  CHECK_MESSAGE(ui::find_label("Max") != nullptr, ui::dump_screen());
  CHECK(active_tab() == Nav::PRINT);

  ui::tap_tab(Nav::PRINT);
  CHECK_MESSAGE(print_screen_shown(), ui::dump_screen());
}

TEST_CASE("connecting during a print activates the Print tab") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  std::ifstream f(app::fixture("ad5m_basic.json"));
  json sub = json::parse(f)["printer.objects.subscribe"];
  sub["status"]["print_stats"]["state"] = "printing";
  sub["status"]["print_stats"]["filename"] = "cube.gcode";
  moonraker.set_result("printer.objects.subscribe", sub);
  app::start(moonraker);
  app::wait_ready(moonraker);

  CHECK_MESSAGE(app::wait_until(print_screen_shown), ui::dump_screen());
  CHECK(active_tab() == Nav::PRINT);

  ui::tap_tab(Nav::HOME);
  moonraker.forget_requests();
  moonraker.drop_client();
  REQUIRE(app::wait_until([&] { return moonraker.request_count("printer.objects.subscribe") > 0; }, 10000));
  app::settle(moonraker);
  CHECK_MESSAGE(app::wait_until(print_screen_shown), ui::dump_screen());
  CHECK(active_tab() == Nav::PRINT);
}

TEST_CASE("print screen fits the content area") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start_plain(moonraker);

  // What one sees: labels and images. (The details grid's own box is wider
  // than its column and was clipped at 800 px already; its texts fit.)
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  lv_obj_update_layout(lv_scr_act());
  lv_obj_t *root = Nav::get().root(Nav::PRINT);
  int checked = 0;
  std::function<void(lv_obj_t *)> walk = [&](lv_obj_t *o) {
    if (!lv_obj_is_visible(o)) return;
    bool label = lv_obj_check_type(o, &lv_label_class);
    if (label || lv_obj_check_type(o, &lv_img_class)) {
      lv_area_t a;
      lv_obj_get_coords(o, &a);
      std::string what = label ? std::string("label ") + lv_label_get_text(o) : std::string("img");
      CHECK_MESSAGE(a.x1 >= 60, what << " x1 " << a.x1);
      CHECK_MESSAGE(a.x2 <= 799, what << " x2 " << a.x2);
      checked++;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(o); i++) walk(lv_obj_get_child(o, i));
  };
  walk(root);
  CHECK(checked > 10);
}

TEST_CASE("a print started elsewhere opens the Print tab") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start_plain(moonraker);
  push_print(moonraker, {{"state", "complete"}});
  REQUIRE(!print_screen_shown());

  // The same file again from a slicer or a macro: no filename change.
  ui::tap_tab(Nav::HOME);
  push_print(moonraker, {{"state", "printing"}});
  CHECK(active_tab() == Nav::PRINT);
  CHECK_MESSAGE(print_screen_shown(), ui::dump_screen());
}

TEST_CASE("a file selected without printing keeps the file list") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker);
  app::wait_ready(moonraker);

  push_print(moonraker, {{"filename", "cube.gcode"}});  // e.g. M23, state stays standby
  CHECK(active_tab() == Nav::HOME);
  ui::tap_tab(Nav::PRINT);
  CHECK_MESSAGE(!print_screen_shown(), ui::dump_screen());
}

TEST_CASE("a double tap on Print starts one print") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  FakePrinter::Options o = fast(30);
  o.tick_ms = 1000;  // Klipper is slow to report printing: the list stays up
  FakePrinter printer(moonraker, test_files(), o);
  app::start(moonraker);
  app::wait_ready(moonraker);

  ui::tap_tab(Nav::PRINT);
  REQUIRE(app::wait_until(file_list_shown));
  ui::tap_table_row("cube.gcode");
  REQUIRE(app::wait_until([&] { return moonraker.request_count("server.files.metadata") > 0; }));
  app::settle(moonraker);
  lv_obj_t *print = ui::find_label("Print");
  REQUIRE(print != nullptr);
  lv_area_t a;
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    lv_obj_get_coords(lv_obj_get_parent(print), &a);
  }
  lv_coord_t x = (a.x1 + a.x2) / 2, y = (a.y1 + a.y2) / 2;
  ui::tap_at(x, y);
  ui::tap_at(x, y);
  app::pump(300);
  CHECK(moonraker.request_count("printer.print.start") == 1);
}

namespace {
// The Print tab button's background as drawn now: transparent or a colour.
bool print_tab_orange() {
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  lv_obj_t *btn = Nav::get().tab_button(Nav::PRINT);
  if (lv_obj_get_style_bg_opa(btn, LV_PART_MAIN) != LV_OPA_COVER) return false;
  lv_color_t c = lv_obj_get_style_bg_color(btn, LV_PART_MAIN);
  return c.full == lv_palette_main(LV_PALETTE_ORANGE).full
    || c.full == lv_palette_darken(LV_PALETTE_ORANGE, 1).full;
}
}  // namespace

TEST_CASE("the Print tab is orange while printing") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker, {{"prompt_emergency_stop", false}});
  app::wait_ready(moonraker);
  CHECK(!print_tab_orange());

  push_print(moonraker, {{"filename", "cube.gcode"}, {"state", "printing"}});
  CHECK(print_tab_orange());  // the active tab
  ui::tap_tab(Nav::HOME);
  CHECK(print_tab_orange());  // and not the active one

  push_print(moonraker, {{"state", "paused"}});
  CHECK(print_tab_orange());
  push_print(moonraker, {{"state", "printing"}});
  CHECK(print_tab_orange());
  push_print(moonraker, {{"state", "complete"}});
  CHECK(!print_tab_orange());
}

TEST_CASE("connecting during a print makes the Print tab orange") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  std::ifstream f(app::fixture("ad5m_basic.json"));
  json sub = json::parse(f)["printer.objects.subscribe"];
  sub["status"]["print_stats"]["state"] = "printing";
  sub["status"]["print_stats"]["filename"] = "cube.gcode";
  moonraker.set_result("printer.objects.subscribe", sub);
  app::start(moonraker);
  app::wait_ready(moonraker);
  REQUIRE(app::wait_until(print_screen_shown));
  CHECK(print_tab_orange());
}

namespace {

lv_area_t parent_area(lv_obj_t *label) {
  lv_area_t a;
  lv_obj_get_coords(lv_obj_get_parent(label), &a);
  return a;
}

// Home during a print: every sensor and the print status are on screen,
// above the chart and left of the buttons; the status under the sensors.
void check_home_fits(FakeMoonraker &moonraker, const json &config_patch,
                     const std::vector<std::string> &sensors) {
  app::start(moonraker, config_patch);
  app::wait_ready(moonraker);
  push_print(moonraker, {{"filename", "cube.gcode"}, {"state", "printing"}});
  ui::tap_tab(Nav::HOME);
  REQUIRE_MESSAGE(app::wait_until([] { return ui::find_label("Status: printing", ui::Match::Contains) != nullptr; }),
                  ui::dump_screen());

  lv_obj_t *status = ui::find_label("Status: printing", ui::Match::Contains);
  lv_obj_t *homing = ui::find_label("Homing");
  lv_obj_t *chart = ui::find_shown(&lv_chart_class);
  REQUIRE(homing != nullptr);
  REQUIRE(chart != nullptr);
  std::vector<lv_obj_t *> labels;
  for (const auto &name : sensors) {
    lv_obj_t *l = ui::find_label(name);
    REQUIRE_MESSAGE(l != nullptr, name << " not shown: " << ui::dump_screen());
    labels.push_back(l);
  }

  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  lv_area_t c, h;
  lv_obj_get_coords(chart, &c);
  lv_obj_get_coords(lv_obj_get_parent(homing), &h);
  lv_area_t s = parent_area(status);
  CHECK(s.y2 < c.y1);
  CHECK(s.x2 < h.x1);
  // The progress: a bar along the bottom, as thick as the tiles' stripes,
  // instead of a ring.
  lv_obj_t *box = lv_obj_get_parent(status);
  lv_obj_t *bar = nullptr;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(box); i++) {
    lv_obj_t *ch = lv_obj_get_child(box, i);
    CHECK_FALSE(lv_obj_check_type(ch, &lv_arc_class));
    if (lv_obj_check_type(ch, &lv_bar_class)) bar = ch;
  }
  REQUIRE(bar != nullptr);
  lv_area_t b, inner;
  lv_obj_get_coords(bar, &b);
  lv_obj_get_content_coords(box, &inner);
  CHECK(lv_area_get_height(&b) == SensorContainer::stripe_width());
  CHECK(b.y2 == inner.y2);
  CHECK(b.x1 == inner.x1);
  CHECK(b.x2 == inner.x2);
  // Four tile columns wide whatever its text: from the first tile's left edge
  // to where a fourth tile would end.
  lv_area_t t0 = parent_area(labels[0]), t1 = parent_area(labels[1]);
  lv_coord_t gap = t1.x1 - t0.x2 - 1;
  INFO("status " << s.x1 << ".." << s.x2 << ", tile " << t0.x1 << ".." << t0.x2 << ", gap " << gap);
  CHECK(s.x1 == t0.x1);
  CHECK(lv_area_get_width(&s) == 4 * lv_area_get_width(&t0) + 3 * gap);
  for (size_t i = 0; i < labels.size(); i++) {
    lv_area_t t = parent_area(labels[i]);
    INFO(sensors[i]);
    CHECK(t.y2 < c.y1);
    CHECK(t.x2 < h.x1);
    CHECK(s.y1 >= t.y2);
  }
}

// A zMod guppyconfig's sensors, fans and macros under the test config's
// printer name.
json printer_config(const std::string &fixture, const std::string &printer) {
  json zmod = json::parse(std::ifstream(app::fixture(fixture)));
  const json &p = zmod["printers"][printer];
  json patch = {{"monitored_sensors", p["monitored_sensors"]}, {"fans", p["fans"]}};
  if (p.contains("default_macros")) patch["default_macros"] = p["default_macros"];
  return {{"printers", {{"FF5M", patch}}}, {"prompt_emergency_stop", false}};
}

}  // namespace

TEST_CASE("Home fits the AD5M temperatures and print status") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  check_home_fits(moonraker, {{"prompt_emergency_stop", false}}, {"Extruder", "Bed"});
}

TEST_CASE("Home fits the AD5X temperatures and print status") {
  FakeMoonraker moonraker(app::fixture("ad5x_basic.json"));
  check_home_fits(moonraker, printer_config("guppyconfig_ad5x.json", "AD5X"),
                  {"Extruder", "Bed", "Head", "Weight"});
}

TEST_CASE("Home fits the C5 Pro temperatures and print status") {
  FakeMoonraker moonraker(app::fixture("c5pro_basic.json"));
  check_home_fits(moonraker, printer_config("guppyconfig_c5pro.json", "C5Pro"),
                  {"T0", "T1", "T2", "T3", "Bed", "Chamber"});
}

// Имя печатаемого файла открывает каталог; кнопка Limits — настройки ограничений.
TEST_CASE("tapping the filename opens the file list from the print screen") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  FakePrinter printer(moonraker, test_files(), fast(60));
  app::start(moonraker);
  app::wait_ready(moonraker);
  start_print(moonraker);
  REQUIRE(app::wait_until(print_screen_shown));
  REQUIRE(app::wait_until([] { return ui::find_label("cube.gcode") != nullptr; }));

  lv_obj_t *limits = ui::label_container("Limits");
  lv_obj_t *fine_tune = ui::label_container("Fine Tune");
  REQUIRE_MESSAGE(limits != nullptr, ui::dump_screen());
  REQUIRE(fine_tune != nullptr);
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    lv_obj_update_layout(lv_scr_act());
    lv_area_t f, t;
    lv_obj_get_coords(limits, &f);
    lv_obj_get_coords(fine_tune, &t);
    INFO("Limits " << f.x1 << ".." << f.x2 << ", Fine Tune " << t.x1 << ".." << t.x2);
    CHECK(f.x2 < t.x1);
    CHECK(f.y1 == t.y1);
  }

  ui::tap_label("cube.gcode");
  CHECK_MESSAGE(app::wait_until(file_list_shown), ui::dump_screen());
  CHECK(active_tab() == Nav::PRINT);
  ui::tap_tab(Nav::PRINT);
  CHECK_MESSAGE(app::wait_until(print_screen_shown), ui::dump_screen());
  CHECK(!file_list_shown());
}

TEST_CASE("the print screen Limits button opens the existing limits panel") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  FakePrinter printer(moonraker, test_files(), fast(60));
  app::start(moonraker);
  app::wait_ready(moonraker);
  start_print(moonraker);
  REQUIRE(app::wait_until(print_screen_shown));

  ui::tap_label("Limits");
  CHECK_MESSAGE(app::wait_until([] { return ui::find_label("Velocity (mm/s)") != nullptr; }), ui::dump_screen());
  CHECK(active_tab() == Nav::PRINT);
}

TEST_CASE("the print screen shows the acceleration above elapsed time and opens Limits from it") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  moonraker.set_result("server.files.metadata", {{"estimated_time", 1063}});
  start_plain(moonraker);
  REQUIRE(ui::find_label("20000 mm/s2") != nullptr);

  moonraker.push_status({{"toolhead", {{"max_accel", 12345.0}}},
                         {"print_stats", {{"print_duration", 57}}}});
  const auto elapsed_text = KUtils::eta_string(57);
  const auto eta_text = "ETA: " + KUtils::eta_string(1006);
  REQUIRE(app::wait_until([&] { return ui::find_label(eta_text) != nullptr; }));
  auto *acceleration = ui::find_label("12345 mm/s2");
  auto *elapsed = ui::find_label(elapsed_text);
  REQUIRE(acceleration != nullptr);
  REQUIRE(elapsed != nullptr);
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    auto *acceleration_box = lv_obj_get_parent(acceleration);
    auto *elapsed_box = lv_obj_get_parent(elapsed);
    CHECK(lv_obj_get_parent(acceleration_box) == lv_obj_get_parent(elapsed_box));
    CHECK(lv_obj_get_style_grid_cell_row_pos(acceleration_box, 0) == 3);
    CHECK(lv_obj_get_style_grid_cell_row_pos(elapsed_box, 0) == 4);
  }

  ui::tap(acceleration);
  CHECK_MESSAGE(app::wait_until([] { return ui::find_label("Velocity (mm/s)") != nullptr; }), ui::dump_screen());
}

namespace {

// Whether `font` has a glyph for every character of the UTF-8 `text`.
// The first letter of `text` that `font` doesn't draw itself (its fallback
// would, smaller), or 0.
uint32_t missing_glyph(const lv_font_t *font, const std::string &text) {
  uint32_t i = 0;
  while (i < text.size()) {
    uint32_t letter = _lv_txt_encoded_next(text.c_str(), &i);
    lv_font_glyph_dsc_t dsc;
    if (!lv_font_get_glyph_dsc(font, &dsc, letter, 0) || dsc.resolved_font != font) return letter;
  }
  return 0;
}

// The print screen's value labels (right of the details' icons). Fits: the
// label right of the icon and inside its box; a text longer than the label
// scrolls.
std::vector<lv_obj_t *> detail_values() {
  std::vector<lv_obj_t *> found;
  for (const char *text : {"T0: ", "mm/s", "mm3/s", " mm", " / ", "%, "}) {  // "%, ": the fans, not the bar
    lv_obj_t *l = ui::find_label(text, ui::Match::Contains);
    if (l != nullptr) found.push_back(l);
  }
  return found;
}

}  // namespace

TEST_CASE("the print screen's values are as big as the tiles' temperatures") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start_plain(moonraker);
  moonraker.push_status({{"extruder", {{"temperature", 215.0}, {"target", 220.0}}}});
  app::settle(moonraker);

  auto values = detail_values();
  CHECK(values.size() >= 5);
  for (lv_obj_t *l : values) {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    std::string text = lv_label_get_text(l);
    INFO(text);
    CHECK(lv_obj_get_style_text_font(l, 0) == SensorContainer::temperature_font());
    CHECK(missing_glyph(lv_obj_get_style_text_font(l, 0), text) == 0);
  }
}

// Every language's units and time (d h min s, Turkish g s d sn) in the font.
TEST_CASE("the tiles' temperature font has the print screen's units in every language") {
  for (const char *text : {"T0: 215 / 220", "100%, 100%, 100%", "0.000 mm", "12.3 mm3/s", "1.25 m", "...",
                           "1d 2h 3min 4s", "1g 2s 3d 4sn", "мм/с мм3/с 1д 2ч 3м 4с"}) {
    INFO(std::string(text));
    CHECK(missing_glyph(SensorContainer::temperature_font(), text) == 0);
  }
}

TEST_CASE("the print screen's values fit right of their icons") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start_plain(moonraker);
  moonraker.push_status({{"extruder", {{"temperature", 215.0}, {"target", 220.0}}},
                         {"output_pin fan0", {{"value", 1.0}}}, {"output_pin fan1", {{"value", 1.0}}},
                         {"output_pin fan2", {{"value", 1.0}}}});
  app::settle(moonraker);

  for (lv_obj_t *l : detail_values()) {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    lv_obj_update_layout(lv_scr_act());
    lv_obj_t *box = lv_obj_get_parent(l);
    lv_obj_t *icon = lv_obj_get_child(box, 0);
    lv_area_t a, i, b;
    lv_obj_get_coords(l, &a);
    lv_obj_get_coords(icon, &i);
    lv_obj_get_content_coords(box, &b);
    // The text's own width: the label may be wider than it.
    lv_point_t size;
    const char *text = lv_label_get_text(l);
    lv_txt_get_size(&size, text, lv_obj_get_style_text_font(l, 0), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    INFO(text << ": icon " << i.x1 << ".." << i.x2 << ", label " << a.x1 << ".." << a.x2 << ", text width "
         << size.x << ", box " << b.x1 << ".." << b.x2);
    CHECK(a.x1 > i.x2);
    CHECK(a.x2 <= b.x2);
    if (size.x > lv_area_get_width(&a)) {
      CHECK(lv_label_get_long_mode(l) == LV_LABEL_LONG_SCROLL_CIRCULAR);  // too long: it scrolls
    }
  }
}

// FakePrinter's thumbnail is 300 px, drawn at 192: laid out at 192 too, the
// file name and the thumbnail fit between the top of the screen and the
// progress bar.
TEST_CASE("the print screen's thumbnail column fits above the progress bar") {
  const char *tmp = getenv("TMPDIR");
  std::string gcodes = std::string(tmp && *tmp ? tmp : "/tmp") + "/guppy-gcodes-XXXXXX";
  REQUIRE(mkdtemp(gcodes.data()) != NULL);
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  FakePrinter::Options o = fast(60);
  o.gcodes_dir = gcodes;
  FakePrinter printer(moonraker, test_files(), o);
  app::start(moonraker);
  app::wait_ready(moonraker);
  start_print(moonraker);
  REQUIRE(app::wait_until(print_screen_shown));
  app::settle(moonraker);

  lv_obj_t *clock = nullptr;
  std::string first_time;
  REQUIRE(app::wait_until([&] {
    const auto now = std::time(nullptr);
    for (auto stamp : {now, now - 1}) {
      std::tm local;
      char text[9];
      REQUIRE(localtime_r(&stamp, &local) != nullptr);
      REQUIRE(std::strftime(text, sizeof(text), "%H:%M:%S", &local) == 8);
      if ((clock = ui::find_label(text)) != nullptr) {
        first_time = text;
        return true;
      }
    }
    return false;
  }));
  REQUIRE(app::wait_until([&] {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    return first_time != lv_label_get_text(clock);
  }));

  // Not find_label(): the bug put the name off the screen.
  lv_obj_t *name = nullptr;
  lv_obj_t *limits = ui::label_container("Limits");
  REQUIRE(limits != nullptr);
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  lv_obj_update_layout(lv_scr_act());
  ui::walk(Nav::get().root(Nav::PRINT), [&](lv_obj_t *o) {
    if (lv_obj_check_type(o, &lv_label_class) && std::string(lv_label_get_text(o)) == "cube.gcode") name = o;
  });
  lv_obj_t *bar = ui::find_in(Nav::get().root(Nav::PRINT), &lv_bar_class);
  REQUIRE(name != nullptr);
  REQUIRE(bar != nullptr);
  lv_obj_t *column = lv_obj_get_parent(name);
  lv_area_t n, b, c, f, t;
  lv_obj_get_coords(clock, &t);
  lv_obj_get_coords(name, &n);
  lv_obj_get_coords(bar, &b);
  lv_obj_get_coords(column, &c);
  lv_obj_get_coords(limits, &f);
  INFO("name y " << n.y1 << "; bar " << b.x1 << "," << b.y1 << ".." << b.x2 << "," << b.y2 << "; column x " << c.x1
       << ".." << c.x2 << "; Limits y " << f.y1);
  CHECK(t.y1 >= 0);
  CHECK(t.y2 < n.y1);
  CHECK(c.y2 < b.y1);
  CHECK(b.y2 < f.y1);
}

// The progress bar: a full row between the thumbnail with the details and the
// buttons, the ETA at its left.
TEST_CASE("the print screen's progress bar spans the screen with the ETA at its left") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  FakePrinter printer(moonraker, test_files(), fast(60));
  app::start(moonraker);
  app::wait_ready(moonraker);
  start_print(moonraker);
  REQUIRE(app::wait_until(print_screen_shown));
  REQUIRE_MESSAGE(app::wait_until([] {
    lv_obj_t *l = ui::find_label("ETA: ", ui::Match::Contains);
    if (l == nullptr) return false;
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    return std::string(lv_label_get_text(l)).find_first_of("0123456789") != std::string::npos;
  }), ui::dump_screen());

  lv_obj_t *eta = ui::find_label("ETA: ", ui::Match::Contains);
  lv_obj_t *limits = ui::label_container("Limits");
  lv_obj_t *nozzle = ui::find_label("T0: ", ui::Match::Contains);  // the details' first value
  REQUIRE(limits != nullptr);
  REQUIRE(nozzle != nullptr);
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  lv_obj_update_layout(lv_scr_act());
  lv_obj_t *bar = ui::find_in(Nav::get().root(Nav::PRINT), &lv_bar_class);
  REQUIRE(bar != nullptr);
  lv_obj_t *details = lv_obj_get_parent(lv_obj_get_parent(nozzle));  // label < box < details grid
  lv_obj_t *screen = lv_obj_get_parent(details);
  lv_area_t b, d, s, f, e;
  lv_obj_get_coords(bar, &b);
  lv_obj_get_coords(details, &d);
  lv_obj_get_coords(screen, &s);
  lv_obj_get_coords(limits, &f);
  lv_obj_get_coords(eta, &e);
  INFO("bar " << b.x1 << "," << b.y1 << ".." << b.x2 << "," << b.y2 << "; details " << d.x1 << ".." << d.x2 << ","
              << d.y2 << "; screen x " << s.x1 << ".." << s.x2 << "; Limits y " << f.y1 << "; ETA x " << e.x1 << ".."
              << e.x2);
  CHECK(b.x1 == s.x1 + lv_obj_get_style_pad_left(screen, 0));
  CHECK(b.x2 == d.x2);
  CHECK(b.y1 > d.y2);
  CHECK(b.y2 < f.y1);
  CHECK(e.x1 > b.x1);
  CHECK(e.x2 < (b.x1 + b.x2) / 2);  // the left half
  CHECK(e.y1 >= b.y1);
  CHECK(e.y2 <= b.y2);
  // 40 px tall, its texts in the details' font.
  CHECK(lv_obj_get_height(bar) == 40);
  lv_obj_t *row = lv_obj_get_parent(bar);
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(row); i++) {
    lv_obj_t *label = lv_obj_get_child(row, i);
    if (lv_obj_check_type(label, &lv_label_class)) {
      CHECK(lv_obj_get_style_text_font(label, 0) == SensorContainer::temperature_font());
    }
  }
}

// The digits font's line has room under the baseline for descenders: the
// texts on the progress bar must look centred, by their ink, not their box.
TEST_CASE("the print screen's progress bar texts are centred on the bar") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  // So slow the bar stays empty: its filled part never runs under a text.
  FakePrinter printer(moonraker, test_files(), fast(1e6));
  app::start(moonraker);
  app::wait_ready(moonraker);
  start_print(moonraker);
  REQUIRE(app::wait_until(print_screen_shown));
  REQUIRE_MESSAGE(app::wait_until([] {
    lv_obj_t *l = ui::find_label("ETA: ", ui::Match::Contains);
    if (l == nullptr) return false;
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    return std::string(lv_label_get_text(l)).find_first_of("0123456789") != std::string::npos;
  }), ui::dump_screen());

  REQUIRE_MESSAGE(ui::find_label("0%") != nullptr, ui::dump_screen());

  app::redraw();
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  lv_obj_t *bar = ui::find_in(Nav::get().root(Nav::PRINT), &lv_bar_class);
  REQUIRE(bar != nullptr);
  REQUIRE(lv_bar_get_value(bar) == 0);
  lv_area_t b;
  lv_obj_get_coords(bar, &b);
  int bar_middle2 = b.y1 + b.y2;  // twice the centre: no rounding
  int texts = 0;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(lv_obj_get_parent(bar)); i++) {
    lv_obj_t *label = lv_obj_get_child(lv_obj_get_parent(bar), i);
    if (!lv_obj_check_type(label, &lv_label_class)) continue;
    texts++;
    lv_area_t l;
    lv_obj_get_coords(label, &l);
    // Ink: rows with a pixel unlike the bar's beside the label, on the side
    // away from the bar's rounded end.
    int bg_x = l.x1 - b.x1 > b.x2 - l.x2 ? l.x1 - 2 : l.x2 + 2;
    ink::Rows r = ink::rows(b, l.x1, l.x2, [&](int, int y) { return fake_hw::pixel(bg_x, y); });
    std::string text = lv_label_get_text(label);
    REQUIRE_MESSAGE(r.top >= 0, text);
    INFO(text << ": ink " << r.top << ".." << r.bottom << ", bar " << b.y1 << ".." << b.y2);
    CHECK(std::abs((r.top + r.bottom) - bar_middle2) <= 1);  // within half a pixel
  }
  CHECK(texts == 3);
}

// The details' values are centred in their frames by their digits (the
// font keeps room for descenders under the baseline).
TEST_CASE("the print screen's values are centred in their frames") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  FakePrinter printer(moonraker, test_files(), fast(60));
  app::start(moonraker);
  app::wait_ready(moonraker);
  start_print(moonraker);
  REQUIRE(app::wait_until(print_screen_shown));
  lv_obj_t *nozzle = ui::find_label("T0: ", ui::Match::Contains);
  REQUIRE_MESSAGE(nozzle != nullptr, ui::dump_screen());

  app::redraw();
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  lv_obj_t *details = lv_obj_get_parent(lv_obj_get_parent(nozzle));  // label < box < details grid
  int framed = 0;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(details); i++) {
    lv_obj_t *box = lv_obj_get_child(details, i);
    for (uint32_t j = 0; j < lv_obj_get_child_cnt(box); j++) {
      lv_obj_t *label = lv_obj_get_child(box, j);
      if (lv_obj_check_type(label, &lv_label_class)) {
        ink::check_framed(label);
        framed++;
      }
    }
  }
  CHECK(framed == 10);
}

// The progress bar's ETA is in the details' font, which has only the glyphs
// listed in assets/fonts/build.sh: every translation of "ETA: {}" must be
// among them (CJK aside: no font of ours has those).
TEST_CASE("the details' font has the letters of every translation of the ETA") {
  std::string locale = app::fixture("../../src/locale");
  const lv_font_t *font = SensorContainer::temperature_font();
  for (const char *lang : {"ru", "en", "de", "it", "fr", "es", "pt", "cs", "tr"}) {
    std::ifstream po(locale + "/" + lang + "/LC_MESSAGES/guppyscreen.po");
    REQUIRE(po);
    std::string line, text;
    while (std::getline(po, line)) {
      if (line == "msgid \"ETA: {}\"") {
        std::getline(po, line);
        text = line.substr(8, line.size() - 9);  // msgstr "..."
      }
    }
    REQUIRE_MESSAGE(!text.empty(), std::string(lang) << ": no one-line msgstr for \"ETA: {}\"");
    size_t time = text.find("{}");
    REQUIRE_MESSAGE(time != std::string::npos, std::string(lang) << ": no {} for the time in \"" << text << "\"");
    text.erase(time, 2);
    uint32_t missing = missing_glyph(font, text);
    CHECK_MESSAGE(missing == 0, std::string(lang) << ": U+" << std::hex << missing);
  }
}

TEST_CASE("the print screen writes its units and time in English without a Russian translation") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start_plain(moonraker);
  moonraker.push_status({{"print_stats", {{"print_duration", 3725}}}});
  CHECK_MESSAGE(app::wait_until([] { return ui::find_label("1h 2min 5s") != nullptr; }), ui::dump_screen());
  std::string screen = ui::dump_screen();
  CHECK_MESSAGE(ui::find_label(" mm/s", ui::Match::Contains) != nullptr, screen);
  CHECK_MESSAGE(ui::find_label(" mm3/s", ui::Match::Contains) != nullptr, screen);
  CHECK_MESSAGE(ui::find_label("0 m") != nullptr, screen);
  CHECK_MESSAGE(screen.find(" м") == std::string::npos, screen);
}
