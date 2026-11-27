// Фон панелей: панели, залитые плоским цветом карточки темы, сохраняют его
// (#282B30, как в upstream) или заливаются плиткой из PNG, выбранного в
// "background" в guppyconfig.json (backgrounds/<имя>.png рядом), на одной
// сетке от начала координат экрана (patches/0008), так что соседние
// контейнеры стыкуются без швов. "none" и "translucent" — тоже цвет карточки
// (с непрозрачными или полупрозрачными плитками). Прозрачные пиксели PNG
// показывают цвет карточки.

#include "doctest/doctest.h"

#include "app.h"
#include "fake_hw.h"
#include "fake_moonraker.h"
#include "png_writer.h"
#include "ui.h"

#include "guppyscreen.h"

#include <fstream>
#include <functional>
#include <mutex>
#include <set>
#include <sys/stat.h>

namespace {

const lv_color_t CARD = lv_color_make(0x28, 0x2B, 0x30);  // цвет панелей темы

// Две пустые области вкладки Home в разных контейнерах (её левая и правая
// половины, начинающиеся с разных x).
struct Area { int x1, y1, x2, y2; };
const Area BLANK[] = {{110, 200, 420, 270}, {440, 140, 799, 180}};

// Сверяет каждый пиксель пустых областей с expected(x, y); возвращает число
// встреченных разных цветов.
size_t check_blank(const std::function<lv_color_t(int, int)> &expected) {
  std::set<uint32_t> colours;
  for (Area a : BLANK) {
    CAPTURE(a.x1);
    int differ = 0;
    for (int y = a.y1; y < a.y2; y++)
      for (int x = a.x1; x < a.x2; x++) {
        lv_color_t c = fake_hw::pixel(x, y);
        colours.insert(c.full);
        differ += c.full != expected(x, y).full;
      }
    CHECK(differ == 0);
  }
  return colours.size();
}

// Пишет backgrounds/<name> (`write(path)`) и выбирает его в guppyconfig.json.
void choose_file(const std::string &dir, json &config, const std::string &name,
                 const std::function<void(const std::string &)> &write) {
  mkdir((dir + "/backgrounds").c_str(), 0755);
  write(dir + "/backgrounds/" + name);
  config["background"] = "backgrounds/" + name;
}

// Только сигнатура и блок IHDR PNG `w` x `h`, без данных: не раскодируется.
void write_png_header(const std::string &path, unsigned w, unsigned h) {
  const unsigned char head[] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n', 0, 0, 0, 13, 'I', 'H', 'D', 'R',
                                0, 0, (unsigned char)(w >> 8), (unsigned char)w,
                                0, 0, (unsigned char)(h >> 8), (unsigned char)h, 8, 6, 0, 0, 0};
  std::ofstream(path, std::ios::binary).write((const char *)head, sizeof(head));
}

size_t log_count(const std::string &text) {
  std::ifstream log(app::dir() + "/guppyscreen.log");
  std::string s((std::istreambuf_iterator<char>(log)), std::istreambuf_iterator<char>());
  size_t n = 0;
  for (size_t i = s.find(text); i != std::string::npos; i = s.find(text, i + 1)) n++;
  return n;
}

void start(FakeMoonraker &moonraker, const std::function<void(const std::string &, json &)> &setup = nullptr) {
  app::start(moonraker, json::object(), setup);
  app::wait_ready(moonraker);
  app::redraw();
}

}  // namespace

TEST_CASE("without a background PNG the panels keep the theme's card colour") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start(moonraker);
  CHECK(GuppyScreen::background_mode() == GuppyScreen::BG_THEME);
  CHECK(GuppyScreen::bg_tile_src() == nullptr);
  CHECK(check_blank([](int, int) { return CARD; }) == 1);
}

