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
#ifdef GUPPY_FF5M
  // Кнопки циркуляции, если `circulation` (их нет на экране вентиляторов), и
  // калибровки, если у Klipper есть их макросы; без циркуляции калибровки
  // в первом ряду. Возвращает, показана ли хоть одна кнопка.
  bool configure(bool circulation, bool calibrate_extruders, bool calibrate_vfa);
#endif
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
  ButtonContainer calibrate_extruders_btn;
  ButtonContainer calibrate_vfa_btn;
};

#endif // __PRO_PANEL_H__
