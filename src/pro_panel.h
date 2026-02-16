#ifndef __PRO_PANEL_H__
#define __PRO_PANEL_H__

#include "slider_container.h"
#include "button_container.h"
#include "lvgl/lvgl.h"
#include "websocket_client.h"
#include "notify_consumer.h"

#include <mutex>

class ProPanel : public NotifyConsumer {
 public:
  ProPanel(KWebSocketClient &c, std::mutex &l);
  ~ProPanel();

  void foreground();
  void handle_callback(lv_event_t *event);
  void consume(json &j);
  static void _handle_callback(lv_event_t *event) {
    ProPanel *panel = (ProPanel*)event->user_data;
    panel->handle_callback(event);
  };

 private:
  KWebSocketClient &ws;
  lv_obj_t *cont;
  ButtonContainer air_circulation_internal_btn;
  ButtonContainer air_circulation_external_btn;
  ButtonContainer air_circulation_stop_btn;
  ButtonContainer back_btn;
};

#endif // __PRO_PANEL_H__
