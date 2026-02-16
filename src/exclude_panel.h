#ifndef __EXCLUDE_PANEL_H__
#define __EXCLUDE_PANEL_H__

#include "slider_container.h"
#include "button_container.h"
#include "switch_container.h"
#include "lvgl/lvgl.h"
#include "websocket_client.h"
#include "notify_consumer.h"

#include <mutex>

class ExcludePanel : public NotifyConsumer {
 public:
  ExcludePanel(KWebSocketClient &c, std::mutex &l);
  ~ExcludePanel();

  void init();
  void reset();
  void foreground();
  void background();
  void consume(json &j);

  void handle_callback(lv_event_t *event);

  static void _handle_callback(lv_event_t *event) {
    ExcludePanel *panel = (ExcludePanel*)event->user_data;
    panel->handle_callback(event);
  };

 private:
  KWebSocketClient &ws;
  lv_obj_t *cont;
  lv_obj_t *exclude_cont;
  std::vector<SwitchContainer*> switches;
  ButtonContainer back_btn;
};

#endif // __EXCLUDE_PANEL_H__
