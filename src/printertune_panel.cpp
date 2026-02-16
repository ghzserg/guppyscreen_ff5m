#include "printertune_panel.h"
#include "state.h"
#include "spdlog/spdlog.h"

#include <experimental/filesystem>

namespace fs = std::experimental::filesystem;

LV_IMG_DECLARE(bedmesh_img);
LV_IMG_DECLARE(fine_tune_img);
LV_IMG_DECLARE(inputshaper_img);
LV_IMG_DECLARE(limit_img);
LV_IMG_DECLARE(retract);
LV_IMG_DECLARE(motor_img);
LV_IMG_DECLARE(chart_img);
LV_IMG_DECLARE(pro_img);
LV_IMG_DECLARE(pid_img);

#ifndef ZBOLT
LV_IMG_DECLARE(belts_calibration_img);
#ifndef GUPPY_FF5M
LV_IMG_DECLARE(power_devices_img);
#endif
#else
LV_IMG_DECLARE(print);
#endif

PrinterTunePanel::PrinterTunePanel(KWebSocketClient &c, std::mutex &l, lv_obj_t *parent, FineTunePanel &finetune, RetractPanel &retract_p, ProPanel &pro_p, PidPanel &pid_p, PromptPanel &p)
  : cont(lv_obj_create(parent))
  , bedmesh_panel(c, l)
  , finetune_panel(finetune)
  , limits_panel(c, l)
  , retract_panel(retract_p)
  , pro_panel(pro_p)
  , pid_panel(pid_p)
  , inputshaper_panel(c, l, p)
  , belts_calibration_panel(c, l)
#ifndef GUPPY_FF5M
  , tmc_tune_panel(c)
  , tmc_status_panel(c, l)
  , power_panel(c, l)
#endif
  , finetune_btn(cont, &fine_tune_img, _("Fine Tune") /* "Настройки" */, &PrinterTunePanel::_handle_callback, this)
  , limits_btn(cont, &limit_img, _("Limits") /* "Ограничения" */, &PrinterTunePanel::_handle_callback, this)
  , retract_btn(cont, &retract, _("Retract") /* "Ретракт" */, &PrinterTunePanel::_handle_callback, this)
  , bedmesh_btn(cont, &bedmesh_img, _("Bed Mesh") /* "Стол" */, &PrinterTunePanel::_handle_callback, this)
  , inputshaper_btn(cont, &inputshaper_img, "Input Shaper", &PrinterTunePanel::_handle_callback, this)
#ifndef ZBOLT
  , belts_calibration_btn(cont, &belts_calibration_img, _("Belts/Shake") /* "Ремни/Тряска" */, &PrinterTunePanel::_handle_callback, this)
#else
  , belts_calibration_btn(cont, &inputshaper_img, _("Belts/Shake") /* "Ремни/Тряска" */, &PrinterTunePanel::_handle_callback, this)
#endif
  , pro_btn(cont, &pro_img, "Pro", &PrinterTunePanel::_handle_callback, this)
  , pid_btn(cont, &pid_img, "PID", &PrinterTunePanel::_handle_callback, this)
#ifndef GUPPY_FF5M
  , tmc_tune_btn(cont, &motor_img, "TMC Autotune", &PrinterTunePanel::_handle_callback, this)
  , tmc_status_btn(cont, &chart_img, "TMC Metrics", &PrinterTunePanel::_handle_callback, this)
#ifndef ZBOLT
  , power_devices_btn(cont, &power_devices_img, "Power Devices", &PrinterTunePanel::_handle_callback, this)
#else
  , power_devices_btn(cont, &print, "Power Devices", &PrinterTunePanel::_handle_callback, this)
#endif
#endif
{
  lv_obj_move_background(cont);

  lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));

#ifndef GUPPY_FF5M
  tmc_tune_btn.disable();
