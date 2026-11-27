#include "guppyscreen.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <vector>

// lodepng из LVGL (lvgl/src/extra/libs/png), собран как C. Его заголовок не
// объявляет C-функции с C-линковкой при включении из C++.
extern "C" {
unsigned lodepng_decode32_file(unsigned char **out, unsigned *w, unsigned *h, const char *filename);
const char *lodepng_error_text(unsigned code);
}

#include "config.h"
#ifndef OS_ANDROID
  #include "lv_drivers/display/fbdev.h"
  #include "lv_drivers/indev/evdev.h"

  #include "spdlog/sinks/rotating_file_sink.h"
  #include "spdlog/sinks/stdout_sinks.h"

#else
  #include "spdlog/sinks/android_sink.h"
#endif

#ifndef GUPPY_FF5M
#include "printer_select_panel.h"
#endif
#include "spdlog/spdlog.h"
#include "state.h"
#include "nav.h"
#include "mini_print_status.h"
#include "theme.h"
#include <time.h>
#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;

GuppyScreen *GuppyScreen::instance = NULL;
lv_style_t GuppyScreen::style_container;
lv_style_t GuppyScreen::style_imgbtn_default;
lv_style_t GuppyScreen::style_imgbtn_pressed;
lv_style_t GuppyScreen::style_imgbtn_disabled;
lv_theme_t GuppyScreen::th_new;
lv_style_t GuppyScreen::style_bg;
lv_style_t GuppyScreen::style_see_through;
lv_style_t GuppyScreen::style_tint;
lv_style_t GuppyScreen::style_tile;
lv_img_dsc_t GuppyScreen::bg_dsc;
GuppyScreen::BgMode GuppyScreen::bg_mode = GuppyScreen::BG_THEME;
static const lv_color_t BG_CARD = lv_color_hex(0x282B30);  // цвет карточки тёмной темы LVGL
static const unsigned BG_MIN_SIZE = 64, BG_MAX_SIZE = 512;  // сторона плитки, px
std::string GuppyScreen::bg_dir;
std::string GuppyScreen::bg_file;

#ifndef OS_ANDROID
lv_obj_t *GuppyScreen::screen_saver = NULL;
#endif

KWebSocketClient GuppyScreen::ws(NULL);

std::mutex GuppyScreen::lv_lock;
std::mutex GuppyScreen::lv_lock_update;

GuppyScreen::GuppyScreen()
  : spoolman_panel(ws, lv_lock)
  , main_panel(ws, lv_lock, spoolman_panel)
  , init_panel(main_panel, main_panel.get_tune_panel().get_bedmesh_panel(), lv_lock)
{
  main_panel.create_panel();
  Nav::get().start();
}

GuppyScreen *GuppyScreen::get() {
  if (instance == NULL) {
    instance = new GuppyScreen();
  }

  return instance;
}

GuppyScreen *GuppyScreen::init(std::function<void(lv_color_t, lv_color_t)> hal_init) {
  hlog_disable();

  // config
  Config *conf = Config::get_instance();
  const std::string ll_path = conf->df() + "log_level";
  auto ll = spdlog::level::from_str(
      conf->get_json("/printers").empty()
      ? "debug"
      : conf->get<std::string>(ll_path));

  auto selected_theme = conf->get_json("/theme").empty()
          ? "blue.json"
          : conf->get<std::string>("/theme") + ".json";
  // Настоящий каталог конфига (в Z-Mod guppyconfig.json — ссылка в
  // /opt/config/mod_data, который правится из Fluidd/Mainsail): themes/ и
  // backgrounds/ ищутся там.
  bg_dir = fs::canonical(conf->get_path()).parent_path().string();
  auto theme_config = fs::path(bg_dir) / "themes" / selected_theme;

  ThemeConfig *theme_conf = ThemeConfig::get_instance();
  theme_conf->init(theme_config);

  auto primary_color = theme_conf->get_json("/primary_color").empty()
          ? lv_color_hex(0x2196F3)
          : lv_color_hex(std::stoul(theme_conf->get<std::string>("/primary_color"), nullptr, 16));

  auto secondary_color = theme_conf->get_json("/secondary_color").empty()
          ? lv_color_hex(0xF44336)
          : lv_color_hex(std::stoul(theme_conf->get<std::string>("/secondary_color"), nullptr, 16));

#ifndef OS_ANDROID
  auto console_sink = std::make_shared<spdlog::sinks::stdout_sink_mt>();
  auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
      conf->get<std::string>("/log_path"), 1048576 * 10, 3);
  spdlog::sinks_init_list log_sinks{console_sink, file_sink};

