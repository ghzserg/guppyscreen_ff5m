#ifndef __SWITCH_CONTAINER_H__
#define __SWITCH_CONTAINER_H__

#include "lvgl/lvgl.h"
#include <string>
#include <functional>
#include "websocket_client.h"
#include "notify_consumer.h"

#include <mutex>

class SwitchContainer {
 public:
  SwitchContainer(lv_obj_t *parent,
                  const char *text,
                  void *user_data,
                  KWebSocketClient &c,
                  const std::string &prompt_text = {});
  ~SwitchContainer();

  lv_obj_t *get_container();
  lv_obj_t *get_switch();
  void disable();
  void enable();
  void hide();
  void on();
  void on_exlude();
  void off();

  std::string get_name() {
    return prompt_text;
  };

  void handle_prompt();
  void run_callback();

 private:
  lv_obj_t *swt_cont;
  lv_obj_t *swt;
  lv_obj_t *label;
  KWebSocketClient &ws;
  std::string prompt_text;
  std::function<void()> prompt_callback;
};

#endif // __SWITCH_CONTAINER_H__