#endif
  static lv_coord_t grid_main_row_dsc[] = {LV_GRID_FR(2), LV_GRID_FR(5), LV_GRID_FR(5), LV_GRID_TEMPLATE_LAST};
  static lv_coord_t grid_main_col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
      LV_GRID_TEMPLATE_LAST};

  lv_obj_set_grid_dsc_array(cont, grid_main_col_dsc, grid_main_row_dsc);

  // row 1
  lv_obj_set_grid_cell(finetune_btn.get_container(),          LV_GRID_ALIGN_CENTER, 0, 1, LV_GRID_ALIGN_START, 1, 1);
  lv_obj_set_grid_cell(limits_btn.get_container(),            LV_GRID_ALIGN_CENTER, 1, 1, LV_GRID_ALIGN_START, 1, 1);
  lv_obj_set_grid_cell(retract_btn.get_container(),           LV_GRID_ALIGN_CENTER, 2, 1, LV_GRID_ALIGN_START, 1, 1);
  lv_obj_set_grid_cell(bedmesh_btn.get_container(),           LV_GRID_ALIGN_CENTER, 3, 1, LV_GRID_ALIGN_START, 1, 1);

  // row 2
  lv_obj_set_grid_cell(inputshaper_btn.get_container(),       LV_GRID_ALIGN_CENTER, 0, 1, LV_GRID_ALIGN_START, 2, 1);
  lv_obj_set_grid_cell(belts_calibration_btn.get_container(), LV_GRID_ALIGN_CENTER, 1, 1, LV_GRID_ALIGN_START, 2, 1);
  lv_obj_set_grid_cell(pro_btn.get_container(),               LV_GRID_ALIGN_CENTER, 2, 1, LV_GRID_ALIGN_START, 2, 1);
  lv_obj_set_grid_cell(pid_btn.get_container(),               LV_GRID_ALIGN_CENTER, 3, 1, LV_GRID_ALIGN_START, 2, 1);
#ifndef GUPPY_FF5M
  lv_obj_set_grid_cell(tmc_tune_btn.get_container(), LV_GRID_ALIGN_CENTER, 1, 1, LV_GRID_ALIGN_START, 2, 1);
  lv_obj_set_grid_cell(tmc_status_btn.get_container(), LV_GRID_ALIGN_CENTER, 2, 1, LV_GRID_ALIGN_START, 2, 1);
  lv_obj_set_grid_cell(power_devices_btn.get_container(), LV_GRID_ALIGN_CENTER, 3, 1, LV_GRID_ALIGN_START, 2, 1);
#endif
  // lv_obj_set_grid_cell(restart_firmware_btn.get_container(), LV_GRID_ALIGN_CENTER, 3, 1, LV_GRID_ALIGN_START, 2, 1);
}

PrinterTunePanel::~PrinterTunePanel() {
  if (cont != NULL) {
    lv_obj_del(cont);
    cont = NULL;
  }
}

lv_obj_t *PrinterTunePanel::get_container() {
  return cont;
}

BedMeshPanel& PrinterTunePanel::get_bedmesh_panel() {
  return bedmesh_panel;
}

#ifndef GUPPY_FF5M
PowerPanel& PrinterTunePanel::get_power_panel() {
  return power_panel;
}
#endif

void PrinterTunePanel::init(json &j) {
  limits_panel.init(j);
//  retract_panel.init(j);

#ifndef GUPPY_FF5M
  tmc_status_panel.init(j);

  // TODO: handle remote guppy instance
  State *s = State::get_instance();
  auto kp = s->get_data("/printer_info/klipper_path"_json_pointer);
  if (!kp.is_null()) {
    auto p = fs::path(kp.template get<std::string>()) / "klippy/extras/motor_database.cfg";
    if (fs::exists(p)) {
      tmc_tune_btn.enable();
      tmc_tune_panel.init(j, p);
    } else {
      tmc_tune_btn.disable();
    }
  }
#endif
}

void PrinterTunePanel::handle_callback(lv_event_t *event) {
  if (lv_event_get_code(event) == LV_EVENT_SHORT_CLICKED) {
    lv_obj_t *btn = lv_event_get_current_target(event);

    if (btn == finetune_btn.get_container()) {
      spdlog::trace("tune finetune pressed");
      finetune_panel.foreground();
    } else if (btn == bedmesh_btn.get_container()) {
      spdlog::trace("tune bedmesh pressed");
      bedmesh_panel.foreground();
    } else if (btn == inputshaper_btn.get_container()) {
      spdlog::trace("tune inputshaper pressed");
      inputshaper_panel.foreground();
    } else if (btn == belts_calibration_btn.get_container()) {
      spdlog::trace("tune belts pressed");
      belts_calibration_panel.foreground();
    } else if (btn == limits_btn.get_container()) {
      spdlog::trace("limits pressed");
      limits_panel.foreground();
    } else if (btn == retract_btn.get_container()) {
      spdlog::trace("retract pressed");
      retract_panel.foreground();
    } else if (btn == pro_btn.get_container()) {
      spdlog::trace("pro pressed");
      pro_panel.foreground();
    } else if (btn == pid_btn.get_container()) {
      spdlog::trace("pid pressed");
      pid_panel.foreground();
#ifndef GUPPY_FF5M
    } else if (btn == tmc_tune_btn.get_container()) {
      spdlog::trace("tmc auto tune pressed");
      tmc_tune_panel.foreground();
    } else if (btn == tmc_status_btn.get_container()) {
      spdlog::trace("tmc metrics pressed");
      tmc_status_panel.foreground();
    } else if (btn == power_devices_btn.get_container()) {
      spdlog::trace("power devices pressed");
      power_panel.foreground();
#endif
    }
  }
}
