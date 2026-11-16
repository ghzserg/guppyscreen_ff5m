// The geometry of every object on every screen after startup, compared with
// tests/fixtures/layout_snapshot.json. All panels are created at startup and
// only moved behind the main screen, so this covers every panel's layout at
// once: a refactoring of the layout code must not move anything.
//
// After an intended layout change, regenerate the file:
//   UPDATE_SNAPSHOT=1 make test TEST='*snapshot*'

#include "doctest/doctest.h"

#include "app.h"
#include "fake_moonraker.h"

#include "grid_layout.h"
#include "guppyscreen.h"

#include <cstdlib>
#include <fstream>
#include <mutex>
#include <set>
#include <sstream>
#include <string>

namespace {

const char *class_name(lv_obj_t *o) {
  static const std::pair<const lv_obj_class_t *, const char *> classes[] = {
    {&lv_label_class, "label"}, {&lv_btn_class, "btn"}, {&lv_img_class, "img"},
    {&lv_imgbtn_class, "imgbtn"}, {&lv_chart_class, "chart"}, {&lv_table_class, "table"},
    {&lv_textarea_class, "textarea"}, {&lv_keyboard_class, "keyboard"}, {&lv_btnmatrix_class, "btnmatrix"},
    {&lv_slider_class, "slider"}, {&lv_bar_class, "bar"}, {&lv_arc_class, "arc"},
    {&lv_switch_class, "switch"}, {&lv_dropdown_class, "dropdown"},
    {&lv_spinbox_class, "spinbox"}, {&lv_tabview_class, "tabview"}, {&lv_canvas_class, "canvas"},
    {&lv_msgbox_class, "msgbox"}, {&lv_spinner_class, "spinner"},
  };
  for (const auto &c : classes) {
    if (lv_obj_get_class(o) == c.first) return c.second;
  }
  return "obj";
}

// One line per object: tree path, class, coordinates, hidden.
void dump(lv_obj_t *o, const std::string &path, std::ostringstream &out) {
  lv_area_t a;
  lv_obj_get_coords(o, &a);
  out << path << " " << class_name(o) << " " << a.x1 << "," << a.y1 << "," << a.x2 << "," << a.y2
      << (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN) ? " hidden" : "") << "\n";
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(o); i++) {
    dump(lv_obj_get_child(o, i), path + "/" + std::to_string(i), out);
  }
}

std::string snapshot() {
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  lv_disp_t *disp = lv_disp_get_default();
  std::ostringstream out;
  for (uint32_t s = 0; s < disp->screen_cnt; s++) {
    lv_obj_update_layout(disp->screens[s]);
    dump(disp->screens[s], "s" + std::to_string(s), out);
  }
  lv_obj_update_layout(lv_layer_top());
  dump(lv_layer_top(), "top", out);
  return out.str();
}

std::vector<std::string> lines(const std::string &s) {
  std::vector<std::string> out;
  std::istringstream in(s);
  for (std::string l; std::getline(in, l);) out.push_back(l);
  return out;
}

}  // namespace

TEST_CASE("layout snapshot of every panel is unchanged") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker);
  app::wait_ready(moonraker);
  app::settle(moonraker);

  const std::string path = app::fixture("layout_snapshot.txt");
  std::string now = snapshot();
  const char *update = getenv("UPDATE_SNAPSHOT");
  if (update && *update) {
    std::ofstream(path) << now;
    MESSAGE("wrote " << path);
    return;
  }

  std::ifstream in(path);
  REQUIRE_MESSAGE(in.good(), "no " << path << "; create it with UPDATE_SNAPSHOT=1");
  std::stringstream golden;
  golden << in.rdbuf();

  auto want = lines(golden.str()), got = lines(now);
  int shown = 0;
  std::ostringstream diff;
  for (size_t i = 0; i < std::max(want.size(), got.size()) && shown < 15; i++) {
    std::string w = i < want.size() ? want[i] : "(none)", g = i < got.size() ? got[i] : "(none)";
    if (w != g) {
      diff << "\n  want " << w << "\n  got  " << g;
      shown++;
    }
  }
  CHECK_MESSAGE(want.size() == got.size(), "objects: want " << want.size() << ", got " << got.size());
  CHECK_MESSAGE(shown == 0, "first differences:" << diff.str());
}

