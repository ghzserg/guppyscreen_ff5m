#include "main_panel.h"
#include "state.h"
#include "config.h"
#include "lvgl/lvgl.h"
#include "spdlog/spdlog.h"

#include <algorithm>
#include <strings.h>
#include <string>
#include <map>
#include <optional>
#include <vector>

#ifdef GUPPY_FF5M
LV_IMG_DECLARE(extrude_img);  // as on the extruder screen's Load button
#define EXTRUDE_IMG extrude_img
#else
LV_IMG_DECLARE(filament_img);
#define EXTRUDE_IMG filament_img
#endif
LV_IMG_DECLARE(light_img);
LV_IMG_DECLARE(move);
LV_IMG_DECLARE(extruder);
LV_IMG_DECLARE(bed);
LV_IMG_DECLARE(fan);
LV_IMG_DECLARE(heater);
#ifdef GUPPY_FF5M
LV_IMG_DECLARE(speed_up_img);  // Fine Tune's Speed+; unlike the Tune tab's sliders
LV_IMG_DECLARE(bedmesh_img);
LV_IMG_DECLARE(thermometer_img);
#endif

#define TUNE_SYMBOL "\xF3\xB1\x95\x82"
#define HOME_SYMBOL "\xF3\xB0\x8B\x9C"
#define SETTING_SYMBOL "\xF3\xB0\x92\x93"


prompt_data *pdata;

MainPanel::MainPanel(KWebSocketClient &websocket,
                     std::mutex &lock
                     ,SpoolmanPanel &sm
)
  : NotifyConsumer(lock)
  , ws(websocket)
  , homing_panel(ws, lock)
  , fan_panel(ws, lock)
  , led_panel(ws, lock)
  , main_tab(Nav::get().add_tab(Nav::HOME, HOME_SYMBOL))
  , printertune_tab(Nav::get().add_tab(Nav::TUNE, TUNE_SYMBOL))
  , setting_tab(Nav::get().add_tab(Nav::SETTINGS, SETTING_SYMBOL))
  , macros_tab(Nav::get().add_screen())
  , macros_panel(ws, lock, macros_tab)
  , console_tab(Nav::get().add_screen())
  , console_panel(ws, lock, console_tab)
  , setting_panel(websocket, lock, setting_tab, [this]() { open_macros(); }, [this]() { open_console(); }
#ifdef GUPPY_FF5M
                  , [this](bool enabled) { print_status_panel.set_clock_enabled(enabled); }
                  , [this]() { refresh_filament_colors(); }
                  , [this]() { SensorContainer::set_chart_max(temp_chart, SensorContainer::chart_max_temp()); }
#endif
                  )
  , main_cont(lv_obj_create(main_tab))
  , print_status_panel(websocket, lock, main_cont, *this)
  , print_panel(ws, lock, print_status_panel)
  , numpad(Numpad(main_cont))
  , extruder_panel(ws, lock, numpad, sm)
  , prompt_panel(websocket, lock, main_cont, extruder_panel)
  , printertune_panel(ws, lock, printertune_tab, print_status_panel.get_finetune_panel(), print_status_panel.get_retract_panel(), print_status_panel.get_pid_panel(), prompt_panel)
  , spoolman_panel(sm)
  , temp_cont(lv_obj_create(main_cont))
#ifdef GUPPY_FF5M
  , sensor_tiles(lv_obj_create(temp_cont))
#endif
  , temp_chart(lv_chart_create(main_cont))
  , homing_btn(main_cont, &move, _("Homing") /* "Домой" */, &MainPanel::_handle_homing_cb, this)
  , extrude_btn(main_cont, &EXTRUDE_IMG, _("Extrude") /* "Экструдер" */, &MainPanel::_handle_extrude_cb, this)
  , action_btn(main_cont, &fan, _("Fans") /* "Обдув" */, &MainPanel::_handle_fanpanel_cb, this)
  , led_btn(main_cont, &light_img, "LED", &MainPanel::_handle_ledpanel_cb, this)
#ifdef GUPPY_FF5M
  , finetune_btn(main_cont, &speed_up_img, _("Fine Tune") /* "Настройки" */, &MainPanel::_handle_finetune_cb, this)
  , bedmesh_btn(main_cont, &bedmesh_img, _("Bed Mesh") /* "Стол" */, &MainPanel::_handle_bedmesh_cb, this)