#else
  auto android_sink = std::make_shared<spdlog::sinks::android_sink_mt>();
  spdlog::sinks_init_list log_sinks{android_sink};
#endif  // OS_ANDROID

  auto klogger = std::make_shared<spdlog::logger>("guppyscreen", log_sinks);
  spdlog::register_logger(klogger);

  spdlog::set_level(ll);
  spdlog::set_default_logger(klogger);
  klogger->flush_on(ll);

#ifdef GUPPYSCREEN_VERSION
  spdlog::info("Guppy Screen Version: {}", GUPPYSCREEN_VERSION);
#endif  // GUPPYSCREEN_VERSION

  spdlog::info(_(" Waiting for the Klipper launch..."));

  spdlog::info("DPI: {}", LV_DPI_DEF);
  /*LittlevGL init*/
  lv_init();

#if !defined(SIMULATOR) && !defined(OS_ANDROID)
  /*Linux frame buffer device init*/
  fbdev_init();
  fbdev_unblank();
#endif  // OS_ANDROID

  hal_init(primary_color, secondary_color);
  lv_png_init();

  lv_style_init(&style_container);
  lv_style_set_border_width(&style_container, 0);
  lv_style_set_radius(&style_container, 0);

//  lv_style_init(&style_imgbtn_default);
//  lv_style_set_img_recolor_opa(&style_imgbtn_default, LV_OPA_100);
//  lv_style_set_img_recolor(&style_imgbtn_default, lv_color_black());

  load_bg_tile(conf);
  lv_style_init(&style_bg);
  set_bg_style();
  lv_style_init(&style_see_through);
  lv_style_set_bg_opa(&style_see_through, LV_OPA_TRANSP);
  lv_style_init(&style_tint);
  lv_style_init(&style_tile);
#ifdef GUPPY_FF5M
  MiniPrintStatus::init_look();
#endif
  fill_tint_styles();

  lv_style_init(&style_imgbtn_pressed);
  lv_style_set_img_recolor_opa(&style_imgbtn_pressed, LV_OPA_100);
  lv_style_set_img_recolor(&style_imgbtn_pressed, primary_color);

  lv_style_init(&style_imgbtn_disabled);
  lv_style_set_img_recolor_opa(&style_imgbtn_disabled, LV_OPA_100);
  lv_style_set_img_recolor(&style_imgbtn_disabled, lv_palette_darken(LV_PALETTE_GREY, 1));

  /*Initia1ize the new theme from the current theme*/

  lv_theme_t *th_act = lv_disp_get_theme(NULL);
  th_new = *th_act;

  /*Set the parent theme and the style apply callback for the new theme*/
  lv_theme_set_parent(&th_new, th_act);
  lv_theme_set_apply_cb(&th_new, &GuppyScreen::new_theme_apply_cb);

  /*Assign the new theme to the current display*/
  lv_disp_set_theme(NULL, &th_new);

