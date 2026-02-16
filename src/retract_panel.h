#ifndef __RETRACT_PANEL_H__
#define __RETRACT_PANEL_H__

#include "slider_container.h"
#include "button_container.h"
#include "lvgl/lvgl.h"
#include "websocket_client.h"
#include "notify_consumer.h"

#include <mutex>

class RetractPanel : public NotifyConsumer {
 public:
  RetractPanel(KWebSocketClient &c, std::mutex &l);
  ~RetractPanel();

  void init(json &j);
  void foreground();
  void consume(json &j);

  void handle_callback(lv_event_t *event);

  static void _handle_callback(lv_event_t *event) {
    RetractPanel *panel = (RetractPanel*)event->user_data;
    panel->handle_callback(event);
  };

 private:
  KWebSocketClient &ws;
  lv_obj_t *cont;
  lv_obj_t *retract_cont;
  SliderContainer retract_length;
  SliderContainer retract_speed;
  SliderContainer unretract_extra_length;
  SliderContainer unretract_speed;
  ButtonContainer back_btn;
  float retract_length_default;
  int retract_speed_default;
  float unretract_extra_length_default;
  int unretract_speed_default;

};

#endif // __RETRACT_PANEL_H__
