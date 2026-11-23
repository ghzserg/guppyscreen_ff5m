#ifndef __BEDMESH_PANEL_H__
#define __BEDMESH_PANEL_H__

#include "websocket_client.h"
#include "notify_consumer.h"
#include "button_container.h"
#include "numpad.h"
#include "lvgl/lvgl.h"

#include <mutex>
#include <vector>

class BedMeshPanel : public NotifyConsumer {
 public:
  BedMeshPanel(KWebSocketClient &c, std::mutex &l);
  ~BedMeshPanel();

  void consume(json &j);
  void foreground();
  // As with the weight controls: show the nozzle field only if Klipper
  // exposes the hardware feature requiring nozzle preheat.
  void show_screw_nozzle_temp(bool show);
  void set_print_locked(bool locked) {
    ButtonContainer::set_print_locked(locked, {&save_btn, &clear_btn, &auto_btn, &calibrate_btn});
    for (auto *obj : {screw_nozzle_btn, screw_bed_btn, screw_adjust_btn}) {
      if (locked) {
        lv_obj_add_state(obj, LV_STATE_DISABLED);
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
      } else {
        lv_obj_clear_state(obj, LV_STATE_DISABLED);
        lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
      }
    }
    for (auto *obj : {mesh_table, profile_table}) {
      if (locked) {
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_state(obj, LV_STATE_DISABLED);
      } else {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_state(obj, LV_STATE_DISABLED);
      }
    }
    if (locked && !lv_obj_has_flag(prompt, LV_OBJ_FLAG_HIDDEN)) {
      lv_obj_add_flag(prompt, LV_OBJ_FLAG_HIDDEN);
      lv_obj_move_background(prompt);
    }
  }
  void refresh_views_with_lock(json &);
  void refresh_views(json &);
  void refresh_profile_info(std::string pname);
  void handle_callback(lv_event_t *event);
  void handle_profile_action(lv_event_t *event);
  void handle_prompt_save(lv_event_t *event);
  void handle_prompt_cancel(lv_event_t *event);
  void handle_kb_input(lv_event_t *e);
  void mesh_draw_cb(lv_event_t *e);

  static void _handle_callback(lv_event_t *event) {
    BedMeshPanel *panel = (BedMeshPanel*)event->user_data;
    panel->handle_callback(event);
  };

  static void _handle_profile_action(lv_event_t *event) {
    BedMeshPanel *panel = (BedMeshPanel*)event->user_data;
    panel->handle_profile_action(event);
  };

  static void _handle_prompt_save(lv_event_t *event) {
    BedMeshPanel *panel = (BedMeshPanel*)event->user_data;
    panel->handle_prompt_save(event);
  };

  static void _handle_prompt_cancel(lv_event_t *event) {
    BedMeshPanel *panel = (BedMeshPanel*)event->user_data;
    panel->handle_prompt_cancel(event);
  };

  static void _handle_kb_input(lv_event_t *e) {
    BedMeshPanel *panel = (BedMeshPanel*)e->user_data;
    panel->handle_kb_input(e);
  };

  static void _mesh_draw_cb(lv_event_t *e) {
    BedMeshPanel *panel = (BedMeshPanel*)e->user_data;
    panel->mesh_draw_cb(e);
  };

  static void _table_event_cb(lv_event_t *e) {
    BedMeshPanel *panel = (BedMeshPanel*)e->user_data;
    panel->table_event_cb(e);
  };

 private:
  void getCoordinates(int row, int col);
  void table_event_cb(lv_event_t *e);
  KWebSocketClient &ws;
  lv_obj_t *cont;
  lv_obj_t *prompt;
  lv_obj_t *top_cont;
  lv_obj_t *mesh_table;
  lv_obj_t *profile_cont;
  lv_obj_t *profile_table;
  lv_obj_t *profile_info;
  lv_obj_t *screws_cont;
  lv_obj_t *screw_nozzle_label;
  lv_obj_t *screw_nozzle_btn;
  lv_obj_t *screw_nozzle_value;
  lv_obj_t *screw_bed_label;
  lv_obj_t *screw_bed_btn;
  lv_obj_t *screw_bed_value;
  lv_obj_t *screw_adjust_btn;
  Numpad screws_numpad;
  lv_obj_t *controls_cont;
  ButtonContainer save_btn;
  ButtonContainer clear_btn;
  ButtonContainer auto_btn;
  ButtonContainer calibrate_btn;
  lv_obj_t *msgbox;
  lv_obj_t *input;
  lv_obj_t *kb;
  std::string active_profile;
  std::vector<std::vector<double>> mesh;
  double sr_offset;
  double delta;
  double min_x;
  double max_x;
  double min_y;
  double max_y;
  int x_count;
  int y_count;
  lv_color_t color_gradient(double offset);
  lv_color_t color_gradient2(double offset);
};

#endif // __BEDMESH_PANEL_H__