#ifndef OS_ANDROID
  screen_saver = lv_obj_create(lv_scr_act());

  lv_obj_set_size(screen_saver, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_opa(screen_saver, LV_OPA_100, 0);
  lv_obj_move_background(screen_saver);

/*
  lv_obj_t *main_screen = lv_disp_get_scr_act(NULL);
  auto touch_calibrated = conf->get_json("/touch_calibrated");
  if (!touch_calibrated.is_null()) {
    auto is_calibrated = touch_calibrated.template get<bool>();
    if (is_calibrated) {
      auto calibration_coeff = conf->get_json("/touch_calibration_coeff");
      if (calibration_coeff.is_null()) {
        lv_tc_register_coeff_save_cb(&GuppyScreen::save_calibration_coeff);
        lv_obj_t *touch_calibrate_scr = lv_tc_screen_create();

        lv_disp_load_scr(touch_calibrate_scr);

        lv_tc_screen_start(touch_calibrate_scr);
        lv_obj_add_event_cb(touch_calibrate_scr, &GuppyScreen::handle_calibrated, LV_EVENT_READY, main_screen);
        spdlog::info("running touch calibration");
      } else {
        // load calibration data
        auto c = calibration_coeff.template get<std::vector<float>>();
        lv_tc_coeff_t coeff = {true, c[0], c[1], c[2], c[3], c[4], c[5]};
        lv_tc_set_coeff(coeff, false);
        spdlog::info("loaded calibration coefficients");
      }
    }
  }
*/
#endif // OS_ANDROID

  ws.register_notify_update(State::get_instance());

  GuppyScreen *gs = GuppyScreen::get();
  auto printers = conf->get_json("/printers");
  if (!printers.empty()) {
    // start initializing all guppy components
    std::string ws_url = fmt::format("ws://{}:{}/websocket",
                                     conf->get<std::string>(conf->df() + "moonraker_host"),
                                     conf->get<uint32_t>(conf->df() + "moonraker_port"));

    spdlog::info("connecting to printer at {}", ws_url);
    gs->connect_ws(ws_url);
  }


  return gs;
}

void GuppyScreen::up(bool full) {
    spdlog::debug("waking up display");

    lv_lock_update.lock();
    if (full) {
        update_time = time(NULL);
        update = false;
    }
    is_sleeping = false;
    lv_lock_update.unlock();

    fbdev_unblank();
    lv_obj_move_background(screen_saver);
}

void GuppyScreen::down() {
    spdlog::debug("putting display to sleeping");

    fbdev_blank();
    lv_obj_move_foreground(screen_saver);

    lv_lock_update.lock();
    is_sleeping = true;
    lv_lock_update.unlock();
}

void GuppyScreen::loop() {
  /*Handle LitlevGL tasks (tickless mode)*/
#if !defined(SIMULATOR) && !defined(OS_ANDROID)
  Config *conf = Config::get_instance();
  int32_t display_sleep = conf->get<int32_t>("/display_sleep_sec") * 1000;
  up(true);
#endif

  while (1) {
    lv_lock.lock();
    uint32_t idle_ms = lv_timer_handler();  // until the next LVGL timer is due
    lv_lock.unlock();

    lv_lock_update.lock();
    if (!update) {
        time_t cur_time = time(NULL);
        if (cur_time - update_time > 60 * 60 * 24)
            update_time = cur_time;

        if (cur_time - update_time > (time_t)(display_sleep/1000))
            update = true;
    }
    lv_lock_update.unlock();

#if !defined(SIMULATOR) && !defined(OS_ANDROID)
    if (display_sleep != -1) {
      if (update && lv_disp_get_inactive_time(NULL) > display_sleep) {
        if (!is_sleeping.load()) {
          down();
        }
      } else {
        if (is_sleeping.load()) {
          up(false);
        }
      }
    }
#endif  // SIMULATOR/OS_ANDROID

    // Sleep until the next LVGL timer is due instead of a fixed 5 ms: the
    // display refresh and the touch read run every LV_DISP_DEF_REFR_PERIOD /
    // LV_INDEV_DEF_READ_PERIOD (30 ms), so this wakes ~33 times a second
    // instead of 200, with the same redraw and touch latency. Timers that
    // other threads add (lv_async_call) wait at most that long.
    if (idle_ms > LV_DISP_DEF_REFR_PERIOD) idle_ms = LV_DISP_DEF_REFR_PERIOD;  // also LV_NO_TIMER_READY
    usleep(idle_ms * 1000);
  }
}

