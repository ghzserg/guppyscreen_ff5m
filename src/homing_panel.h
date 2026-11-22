#ifndef __HOMING_PANEL_H__
#define __HOMING_PANEL_H__

#include "lvgl/lvgl.h"
#include "button_container.h"
#include "websocket_client.h"
#include "selector.h"
#include "notify_consumer.h"

#include <mutex>

class HomingPanel : public NotifyConsumer {
 public:
  HomingPanel(KWebSocketClient &ws, std::mutex &);
  ~HomingPanel();

  void consume(json &data);
  lv_obj_t * get_container();
  void foreground();
  void set_print_locked(bool printing, bool busy) {
    ButtonContainer::set_print_locked(busy, {&home_all_btn, &home_xy_btn, &motoroff_btn});
    ButtonContainer::set_print_locked(printing, {&x_up_btn, &x_down_btn, &y_up_btn, &y_down_btn, &z_up_btn, &z_down_btn});
    if (printing) lv_btnmatrix_set_btn_ctrl_all(distance_selector.get_selector(), LV_BTNMATRIX_CTRL_DISABLED);
    else lv_btnmatrix_clear_btn_ctrl_all(distance_selector.get_selector(), LV_BTNMATRIX_CTRL_DISABLED);
  }
  void update_motion_status();
  void handle_callback(lv_event_t *event);
  void handle_selector_cb(lv_event_t *event);

  static void _handle_callback(lv_event_t *event) {
    HomingPanel *panel = (HomingPanel*)event->user_data;
    panel->handle_callback(event);
  };

  static void _handle_selector_cb(lv_event_t *event) {
    HomingPanel *panel = (HomingPanel*)event->user_data;
    panel->handle_selector_cb(event);
  };

 private:
  void update_motion_status(const json &move, bool partial);

  KWebSocketClient &ws;
  lv_obj_t *homing_cont;
  ButtonContainer home_all_btn;
  ButtonContainer home_xy_btn;
  ButtonContainer y_up_btn;
  ButtonContainer y_down_btn;
  ButtonContainer x_up_btn;
  ButtonContainer x_down_btn;
  ButtonContainer z_up_btn;
  ButtonContainer z_down_btn;
  ButtonContainer motoroff_btn;
  Selector distance_selector;
  lv_obj_t *motion_status;
  lv_obj_t *motion_labels[5];
};

#endif // __HOMING_PANEL_H__
