#include "print_status_panel.h"
#include "main_panel.h"
#include "nav.h"
#include "finetune_panel.h"
#include "retract_panel.h"
#include "exclude_panel.h"
#include "pid_panel.h"
#include "guppyscreen.h"
#include "state.h"
#include "config.h"
#include "utils.h"
#include "spdlog/spdlog.h"
#include <algorithm>
#include <cstring>
#include <unistd.h> // Заголовочный файл для POSIX-функций
#include <fstream>

LV_IMG_DECLARE(extruder);
LV_IMG_DECLARE(speed_up_img);
LV_IMG_DECLARE(extrude);
LV_IMG_DECLARE(clock_img);
LV_IMG_DECLARE(hourglass);
LV_IMG_DECLARE(filament_img);
LV_IMG_DECLARE(bed);
LV_IMG_DECLARE(home_z);
LV_IMG_DECLARE(fan);
LV_IMG_DECLARE(layers_img);

LV_IMG_DECLARE(retract);
#ifdef GUPPY_FF5M
LV_IMG_DECLARE(speed_img);  // unlike the Tune tab's sliders in the nav bar
#define FINE_TUNE_IMG speed_img
#else
LV_IMG_DECLARE(fine_tune_img);
#define FINE_TUNE_IMG fine_tune_img
#endif
#ifdef GUPPY_FF5M
LV_IMG_DECLARE(limit_img);
#include "sensor_container.h"
#endif
LV_IMG_DECLARE(pause_img);
LV_IMG_DECLARE(resume);
LV_IMG_DECLARE(cancel);

double pi() { return std::atan(1)*4; }

PrintStatusPanel::PrintStatusPanel(KWebSocketClient &websocket_client,
                                   std::mutex &lock,
                                   lv_obj_t *mini_parent,
                                   MainPanel &main_p)
  : NotifyConsumer(lock)
  , ws(websocket_client)
  , main_panel_ref(main_p)
  , finetune_panel(websocket_client, lock)
  , retract_panel(websocket_client, lock)
  , exclude_panel(websocket_client, lock)
  , pid_panel(websocket_client, lock)
  , mini_print_status(mini_parent, &PrintStatusPanel::_handle_callback, this, websocket_client)
  , status_cont(lv_obj_create(Nav::get().content()))
  , buttons_cont(lv_obj_create(status_cont))
#ifdef GUPPY_FF5M
  , limits_btn(buttons_cont, &limit_img, _("Limits") /* "Ограничения" */, &PrintStatusPanel::_handle_callback, this)
#endif
  , finetune_btn(buttons_cont, &FINE_TUNE_IMG, _("Fine Tune") /* "Настройки" */, &PrintStatusPanel::_handle_callback, this)
  , retract_btn(buttons_cont, &retract, _("Retract") /* "Ретракт" */, &PrintStatusPanel::_handle_callback, this)
  , pause_btn(buttons_cont, &pause_img, _("Pause") /* "Пауза" */, &PrintStatusPanel::_handle_callback, this)
  , resume_btn(buttons_cont, &resume, _("Resume") /* "Продолжить" */, &PrintStatusPanel::_handle_callback, this)
  , cancel_btn(buttons_cont, &cancel, _("Cancel") /* "Отменить" */, &PrintStatusPanel::_handle_callback, this,
               _("Do you want to cancel the print?") /* "Вы хотите отменить печать?" */,
               [&websocket_client]() {
                 spdlog::debug("cancel print prompt");
                 websocket_client.send_jsonrpc("printer.print.cancel");
                 websocket_client.send_jsonrpc("printer.gcode.script",
                    json::parse(R"({"script":"SDCARD_RESET_FILE"})"));
               })
  , thumbnail_cont(lv_obj_create(status_cont))
  , thumbnail(lv_img_create(thumbnail_cont))
#ifdef GUPPY_FF5M
  , pbar_cont(lv_obj_create(status_cont))  // a row of its own
#else
  , pbar_cont(lv_obj_create(thumbnail_cont))
#endif
  , progress_bar(lv_bar_create(pbar_cont))
  , progress_label(lv_label_create(pbar_cont))
  , progress_end(lv_label_create(pbar_cont))
  , detail_cont(lv_obj_create(status_cont))
  , extruder_temp(detail_cont, &extruder, 100, "20")
  , bed_temp(detail_cont, &bed, 100, "...")
  , print_speed(detail_cont, &speed_up_img, 100, ("0 "+ std::string(_("mm")) +"/"+ _("s")).c_str())
  , z_offset(detail_cont, &home_z, 100, ("0.0 "+ std::string(_("mm"))).c_str())
  , flow_rate(detail_cont, &extrude, 100, ("0.0 "+ std::string(_("mm")) +"3/"+ _("s")).c_str())
  , layers(detail_cont, &layers_img, 100, "...")
  , fan0(detail_cont, &fan, 100, "0%")
#ifdef GUPPY_FF5M
  , print_acceleration(detail_cont, &limit_img, 100, "...")
#endif
  , elapsed(detail_cont, &clock_img, 100, ("0 "+ std::string(_("s"))).c_str())
#ifndef GUPPY_FF5M
  , time_left(detail_cont, &hourglass, 100, "...")