namespace {

// "panel item" per line, each once, in the order the panels place them.
std::string placed_items() {
  std::set<std::string> seen;
  std::string out;
  for (const auto &i : GridLayout::placed()) {
    std::string line = i.panel + " " + i.id;
    if (seen.insert(line).second) out += line + "\n";
  }
  return out;
}

}  // namespace

TEST_CASE("layout item list is unchanged") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker);
  app::wait_ready(moonraker);

  const std::string path = app::fixture("layout_items.txt");
  std::string now = placed_items();
  const char *update = getenv("UPDATE_SNAPSHOT");
  if (update && *update) {
    std::ofstream(path) << now;
    MESSAGE("wrote " << path);
    return;
  }
  std::ifstream in(path);
  REQUIRE_MESSAGE(in.good(), "no " << path << "; create it with UPDATE_SNAPSHOT=1");
  std::stringstream golden;
  golden << in.rdbuf();
  CHECK_MESSAGE(golden.str() == now, "the panels place other items than " << path
                << " lists (update it and preview/README.md):\n" << now);
}

TEST_CASE("every layout item can be hidden from the config") {
  // Hide every item in tests/fixtures/layout_items.txt.
  std::ifstream in(app::fixture("layout_items.txt"));
  REQUIRE(in.good());
  json layout = json::object();
  for (std::string panel, id; in >> panel >> id;) {
    layout[panel]["items"][id] = {{"hidden", true}};
  }

  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker, {{"layout", layout}});
  app::wait_ready(moonraker);

  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  std::vector<std::string> shown, uncelled;
  for (const auto &i : GridLayout::placed()) {
    if (!lv_obj_has_flag(i.obj, LV_OBJ_FLAG_HIDDEN)) shown.push_back(i.panel + "." + i.id);
    // Still in its cell: panels show some items themselves (calibration
    // graphs and spinners), and those must appear where they belong.
    if (lv_obj_get_style_grid_cell_column_pos(i.obj, 0) != i.col
        || lv_obj_get_style_grid_cell_row_pos(i.obj, 0) != i.row
        || lv_obj_get_style_grid_cell_column_span(i.obj, 0) != i.col_span
        || lv_obj_get_style_grid_cell_row_span(i.obj, 0) != i.row_span) {
      uncelled.push_back(i.panel + "." + i.id);
    }
  }
  CHECK_MESSAGE(shown.empty(), "not hidden: " << shown.size() << " items, e.g. "
                << (shown.empty() ? "" : shown.front()));
  CHECK_MESSAGE(uncelled.empty(), "hidden without their cell: " << uncelled.size() << " items, e.g. "
                << (uncelled.empty() ? "" : uncelled.front()));
}

TEST_CASE("layout moves an item on another panel") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker, {{"layout", {{"homing", {{"items", {{"home_all", {{"col", 4}, {"row", 1}}}}}}}}}});
  app::wait_ready(moonraker);

  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  lv_obj_t *home_all = nullptr, *motoroff = nullptr;
  for (const auto &i : GridLayout::placed()) {
    if (i.panel == "homing" && i.id == "home_all") home_all = i.obj;
    if (i.panel == "homing" && i.id == "motoroff") motoroff = i.obj;
  }
  REQUIRE(home_all != nullptr);
  REQUIRE(motoroff != nullptr);
  lv_obj_update_layout(lv_obj_get_parent(home_all));
  lv_area_t a, b;
  lv_obj_get_coords(home_all, &a);
  lv_obj_get_coords(motoroff, &b);
  CHECK(a.x1 == b.x1);  // same cell: column 4, row 1
  CHECK(a.y1 == b.y1);
}

TEST_CASE("no layout item lies outside its grid") {
  // LVGL reads past its track arrays for such an item (lv_grid.c item_repos).
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker);
  app::wait_ready(moonraker);

  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  std::vector<std::string> outside;
  for (const auto &i : GridLayout::placed()) {
    if (i.col < 0 || i.row < 0 || i.col + i.col_span > i.grid_cols || i.row + i.row_span > i.grid_rows) {
      lv_area_t a;
      lv_obj_get_coords(i.obj, &a);
      outside.push_back(i.panel + "." + i.id + " at " + std::to_string(i.col) + "," + std::to_string(i.row)
                        + " in " + std::to_string(i.grid_cols) + "x" + std::to_string(i.grid_rows)
                        + ", drawn at " + std::to_string(a.x1) + "," + std::to_string(a.y1));
    }
  }
  CHECK_MESSAGE(outside.empty(), "outside: " << (outside.empty() ? "" : outside.front())
                << (outside.size() > 1 ? " and " + std::to_string(outside.size() - 1) + " more" : ""));
}

