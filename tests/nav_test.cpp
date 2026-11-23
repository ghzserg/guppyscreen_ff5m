// The navigation bar on the left of every screen (src/nav.h): tabs, Stop,
// and the content area the screens live in.

#include "doctest/doctest.h"

#include "app.h"
#include "fake_moonraker.h"
#include "ui.h"

#include "guppyscreen.h"
#include "config.h"
#include "nav.h"

#include <functional>
#include <fstream>
#include <mutex>

namespace {

void start(FakeMoonraker &moonraker, const json &patch = json::object()) {
  app::start(moonraker, patch);
  app::wait_ready(moonraker);
}

lv_area_t coords(lv_obj_t *obj) {
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  lv_obj_update_layout(lv_scr_act());
  lv_area_t a;
  lv_obj_get_coords(obj, &a);
  return a;
}

// В окне подтверждения кнопка OK находится справа.
void tap_msgbox_ok() {
  lv_obj_t *btns = ui::find_shown(&lv_btnmatrix_class);
  REQUIRE_MESSAGE(btns != nullptr, "no msgbox; screen: " << ui::dump_screen());
  lv_area_t a = coords(btns);
  ui::tap_at(a.x1 + lv_area_get_width(&a) * 3 / 4, (a.y1 + a.y2) / 2);
}

Nav::Tab active_tab() {
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  return Nav::get().active();
}

}  // namespace

TEST_CASE("nav bar shows three tabs and Stop") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start(moonraker);

  for (Nav::Tab t : {Nav::HOME, Nav::TUNE, Nav::SETTINGS}) {
    CHECK_MESSAGE(ui::is_shown(Nav::get().tab_button(t)), "tab " << t);
  }
  CHECK(ui::is_shown(Nav::get().stop_button()));

  lv_area_t bar = coords(Nav::get().bar());
  lv_area_t content = coords(Nav::get().content());
  CHECK(bar.x1 == 0);
  CHECK(lv_area_get_width(&bar) == 60);
  CHECK(content.x1 == 60);
  CHECK(lv_area_get_width(&content) == 740);
  CHECK(lv_area_get_height(&content) == 480);
}

TEST_CASE("tapping a tab shows its root") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start(moonraker);

  ui::tap_tab(Nav::SETTINGS);
  CHECK_MESSAGE(ui::find_label("Klipper Restart") != nullptr, ui::dump_screen());
  CHECK(ui::find_label("Extrude") == nullptr);
  CHECK(active_tab() == Nav::SETTINGS);

  ui::tap_tab(Nav::HOME);
  CHECK_MESSAGE(ui::find_label("Extrude") != nullptr, ui::dump_screen());
  CHECK(active_tab() == Nav::HOME);
}

TEST_CASE("Stop asks first when prompt_emergency_stop is on") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start(moonraker, {{"prompt_emergency_stop", true}});

  ui::tap(Nav::get().stop_button());
  app::pump(100);
  CHECK(moonraker.request_count("printer.emergency_stop") == 0);
  tap_msgbox_ok();
  CHECK(app::wait_until([&] { return moonraker.request_count("printer.emergency_stop") == 1; }));
}

TEST_CASE("Stop stops at once when prompt_emergency_stop is off") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start(moonraker, {{"prompt_emergency_stop", false}});

  ui::tap(Nav::get().stop_button());
  CHECK(app::wait_until([&] { return moonraker.request_count("printer.emergency_stop") == 1; }));
  CHECK(ui::find_shown(&lv_btnmatrix_class) == nullptr);
}

TEST_CASE("main screen has no Stop button") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start(moonraker);

  CHECK(ui::find_label("Extrude") != nullptr);
  CHECK_MESSAGE(ui::find_label("Stop") == nullptr, ui::dump_screen());
}

TEST_CASE("Macros and Console open from Settings") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start(moonraker);

  ui::tap_tab(Nav::SETTINGS);
  ui::tap_label("Macros");
  CHECK_MESSAGE(app::wait_until([] { return ui::find_label("Show Hidden") != nullptr; }), ui::dump_screen());
  CHECK(active_tab() == Nav::SETTINGS);
  CHECK(ui::is_shown(Nav::get().stop_button()));

  ui::tap_tab(Nav::SETTINGS);
  CHECK(ui::find_label("Show Hidden") == nullptr);
  CHECK(ui::find_label("Klipper Restart") != nullptr);

  ui::tap_label("Console");  // its G-code input
  CHECK_MESSAGE(ui::find_shown(&lv_textarea_class) != nullptr, ui::dump_screen());
  ui::tap_tab(Nav::HOME);
  CHECK(ui::find_shown(&lv_textarea_class) == nullptr);
}