TEST_CASE("a background PNG is tiled once on one screen grid") {
  // 72 x 66 (не степень двойки, не квадрат), слева непрозрачный, справа
  // полупрозрачный: смешивается с цветом карточки.
  const int W = 72, H = 66;
  std::vector<unsigned char> rgba;
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) rgba.insert(rgba.end(), {(unsigned char)(x * 6), (unsigned char)(y * 8), 200,
                                                         (unsigned char)(x < W / 2 ? 255 : 128)});
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start(moonraker, [&](const std::string &dir, json &config) {
    choose_file(dir, config, "t.png", [&](const std::string &path) { test_png::write(path, rgba, W, H); });
  });
  CHECK(GuppyScreen::background_mode() == GuppyScreen::BG_TILE);
  const lv_img_dsc_t *t = GuppyScreen::bg_tile_src();
  REQUIRE(t != nullptr);
  CHECK(t->header.w == W);
  CHECK(t->header.h == H);

  // Плитка по сетке экрана в обоих контейнерах.
  CHECK(check_blank([&](int x, int y) {
          const unsigned char *p = &rgba[((y % H) * W + x % W) * 4];
          return lv_color_mix(lv_color_make(p[0], p[1], p[2]), CARD, p[3]);
        }) > 3);

  // Каждая область покрывается плиткой один раз: серый контейнер внутри
  // заполненного плиткой прозрачен, а не рисует её снова. Кроме графиков
  // (GuppyScreen::own_bg), которые разворачиваются поверх соседей.
  int tiled = 0, nested = 0;
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    std::function<void(lv_obj_t *, bool)> walk = [&](lv_obj_t *o, bool inside) {
      bool b = lv_obj_get_style_bg_img_src(o, 0) == t;
      tiled += b;
      nested += b && inside && !lv_obj_has_flag(o, LV_OBJ_FLAG_CLICKABLE);
      for (uint32_t i = 0; i < lv_obj_get_child_cnt(o); i++) walk(lv_obj_get_child(o, i), inside || b);
    };
    walk(lv_scr_act(), false);
  }
  MESSAGE(tiled << " tiled containers");
  CHECK(tiled > 0);
  CHECK(nested == 0);
}

TEST_CASE("background none keeps the flat theme colour") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start(moonraker, [](const std::string &, json &config) { config["background"] = "none"; });
  CHECK(GuppyScreen::background_mode() == GuppyScreen::BG_THEME);
  CHECK(check_blank([](int, int) { return CARD; }) == 1);
}

TEST_CASE("background translucent keeps the flat theme colour") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start(moonraker, [](const std::string &, json &config) { config["background"] = "translucent"; });
  CHECK(GuppyScreen::background_mode() == GuppyScreen::BG_TRANSLUCENT);
  CHECK(GuppyScreen::bg_tile_src() == nullptr);
  CHECK(check_blank([](int, int) { return CARD; }) == 1);
}

TEST_CASE("an unreadable background PNG gives the card colour") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start(moonraker, [](const std::string &dir, json &config) {
    choose_file(dir, config, "broken.png", [](const std::string &path) { std::ofstream(path) << "not a png"; });
  });
  CHECK(GuppyScreen::background_mode() == GuppyScreen::BG_TRANSLUCENT);  // выбрана текстура: плитки полупрозрачны
  CHECK(check_blank([](int, int) { return CARD; }) == 1);
}

