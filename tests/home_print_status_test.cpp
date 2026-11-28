// Home's print status line (thumbnail, ETA, progress bar) lines up with the
// Print tab button on its left when the sensor tiles leave room for it
// (AD5M: one row of tiles): the same top and bottom. With more rows of
// tiles (C5 Pro) it stays under them.

#include "doctest/doctest.h"

#include "app.h"
#include "fake_hw.h"
#include "fake_moonraker.h"
#include "ink.h"
#include "ui.h"

#include "guppyscreen.h"
#include "config.h"

#include <mutex>
#include <ctime>
#include <cstring>
#include <fstream>

namespace {

// The status line around its "ETA: ..." label.
lv_obj_t *status_line() {
  lv_obj_t *label = ui::find_label("ETA:", ui::Match::Contains);
  REQUIRE_MESSAGE(label != nullptr, ui::dump_screen());
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  return lv_obj_get_parent(label);
}

void print_on_home(FakeMoonraker &moonraker) {
  moonraker.push_status({{"print_stats", {{"filename", "cube.gcode"}, {"state", "printing"}}}});
  app::settle(moonraker);
  ui::tap_tab(Nav::HOME);
  app::settle(moonraker);
}

}  // namespace

TEST_CASE("the AD5M print status on Home lines up with the Print tab") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker);
  app::wait_ready(moonraker);
  print_on_home(moonraker);

  lv_area_t status = ui::coords(status_line());
  lv_area_t tab = ui::coords(Nav::get().tab_button(Nav::PRINT));
  INFO("status y " << status.y1 << ".." << status.y2 << "; Print tab y " << tab.y1 << ".." << tab.y2);
  CHECK(status.y1 == tab.y1);
  CHECK(status.y2 == tab.y2);
}

TEST_CASE("the C5 Pro print status on Home stays under its sensor tiles") {
  FakeMoonraker moonraker(app::fixture("c5pro_basic.json"));
  app::start(moonraker, app::zmod_sensors("guppyconfig_c5pro.json", "C5Pro"));
  app::wait_ready(moonraker);
  print_on_home(moonraker);

  lv_area_t status = ui::coords(status_line());
  lv_area_t tile = ui::coords(ui::tile("T3"));
  INFO("status y " << status.y1 << ".." << status.y2 << "; tile T3 y " << tile.y1 << ".." << tile.y2);
  CHECK(status.y1 > tile.y2);
  CHECK(status.y2 - status.y1 == ui::coords(Nav::get().tab_button(Nav::PRINT)).y2 -
                                     ui::coords(Nav::get().tab_button(Nav::PRINT)).y1);
}

TEST_CASE("a tap on the progress bar of Home's print status opens the print screen") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker);
  app::wait_ready(moonraker);
  print_on_home(moonraker);

  lv_obj_t *line = status_line();
  lv_obj_t *found;
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    found = ui::find_in(line, &lv_bar_class);
  }
  REQUIRE(found != nullptr);
  lv_area_t bar = ui::coords(found);
  ui::tap_at((bar.x1 + bar.x2) / 2, (bar.y1 + bar.y2) / 2);
  CHECK_MESSAGE(app::wait_until([] { return ui::find_label("Pause") != nullptr; }), ui::dump_screen());
}

