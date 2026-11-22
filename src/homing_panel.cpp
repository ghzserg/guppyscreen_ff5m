#include "homing_panel.h"
#include "nav.h"
#include "state.h"
#include "utils.h"
#include "spdlog/spdlog.h"
#include "config.h"

static const float distances[] = {0.1, 0.5, 1, 5, 10, 25, 50};

LV_IMG_DECLARE(arrow_left);
LV_IMG_DECLARE(arrow_up);
LV_IMG_DECLARE(arrow_right);
LV_IMG_DECLARE(arrow_down);
LV_IMG_DECLARE(home);
LV_IMG_DECLARE(z_closer);
LV_IMG_DECLARE(z_farther);
LV_IMG_DECLARE(motor_off_img);

HomingPanel::HomingPanel(KWebSocketClient &websocket_client, std::mutex &lock)
  : NotifyConsumer(lock)
  , ws(websocket_client)
  , homing_cont(lv_obj_create(Nav::get().content()))
  , home_all_btn(homing_cont, &home, "G28", &HomingPanel::_handle_callback, this)
  , home_xy_btn(homing_cont, &home, "G28 XY", &HomingPanel::_handle_callback, this)
  , y_up_btn(homing_cont, &arrow_up, "Y+", &HomingPanel::_handle_callback, this)
  , y_down_btn(homing_cont, &arrow_down, "Y-", &HomingPanel::_handle_callback, this)
  , x_up_btn(homing_cont, &arrow_right, "X+", &HomingPanel::_handle_callback, this)
  , x_down_btn(homing_cont, &arrow_left, "X-", &HomingPanel::_handle_callback, this)
  , z_up_btn(homing_cont, &z_closer, "Z+", &HomingPanel::_handle_callback, this)
  , z_down_btn(homing_cont, &z_farther, "Z-", &HomingPanel::_handle_callback, this)
  , motoroff_btn(homing_cont, &motor_off_img, _("Motor off") /* "Мотор выкл" */, &HomingPanel::_handle_callback, this)
  , distance_selector(homing_cont, _("Travel distance (mm)") /* "Расстояние перемещения (мм)" */,
                     {".1", ".5", "1", "5", "10", "25", "50", ""}, 2, 70, 15, &HomingPanel::_handle_selector_cb, this)
  , motion_status(lv_obj_create(homing_cont))
{
  lv_obj_clear_flag(homing_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_height(homing_cont, lv_pct(100));
  lv_obj_set_width(homing_cont, lv_pct(100));

  static lv_coord_t grid_main_row_dsc[] = {LV_GRID_FR(4), LV_GRID_FR(4), LV_GRID_FR(2), LV_GRID_TEMPLATE_LAST};
  static lv_coord_t grid_main_col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
    LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};

  lv_obj_set_grid_dsc_array(homing_cont, grid_main_col_dsc, grid_main_row_dsc);

  // row 1
  lv_obj_set_grid_cell(home_all_btn.get_container(), LV_GRID_ALIGN_CENTER, 0, 1, LV_GRID_ALIGN_CENTER, 0, 1);
  lv_obj_set_grid_cell(y_up_btn.get_container(), LV_GRID_ALIGN_CENTER, 1, 1, LV_GRID_ALIGN_CENTER, 0, 1);
  lv_obj_set_grid_cell(home_xy_btn.get_container(), LV_GRID_ALIGN_CENTER, 2, 1, LV_GRID_ALIGN_CENTER, 0, 1);
  lv_obj_set_grid_cell(z_up_btn.get_container(), LV_GRID_ALIGN_CENTER, 3, 1, LV_GRID_ALIGN_CENTER, 0, 1);

  // row 2
  lv_obj_set_grid_cell(x_down_btn.get_container(), LV_GRID_ALIGN_CENTER, 0, 1, LV_GRID_ALIGN_CENTER, 1, 1);
  lv_obj_set_grid_cell(y_down_btn.get_container(), LV_GRID_ALIGN_CENTER, 1, 1, LV_GRID_ALIGN_CENTER, 1, 1);
  lv_obj_set_grid_cell(x_up_btn.get_container(), LV_GRID_ALIGN_CENTER, 2, 1, LV_GRID_ALIGN_CENTER, 1, 1);
  lv_obj_set_grid_cell(z_down_btn.get_container(), LV_GRID_ALIGN_CENTER, 3, 1, LV_GRID_ALIGN_CENTER, 1, 1);
  lv_obj_set_grid_cell(motoroff_btn.get_container(), LV_GRID_ALIGN_CENTER, 4, 1, LV_GRID_ALIGN_CENTER, 1, 1);

  lv_obj_set_grid_cell(distance_selector.get_container(), LV_GRID_ALIGN_CENTER, 0, 5, LV_GRID_ALIGN_CENTER, 2, 1);
  lv_obj_remove_style_all(motion_status);
  lv_obj_add_flag(motion_status, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  lv_obj_clear_flag(motion_status, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_grid_cell(motion_status, LV_GRID_ALIGN_STRETCH, 4, 1, LV_GRID_ALIGN_CENTER, 0, 1);

  const int rows[] = {0, 1, 2, 4};
  const auto line_height = lv_font_get_line_height(lv_obj_get_style_text_font(motion_status, 0));
  lv_obj_set_height(motion_status, 7 * line_height);
  for (size_t i = 0; i < 5; ++i) {
    auto *label = lv_label_create(i == 4 ? homing_cont : motion_status);
    if (i < 4) lv_obj_set_y(label, rows[i] * line_height);
    else lv_obj_add_flag(label, LV_OBJ_FLAG_IGNORE_LAYOUT);
    // У этих надписей нет выносных элементов за границами строки.
    // Иначе запас LVGL захватывает соседнюю строку при перерисовке.
    lv_obj_add_event_cb(label, [](lv_event_t *event) {
      *static_cast<lv_coord_t *>(lv_event_get_param(event)) = 0;
    }, LV_EVENT_REFR_EXT_DRAW_SIZE, nullptr);
    lv_obj_refresh_ext_draw_size(label);
    motion_labels[i] = label;
  }

  ws.register_notify_update(this);
}

HomingPanel::~HomingPanel() {
}

void HomingPanel::consume(json &j) {
  static const auto move_path = "/params/0/gcode_move"_json_pointer;
  if (j.contains(move_path)) {
    const auto &move = j.at(move_path);
    if (move.contains("gcode_position") || move.contains("homing_origin") ||
        move.contains("absolute_coordinates")) {
      std::lock_guard<std::mutex> lock(lv_lock);
      if (lv_obj_get_child(lv_obj_get_parent(homing_cont), -1) == homing_cont) {
        update_motion_status(move, true);
      }
    }
  }

  auto v = j["/params/0/toolhead/homed_axes"_json_pointer];
  if (!v.is_null()) {
    std::string homed_axes = v.template get<std::string>();
    std::lock_guard<std::mutex> lock(lv_lock);
    if (homed_axes.find("x") != std::string::npos) {
      x_up_btn.enable();
      x_down_btn.enable();
    } else {
      x_up_btn.disable();
      x_down_btn.disable();
    }

    if (homed_axes.find("y") != std::string::npos) {
      y_up_btn.enable();
      y_down_btn.enable();
    } else {
      y_up_btn.disable();
      y_down_btn.disable();
    }

    if (homed_axes.find("z") != std::string::npos) {
      z_up_btn.enable();
      z_down_btn.enable();
    } else {
      z_up_btn.disable();
      z_down_btn.disable();
    }
  }
}

lv_obj_t *HomingPanel::get_container() {
  return homing_cont;
}

void HomingPanel::foreground() {
  auto v = State::get_instance()
    ->get_data("/printer_state/toolhead/homed_axes"_json_pointer);
  if (!v.is_null()) {
    std::string homed_axes = v.template get<std::string>();
    if (homed_axes.find("x") != std::string::npos) {
      x_up_btn.enable();
      x_down_btn.enable();
    } else {
      x_up_btn.disable();
      x_down_btn.disable();
    }

    if (homed_axes.find("y") != std::string::npos) {
      y_up_btn.enable();
      y_down_btn.enable();
    } else {
      y_up_btn.disable();
      y_down_btn.disable();
    }

    //Set the Z axis buttons
    z_up_btn.set_image(&z_farther);
    z_down_btn.set_image(&z_closer);

    if (homed_axes.find("z") != std::string::npos) {
      z_up_btn.enable();
      z_down_btn.enable();
    } else {
      z_up_btn.disable();
      z_down_btn.disable();
    }
  }

  //Set the Z axis buttons

  v = Config::get_instance()->get_json("/invert_z_icon");
  bool inverted = !v.is_null() && v.template get<bool>();
  if (inverted) {
    // UP arrow
    z_up_btn.set_image(&z_farther);
    z_down_btn.set_image(&z_closer);
  } else {
    // DOWN arrow
    z_up_btn.set_image(&z_closer);
    z_down_btn.set_image(&z_farther);
  }

  update_motion_status();
  lv_obj_move_foreground(homing_cont);
  lv_obj_update_layout(homing_cont);
  const auto button_height = lv_obj_get_height(z_up_btn.get_container());
  lv_obj_set_style_translate_y(motion_status, (lv_obj_get_height(motion_status) - button_height) / 2, 0);
  lv_obj_set_style_translate_y(motoroff_btn.get_container(),
                               (button_height - lv_obj_get_height(motoroff_btn.get_container())) / 2, 0);
  lv_obj_update_layout(homing_cont);
  lv_obj_align_to(motion_labels[4], motoroff_btn.get_container(), LV_ALIGN_OUT_BOTTOM_MID, 0, 8);
}

void HomingPanel::update_motion_status() {
  static const auto move_path = "/printer_state/gcode_move"_json_pointer;
  update_motion_status(State::get_instance()->get_data_copy(move_path), false);
}

void HomingPanel::update_motion_status(const json &move, bool partial) {
  const auto set_coordinate = [&move](lv_obj_t *label, const char *name,
                                      json::const_iterator values, size_t index, int precision) {
    const bool valid = values != move.end() && values->is_array() &&
                       index < values->size() && (*values)[index].is_number();
    const auto text = valid ? fmt::format("{}: {:.{}f}", name, (*values)[index].get<double>(), precision)
                            : fmt::format("{}: —", name);
    KUtils::set_label_text(label, text.c_str());
  };

  const auto position = move.find("gcode_position");
  if (!partial || position != move.end()) {
    const char *axes[] = {"X", "Y", "Z"};
    for (size_t i = 0; i < 3; ++i) {
      set_coordinate(motion_labels[i], axes[i], position, i, 2);
    }
  }
  const auto offset = move.find("homing_origin");
  if (!partial || offset != move.end()) {
    set_coordinate(motion_labels[3], "Z-offset", offset, 2, 3);
  }
  const auto xyz = move.find("absolute_coordinates");
  if (!partial || xyz != move.end()) {
    const char *text = "XYZ: —";
    if (xyz != move.end() && xyz->is_boolean()) {
      text = xyz->get<bool>() ? "XYZ: G90" : "XYZ: G91";
    }
    KUtils::set_label_text(motion_labels[4], text);
  }
}

void HomingPanel::handle_callback(lv_event_t *event) {
  if (State::print_active()) return;
  lv_obj_t *btn = lv_event_get_current_target(event);
  if ((btn == home_all_btn.get_container() || btn == home_xy_btn.get_container() ||
       btn == motoroff_btn.get_container()) && State::print_pause_active())
    return;
  const char * distance = lv_btnmatrix_get_btn_text(distance_selector.get_selector(),
                                                    distance_selector.get_selected_idx());
  std::string move_op;

  if (btn == home_all_btn.get_container()) {
    spdlog::debug("home all pressed");
    ws.gcode_script("G28");

  }
  else if (btn == home_xy_btn.get_container()) {
    spdlog::debug("home xy pressed");
    ws.gcode_script("G28 X Y");

  }
  else if (btn == y_up_btn.get_container()) {
    spdlog::debug("y up pressed");
    move_op = fmt::format("_CLIENT_LINEAR_MOVE Y=+{} F=7800", distance);

  }
  else if (btn == y_down_btn.get_container()) {
    spdlog::debug("y down pressed");
    move_op = fmt::format("_CLIENT_LINEAR_MOVE Y=-{} F=7800", distance);
  }
  else if (btn == x_up_btn.get_container()) {
    spdlog::debug("x up pressed");
    move_op = fmt::format("_CLIENT_LINEAR_MOVE X=+{} F=7800", distance);
  }
  else if (btn == x_down_btn.get_container()) {
    spdlog::debug("x down pressed");
    move_op = fmt::format("_CLIENT_LINEAR_MOVE X=-{} F=7800", distance);
  }
  else if (btn == z_up_btn.get_container()) {
    spdlog::debug("z up pressed");
    move_op = fmt::format("_CLIENT_LINEAR_MOVE Z=+{} F=7800", distance);
  }
  else if (btn == z_down_btn.get_container()) {
    spdlog::debug("z down pressed");
    move_op = fmt::format("_CLIENT_LINEAR_MOVE Z=-{} F=7800", distance);
  }
  else if (btn == motoroff_btn.get_container()) {
    spdlog::debug("motor off pressed");
    ws.gcode_script("M84");
  }
  else {
    spdlog::debug("Unknown action button pressed");
  }

  if (move_op.size() > 0) {
    // ws.gcode_script("G91");
    ws.gcode_script(fmt::format("G91\n{}", move_op));
  }
}

void HomingPanel::handle_selector_cb(lv_event_t *event) {
  lv_obj_t * obj = lv_event_get_target(event);
  uint32_t idx = lv_btnmatrix_get_selected_btn(obj);
  distance_selector.set_selected_idx(idx);
  spdlog::debug("selector move distance index {}", idx);
}