TEST_CASE("current layout matches the default layout file") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker);
  app::wait_ready(moonraker);

  const std::string path = app::fixture("layout_default.json");
  json now;
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    now = json(GridLayout::current_layout());
  }
  const char *update = getenv("UPDATE_SNAPSHOT");
  if (update && *update) {
    std::ofstream(path) << GridLayout::current_layout().dump(2) << "\n";
    MESSAGE("wrote " << path);
    return;
  }
  std::ifstream in(path);
  REQUIRE_MESSAGE(in.good(), "no " << path << "; create it with UPDATE_SNAPSHOT=1");
  CHECK(json::parse(in) == now);
}

TEST_CASE("the default layout written out in the config changes nothing") {
  std::ifstream in(app::fixture("layout_default.json"));
  REQUIRE(in.good());
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker, {{"layout", json::parse(in)}});
  app::wait_ready(moonraker);
  app::settle(moonraker);

  std::ifstream golden_in(app::fixture("layout_snapshot.txt"));
  std::stringstream golden;
  golden << golden_in.rdbuf();
  auto want = lines(golden.str()), got = lines(snapshot());
  std::string first;
  for (size_t i = 0; i < std::min(want.size(), got.size()) && first.empty(); i++) {
    if (want[i] != got[i]) first = "want " + want[i] + "\n got " + got[i];
  }
  CHECK(want.size() == got.size());
  CHECK_MESSAGE(first.empty(), first);
}

namespace {

// The item's top-left corner relative to its grid container's.
std::pair<int, int> offset_in_grid(const std::string &panel, const std::string &id) {
  for (const auto &i : GridLayout::placed()) {
    if (i.panel == panel && i.id == id) {
      lv_obj_update_layout(lv_obj_get_parent(i.obj));
      lv_area_t a, p;
      lv_obj_get_coords(i.obj, &a);
      lv_obj_get_coords(lv_obj_get_parent(i.obj), &p);
      return {a.x1 - p.x1, a.y1 - p.y1};
    }
  }
  FAIL("no item " << panel << "." << id);
  return {0, 0};
}

}  // namespace

TEST_CASE("layout padding and gap space the grid") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  json start_top = {{"align_x", "start"}, {"align_y", "start"}};
  app::start(moonraker, {{"layout", {
    {"homing", {{"padding", 0}, {"gap", {{"row", 0}, {"column", 40}}},
                {"items", {{"home_all", start_top}, {"y_up", start_top}}}}},
    // main_panel.cpp sets its own top padding: the config must win.
    {"main", {{"padding", {{"top", 30}, {"left", 5}}}, {"gap", 0},
              {"items", {{"temps", {{"hidden", true}}}, {"homing", start_top}}}}},
  }}});
  app::wait_ready(moonraker);

  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  auto home_all = offset_in_grid("homing", "home_all");
  auto y_up = offset_in_grid("homing", "y_up");
  CHECK(home_all == std::make_pair(0, 0));
  // 740 px (the content area beside the nav bar), 5 columns, 4 gaps of 40:
  // columns of 116, so the next one starts at 156.
  CHECK(y_up.first == 156);
  CHECK(y_up.second == 0);

  auto main_homing = offset_in_grid("main", "homing");
  CHECK(main_homing.second == 30);
}

TEST_CASE("layout padding and gap of the wrong type keep the panel's own") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker, {{"layout", {
    {"homing", {{"padding", "wide"}, {"gap", {{"row", "x"}}}}},
    {"main", {{"padding", {{"top", -3}}}}},
  }}});
  app::wait_ready(moonraker);
  app::settle(moonraker);

  std::ifstream golden_in(app::fixture("layout_snapshot.txt"));
  std::stringstream golden;
  golden << golden_in.rdbuf();
  CHECK(golden.str() == snapshot());
}