std::mutex &GuppyScreen::get_lock() {
    return lv_lock;
}

// "background" в guppyconfig.json для режимов без файла.
const char *GuppyScreen::mode_name(BgMode mode) {
  return mode == BG_TRANSLUCENT ? "translucent" : "none";
}

// Фон панелей, из "background" в guppyconfig.json:
// - не задан или "none": цвет карточки темы (#282B30) и непрозрачные плитки,
//   как в upstream;
// - "translucent": цвет карточки и полупрозрачные плитки;
// - "backgrounds/<имя>.png" (рядом с guppyconfig.json): PNG плиткой.
// PNG раскодируется один раз, здесь (см. use_tile). Если файл не годится,
// показывается цвет карточки, как с "translucent".
void GuppyScreen::load_bg_tile(Config *conf) {
  json v = conf->get_json("/background");
  const std::string setting = v.is_string() ? v.get<std::string>() : v.is_null() ? "" : v.dump();
  bg_mode = BG_THEME;
  if (setting == mode_name(BG_TRANSLUCENT))
    bg_mode = BG_TRANSLUCENT;
  else if (setting.rfind("backgrounds/", 0) == 0)
    bg_mode = use_tile((fs::path(bg_dir) / setting).string()) ? BG_TILE : BG_TRANSLUCENT;
  else if (!setting.empty() && setting != mode_name(BG_THEME))
    spdlog::warn("background: unknown {}, using the theme colour", setting);
}

// style_tint и style_tile: полупрозрачные плитки. В BG_THEME у виджетов вид
// темы, кроме скругления style_tile в 4 px (как у статуса печати на Home в
// upstream; у темы 7 px).
void GuppyScreen::fill_tint_styles() {
  lv_style_reset(&style_tint);
  lv_style_reset(&style_tile);
  if (translucent()) {
    for (lv_style_t *style : {&style_tint, &style_tile}) {
      lv_style_set_bg_color(style, lv_theme_get_color_primary(NULL));
      lv_style_set_bg_opa(style, LV_OPA_20);
    }
    lv_style_set_border_width(&style_tile, 0);
    lv_style_set_border_opa(&style_tile, LV_OPA_TRANSP);  // рамка, заданная у объекта (статус печати)
    lv_style_set_radius(&style_tile, 0);
  } else {
    lv_style_set_radius(&style_tile, 4);
  }
#ifdef GUPPY_FF5M
  MiniPrintStatus::set_look(translucent(), BG_CARD);
#endif
}

// Заменяет плитку на PNG по пути `path` ("": без плитки). Старая освобождается
// до раскодирования новой, так что двух сразу в памяти не бывает. false, если
// `path` не годится: при негодном заголовке старая остаётся, а если PNG не
// раскодировался, плитки нет (bg_file пуст).
// Размер из заголовка PNG (IHDR) проверяется до декодирования: фото на
// несколько мегапикселей заняло бы всю память принтера.
static bool tile_header_ok(const std::string &path) {
  unsigned char hdr[24] = {0};
  std::ifstream(path, std::ios::binary).read((char *)hdr, sizeof(hdr));
  auto be32 = [&hdr](int i) { return (unsigned)hdr[i] << 24 | (unsigned)hdr[i + 1] << 16 | hdr[i + 2] << 8 | hdr[i + 3]; };
  unsigned w = be32(16), h = be32(20);
  if (memcmp(hdr + 12, "IHDR", 4) != 0 || w < BG_MIN_SIZE || h < BG_MIN_SIZE || w > BG_MAX_SIZE || h > BG_MAX_SIZE) {
    spdlog::warn("background: can't use {} (not a PNG of {}..{} px a side)", path, BG_MIN_SIZE, BG_MAX_SIZE);
    return false;
  }
  return true;
}