TEST_CASE("nav buttons show a press") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start(moonraker);

  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  for (lv_obj_t *btn : {Nav::get().stop_button(), Nav::get().tab_button(Nav::TUNE)}) {
    lv_opa_t idle = lv_obj_get_style_bg_opa(btn, LV_PART_MAIN);
    lv_obj_add_state(btn, LV_STATE_PRESSED);
    CHECK(lv_obj_get_style_bg_opa(btn, LV_PART_MAIN) == LV_OPA_COVER);
    CHECK(idle == LV_OPA_TRANSP);
    lv_obj_clear_state(btn, LV_STATE_PRESSED);
  }
}

namespace {
// Objects drawn by the next frame, by the content area's child they are in.
int drawn_outside(lv_obj_t *shown) {
  static int outside;
  static lv_obj_t *keep;
  outside = 0;
  keep = shown;
  auto cb = [](lv_event_t *e) {
    lv_obj_t *c = Nav::get().content();
    lv_obj_t *p = lv_event_get_target(e);
    while (p != NULL && lv_obj_get_parent(p) != c) p = lv_obj_get_parent(p);
    if (p != NULL && p != keep) outside++;
  };
  std::function<void(lv_obj_t *, bool)> hook = [&](lv_obj_t *o, bool add) {
    if (add) lv_obj_add_event_cb(o, cb, LV_EVENT_DRAW_MAIN_BEGIN, NULL);
    else lv_obj_remove_event_cb(o, cb);
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(o); i++) hook(lv_obj_get_child(o, i), add);
  };
  hook(Nav::get().content(), true);
  lv_refr_now(NULL);
  hook(Nav::get().content(), false);
  return outside;
}
}  // namespace

TEST_CASE("a tab switch draws only the shown screen") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start(moonraker);

  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  lv_refr_now(NULL);
  Nav::get().activate(Nav::TUNE);
  // The screens under Tune's root are covered: drawing them is what made
  // every switch take 1.4 s on the printer.
  CHECK(drawn_outside(Nav::get().root(Nav::TUNE)) == 0);
}

TEST_CASE("a full screen redraw draws only the shown screen") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start(moonraker);

  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  lv_refr_now(NULL);
  // As at startup and on reconnect: an area over both the bar and the
  // content area, which no screen covers. Drawing the covered screens
  // decoded the input shaper graphs (2 x 1.5 MB) on the printer.
  lv_obj_invalidate(lv_scr_act());
  CHECK(drawn_outside(Nav::get().root(Nav::HOME)) == 0);
}

TEST_CASE("Homing keeps the nav bar and Stop") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start(moonraker);

  ui::tap_label("Homing");
  CHECK_MESSAGE(ui::find_label("Motor off") != nullptr, ui::dump_screen());
  CHECK(ui::is_shown(Nav::get().stop_button()));
  CHECK(ui::is_shown(Nav::get().tab_button(Nav::HOME)));
  CHECK(ui::is_shown(Nav::get().tab_button(Nav::SETTINGS)));
}

TEST_CASE("tapping Home closes Homing") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start(moonraker);

  ui::tap_label("Homing");
  REQUIRE(ui::find_label("Motor off") != nullptr);
  ui::tap_tab(Nav::HOME);
  CHECK(ui::find_label("Motor off") == nullptr);
  CHECK(ui::find_label("Extrude") != nullptr);
}

TEST_CASE("homing fans and led start closed") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start(moonraker);

  CHECK_MESSAGE(ui::find_label("Extrude") != nullptr, ui::dump_screen());
  CHECK(ui::find_label("Motor off") == nullptr);
}