TEST_CASE("Home shows a large clock only when not printing or paused") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker);
  app::wait_ready(moonraker);

  lv_obj_t *hours = nullptr, *minutes = nullptr, *colon = nullptr;
  lv_obj_t *line = nullptr;
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    ui::walk(Nav::get().root(Nav::HOME), [&](lv_obj_t *obj) {
      if (lv_obj_check_type(obj, &lv_label_class) &&
          lv_obj_get_style_text_font(obj, 0)->dsc == lv_font_montserrat_48.dsc) {
        if (!strcmp(lv_label_get_text(obj), ":")) colon = obj;
        else if (!hours) hours = obj;
        else minutes = obj;
      }
    });
    REQUIRE(hours != nullptr);
    REQUIRE(minutes != nullptr);
    REQUIRE(colon != nullptr);
    const auto *font = lv_obj_get_style_text_font(hours, 0);
    const auto width = lv_font_get_glyph_width(font, '0', 0);
    for (uint32_t digit = '0'; digit <= '9'; ++digit) {
      CHECK(lv_font_get_glyph_width(font, digit, 0) == width);
      CHECK(lv_font_get_glyph_width(font, digit, ':') == width);
      CHECK(lv_font_get_glyph_width(font, ':', digit) == lv_font_get_glyph_width(font, ':', 0));
      for (uint32_t next = '0'; next <= '9'; ++next) {
        CHECK(lv_font_get_glyph_width(font, digit, next) == width);
      }
    }
    CHECK(lv_font_get_glyph_width(font, '-', '-') == width);
    line = lv_obj_get_parent(hours);
    CHECK(lv_obj_get_parent(minutes) == line);
    CHECK(lv_obj_get_parent(colon) == line);
    CHECK_FALSE(lv_obj_has_flag(line, LV_OBJ_FLAG_HIDDEN));
    CHECK_FALSE(lv_obj_has_flag(line, LV_OBJ_FLAG_CLICKABLE));
    CHECK_FALSE(lv_obj_has_flag(hours, LV_OBJ_FLAG_HIDDEN));
    CHECK_FALSE(lv_obj_has_flag(minutes, LV_OBJ_FLAG_HIDDEN));
    CHECK(lv_obj_has_flag(colon, LV_OBJ_FLAG_FLOATING));
  }
  REQUIRE(app::wait_until([&] {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    const auto now = std::time(nullptr);
    for (auto stamp : {now, now - 1}) {
      std::tm local;
      char expected[6];
      if (localtime_r(&stamp, &local) && std::strftime(expected, sizeof(expected), "%H:%M", &local) == 5 &&
          !strncmp(lv_label_get_text(hours), expected, 2) && !strcmp(lv_label_get_text(minutes), expected + 3) &&
          strlen(lv_label_get_text(hours)) == 2) {
        return true;
      }
    }
    return false;
  }));
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    CHECK_FALSE(lv_obj_has_flag(colon, LV_OBJ_FLAG_HIDDEN));
  }
  lv_area_t idle = ui::coords(line);
  lv_area_t left = ui::coords(hours);
  lv_area_t right = ui::coords(minutes);
  lv_area_t separator = ui::coords(colon);
  for (int i = 0; i < 2; ++i) {
    app::pump(1100);
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    CHECK_FALSE(lv_obj_has_flag(colon, LV_OBJ_FLAG_HIDDEN));
  }
  for (auto *obj : {hours, minutes, colon}) {
    lv_area_t digits = ui::coords(obj);
    lv_area_t before = obj == hours ? left : obj == minutes ? right : separator;
    CHECK(digits.x1 == before.x1);
    CHECK(digits.x2 == before.x2);
    CHECK(digits.y1 == before.y1);
    CHECK(digits.y2 == before.y2);
    CHECK(digits.x1 >= idle.x1);
    CHECK(digits.x2 <= idle.x2);
    CHECK(digits.y1 >= idle.y1);
    CHECK(digits.y2 <= idle.y2);
  }
  CHECK(left.x2 < separator.x1);
  CHECK(separator.x2 < right.x1);
  ui::tap(line);
  CHECK(Nav::get().active() == Nav::HOME);

  for (const char *state : {"printing", "paused", "complete", "error", "cancelled"}) {
    moonraker.push_status({{"print_stats", {{"filename", "cube.gcode"}, {"state", state}}}});
    app::settle(moonraker);
    ui::tap_tab(Nav::HOME);
    const bool printing = !strcmp(state, "printing") || !strcmp(state, "paused");
    {
      std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
      CHECK(lv_obj_has_flag(hours, LV_OBJ_FLAG_HIDDEN) == printing);
      CHECK(lv_obj_has_flag(minutes, LV_OBJ_FLAG_HIDDEN) == printing);
      CHECK(lv_obj_has_flag(colon, LV_OBJ_FLAG_HIDDEN) == printing);
      CHECK(lv_obj_has_flag(line, LV_OBJ_FLAG_CLICKABLE) == printing);
      CHECK_FALSE(lv_obj_has_flag(line, LV_OBJ_FLAG_HIDDEN));
    }
    if (printing) {
      CHECK(status_line() == line);
      ui::tap(line);
      CHECK(Nav::get().active() == Nav::PRINT);
    } else {
      ui::tap(line);
      CHECK(Nav::get().active() == Nav::HOME);
    }
  }
}