bool GuppyScreen::use_tile(const std::string &path) {
  if (!path.empty() && !tile_header_ok(path)) return false;  // старая плитка остаётся
  if (bg_dsc.data != NULL) {
    lv_img_cache_invalidate_src(&bg_dsc);  // LVGL кэширует открытые изображения по источнику
    lv_mem_free((void *)bg_dsc.data);
    bg_dsc.data = NULL;
  }
  bg_file.clear();
  if (path.empty()) return true;
  if (!decode_bg_tile(path, &bg_dsc)) return false;
  bg_file = path;
  return true;
}

// Раскодирует PNG по пути `path` в `dsc`: true colour (4 байта на пиксель,
// рисуется быстрее всего), прозрачные пиксели смешаны с цветом карточки,
// так что полупрозрачный PNG работает как наложение. false, если не вышло
// (`dsc` тогда не меняется).
// Заголовок уже проверен (tile_header_ok).
// 64..512 px по стороне: 512 x 512 занимает 1 МБ (при раскодировании на миг
// втрое больше); на мелкие плитки уходит больше отрисовок за перерисовку
// (64 px: ~100 на весь экран).
bool GuppyScreen::decode_bg_tile(const std::string &path, lv_img_dsc_t *dsc) {
  unsigned w, h;
  unsigned char *px = NULL;
  unsigned err = lodepng_decode32_file(&px, &w, &h, ("A:" + path).c_str());  // через lv_fs
  if (err != 0) {
    spdlog::warn("background: can't use {} ({})", path, lodepng_error_text(err));
    lv_mem_free(px);
    return false;
  }
  // R, G, B, A -> lv_color_t на месте, смешанный с цветом карточки.
  static_assert(LV_COLOR_DEPTH == 32, "tile pixels are lv_color32_t");  // 4 байта, как в PNG
  for (lv_color_t *c = (lv_color_t *)px; c < (lv_color_t *)px + (size_t)w * h; c++) {
    const uint8_t *p = (const uint8_t *)c;
    *c = lv_color_mix(lv_color_make(p[0], p[1], p[2]), BG_CARD, p[3]);
  }
  dsc->header.cf = LV_IMG_CF_TRUE_COLOR;
  dsc->header.always_zero = 0;
  dsc->header.w = w;
  dsc->header.h = h;
  dsc->data_size = (uint32_t)w * h * 4;
  dsc->data = px;
  spdlog::info("background: {} ({}x{})", path, w, h);
  return true;
}

// style_bg рисует цвет карточки и плитку, если она есть.
void GuppyScreen::set_bg_style() {
  lv_style_set_bg_color(&style_bg, BG_CARD);
  if (const lv_img_dsc_t *tile = bg_tile_src()) {
    lv_style_set_bg_img_src(&style_bg, tile);
    lv_style_set_bg_img_tiled(&style_bg, true);
  } else {
    lv_style_remove_prop(&style_bg, LV_STYLE_BG_IMG_SRC);
  }
}

static bool has_style(lv_obj_t *o, const lv_style_t *style) {
  for (uint32_t i = 0; i < o->style_cnt; i++)
    if (o->styles[i].style == style) return true;
  return false;
}

