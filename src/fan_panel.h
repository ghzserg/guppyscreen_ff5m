#ifndef __FAN_PANEL_H__
#define __FAN_PANEL_H__

#include "lvgl/lvgl.h"
#include "websocket_client.h"
#include "notify_consumer.h"
#include "slider_container.h"
#include "button_container.h"

#include <map>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

class FanPanel : public NotifyConsumer {
 public:
  FanPanel(KWebSocketClient &ws, std::mutex &lock);
  ~FanPanel();

  void consume(json &j);

  lv_obj_t *get_container();
  void create_fans(json &f);
  void foreground();
#ifdef GUPPY_FF5M
  // The AD5M Pro's air circulation buttons in a column right of the fans.
  void show_air_circulation(bool show);
  void handle_air_circulation(lv_event_t *event);
  static void _handle_air_circulation(lv_event_t *event) {
    ((FanPanel *)event->user_data)->handle_air_circulation(event);
  };
#endif
  void handle_fan_update(lv_event_t *event);
  void handle_fan_update_part_fan(lv_event_t *event);
  void handle_fan_update_generic(lv_event_t *event);


  static void _handle_fan_update(lv_event_t *event) {
    FanPanel *panel = (FanPanel*)event->user_data;
    panel->handle_fan_update(event);
  };

  static void _handle_fan_update_part_fan(lv_event_t *event) {
    FanPanel *panel = (FanPanel*)event->user_data;
    panel->handle_fan_update_part_fan(event);
  };

  static void _handle_fan_update_generic(lv_event_t *event) {
    FanPanel *panel = (FanPanel*)event->user_data;
    panel->handle_fan_update_generic(event);
  };

 private:

  KWebSocketClient &ws;
  lv_obj_t *fanpanel_cont;
  lv_obj_t *fans_cont;
  // In the screen's order (FF5M: see create_fans).
  std::vector<std::pair<std::string, std::shared_ptr<SliderContainer>>> fans;
#ifdef GUPPY_FF5M
  lv_obj_t *air_cont;
  ButtonContainer air_internal_btn;
  ButtonContainer air_external_btn;
  ButtonContainer air_stop_btn;
#endif
  /* SliderContainer fan0; */
  /* SliderContainer fan1; */
  /* SliderContainer fan2; */
};

#endif // __FAN_PANEL_H__