TEST_CASE("System Clock switch persists and hides both clocks but not print status") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker);
  app::wait_ready(moonraker);

  lv_obj_t *home_clock = nullptr;
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    ui::walk(Nav::get().root(Nav::HOME), [&](lv_obj_t *obj) {
      if (lv_obj_check_type(obj, &lv_label_class) && !strcmp(lv_label_get_text(obj), ":") &&
          lv_obj_get_style_text_font(obj, 0)->dsc == lv_font_montserrat_48.dsc)
        home_clock = lv_obj_get_parent(obj);
    });
  }
  REQUIRE(home_clock != nullptr);

  ui::tap_tab(Nav::SETTINGS);
  ui::tap_label("System");
  lv_obj_t *label = ui::find_label("Clock");
  REQUIRE_MESSAGE(label != nullptr, ui::dump_screen());
  lv_obj_t *toggle = nullptr;
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    toggle = ui::find_in(lv_obj_get_parent(label), &lv_switch_class);
  }
  REQUIRE(toggle != nullptr);
  CHECK(lv_obj_has_state(toggle, LV_STATE_CHECKED));

  ui::tap(toggle);
  CHECK_FALSE(Config::get_instance()->get<bool>("/show_clock"));
  CHECK(json::parse(std::ifstream(app::dir() + "/guppyconfig.json"))["show_clock"] == false);
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    CHECK(lv_obj_has_flag(home_clock, LV_OBJ_FLAG_HIDDEN));
  }

  moonraker.push_status({{"print_stats", {{"filename", "cube.gcode"}, {"state", "printing"}}}});
  app::settle(moonraker);
  ui::tap_tab(Nav::HOME);
  CHECK(status_line() == home_clock);
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    CHECK_FALSE(lv_obj_has_flag(home_clock, LV_OBJ_FLAG_HIDDEN));
  }

  lv_obj_t *print_clock = nullptr;
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    ui::walk(Nav::get().root(Nav::PRINT), [&](lv_obj_t *obj) {
      if (lv_obj_check_type(obj, &lv_label_class) && !strcmp(lv_label_get_text(obj), "--:--:--"))
        print_clock = obj;
    });
  }
  REQUIRE(print_clock != nullptr);
  CHECK(lv_obj_has_flag(print_clock, LV_OBJ_FLAG_HIDDEN));

  ui::tap_tab(Nav::SETTINGS);
  ui::tap_label("System");
  ui::tap(toggle);
  CHECK(Config::get_instance()->get<bool>("/show_clock"));
  CHECK(json::parse(std::ifstream(app::dir() + "/guppyconfig.json"))["show_clock"] == true);
  ui::tap_tab(Nav::PRINT);
  CHECK_FALSE(lv_obj_has_flag(print_clock, LV_OBJ_FLAG_HIDDEN));
  CHECK(app::wait_until([&] {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    return strcmp(lv_label_get_text(print_clock), "--:--:--") != 0;
  }));
}

TEST_CASE("Clock disabled in configuration stays hidden on startup") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker, {{"show_clock", false}});
  app::wait_ready(moonraker);
  CHECK_FALSE(Config::get_instance()->get<bool>("/show_clock"));

  lv_obj_t *home_clock = nullptr;
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    ui::walk(Nav::get().root(Nav::HOME), [&](lv_obj_t *obj) {
      if (lv_obj_check_type(obj, &lv_label_class) && !strcmp(lv_label_get_text(obj), ":") &&
          lv_obj_get_style_text_font(obj, 0)->dsc == lv_font_montserrat_48.dsc)
        home_clock = lv_obj_get_parent(obj);
    });
  }
  REQUIRE(home_clock != nullptr);
  CHECK(lv_obj_has_flag(home_clock, LV_OBJ_FLAG_HIDDEN));
  moonraker.push_status({{"print_stats", {{"filename", "cube.gcode"}, {"state", "printing"}}}});
  app::settle(moonraker);
  ui::tap_tab(Nav::HOME);
  CHECK_FALSE(lv_obj_has_flag(home_clock, LV_OBJ_FLAG_HIDDEN));
  bool status_visible = false;
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    CHECK(lv_obj_has_flag(home_clock, LV_OBJ_FLAG_CLICKABLE));
    ui::walk(home_clock, [&](lv_obj_t *obj) {
      if (lv_obj_check_type(obj, &lv_label_class) &&
          strstr(lv_label_get_text(obj), "ETA:") &&
          !lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN))
        status_visible = true;
    });
  }
  CHECK(status_visible);
}