// Settings > System > Background: None, Translucent, затем backgrounds/*.png по
// имени. Выбор сразу показывается и сохраняется в guppyconfig.json.
namespace {

std::vector<unsigned char> solid(int w, int h, unsigned char r, unsigned char g, unsigned char b) {
  std::vector<unsigned char> rgba;
  for (int i = 0; i < w * h; i++) rgba.insert(rgba.end(), {r, g, b, 255});
  return rgba;
}

// backgrounds/a.png (64 x 64, красный), b.png (80 x 72, синий), слишком
// маленький PNG и файл, который не PNG.
void write_backgrounds(const std::string &dir) {
  std::string d = dir + "/backgrounds";
  REQUIRE(mkdir(d.c_str(), 0755) == 0);
  test_png::write(d + "/a.png", solid(64, 64, 200, 0, 0), 64, 64);
  test_png::write(d + "/b.png", solid(80, 72, 0, 0, 200), 80, 72);
  test_png::write(d + "/c_small.png", solid(63, 64, 0, 200, 0), 63, 64);
  std::ofstream(d + "/notes.txt") << "not a background";
}

// "background" в guppyconfig.json, как записан.
json saved_background() {
  std::ifstream f(app::dir() + "/guppyconfig.json");
  json config = json::parse(f);
  return config.contains("background") ? config["background"] : json();
}

// Открывает Settings > System > Background (список читается заново при
// входе в System), сверяет варианты с `entries` и нажимает `entry`. Затем назад на Home.
void choose_background(const std::string &entries, const std::string &entry) {
  ui::tap_tab(Nav::SETTINGS);
  ui::tap_label("System");
  lv_obj_t *label = ui::find_label("Background");
  REQUIRE_MESSAGE(label != nullptr, ui::dump_screen());
  lv_obj_t *dd;
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    dd = ui::find_in(lv_obj_get_parent(label), &lv_dropdown_class);
  }
  REQUIRE(dd != nullptr);
  ui::tap(dd);

  std::string options;
  lv_obj_t *list;
  int idx = -1, n = 0, row = 0, top;
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    options = lv_dropdown_get_options(dd);
    list = lv_dropdown_get_list(dd);
    REQUIRE(list != nullptr);
    row = lv_font_get_line_height(lv_obj_get_style_text_font(list, LV_PART_MAIN)) +
          lv_obj_get_style_text_line_space(list, LV_PART_MAIN);
  }
  CHECK(options == entries);
  for (size_t start = 0;; n++) {
    size_t end = options.find('\n', start);
    if (options.substr(start, end - start) == entry) idx = n;
    if (end == std::string::npos) break;
    start = end + 1;
  }
  REQUIRE_MESSAGE(idx >= 0, entry << " not in " << options);
  {
    // Список открыт прокрученным к выбранному пункту: прокрутить к нужному, как пальцем.
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    lv_obj_scroll_to_y(list, idx * row, LV_ANIM_OFF);
    top = lv_obj_get_style_pad_top(list, LV_PART_MAIN) - lv_obj_get_scroll_y(list);
  }
  lv_area_t a = ui::coords(list);
  ui::tap_at((a.x1 + a.x2) / 2, a.y1 + top + idx * row + row / 2);
  ui::tap_tab(Nav::HOME);
  app::redraw();
}

}  // namespace

TEST_CASE("Settings > System > Background lists the backgrounds and saves the choice") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start(moonraker, [](const std::string &dir, json &) { write_backgrounds(dir); });
  REQUIRE(GuppyScreen::background_mode() == GuppyScreen::BG_THEME);
  CHECK(saved_background().is_null());

  const std::string head = "None\nTranslucent\n", files = "a.png\nb.png\nc_small.png";
  choose_background(head + files, "b.png");
  CHECK(GuppyScreen::background_mode() == GuppyScreen::BG_TILE);
  REQUIRE(GuppyScreen::bg_tile_src() != nullptr);
  CHECK(GuppyScreen::bg_tile_src()->header.w == 80);
  CHECK(check_blank([](int, int) { return lv_color_make(0, 0, 200); }) == 1);
  CHECK(saved_background() == "backgrounds/b.png");

  // Загруженный позже файл появляется при следующем входе в System.
  test_png::write(app::dir() + "/backgrounds/0.png", solid(64, 64, 0, 200, 0), 64, 64);
  choose_background(head + "0.png\n" + files, "a.png");
  REQUIRE(GuppyScreen::bg_tile_src() != nullptr);
  CHECK(GuppyScreen::bg_tile_src()->header.w == 64);
  CHECK(check_blank([](int, int) { return lv_color_make(200, 0, 0); }) == 1);

  // Негодный файл ничего не меняет: a.png остаётся показанным, сохранённым и
  // выбранным в списке.
  choose_background(head + "0.png\n" + files, "c_small.png");
  REQUIRE(GuppyScreen::bg_tile_src() != nullptr);
  CHECK(GuppyScreen::bg_tile_src()->header.w == 64);
  CHECK(check_blank([](int, int) { return lv_color_make(200, 0, 0); }) == 1);
  CHECK(saved_background() == "backgrounds/a.png");
  ui::tap_tab(Nav::SETTINGS);
  ui::tap_label("System");
  lv_obj_t *label = ui::find_label("Background");
  REQUIRE(label != nullptr);
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    lv_obj_t *dd = ui::find_in(lv_obj_get_parent(label), &lv_dropdown_class);
    CHECK(lv_dropdown_get_selected(dd) == 3);  // None, Translucent, 0.png, a.png
  }

  choose_background(head + "0.png\n" + files, "b.png");
  choose_background(head + "0.png\n" + files, "Translucent");
  CHECK(GuppyScreen::background_mode() == GuppyScreen::BG_TRANSLUCENT);
  CHECK(GuppyScreen::bg_tile_src() == nullptr);
  CHECK(check_blank([](int, int) { return CARD; }) == 1);
  CHECK(saved_background() == "translucent");
  choose_background(head + "0.png\n" + files, "None");
  CHECK(GuppyScreen::background_mode() == GuppyScreen::BG_THEME);
  CHECK(GuppyScreen::bg_tile_src() == nullptr);
  CHECK(check_blank([](int, int) { return CARD; }) == 1);
  CHECK(saved_background() == "none");
}

