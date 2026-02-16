#include "retract_panel.h"
#include "state.h"
#include "spdlog/spdlog.h"

LV_IMG_DECLARE(refresh_img);
LV_IMG_DECLARE(back);

RetractPanel::RetractPanel(KWebSocketClient &c, std::mutex &l)
  : NotifyConsumer(l)
  , ws(c)
  , cont(lv_obj_create(lv_scr_act()))
  , retract_cont(lv_obj_create(cont))
  , retract_length(retract_cont, _("Retract Length") /* "Длина отката (мм)" */, &refresh_img, _("Reset") /* "Сброс" */, &refresh_img, NULL, &RetractPanel::_handle_callback, this, "")
  , retract_speed(retract_cont, _("Retract Speed") /* "Скорость отката (мм/с)" */, &refresh_img, _("Reset") /* "Сброс" */, &refresh_img, NULL, &RetractPanel::_handle_callback, this, "")
  , unretract_extra_length(retract_cont, _("Unretract Extra Length") /* "Доп. длина подачи (мм)" */, &refresh_img, _("Reset") /* "Сброс" */, &refresh_img, NULL, &RetractPanel::_handle_callback, this, "")
  , unretract_speed(retract_cont, _("Unretract Speed") /* "Скорость подачи (мм/с)" */, &refresh_img, _("Reset") /* "Сброс" */, &refresh_img, NULL, &RetractPanel::_handle_callback, this, "")
  , back_btn(cont, &back, _("Back") /* "Назад" */, &RetractPanel::_handle_callback, this)
  , retract_length_default(1.4)
  , retract_speed_default(70)
  , unretract_extra_length_default(15)
  , unretract_speed_default(70)
{
  lv_obj_move_background(cont);
  lv_obj_set_style_pad_all(cont, 0, 0);
  lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_center(retract_cont);
  lv_obj_set_size(retract_cont, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_flow(retract_cont, LV_FLEX_FLOW_COLUMN);

  lv_obj_align(back_btn.get_container(), LV_ALIGN_BOTTOM_RIGHT, 0, -20);

  ws.register_notify_update(this);
}

RetractPanel::~RetractPanel() {
  if (cont != NULL) {
    lv_obj_del(cont);
    cont = NULL;
  }
  ws.unregister_notify_update(this);
}

void RetractPanel::init(json &j) {
  State *s = State::get_instance();
  auto v = s->get_data("/printer_state/configfile/settings/firmware_retraction"_json_pointer);
  if (!v.is_null()) {
    if (v.contains("retract_length")) {
      retract_length_default = v["retract_length"].template get<float>();
      retract_length.set_range_float(0, 1400);
    }

    if (v.contains("retract_speed")) {
      retract_speed_default = v["retract_speed"].template get<int>();
      retract_speed.set_range(1, 70);
    }

    if (v.contains("unretract_extra_length")) {
      unretract_extra_length_default = v["unretract_extra_length"].template get<float>();
      unretract_extra_length.set_range_float(0, 15000);
    }

    if (v.contains("unretract_speed")) {
      unretract_speed_default = v["unretract_speed"].template get<int>();
      unretract_speed.set_range(1, 70);
    }
  }

  v = j["/result/status/firmware_retraction/retract_length"_json_pointer];
  if (!v.is_null()) {
    retract_length.update_value((int)((v.template get<float>())*1000));
  }

  v = j["/result/status/firmware_retraction/retract_speed"_json_pointer];
  if (!v.is_null()) {
    retract_speed.update_value(v.template get<int>());
  }

  v = j["/result/status/firmware_retraction/unretract_extra_length"_json_pointer];
  if (!v.is_null()) {
    unretract_extra_length.update_value((int)((v.template get<float>())*1000));
  }

  v = j["/result/status/firmware_retraction/unretract_speed"_json_pointer];
  if (!v.is_null()) {
    unretract_speed.update_value(v.template get<int>());
  }


}

void RetractPanel::foreground() {
  lv_obj_move_foreground(cont);
}

void RetractPanel::consume(json &j) {
  std::lock_guard<std::mutex> lock(lv_lock);
  auto v = j["/params/0/firmware_retraction/retract_length"_json_pointer];
  if (!v.is_null()) {
    retract_length.update_value((int)((v.template get<float>())*1000));
  }

  v = j["/params/0/firmware_retraction/retract_speed"_json_pointer];
  if (!v.is_null()) {
    retract_speed.update_value(v.template get<int>());
  }

  v = j["/params/0/firmware_retraction/unretract_extra_length"_json_pointer];
  if (!v.is_null()) {
    unretract_extra_length.update_value((int)((v.template get<float>())*1000));
  }

  v = j["/params/0/firmware_retraction/unretract_speed"_json_pointer];
  if (!v.is_null()) {
    unretract_speed.update_value(v.template get<int>());
  }

}

void RetractPanel::handle_callback(lv_event_t *e) {
  lv_obj_t *btn = lv_event_get_current_target(e);

  if (btn == back_btn.get_container()) {
    lv_obj_move_background(cont);
    return;
  }

  if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
    lv_obj_t *obj = lv_event_get_target(e);
    int v = lv_slider_get_value(obj);

    if (obj == retract_length.get_slider()) {
      ws.gcode_script(fmt::format("SET_RETRACTION RETRACT_LENGTH={}", (float)v/1000));

    } else if (obj == retract_speed.get_slider()) {
      ws.gcode_script(fmt::format("SET_RETRACTION RETRACT_SPEED={}", v));

    } else if (obj == unretract_extra_length.get_slider()) {
      ws.gcode_script(fmt::format("SET_RETRACTION UNRETRACT_EXTRA_LENGTH={}", (float)v/1000));

    } else if (obj == unretract_speed.get_slider()) {
      ws.gcode_script(fmt::format("SET_RETRACTION UNRETRACT_SPEED={}", v));

    }

  } else if (lv_event_get_code(e) == LV_EVENT_SHORT_CLICKED) {
    if (btn == retract_length.get_off()) {
      ws.gcode_script(fmt::format("SET_RETRACTION RETRACT_LENGTH={}", retract_length_default));

    } else if (btn == retract_speed.get_off()) {
      ws.gcode_script(fmt::format("SET_RETRACTION RETRACT_SPEED={}", retract_speed_default));

    } else if (btn == unretract_extra_length.get_off()) {
      ws.gcode_script(fmt::format("SET_RETRACTION UNRETRACT_EXTRA_LENGTH={}", unretract_extra_length_default));

    } else if (btn == unretract_speed.get_off()) {
      ws.gcode_script(fmt::format("SET_RETRACTION UNRETRACT_SPEED={}", unretract_speed_default));

    }
  }
}
