#ifndef __MACROS_PANEL_H__
#define __MACROS_PANEL_H__

#include "websocket_client.h"
#include "macro_item.h"
#include "lvgl/lvgl.h"

#include <vector>
#include <memory>
#include <mutex>

class MacrosPanel {
 public:
  MacrosPanel(KWebSocketClient &c, std::mutex &l, lv_obj_t *parent);
  ~MacrosPanel();

  // The macro list (~12 LVGL objects per macro, ~330 kB for zMod's macros)
  // is built when the tab is first shown, and kept: freeing it on leaving
  // the tab gives little back (heap fragmentation) and building takes
  // ~0.4 s on the printer. Call with lv_lock held.
  void foreground();   // the tab is shown: build the list if needed
  void background();   // the tab is left
  void refresh();      // the macros changed (reconnect): rebuild if shown, else free
  void handle_hide_show(lv_event_t *e);

  static void _handle_hide_show(lv_event_t *e) {
    MacrosPanel *panel = (MacrosPanel*)e->user_data;
    panel->handle_hide_show(e);
  };

 private:
  KWebSocketClient &ws;
  std::mutex &lv_lock;
  lv_obj_t *cont;
  lv_obj_t *top_controls;
  lv_obj_t *show_hide_switch;
  lv_obj_t *top_cont;
  lv_obj_t *kb;
  std::vector<std::shared_ptr<MacroItem>> macro_items;
  bool shown = false;  // the tab is showing
  bool built = false;  // macro_items holds the list

  void populate();
  static void _rebuild(void *panel);  // lv_async_call callback

};

#endif // __MACROS_PANEL_H__
