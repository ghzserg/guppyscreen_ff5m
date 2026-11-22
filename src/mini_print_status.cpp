#include "guppyscreen.h"
#include "utils.h"
#include "mini_print_status.h"
#ifdef GUPPY_FF5M
#include "nav.h"
#include "sensor_container.h"
#endif
#include "spdlog/spdlog.h"

#ifdef GUPPY_FF5M
static const lv_coord_t THUMB_PX = 76;  // the thumbnail's width on Home
static const lv_coord_t BAR_PX = SensorContainer::stripe_width();  // the progress bar
#endif

MiniPrintStatus::MiniPrintStatus(lv_obj_t *parent,
                                 lv_event_cb_t cb,
                                 void* user_data,
                                KWebSocketClient &c)
  : cont(lv_obj_create(parent))
#ifdef GUPPY_FF5M
  , progress_bar(lv_bar_create(cont))
#else
  , progress_bar(lv_arc_create(cont))
#endif
  , thumb(lv_img_create(cont))
  , status_label(lv_label_create(cont))
#ifdef GUPPY_FF5M
  , clock_label(lv_label_create(cont))
#endif
  , status("-/-")
  , eta("...")
  , ws(c)
{
  lv_color_t cur_bg = lv_obj_get_style_bg_color(cont, 0);
  lv_color_t mixed = lv_color_mix(lv_palette_main(LV_PALETTE_GREY),
                                  cur_bg, LV_OPA_10);

  lv_obj_set_style_bg_color(cont, mixed, 0);
  lv_obj_set_style_bg_opa(cont, LV_OPA_COVER, 0);
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  auto scale = (double)lv_disp_get_physical_hor_res(NULL) / 800.0;

  lv_obj_set_size(cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_top(cont, 0, 0);
  lv_obj_set_style_pad_bottom(cont, 0, 0);

  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW);

  lv_obj_set_style_border_width(cont, 2, 0);
  lv_obj_set_style_radius(cont, 4, 0);
  lv_obj_set_size(cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_add_flag(cont, LV_OBJ_FLAG_FLOATING);

  lv_obj_add_flag(cont, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(cont, cb, LV_EVENT_SHORT_CLICKED, user_data);

  lv_label_set_text(status_label, fmt::format(_("ETA: {}\nStatus: {}") /* "Ост: {}\nСтатус: {}" */, eta, status).c_str());

#ifdef GUPPY_FF5M
  // Home: twice as tall as with the ring, the thumbnail filling the height,
  // the progress a bar along the bottom as thick as the sensor tiles' stripes.
  // Высотой с кнопку панели навигации, вровень с вкладкой Print; миниатюра
  // по центру между верхом и полосой.
  const lv_coord_t content_px = THUMB_PX + BAR_PX + 2 * 2 + 7;  // + border, gap
  lv_obj_set_height(cont, LV_MAX(Nav::slot_height(), content_px));
  lv_obj_set_style_pad_top(cont, LV_MAX(Nav::slot_height() - content_px, 0) / 2, 0);
  lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_column(cont, 10, 0);
  lv_obj_add_flag(progress_bar, LV_OBJ_FLAG_FLOATING);  // out of the row
  lv_obj_clear_flag(progress_bar, LV_OBJ_FLAG_CLICKABLE);  // нажатие на полосу тоже открывает экран печати
  lv_obj_set_height(progress_bar, BAR_PX);
  lv_obj_set_width(progress_bar, LV_PCT(100));
  lv_obj_set_style_radius(progress_bar, 0, LV_PART_MAIN | LV_PART_INDICATOR);
  lv_obj_align(progress_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_pad_top(status_label, (THUMB_PX - 42) / 2, 0);  // two lines, centred by the thumbnail
  // Home gives the status a fixed width: the text takes what the thumbnail
  // leaves and is cut there.
  lv_obj_set_flex_grow(status_label, 1);
  lv_label_set_long_mode(status_label, LV_LABEL_LONG_CLIP);
  lv_obj_add_flag(clock_label, LV_OBJ_FLAG_FLOATING);
  lv_obj_set_style_text_font(clock_label, &lv_font_montserrat_48, 0);
  lv_label_set_text_static(clock_label, "--:--:--");
  lv_obj_center(clock_label);
#else
  lv_arc_set_rotation(progress_bar, 270);
  lv_obj_set_size(progress_bar, 40 * scale, 40 * scale);
  lv_obj_set_style_arc_width(progress_bar, 10 * scale, LV_PART_MAIN);
  lv_obj_set_style_arc_width(progress_bar, 10 * scale, LV_PART_INDICATOR);
  lv_arc_set_bg_angles(progress_bar, 0, 360);
  lv_obj_remove_style(progress_bar, NULL, LV_PART_KNOB);
  lv_obj_clear_flag(progress_bar, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_center(progress_bar);
#endif

  lv_img_set_size_mode(thumb, LV_IMG_SIZE_MODE_REAL);
#ifndef GUPPY_FF5M
  lv_obj_set_size(cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
#endif
//  lv_obj_align(cont, LV_ALIGN_TOP_RIGHT, 30, -14 * scale);
  lv_obj_align(cont, LV_ALIGN_TOP_RIGHT, 0, 0);
  set_printing(false);
}

MiniPrintStatus::~MiniPrintStatus() {
  if (cont != NULL) {
    lv_obj_del(cont);
    cont = NULL;
  }
}


void MiniPrintStatus::set_printing(bool printing) {
#ifdef GUPPY_FF5M
  if (printing) {
    lv_obj_add_flag(clock_label, LV_OBJ_FLAG_HIDDEN);
    for (auto *obj : {thumb, progress_bar, status_label}) lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(cont, LV_OBJ_FLAG_CLICKABLE);
  } else {
    for (auto *obj : {thumb, progress_bar, status_label}) lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(clock_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_CLICKABLE);
  }
#else
  if (printing) {
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(cont);
  } else {
    lv_obj_add_flag(cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_background(cont);
  }
#endif
}

#ifdef GUPPY_FF5M
void MiniPrintStatus::update_clock(const char *time_text) {
  KUtils::set_label_text(clock_label, time_text);
}
#endif

lv_obj_t *MiniPrintStatus::get_container() {
  return cont;
}

void MiniPrintStatus::update_eta(const std::string &eta_str) {
  eta = eta_str;
  KUtils::set_label_text(status_label, fmt::format(_("ETA: {}\nStatus: {}") /* "Ост: {}\nСтатус: {}" */, eta, status).c_str());
}

void MiniPrintStatus::update_status(std::string &status_str) {
  status = status_str;
  lv_label_set_text(status_label, fmt::format(_("ETA: {}\nStatus: {}") /* "Ост: {}\nСтатус: {}" */, eta, status).c_str());
  GuppyScreen *guppy = GuppyScreen::get();
  guppy->up(true);
}

void MiniPrintStatus::update_progress(int p) {
#ifdef GUPPY_FF5M
  lv_bar_set_value(progress_bar, p, LV_ANIM_OFF);
#else
  lv_arc_set_value(progress_bar, p);
#endif
}

void MiniPrintStatus::update_img(const std::string &img_path, size_t twidth) {
#ifdef GUPPY_FF5M
  uint32_t normalized_thumb_scale = THUMB_PX * 256 / twidth;
#else
  auto screen_width = lv_disp_get_physical_hor_res(NULL);
  uint32_t normalized_thumb_scale = ((0.05 * (double)screen_width) / (double)twidth) * 256;
#endif
  lv_img_set_zoom(thumb, normalized_thumb_scale);
  KUtils::set_img_src(thumb, img_path.c_str());
}

void MiniPrintStatus::reset() {
  update_progress(0);

  // free src
  KUtils::set_img_src(thumb, NULL);

  eta = "...";
  status = "-/-";
  lv_label_set_text(status_label, fmt::format(_("ETA: {}\nStatus: {}") /* "Ост: {}\nСтатус: {}" */, eta, status).c_str());
}

