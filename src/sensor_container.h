#ifndef __SENSOR_CONTAINER_H__
#define __SENSOR_CONTAINER_H__

#include "websocket_client.h"
#include "numpad.h"
#include "lvgl/lvgl.h"

#include <ctime>
#include <vector>

class SensorContainer {
 public:
  SensorContainer(KWebSocketClient &c,
                  lv_obj_t *parent,
                  const void *img,
                  const char *text,
                  lv_color_t color,
                  bool editable,
                  bool show_target,
                  Numpad &np,
                  std::string name,
                  lv_obj_t *chart,
                  lv_chart_series_t *chart_series,
                  bool not_set);

  SensorContainer(KWebSocketClient &c,
                  lv_obj_t *parent,
                  const void *img,
                  const char *text,
                  lv_color_t color,
                  bool editable,
                  bool show_target,
                  Numpad &np,
                  std::string name,
                  lv_obj_t *chart,
                  lv_chart_series_t *chart_series);

  SensorContainer(KWebSocketClient &c,
                  lv_obj_t *parent,
                  const void *img,
                  uint16_t img_scale,
                  const char *text,
                  lv_color_t color,
                  bool editable,
                  bool show_target,
                  Numpad &np,
                  std::string name,
                  lv_obj_t *chart,
                  lv_chart_series_t *chart_series);

  SensorContainer(KWebSocketClient &c,
                  lv_obj_t *parent,
                  const void *img,
                  uint16_t img_scale,
                  const char *text,
                  lv_color_t color,
                  bool editable,
                  bool show_target,
                  Numpad &np,
                  std::string name,
                  lv_obj_t *chart,
                  lv_chart_series_t *chart_series,
                  bool not_set);

  ~SensorContainer();

  lv_obj_t *get_sensor();
  void update_target(int new_target);
  void update_value(int new_value);
  void attach_series(lv_obj_t *chart, lv_chart_series_t *series);
  void update_series(int value);
  void clear_series();  // all points, the ones not yet added too
  int get_target_value();
  int get_value() { return value; }
  const std::string &get_id() const { return id; }
  static constexpr lv_coord_t stripe_width() { return 5; }
#ifdef GUPPY_FF5M
  // Home's layout: a tile with the icon and the name under it, the target
  // (red when set, grey when off) above the current temperature on the right.
  void as_tile();
  // The stripe's colour and the chart line's.
  void set_color(lv_color_t color);
  void set_chart_color(lv_color_t color);
  void set_name_colored(bool colored, const lv_color_t *configured = nullptr);
  static lv_coord_t tile_width();  // a tile's width on screen
  static constexpr lv_coord_t tile_gap() { return 6; }  // between tiles, Home and Extrude alike
  // График температур под плитками (Home, PID): вид плиток, слева вровень с
  // ними, диапазон 0–max_temp° (кратно 10, от 210 до 990), шкала справа через 50°;
  // вместе со шкалой шириной с четыре плитки.
  static void style_chart(lv_obj_t *chart, int32_t max_temp);
  // Новый диапазон 0–max_temp° графика после style_chart: шкала и линии сетки.
  static void set_chart_max(lv_obj_t *chart, int32_t max_temp);
  // Верх шкалы графика Home: настройка System (/chart_max_temp, один из
  // chart_max_options), без неё 330° у C5 (больше одного экструдера), иначе 300°.
  static int32_t chart_max_temp();
  // Варианты настройки по возрастанию (выпадающий список в System).
  static const std::vector<int32_t> &chart_max_options();

  // The tiles' temperature font, also the numpad's entered value: the two
  // always match.
  static const lv_font_t *temperature_font();
  void highlight(bool on);
#endif
  void handle_edit(lv_event_t *event);
  void handle_edit2(lv_event_t *event);

  static void _handle_edit(lv_event_t *event) {
    SensorContainer *panel = (SensorContainer*)event->user_data;
    panel->handle_edit(event);
  };

  static void _handle_edit2(lv_event_t *event) {
    SensorContainer *panel = (SensorContainer*)event->user_data;
    panel->handle_edit2(event);
  };

 private:
  KWebSocketClient &ws;
  lv_obj_t *sensor_cont;
  lv_obj_t *sensor_img;
  lv_obj_t *sensor_label;
  lv_obj_t *value_label;
  int value;
  lv_obj_t *divider_label;
  lv_obj_t *target_label;
  int target;
  Numpad &numpad;
  std::string id;
  lv_obj_t *chart;
  lv_chart_series_t *series;
  std::time_t last_updated_ts;
  std::vector<lv_coord_t> pending;  // points not yet added to the chart
  static void add_pending_points(lv_timer_t *);
#ifdef GUPPY_FF5M
  bool tile = false;
#endif

};

#endif // __SENSOR_CONTAINER_H__