TEST_CASE("the saved background is shown at startup and selected in the list") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start(moonraker, [](const std::string &dir, json &config) {
    write_backgrounds(dir);
    test_png::write(dir + "/backgrounds/a_long_name.png", solid(64, 64, 200, 0, 0), 64, 64);
    config["background"] = "backgrounds/a_long_name.png";
  });
  CHECK(GuppyScreen::background_mode() == GuppyScreen::BG_TILE);
  REQUIRE(GuppyScreen::bg_tile_src() != nullptr);
  CHECK(GuppyScreen::bg_tile_src()->header.w == 64);
  CHECK(check_blank([](int, int) { return lv_color_make(200, 0, 0); }) == 1);

  ui::tap_tab(Nav::SETTINGS);
  ui::tap_label("System");
  lv_obj_t *label = ui::find_label("Background");
  REQUIRE(label != nullptr);
  lv_obj_t *chart_max = ui::label_container("Max graph temperature");
  REQUIRE(chart_max != nullptr);
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  CHECK(lv_obj_get_index(lv_obj_get_parent(label)) == lv_obj_get_index(chart_max) + 1);  // строкой под ней
  lv_obj_t *dd = ui::find_in(lv_obj_get_parent(label), &lv_dropdown_class);
  REQUIRE(dd != nullptr);
  CHECK(lv_dropdown_get_selected(dd) == 3);  // None, Translucent, a.png, a_long_name.png
  CHECK(std::string(lv_dropdown_get_text(dd)) == "a_long_");  // первые 7 символов
}

// Плитки от 64 до 512 px по стороне.
void check_tile_size_not_used(int w) {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start(moonraker, [&](const std::string &dir, json &config) {
    choose_file(dir, config, "t.png", [&](const std::string &path) { test_png::write(path, solid(w, 64, 0, 200, 0), w, 64); });
  });
  CHECK(GuppyScreen::bg_tile_src() == nullptr);
  CHECK(check_blank([](int, int) { return CARD; }) == 1);
}

TEST_CASE("a background tile under 64 px a side is not used") { check_tile_size_not_used(63); }

TEST_CASE("a background tile over 512 px a side is not used") { check_tile_size_not_used(513); }

TEST_CASE("a background that is not a string gives the theme colour") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start(moonraker, [](const std::string &, json &config) { config["background"] = false; });
  CHECK(GuppyScreen::background_mode() == GuppyScreen::BG_THEME);
  CHECK(check_blank([](int, int) { return CARD; }) == 1);
}

