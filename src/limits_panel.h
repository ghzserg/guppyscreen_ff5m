#ifndef __LIMITS_PANEL_H__
#define __LIMITS_PANEL_H__

#include "slider_container.h"
#include "button_container.h"
#include "lvgl/lvgl.h"
#include "websocket_client.h"
#include "notify_consumer.h"

#include <mutex>

class LimitsPanel : public NotifyConsumer {
 public:
  LimitsPanel(KWebSocketClient &c, std::mutex &l);
  ~LimitsPanel();

  void init(json &j);
  void foreground();
  void consume(json &j);

  void handle_callback(lv_event_t *event);

  static void _handle_callback(lv_event_t *event) {
    LimitsPanel *panel = (LimitsPanel*)event->user_data;
    panel->handle_callback(event);
  };

 private:
  KWebSocketClient &ws;
  lv_obj_t *cont;
  lv_obj_t *limit_cont;
  SliderContainer velocity;
  SliderContainer acceleration;
  SliderContainer square_corner;
  SliderContainer accel_to_decel;
  int max_velocity_default;
  int max_accel_default;
  double max_accel_to_decel_default;
  int square_corner_default;

  enum class MotionLimit { None, AccelToDecel, MinimumCruiseRatio };
  MotionLimit motion_limit = MotionLimit::None;
  double minimum_cruise_ratio_default = 0.5;
  void configure_motion_limit(const json &settings, const json &toolhead);
  void update_motion_limit(const json &toolhead);
  void send_motion_limit(double value);

};

#endif // __LIMITS_PANEL_H__