// См. apply_bg_texture. `behind_bg`: за `o` виден style_bg.
bool GuppyScreen::bg_walk(lv_obj_t *o, bool behind_bg) {
  lv_opa_t opa = lv_obj_get_style_bg_opa(o, 0);
  bool changed = false;
  bool children_behind;  // за детьми o виден style_bg
  // Плитки (tint(), tile()) не трогаются: в режиме None они в цвете
  // карточки, но при переключении на Translucent снова станут полупрозрачными.
  if (opa == LV_OPA_COVER && !has_style(o, &style_bg) && !has_style(o, &style_tint) && !has_style(o, &style_tile) &&
      o != lv_scr_act() && o != Nav::get().content() &&
      lv_obj_check_type(o, &lv_obj_class) &&
      lv_color_to32(lv_obj_get_style_bg_color(o, 0)) == lv_color_to32(BG_CARD)) {
    lv_obj_add_style(o, behind_bg ? &style_see_through : &style_bg, 0);
    changed = children_behind = true;
  } else {
    children_behind = opa == LV_OPA_TRANSP ? behind_bg : has_style(o, &style_bg);
  }
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(o); i++)
    changed |= bg_walk(lv_obj_get_child(o, i), children_behind);
  return changed;
}

// Панели лежат стопкой на экране и в области содержимого Nav, а не
// скрываются (поэтому ни экран, ни эта область style_bg не получают):
// передняя закрывает остальные своим непрозрачным фоном. Значит, фон рисуют
// фоны панелей: простой контейнер, залитый плоским цветом карточки темы,
// получает style_bg (этот цвет и плитку PNG, если она есть), если
// только у ближайшего предка с фоном уже нет style_bg. Тогда контейнер
// становится прозрачным и показывает фон предка, так что каждая
// перерисовываемая область рисуется один раз, а не на каждом уровне
// вложенности. Патч 0008 кладёт плитки на одну сетку от начала координат
// экрана, поэтому вид тот же, без швов. Кнопки и контейнеры со своим цветом
// его сохраняют. Вызывать после создания панелей; уже обработанные объекты
// пропускаются.
void GuppyScreen::apply_bg_texture(lv_obj_t *root) {
  if (root == NULL) root = lv_scr_act();
  // Виден ли за root именно style_bg?
  bool behind_bg = false;
  for (lv_obj_t *o = lv_obj_get_parent(root); o != NULL; o = lv_obj_get_parent(o)) {
    if (has_style(o, &style_bg)) { behind_bg = true; break; }
    if (lv_obj_get_style_bg_opa(o, 0) != LV_OPA_TRANSP) break;  // другой фон
  }
  // Одно обновление стилей на поддерево, а не на каждый изменённый контейнер.
  lv_obj_enable_style_refresh(false);
  bool changed = bg_walk(root, behind_bg);
  lv_obj_enable_style_refresh(true);
  if (changed) lv_obj_refresh_style(root, LV_PART_ANY, LV_STYLE_PROP_ANY);
}

// Файлы *.png в <каталог конфига>/backgrounds/, по имени. Читаются заново при
// каждом вызове, так что загруженные позже файлы появляются.
std::vector<std::string> GuppyScreen::background_files() {
  std::vector<std::string> names;
  std::error_code ec;
  for (auto &e : fs::directory_iterator(fs::path(bg_dir) / "backgrounds", ec)) {
    std::string ext = e.path().extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == ".png") names.push_back(e.path().filename().string());
  }
  std::sort(names.begin(), names.end());
  return names;
}

// Показывает `mode` (BG_TILE: backgrounds/<name>) и сохраняет его в
// "background" в guppyconfig.json, чтобы он был и после перезапуска.
// Негодный файл ничего не меняет.
void GuppyScreen::set_background(BgMode mode, const std::string &name) {
  const std::string path = mode == BG_TILE ? (fs::path(bg_dir) / "backgrounds" / name).string() : "";
  if (mode == bg_mode && path == bg_file) return;  // LVGL сообщает о выборе и того же пункта
  const std::string prev = bg_file;
  const bool was_translucent = translucent();
  if (use_tile(path)) {
    Config *conf = Config::get_instance();
    conf->set<std::string>("/background", mode == BG_TILE ? "backgrounds/" + name : mode_name(mode));
    conf->save();
    bg_mode = mode;
  } else if (bg_file != prev && !use_tile(prev)) {
    // Новый PNG не раскодировался, а старую плитку уже освободили, и прежний
    // файл тоже испорчен: без плитки, как при запуске с негодным файлом.
    bg_mode = BG_TRANSLUCENT;
  }  // иначе остаётся прежний фон, конфиг не меняется

  set_bg_style();
  lv_obj_report_style_change(&style_bg);
  apply_bg_texture();  // панели, созданные с тех пор (обновляет только их)
  if (translucent() != was_translucent) {  // между текстурами плитки не меняются
    fill_tint_styles();
    lv_obj_report_style_change(&style_tint);
    lv_obj_report_style_change(&style_tile);
  }
  spdlog::info("background: now {}", bg_mode == BG_TILE ? bg_file : mode_name(bg_mode));
}