TEST_CASE("a big PNG is refused by its header before it is decoded") {
  // 4000 x 3000: раскодирование потребовало бы 48 МБ, больше, чем есть у принтера.
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start(moonraker, [&](const std::string &dir, json &config) {
    choose_file(dir, config, "big.png", [](const std::string &path) { write_png_header(path, 4000, 3000); });
  });
  CHECK(GuppyScreen::bg_tile_src() == nullptr);
  CHECK(log_count("not a PNG of 64..512 px a side") == 1);
}

TEST_CASE("the Input Shaper graphs paint the background so an enlarged one hides what is under it") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start(moonraker, [](const std::string &dir, json &config) {
    choose_file(dir, config, "t.png", [](const std::string &path) { test_png::write(path, solid(64, 64, 200, 0, 0), 64, 64); });
  });
  ui::tap_tab(Nav::TUNE);
  ui::tap_label("Input Shaper");
  for (const char *name : {"X Frequency Response", "Y Frequency Response"}) {
    CAPTURE(name);
    lv_obj_t *label = ui::find_label(name);
    REQUIRE(label != nullptr);
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    lv_obj_t *graph = lv_obj_get_parent(label);
    CHECK(lv_obj_get_style_bg_opa(graph, 0) == LV_OPA_COVER);
    CHECK(lv_obj_get_style_bg_img_src(graph, 0) == GuppyScreen::bg_tile_src());
  }
}

TEST_CASE("backgrounds with an upper-case .PNG extension are listed too") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start(moonraker, [](const std::string &dir, json &) {
    write_backgrounds(dir);
    test_png::write(dir + "/backgrounds/D.PNG", solid(64, 64, 0, 200, 0), 64, 64);
  });
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  CHECK(GuppyScreen::background_files() == std::vector<std::string>{"D.PNG", "a.png", "b.png", "c_small.png"});
}

namespace {

// Видна ли за `o` текстура: свой фон — плитка или он прозрачен.
bool shows_tile(lv_obj_t *o) {
  return lv_obj_get_style_bg_opa(o, 0) == LV_OPA_TRANSP || lv_obj_get_style_bg_img_src(o, 0) == GuppyScreen::bg_tile_src();
}

void start_with_tile(FakeMoonraker &moonraker) {
  start(moonraker, [](const std::string &dir, json &config) {
    write_backgrounds(dir);
    config["background"] = "backgrounds/a.png";
  });
  REQUIRE(GuppyScreen::background_mode() == GuppyScreen::BG_TILE);
}

// PNG 64 x 64 с верным заголовком, но без данных: проходит проверку размера и
// не раскодируется.
void write_truncated_png(const std::string &path) { write_png_header(path, 64, 64); }

}  // namespace

TEST_CASE("a Klipper prompt shown later shows the background texture behind its buttons") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start_with_tile(moonraker);
  for (const char *line : {"prompt_begin Hello", "prompt_button_group_start", "prompt_button Aaa|G28",
                           "prompt_button Bbb|G28", "prompt_button_group_end", "prompt_show"})
    moonraker.push_notification("notify_gcode_response", {std::string("// action:") + line});
  REQUIRE_MESSAGE(app::wait_until([] { return ui::find_label("Bbb", ui::Match::Contains) != nullptr; }), ui::dump_screen());
  app::settle(moonraker);
  lv_obj_t *label = ui::find_label("Aaa", ui::Match::Contains);
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  lv_obj_t *group = lv_obj_get_parent(lv_obj_get_parent(label));  // группа кнопок
  CHECK(shows_tile(group));
}

