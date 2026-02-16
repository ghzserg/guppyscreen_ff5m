#include "setting_panel.h"
#include "config.h"
#include "spdlog/spdlog.h"
#include "subprocess.hpp"

#include <experimental/filesystem>

namespace fs = std::experimental::filesystem;
namespace sp = subprocess;
#ifndef GUPPY_FF5M
LV_IMG_DECLARE(network_img);
#endif
LV_IMG_DECLARE(power_img);
LV_IMG_DECLARE(display_on_img);
LV_IMG_DECLARE(web_img);
LV_IMG_DECLARE(refresh_img);
LV_IMG_DECLARE(zrestore_img);
//LV_IMG_DECLARE(spoolman_img);
#ifndef GUPPY_FF5M
LV_IMG_DECLARE(update_img);
#endif

#ifdef ZBOLT
LV_IMG_DECLARE(info_img);
#else
LV_IMG_DECLARE(sysinfo_img);
#endif

LV_IMG_DECLARE(print);

SettingPanel::SettingPanel(KWebSocketClient &c, std::mutex &l, lv_obj_t *parent/*, SpoolmanPanel &sm*/)
  : ws(c)
  , cont(lv_obj_create(parent))
#ifndef GUPPY_FF5M
#ifndef OS_ANDROID
  , wifi_panel(l)
#endif
#endif
  , sysinfo_panel(c)
//  , spoolman_panel(sm)
#ifndef GUPPY_FF5M
  , wifi_btn(cont, &network_img, "WiFi", &SettingPanel::_handle_callback, this)
#endif
  , restart_klipper_btn(cont, &refresh_img, _("Klipper Restart") /* "Рестарт\nKlipper" */, &SettingPanel::_handle_callback, this)
  , restart_firmware_btn(cont, &refresh_img, _("Firmware Restart") /* "Рестарт\nFirmware" */, &SettingPanel::_handle_callback, this)
#ifdef ZBOLT
  , sysinfo_btn(cont, &info_img, _("System") /* "Система" */, &SettingPanel::_handle_callback, this)
#else
  , sysinfo_btn(cont, &sysinfo_img, _("System") /* "Система" */, &SettingPanel::_handle_callback, this)
#endif
//  , spoolman_btn(cont, &spoolman_img, "Spoolman", &SettingPanel::_handle_callback, this)
  , guppy_restart_btn(cont, &refresh_img, _("Guppy Restart") /* "Рестарт\nGuppy" */, &SettingPanel::_handle_callback, this)
  , power_btn(cont, &power_img, _("Power Off") /* "Выключить" */, &SettingPanel::_handle_callback, this)
  , display_on_btn(cont, &display_on_img, _("Native screen") /* "Родной экран" */, &SettingPanel::_handle_callback, this)
  , web_btn(cont, &web_img, _("Change WEB") /* "Сменить WEB" */, &SettingPanel::_handle_callback, this)
  , zrestore_btn(cont, &zrestore_img, _("Restore Print") /* "Восстановить печать" */, &SettingPanel::_handle_callback, this)
#ifndef GUPPY_FF5M
  , guppy_update_btn(cont, &update_img, "Update Guppy", &SettingPanel::_handle_callback, this)
  , printer_select_btn(cont, &print, "Printers", &SettingPanel::_handle_callback, this)
#endif
{
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));

#ifndef GUPPY_FF5M
  spoolman_btn.disable();
#endif

#ifndef GUPPY_FF5M
  // needs to be disabled as it's compiled into the buildroot environment
  guppy_update_btn.disable();
#endif

#ifndef GUPPY_FF5M
#ifdef OS_ANDROID
  wifi_btn.disable();
#endif
#endif

  static lv_coord_t grid_main_row_dsc[] = {LV_GRID_FR(2), LV_GRID_FR(5), LV_GRID_FR(5), LV_GRID_TEMPLATE_LAST};
  static lv_coord_t grid_main_col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
      LV_GRID_TEMPLATE_LAST};

  lv_obj_set_grid_dsc_array(cont, grid_main_col_dsc, grid_main_row_dsc);

  // row 1
#ifndef GUPPY_FF5M
  lv_obj_set_grid_cell(wifi_btn.get_container(),             LV_GRID_ALIGN_CENTER, 0, 1, LV_GRID_ALIGN_START, 1, 1);