void GuppyScreen::connect_ws(const std::string &url) {
  up(true);
  apply_bg_texture();
  init_panel.set_message((LV_SYMBOL_WARNING + std::string(_(" Waiting for the Klipper launch..."))).c_str());
  ws.connect(url.c_str(),
   [this]() { init_panel.connected(ws); },
   [this]() { init_panel.disconnected(ws); });
}


void GuppyScreen::new_theme_apply_cb(lv_theme_t *th, lv_obj_t *obj) {
  LV_UNUSED(th);

  if (lv_obj_check_type(obj, &lv_obj_class)) {
    lv_obj_add_style(obj, &style_container, 0);
  }

  if (lv_obj_check_type(obj, &lv_imgbtn_class)) {
//    lv_obj_add_style(obj, &style_imgbtn_default, LV_STATE_DEFAULT);
    lv_obj_add_style(obj, &style_imgbtn_pressed, LV_STATE_PRESSED);
    lv_obj_add_style(obj, &style_imgbtn_disabled, LV_STATE_DISABLED);
  }
}

/*
void GuppyScreen::handle_calibrated(lv_event_t *event) {
  spdlog::info("finished calibration");
  lv_obj_t *main_screen = (lv_obj_t *)event->user_data;
  lv_disp_load_scr(main_screen);
}

void GuppyScreen::save_calibration_coeff(lv_tc_coeff_t coeff) {
  Config *conf = Config::get_instance();
  conf->set<std::vector<float>>("/touch_calibration_coeff",
                                {coeff.a, coeff.b, coeff.c, coeff.d, coeff.e, coeff.f});
  conf->save();
}

void GuppyScreen::refresh_theme() {
  lv_theme_t *th = lv_theme_default_get();
  ThemeConfig *theme_conf = ThemeConfig::get_instance();
  auto primary_color = theme_conf->get_json("/primary_color").empty()
                       ? lv_color_hex(0x2196F3)
                       : lv_color_hex(std::stoul(theme_conf->get<std::string>("/primary_color"), nullptr, 16));

  auto secondary_color = theme_conf->get_json("/secondary_color").empty()
                         ? lv_color_hex(0xF44336)
                         : lv_color_hex(std::stoul(theme_conf->get<std::string>("/secondary_color"), nullptr, 16));

  lv_disp_t *disp = lv_disp_get_default();
  lv_theme_t * new_theme =  lv_theme_default_init(disp, primary_color, secondary_color, true, th->font_normal);
  lv_disp_set_theme(disp, new_theme);
  lv_style_set_img_recolor(&style_imgbtn_pressed, primary_color);
}
*/

/*Set in lv_conf.h as `LV_TICK_CUSTOM_SYS_TIME_EXPR`*/
uint32_t custom_tick_get(void) {
  // LVGL needs elapsed time, unaffected by wall-clock corrections. Cast
  // before multiplication: time_t can be 32-bit on the MIPS target.
  struct timespec ts{};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  const uint64_t ms = static_cast<uint64_t>(ts.tv_sec) * 1000
                    + static_cast<uint64_t>(ts.tv_nsec) / 1000000;
  // LVGL handles the normal uint32_t millisecond wrap-around.
  return static_cast<uint32_t>(ms);
}