TEST_CASE("a background that does not decode keeps the shown one or falls back to translucent") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start_with_tile(moonraker);
  write_truncated_png(app::dir() + "/backgrounds/t.png");
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  // a.png раскодируется снова: тот же фон, конфиг не меняется.
  GuppyScreen::set_background(GuppyScreen::BG_TILE, "t.png");
  CHECK(GuppyScreen::background_mode() == GuppyScreen::BG_TILE);
  REQUIRE(GuppyScreen::bg_tile_src() != nullptr);
  CHECK(GuppyScreen::background_file() == app::dir() + "/backgrounds/a.png");
  CHECK(saved_background() == "backgrounds/a.png");
  // a.png тоже испортили: без плитки, плитки полупрозрачны.
  write_truncated_png(app::dir() + "/backgrounds/a.png");
  GuppyScreen::set_background(GuppyScreen::BG_TILE, "t.png");
  CHECK(GuppyScreen::background_mode() == GuppyScreen::BG_TRANSLUCENT);
  CHECK(GuppyScreen::bg_tile_src() == nullptr);
  CHECK(GuppyScreen::background_file().empty());
}

TEST_CASE("choosing the background that is shown again changes nothing") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start_with_tile(moonraker);
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  const lv_img_dsc_t *tile = GuppyScreen::bg_tile_src();
  const void *data = tile->data;
  GuppyScreen::set_background(GuppyScreen::BG_TILE, "a.png");
  CHECK(GuppyScreen::bg_tile_src()->data == data);  // не раскодирован заново
  CHECK(log_count("background: now") == 0);
  GuppyScreen::set_background(GuppyScreen::BG_THEME);
  GuppyScreen::set_background(GuppyScreen::BG_THEME);
  CHECK(log_count("background: now") == 1);
}

TEST_CASE("the file list's rows are see-through except with no background") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start(moonraker);
  ui::tap_tab(Nav::PRINT);
  lv_obj_t *table = ui::find_shown(&lv_table_class);
  REQUIRE(table != nullptr);
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  CHECK(lv_obj_get_style_bg_opa(table, LV_PART_MAIN) == LV_OPA_TRANSP);  // под строками фон панели
  CHECK(lv_obj_get_style_bg_opa(table, LV_PART_ITEMS) == LV_OPA_COVER);  // None: как в upstream
  GuppyScreen::set_background(GuppyScreen::BG_TRANSLUCENT);
  CHECK(lv_obj_get_style_bg_opa(table, LV_PART_ITEMS) == LV_OPA_20);
}

TEST_CASE("the Bed Mesh profile tables are see-through except with no background") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start(moonraker);
  ui::tap_label("Bed Mesh");
  // Таблицы с прозрачным фоном и ячейками с opa на верхнем экране.
  auto tables = [](lv_opa_t items) {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    lv_obj_t *content = Nav::get().content();
    lv_obj_t *top = lv_obj_get_child(content, lv_obj_get_child_cnt(content) - 1);
    int n = 0;
    ui::walk(top, [&](lv_obj_t *o) {
      n += lv_obj_check_type(o, &lv_table_class) && lv_obj_get_style_bg_opa(o, LV_PART_MAIN) == LV_OPA_TRANSP &&
           lv_obj_get_style_bg_opa(o, LV_PART_ITEMS) == items;
    });
    return n;
  };
  CHECK(tables(LV_OPA_COVER) == 3);  // None: ячейки как в upstream
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    GuppyScreen::set_background(GuppyScreen::BG_TRANSLUCENT);
  }
  CHECK(tables(LV_OPA_20) == 3);  // профили, их список и сведения о профиле
}

TEST_CASE("the Console macro list's rows are see-through except with no background") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start(moonraker);
  ui::tap_tab(Nav::SETTINGS);
  ui::tap_label("Console");
  lv_obj_t *table = ui::find_shown(&lv_table_class);
  REQUIRE(table != nullptr);
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  CHECK(lv_obj_get_style_bg_opa(table, LV_PART_MAIN) == LV_OPA_TRANSP);  // под строками фон панели
  CHECK(lv_obj_get_style_bg_opa(table, LV_PART_ITEMS) == LV_OPA_COVER);  // None: как в upstream
  GuppyScreen::set_background(GuppyScreen::BG_TRANSLUCENT);
  CHECK(lv_obj_get_style_bg_opa(table, LV_PART_ITEMS) == LV_OPA_20);
}
