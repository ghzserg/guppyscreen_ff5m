#include "limits_panel.h"
#include "state.h"
#include "spdlog/spdlog.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
double limit_number(const json &object, const char *key) {
  const auto value = object.find(key);
  return value != object.end() && value->is_number()
    ? value->get<double>() : std::numeric_limits<double>::quiet_NaN();
}
bool valid_ratio(double value) { return std::isfinite(value) && value >= 0 && value < 1; }
bool valid_accel(double value) { return std::isfinite(value) && value > 0; }
}  // namespace

LV_IMG_DECLARE(refresh_img);
LV_IMG_DECLARE(back);

LimitsPanel::LimitsPanel(KWebSocketClient &c, std::mutex &l)
  : NotifyConsumer(l)
  , ws(c)
  , cont(lv_obj_create(lv_scr_act()))
  , limit_cont(lv_obj_create(cont))
  , velocity(limit_cont, _("Velocity (mm/s)") /* "Скорость (мм/с)" */, &refresh_img, _("Reset") /* "Сброс" */, &refresh_img, NULL, &LimitsPanel::_handle_callback, this, "")
  , acceleration(limit_cont, _("Acceleration (mm/s2)") /* "Ускорение (мм/с2)" */, &refresh_img, _("Reset") /* "Сброс" */, &refresh_img, NULL, &LimitsPanel::_handle_callback, this, "")
  , square_corner(limit_cont, _("Square Corner Velocity (mm/s)") /* "Скорость на прямых углах SCV (мм/с)" */, &refresh_img, _("Reset") /* "Сброс" */, &refresh_img, NULL, &LimitsPanel::_handle_callback, this, "")
  , accel_to_decel(limit_cont, _("Acceleration to Deceleration (mm/s2)") /* "Ограничение ускорение зигзагов (мм/с2)" */, &refresh_img, _("Reset") /* "Сброс" */, &refresh_img, NULL, &LimitsPanel::_handle_callback, this, "")
  , back_btn(cont, &back, _("Back") /* "Назад" */, &LimitsPanel::_handle_callback, this)
  , max_velocity_default(1000)
  , max_accel_default(20000)
  , max_accel_to_decel_default(10000)
  , square_corner_default(5)
{
  lv_obj_move_background(cont);
  lv_obj_set_style_pad_all(cont, 0, 0);
  lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_center(limit_cont);
  lv_obj_set_size(limit_cont, lv_pct(100), lv_pct(100));
  lv_obj_set_flex_flow(limit_cont, LV_FLEX_FLOW_COLUMN);

  lv_obj_align(back_btn.get_container(), LV_ALIGN_BOTTOM_RIGHT, 0, -20);

  lv_obj_add_flag(accel_to_decel.get_container(), LV_OBJ_FLAG_HIDDEN);
  ws.register_notify_update(this);
}

LimitsPanel::~LimitsPanel() {
  if (cont != NULL) {
    lv_obj_del(cont);
    cont = NULL;
  }
}

void LimitsPanel::init(json &j) {
  State *s = State::get_instance();
  auto v = s->get_data("/printer_state/configfile/settings/printer"_json_pointer);
  if (!v.is_null()) {
    if (v.contains("max_velocity")) {
      max_velocity_default = v["max_velocity"].template get<int>();
      velocity.set_range(1, max_velocity_default);
    }

    if (v.contains("max_accel")) {
      max_accel_default = v["max_accel"].template get<int>();
      acceleration.set_range(1, max_accel_default);
    }

    if (v.contains("square_corner_velocity")) {
      square_corner_default = v["square_corner_velocity"].template get<int>();
      square_corner.set_range(0, square_corner_default);
    }

  }

  const json &reply = j;
  const auto toolhead_path = "/result/status/toolhead"_json_pointer;
  const json toolhead = reply.contains(toolhead_path) ? reply.at(toolhead_path) : json::object();
  configure_motion_limit(v, toolhead);

  v = j["/result/status/toolhead/max_velocity"_json_pointer];
  if (!v.is_null()) {
    velocity.update_value(v.template get<int>());
  }

  v = j["/result/status/toolhead/max_accel"_json_pointer];
  if (!v.is_null()) {
    acceleration.update_value(v.template get<int>());
  }

  v = j["/result/status/toolhead/square_corner_velocity"_json_pointer];
  if (!v.is_null()) {
    square_corner.update_value(v.template get<int>());
  }
}

