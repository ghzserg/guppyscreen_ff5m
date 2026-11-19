#include "finetune_panel.h"
#ifdef GUPPY_FF5M
#include "sensor_container.h"
#endif
#include "nav.h"
#include "state.h"
#include "spdlog/spdlog.h"

#include <algorithm>
#include <cmath>

LV_IMG_DECLARE(home_z);
LV_IMG_DECLARE(zoffset_thicker_img);
LV_IMG_DECLARE(zoffset_thinner_img);
LV_IMG_DECLARE(pa_plus_img);
LV_IMG_DECLARE(pa_minus_img);
LV_IMG_DECLARE(refresh_img);
LV_IMG_DECLARE(speed_up_img);
LV_IMG_DECLARE(speed_down_img);
LV_IMG_DECLARE(flow_up_img);
LV_IMG_DECLARE(flow_down_img);

namespace {

// Klipper's factors as percents: 1.15 * 100 is 114.999..., rounded to 115.
int percent(double factor) {
  return static_cast<int>(std::lround(factor * 100));
}

}  // namespace

FineTunePanel::FineTunePanel(KWebSocketClient &websocket_client, std::mutex &l)
  : NotifyConsumer(l)
  , ws(websocket_client)
  , panel_cont(lv_obj_create(Nav::get().content()))
  , values_cont(lv_obj_create(panel_cont))
  , zreset_btn(panel_cont, &refresh_img, _("Reset Z") /* "Сброс Z" */, &FineTunePanel::_handle_zoffset, this)
  , zup_btn(panel_cont, &zoffset_thicker_img, "Z+", &FineTunePanel::_handle_zoffset, this)
  , zdown_btn(panel_cont, &zoffset_thinner_img, "Z-", &FineTunePanel::_handle_zoffset, this)
  , pareset_btn(panel_cont, &refresh_img, _("Reset PA") /* "Сброс PA" */, &FineTunePanel::_handle_pa, this)
  , paup_btn(panel_cont, &pa_plus_img, "PA+", &FineTunePanel::_handle_pa, this)
  , padown_btn(panel_cont, &pa_minus_img, "PA-", &FineTunePanel::_handle_pa, this)
  , speed_reset_btn(panel_cont, &refresh_img, _("Speed Reset") /* "Сброс скорость" */, &FineTunePanel::_handle_speed, this)
  , speed_up_btn(panel_cont, &speed_up_img, _("Speed+") /* "Скор+" */, &FineTunePanel::_handle_speed, this)
  , speed_down_btn(panel_cont, &speed_down_img, _("Speed-") /* "Скор-" */, &FineTunePanel::_handle_speed, this)
  , flow_reset_btn(panel_cont, &refresh_img, _("Flow Reset") /* "Сброс поток" */, &FineTunePanel::_handle_flow, this)
  , flow_up_btn(panel_cont, &flow_up_img, _("Flow+") /* "Поток+" */, &FineTunePanel::_handle_flow, this)
  , flow_down_btn(panel_cont, &flow_down_img, _("Flow-") /* "Поток-" */, &FineTunePanel::_handle_flow, this)
  , zoffset_selector(panel_cont, ("Z ("+ std::string(_("мм")) +") - PA ("+ std::string(_("мм")) +"/"+ _("с") +")").c_str(),
#ifdef GUPPY_FF5M
                     {".005", ".01", ".05", ".10", ""},  // no leading zero: fits the bigger digits
                     0, 40, 15,  // as wide as the multiplier's
#else
                     {"0.005", "0.01", "0.05", "0.10", ""},
                     0, 30, 15,
#endif
                     &FineTunePanel::_handle_callback, this)
  , multipler_selector(panel_cont, _("Multipler Step") /* "Множитель шага (%)" */,
                       {"1", "5", "10", "25", ""}, 0, 40, 15, &FineTunePanel::_handle_callback, this)
  , z_offset(values_cont, &home_z, 150, 100, 15, ("0.0 "+ std::string(_("мм"))).c_str() )
  , pa(values_cont, &pa_plus_img, 150, 100, 15, ("0.0 "+ std::string(_("мм")) +"/"+ _("с")).c_str() )
  , speed_factor(values_cont, &speed_up_img, 150, 100, 15 ,"100%")
  , flow_factor(values_cont, &flow_up_img, 150, 100, 15, "100%")
{
  lv_obj_move_background(panel_cont);

  lv_obj_set_size(panel_cont, LV_PCT(100), LV_PCT(100));
  lv_obj_clear_flag(panel_cont, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_clear_flag(values_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(values_cont, 0, 0);
#ifdef GUPPY_FF5M
  // The values in one row at the bottom, each under its column, no icons.
  lv_obj_set_size(values_cont, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_border_width(values_cont, 0, 0);
  lv_obj_set_style_bg_opa(values_cont, LV_OPA_TRANSP, 0);
  {
    static const lv_coord_t value_cols[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const lv_coord_t value_rows[] = {LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST};
    lv_obj_set_grid_dsc_array(values_cont, value_cols, value_rows);
  }
  for (int col = 0; col < VALUE_COUNT; col++) {
    ImageLabel *value = values[col];
    value->hide_image();
    lv_obj_set_height(value->get_container(), LV_SIZE_CONTENT);
    lv_obj_set_grid_cell(value->get_container(), LV_GRID_ALIGN_STRETCH, col, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    // In the buttons' digits.
    lv_obj_set_style_text_font(value->get_label(), SensorContainer::temperature_font(), 0);
  }
#else
  lv_obj_set_size(values_cont, LV_PCT(20), LV_PCT(80));
  lv_obj_set_flex_flow(values_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(values_cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
#endif

#ifdef GUPPY_FF5M
  // Four columns of buttons; the values in the last row.
  static lv_coord_t grid_main_row_dsc[] = {LV_GRID_FR(3), LV_GRID_FR(3), LV_GRID_FR(3), LV_GRID_FR(3), LV_GRID_FR(2),
    LV_GRID_TEMPLATE_LAST};
  static lv_coord_t grid_main_col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
    LV_GRID_TEMPLATE_LAST};
#else
  static lv_coord_t grid_main_row_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
    LV_GRID_TEMPLATE_LAST};
  static lv_coord_t grid_main_col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
    LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
#endif

  lv_obj_set_grid_dsc_array(panel_cont, grid_main_col_dsc, grid_main_row_dsc);

#ifdef GUPPY_FF5M
  // The step buttons on top, the values at the bottom.
  const int SELECT = 0, RESET = 1, UP = 2, DOWN = 3;
#else
  const int RESET = 0, UP = 1, DOWN = 2, SELECT = 3;
#endif

  // col 1
  lv_obj_set_grid_cell(zreset_btn.get_container(), LV_GRID_ALIGN_CENTER, 0, 1, LV_GRID_ALIGN_CENTER, RESET, 1);
  lv_obj_set_grid_cell(zup_btn.get_container(), LV_GRID_ALIGN_CENTER, 0, 1, LV_GRID_ALIGN_CENTER, UP, 1);
  lv_obj_set_grid_cell(zdown_btn.get_container(), LV_GRID_ALIGN_CENTER, 0, 1, LV_GRID_ALIGN_CENTER, DOWN, 1);
  lv_obj_set_grid_cell(zoffset_selector.get_container(), LV_GRID_ALIGN_CENTER, 0, 2, LV_GRID_ALIGN_CENTER, SELECT, 1);

  // col 2
  lv_obj_set_grid_cell(pareset_btn.get_container(), LV_GRID_ALIGN_CENTER, 1, 1, LV_GRID_ALIGN_CENTER, RESET, 1);
  lv_obj_set_grid_cell(paup_btn.get_container(), LV_GRID_ALIGN_CENTER, 1, 1, LV_GRID_ALIGN_CENTER, UP, 1);
  lv_obj_set_grid_cell(padown_btn.get_container(), LV_GRID_ALIGN_CENTER, 1, 1, LV_GRID_ALIGN_CENTER, DOWN, 1);

  // col 3
  lv_obj_set_grid_cell(speed_reset_btn.get_container(), LV_GRID_ALIGN_CENTER, 2, 1, LV_GRID_ALIGN_CENTER, RESET, 1);
  lv_obj_set_grid_cell(speed_up_btn.get_container(), LV_GRID_ALIGN_CENTER, 2, 1, LV_GRID_ALIGN_CENTER, UP, 1);
  lv_obj_set_grid_cell(speed_down_btn.get_container(), LV_GRID_ALIGN_CENTER, 2, 1, LV_GRID_ALIGN_CENTER, DOWN, 1);
  lv_obj_set_grid_cell(multipler_selector.get_container(), LV_GRID_ALIGN_CENTER, 2, 2, LV_GRID_ALIGN_CENTER, SELECT, 1);

  // col 4
  lv_obj_set_grid_cell(flow_reset_btn.get_container(), LV_GRID_ALIGN_CENTER, 3, 1, LV_GRID_ALIGN_CENTER, RESET, 1);
  lv_obj_set_grid_cell(flow_up_btn.get_container(), LV_GRID_ALIGN_CENTER, 3, 1, LV_GRID_ALIGN_CENTER, UP, 1);
  lv_obj_set_grid_cell(flow_down_btn.get_container(), LV_GRID_ALIGN_CENTER, 3, 1, LV_GRID_ALIGN_CENTER, DOWN, 1);

#ifdef GUPPY_FF5M
  // last row
  lv_obj_set_grid_cell(values_cont, LV_GRID_ALIGN_STRETCH, 0, 4, LV_GRID_ALIGN_CENTER, 4, 1);
  // Its columns lined up with the panel's.
  lv_obj_set_style_pad_column(values_cont, lv_obj_get_style_pad_column(panel_cont, 0), 0);
#else
  // col 5
  lv_obj_set_grid_cell(values_cont, LV_GRID_ALIGN_CENTER, 4, 1, LV_GRID_ALIGN_CENTER, 0, 3);
#endif

  for (int v = 0; v < VALUE_COUNT; v++) {
    flash_timers[v] = lv_timer_create(&FineTunePanel::end_flash, 1000, values[v]->get_container());
    lv_timer_pause(flash_timers[v]);
  }

  ws.register_notify_update(this);
}

FineTunePanel::~FineTunePanel() {
  for (lv_timer_t *timer : flash_timers) {
    lv_timer_del(timer);
  }
  if (panel_cont != NULL) {
    lv_obj_del(panel_cont);
    panel_cont = NULL;
  }
  ws.unregister_notify_update(this);
}

void FineTunePanel::show_values(const json &printer, bool flash_changes) {
  if (!printer.is_object()) {
    return;  // от принтера ещё ничего нет: на не-объекте value() бросает исключение
  }
  auto v = printer.value("/gcode_move/homing_origin/2"_json_pointer, json());
  if (!v.is_null()) {
    show(Z_OFFSET, fmt::format("{:.3f} "+ std::string(_("мм")) , v.template get<double>()), flash_changes);
  }

  v = printer.value("/extruder/pressure_advance"_json_pointer, json());
  if (!v.is_null()) {
    show(PA, fmt::format("{:.3f} "+ std::string(_("мм")) +"/"+ _("с") , v.template get<double>()), flash_changes);
  }

  v = printer.value("/gcode_move/speed_factor"_json_pointer, json());
  if (!v.is_null()) {
    show(SPEED, fmt::format("{}%", percent(v.template get<double>())), flash_changes);
  }

  v = printer.value("/gcode_move/extrude_factor"_json_pointer, json());
  if (!v.is_null()) {
    show(FLOW, fmt::format("{}%", percent(v.template get<double>())), flash_changes);
  }
}

void FineTunePanel::foreground() {
  // Копии берутся под блокировкой State: поток websocket вливает обновления
  // в то же дерево.
  json printer = json::object();
  for (const char *object : {"gcode_move", "extruder"}) {
    json v = State::get_instance()->get_data_copy(json::json_pointer(std::string("/printer_state/") + object));
    if (!v.is_null()) printer[object] = std::move(v);
  }
  show_values(printer, false);

  lv_obj_move_foreground(panel_cont);
}

void FineTunePanel::consume(json &j) {
  std::lock_guard<std::mutex> lock(lv_lock);
  const auto params = "/params/0"_json_pointer;
  if (j.contains(params)) {
    show_values(j[params], true);
  }
}

void FineTunePanel::show(Value value, const std::string &text, bool flash_if_changed) {
  ImageLabel &label = *values[value];
  if (text == lv_label_get_text(label.get_label())) {
    return;
  }
  label.update_label(text.c_str());
  if (!flash_if_changed) {
    return;
  }
  lv_timer_t *timer = flash_timers[value];
  lv_obj_set_style_border_color(label.get_container(), lv_palette_main(LV_PALETTE_YELLOW), 0);
  // Новое изменение в течение секунды продлевает рамку ещё на секунду.
  lv_timer_reset(timer);
  lv_timer_resume(timer);
}

void FineTunePanel::end_flash(lv_timer_t *timer) {
  // Обратно к цвету темы.
  lv_obj_remove_local_style_prop((lv_obj_t *)timer->user_data, LV_STYLE_BORDER_COLOR, 0);
  lv_timer_pause(timer);
}

void FineTunePanel::handle_callback(lv_event_t *e) {
  spdlog::trace("fine tune btn callback");
  if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED) {
    lv_obj_t *selector = lv_event_get_target(e);
    uint32_t idx = lv_btnmatrix_get_selected_btn(selector);

    if (selector == zoffset_selector.get_selector()) {
      zoffset_selector.set_selected_idx(idx);
    }

    if (selector == multipler_selector.get_selector()) {
      multipler_selector.set_selected_idx(idx);
    }
  }

  if (lv_event_get_code(e) == LV_EVENT_SHORT_CLICKED) {
  }
}

void FineTunePanel::handle_zoffset(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_SHORT_CLICKED) {
    lv_obj_t *btn = lv_event_get_current_target(e);

    if (btn == zreset_btn.get_container()) {
      spdlog::trace("clicked zoffset reset");
      ws.gcode_script("_G28\nSET_GCODE_OFFSET FROM=guppy Z=0 MOVE=1");
    } else {
      const char * step = lv_btnmatrix_get_btn_text(zoffset_selector.get_selector(),
                                                    zoffset_selector.get_selected_idx());
      spdlog::trace("clicked z {}", step);
      ws.gcode_script(fmt::format("_G28\nSET_GCODE_OFFSET FROM=guppy Z_ADJUST={}{} MOVE=1",
                                  btn == zup_btn.get_container() ? "+" : "-",
                                  step));
    }
  }
}

void FineTunePanel::handle_pa(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_SHORT_CLICKED) {
    lv_obj_t *btn = lv_event_get_current_target(e);

    if (btn == pareset_btn.get_container()) {
      spdlog::trace("clicked pa reset");
      auto v = State::get_instance()->get_data(
                     "/printer_state/configfile/settings/extruder/pressure_advance"_json_pointer);
      if (!v.is_null()) {
        ws.gcode_script(fmt::format("SET_PRESSURE_ADVANCE ADVANCE={}", v.template get<double>()));
      }
    } else {

      auto cur_pa = State::get_instance()
        ->get_data("/printer_state/extruder/pressure_advance"_json_pointer);
      if (!cur_pa.is_null()) {
        const char * step = lv_btnmatrix_get_btn_text(zoffset_selector.get_selector(),
                                                      zoffset_selector.get_selected_idx());

        double direction = btn == paup_btn.get_container() ? std::stod(step) : -std::stod(step);
        double new_pa = cur_pa.template get<double>() + direction;
        new_pa = new_pa < 0 ? 0 : new_pa;
        ws.gcode_script(fmt::format("SET_PRESSURE_ADVANCE ADVANCE={}", new_pa));
      }
    }
  }
}

void FineTunePanel::handle_speed(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_SHORT_CLICKED) {
    lv_obj_t *btn = lv_event_get_current_target(e);
    if (btn == speed_reset_btn.get_container()) {
      spdlog::trace("speed reset");
      ws.gcode_script("M220 S100");
    } else {
      auto spd_factor = State::get_instance()
        ->get_data("/printer_state/gcode_move/speed_factor"_json_pointer);
      if (!spd_factor.is_null()) {
        const char * step = lv_btnmatrix_get_btn_text(multipler_selector.get_selector(),
                                                      multipler_selector.get_selected_idx());

        int32_t direction = btn == speed_up_btn.get_container() ? std::stoi(step) : -std::stoi(step);
        int32_t new_speed = percent(spd_factor.template get<double>()) + direction;
        new_speed = std::max(new_speed, 1);
        spdlog::trace("speed step {}, {}", direction, new_speed);
        ws.gcode_script(fmt::format("M220 S{}", new_speed));
      }
    }
  }
}

void FineTunePanel::handle_flow(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_SHORT_CLICKED) {
    lv_obj_t *btn = lv_event_get_current_target(e);
    if (btn == flow_reset_btn.get_container()) {
      spdlog::trace("flow reset");
      ws.gcode_script("M221 S100");
    } else {
      auto extrude_factor = State::get_instance()
        ->get_data("/printer_state/gcode_move/extrude_factor"_json_pointer);
      if (!extrude_factor.is_null()) {
        const char * step = lv_btnmatrix_get_btn_text(multipler_selector.get_selector(),
                                                      multipler_selector.get_selected_idx());

        int32_t direction = btn == flow_up_btn.get_container() ? std::stoi(step) : -std::stoi(step);

        int32_t new_flow = percent(extrude_factor.template get<double>()) + direction;
        new_flow = std::max(new_flow, 1);
        spdlog::trace("flow step {}, {}", direction, new_flow);
        ws.gcode_script(fmt::format("M221 S{}", new_flow));
      }
    }
  }
}