TEST_CASE("no screen has a Back button") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start(moonraker);

  for (const char *open : {"Homing", "Extrude", "Fans", "LED", "Fine Tune", "Bed Mesh"}) {
    ui::tap_label(open);
    app::pump(100);
    CHECK_MESSAGE(ui::find_label("Back") == nullptr, open << ": " << ui::dump_screen());
    CHECK_MESSAGE(ui::find_label("Stop") == nullptr, open << ": " << ui::dump_screen());
    CHECK(ui::is_shown(Nav::get().stop_button()));
    ui::tap_tab(Nav::HOME);
  }
  ui::tap_tab(Nav::PRINT);  // the file list
  CHECK_MESSAGE(ui::find_label("Back") == nullptr, ui::dump_screen());
  CHECK(ui::find_label("Status") == nullptr);
  ui::tap_tab(Nav::TUNE);
  for (const char *open : {"Limits", "Retract", "Input Shaper", "Belts/Shake", "PID"}) {
    if (ui::find_label(open) == nullptr) continue;  // not on this printer
    ui::tap_label(open);
    app::pump(100);
    CHECK_MESSAGE(ui::find_label("Back") == nullptr, open << ": " << ui::dump_screen());
    CHECK_MESSAGE(ui::find_label("Stop") == nullptr, open << ": " << ui::dump_screen());
    ui::tap_tab(Nav::TUNE);
  }
}

TEST_CASE("a tab tap runs leave actions only for open screens") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start(moonraker);

  int left_a = 0, left_b = 0, left_closed = 0;
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    Nav &nav = Nav::get();
    lv_obj_t *closed = lv_obj_create(nav.content());
    lv_obj_t *a = lv_obj_create(nav.content());
    lv_obj_t *b = lv_obj_create(nav.content());
    for (lv_obj_t *s : {closed, a, b}) lv_obj_set_size(s, LV_PCT(100), LV_PCT(100));
    nav.on_leave(closed, [&] { left_closed++; });
    nav.on_leave(a, [&] { left_a++; });
    nav.on_leave(b, [&] { left_b++; });
    lv_obj_move_background(closed);
    lv_obj_move_foreground(a);
    lv_obj_move_foreground(b);  // two screens stacked over Home
  }
  ui::tap_tab(Nav::TUNE);
  CHECK(left_a == 1);
  CHECK(left_b == 1);
  CHECK(left_closed == 0);
  CHECK(active_tab() == Nav::TUNE);
}

TEST_CASE("the belts graph is asked for at the content area's width") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start(moonraker);

  ui::tap_tab(Nav::TUNE);
  ui::tap_label("Belts/Shake");
  if (ui::find_label("Shake Belts") == nullptr) {  // the first one is Input Shaper on some printers
    ui::tap_tab(Nav::TUNE);
    lv_obj_t *second = nullptr;
    {
      std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
      ui::walk(Nav::get().root(Nav::TUNE), [&](lv_obj_t *o) {
        if (lv_obj_check_type(o, &lv_label_class) && std::string(lv_label_get_text(o)) == "Belts/Shake") second = o;
      });
    }
    REQUIRE(second != nullptr);
    ui::tap(second);
  }
  ui::tap_label("Shake Belts");
  REQUIRE(app::wait_until([&] {
    for (const auto &g : moonraker.gcodes()) if (g.find("_GUPPY_BELTS_SHAPER_CALIBRATION") == 0) return true;
    return false;
  }));
  bool width = false;
  for (const auto &g : moonraker.gcodes()) width = width || g.find("PNG_WIDTH=7.4 ") != std::string::npos;
  CHECK_MESSAGE(width, ui::join(moonraker.gcodes()));
}

namespace {

// A Klipper action prompt (// action:prompt_begin ... prompt_show).
void show_prompt(FakeMoonraker &moonraker, const std::string &header) {
  moonraker.push_notification("notify_gcode_response", {"// action:prompt_begin " + header});
  moonraker.push_notification("notify_gcode_response", {"// action:prompt_show"});
  REQUIRE_MESSAGE(app::wait_until([&] { return ui::find_label(header) != nullptr; }), ui::dump_screen());
}

}  // namespace

TEST_CASE("a Klipper prompt keeps the bar and blocks tabs") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start(moonraker);

  show_prompt(moonraker, "Hello");
  CHECK(coords(lv_obj_get_parent(ui::find_label("Hello"))).x1 >= 60);
  CHECK(ui::is_shown(Nav::get().stop_button()));
  ui::tap_tab(Nav::SETTINGS);
  CHECK(active_tab() == Nav::HOME);
  CHECK_MESSAGE(ui::find_label("Hello") != nullptr, ui::dump_screen());

  moonraker.push_notification("notify_gcode_response", {"// action:prompt_end"});
  CHECK(app::wait_until([] { return ui::find_label("Hello") == nullptr; }));
  ui::tap_tab(Nav::SETTINGS);
  CHECK(active_tab() == Nav::SETTINGS);
}