TEST_CASE("Home's print status shows new progress once per second") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker);
  app::wait_ready(moonraker);
  print_on_home(moonraker);
  lv_obj_t *line = status_line();
  lv_obj_t *bar = nullptr;
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(line) && bar == nullptr; i++) {
      if (lv_obj_check_type(lv_obj_get_child(line, i), &lv_bar_class)) bar = lv_obj_get_child(line, i);
    }
  }
  REQUIRE(bar != nullptr);
  auto value = [&] {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    return lv_bar_get_value(bar);
  };

  // Отсчитываем интервал от первого показанного значения.
  moonraker.push_status({{"virtual_sdcard", {{"progress", 0.10}}}});
  REQUIRE(app::wait_until([&] { return value() == 10; }));
  moonraker.push_status({{"virtual_sdcard", {{"progress", 0.20}}}});
  app::pump(300);
  moonraker.push_status({{"virtual_sdcard", {{"progress", 0.30}}}});
  app::pump(300);
  CHECK(value() == 10);
  REQUIRE(app::wait_until([&] { return value() != 10; }, 900));
  CHECK(value() == 30);  // оба одним обновлением
}

TEST_CASE("Home's print status shows a new print state at once") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker);
  app::wait_ready(moonraker);
  print_on_home(moonraker);
  CHECK_MESSAGE(ui::find_label("Status: printing", ui::Match::Contains) != nullptr, ui::dump_screen());
  moonraker.push_status({{"print_stats", {{"state", "paused"}}}});
  // Без ожидания секундного обновления.
  CHECK_MESSAGE(app::wait_until([] { return ui::find_label("Status: paused", ui::Match::Contains) != nullptr; }, 300),
                ui::dump_screen());
}

