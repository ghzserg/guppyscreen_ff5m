#ifndef __BUTTON_CONTAINER_H__
#define __BUTTON_CONTAINER_H__

#include "lvgl/lvgl.h"

#include <string>
#include <functional>
#include <initializer_list>

class ButtonContainer {
 public:
  ButtonContainer(lv_obj_t *parent,
                  const void *btn_img,
                  const char *text,
                  lv_event_cb_t cb,
                  void *user_data,
                  const std::string &prompt_text = {},
                  const std::function<void()> &prompt_callback = {});
  ~ButtonContainer();

  lv_obj_t *get_container();
  lv_obj_t *get_button();
  void disable();
  void enable();
  static void set_print_locked(bool locked, std::initializer_list<ButtonContainer *> buttons);
  void hide();

  void set_image(const void *img);

  void handle_callback(lv_event_t *event);

  void handle_prompt();

  // A confirmation dialog with Cancel on the left and OK on the right.
  static void confirm(const std::string &text, std::function<void()> on_ok);
  // guppyconfig.json /prompt_emergency_stop: ask before an emergency stop.
  static bool prompt_emergency_stop();
  void run_callback();

  static void _handle_callback(lv_event_t *event) {
    ButtonContainer *button_container = (ButtonContainer*)event->user_data;
    button_container->handle_callback(event);
  };

 private:
  void refresh_enabled();
  bool enabled = true;
  bool print_locked = false;
  lv_obj_t *btn_cont;
  lv_obj_t *btn;
  lv_obj_t *label;
  std::string prompt_text;
  std::function<void()> prompt_callback;
};

#endif // __BUTTON_CONTAINER_H__
