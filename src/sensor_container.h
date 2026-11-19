#ifndef __SENSOR_CONTAINER_H__
#define __SENSOR_CONTAINER_H__

#include "websocket_client.h"
#include "numpad.h"
#include "lvgl/lvgl.h"

#include <ctime>

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
  void update_series(int value);
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
  static lv_coord_t tile_width();  // a tile's width on screen
  static constexpr lv_coord_t tile_gap() { return 6; }  // between tiles, Home and Extrude alike

  // The tiles' temperature font, also the numpad's entered value: the two
  // always match.
  static const lv_font_t *temperature_font();
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
#ifdef GUPPY_FF5M
  bool tile = false;
  void highlight(bool on);  // синяя, пока открыт её numpad
#endif

};

#endif // __SENSOR_CONTAINER_H__