#endif
  lv_obj_set_grid_cell(restart_klipper_btn.get_container(),  LV_GRID_ALIGN_CENTER, 0, 1, LV_GRID_ALIGN_START, 1, 1);
  lv_obj_set_grid_cell(restart_firmware_btn.get_container(), LV_GRID_ALIGN_CENTER, 1, 1, LV_GRID_ALIGN_START, 1, 1);
  lv_obj_set_grid_cell(guppy_restart_btn.get_container(),    LV_GRID_ALIGN_CENTER, 2, 1, LV_GRID_ALIGN_START, 1, 1);
  lv_obj_set_grid_cell(sysinfo_btn.get_container(),          LV_GRID_ALIGN_CENTER, 3, 1, LV_GRID_ALIGN_START, 1, 1);

  // row 2
  lv_obj_set_grid_cell(power_btn.get_container(),            LV_GRID_ALIGN_CENTER, 0, 1, LV_GRID_ALIGN_START, 2, 1);
  lv_obj_set_grid_cell(display_on_btn.get_container(),       LV_GRID_ALIGN_CENTER, 1, 1, LV_GRID_ALIGN_START, 2, 1);
  lv_obj_set_grid_cell(web_btn.get_container(),              LV_GRID_ALIGN_CENTER, 2, 1, LV_GRID_ALIGN_START, 2, 1);
  lv_obj_set_grid_cell(zrestore_btn.get_container(),         LV_GRID_ALIGN_CENTER, 3, 1, LV_GRID_ALIGN_START, 2, 1);
//  lv_obj_set_grid_cell(spoolman_btn.get_container(),         LV_GRID_ALIGN_CENTER, 3, 1, LV_GRID_ALIGN_START, 2, 1);

#ifndef GUPPY_FF5M
  lv_obj_set_grid_cell(guppy_update_btn.get_container(), LV_GRID_ALIGN_CENTER, 1, 1, LV_GRID_ALIGN_START, 2, 1);
  lv_obj_set_grid_cell(printer_select_btn.get_container(), LV_GRID_ALIGN_CENTER, 2, 1, LV_GRID_ALIGN_START, 2, 1);
#endif
}

SettingPanel::~SettingPanel() {
  if (cont != NULL) {
    lv_obj_del(cont);
    cont = NULL;
  }
}

lv_obj_t *SettingPanel::get_container() {
  return cont;
}

void SettingPanel::handle_callback(lv_event_t *event) {
  if (lv_event_get_code(event) == LV_EVENT_SHORT_CLICKED) {
    lv_obj_t *btn = lv_event_get_current_target(event);

#ifndef GUPPY_FF5M
    if (btn == wifi_btn.get_container()) {
      spdlog::trace("wifi pressed");
#ifndef OS_ANDROID
      wifi_panel.foreground();
#endif
    } else
#endif
    if (btn == sysinfo_btn.get_container()) {
      spdlog::trace("setting system info pressed");
      sysinfo_panel.foreground();
    } else if (btn == restart_klipper_btn.get_container()) {
      spdlog::trace("setting restart klipper pressed");
      ws.send_jsonrpc("printer.restart");
    } else if (btn == restart_firmware_btn.get_container()) {
      spdlog::trace("setting restart klipper pressed");
      ws.send_jsonrpc("printer.firmware_restart");
    } else if (btn == power_btn.get_container()) {
      spdlog::trace("power pressed");
      ws.gcode_script("_SHUTDOWN");
    } else if (btn == display_on_btn.get_container()) {
      spdlog::trace("display_on pressed");
      ws.gcode_script("DISPLAY_ON");
    } else if (btn == web_btn.get_container()) {
      spdlog::trace("web pressed");
      ws.gcode_script("WEB");
    } else if (btn == zrestore_btn.get_container()) {
      spdlog::trace("zrestore");
      ws.gcode_script("_ZRESTORE");
//    } else if (btn == spoolman_btn.get_container()) {
//      spdlog::trace("setting spoolman pressed");
//      spoolman_panel.foreground();
    } else if (btn == guppy_restart_btn.get_container()) {
      spdlog::trace("restart guppy pressed");
      Config *conf = Config::get_instance();
      auto init_script = conf->get<std::string>("/guppy_init_script");
      const fs::path script(init_script);
      if (fs::exists(script) || init_script.rfind("service guppyscreen", 0) == 0) {
        sp::call({init_script, "restart"});
      } else {
                spdlog::warn("Failed to restart Guppy Screen. Did not find restart script.");
      }
#ifndef GUPPY_FF5M
    } else if (btn == guppy_update_btn.get_container()) {
      spdlog::trace("update guppy pressed");
      // TODO: throw this inside the global threadpool to make it async
      auto update_script = fs::canonical("/proc/self/exe").parent_path() / "update.sh";
      const fs::path script(update_script);
      if (fs::exists(script)) {
        sp::call(script);
      } else {
        spdlog::warn("Failed to update Guppy Screen. Did not find update script.");
      }
    } else if (btn == printer_select_btn.get_container()) {
      spdlog::trace("setting printers pressed");
      printer_select_panel.foreground();
#endif
    }
  }
}

//void SettingPanel::enable_spoolman() {
//  spoolman_btn.enable();
//}