namespace {

// График Home со шкалой 0–top°: пунктир через 100°, цифры через 50°
// справа. Приложение уже запущено, Home на экране.
void check_home_chart(int top) {
  CAPTURE(top);
  // Плитка часов (она же статус печати) и график Home.
  lv_obj_t *colon = ui::find_label(":");
  REQUIRE_MESSAGE(colon != nullptr, ui::dump_screen());
  struct { lv_obj_t *clock, *chart; } t;
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    t = {lv_obj_get_parent(colon), ui::find_in(Nav::get().root(Nav::HOME), &lv_chart_class)};
  }
  REQUIRE(t.chart != nullptr);
  app::redraw();
  const lv_area_t clock = ui::coords(t.clock), plot = ui::coords(t.chart);
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  const lv_chart_t *chart = (const lv_chart_t *)t.chart;
  CHECK(chart->hdiv_cnt == 0);
  CHECK(chart->vdiv_cnt == 0);
  CHECK(!chart->tick[0].label_en);
  CHECK(!chart->tick[2].label_en);
  CHECK(chart->tick[0].major_cnt == 0);
  CHECK(chart->tick[2].major_cnt == 0);
  const lv_font_t *font = lv_obj_get_style_text_font(t.chart, LV_PART_TICKS);
  CHECK(lv_font_get_line_height(font) == 26);  // Noto SemiBold 20 px
  CHECK(lv_obj_get_style_text_letter_space(t.chart, LV_PART_TICKS) == 0);
  // Шкала справа внутри графика, график слева вровень с плитками, шириной с плитку часов.
  CHECK(plot.x1 == clock.x1);
  CHECK(plot.x2 == clock.x2);
  // Исходный серый цвет темы.
  CHECK(lv_color_to32(lv_obj_get_style_text_color(t.chart, LV_PART_TICKS)) ==
        lv_color_to32(lv_palette_main(LV_PALETTE_GREY)));
  lv_area_t c;
  lv_obj_get_content_coords(t.chart, &c);
  // Шкала на фоне графика: у правого края колонки тот же цвет, что в пустом
  // месте графика (выше линии 200°, над кривыми).
  const int y_empty = c.y1 + (c.y2 - c.y1) / 6;
  CHECK(c.x2 < clock.x2 - 20);  // справа от графика место под цифры
  // Над областью графика половина строки фона, как у Home без шкалы.
  CHECK(c.y1 - plot.y1 >= lv_font_get_line_height(lv_obj_get_style_text_font(t.chart, LV_PART_TICKS)) / 2);
  CHECK(fake_hw::pixel(clock.x2 - 2, y_empty).full == fake_hw::pixel(c.x1 + 2, y_empty).full);
  const lv_color_t line = lv_obj_get_style_text_color(t.chart, LV_PART_TICKS);
  // Доля пикселей строки цвета линии сетки (график температур занимает лишь несколько).
  auto share = [&](int y, lv_color_t colour) {
    int n = 0;
    for (int x = c.x1; x <= c.x2; x++) n += fake_hw::pixel(x, y).full == colour.full;
    return (double)n / (c.x2 - c.x1 + 1);
  };
  auto line_share = [&](int y) { return share(y, line); };
  const int h = c.y2 - c.y1 + 1;
  // Пунктир через 100° цветом шкалы, 1 px толщиной.
  for (int deg = 100; deg < top; deg += 100) {
    CAPTURE(deg);
    const int y = c.y1 + h * (top - deg) / top;
    int rows = 0;
    for (int yy = y - 3; yy <= y + 3; yy++) {
      const double fraction = line_share(yy);
      if (fraction < 0.35 || fraction > 0.75) continue;
      int transitions = 0;
      for (int x = c.x1 + 1; x <= c.x2; x++)
        transitions += (fake_hw::pixel(x, yy).full == line.full) !=
                       (fake_hw::pixel(x - 1, yy).full == line.full);
      CHECK(transitions > (c.x2 - c.x1) / 8);
      rows++;
    }
    CHECK(rows == 1);
  }
  for (int deg = (top > 300 ? 310 : 300); deg <= top; deg += 10) {  // от 300° до верха: ни серой, ни чёрной линии
    CAPTURE(deg);
    const int y = c.y1 + h * (top - deg) / top;
    CHECK(line_share(y) < 0.1);
    CHECK(share(y, lv_color_black()) < 0.1);
  }
  for (int y = c.y2 - 2; y <= c.y2 + 1; y++) {  // 0°: ни серой, ни чёрной линии
    CAPTURE(y);
    CHECK(line_share(y) < 0.1);
    CHECK(share(y, lv_color_black()) < 0.1);
  }
  CHECK(line_share(c.y1 + h / 2) < 0.1);  // 150°, как было раньше
  // Цифры шкалы через 50° вровень по правому краю: 50 кончается там же, где 250 и 150.
  const uint32_t bg = fake_hw::pixel(clock.x2 - 2, y_empty).full;
  // До скругления и рамки графика (тема): они не фон.
  const int x_end = plot.x2 - std::max(lv_obj_get_style_radius(t.chart, 0), lv_obj_get_style_border_width(t.chart, 0));
  const int y_end = plot.y2 - lv_obj_get_style_border_width(t.chart, 0);  // над нижней рамкой
  auto label_right = [&](int deg) {
    const int y = c.y1 + (h - 1) * (top - deg) / top;
    int right = -1;
    for (int yy = y - 5; yy <= std::min(y + 5, y_end); yy++)  // ниже графика — фон экрана
      for (int x = c.x2 + 1; x <= x_end; x++)
        if (fake_hw::pixel(x, yy).full != bg) right = std::max(right, x);
    return right;
  };
  const int right = label_right(250);  // 250 на шкале при любом верхе
  CHECK(right > c.x2);
  for (int deg = 50; deg <= top; deg += 50) {
    CAPTURE(deg);
    CHECK(label_right(deg) == right);
  }
  CHECK(chart->ymax[0] == top);
  CHECK(chart->ymax[1] == top);
  if (top < 300) {  // над верхом шкалы цифр нет: ни 300, ни других
    const int y250 = c.y1 + (h - 1) * (top - 250) / top;
    const lv_area_t above = {c.x2 + 1, (lv_coord_t)(plot.y1 + lv_obj_get_style_border_width(t.chart, 0)),  // под рамкой
                             (lv_coord_t)x_end, (lv_coord_t)(y250 - 11)};
    CHECK(ink::rows(above, above.x1, above.x2, [&](int, int) { lv_color_t b; b.full = bg; return b; }).top == -1);
  }
  // У нижнего края шкалы пусто: ни цифры 0, ни полосы прокрутки растянутого графика.
  const int y0 = c.y1 + h - 1;
  CHECK(label_right(0) == -1);
  for (int x = c.x2 + 1; x <= x_end; x++)
    for (int y = y0 - 3; y <= std::min(y0 + 3, y_end); y++) {
      CAPTURE(x);
      CAPTURE(y);
      CHECK(fake_hw::pixel(x, y).full == bg);
    }
}

}  // namespace