#endif
  , filament(detail_cont, &filament_img, 100, "...")
  , filament_m(0)
  , estimated_time_s(0)
  , filament_diameter(1.75) // XXX: check config
  , extruder_target(-1)
{
  lv_obj_move_background(status_cont);
  lv_obj_clear_flag(status_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(status_cont, LV_PCT(100), LV_PCT(100));

  static lv_coord_t grid_main_row_dsc_detail[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
    LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  static lv_coord_t grid_main_col_dsc_detail[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  lv_obj_set_grid_dsc_array(detail_cont, grid_main_col_dsc_detail, grid_main_row_dsc_detail);

  lv_obj_clear_flag(detail_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(detail_cont, LV_PCT(60), LV_PCT(60));

#ifdef GUPPY_FF5M
  // Each value fills its cell: room for the bigger digits.
  const lv_grid_align_t DETAIL_X = LV_GRID_ALIGN_STRETCH;
#else
  const lv_grid_align_t DETAIL_X = LV_GRID_ALIGN_START;
#endif
  //detail containter row 1
  lv_obj_set_grid_cell(extruder_temp.get_container(), DETAIL_X, 0, 1, LV_GRID_ALIGN_START, 0, 1);
  lv_obj_set_grid_cell(bed_temp.get_container(), DETAIL_X, 1, 1, LV_GRID_ALIGN_START, 0, 1);

  //detail containter row 2
  lv_obj_set_grid_cell(print_speed.get_container(), DETAIL_X, 0, 1, LV_GRID_ALIGN_START, 1, 1);
  lv_obj_set_grid_cell(z_offset.get_container(), DETAIL_X, 1, 1, LV_GRID_ALIGN_START, 1, 1);

  //detail containter row 3
  lv_obj_set_grid_cell(flow_rate.get_container(), DETAIL_X, 0, 1, LV_GRID_ALIGN_START, 2, 1);
  lv_obj_set_grid_cell(layers.get_container(), DETAIL_X, 1, 1, LV_GRID_ALIGN_START, 2, 1);

  //detail containter row 4
#ifdef GUPPY_FF5M
  lv_obj_set_grid_cell(print_acceleration.get_container(), DETAIL_X, 0, 1, LV_GRID_ALIGN_START, 3, 1);
#else
  lv_obj_set_grid_cell(elapsed.get_container(), DETAIL_X, 0, 1, LV_GRID_ALIGN_START, 3, 1);
#endif
  lv_obj_set_grid_cell(fan0.get_container(), DETAIL_X, 1, 1, LV_GRID_ALIGN_START, 3, 1);

  //detail containter row 5
#ifdef GUPPY_FF5M
  lv_obj_set_grid_cell(elapsed.get_container(), DETAIL_X, 0, 1, LV_GRID_ALIGN_START, 4, 1);
#else
  lv_obj_set_grid_cell(time_left.get_container(), DETAIL_X, 0, 1, LV_GRID_ALIGN_START, 4, 1);
#endif
  lv_obj_set_grid_cell(filament.get_container(), DETAIL_X, 1, 1, LV_GRID_ALIGN_START, 4, 1);
  // lv_obj_set_grid_cell(fan2.get_container(), LV_GRID_ALIGN_START, 1, 1, LV_GRID_ALIGN_START, 4, 1);
#ifdef GUPPY_FF5M
  // The values as big as Home's temperatures, right of their icons; one
  // longer than its cell (three fans at 100%) scrolls, as the file name does.
  for (ImageLabel *value : {&extruder_temp, &bed_temp, &print_speed, &z_offset, &flow_rate, &layers, &fan0,
                            &print_acceleration, &elapsed, &filament}) {
    lv_obj_t *box = value->get_container();
    lv_obj_t *label = value->get_label();
    // Рамка ImageLabel (2 px) в режиме None как в upstream, в полупрозрачном прозрачна.
    GuppyScreen::tile(box);
    // Ячейки во всю высоту строки, между ними 2 px (pad_row и pad_column ниже).
    lv_obj_set_style_pad_hor(box, 8, 0);  // ImageLabel: 5
    lv_obj_set_style_grid_cell_y_align(box, LV_GRID_ALIGN_STRETCH, 0);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(box, 6, 0);
    lv_obj_set_flex_grow(label, 1);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_font(label, SensorContainer::temperature_font(), 0);
  }
  lv_obj_set_style_pad_row(detail_cont, 2, 0);
  lv_obj_set_style_pad_column(detail_cont, 2, 0);
#endif

#ifdef GUPPY_FF5M
  // The progress bar on a row of its own, between them and the buttons.
  static lv_coord_t grid_main_row_dsc[] = {LV_GRID_FR(2), LV_GRID_CONTENT, LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  // The details wider: the bigger digits.
  static lv_coord_t grid_main_col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(2), LV_GRID_TEMPLATE_LAST};
#else
  static lv_coord_t grid_main_row_dsc[] = {LV_GRID_FR(2), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  static lv_coord_t grid_main_col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
#endif

  lv_obj_set_grid_dsc_array(status_cont, grid_main_col_dsc, grid_main_row_dsc);

  lv_obj_set_size(buttons_cont, LV_PCT(100), LV_PCT(40));
  lv_obj_clear_flag(buttons_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(buttons_cont, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(buttons_cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
#ifdef GUPPY_FF5M
  // Пять кнопок равномерно занимают всю ширину.
  lv_obj_set_style_pad_hor(buttons_cont, 0, 0);
  lv_obj_set_style_pad_column(buttons_cont, 0, 0);
  // 14 px lower than its cell's centre: away from the progress bar, still
  // on the screen (371..455 on the AD5M).
  lv_obj_set_style_translate_y(buttons_cont, 14, 0);
  for (ButtonContainer *b : {&limits_btn, &finetune_btn, &retract_btn, &pause_btn, &resume_btn, &cancel_btn}) {
    lv_obj_set_width(b->get_container(), LV_PCT(20));
  }
#endif

  lv_obj_set_style_pad_all(pbar_cont, 0, 0);
  lv_obj_set_size(pbar_cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);

  lv_obj_add_flag(thumbnail_cont, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(thumbnail_cont, &PrintStatusPanel::_handle_callback, LV_EVENT_SHORT_CLICKED, this);

  // lv_obj_set_style_border_width(pbar_cont, 2, 0);
  // lv_obj_set_style_border_width(thumbnail_cont, 2, 0);

#ifdef GUPPY_FF5M
  // 30%: the thumbnail is 192 px; the details' column takes the rest.
  auto side_width = (double)Nav::content_width() * 0.30;
#else
  // 35% of the content area beside the nav bar.
  auto side_width = (double)Nav::content_width() * 0.35;
#endif
  auto hscale = (double)lv_disp_get_physical_ver_res(NULL) / 480.0;

#ifdef GUPPY_FF5M
  // Across the screen, under the thumbnail and the details, twice as tall:
  // the ETA at its left, the progress in the middle, the end time at its
  // right, in the details' font.
  lv_obj_set_height(pbar_cont, 40 * hscale);
  lv_obj_set_size(progress_bar, LV_PCT(100), LV_PCT(100));
  lv_obj_clear_flag(progress_bar, LV_OBJ_FLAG_CLICKABLE);  // only shows
  progress_eta = lv_label_create(pbar_cont);
  lv_obj_move_to_index(progress_eta, 1);  // сразу за полосой: время окончания остаётся последним
  for (lv_obj_t *label : {progress_eta, progress_label, progress_end}) {
    lv_obj_set_style_text_font(label, SensorContainer::temperature_font(), 0);
  }
  show_time_left("...");
  lv_obj_align(progress_eta, LV_ALIGN_LEFT_MID, 10, 0);
#else
  lv_obj_set_size(progress_bar, side_width, 20 * hscale);
#endif
  lv_bar_set_value(progress_bar, 0, LV_ANIM_OFF);

  lv_label_set_text(progress_label, "0%");
  lv_obj_center(progress_label);
  lv_label_set_text(progress_end, "00:00");
  lv_obj_set_align(progress_bar, LV_ALIGN_CENTER);
#ifdef GUPPY_FF5M
  lv_obj_align(progress_end, LV_ALIGN_RIGHT_MID, -10, 0);
#else
  lv_obj_set_align(progress_end, LV_ALIGN_RIGHT_MID);
#endif

  lv_obj_set_flex_flow(thumbnail_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(thumbnail_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
  lv_obj_set_size(thumbnail_cont, side_width, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(thumbnail_cont, 0, 0);
  lv_obj_set_style_pad_row(thumbnail_cont, 20, 0);

  filename_label = lv_label_create(thumbnail_cont);
  lv_label_set_text(filename_label, "...");
  lv_obj_set_style_text_align(filename_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_width(filename_label, side_width);
  lv_label_set_long_mode(filename_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
#ifdef GUPPY_FF5M
  lv_obj_add_flag(filename_label, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(filename_label, &PrintStatusPanel::_handle_callback, LV_EVENT_SHORT_CLICKED, this);
#endif

  lv_obj_move_to_index(filename_label, 0);
#ifdef GUPPY_FF5M
  lv_obj_set_style_pad_row(thumbnail_cont, 8, 0);
  clock_label = lv_label_create(thumbnail_cont);
  lv_obj_move_to_index(clock_label, 0);
  lv_obj_set_style_text_font(clock_label, SensorContainer::temperature_font(), 0);
  lv_label_set_text(clock_label, "--:--:--");
  clock_timer = lv_timer_create([](lv_timer_t *timer) {
    auto *panel = static_cast<PrintStatusPanel *>(timer->user_data);
    auto *top = lv_obj_get_child(Nav::get().content(), -1);
    const bool print_visible = top == panel->status_cont;
    const bool home_clock_visible = !panel->running && top == Nav::get().root(Nav::HOME);
    if (!print_visible && !home_clock_visible) return;
    const auto now = std::time(nullptr);
    std::tm local;
    if (localtime_r(&now, &local)) {
      if (print_visible) {
        char time_text[9];
        if (std::strftime(time_text, sizeof(time_text), "%H:%M:%S", &local))
          KUtils::set_label_text(panel->clock_label, time_text);
      } else {
        panel->mini_print_status.update_clock(local.tm_hour, local.tm_min);
      }
    }
  }, 1000, this);
  set_clock_enabled(Config::get_instance()->get<bool>("/show_clock"));
#endif
  // Значения выводятся при уведомлении (см. consume); таймер выводит
  // оставшиеся, когда уведомления перестали приходить.
  values_timer = lv_timer_create([](lv_timer_t *timer) {
    auto *panel = static_cast<PrintStatusPanel *>(timer->user_data);
    if (lv_tick_elaps(panel->values_shown_at) >= 1000) panel->show_pending_values();
    panel->mini_print_status.show();
  }, 500, this);
  lv_obj_set_size(thumbnail, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
#ifdef GUPPY_FF5M
  // Laid out at its zoomed size (192 px), not the file's (often 300 px): else
  // the column outgrows the screen, the file name goes off the top and the
  // progress bar slides under the buttons.
  lv_img_set_size_mode(thumbnail, LV_IMG_SIZE_MODE_REAL);
  lv_obj_set_style_outline_color(thumbnail, lv_palette_main(LV_PALETTE_BLUE), 0);
  lv_obj_set_style_outline_opa(thumbnail, LV_OPA_COVER, 0);
#endif

  // row 1
  lv_obj_set_grid_cell(thumbnail_cont, LV_GRID_ALIGN_CENTER, 0, 1, LV_GRID_ALIGN_CENTER, 0, 1);
#ifdef GUPPY_FF5M
  lv_obj_set_grid_cell(detail_cont, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_CENTER, 0, 1);
#else
  lv_obj_set_grid_cell(detail_cont, LV_GRID_ALIGN_CENTER, 1, 1, LV_GRID_ALIGN_CENTER, 0, 1);
#endif

#ifdef GUPPY_FF5M
  lv_obj_set_grid_cell(pbar_cont, LV_GRID_ALIGN_STRETCH, 0, 2, LV_GRID_ALIGN_CENTER, 1, 1);
  lv_obj_set_grid_cell(buttons_cont, LV_GRID_ALIGN_CENTER, 0, 2, LV_GRID_ALIGN_CENTER, 2, 1);
#else
  //row 2
  lv_obj_set_grid_cell(buttons_cont, LV_GRID_ALIGN_CENTER, 0, 2, LV_GRID_ALIGN_CENTER, 1, 1);
#endif

#ifdef GUPPY_FF5M
  lv_obj_add_flag(print_acceleration.get_container(), LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(print_acceleration.get_container(), &PrintStatusPanel::_handle_callback, LV_EVENT_SHORT_CLICKED, this);
#endif
  lv_obj_add_flag(print_speed.get_container(), LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(print_speed.get_container(), &PrintStatusPanel::_handle_callback, LV_EVENT_SHORT_CLICKED, this);
  lv_obj_add_flag(z_offset.get_container(), LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(z_offset.get_container(), &PrintStatusPanel::_handle_callback, LV_EVENT_SHORT_CLICKED, this);
  lv_obj_add_flag(flow_rate.get_container(), LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(flow_rate.get_container(), &PrintStatusPanel::_handle_callback, LV_EVENT_SHORT_CLICKED, this);
  lv_obj_add_flag(extruder_temp.get_container(), LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(extruder_temp.get_container(), &PrintStatusPanel::_handle_callback, LV_EVENT_SHORT_CLICKED, this);
  lv_obj_add_flag(bed_temp.get_container(), LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(bed_temp.get_container(), &PrintStatusPanel::_handle_callback, LV_EVENT_SHORT_CLICKED, this);
  lv_obj_add_flag(fan0.get_container(), LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(fan0.get_container(), &PrintStatusPanel::_handle_callback, LV_EVENT_SHORT_CLICKED, this);

  update_pause_buttons(nullptr);
  ws.register_notify_update(this);
}

#ifdef GUPPY_FF5M
void PrintStatusPanel::set_clock_enabled(bool enabled) {
  mini_print_status.set_clock_enabled(enabled);
  if (enabled) {
    lv_obj_clear_flag(clock_label, LV_OBJ_FLAG_HIDDEN);
    lv_timer_resume(clock_timer);
    lv_timer_ready(clock_timer);
  } else {
    lv_timer_pause(clock_timer);
    lv_obj_add_flag(clock_label, LV_OBJ_FLAG_HIDDEN);
  }
}
#endif

PrintStatusPanel::~PrintStatusPanel() {
  if (resume_timer) lv_timer_del(resume_timer);
  lv_timer_del(values_timer);
#ifdef GUPPY_FF5M
  lv_timer_del(clock_timer);
#endif
  if (status_cont != NULL) {
    lv_obj_del(status_cont);
    status_cont = NULL;
  }

  ws.unregister_notify_update(this);
}

void PrintStatusPanel::set_idle_screen(lv_obj_t *screen) {
  idle_screen = screen;
}

// The Print tab shows this screen while printing or paused.
void PrintStatusPanel::show_status() {
  Nav::get().set_root(Nav::PRINT, status_cont);
#ifdef GUPPY_FF5M
  lv_timer_ready(clock_timer);
#endif
}

// Shows the Print tab: the print status while printing or paused, the
// file list otherwise.
void PrintStatusPanel::foreground() {
  if (running) {
    show_status();
  }
  Nav::get().activate(Nav::PRINT);
}

// No print: the Print tab goes back to the file list.
void PrintStatusPanel::background() {
  if (idle_screen != NULL) {
    Nav::get().set_root(Nav::PRINT, idle_screen);
  }
  exclude_panel.background();
}

void PrintStatusPanel::reset() {
  // Сначала изменившиеся значения, чтобы они потом не затёрли сброс.
  show_pending_values();
  file_loaded = false;
  ++metadata_generation;
  current_file = nullptr;
  lv_bar_set_value(progress_bar, 0, LV_ANIM_OFF);
  lv_label_set_text(progress_label, "0%");
  lv_label_set_text(progress_end, "00:00");
  print_speed.update_label(("0 "+ std::string(_("mm")) +"/"+ _("s")).c_str());
  flow_rate.update_label(("0.0 "+ std::string(_("mm")) +"3/"+ _("s")).c_str());
  elapsed.update_label(("0 "+ std::string(_("s"))).c_str());
  show_time_left("...");
  filament.update_label(("0 " + std::string(_("m"))).c_str());
  filament_m = 0;
  estimated_time_s = 0;
  layers.update_label(fmt::format("{} / {}", 0, 0).c_str());
  lv_label_set_text(filename_label, "...");

  auto v = State::get_instance()
    ->get_data("/printer_state/configfile/config/extruder/filament_diameter"_json_pointer);
  filament_diameter = v.is_null() ? 1.750 : std::atof(v.template get<std::string>().c_str());
  extruder_target = -1;

  // free src
#ifdef GUPPY_FF5M
  lv_obj_set_style_outline_width(thumbnail, 0, 0);
#endif
  KUtils::set_img_src(thumbnail, NULL);

  mini_print_status.reset();
  exclude_panel.reset();
}

void PrintStatusPanel::update_bed_temperature() {
  static const auto temperature_path = "/printer_state/heater_bed/temperature"_json_pointer;
  static const auto target_path = "/printer_state/heater_bed/target"_json_pointer;
  auto *state = State::get_instance();
  const auto temperature = state->get_data_copy(temperature_path);
  if (!temperature.is_number()) {
    bed_temp.update_label("...");
    return;
  }
  const auto target = state->get_data_copy(target_path);
  const int current = temperature.get<int>();
  const int desired = target.is_number() ? target.get<int>() : 0;
  bed_temp.update_label((desired > 0 ? fmt::format("{} / {}", current, desired)
                                     : fmt::format("{}", current)).c_str());
}

#ifdef GUPPY_FF5M
void PrintStatusPanel::update_acceleration(const json &value) {
  if (value.is_number()) {
    print_acceleration.update_label(fmt::format("{} {}/{}2", value.get<int>(), _("mm"), _("s")).c_str());
  } else {
    print_acceleration.update_label("...");
  }
}
#endif

void PrintStatusPanel::init(json &fans) {
  update_bed_temperature();
#ifdef GUPPY_FF5M
  update_acceleration(State::get_instance()->get_data_copy("/printer_state/toolhead/max_accel"_json_pointer));
#endif
  fan_speeds.clear();
  std::vector<std::string> values;
  for (auto &f : fans.items()) {
    std::string fan_name = f.key();

    auto fan_value = State::get_instance()
      ->get_data(json::json_pointer(fmt::format("/printer_state/{}/value", fan_name)));
    if (!fan_value.is_null()) {
      int v = static_cast<int>(fan_value.template get<double>() * 100);
      fan_speeds.insert({fan_name, v});
      values.push_back(fmt::format("{}%", v));
    }

    fan_value = State::get_instance()
      ->get_data(json::json_pointer(fmt::format("/printer_state/{}/speed", fan_name)));
    if (!fan_value.is_null()) {
      int v = static_cast<int>(fan_value.template get<double>() * 100);
      fan_speeds.insert({fan_name, v});
      values.push_back(fmt::format("{}%", v));
    }
  }

  //fan0.update_label(fmt::format("{}", fmt::join(values, ", ")).c_str());
  if (fan_speeds.count("fan_generic fanM106")) {
    fan0.update_label(fmt::format("{}%", fan_speeds["fan_generic fanM106"]).c_str());
  } else {
    fan0.update_label(fmt::format("{}", fmt::join(values, ", ")).c_str());
  }

  reset();
  populate();
  json &pstat_state = State::get_instance()
    ->get_data("/printer_state/print_stats/state"_json_pointer);
  if (!pstat_state.is_null()) {
    auto pstatus = pstat_state.template get<std::string>();
    running = pstatus == "printing" || pstatus == "paused";
    Nav::get().set_attention(Nav::PRINT, running);
    mini_print_status.set_printing(running);
    if (!running) {
      file_loaded = false;
      background();
    } else {
      foreground();  // connected during a print
    }
    mini_print_status.update_status(pstatus);
  } else {
#ifdef GUPPY_FF5M
    running = false;
    mini_print_status.set_printing(false);
#else
    mini_print_status.set_printing(true);
#endif
  }
#ifdef GUPPY_FF5M
  lv_timer_ready(clock_timer);
  if (running) {
    const auto &objects = State::get_instance()->get_data("/printer_state/exclude_object/objects"_json_pointer);
    lv_obj_set_style_outline_width(thumbnail, objects.is_array() && !objects.empty() ? 1 : 0, 0);
  }
#endif
  // Подпись сопла выводится при его изменениях: первый раз — из State.
  changed_fields["extruder"].insert("temperature");
  show_pending_values();
}

void PrintStatusPanel::populate() {
  State* s = State::get_instance();
  json& printfile = s->get_data("/printer_state/print_stats/filename"_json_pointer);
  if (!printfile.is_null()) {
    const std::string fname = printfile.template get<std::string>();
    if (fname.length() > 0) {
      file_loaded = true;
      lv_label_set_text(filename_label, fname.c_str());
      json fname_input = {{"filename", fname }};
      ws.send_jsonrpc("server.files.metadata", fname_input,
                      [fname, generation = metadata_generation, this](json &d) {
                        handle_metadata(fname, generation, d);
                      });

#ifndef GUPPY_FF5M
      mini_print_status.set_printing(true);
#endif
      exclude_panel.init();
    }
  }

  update_pause_buttons(s->get_data_copy("/printer_state/print_stats/state"_json_pointer));

  // progress percentage
  auto v = s->get_data("/printer_state/virtual_sdcard/progress"_json_pointer);
  if (!v.is_null()) {
    int new_value = static_cast<int>(v.template get<double>() * 100);
    lv_bar_set_value(progress_bar, new_value, LV_ANIM_ON);
    KUtils::set_label_text(progress_label, fmt::format("{}%", new_value).c_str());
    mini_print_status.update_progress(new_value);
    sync();
    std::ofstream("/proc/sys/vm/drop_caches") << "3";
  }

  v = s->get_data(
      "/printer_state/gcode_move/homing_origin/2"_json_pointer);
  if (!v.is_null()) {
    z_offset.update_label(fmt::format("{:.3f} " + std::string(_("mm")), v.template get<double>()).c_str());
  }
}

void PrintStatusPanel::handle_metadata(const std::string &gcode_file, uint32_t generation, json &j) {
  std::unique_lock<std::mutex> lock(lv_lock);
  if (generation != metadata_generation) {
    return;
  }

  auto eta = j["/result/estimated_time"_json_pointer];
  if (!eta.is_null()) {
    estimated_time_s = static_cast<uint32_t>(eta.template get<float>());
    spdlog::trace("updated eta {}", estimated_time_s);

    auto v = State::get_instance()->get_data_copy("/printer_state/print_stats/print_duration"_json_pointer);
    if (!v.is_null()) {
      uint32_t passed = static_cast<uint32_t>(v.template get<float>());
      spdlog::trace("updated time progress in handle metadata, passed {}", passed);

      update_time_progress(passed);
    }

    v = State::get_instance()->get_data_copy("/printer_state/print_stats/filament_used"_json_pointer);
    if (!v.is_null()) {
      float passed = v.template get<float>()/1000;
      spdlog::trace("updated filament progress in handle metadata, passed {:.1f}", passed);
      update_filament_progress(passed);
    }
  }

  current_file = j["/result"_json_pointer];

  auto width_scale = (double)lv_disp_get_physical_hor_res(NULL) / 800.0;
  // Миниатюра может скачиваться по HTTP. Проверяем задание ещё раз после загрузки.
  lock.unlock();
  auto thumb_detail = KUtils::get_thumbnail(gcode_file, j, width_scale);
  std::string fullpath = thumb_detail.first;
  if (fullpath.length() > 0) {
    spdlog::trace("thumb path: {}", fullpath);
    lock.lock();
    if (generation != metadata_generation) {
      return;
    }
    const std::string img_path = "A:" + fullpath;

    //auto screen_width = lv_disp_get_physical_hor_res(NULL);
    //uint32_t normalized_thumb_scale = ((0.34 * (double)screen_width) / (double)thumb_detail.second) * 256;
    //lv_img_set_zoom(thumbnail, normalized_thumb_scale);
    uint32_t target_zoom = (192 * 256) / thumb_detail.second;
    // The zoom first: setting the source sizes the image (FF5M lays it out
    // zoomed); setting the zoom later doesn't.
    lv_img_set_zoom(thumbnail, target_zoom);
    KUtils::set_img_src(thumbnail, img_path.c_str());
    mini_print_status.update_img(img_path, thumb_detail.second);
  }
}


void PrintStatusPanel::consume(json &j) {
  std::lock_guard<std::mutex> lock(lv_lock);

  auto printfile = j["/params/0/print_stats/filename"_json_pointer];
  if (!printfile.is_null()) {
    // filename change indicates a start of a print
    reset();
    populate();
    lv_label_set_text(filename_label, printfile.template get<std::string>().c_str());
  }

  auto& pstate = j["/params/0/print_stats/state"_json_pointer];
  if (!pstate.is_null()) {
    update_pause_buttons(pstate);
    auto print_status = pstate.template get<std::string>();
    bool was_running = running;
    running = print_status == "printing" || print_status == "paused";
    Nav::get().set_attention(Nav::PRINT, running);
    if (!running) {
      background();
      reset();
    } else {
      if (!file_loaded && printfile.is_null()) {
        reset();
        populate();
      }
      show_status();
      if (!was_running) {
        // A print starts, from here or elsewhere (a slicer, a macro, the
        // same file again). Pause and resume don't switch tabs: a screen
        // opened meanwhile (e.g. Extruder for a filament change) stays.
        Nav::get().activate(Nav::PRINT);
      }
    }

    mini_print_status.set_printing(running);
    mini_print_status.update_status(print_status);
#ifdef GUPPY_FF5M
    lv_timer_ready(clock_timer);
#endif
  }
#ifdef GUPPY_FF5M
  const auto &objects = j["/params/0/exclude_object/objects"_json_pointer];
  if (running && objects.is_array()) {
    lv_obj_set_style_outline_width(thumbnail, objects.empty() ? 0 : 1, 0);
  }
#endif

  // State уже принял это уведомление; здесь только отмечается, какие поля
  // изменились. Поля, которые искали через operator[] и не нашли, равны null.
  const auto &params = j["/params/0"_json_pointer];
  if (params.is_object()) {
    for (auto it = params.begin(); it != params.end(); ++it) {
      if (!it->is_object()) continue;
      for (auto f = it->begin(); f != it->end(); ++f)
        if (!f->is_null()) changed_fields[it.key()].insert(f.key());
    }
  }
  // Скорость, поток и время меняются в каждом уведомлении (4 раза в секунду):
  // значения выводятся не чаще 2 раз в секунду, в той же перерисовке, что и
  // остальные панели по этому уведомлению.
  if (lv_tick_elaps(values_shown_at) >= 500) show_pending_values();
}

// Выводит последние значения полей, изменившихся с прошлого вызова.
void PrintStatusPanel::show_pending_values() {
  if (changed_fields.empty()) return;
  json params = json::object();
  auto *state = State::get_instance();
  for (const auto &[name, fields] : changed_fields)
    for (const auto &f : fields)
      params[name][f] = state->get_data_copy(json::json_pointer("/printer_state") / name / f);
  changed_fields.clear();
  json j = {{"params", json::array({std::move(params)})}};
  values_shown_at = lv_tick_get();
  show_values(j);
  mini_print_status.show();
}

void PrintStatusPanel::show_values(json &j) {
  // Подписи сопла и вентиляторов — только если их объекты изменились.
  const json &params = j["/params/0"_json_pointer];
  bool extruder_changed = params.contains("zmod_color");
  bool fans_changed = false;
  for (auto it = params.begin(); it != params.end(); ++it) {
    extruder_changed = extruder_changed || it.key().rfind("extruder", 0) == 0;
    fans_changed = fans_changed || fan_speeds.count(it.key());
  }

  json v;
  if (extruder_changed) {
    v = j["/params/0/extruder/target"_json_pointer];
    if (!v.is_null()) {
      extruder_target = v.template get<int>();
    }

    int active_tool_id = 0;
    auto zmod_active = j["/params/0/zmod_color/active_tool_id"_json_pointer];
    if (!zmod_active.is_null()) {
      active_tool_id = zmod_active.template get<int>();
    } else {
      auto v_active = State::get_instance()->get_data_copy("/printer_state/zmod_color/active_tool_id"_json_pointer);
      if (!v_active.is_null()) {
        active_tool_id = v_active.template get<int>();
      }
    }
    int effective_id = (active_tool_id >= 0) ? active_tool_id : 0;
    std::string tool_prefix = "T" + std::to_string(effective_id);
    std::string ext_name = "extruder" + (effective_id > 0 ? std::to_string(effective_id) : "");
    // AD5X channels T1-T3 share extruder; real multi-nozzle printers have
    // their own extruderN object, even before its temperature is reported.
    if (effective_id > 0) {
      auto extruders = State::get_instance()->get_extruders();
      if (std::find(extruders.begin(), extruders.end(), ext_name) == extruders.end()) {
        ext_name = "extruder";
      }
    }

    auto v_target = j[json::json_pointer("/params/0/" + ext_name + "/target")];
    if (!v_target.is_null()) {
      extruder_target = v_target.template get<int>();
    } else {
      auto st_target = State::get_instance()->get_data_copy(json::json_pointer("/printer_state/" + ext_name + "/target"));
      if (!st_target.is_null()) {
        extruder_target = st_target.template get<int>();
      }
    }

    v = j[json::json_pointer("/params/0/" + ext_name + "/temperature")];
    if (v.is_null()) {
      v = State::get_instance()->get_data_copy(json::json_pointer("/printer_state/" + ext_name + "/temperature"));
    }
    if (!v.is_null()) {
      extruder_temp.update_label(extruder_target > 0 ? fmt::format("{}: {} / {}", tool_prefix, v.template get<int>(), extruder_target).c_str() : fmt::format("{}: {}", tool_prefix, v.template get<int>()).c_str());
    }
  }

  static const auto bed_target_path = "/params/0/heater_bed/target"_json_pointer;
  static const auto bed_temperature_path = "/params/0/heater_bed/temperature"_json_pointer;
  if (!j[bed_target_path].is_null() || !j[bed_temperature_path].is_null()) {
    update_bed_temperature();
  }

#ifdef GUPPY_FF5M
  static const auto acceleration_path = "/params/0/toolhead/max_accel"_json_pointer;
  if (j.contains(acceleration_path)) {
    update_acceleration(j.at(acceleration_path));
  }
#endif

  // speed
  auto speed = j["/params/0/motion_report/live_velocity"_json_pointer];
  if (!speed.is_null()) {
    int s = static_cast<int>(speed.template get<double>());
    print_speed.update_label((std::to_string(s) + " "+ std::string(_("mm")) +"/"+ _("s")).c_str());
  }

  // zoffset
  v = j["/params/0/gcode_move/homing_origin/2"_json_pointer];
  if (!v.is_null()) {
    z_offset.update_label(fmt::format("{:.3f} " + std::string(_("mm")), v.template get<double>()).c_str());
  }

  if (fans_changed) {
    std::vector<std::string> values;
    for (auto &f : fan_speeds) {
      std::string fan_name = f.first;

      int fv = f.second;
      auto fan_value = j[json::json_pointer(fmt::format("/params/0/{}/value", fan_name))];
      if (!fan_value.is_null()) {
        fv = static_cast<int>(fan_value.template get<double>() * 100);
        f.second = fv;
      }

      fan_value = j[json::json_pointer(fmt::format("/params/0/{}/speed", fan_name))];
      if (!fan_value.is_null()) {
        fv = static_cast<int>(fan_value.template get<double>() * 100);
        f.second = fv;
      }
      values.push_back(fmt::format("{}%", fv));
    }

    //fan0.update_label(fmt::format("{}", fmt::join(values, ", ")).c_str());
    if (fan_speeds.count("fan_generic fanM106")) {
      fan0.update_label(fmt::format("{}%", fan_speeds["fan_generic fanM106"]).c_str());
    } else {
      fan0.update_label(fmt::format("{}", fmt::join(values, ", ")).c_str());
    }
  }

  // progress
  v = j["/params/0/print_stats/print_duration"_json_pointer];
  if (!v.is_null()) {
    uint32_t passed = static_cast<uint32_t>(v.template get<float>());
    update_time_progress(passed);
  }

  v = j["/params/0/print_stats/filament_used"_json_pointer];
  if (!v.is_null()) {
    float passed = v.template get<float>()/1000;
    update_filament_progress(passed);
  }

  // progress percentage
  v = j["/params/0/virtual_sdcard/progress"_json_pointer];
  if (!v.is_null()) {
    int new_value = static_cast<int>(v.template get<double>() * 100);
    lv_bar_set_value(progress_bar, new_value, LV_ANIM_ON);
    KUtils::set_label_text(progress_label, fmt::format("{}%", new_value).c_str());
    mini_print_status.update_progress(new_value);
  }

  v = j["/params/0/motion_report/live_extruder_velocity"_json_pointer];
  if (!v.is_null()) {
    double flow = pi() / 4 * std::pow(filament_diameter, 2) * v.template get<double>();
    flow_rate.update_label(fmt::format("{:.1f} "+ std::string(_("mm")) +"3/"+ _("s"), flow > 0.0 ? flow : 0.0).c_str());
  }

  // layers
  v = j["/params/0/print_stats/info"_json_pointer];
  update_layers(v);
}

void PrintStatusPanel::update_pause_buttons(const json &state) {
  if (state == "paused") {
    lv_obj_clear_flag(resume_btn.get_container(), LV_OBJ_FLAG_HIDDEN);
  } else {
    if (resume_timer) {
      lv_timer_del(resume_timer);
      resume_timer = nullptr;
      resume_btn.enable();
    }
    resume_btn.hide();
  }
  if (state == "printing") {
    lv_obj_clear_flag(pause_btn.get_container(), LV_OBJ_FLAG_HIDDEN);
  } else {
    pause_btn.hide();
  }
}

void PrintStatusPanel::handle_callback(lv_event_t *event) {
  lv_obj_t *btn = lv_event_get_current_target(event);
  if (btn == pause_btn.get_container()) {
    ws.send_jsonrpc("printer.print.pause");
  } else if (btn == thumbnail_cont) {
    exclude_panel.foreground();
  } else if (btn == resume_btn.get_container()) {
    if (resume_timer || lv_obj_has_flag(btn, LV_OBJ_FLAG_HIDDEN)) return;
    resume_timer = lv_timer_create([](lv_timer_t *timer) {
      auto *panel = static_cast<PrintStatusPanel *>(timer->user_data);
      panel->resume_timer = nullptr;
      panel->resume_btn.enable();
    }, 20000, this);
    if (!resume_timer) return;
    lv_timer_set_repeat_count(resume_timer, 1);
    resume_btn.disable();
    ws.send_jsonrpc("printer.print.resume");
  } else if (btn == cancel_btn.get_container()) {
    // The Print tab goes back to the file list when Klipper reports it.
    ws.send_jsonrpc("printer.print.cancel");
  } else if (btn == retract_btn.get_container()) {
    retract_panel.foreground();
#ifdef GUPPY_FF5M
  } else if (btn == filename_label) {
    // Список файлов открывается по нажатию на имя печатаемого файла.
    if (idle_screen != NULL) lv_obj_move_foreground(idle_screen);
  } else if (btn == limits_btn.get_container() || btn == print_acceleration.get_container()) {
    main_panel_ref.get_tune_panel().show_limits();
#endif
  } else if (btn == mini_print_status.get_container()) {
#ifdef GUPPY_FF5M
    if (running) foreground();
#else
    foreground();
#endif
  } else if ((btn == finetune_btn.get_container()) or
             (btn == print_speed.get_container()) or
             (btn == z_offset.get_container()) or
             (btn == flow_rate.get_container())) {
    finetune_panel.foreground();
  } else if (btn == fan0.get_container()) {
    main_panel_ref.handle_fanpanel_cb(event);
  } else if ((btn == extruder_temp.get_container()) or
             (btn == bed_temp.get_container())) {
    main_panel_ref.foreground();
  }
}

void PrintStatusPanel::show_time_left(const std::string &eta) {
#ifdef GUPPY_FF5M
  KUtils::set_label_text(progress_eta, fmt::format(_("ETA: {}"), eta).c_str());
#else
  time_left.update_label(eta.c_str());
#endif
}

void PrintStatusPanel::update_time_progress(uint32_t time_passed) {
    if (time_passed > estimated_time_s) {
      if (std::strcmp(lv_label_get_text(progress_end), "...") != 0) {
        lv_label_set_text_static(progress_end, "...");
        show_time_left("...");
        mini_print_status.update_eta("...");
      }
    } else {
      const uint32_t remaining = estimated_time_s - time_passed;
      auto eta_str = KUtils::eta_string(remaining);
      auto end_str = KUtils::end_time(remaining);
      auto str = eta_str + " / " + end_str;
      KUtils::set_label_text(progress_end, end_str.c_str());
      show_time_left(eta_str);
      mini_print_status.update_eta(str);
    }

    elapsed.update_label(KUtils::eta_string(time_passed).c_str());
}

void PrintStatusPanel::update_layers(json &info) {
    layers.update_label(fmt::format("{} / {}", current_layer(info), max_layer(info)).c_str());
}

int PrintStatusPanel::max_layer(json &info) {
  if (!info.is_null()) {
    auto v = info["/total_layer"_json_pointer];
    if (!v.is_null()) {
      return v.template get<int>();
    }
  }

  if (!current_file.is_null()) {
    auto v = current_file["/layer_count"_json_pointer];
    if (!v.is_null()) {
      return v.template get<int>();
    } else {
      auto first_layer_height = current_file["/first_layer_height"_json_pointer];
      auto layer_height = current_file["/layer_height"_json_pointer];
      auto object_height = current_file["/object_height"_json_pointer];

      if (!first_layer_height.is_null() && !layer_height.is_null() && !object_height.is_null()) {
        auto layer = static_cast<int>(std::ceil((object_height.template get<double>() - first_layer_height.template get<double>()) / layer_height.template get<double>() + 1));
        return layer > 0 ? layer : 0;
      }
    }
  }
  return 0;
}

int PrintStatusPanel::current_layer(json &info) {
  if (!info.is_null()) {
    auto v = info["/current_layer"_json_pointer];
    if (!v.is_null()) {
      return v.template get<int>();
    }
  }

  if (!current_file.is_null()) {
    State *s = State::get_instance();
    auto pd = s->get_data_copy("/printer_state/print_stats/print_duration"_json_pointer);
    auto zpos = s->get_data_copy("/printer_state/gcode_move/gcode_position/2"_json_pointer);

    auto first_layer_height = current_file["/first_layer_height"_json_pointer];
    auto layer_height = current_file["/layer_height"_json_pointer];

    if (!pd.is_null()
        && pd.template get<int>() > 0
        && !zpos.is_null()
        && !first_layer_height.is_null()
        && !layer_height.is_null()) {
      auto layer = static_cast<int>(std::ceil((zpos.template get<double>() - first_layer_height.template get<double>()) / layer_height.template get<double>() + 1));
      auto total = max_layer(info);
      if (layer > total) {
        return total;
      }

      if (layer > 0) {
        return layer;
      }
    }
  }

  return 0;
}

void PrintStatusPanel::update_filament_progress(float value) {
  filament.update_label(fmt::format("{:.2f} {}", value, _("m")).c_str());
    if (value>filament_m+(float)0.1) {
      filament_m=value;
      spdlog::trace("drop_caches");
      sync();
      std::ofstream("/proc/sys/vm/drop_caches") << "3";
    }
}

FineTunePanel &PrintStatusPanel::get_finetune_panel() {
  return finetune_panel;
}

RetractPanel &PrintStatusPanel::get_retract_panel() {
  return retract_panel;
}

ExcludePanel &PrintStatusPanel::get_exclude_panel() {
  return exclude_panel;
}

PidPanel &PrintStatusPanel::get_pid_panel() {
  return pid_panel;
}
