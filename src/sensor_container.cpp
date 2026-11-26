#include "sensor_container.h"
#include "spdlog/spdlog.h"
#include "config.h"
#include "state.h"

#include <algorithm>
#include <numeric>

#include <set>
#include <string>

LV_FONT_DECLARE(notosemi_22_digits);
LV_FONT_DECLARE(notosemi_16);
LV_FONT_DECLARE(dejavusans_mono_14);

// Шаг делений шкалы графика и его линий сетки, °: наибольший, при котором на
// делениях и верх шкалы, и цифры через 50 (у 300 и 350 — 50, у 280, 320, 330 — 10).
static int32_t chart_step(int32_t max_temp) { return std::gcd(max_temp, (int32_t)50); }

// Половина строки цифр шкалы: на столько они выходят за деление вверх и вниз.
static lv_coord_t chart_half_line() { return lv_font_get_line_height(&dejavusans_mono_14) / 2 + 1; }

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
  // Только горизонтальные линии, на 100° и 200°, чёрные толщиной 1 px: из
  // вертикальных при растянутом по X графике видна лишь крайняя справа.
  lv_obj_set_style_line_color(chart, lv_color_black(), 0);
  // Цифры шкалы мельче (тема: 16 px) и цвета текста, а не серые.
  lv_obj_set_style_text_font(chart, &dejavusans_mono_14, LV_PART_TICKS);
  lv_obj_set_style_text_color(chart, lv_obj_get_style_text_color(chart, LV_PART_MAIN), LV_PART_TICKS);
  // Шкала справа (вторая ось Y с тем же диапазоном) внутри графика, на его
  // фоне: LVGL ставит подписи за правым краем через отступ pad_right(TICKS),
  // отрицательный отступ возвращает их внутрь, а pad_right графика оставляет
  // под ними место. График в ячейке сетки родителя (столбцы 0–1, строка 2),
  // слева вровень с плитками, шириной с четыре плитки.
  const lv_coord_t text_px = lv_txt_get_width("300", 3, &dejavusans_mono_14, 0, LV_TEXT_FLAG_NONE), gap = 4;
  lv_obj_set_style_pad_right(chart, -(text_px + gap) - 1, LV_PART_TICKS);  // от x2 + 1
  lv_obj_set_style_pad_right(chart, text_px + 2 * gap, 0);
  lv_chart_set_axis_tick(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 0, 7, 5, false, 0);
  set_chart_max(chart, max_temp);
  // Фон над областью графика выше на половину строки (верхняя цифра у края
  // целиком на фоне), область графика прежняя.
  const lv_coord_t half_line = chart_half_line();
  lv_obj_set_width(chart, 4 * tile_width() + 3 * tile_gap());
  lv_obj_update_layout(chart);  // высота задана в процентах: узнать её в пикселях
  lv_obj_set_height(chart, lv_obj_get_height(chart) + half_line - lv_obj_get_style_pad_top(chart, 0));
  lv_obj_set_style_pad_top(chart, half_line, 0);
  lv_obj_set_grid_cell(chart, LV_GRID_ALIGN_START, 0, 2, LV_GRID_ALIGN_END, 2, 1);
  // График растянут по X (zoom) и прокручен к последним точкам: без этого
  // LVGL рисует полосу прокрутки штрихом у нижней цифры шкалы.
  lv_obj_set_scrollbar_mode(chart, LV_SCROLLBAR_MODE_OFF);
  lv_event_cb_t draw_part = [](lv_event_t *e) {
    lv_obj_draw_part_dsc_t *dsc = lv_event_get_draw_part_dsc(e);
    if (dsc->type == LV_CHART_DRAW_PART_TICK_LABEL && dsc->id == LV_CHART_AXIS_SECONDARY_Y && dsc->text != NULL &&
        lv_event_get_code(e) == LV_EVENT_DRAW_PART_BEGIN) {
      // LVGL ставит подписи второй оси от левого края: дополнить пробелами до
      // трёх знаков, шрифт моноширинный — 50 и 100 встанут вровень по правому
      // краю. Нуля нет: он у нижнего края, наполовину за ним.
      if (dsc->value == 0 || dsc->value % 50 != 0)
        dsc->text[0] = '\0';
      else
        lv_snprintf(dsc->text, dsc->text_length, "%3" LV_PRId32, dsc->value);
      return;
    }
    // id линии сверху вниз: 0 — max_temp°, 1 — на шаг ниже, …, последняя — 0°.
    if (dsc->type != LV_CHART_DRAW_PART_DIV_LINE_HOR) return;
    static lv_draw_line_dsc_t saved;  // line_dsc общий для всех линий: после каждой вернуть
    if (lv_event_get_code(e) == LV_EVENT_DRAW_PART_END) {
      *dsc->line_dsc = saved;
      return;
    }
    saved = *dsc->line_dsc;
    const int32_t max_temp = ((const lv_chart_t *)lv_event_get_current_target(e))->ymax[LV_CHART_AXIS_SECONDARY_Y];
    const int32_t deg = max_temp - (int32_t)dsc->id * chart_step(max_temp);
    if (deg != 100 && deg != 200) dsc->line_dsc->opa = LV_OPA_TRANSP;
  };
  lv_obj_add_event_cb(chart, draw_part, LV_EVENT_DRAW_PART_BEGIN, NULL);
  lv_obj_add_event_cb(chart, draw_part, LV_EVENT_DRAW_PART_END, NULL);
}

void SensorContainer::set_chart_max(lv_obj_t *chart, int32_t max_temp) {
  // Шкала 0–max_temp°, цифры через 50° (верхняя — max_temp, если кратна 50: у 330 это 300).
  // LVGL делит высоту поровну на деления и линии сетки: их по одной на каждый
  // шаг (chart_step), видны только нужные (обработчик в style_chart).
  // Цифры шкалы не длиннее трёх знаков, линии 100° и 200° ниже верха.
  LV_ASSERT(max_temp % 10 == 0 && max_temp > 200 && max_temp < 1000);
  const int32_t count = max_temp / chart_step(max_temp) + 1;
  lv_chart_set_div_line_count(chart, count, 0);
  lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, max_temp);
  lv_chart_set_range(chart, LV_CHART_AXIS_SECONDARY_Y, 0, max_temp);
  // Подписи у каждого деления, цифры оставлены только у кратных 50. draw_size —
  // запас рисования подписей на половину строки за краями.
  lv_chart_set_axis_tick(chart, LV_CHART_AXIS_SECONDARY_Y, 0, 0, count, 1, true, chart_half_line());
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

void SensorContainer::as_tile() {
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
