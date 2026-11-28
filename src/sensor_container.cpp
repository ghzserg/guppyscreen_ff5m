#include "sensor_container.h"
#include "spdlog/spdlog.h"
#include "config.h"
#include "state.h"

#include <algorithm>

#include <set>
#include <string>

LV_FONT_DECLARE(notosemi_22_digits);
LV_FONT_DECLARE(notosemi_16);

static constexpr lv_coord_t chart_label_gap = 4;

// Датчики с точками, ещё не добавленными в график.
static std::set<SensorContainer *> with_pending;

SensorContainer::SensorContainer(KWebSocketClient &c,
                                 lv_obj_t *parent,
                                 const void *img,
                                 const char *text,
                                 lv_color_t color,
                                 bool can_edit,
                                 bool show_target,
                                 Numpad &np,
                                 std::string name,
                                 lv_obj_t *chart_chart,
                                 lv_chart_series_t *chart_series,
                                 bool not_set)
  : ws(c)
  , sensor_cont(lv_obj_create(parent))
  , sensor_img(lv_img_create(sensor_cont))
  , sensor_label(lv_label_create(sensor_cont))
  , value_label(lv_label_create(sensor_cont))
  , value(0)
  , divider_label(lv_label_create(sensor_cont))
  , target_label(lv_label_create(sensor_cont))
  , target(-1)
  , numpad(np)
  , id(name)
  , chart(chart_chart)
  , series(chart_series)
  , last_updated_ts(std::time(nullptr))
{
    lv_obj_clear_flag(sensor_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_color(sensor_cont, color, LV_PART_MAIN);
    lv_obj_set_style_border_side(sensor_cont, LV_BORDER_SIDE_LEFT, LV_PART_MAIN);
    lv_obj_set_style_border_width(sensor_cont, stripe_width(), LV_PART_MAIN);

    // auto cont_width = (double)lv_disp_get_physical_hor_res(NULL) * 0.4125;
    // cont_width = cont_width > 330 ? 330 : cont_width;
    // auto cont_height = (double)lv_disp_get_physical_ver_res(NULL) * 0.125;
    // cont_height = cont_height > 60 ? 60 : cont_height;

    auto width_scale = (double)lv_disp_get_physical_hor_res(NULL) / 800.0;
    auto height_scale = (double)lv_disp_get_physical_ver_res(NULL) / 480.0;
    lv_obj_set_size(sensor_cont, 330 * width_scale, 60 * height_scale);
    lv_obj_set_style_pad_all(sensor_cont, 0, 0);

    lv_img_set_src(sensor_img, img);
    lv_obj_align(sensor_img, LV_ALIGN_LEFT_MID, 0, 0);

    lv_label_set_text(sensor_label, text);
    lv_obj_align_to(sensor_label, sensor_img, LV_ALIGN_OUT_RIGHT_MID, -7 * width_scale, 0);

    lv_label_set_text(value_label, "0");
    lv_obj_set_width(value_label, 50 * width_scale);
    lv_obj_align(value_label, LV_ALIGN_RIGHT_MID, -75 * width_scale, 0);
    lv_obj_set_style_pad_all(value_label, 8 * width_scale, 0);

    lv_label_set_text(divider_label, "/");
    lv_obj_set_width(divider_label, 50 * width_scale);
    lv_obj_align(divider_label, LV_ALIGN_RIGHT_MID, -32 * width_scale, 0);
    lv_obj_set_style_pad_all(divider_label, 8 * width_scale, 0);

    if (show_target || can_edit) {
      lv_label_set_text(target_label, "0");
      lv_obj_set_width(target_label, 60 * width_scale);
      lv_obj_align(target_label, LV_ALIGN_RIGHT_MID, 0, 0);
      lv_obj_set_style_pad_all(target_label, 8 * width_scale, 0);
    } else {
      lv_obj_add_flag(target_label, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(divider_label, LV_OBJ_FLAG_HIDDEN);
    }

    if (can_edit) {
      lv_obj_set_style_border_width(target_label, 2, LV_PART_MAIN);
      lv_obj_set_style_radius(target_label, 6, LV_PART_MAIN);
      lv_obj_set_style_border_color(target_label, lv_palette_darken(LV_PALETTE_GREY, 1), LV_PART_MAIN);

      spdlog::debug("sensor cb registered name {}, cont {}, this {}, np {}",
                    id, fmt::ptr(sensor_cont), fmt::ptr(this), fmt::ptr(&np));
      if (not_set)
          lv_obj_add_event_cb(sensor_cont, &SensorContainer::_handle_edit2, LV_EVENT_SHORT_CLICKED, this);
      else
          lv_obj_add_event_cb(sensor_cont, &SensorContainer::_handle_edit, LV_EVENT_SHORT_CLICKED, this);
    }
}

SensorContainer::SensorContainer(KWebSocketClient &c,
                                 lv_obj_t *parent,
                                 const void *img,
                                 uint16_t img_scale,
                                 const char *text,
                                 lv_color_t color,
                                 bool can_edit,
                                 bool show_target,
                                 Numpad &np,
                                 std::string name,
                                 lv_obj_t *chart,
                                 lv_chart_series_t *chart_series)
  : SensorContainer(c, parent, img, text, color, can_edit, show_target, np, name, chart, chart_series, false)
{
  lv_img_set_zoom(sensor_img, img_scale);
}

SensorContainer::SensorContainer(KWebSocketClient &c,
                                 lv_obj_t *parent,
                                 const void *img,
                                 uint16_t img_scale,
                                 const char *text,
                                 lv_color_t color,
                                 bool can_edit,
                                 bool show_target,
                                 Numpad &np,
                                 std::string name,
                                 lv_obj_t *chart,
                                 lv_chart_series_t *chart_series,
                                 bool not_set)
  : SensorContainer(c, parent, img, text, color, can_edit, show_target, np, name, chart, chart_series, not_set)
{
  lv_img_set_zoom(sensor_img, img_scale);
}

SensorContainer::SensorContainer(KWebSocketClient &c,
                                 lv_obj_t *parent,
                                 const void *img,
                                 const char *text,
                                 lv_color_t color,
                                 bool can_edit,
                                 bool show_target,
                                 Numpad &np,
                                 std::string name,
                                 lv_obj_t *chart,
                                 lv_chart_series_t *chart_series)
  : SensorContainer(c, parent, img, text, color, can_edit, show_target, np, name, chart, chart_series, false)
{

}

SensorContainer::~SensorContainer() {
  numpad.forget(this);
  with_pending.erase(this);
  if (sensor_cont != NULL) {
    spdlog::debug("deleting sensor {}", id);
    lv_obj_del(sensor_cont);
    sensor_cont = NULL;
  }

  if (series != NULL && chart != NULL) {
    lv_chart_remove_series(chart, series);
    series = NULL;
  }
}

lv_obj_t *SensorContainer::get_sensor() {
  return sensor_cont;
}

void SensorContainer::update_target(int new_target) {
#ifdef GUPPY_FF5M
  if (tile && new_target == target) return;  // no text and colour restyle
#endif
  if (new_target >= 0) {
    target = new_target;
    lv_label_set_text(target_label, fmt::format("{}", new_target).c_str());
#ifdef GUPPY_FF5M
    if (tile) {
      lv_obj_set_style_text_color(target_label,
          lv_palette_main(new_target > 0 ? LV_PALETTE_RED : LV_PALETTE_GREY), 0);
    }
#endif
  }
}

void SensorContainer::update_value(int new_value) {
  if (value != new_value) {
    value = new_value;
    lv_label_set_text(value_label, fmt::format("{}", new_value).c_str());
  }
}

#ifdef GUPPY_FF5M
const lv_font_t *SensorContainer::temperature_font() {
  // В сгенерированном шрифте под базовой линией есть место для выносных
  // элементов, а над цифрами нет: текст, отцентрованный по своему блоку
  // (значение в рамке, число на кнопке), стоит выше центра. У этой копии
  // столько же места над цифрами, и строка симметрична относительно них.
  // Буква, которой в шрифте нет (новый перевод), берётся из шрифта темы.
  static const lv_font_t font = [] {
    lv_font_t f = notosemi_22_digits;
    f.line_height += f.base_line;
    f.fallback = &notosemi_20;
    return f;
  }();
  return &font;
}

lv_coord_t SensorContainer::tile_width() {
  return 84 * ((double)lv_disp_get_physical_hor_res(NULL) / 800.0);
}

void SensorContainer::style_chart(lv_obj_t *chart, int32_t max_temp) {
  lv_obj_set_style_line_width(chart, 1, 0);
  lv_obj_set_style_text_font(chart, &notosemi_20, LV_PART_TICKS);
  lv_obj_set_style_text_letter_space(chart, 0, LV_PART_TICKS);
  // Шкала внутри графика, справа от температурных кривых.
  const lv_coord_t text_px = lv_txt_get_width("300", 3, &notosemi_20, 0, LV_TEXT_FLAG_NONE);
  lv_obj_set_style_pad_right(chart, text_px + 2 * chart_label_gap, 0);
  lv_chart_set_div_line_count(chart, 0, 0);
  lv_chart_set_axis_tick(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 0, 0, 0, false, 0);
  lv_chart_set_axis_tick(chart, LV_CHART_AXIS_SECONDARY_Y, 0, 0, 0, 0, false, 0);
  set_chart_max(chart, max_temp);
  // Фон над областью графика выше на половину строки (верхняя цифра у края
  // целиком на фоне), область графика прежняя.
  const lv_coord_t half_line = lv_font_get_line_height(&notosemi_20) / 2 + 1;
  lv_obj_set_width(chart, 4 * tile_width() + 3 * tile_gap());
  lv_obj_update_layout(chart);  // высота задана в процентах: узнать её в пикселях
  lv_obj_set_height(chart, lv_obj_get_height(chart) + half_line - lv_obj_get_style_pad_top(chart, 0));
  lv_obj_set_style_pad_top(chart, half_line, 0);
  lv_obj_set_grid_cell(chart, LV_GRID_ALIGN_START, 0, 2, LV_GRID_ALIGN_END, 2, 1);
  // График растянут по X (zoom) и прокручен к последним точкам: без этого
  // LVGL рисует полосу прокрутки штрихом у нижней цифры шкалы.
  lv_obj_set_scrollbar_mode(chart, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_event_cb(chart, [](lv_event_t *e) {
    lv_obj_draw_part_dsc_t *dsc = lv_event_get_draw_part_dsc(e);
    // Рисуем сетку и шкалу один раз до температурных кривых, без скрытых делений.
    if (!lv_obj_draw_part_check_type(dsc, &lv_chart_class, LV_CHART_DRAW_PART_DIV_LINE_INIT)) return;
    lv_obj_t *obj = lv_event_get_current_target(e);
    const int32_t top = ((const lv_chart_t *)obj)->ymax[0];
    lv_area_t plot;
    lv_obj_get_content_coords(obj, &plot);
    const int32_t h = lv_area_get_height(&plot);
    const lv_coord_t y0 = plot.y1 - lv_obj_get_scroll_top(obj);
    if (h <= 0 || top <= 200) return;

    dsc->line_dsc->color = lv_obj_get_style_text_color(obj, LV_PART_TICKS);
    dsc->line_dsc->dash_width = 4;
    dsc->line_dsc->dash_gap = 4;
    for (int32_t deg = 100; deg < top; deg += 100) {
      const lv_coord_t y = y0 + h - h * deg / top;
      const lv_point_t p1 = {plot.x1, y}, p2 = {plot.x2, y};
      lv_draw_line(dsc->draw_ctx, dsc->line_dsc, &p1, &p2);
    }

    lv_area_t area;
    lv_obj_get_coords(obj, &area);
    area.x1 = plot.x2 + 1;
    area.x2 -= chart_label_gap;
    if (dsc->draw_ctx->clip_area->x2 < area.x1 || dsc->draw_ctx->clip_area->x1 > area.x2) return;
    lv_draw_label_dsc_t label;
    lv_draw_label_dsc_init(&label);
    lv_obj_init_draw_label_dsc(obj, LV_PART_TICKS, &label);
    label.align = LV_TEXT_ALIGN_RIGHT;
    const lv_coord_t line_height = lv_font_get_line_height(label.font);
    for (int32_t deg = 50; deg <= top; deg += 50) {
      const lv_coord_t y = y0 + h - h * deg / top;
      area.y1 = y - line_height / 2;
      area.y2 = area.y1 + line_height - 1;
      if (area.y2 < dsc->draw_ctx->clip_area->y1 || area.y1 > dsc->draw_ctx->clip_area->y2) continue;
      char text[4];
      lv_snprintf(text, sizeof(text), "%" LV_PRId32, deg);
      lv_draw_label(dsc->draw_ctx, &label, &area, text, NULL);
    }
  }, LV_EVENT_DRAW_PART_BEGIN, NULL);
}

void SensorContainer::set_chart_max(lv_obj_t *chart, int32_t max_temp) {
  LV_ASSERT(max_temp % 10 == 0 && max_temp > 200 && max_temp < 1000);
  lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, max_temp);
  lv_chart_set_range(chart, LV_CHART_AXIS_SECONDARY_Y, 0, max_temp);
}

const std::vector<int32_t> &SensorContainer::chart_max_options() {
  static const std::vector<int32_t> options = {280, 300, 320, 330, 350};
  return options;
}

int32_t SensorContainer::chart_max_temp() {
  const json v = Config::get_instance()->get_json("/chart_max_temp");
  if (v.is_number_integer()) {
    const int32_t t = v.get<int32_t>();
    const auto &o = chart_max_options();
    if (std::find(o.begin(), o.end(), t) != o.end()) return t;
  }
  return State::get_instance()->get_extruders().size() > 1 ? 330 : 300;
}

void SensorContainer::set_color(lv_color_t color) {
  lv_obj_set_style_border_color(sensor_cont, color, LV_PART_MAIN);
  if (chart != NULL && series != NULL) {
    lv_chart_set_series_color(chart, series, color);
  }
}

void SensorContainer::highlight(bool on) {
  // Как выбранное сопло на экране Extrude.
  if (on) {
    lv_obj_set_style_bg_color(sensor_cont, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_set_style_bg_opa(sensor_cont, LV_OPA_COVER, 0);
  } else {
    lv_obj_remove_local_style_prop(sensor_cont, LV_STYLE_BG_COLOR, 0);
    lv_obj_remove_local_style_prop(sensor_cont, LV_STYLE_BG_OPA, 0);
  }
}

void SensorContainer::as_tile(bool color_name) {
  tile = true;
  auto scale = (double)lv_disp_get_physical_hor_res(NULL) / 800.0;
  // The colour stripe along the bottom. The border counts on every side of
  // the content area, not only the drawn one: the content is 74x68, 4 tiles
  // fill the 356 px temperature column.
  lv_obj_set_style_border_side(sensor_cont, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
  lv_obj_set_size(sensor_cont, tile_width(), 78 * scale);

  // Top: the icon, the target and the current temperature stacked right of it.
  lv_img_set_zoom(sensor_img, 128);  // the 64 px icons at 32 px
  lv_img_set_size_mode(sensor_img, LV_IMG_SIZE_MODE_REAL);  // the zoomed size
  lv_obj_align(sensor_img, LV_ALIGN_TOP_LEFT, 0, 4 * scale);

  lv_obj_add_flag(divider_label, LV_OBJ_FLAG_HIDDEN);
  for (lv_obj_t *l : {value_label, target_label}) {
    lv_obj_set_width(l, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(l, 0, 0);
    lv_obj_set_style_border_width(l, 0, 0);
    lv_obj_set_style_text_font(l, temperature_font(), 0);  // bigger than the name
  }
  // Ставятся по верху цифр: добавленное над ними место вычитается.
  lv_coord_t room = temperature_font()->line_height - notosemi_22_digits.line_height;
  if (lv_obj_has_flag(target_label, LV_OBJ_FLAG_HIDDEN)) {
    lv_obj_align(value_label, LV_ALIGN_TOP_RIGHT, 0, 12 * scale - room);
  } else {
    lv_obj_align(target_label, LV_ALIGN_TOP_RIGHT, 0, 2 * scale - room);
    lv_obj_align(value_label, LV_ALIGN_TOP_RIGHT, 0, 24 * scale - room);
    update_target(target < 0 ? 0 : target);
  }

  // Bottom: the name, the tile's whole width.
  lv_label_set_long_mode(sensor_label, LV_LABEL_LONG_CLIP);
  lv_obj_set_width(sensor_label, LV_PCT(100));
  // 16 px задан явно: шрифт по умолчанию 20 px, и с ним «Extruder» и «Chamber»
  // не помещаются в плитку.
  lv_obj_set_style_text_font(sensor_label, &notosemi_16, 0);
  if (color_name)
    lv_obj_set_style_text_color(sensor_label, lv_obj_get_style_border_color(sensor_cont, LV_PART_MAIN), 0);
  lv_obj_align(sensor_label, LV_ALIGN_BOTTOM_LEFT, 0, 0);
}
#endif

int SensorContainer::get_target_value() {
  return target;
}

void SensorContainer::attach_series(lv_obj_t *new_chart, lv_chart_series_t *new_series) {
  pending.clear();
  with_pending.erase(this);
  chart = new_chart;
  series = new_series;
}

// Раз в 2 с добавляет накопленные точки всех датчиков и перерисовывает каждый
// график один раз: датчики приходят в разных уведомлениях, и иначе график
// перерисовывался бы целиком на каждую точку каждого датчика. Точки пишутся
// прямо перед перерисовкой, чтобы частичная перерисовка (закрылся попап) не
// показала в одной части графика новые данные, а в другой старые.
void SensorContainer::add_pending_points(lv_timer_t *) {
  std::set<lv_obj_t *> charts;
  for (SensorContainer *s : with_pending) {
    LV_ASSERT_OBJ(s->chart, &lv_chart_class);
    uint16_t count = lv_chart_get_point_count(s->chart);
    // Как lv_chart_set_next_value, но без её перерисовки: та захватывает и поля
    // под подписи осей со всех четырёх сторон, а подписи от точек не меняются.
    for (lv_coord_t v : s->pending) {
      s->series->y_points[s->series->start_point] = v;
      s->series->start_point = (s->series->start_point + 1) % count;
    }
    s->pending.clear();
    charts.insert(s->chart);
  }
  with_pending.clear();
  for (lv_obj_t *chart : charts) lv_obj_invalidate_area(chart, &chart->coords);
}

void SensorContainer::update_series(int v) {
  if (series != NULL && chart != NULL) {
    auto delta = std::time(nullptr) - last_updated_ts;
    if (delta > 1) {
      static lv_timer_t *timer = lv_timer_create(add_pending_points, 2000, NULL);
      (void)timer;
      pending.push_back(v);
      with_pending.insert(this);
      last_updated_ts = std::time(nullptr);
    }
  }
}

void SensorContainer::clear_series() {
  pending.clear();
  with_pending.erase(this);
  lv_chart_set_all_value(chart, series, LV_CHART_POINT_NONE);
}

void SensorContainer::handle_edit(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_SHORT_CLICKED) {
    spdlog::trace("sensor callback this {}, {}, {}", id, fmt::ptr(this), fmt::ptr(&numpad));
    numpad.set_callback([this](double v) {
      // Если это нагреватель камеры (с префиксом из конфига или без), вызываем специальный макрос
      if (id == "heater_generic chamber_heater" || id == "chamber_heater") {
        ws.gcode_script(fmt::format("_T_HEATER TEMP={}", (int)v));
      } else {
        ws.gcode_script(fmt::format("SET_HEATER_TEMPERATURE HEATER={} TARGET={}", id, v));
      }
    });
    numpad.foreground_reset();
    std::function<void()> on_close;
#ifdef GUPPY_FF5M
    if (tile) {  // плитка Home: для какой из них вводится температура
      highlight(true);
      on_close = [this] { highlight(false); };
    }
#endif
    numpad.set_close_callback(this, std::move(on_close));  // и для forget() при удалении
  }
}

void SensorContainer::handle_edit2(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_SHORT_CLICKED) {
    spdlog::trace("sensor callback this {}, {}, {}", id, fmt::ptr(this), fmt::ptr(&numpad));
    numpad.set_callback([this](double v) {
        update_target(v);
    });
    numpad.foreground_reset();
    std::function<void()> on_close;
#ifdef GUPPY_FF5M
    if (tile) {
      highlight(true);
      on_close = [this] { highlight(false); };
    }
#endif
    numpad.set_close_callback(this, std::move(on_close));
  }
}