TEST_CASE("a Klipper prompt survives a print start") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start(moonraker);

  show_prompt(moonraker, "Hello");
  moonraker.push_status({{"print_stats", {{"filename", "cube.gcode"}, {"state", "printing"}}}});
  CHECK(app::wait_until([] { return active_tab() == Nav::PRINT; }));
  app::pump(200);
  CHECK_MESSAGE(ui::find_label("Hello") != nullptr, ui::dump_screen());
}

TEST_CASE("only the nav and full screen panels sit on the screen") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start(moonraker);

  // The screen saver, the bar and the content area; the init panel is
  // hidden once connected, the rest directly on the screen are small hidden
  // objects (not screens).
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  int shown = 0;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(lv_scr_act()); i++) {
    if (!lv_obj_has_flag(lv_obj_get_child(lv_scr_act(), i), LV_OBJ_FLAG_HIDDEN)) shown++;
  }
  CHECK(shown == 3);
}

TEST_CASE("Home opens Fine Tune") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start(moonraker);

  REQUIRE_MESSAGE(ui::find_label("Fine Tune") != nullptr, ui::dump_screen());
  ui::tap_label("Fine Tune");
  CHECK_MESSAGE(app::wait_until([] { return ui::find_label("Speed Reset") != nullptr; }), ui::dump_screen());
  ui::tap_tab(Nav::HOME);
  CHECK(ui::find_label("Speed Reset") == nullptr);
  CHECK(ui::find_label("Fine Tune") != nullptr);
}

TEST_CASE("Home opens Bed Mesh") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start(moonraker);

  REQUIRE_MESSAGE(ui::find_label("Bed Mesh") != nullptr, ui::dump_screen());
  ui::tap_label("Bed Mesh");
  CHECK_MESSAGE(app::wait_until([] { return ui::find_label("Calibrate") != nullptr; }), ui::dump_screen());
  ui::tap_tab(Nav::HOME);
  CHECK(ui::find_label("Calibrate") == nullptr);
}

TEST_CASE("Screw adjustment uses its own saved temperatures") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start(moonraker);

  ui::tap_tab(Nav::TUNE);
  ui::tap_label("PID");
  CHECK(ui::find_label("Screw adjustment") == nullptr);

  ui::tap_tab(Nav::HOME);
  ui::tap_label("Bed Mesh");
  REQUIRE_MESSAGE(ui::find_label("Screw adjustment") != nullptr, ui::dump_screen());
  REQUIRE_MESSAGE(ui::find_label("245 °C") != nullptr, ui::dump_screen());
  REQUIRE_MESSAGE(ui::find_label("80 °C") != nullptr, ui::dump_screen());
  lv_area_t a = coords(ui::find_label("Screw adjustment"));
  CHECK(a.x1 >= 420);
  CHECK(a.x2 <= 790);
  CHECK(a.y1 >= 320);
  CHECK(a.y2 <= 395);

  ui::tap_label("245 °C");
  lv_obj_t *input = ui::find_shown(&lv_textarea_class);
  REQUIRE_MESSAGE(input != nullptr, ui::dump_screen());
  lv_textarea_set_text(input, "230");
  lv_event_send(input, LV_EVENT_READY, nullptr);
  REQUIRE(ui::find_label("230 °C") != nullptr);

  ui::tap_label("80 °C");
  input = ui::find_shown(&lv_textarea_class);
  REQUIRE_MESSAGE(input != nullptr, ui::dump_screen());
  lv_textarea_set_text(input, "65");
  lv_event_send(input, LV_EVENT_READY, nullptr);
  REQUIRE(ui::find_label("65 °C") != nullptr);

  std::ifstream file(Config::get_instance()->get_path());
  REQUIRE(file.good());
  json config = json::parse(file);
  CHECK(config["screw_nozzle_temp"] == 230);
  CHECK(config["screw_bed_temp"] == 65);

  ui::tap_label("Screw adjustment");
  CHECK_MESSAGE(app::wait_until([&] {
    for (const auto &g : moonraker.gcodes()) {
      if (g == "BED_LEVEL_SCREWS_TUNE EXTRUDER_TEMP=230 BED_TEMP=65") return true;
    }
    return false;
  }), ui::join(moonraker.gcodes()));
}

TEST_CASE("Tune has no Fine Tune or Bed Mesh") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start(moonraker);

  ui::tap_tab(Nav::TUNE);
  REQUIRE_MESSAGE(ui::find_label("Limits") != nullptr, ui::dump_screen());
  CHECK(ui::find_label("Fine Tune") == nullptr);
  CHECK(ui::find_label("Bed Mesh") == nullptr);
}
