#ifndef __PRINTERTUNE_PANEL_H__
#define __PRINTERTUNE_PANEL_H__

#include "finetune_panel.h"
#include "limits_panel.h"
#include "retract_panel.h"
#include "pro_panel.h"
#include "pid_panel.h"
#include "bedmesh_panel.h"
#include "inputshaper_panel.h"
#include "belts_calibration_panel.h"
#include "prompt_panel.h"
#ifndef GUPPY_FF5M
#include "tmc_tune_panel.h"
#include "tmc_status_panel.h"
#include "power_panel.h"
#endif
#include "button_container.h"
#include "lvgl/lvgl.h"

#include <mutex>
class PrinterTunePanel {
 public:
  PrinterTunePanel(KWebSocketClient &c, std::mutex &l, lv_obj_t *parent, FineTunePanel &, RetractPanel &, ProPanel &, PidPanel &, PromptPanel &);
  ~PrinterTunePanel();

  lv_obj_t *get_container();
  BedMeshPanel &get_bedmesh_panel();
#ifndef GUPPY_FF5M
  PowerPanel &get_power_panel();
#endif
  void init(json &j);
  void handle_callback(lv_event_t *event);

  static void _handle_callback(lv_event_t *event) {
    PrinterTunePanel *panel = (PrinterTunePanel*)event->user_data;
    panel->handle_callback(event);
  };

 private:
  lv_obj_t *cont;
  BedMeshPanel bedmesh_panel;
  FineTunePanel &finetune_panel;
  LimitsPanel limits_panel;
  RetractPanel retract_panel;
  ProPanel pro_panel;
  PidPanel pid_panel;
  InputShaperPanel inputshaper_panel;
  BeltsCalibrationPanel belts_calibration_panel;
#ifndef GUPPY_FF5M
  TmcTunePanel tmc_tune_panel;
  TmcStatusPanel tmc_status_panel;
  PowerPanel power_panel;
#endif
  ButtonContainer finetune_btn;
  ButtonContainer limits_btn;
  ButtonContainer retract_btn;
  ButtonContainer bedmesh_btn;
  ButtonContainer inputshaper_btn;
  ButtonContainer belts_calibration_btn;
  ButtonContainer pro_btn;
  ButtonContainer pid_btn;
#ifndef GUPPY_FF5M
  ButtonContainer tmc_tune_btn;
  ButtonContainer tmc_status_btn;
  ButtonContainer power_devices_btn;
#endif

};

#endif // __PRINTERTUNE_PANEL_H__
