#ifndef __PID_PANEL_H__
#define __PID_PANEL_H__

#include "sensor_container.h"
#include "numpad.h"
#include "button_container.h"
#include "lvgl/lvgl.h"
#include "websocket_client.h"
#include "notify_consumer.h"

#include <mutex>

class PidPanel : public NotifyConsumer {
 public:
  PidPanel(KWebSocketClient &c, std::mutex &l);
  ~PidPanel();

  void foreground();
  void handle_callback(lv_event_t *event);
  void consume(json &j);
  static void _handle_callback(lv_event_t *event) {
    PidPanel *panel = (PidPanel*)event->user_data;
    panel->handle_callback(event);
  };

 private:
  KWebSocketClient &ws;
  lv_obj_t *cont;
  Numpad numpad;

  lv_obj_t *temp_cont;
  lv_obj_t *temp_chart;
  lv_chart_series_t *extruder_temp_series;
  lv_chart_series_t *heater_bed_temp_series;
  lv_chart_series_t *weight_temp_series;
  SensorContainer extruder_temp;
  SensorContainer heater_bed_temp;
  SensorContainer weight_temp;
  ButtonContainer pid_extruder_btn;
  ButtonContainer pid_bed_btn;
  ButtonContainer clear_nozzle_btn;
  ButtonContainer load_cell_tare_btn;
  ButtonContainer bed_level_screws_tune_btn;
  ButtonContainer back_btn;
};

#endif // __PID_PANEL_H__
