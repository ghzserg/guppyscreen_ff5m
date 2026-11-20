#ifndef __FAN_PANEL_H__
#define __FAN_PANEL_H__

#include "lvgl/lvgl.h"
#include "websocket_client.h"
#include "notify_consumer.h"
#include "slider_container.h"
#include "button_container.h"

#include <map>
#include <cmath>
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
  // Кнопки циркуляции воздуха (AD5M Pro, C5 Pro) столбцом справа от вентиляторов.
  void show_air_circulation(bool show);
  // Подсвечивает Int или Ext по последним значениям от Klipper: у AD5M Pro —
  // заслонка на угле из их макроса, у C5 Pro — скорости вентиляторов камеры.
  void show_air_mode();
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
  // В порядке на экране (FF5M: см. create_fans).
  std::vector<std::pair<std::string, std::shared_ptr<SliderContainer>>> fans;
#ifdef GUPPY_FF5M
  lv_obj_t *air_cont;
  ButtonContainer air_internal_btn;
  ButtonContainer air_external_btn;
  ButtonContainer air_stop_btn;
  // value заслонки в AIR_CIRCULATION_INTERNAL и _EXTERNAL и допуск сравнения с ним (1°)
  double air_value[2] = {NAN, NAN};
  double air_tol = 0;
  // Последние от Klipper: value заслонки (AD5M Pro), speed у chamber_heat_fan,
  // chamber_loop_fan, chamber_fan, chamber_cool_fan (C5 Pro)
  double air_now[5] = {NAN, NAN, NAN, NAN, NAN};
#endif
  /* SliderContainer fan0; */
  /* SliderContainer fan1; */
  /* SliderContainer fan2; */
};

#endif // __FAN_PANEL_H__
