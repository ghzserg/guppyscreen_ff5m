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

 private:
  KWebSocketClient &ws;
  lv_obj_t *cont;
  lv_obj_t *exclude_cont;
  std::vector<SwitchContainer*> switches;
};

#endif // __EXCLUDE_PANEL_H__
