#include "main_panel.h"
#include "state.h"
#include "lvgl/lvgl.h"
#include "spdlog/spdlog.h"

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
  , setting_panel(websocket, lock, setting_tab, [this]() { open_macros(); }, [this]() { open_console(); }/*, sm*/)
  , main_cont(lv_obj_create(main_tab))
  , print_status_panel(websocket, lock, main_cont, *this)
  , print_panel(ws, lock, print_status_panel)
  , extruder_panel(ws, lock, numpad, sm)
  , prompt_panel(websocket, lock, main_cont, extruder_panel)
  , printertune_panel(ws, lock, printertune_tab, print_status_panel.get_finetune_panel(), print_status_panel.get_retract_panel(), print_status_panel.get_pro_panel(), print_status_panel.get_pid_panel(), prompt_panel)
  , numpad(Numpad(main_cont))
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
  , main_layout(GridLayout::from_config("main",
      {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1)},
      {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1)}))
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

  macros_panel.refresh();

  auto fans = State::get_instance()->get_display_fans();
  print_status_panel.init(fans);
  printertune_panel.init(j);
//  print_status_panel.get_exclude_panel().init(j);
  print_status_panel.get_retract_panel().init(j);
}

void MainPanel::consume(json &j) {
  std::lock_guard<std::mutex> lock(lv_lock);
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

    lv_obj_clear_flag(main_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_height(main_cont, LV_PCT(100));

    lv_obj_set_flex_grow(main_cont, 1);
#ifdef GUPPY_FF5M
    lv_obj_set_style_pad_top(main_cont, 0, 0);
    lv_obj_set_style_pad_bottom(main_cont, 0, 0);
#endif
    main_layout.apply_tracks(main_cont);  // after the padding: the config's wins

    main_layout.place(homing_btn.get_container(),    "homing",  LV_GRID_ALIGN_CENTER, 2, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    main_layout.place(extrude_btn.get_container(),   "extrude", LV_GRID_ALIGN_CENTER, 3, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    main_layout.place(action_btn.get_container(),    "fans",    LV_GRID_ALIGN_CENTER, 2, 1, LV_GRID_ALIGN_CENTER, 1, 1);
    main_layout.place(led_btn.get_container(),       "led",     LV_GRID_ALIGN_CENTER, 3, 1, LV_GRID_ALIGN_CENTER, 1, 1);
#ifdef GUPPY_FF5M
    main_layout.place(finetune_btn.get_container(),  "finetune", LV_GRID_ALIGN_CENTER, 2, 1, LV_GRID_ALIGN_CENTER, 2, 1);
    main_layout.place(bedmesh_btn.get_container(),   "bedmesh", LV_GRID_ALIGN_CENTER, 3, 1, LV_GRID_ALIGN_CENTER, 2, 1);
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

    main_layout.place(temp_cont, "temps", LV_GRID_ALIGN_START, 0, 2, LV_GRID_ALIGN_START, 0, 2);

    lv_obj_t *mini_status = print_status_panel.get_mini_print_status().get_container();
    lv_obj_set_parent(mini_status, temp_cont);
    lv_obj_clear_flag(mini_status, LV_OBJ_FLAG_FLOATING);
    // Four tile columns wide, whatever its text.
    lv_obj_set_width(mini_status, 4 * SensorContainer::tile_width() + 3 * TILE_GAP);
    // Level with the Print tab when the tiles leave room above it (one row,
    // AD5M), else right under them: the tiles' row reaches down to it at
    // least. temp_cont sits at the top of main_cont's grid.
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

    main_layout.place(temp_cont, "temps", LV_GRID_ALIGN_START, 0, 2, LV_GRID_ALIGN_START, 0, 2);
#endif

    lv_obj_align(temp_chart, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_size(temp_chart, LV_PCT(45), LV_PCT(40));
#ifdef GUPPY_FF5M
    lv_obj_set_style_pad_top(temp_chart, 0, 0);
    lv_obj_set_style_pad_bottom(temp_chart, 4, 0);
#endif
    lv_obj_set_style_size(temp_chart, 0, LV_PART_INDICATOR);

    lv_chart_set_range(temp_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 300);
    main_layout.place(temp_chart, "chart", LV_GRID_ALIGN_END, 0, 2, LV_GRID_ALIGN_END, 2, 1);
    lv_chart_set_axis_tick(temp_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 0, 6, 5, true, 50);

    lv_chart_set_div_line_count(temp_chart, 3, 8);
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

void MainPanel::create_sensors(json &temp_sensors) {
  std::lock_guard<std::mutex> lock(lv_lock);
  sensors.clear();
#ifdef GUPPY_FF5M
  // PID shows the bed and the weight even when Home doesn't.
  std::map<std::string, lv_color_t> colors = sensor_colors(
      temp_sensors, {{PidPanel::BED, LV_PALETTE_PURPLE}, {PidPanel::WEIGHT, LV_PALETTE_ORANGE}});
  std::vector<NozzleTiles::Nozzle> nozzles;
  for (auto &sensor : temp_sensors.items()) {
    int tool = NozzleTiles::tool_of(sensor.key());
    if (tool >= 0) {
      nozzles.push_back({sensor.key(), sensor.value()["display_name"].template get<std::string>(), colors[sensor.key()],
                         tool});
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
                           display_name.c_str(), color_code, controllable, false, numpad, key,
                           temp_chart, temp_series)});
#ifdef GUPPY_FF5M
    sensors[key]->as_tile();
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
