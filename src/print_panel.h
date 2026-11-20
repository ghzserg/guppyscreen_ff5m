#ifndef __PRINT_PANEL_H__
#define __PRINT_PANEL_H__

#include "lvgl/lvgl.h"
#include "websocket_client.h"
#include "notify_consumer.h"
#include "button_container.h"
#include "file_panel.h"
#include "print_status_panel.h"
#include "tree.h"

class PrintPanel : public NotifyConsumer {
 public:
  PrintPanel(KWebSocketClient &ws, std::mutex &lv_lock, PrintStatusPanel &ps);
  ~PrintPanel();

  void consume(json &data);
  void subscribe();
  void handle_callback(lv_event_t *event);
  void handle_metadata(const std::string &path, uint32_t request_id, json &data);
  void handle_print_callback(lv_event_t *event);
  void handle_delete_callback(lv_event_t *event);
  void handle_btns(lv_event_t *event);

  static void _handle_callback(lv_event_t *event) {
    PrintPanel *panel = (PrintPanel*)event->user_data;
    panel->handle_callback(event);
  };

  static void _handle_print_callback(lv_event_t *event) {
    PrintPanel *panel = (PrintPanel*)event->user_data;
    panel->handle_print_callback(event);
  };


  static void _handle_delete_callback(lv_event_t *event) {
    PrintPanel *panel = (PrintPanel*)event->user_data;
    panel->handle_delete_callback(event);
  };

  static void _handle_btns(lv_event_t *event) {
    PrintPanel *panel = (PrintPanel*)event->user_data;
    panel->handle_btns(event);
  };


 private:
  void show_dir(Tree *dir);
  void show_file_detail(Tree *f);

  KWebSocketClient &ws;
  lv_obj_t *files_cont;

#ifndef GUPPY_FF5M
  // prompt
  lv_obj_t *prompt_cont;
  lv_obj_t *msgbox;
  lv_obj_t *job_btn;
  lv_obj_t *cancel_btn;
  lv_obj_t *queue_btn;
#endif

  lv_obj_t *left_cont;
  lv_obj_t *file_table_btns;
  lv_obj_t *refresh_btn;
  lv_obj_t *modified_sort_btn;
  lv_obj_t *az_sort_btn;

  lv_obj_t *file_table;
  lv_obj_t *file_view;
  ButtonContainer print_btn;
  ButtonContainer delete_btn;
  Tree root;
  Tree *cur_dir;
  Tree *cur_file;
  FilePanel file_panel;
  PrintStatusPanel &print_status;
  enum Sort { SortByName, SortByModified };
  Sort sorted_by = SortByModified;
  bool reverse_sort = false;
  uint32_t metadata_request_id = 0;
  bool metadata_loaded = false;
  bool delete_pending = false;
};

#endif // __PRINT_PANEL_H__