#endif
{
    lv_style_init(&style);
    lv_style_set_img_recolor_opa(&style, LV_OPA_30);
    lv_style_set_img_recolor(&style, lv_color_black());
    lv_style_set_border_width(&style, 0);
    lv_style_set_bg_color(&style, lv_palette_darken(LV_PALETTE_GREY, 4));

    ws.register_notify_update(this);

    Nav::get().set_stop([this]() { ws.send_jsonrpc("printer.emergency_stop"); });
    Nav::get().on_leave(macros_tab, [this]() { macros_panel.background(); });
}

MainPanel::~MainPanel() {
  sensors.clear();
}

void MainPanel::foreground() {
  Nav::get().activate(Nav::HOME);
}

// Macros and Console: screens opened from Settings, left through the bar.
void MainPanel::open_macros() {
  lv_obj_move_foreground(macros_tab);
  macros_panel.foreground();  // builds the list the first time
}

void MainPanel::open_console() {
  lv_obj_move_foreground(console_tab);
}

void MainPanel::subscribe() {
  spdlog::trace("main panel subscribing");
  ws.send_jsonrpc("printer.gcode.help", [this](json &d) { console_panel.handle_macros(d); });
  print_panel.subscribe();
}

PrinterTunePanel& MainPanel::get_tune_panel() {
  return printertune_panel;
}

void MainPanel::init(json &j) {
  std::lock_guard<std::mutex> lock(lv_lock);
#ifdef GUPPY_FF5M
  // Экструдеры уже известны: верх шкалы по умолчанию зависит от их числа (C5).
  SensorContainer::set_chart_max(temp_chart, SensorContainer::chart_max_temp());
#endif
  homing_panel.update_motion_status();
  for (const auto &el : sensors) {
    auto target_value = j[json::json_pointer(fmt::format("/result/status/{}/target", el.first))];
    if (!target_value.is_null()) {
      int target = target_value.template get<int>();
      el.second->update_target(target);
    }

    auto temp_value = j[json::json_pointer(fmt::format("/result/status/{}/temperature", el.first))];
    if (!temp_value.is_null()) {
      int value = temp_value.template get<int>();
      el.second->update_series(value);
      el.second->update_value(value);
    }
  }

#ifdef GUPPY_FF5M
  if (j.contains("/result/status/zmod_color/slots"_json_pointer))
    update_filament_colors(j.at("/result/status/zmod_color/slots"_json_pointer));
#endif

  macros_panel.refresh();

  auto fans = State::get_instance()->get_display_fans();
  print_status_panel.init(fans);
  printertune_panel.init(j);
#ifdef GUPPY_FF5M
  // Циркуляция воздуха там, где у Klipper есть вентиляторы, которыми управляют
  // макросы AIR_CIRCULATION_* Z-Mod (на AD5X они пустые): внутренний и внешний
  // вентиляторы и заслонка AD5M Pro, вентиляторы камеры C5 Pro. Тогда её
  // кнопки на экране вентиляторов.
  json objs = State::get_instance()->get_data("/printer_objs/objects"_json_pointer);
  auto has = [&objs](std::initializer_list<const char *> names) {
    for (const char *n : names) {
      // Без учёта регистра: Klipper отдаёт имя макроса так, как оно записано в конфиге.
      auto same = [n](const json &o) { return o.is_string() && strcasecmp(o.get_ref<const std::string &>().c_str(), n) == 0; };
      if (!objs.is_array() || std::none_of(objs.begin(), objs.end(), same)) return false;
    }
    return true;
  };
  bool air = has({"servo my_servo", "fan_generic internal", "fan_generic external"})
      || has({"fan_generic chamber_heat_fan", "fan_generic chamber_loop_fan", "fan_generic chamber_cool_fan"});
  fan_panel.show_air_circulation(air);
  bool has_weight = has({PidPanel::WEIGHT});
  print_status_panel.get_pid_panel().show_weight_controls(has_weight);
  printertune_panel.get_bedmesh_panel().show_screw_nozzle_temp(has_weight);
  printertune_panel.show_calibrations(has({"gcode_macro CALIBRATE_EXTRUDERS"}),
                                      has({"gcode_macro CALIBRATE_VFA"}));
#endif
//  print_status_panel.get_exclude_panel().init(j);
  print_status_panel.get_retract_panel().init(j);
  set_print_locked(j["/result/status/print_stats/state"_json_pointer]);
}

