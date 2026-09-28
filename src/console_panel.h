#ifndef __CONSOLE_PANEL_H__
#define __CONSOLE_PANEL_H__

#include "lvgl.h"
#include "websocket_client.h"

#include <deque>
#include <list>
#include <mutex>
#include <string>

class ConsolePanel {
public:
  ConsolePanel(KWebSocketClient &websocket_client, std::mutex &lock, lv_obj_t *parent);
  ~ConsolePanel();

  lv_obj_t *get_container();
  void handle_macros(json &j);
  void handle_macro_response(json &j);

private:
  // ---- static LVGL dispatchers (registry entry points) ----
  static void _handle_kb_input(lv_event_t *e) {
    auto *self = static_cast<ConsolePanel *>(lv_event_get_user_data(e));
    if (self) self->handle_kb_input(e);
  }
  static void _handle_select_macro(lv_event_t *e) {
    auto *self = static_cast<ConsolePanel *>(lv_event_get_user_data(e));
    if (self) self->handle_select_macro(e);
  }
  static void _handle_send_macro(lv_event_t *e) {
    auto *self = static_cast<ConsolePanel *>(lv_event_get_user_data(e));
    if (self) self->handle_send_macro(e);
  }
  static void _handle_clear_input(lv_event_t *e) {
    auto *self = static_cast<ConsolePanel *>(lv_event_get_user_data(e));
    if (self) self->handle_clear_input(e);
  }
  static void _output_flush_timer_cb(lv_timer_t *t) {
    auto *self = static_cast<ConsolePanel *>(t->user_data);
    if (self) self->flush_output();
  }

  // ---- instance handlers ----
  void handle_kb_input(lv_event_t *e);
  void handle_select_macro(lv_event_t *e);
  void handle_send_macro(lv_event_t *e);
  void handle_clear_input(lv_event_t *e);

  // ---- console output helpers ----
  void append_line(const std::string &line);
  void flush_output();

  // ---- members ----
  KWebSocketClient &ws;
  std::mutex &lv_lock;

  lv_obj_t *console_cont;
  lv_obj_t *top_cont;
  lv_obj_t *output;         // scrollable container of labels (NOT a textarea anymore)
  lv_obj_t *macro_list;
  lv_obj_t *input_cont;
  lv_obj_t *input;
  lv_obj_t *kb;

  std::list<std::string> history;
  std::list<std::string> all_macros;

  // console output: one line == one lv_label
  std::deque<lv_obj_t *> output_lines;
  static constexpr size_t MAX_LINES = 200;

  // throttle buffer for console output (cross-thread)
  std::string pending_buffer;
  std::mutex  pending_mutex;
  lv_timer_t *output_flush_timer = nullptr;
};

#endif // __CONSOLE_PANEL_H__