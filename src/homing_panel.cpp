#include "homing_panel.h"
#include "nav.h"
#include "grid_layout.h"
#include "state.h"
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
{
  lv_obj_clear_flag(homing_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_height(homing_cont, lv_pct(100));
  lv_obj_set_width(homing_cont, lv_pct(100));

  static GridLayout layout = GridLayout::from_config("homing",
      {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1)},
      {LV_GRID_FR(4), LV_GRID_FR(4), LV_GRID_FR(2)});

  layout.apply_tracks(homing_cont);

  // row 1
  layout.place(home_all_btn.get_container(), "home_all", LV_GRID_ALIGN_CENTER, 0, 1, LV_GRID_ALIGN_CENTER, 0, 1);
  layout.place(y_up_btn.get_container(), "y_up", LV_GRID_ALIGN_CENTER, 1, 1, LV_GRID_ALIGN_CENTER, 0, 1);
  layout.place(home_xy_btn.get_container(), "home_xy", LV_GRID_ALIGN_CENTER, 2, 1, LV_GRID_ALIGN_CENTER, 0, 1);
  layout.place(z_up_btn.get_container(), "z_up", LV_GRID_ALIGN_CENTER, 3, 1, LV_GRID_ALIGN_CENTER, 0, 1);

  // row 2
  layout.place(x_down_btn.get_container(), "x_down", LV_GRID_ALIGN_CENTER, 0, 1, LV_GRID_ALIGN_CENTER, 1, 1);
  layout.place(y_down_btn.get_container(), "y_down", LV_GRID_ALIGN_CENTER, 1, 1, LV_GRID_ALIGN_CENTER, 1, 1);
  layout.place(x_up_btn.get_container(), "x_up", LV_GRID_ALIGN_CENTER, 2, 1, LV_GRID_ALIGN_CENTER, 1, 1);
  layout.place(z_down_btn.get_container(), "z_down", LV_GRID_ALIGN_CENTER, 3, 1, LV_GRID_ALIGN_CENTER, 1, 1);
  layout.place(motoroff_btn.get_container(), "motoroff", LV_GRID_ALIGN_CENTER, 4, 1, LV_GRID_ALIGN_CENTER, 1, 1);

  layout.place(distance_selector.get_container(), "distance_selector", LV_GRID_ALIGN_CENTER, 0, 5, LV_GRID_ALIGN_CENTER, 2, 1);


  ws.register_notify_update(this);
}

HomingPanel::~HomingPanel() {
}

void HomingPanel::consume(json &j) {
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

  lv_obj_move_foreground(homing_cont);
}

void HomingPanel::handle_callback(lv_event_t *event) {
  lv_obj_t *btn = lv_event_get_current_target(event);
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