void MainPanel::set_print_locked(const json &state) {
  const bool printing = state == "printing";
  const bool busy = printing || state == "paused";
  homing_panel.set_print_locked(printing, busy);
  printertune_panel.set_print_locked(busy);
  setting_panel.set_print_locked(busy);
  extruder_panel.set_print_locked(printing);
}

void MainPanel::consume(json &j) {
  std::lock_guard<std::mutex> lock(lv_lock);
  static const auto state_path = "/params/0/print_stats/state"_json_pointer;
  if (j.contains(state_path) && j.at(state_path).is_string())
    set_print_locked(j.at(state_path));
  for (const auto &el : sensors) {
    auto target_value = j[json::json_pointer(fmt::format("/params/0/{}/target", el.first))];
    if (!target_value.is_null()) {
      int target = target_value.template get<int>();
      el.second->update_target(target);
    }

    auto temp_value = j[json::json_pointer(fmt::format("/params/0/{}/temperature", el.first))];
    if (!temp_value.is_null()) {
      int value = temp_value.template get<int>();
      el.second->update_series(value);
      el.second->update_value(value);
    }
  }
#ifdef GUPPY_FF5M
  if (j.contains("/params/0/zmod_color/slots"_json_pointer))
    update_filament_colors(j.at("/params/0/zmod_color/slots"_json_pointer));
#endif
}

void MainPanel::create_panel() {
  create_main(main_tab);
}

void MainPanel::handle_homing_cb(lv_event_t *event) {
  spdlog::trace("clicked homing1");
  if (lv_event_get_code(event) == LV_EVENT_SHORT_CLICKED) {
    spdlog::trace("clicked homing");
    homing_panel.foreground();
  }
}

void MainPanel::handle_extrude_cb(lv_event_t *event) {
  if (lv_event_get_code(event) == LV_EVENT_SHORT_CLICKED) {
    spdlog::trace("clicked extruder");
    extruder_panel.foreground();
  }
}

void MainPanel::handle_fanpanel_cb(lv_event_t *event) {
  if (lv_event_get_code(event) == LV_EVENT_SHORT_CLICKED) {
    spdlog::trace("clicked fan panel");
    fan_panel.foreground();
  }
}

void MainPanel::handle_ledpanel_cb(lv_event_t *event) {
  if (lv_event_get_code(event) == LV_EVENT_SHORT_CLICKED) {
    spdlog::trace("clicked led panel");
    led_panel.foreground();
  }
}

#ifdef GUPPY_FF5M
void MainPanel::handle_finetune_cb(lv_event_t *event) {
  if (lv_event_get_code(event) == LV_EVENT_SHORT_CLICKED) {
    spdlog::trace("clicked fine tune");
    print_status_panel.get_finetune_panel().foreground();
  }
}

void MainPanel::handle_bedmesh_cb(lv_event_t *event) {
  if (lv_event_get_code(event) == LV_EVENT_SHORT_CLICKED) {
    spdlog::trace("clicked bed mesh");
    printertune_panel.get_bedmesh_panel().foreground();
  }
}
#endif

