#ifndef __PID_PANEL_H__
#define __PID_PANEL_H__

#include "sensor_container.h"
#include "numpad.h"
#include "button_container.h"
#include "lvgl/lvgl.h"
#include "websocket_client.h"
#include "notify_consumer.h"
#ifdef GUPPY_FF5M
#include "nozzle_tiles.h"
#endif

#include <mutex>

class PidPanel : public NotifyConsumer {
 public:
  PidPanel(KWebSocketClient &c, std::mutex &l);
  ~PidPanel();

  void foreground();
  void handle_callback(lv_event_t *event);
  void consume(json &j);
  static void _handle_callback(lv_event_t *event) {
    PidPanel *panel = (PidPanel*)event->user_data;
    panel->handle_callback(event);
  };

  // The ids of the bed's and the weight's sensors.
  static constexpr const char *BED = "heater_bed";
  static constexpr const char *WEIGHT = "temperature_sensor weightValue";

#ifdef GUPPY_FF5M
  // Вес, очистка сопла и сброс веса доступны при наличии тензодатчика Klipper.
  void show_weight_controls(bool show);

  // The nozzles' tiles instead of the extruder's and the T0-T3 buttons: a
  // tap selects the nozzle to calibrate (with several) and asks for the
  // calibration temperature. Called by MainPanel::create_sensors, under the
  // LVGL lock, with Home's names and colours and where Home's tiles start in
  // its content area (`left`, `top`).
  void create_nozzle_tiles(const std::vector<NozzleTiles::Nozzle> &nozzles, lv_coord_t left, lv_coord_t top);

  // Home's colours for the bed and the weight (tile and chart line). Called by
  // MainPanel::create_sensors, under the LVGL lock.
  void set_colors(lv_color_t bed, lv_color_t weight);
#endif

 private:
  void create_chart();

  KWebSocketClient &ws;
  lv_obj_t *cont;
  Numpad numpad;
  int active_tool_id = 0;
  int total_tools = 1;

  lv_obj_t *temp_cont;
  lv_obj_t *temp_chart = nullptr;
  lv_obj_t *tools_btnmatrix;
  SensorContainer extruder_temp;
  SensorContainer heater_bed_temp;
  SensorContainer weight_temp;
  ButtonContainer pid_extruder_btn;
  ButtonContainer pid_bed_btn;
  ButtonContainer clear_nozzle_btn;
  ButtonContainer load_cell_tare_btn;
  ButtonContainer bed_level_screws_tune_btn;
#ifdef GUPPY_FF5M
  // Hidden extruder_temp still holds the calibration temperature and feeds
  // the chart with the selected nozzle's temperature.
  NozzleTiles nozzle_tiles;
  // Once a nozzle is tapped, the printer's tool no longer moves the selection.
  bool nozzle_picked = false;
  bool bed_selected = false;
  lv_chart_series_t *extruder_temp_series = nullptr;
  int charted_tool = -1;  // whose temperature and colour the chart's line has
  int chart_selected_nozzle();  // the selected tool
#endif
  // T= of PID_TUNE_EXTRUDER (`extruder` for 0, else `extruderT`) and
  // CLEAR_NOZZLE.
  int calibrated_tool() const;
};

#endif // __PID_PANEL_H__
