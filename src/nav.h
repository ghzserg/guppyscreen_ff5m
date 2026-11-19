#ifndef __NAV_H__
#define __NAV_H__

// The navigation bar on the left of every screen, and the content area
// to its right that holds the screens.
//
//   lv_scr_act()
//   ├── bar      x 0..59: tab buttons (Home, Print, Tune, Settings), Stop at the bottom
//   └── content  the rest: tab roots, the screens opened from them, dialogs
//
// Panels open a screen with lv_obj_move_foreground() and close it with
// lv_obj_move_background(), as before. A screen is "open" when it is above
// the topmost tab root in the content area. Activating a tab closes the
// open screens (running their leave actions) and brings the tab's root to
// the front; open dialogs stay on top.

#include "lvgl/lvgl.h"

#include <functional>
#include <map>
#include <vector>

class Nav {
 public:
  enum Tab { HOME, PRINT, TUNE, SETTINGS, TAB_COUNT };

  // The first call creates the bar and the content area on lv_scr_act().
  static Nav &get();

  lv_obj_t *bar();
  lv_obj_t *content();
  // The content area's width: what a screen is laid out for (740 on the AD5M).
  static lv_coord_t content_width();
  // Место кнопки вкладки в панели в координатах экрана (96 px высотой на
  // AD5M): по нему выравниваются экраны.
  static lv_coord_t slot_height();
  static lv_coord_t slot_top(Tab t);

  // A new, empty screen in the content area (100% x 100%, no padding or
  // border, not scrollable).
  lv_obj_t *add_screen();

  // A new tab with a new, empty root (as add_screen()).
  lv_obj_t *add_tab(Tab t, const char *glyph);
  // A tab whose root already exists in the content area.
  void add_tab(Tab t, lv_obj_t *root, const lv_img_dsc_t *icon);

  // Swap a tab's root. If the tab is active, the new root is shown and the
  // screens open over the old one are closed.
  void set_root(Tab t, lv_obj_t *root);
  lv_obj_t *root(Tab t) const;

  void activate(Tab t);
  Tab active() const;

  // An orange tab button (the Print tab while printing), darker when the
  // tab is the active one.
  void set_attention(Tab t, bool on);

  // Run `action` when `screen` (a screen or a tab root) is closed or left
  // through the bar: what its Back button did besides hiding it.
  void on_leave(lv_obj_t *screen, std::function<void()> action);

  // A dialog in the content area: kept on top when a tab is activated, and
  // the tab buttons don't work while it is open.
  void add_dialog(lv_obj_t *dialog);
  bool dialog_open() const;

  // What Stop does (after the confirmation, if /prompt_emergency_stop).
  void set_stop(std::function<void()> stop);

  lv_obj_t *tab_button(Tab t);  // nullptr until the tab is added
  lv_obj_t *stop_button();

  // Once every panel exists: closes every screen, shows Home.
  void start();

 private:
  Nav();
  lv_obj_t *add_button(Tab t);
  int top_root_index() const;
  bool is_dialog(lv_obj_t *obj) const;
  void leave(lv_obj_t *screen);
  void close_open_screens_and_show(lv_obj_t *root);
  void highlight();

  static void tab_clicked(lv_event_t *e);
  static void stop_clicked(lv_event_t *e);

  lv_obj_t *bar_;
  lv_obj_t *content_;
  lv_obj_t *stop_;
  lv_obj_t *roots[TAB_COUNT] = {};
  lv_obj_t *buttons[TAB_COUNT] = {};
  Tab active_ = HOME;
  std::map<lv_obj_t *, std::function<void()>> leave_actions;
  std::vector<lv_obj_t *> dialogs;
  std::function<void()> stop_action;
};

#endif  // __NAV_H__