void MainPanel::create_main(lv_obj_t * parent)
{
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_ROW_WRAP);

    static lv_coord_t grid_main_row_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static lv_coord_t grid_main_col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
      LV_GRID_TEMPLATE_LAST};

    lv_obj_clear_flag(main_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_height(main_cont, LV_PCT(100));

    lv_obj_set_flex_grow(main_cont, 1);
    lv_obj_set_grid_dsc_array(main_cont, grid_main_col_dsc, grid_main_row_dsc);
#ifdef GUPPY_FF5M
    lv_obj_set_style_pad_top(main_cont, 0, 0);
    lv_obj_set_style_pad_bottom(main_cont, 0, 0);
#endif

    lv_obj_set_grid_cell(homing_btn.get_container(),    LV_GRID_ALIGN_CENTER, 2, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    lv_obj_set_grid_cell(extrude_btn.get_container(),   LV_GRID_ALIGN_CENTER, 3, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    lv_obj_set_grid_cell(action_btn.get_container(),    LV_GRID_ALIGN_CENTER, 2, 1, LV_GRID_ALIGN_CENTER, 1, 1);
    lv_obj_set_grid_cell(led_btn.get_container(),       LV_GRID_ALIGN_CENTER, 3, 1, LV_GRID_ALIGN_CENTER, 1, 1);
#ifdef GUPPY_FF5M
    lv_obj_set_grid_cell(finetune_btn.get_container(),  LV_GRID_ALIGN_CENTER, 2, 1, LV_GRID_ALIGN_CENTER, 2, 1);
    lv_obj_set_grid_cell(bedmesh_btn.get_container(),   LV_GRID_ALIGN_CENTER, 3, 1, LV_GRID_ALIGN_CENTER, 2, 1);
#endif

#ifdef GUPPY_FF5M
    // The sensors as tiles, 4 per row, and the print status on its own line
    // under them: everything above the chart without scrolling, even with
    // C5 Pro's six heaters. show() keeps the status the column's last child.
    lv_obj_clear_flag(temp_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(temp_cont, LV_PCT(50), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(temp_cont, 0, 0);
    const lv_coord_t TILE_GAP = SensorContainer::tile_gap();
    lv_obj_set_style_pad_top(temp_cont, TILE_GAP, 0);
    lv_obj_set_style_pad_row(temp_cont, TILE_GAP, 0);
    lv_obj_set_flex_flow(temp_cont, LV_FLEX_FLOW_COLUMN);

    lv_obj_remove_style_all(sensor_tiles);
    lv_obj_clear_flag(sensor_tiles, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(sensor_tiles, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_row(sensor_tiles, TILE_GAP, 0);
    lv_obj_set_style_pad_column(sensor_tiles, TILE_GAP, 0);
    lv_obj_set_flex_flow(sensor_tiles, LV_FLEX_FLOW_ROW_WRAP);

    lv_obj_set_grid_cell(temp_cont, LV_GRID_ALIGN_START, 0, 2, LV_GRID_ALIGN_START, 0, 2);

    lv_obj_t *mini_status = print_status_panel.get_mini_print_status().get_container();
    lv_obj_set_parent(mini_status, temp_cont);
    lv_obj_clear_flag(mini_status, LV_OBJ_FLAG_FLOATING);
    // Four tile columns wide, whatever its text.
    lv_obj_set_width(mini_status, 4 * SensorContainer::tile_width() + 3 * TILE_GAP);
    // Вровень с вкладкой Print, если плитки оставляют над ней место (один ряд,
    // AD5M), иначе сразу под плитками: ряд плиток тянется вниз хотя бы до неё.
    // temp_cont стоит в верхней ячейке сетки main_cont.
    lv_obj_set_style_min_height(sensor_tiles, Nav::slot_top(Nav::PRINT) - lv_obj_get_style_pad_top(main_cont, 0) -
                                lv_obj_get_style_pad_top(temp_cont, 0) - TILE_GAP, 0);
#else
    // Разрешаем прокрутку контейнера
    lv_obj_add_flag(temp_cont, LV_OBJ_FLAG_SCROLLABLE);
    // Разрешаем скроллинг только по вертикали
    lv_obj_set_scroll_dir(temp_cont, LV_DIR_VER);
    // Отключаем передачу прокрутки наверх, чтобы скроллился именно список
    lv_obj_clear_flag(temp_cont, LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_set_size(temp_cont, LV_PCT(50), LV_PCT(50));

    lv_obj_set_style_pad_all(temp_cont, 0, 0);

    // Выстраиваем датчики строго в вертикальную колонку
    lv_obj_set_flex_flow(temp_cont, LV_FLEX_FLOW_COLUMN);

    lv_obj_set_grid_cell(temp_cont, LV_GRID_ALIGN_START, 0, 2, LV_GRID_ALIGN_START, 0, 2);
#endif

    lv_obj_align(temp_chart, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_size(temp_chart, LV_PCT(45), LV_PCT(40));
#ifdef GUPPY_FF5M
    lv_obj_set_style_pad_top(temp_chart, 0, 0);
    lv_obj_set_style_pad_bottom(temp_chart, 4, 0);
#endif
    lv_obj_set_style_size(temp_chart, 0, LV_PART_INDICATOR);

    lv_chart_set_range(temp_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 300);
    lv_obj_set_grid_cell(temp_chart, LV_GRID_ALIGN_END, 0, 2, LV_GRID_ALIGN_END, 2, 1);
    lv_chart_set_axis_tick(temp_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 0, 6, 5, true, 50);

#ifdef GUPPY_FF5M
    SensorContainer::style_chart(temp_chart, SensorContainer::chart_max_temp());
#else
    lv_chart_set_div_line_count(temp_chart, 3, 8);
#endif
    lv_chart_set_point_count(temp_chart, 500);
    lv_chart_set_zoom_x(temp_chart, 5000);
    lv_obj_scroll_to_x(temp_chart, LV_COORD_MAX, LV_ANIM_OFF);
}

#ifdef GUPPY_FF5M
// The colour of each sensor's stripe and chart line, by sensor id: the one
// named in its config, unless that name is unknown or an earlier sensor
// already has it. Those get the first colour of CONTRASTING no sensor has,
// so no two lines on the chart look alike. `others` (sensors other screens
// show though Home may not, with their usual colour) get a colour too: their
// own if Home shows them, else the usual one or, if a Home sensor has it, the
// first of CONTRASTING none has.
struct OtherSensor { const char *id; lv_palette_t usual; };
static std::map<std::string, lv_color_t> sensor_colors(json &temp_sensors,
                                                       std::initializer_list<OtherSensor> others) {
  // In .rodata: looked up once per sensor at startup.
  static const struct { const char *name; lv_palette_t palette; } NAMED[] = {
    {"red", LV_PALETTE_RED},       {"purple", LV_PALETTE_PURPLE}, {"blue", LV_PALETTE_BLUE},
    {"orange", LV_PALETTE_ORANGE}, {"yellow", LV_PALETTE_YELLOW}, {"green", LV_PALETTE_GREEN},
    {"cyan", LV_PALETTE_CYAN},     {"teal", LV_PALETTE_TEAL},     {"lime", LV_PALETTE_LIME},
    {"amber", LV_PALETTE_AMBER},   {"pink", LV_PALETTE_PINK},     {"grey", LV_PALETTE_GREY},
    {"gray", LV_PALETTE_GREY},     {"indigo", LV_PALETTE_INDIGO}, {"deep_purple", LV_PALETTE_DEEP_PURPLE},
    {"light_blue", LV_PALETTE_LIGHT_BLUE}, {"brown", LV_PALETTE_BROWN},
  };
  static const lv_color_t CONTRASTING[] = {
    lv_palette_main(LV_PALETTE_RED),    lv_palette_main(LV_PALETTE_BLUE),   lv_palette_main(LV_PALETTE_GREEN),
    lv_palette_main(LV_PALETTE_YELLOW), lv_palette_main(LV_PALETTE_PURPLE), lv_palette_main(LV_PALETTE_CYAN),
    lv_palette_main(LV_PALETTE_ORANGE), lv_palette_main(LV_PALETTE_PINK),   lv_palette_main(LV_PALETTE_LIME),
    lv_color_white(),
  };
  // The colour a config value names, if any.
  auto parse = [](const json &color) -> std::optional<lv_color_t> {
    if (color.is_number()) {
      int p = color.template get<int>();
      if (p >= 0 && p < _LV_PALETTE_LAST) return lv_palette_main((lv_palette_t)p);
    } else if (color.is_string()) {
      const std::string name = color.template get<std::string>();
      if (name == "white") return lv_color_white();
      for (auto &n : NAMED) if (name == n.name) return lv_palette_main(n.palette);
    }
    return std::nullopt;
  };
  std::map<std::string, lv_color_t> colors;
  auto taken = [&](lv_color_t c) {
    for (auto &[k, v] : colors) if (lv_color_to32(v) == lv_color_to32(c)) return true;
    return false;
  };

  std::vector<std::string> unset;
  for (auto &sensor : temp_sensors.items()) {
    auto c = parse(sensor.value()["color"]);
    if (c && !taken(*c)) {
      colors[sensor.key()] = *c;
    } else {
      unset.push_back(sensor.key());
    }
  }
  auto first_free = [&]() {
    for (lv_color_t c : CONTRASTING) {
      if (!taken(c)) return c;
    }
    return lv_palette_main(LV_PALETTE_ORANGE);  // more sensors than CONTRASTING colours
  };
  for (auto &key : unset) {
    colors[key] = first_free();
  }
  // After Home's: they never take a colour a Home sensor could have.
  for (const OtherSensor &o : others) {
    if (temp_sensors.contains(o.id)) continue;
    colors[o.id] = taken(lv_palette_main(o.usual)) ? first_free() : lv_palette_main(o.usual);
  }
  return colors;
}
#endif

#ifdef GUPPY_FF5M
void MainPanel::refresh_filament_colors() {
  update_filament_colors(State::get_instance()->get_data_copy("/printer_state/zmod_color/slots"_json_pointer));
}

void MainPanel::update_filament_colors(const json &slots) {
  if (default_tool_colors.size() < 2) return;
  const bool enabled = Config::get_instance()->get<bool>("/use_filament_colors");
  for (const auto &[id, original] : default_tool_colors) {
    lv_color_t color = original;
    bool stripe = true;
    if (enabled && slots.is_array()) {
      const int tool = NozzleTiles::tool_of(id);
      for (const auto &slot : slots) {
        if (!slot.is_object()) continue;
        auto index = slot.find("ID");
        if (index == slot.end()) continue;
        int number = -1;
        if (index->is_number_integer()) number = index->get<int>();
        else if (index->is_string()) {
          const std::string value = index->get<std::string>();
          if (value.size() == 1 && value[0] >= '1' && value[0] <= '9') number = value[0] - '0';
        }
        if (number != tool + 1) continue;
        auto present = slot.find("hasFilament");
        if (present != slot.end() && present->is_boolean() && !present->get<bool>()) {
          color = lv_palette_main(LV_PALETTE_GREY);
          stripe = false;
        } else {
          auto value = slot.find("HEX");
          if (value == slot.end()) value = slot.find("Color");
          if (value != slot.end() && value->is_string()) {
            std::string hex = value->get<std::string>();
            if (hex.size() == 7 && hex[0] == '#') hex.erase(0, 1);
            if (hex.size() == 6 && hex.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos)
              color = lv_color_hex(std::stoul(hex, nullptr, 16));
          }
        }
        break;
      }
    }
    auto sensor = sensors.find(id);
    if (sensor == sensors.end()) continue;
    lv_obj_t *tile = sensor->second->get_sensor();
    if (lv_color_to32(lv_obj_get_style_border_color(tile, LV_PART_MAIN)) != lv_color_to32(color))
      lv_obj_set_style_border_color(tile, color, LV_PART_MAIN);
    const lv_opa_t opacity = stripe ? LV_OPA_COVER : LV_OPA_TRANSP;
    if (lv_obj_get_style_border_opa(tile, LV_PART_MAIN) != opacity)
      lv_obj_set_style_border_opa(tile, opacity, LV_PART_MAIN);
  }
}
#endif

void MainPanel::create_sensors(json &temp_sensors) {
  std::lock_guard<std::mutex> lock(lv_lock);
  sensors.clear();
#ifdef GUPPY_FF5M
  // PID shows the bed and the weight even when Home doesn't.
  std::map<std::string, lv_color_t> colors = sensor_colors(
      temp_sensors, {{PidPanel::BED, LV_PALETTE_PURPLE}, {PidPanel::WEIGHT, LV_PALETTE_ORANGE}});
  std::vector<NozzleTiles::Nozzle> nozzles;
  default_tool_colors.clear();
  for (auto &sensor : temp_sensors.items()) {
    int tool = NozzleTiles::tool_of(sensor.key());
    if (tool >= 0) {
      const std::string name = sensor.value()["display_name"].template get<std::string>();
      nozzles.push_back({sensor.key(), _(name.c_str()), colors[sensor.key()],
                         tool});
      default_tool_colors[sensor.key()] = colors[sensor.key()];
    }
  }
  // Home's content area, the extruder screen's and PID's all start at the
  // content area's top left corner; the temperatures are Home's top left cell.
  lv_coord_t left = lv_obj_get_style_pad_left(main_cont, 0) + lv_obj_get_style_pad_left(temp_cont, 0);
  lv_coord_t top = lv_obj_get_style_pad_top(main_cont, 0) + lv_obj_get_style_pad_top(temp_cont, 0);
  print_status_panel.get_pid_panel().create_nozzle_tiles(nozzles, left, top);
  print_status_panel.get_pid_panel().set_colors(colors[PidPanel::BED], colors[PidPanel::WEIGHT]);
  extruder_panel.create_nozzle_tiles(nozzles, left, top);
#endif
  for (auto &sensor : temp_sensors.items()) {
    std::string key = sensor.key();
    bool controllable = sensor.value()["controllable"].template get<bool>();

#ifdef GUPPY_FF5M
    lv_color_t color_code = colors[key];
#else
    lv_color_t color_code = lv_palette_main(LV_PALETTE_ORANGE);
    if (!sensor.value()["color"].is_number()) {
      std::string color = sensor.value()["color"].template get<std::string>();
      if (color == "red") {
        color_code = lv_palette_main(LV_PALETTE_RED);
      } else if (color == "purple") {
        color_code = lv_palette_main(LV_PALETTE_PURPLE);
      } else if (color == "blue") {
        color_code = lv_palette_main(LV_PALETTE_BLUE);
      } else if (color == "orange") {
        color_code = lv_palette_main(LV_PALETTE_ORANGE);
      } else if (color == "yellow") {
        color_code = lv_palette_main(LV_PALETTE_YELLOW);
      } else if (color == "green") {
        color_code = lv_palette_main(LV_PALETTE_GREEN);
      } else if (color == "cyan") {
        color_code = lv_palette_main(LV_PALETTE_CYAN);
      } else if (color == "teal") {
        color_code = lv_palette_main(LV_PALETTE_TEAL);
      } else if (color == "lime") {
        color_code = lv_palette_main(LV_PALETTE_LIME);
      } else if (color == "amber") {
        color_code = lv_palette_main(LV_PALETTE_AMBER);
      } else if (color == "pink") {
        color_code = lv_palette_main(LV_PALETTE_PINK);
      } else if (color == "grey" || color == "gray") {
        color_code = lv_palette_main(LV_PALETTE_GREY);
      }
    } else {
      color_code = lv_palette_main((lv_palette_t)sensor.value()["color"].template get<int>());
    }

#endif

    std::string display_name = sensor.value()["display_name"].template get<std::string>();

    const void* sensor_img = &heater;
    if (key.rfind("extruder", 0) == 0) {
      sensor_img = &extruder;
    } else if (key == "heater_bed") {
      sensor_img = &bed;
    }
#ifdef GUPPY_FF5M
    else if (key == "temperature_sensor head") {
      sensor_img = &thermometer_img;
    }
#endif

    lv_chart_series_t *temp_series =
      lv_chart_add_series(temp_chart, color_code, LV_CHART_AXIS_PRIMARY_Y);

#ifdef GUPPY_FF5M
    lv_obj_t *sensor_parent = sensor_tiles;
#else
    lv_obj_t *sensor_parent = temp_cont;
#endif
    sensors.insert({key, std::make_shared<SensorContainer>(ws, sensor_parent, sensor_img, 150,
                           _(display_name.c_str()), color_code, controllable, false, numpad, key,
                           temp_chart, temp_series)});
#ifdef GUPPY_FF5M
    sensors[key]->as_tile(default_tool_colors.size() > 1);
#endif
  }
}

void MainPanel::create_fans(json &fans) {
  fan_panel.create_fans(fans);
}

void MainPanel::create_leds(json &leds) {
  led_panel.init(leds);
}

void MainPanel::enable_spoolman() {
  spoolman_panel.init();
//  setting_panel.enable_spoolman();
  extruder_panel.enable_spoolman();
}