TEST_CASE("Home's chart has dashed grid lines at 100 and 200 degrees and its scale by 50 on the right") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker);
  app::wait_ready(moonraker);
  check_home_chart(300);  // AD5M: по умолчанию до 300°
}

namespace {

// Settings → System: выпадающий список «Max graph temperature».
lv_obj_t *open_chart_max_setting() {
  ui::tap_tab(Nav::SETTINGS);
  ui::tap_label("System");
  lv_obj_t *row = ui::label_container("Max graph temperature");
  REQUIRE_MESSAGE(row != nullptr, ui::dump_screen());
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  lv_obj_t *dd = ui::find_in(row, &lv_dropdown_class);
  REQUIRE(dd != nullptr);
  return dd;
}

// Выбранный пункт списка. Держать блокировку LVGL.
std::string selected(lv_obj_t *dd) {
  char buf[16];
  lv_dropdown_get_selected_str(dd, buf, sizeof(buf));
  return buf;
}

}  // namespace

TEST_CASE("Home's chart on the C5 Pro goes up to 330 degrees by default") {
  FakeMoonraker moonraker(app::fixture("c5pro_basic.json"));
  app::start(moonraker, app::zmod_sensors("guppyconfig_c5pro.json", "C5Pro"));
  app::wait_ready(moonraker);
  check_home_chart(330);  // несколько экструдеров: C5
  lv_obj_t *dd = open_chart_max_setting();
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  CHECK(selected(dd) == "330");
}

TEST_CASE("System's Max graph temperature sets the top of Home's chart and persists") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker);
  app::wait_ready(moonraker);
  lv_obj_t *dd = open_chart_max_setting();
  // Строка сразу под «Log level».
  lv_obj_t *row = ui::label_container("Max graph temperature"), *log_level = ui::label_container("Log level");
  REQUIRE(log_level != nullptr);
  CHECK(ui::coords(row).y1 > ui::coords(log_level).y2);
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    CHECK(lv_obj_get_index(row) == lv_obj_get_index(log_level) + 1);
    CHECK(std::string(lv_dropdown_get_options(dd)) == "280\n300\n320\n330\n350");
    CHECK(selected(dd) == "300");  // AD5M: по умолчанию 300
    // Выбор в списке: то же, что нажатие на пункт.
    lv_dropdown_set_selected(dd, 2);
    lv_event_send(dd, LV_EVENT_VALUE_CHANGED, NULL);
  }
  ui::tap_tab(Nav::HOME);
  check_home_chart(320);
  dd = open_chart_max_setting();
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    lv_dropdown_set_selected(dd, 4);
    lv_event_send(dd, LV_EVENT_VALUE_CHANGED, NULL);
  }
  CHECK(Config::get_instance()->get_json("/chart_max_temp") == 350);
  CHECK(json::parse(std::ifstream(app::dir() + "/guppyconfig.json"))["chart_max_temp"] == 350);
  ui::tap_tab(Nav::HOME);
  check_home_chart(350);
}

TEST_CASE("a saved Max graph temperature sets Home's chart at start") {
  FakeMoonraker moonraker(app::fixture("c5pro_basic.json"));
  app::start(moonraker, [] {
    json config = app::zmod_sensors("guppyconfig_c5pro.json", "C5Pro");
    config["chart_max_temp"] = 280;
    return config;
  }());
  app::wait_ready(moonraker);
  check_home_chart(280);  // настройка, а не 330 по умолчанию для C5
}
