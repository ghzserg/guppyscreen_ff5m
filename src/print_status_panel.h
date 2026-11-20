#ifndef __PRINT_STATUS_PANEL_H__
#define __PRINT_STATUS_PANEL_H__

#include "websocket_client.h"
#include "notify_consumer.h"
#include "button_container.h"
#include "image_label.h"
#include "finetune_panel.h"
#include "retract_panel.h"
#include "exclude_panel.h"
#include "pid_panel.h"
#include "mini_print_status.h"
#include "lvgl/lvgl.h"

#include <mutex>
#include <ctime>
#include <map>

class MainPanel;

class PrintStatusPanel : public NotifyConsumer {
 public:
  /* PrintStatusPanel(KWebSocketClient &ws, std::mutex &lock, json &j); */
  PrintStatusPanel(KWebSocketClient &ws, std::mutex &lock, lv_obj_t *mini_parent, MainPanel &main_p);
  ~PrintStatusPanel();

  void init(json &fans);
  void reset();
  void populate();
  void foreground();
  void background();
  void show_status();
  // The Print tab's screen when nothing is printing (the file list).
  void set_idle_screen(lv_obj_t *screen);

  void handle_callback(lv_event_t *event);

  static void _handle_callback(lv_event_t *event) {
    PrintStatusPanel *panel = (PrintStatusPanel*)event->user_data;
    panel->handle_callback(event);
  };

  void consume(json &j);
  void update_time_progress(uint32_t time_passed);
  // Оставшееся время в полосе прогресса FF5M или рядом с песочными часами.
  void show_time_left(const std::string &eta);
  void update_filament_progress(float value);
  void update_flow_rate(double filament_used);
  void update_layers(json &info);
  int max_layer(json &info);
  int current_layer(json &info);

  FineTunePanel &get_finetune_panel();
  RetractPanel &get_retract_panel();
  ExcludePanel &get_exclude_panel();
  PidPanel &get_pid_panel();
  MiniPrintStatus &get_mini_print_status() { return mini_print_status; }

 private:
#ifdef GUPPY_FF5M
  void update_acceleration(const json &value);
#endif
  void handle_metadata(const std::string &gcode_file, uint32_t generation, json &j);
  void update_pause_buttons(const json &state);
  void update_bed_temperature();
  KWebSocketClient &ws;
  MainPanel &main_panel_ref;
  FineTunePanel finetune_panel;
  RetractPanel retract_panel;
  ExcludePanel exclude_panel;
  PidPanel pid_panel;
  MiniPrintStatus mini_print_status;
  lv_obj_t *status_cont;
  lv_obj_t *idle_screen = NULL;
  bool running = false;  // print_stats.state is printing or paused
  lv_obj_t *buttons_cont;
#ifdef GUPPY_FF5M
  ButtonContainer limits_btn;
#endif
  ButtonContainer finetune_btn;
  ButtonContainer retract_btn;
  ButtonContainer pause_btn;
  ButtonContainer resume_btn;
  ButtonContainer cancel_btn;
  lv_obj_t *thumbnail_cont;
  lv_obj_t *thumbnail;
  lv_obj_t *pbar_cont;
  lv_obj_t *progress_bar;
  lv_obj_t *progress_label;
  lv_obj_t *progress_end;
#ifdef GUPPY_FF5M
  lv_obj_t *progress_eta;  // at the bar's left: "ETA: 23м 18с"
  lv_obj_t *clock_label;
  lv_timer_t *clock_timer;
#endif
  lv_obj_t *filename_label;
  lv_obj_t *detail_cont;

  ImageLabel extruder_temp;
  ImageLabel bed_temp;
  ImageLabel print_speed;
  ImageLabel z_offset;
  ImageLabel flow_rate;
  ImageLabel layers;
  ImageLabel fan0;
#ifdef GUPPY_FF5M
  ImageLabel print_acceleration;
#endif
  ImageLabel elapsed;
#ifndef GUPPY_FF5M
  ImageLabel time_left;
#endif
  ImageLabel filament;
  /* ImageLabel fan2; */

  /* json &metadata; */
  float filament_m;
  uint32_t estimated_time_s;

  // flow rate
  std::time_t flow_ts;
  double last_filament_used;
  double filament_diameter;
  double flow;
  int extruder_target;
  json current_file;
  bool file_loaded = false;
  uint32_t metadata_generation = 0;

  std::map<std::string, int> fan_speeds;
};

#endif // __PRINT_STATUS_PANEL_H__
