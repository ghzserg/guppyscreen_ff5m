#include "macros_panel.h"
#include "state.h"
#include "utils.h"
#include "spdlog/spdlog.h"

#include <chrono>

MacrosPanel::MacrosPanel(KWebSocketClient &c, std::mutex &l, lv_obj_t *parent)
  : ws(c)
  , lv_lock(l)
  , cont(lv_obj_create(parent))
  , top_controls(lv_obj_create(cont))
  , show_hide_switch(lv_switch_create(top_controls))
  , top_cont(lv_obj_create(cont))
  , kb(lv_keyboard_create(cont))
{
  lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_pad_all(cont, 0, 0);

  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(cont, 0, 0);

  lv_obj_set_size(top_controls, LV_PCT(100), LV_SIZE_CONTENT);

  lv_obj_align(show_hide_switch, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_t *label = lv_label_create(top_controls);
  lv_label_set_text(label, _("Show Hidden") /* "Показать скрытые" */);
  lv_obj_align_to(label, show_hide_switch, LV_ALIGN_OUT_LEFT_MID, 0, 0);
  lv_obj_add_event_cb(show_hide_switch, &MacrosPanel::_handle_hide_show, LV_EVENT_VALUE_CHANGED, this);

  lv_obj_set_flex_grow(top_cont, 1);
  lv_obj_set_style_pad_all(top_cont, 0, 0);
  lv_obj_set_width(top_cont, LV_PCT(100));
  lv_obj_set_flex_flow(top_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(top_cont, 0, 0);

  lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_text_font(kb, &notosemi_16, LV_STATE_DEFAULT);
}

MacrosPanel::~MacrosPanel()
{
  lv_async_call_cancel(&MacrosPanel::_rebuild, this);
  // The items delete their own containers, which are inside cont.
  std::vector<std::shared_ptr<MacroItem>>().swap(macro_items);
  if (cont != NULL) {
    lv_obj_del(cont);
    cont = NULL;
  }
}

void MacrosPanel::foreground() {
  shown = true;
  if (!built) {
    populate();
  }
}

void MacrosPanel::background() {
  shown = false;
}

void MacrosPanel::refresh() {
  if (!built) {
    return;
  }
  lv_keyboard_set_textarea(kb, NULL);  // may point into a macro item
  lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  std::vector<std::shared_ptr<MacroItem>>().swap(macro_items);
  built = false;
  if (shown) {
    // refresh() runs on the websocket thread (MainPanel::init). Build on the
    // LVGL thread, so the list comes from the main heap, not libhv's arena.
    lv_async_call_cancel(&MacrosPanel::_rebuild, this);
    lv_async_call(&MacrosPanel::_rebuild, this);
  }
}

void MacrosPanel::_rebuild(void *panel) {
  MacrosPanel *p = (MacrosPanel *)panel;
  if (p->shown && !p->built) {
    p->populate();
  }
}

void MacrosPanel::populate() {
  auto t0 = std::chrono::steady_clock::now();
  built = true;
  macro_items.clear();

  // Runs on the LVGL thread: copy under State's lock, the websocket thread
  // may reset or update guppysettings meanwhile.
  json macro_settings = State::get_instance()->get_data_copy("/guppysettings/macros/settings"_json_pointer);

  auto macros = State::get_instance()->get_macros();

  // Each item is built on a detached screen and then moved into the list.
  // Creating a textarea runs a layout pass over its whole screen; on the
  // list's screen that pass covers every item built so far (~0.9 s on the
  // printer for zMod's ~110 visible macros).
  lv_obj_t *staging = lv_obj_create(NULL);

  // uint32_t i = 0;
  // lv_color_t cur_bg = lv_obj_get_style_bg_color(cont, 0);
  // lv_color_t mixed = lv_color_mix(lv_palette_main(LV_PALETTE_GREY),
  //                                     cur_bg, LV_OPA_10);

  for (auto const & [k, v] : macros) {
    auto hidden_json = macro_settings[json::json_pointer(fmt::format("/{}/hidden", k))];
    bool hidden = !hidden_json.is_null() ? hidden_json.template get<bool>() : false;
    macro_items.push_back(std::make_shared<MacroItem>(ws, staging, k, v, kb, hidden));
    lv_obj_set_parent(macro_items.back()->get_container(), top_cont);

    // if (i % 2 == 0) {
    //         macro_items.push_back(std::make_shared<MacroItem>(ws, top_cont, k, v, kb, mixed, hidden));
    // } else {
    //         macro_items.push_back(std::make_shared<MacroItem>(ws, top_cont, k, v, kb, cur_bg, hidden));
    // }
    // i++;
  }

  lv_obj_del(staging);

  if (lv_obj_has_state(show_hide_switch, LV_STATE_CHECKED)) {
    for (const auto &m : macro_items) {
      m->show();
    }
  }

  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
  spdlog::info("macros: built {} items in {} ms", macro_items.size(), ms);
}

void MacrosPanel::handle_hide_show(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if(code == LV_EVENT_VALUE_CHANGED) {
    bool show_hidden = lv_obj_has_state(show_hide_switch, LV_STATE_CHECKED);
    if (show_hidden) {
      for (const auto &m : macro_items) {
        m->show();
      }
    } else {
      for (const auto &m : macro_items) {
        m->hide_if_hidden();
      }
    }
  }
}
