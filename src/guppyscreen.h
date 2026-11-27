#ifndef __GUPPY_SCREEN_H__
#define __GUPPY_SCREEN_H__

#include <string>
#include <vector>
#include <mutex>
#include <functional>

#include "lv_tc.h"
#include "lv_tc_screen.h"
#include "lvgl/lvgl.h"

#include "platform.h"
#include "init_panel.h"
#include "main_panel.h"
#include "spoolman_panel.h"
#include "websocket_client.h"

class GuppyScreen {
 private:
  static GuppyScreen *instance;
  static lv_style_t style_container;
  static lv_style_t style_bg;
  static lv_style_t style_see_through;
 public:
  // BG_THEME: "none", цвет карточки темы (#282B30) и непрозрачные плитки, как
  // в upstream; BG_TRANSLUCENT: тот же цвет и полупрозрачные плитки;
  // BG_TILE: PNG и полупрозрачные плитки.
  enum BgMode { BG_THEME, BG_TRANSLUCENT, BG_TILE };
 private:
  // Полупрозрачный фон плиток и панелей поверх фона панели: цвет темы на 20%,
  // как фон полосы прогресса темы. Пуст в режиме None: у виджетов вид темы.
  static lv_style_t style_tint;
  // То же, плюс без рамки и скруглений (плитки Home: часы и статус, график);
  // в режиме None — только скругление 4 px.
  static lv_style_t style_tile;
  static lv_img_dsc_t bg_dsc;  // плитка; data NULL, если её нет
  static BgMode bg_mode;
  static const char *mode_name(BgMode mode);
  static void fill_tint_styles();
  static std::string bg_dir;  // каталог guppyconfig.json (по ссылке)
  static std::string bg_file;  // показанный PNG, "", если его нет
  static void load_bg_tile(class Config *conf);
  static bool use_tile(const std::string &path);
  static bool decode_bg_tile(const std::string &path, lv_img_dsc_t *dsc);
  static void set_bg_style();
  static bool bg_walk(lv_obj_t *o, bool behind_bg);
  static lv_style_t style_imgbtn_default;
  static lv_style_t style_imgbtn_pressed;
  static lv_style_t style_imgbtn_disabled;
  static lv_theme_t th_new;
#ifndef OS_ANDROID
  static lv_obj_t *screen_saver;
#endif
  static std::mutex lv_lock;
  static std::mutex lv_lock_update;
  static KWebSocketClient ws;

  SpoolmanPanel spoolman_panel;
  MainPanel main_panel;
  InitPanel init_panel;

  time_t update_time;
  bool update;
  std::atomic_bool is_sleeping;

 public:
  GuppyScreen();
  GuppyScreen(GuppyScreen &o) = delete;
  void operator=(const GuppyScreen &) = delete;

  std::mutex &get_lock();

  void connect_ws(const std::string &url);
  static GuppyScreen *get();
  static GuppyScreen *init(std::function<void(lv_color_t, lv_color_t)> hal_init);
  void loop();
  void up(bool full);
  void down();
  static void new_theme_apply_cb(lv_theme_t *th, lv_obj_t *obj);
  static void apply_bg_texture(lv_obj_t *root = NULL);  // NULL: весь экран
  // Контейнер, разворачиваемый поверх соседей (графики), рисует фон сам, а не становится прозрачным.
  static void own_bg(lv_obj_t *o) { lv_obj_add_style(o, &style_bg, 0); }
  static const lv_img_dsc_t *bg_tile_src() { return bg_dsc.data != NULL ? &bg_dsc : NULL; }
  static BgMode background_mode() { return bg_mode; }
  static bool translucent() { return bg_mode != BG_THEME; }  // плитки полупрозрачны
  // Плитка или панель поверх фона панели: полупрозрачна, кроме режима None.
  static void tint(lv_obj_t *o) { lv_obj_add_style(o, &style_tint, 0); }
  // Таблица так же: ячейки полупрозрачны, под ними фон панели (в режиме None
  // тот же цвет карточки).
  static void tint_table(lv_obj_t *t) {
    lv_obj_set_style_bg_opa(t, LV_OPA_TRANSP, 0);
    lv_obj_add_style(t, &style_tint, LV_PART_ITEMS);
  }
  // То же без рамки и скруглений (в режиме None скругление 4 px).
  static void tile(lv_obj_t *o) { lv_obj_add_style(o, &style_tile, 0); }
  static const std::string &background_file() { return bg_file; }  // "", если его нет
  static std::vector<std::string> background_files();  // backgrounds/*.png, по имени
  // `mode`, для BG_TILE — с backgrounds/<name>; сохраняется.
  static void set_background(BgMode mode, const std::string &name = "");
/*  static void handle_calibrated(lv_event_t *event);
  static void save_calibration_coeff(lv_tc_coeff_t coeff);
*/
/*  static void refresh_theme(); */
};

#endif  // __GUPPY_SCREEN_H__
