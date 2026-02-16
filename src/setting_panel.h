#ifndef __SETTING_PANEL_H__
#define __SETTING_PANEL_H__

#include "platform.h"

#ifndef GUPPY_FF5M
#ifndef OS_ANDROID
#include "wifi_panel.h"
#endif
#endif

#include "sysinfo_panel.h"
//#include "spoolman_panel.h"
#ifndef GUPPY_FF5M
#include "printer_select_panel.h"
#endif
#include "button_container.h"
#include "websocket_client.h"
#include "lvgl/lvgl.h"

#include <mutex>

class SettingPanel {
 public:
  SettingPanel(KWebSocketClient &c, std::mutex &l, lv_obj_t *parent/*, SpoolmanPanel &sm*/);
  ~SettingPanel();

  lv_obj_t *get_container();
//  void enable_spoolman();

  void handle_callback(lv_event_t *event);

  static void _handle_callback(lv_event_t *event) {
    SettingPanel *panel = (SettingPanel*)event->user_data;
    panel->handle_callback(event);
  };

 private:
  KWebSocketClient &ws;
  lv_obj_t *cont;

#ifndef GUPPY_FF5M
#ifndef OS_ANDROID
  WifiPanel wifi_panel;
#endif
#endif

  SysInfoPanel sysinfo_panel;
//  SpoolmanPanel &spoolman_panel;
#ifndef GUPPY_FF5M
  PrinterSelectPanel printer_select_panel;
  ButtonContainer wifi_btn;
#endif
  ButtonContainer restart_klipper_btn;
  ButtonContainer restart_firmware_btn;
  ButtonContainer sysinfo_btn;
//  ButtonContainer spoolman_btn;
  ButtonContainer guppy_restart_btn;
  ButtonContainer power_btn;
  ButtonContainer display_on_btn;
  ButtonContainer web_btn;
  ButtonContainer zrestore_btn;
#ifndef GUPPY_FF5M
  ButtonContainer guppy_update_btn;
  ButtonContainer printer_select_btn;
#endif

};

#endif // __SETTING_PANEL_H__