void LimitsPanel::configure_motion_limit(const json &settings, const json &toolhead) {
  const double current_ratio = limit_number(toolhead, "minimum_cruise_ratio");
  const double current_accel = limit_number(toolhead, "max_accel_to_decel");
  const double configured_ratio = limit_number(settings, "minimum_cruise_ratio");
  const double configured_accel = limit_number(settings, "max_accel_to_decel");

  // Prefer the live API over config aliases retained by compatibility forks.
  motion_limit = MotionLimit::None;
  if (valid_ratio(current_ratio)) motion_limit = MotionLimit::MinimumCruiseRatio;
  else if (valid_accel(current_accel)) motion_limit = MotionLimit::AccelToDecel;
  else if (valid_ratio(configured_ratio)) motion_limit = MotionLimit::MinimumCruiseRatio;
  else if (valid_accel(configured_accel)) motion_limit = MotionLimit::AccelToDecel;

  if (motion_limit == MotionLimit::None) {
    lv_obj_add_flag(accel_to_decel.get_container(), LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_obj_clear_flag(accel_to_decel.get_container(), LV_OBJ_FLAG_HIDDEN);
  if (motion_limit == MotionLimit::MinimumCruiseRatio) {
    minimum_cruise_ratio_default = valid_ratio(configured_ratio) ? configured_ratio : current_ratio;
    accel_to_decel.set_label(_("Minimum Cruise Ratio"));
    // Klipper accepts 0 <= ratio < 1. Slider units are thousandths.
    accel_to_decel.set_range_float(0, 999);
    accel_to_decel.update_value(std::clamp((int)std::lround(minimum_cruise_ratio_default * 1000), 0, 999));
  } else {
    max_accel_to_decel_default = valid_accel(configured_accel) ? configured_accel : current_accel;
    accel_to_decel.set_label(_("Acceleration to Deceleration (mm/s2)"));
    accel_to_decel.set_range(1, std::max(1, (int)std::ceil(max_accel_to_decel_default)));
    accel_to_decel.update_value(std::max(1, (int)std::lround(max_accel_to_decel_default)));
  }
  update_motion_limit(toolhead);
}

void LimitsPanel::update_motion_limit(const json &toolhead) {
  if (motion_limit == MotionLimit::MinimumCruiseRatio) {
    const double value = limit_number(toolhead, "minimum_cruise_ratio");
    if (valid_ratio(value)) {
      accel_to_decel.update_value(std::clamp((int)std::lround(value * 1000), 0, 999));
    }
  } else if (motion_limit == MotionLimit::AccelToDecel) {
    const double value = limit_number(toolhead, "max_accel_to_decel");
    if (valid_accel(value)) {
      const int scaled = std::max(1, (int)std::lround(value));
      const int upper = lv_slider_get_max_value(accel_to_decel.get_slider());
      if (scaled > upper) accel_to_decel.set_range(1, scaled);
      accel_to_decel.update_value(scaled);
    }
  }
}

void LimitsPanel::send_motion_limit(double value) {
  if (motion_limit == MotionLimit::MinimumCruiseRatio && valid_ratio(value)) {
    ws.gcode_script(fmt::format("SET_VELOCITY_LIMIT MINIMUM_CRUISE_RATIO={}", value));
  } else if (motion_limit == MotionLimit::AccelToDecel && valid_accel(value)) {
    ws.gcode_script(fmt::format("SET_VELOCITY_LIMIT ACCEL_TO_DECEL={}", value));
  }
}

void LimitsPanel::foreground() {
  lv_obj_move_foreground(cont);
}

void LimitsPanel::consume(json &j) {
  std::lock_guard<std::mutex> lock(lv_lock);
  auto v = j["/params/0/toolhead/max_velocity"_json_pointer];
  if (!v.is_null()) {
    velocity.update_value(v.template get<int>());
  }

  v = j["/params/0/toolhead/max_accel"_json_pointer];
  if (!v.is_null()) {
    acceleration.update_value(v.template get<int>());
  }

  const json &notification = j;
  const auto toolhead_path = "/params/0/toolhead"_json_pointer;
  if (notification.contains(toolhead_path)) update_motion_limit(notification.at(toolhead_path));

  v = j["/params/0/toolhead/square_corner_velocity"_json_pointer];
  if (!v.is_null()) {
    square_corner.update_value(v.template get<int>());
  }

}

void LimitsPanel::handle_callback(lv_event_t *e) {
  lv_obj_t *btn = lv_event_get_current_target(e);

  if (btn == back_btn.get_container()) {
    lv_obj_move_background(cont);
    return;
  }

  if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
    lv_obj_t *obj = lv_event_get_target(e);
    int v = lv_slider_get_value(obj);

    if (obj == velocity.get_slider()) {
      ws.gcode_script(fmt::format("SET_VELOCITY_LIMIT VELOCITY={}", v));

    } else if (obj == acceleration.get_slider()) {
      ws.gcode_script(fmt::format("SET_VELOCITY_LIMIT ACCEL={}", v));

    } else if (obj == accel_to_decel.get_slider()) {
      send_motion_limit(motion_limit == MotionLimit::MinimumCruiseRatio
                          ? std::clamp(v, 0, 999) / 1000.0 : std::max(v, 1));

    } else if (obj == square_corner.get_slider()) {
      ws.gcode_script(fmt::format("SET_VELOCITY_LIMIT SQUARE_CORNER_VELOCITY={}", v));

    }

  } else if (lv_event_get_code(e) == LV_EVENT_SHORT_CLICKED) {
    if (btn == velocity.get_off()) {
      ws.gcode_script(fmt::format("SET_VELOCITY_LIMIT VELOCITY={}", max_velocity_default));

    } else if (btn == acceleration.get_off()) {
      ws.gcode_script(fmt::format("SET_VELOCITY_LIMIT ACCEL={}", max_accel_default));

    } else if (btn == accel_to_decel.get_off()) {
      send_motion_limit(motion_limit == MotionLimit::MinimumCruiseRatio
                          ? minimum_cruise_ratio_default : max_accel_to_decel_default);

    } else if (btn == square_corner.get_off()) {
      ws.gcode_script(fmt::format("SET_VELOCITY_LIMIT SQUARE_CORNER_VELOCITY={}", square_corner_default));

    }
  }
}
