#include "console_panel.h"
#include "state.h"
#include "spdlog/spdlog.h"

#include <algorithm>
#include <cctype>

LV_FONT_DECLARE(dejavusans_mono_14);

ConsolePanel::ConsolePanel(KWebSocketClient &websocket_client, std::mutex &lock, lv_obj_t *parent)
  : ws(websocket_client)
  , lv_lock(lock)
  , console_cont(lv_obj_create(parent))
  , top_cont(lv_obj_create(console_cont))
  , output(lv_obj_create(top_cont))
  , macro_list(lv_table_create(top_cont))
  , input_cont(lv_obj_create(console_cont))
  , input(lv_textarea_create(input_cont))
  , kb(lv_keyboard_create(console_cont))
{
  lv_obj_align(console_cont, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_size(console_cont, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_flow(console_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(console_cont, 0, 0);
  lv_obj_set_style_text_font(console_cont, &dejavusans_mono_14, LV_STATE_DEFAULT);
  lv_obj_clear_flag(console_cont, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_set_flex_grow(top_cont, 1);
  lv_obj_set_style_pad_all(top_cont, 0, 0);
  lv_obj_set_width(top_cont, LV_PCT(100));
  lv_obj_clear_flag(top_cont, LV_OBJ_FLAG_SCROLLABLE);

  // ---- output: scrollable column of labels ----
  lv_obj_set_size(output, LV_PCT(60), LV_PCT(100));
  lv_obj_set_style_border_width(output, 0, 0);
  lv_obj_set_style_pad_all(output, 0, 0);
  lv_obj_set_flex_flow(output, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(output, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(output, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_clear_flag(output, LV_OBJ_FLAG_SCROLL_ELASTIC);
  lv_obj_set_style_text_font(output, &dejavusans_mono_14, LV_STATE_DEFAULT);

  lv_obj_set_flex_grow(input, 1);
  lv_obj_set_width(input, LV_PCT(100));
  lv_textarea_set_one_line(input, true);

  lv_obj_set_flex_flow(input_cont, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_all(input_cont, 0, 0);
  lv_obj_set_size(input_cont, LV_PCT(100), LV_SIZE_CONTENT);

  lv_obj_t *send_btn = lv_btn_create(input_cont);
  lv_obj_set_style_text_font(send_btn, &notosemi_16, LV_STATE_DEFAULT);
  lv_obj_set_width(send_btn, 100);
  lv_obj_t *send_btn_label = lv_label_create(send_btn);
  lv_label_set_text(send_btn_label, LV_SYMBOL_NEW_LINE);
  lv_obj_center(send_btn_label);
  lv_obj_add_event_cb(send_btn, &ConsolePanel::_handle_send_macro, LV_EVENT_SHORT_CLICKED, this);

  lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_text_font(kb, &notosemi_16, LV_STATE_DEFAULT);
  lv_obj_add_event_cb(input, &ConsolePanel::_handle_kb_input, LV_EVENT_ALL, this);

  lv_obj_set_size(macro_list, LV_PCT(40), LV_PCT(100));
  lv_obj_align(macro_list, LV_ALIGN_TOP_RIGHT, 0, 0);
  lv_table_set_col_width(macro_list, 0, LV_PCT(100));

  lv_obj_add_event_cb(macro_list, &ConsolePanel::_handle_select_macro, LV_EVENT_ALL, this);
  lv_obj_set_scroll_dir(macro_list, LV_DIR_TOP | LV_DIR_BOTTOM);

  lv_obj_t *label = lv_label_create(input);
  lv_obj_set_style_text_font(label, &notosemi_16, LV_STATE_DEFAULT);
  lv_label_set_text(label, "      " LV_SYMBOL_CLOSE "      ");
  lv_obj_align(label, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_add_flag(label, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(label, &ConsolePanel::_handle_clear_input, LV_EVENT_SHORT_CLICKED, this);

  // ---- throttled output flush timer ----
  // Runs in the LVGL thread (inside lv_timer_handler), lv_lock is already held there.
  output_flush_timer = lv_timer_create(&ConsolePanel::_output_flush_timer_cb, 250, this);

  ws.register_method_callback("notify_gcode_response",
                              "ConsolePanel",
                              [this](json& d) { this->handle_macro_response(d); });
}

ConsolePanel::~ConsolePanel() {
  if (output_flush_timer) {
    lv_timer_del(output_flush_timer);
    output_flush_timer = nullptr;
  }
  if (console_cont != NULL) {
    lv_obj_del(console_cont);
    console_cont = NULL;
  }
}

lv_obj_t *ConsolePanel::get_container() {
  return console_cont;
}

void ConsolePanel::handle_kb_input(lv_event_t *e)
{
  const lv_event_code_t code = lv_event_get_code(e);

  if (code == LV_EVENT_FOCUSED) {
    lv_keyboard_set_textarea(kb, input);
    lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
  }

  if (code == LV_EVENT_DEFOCUSED) {
    lv_keyboard_set_textarea(kb, NULL);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  }

  if (code == LV_EVENT_VALUE_CHANGED) {
    // filter macros with input
    lv_obj_scroll_to_y(macro_list, 0, LV_ANIM_OFF);
    std::string cmd = std::string(lv_textarea_get_text(input));
    if (cmd.find_first_of(' ') == std::string::npos) {
      std::string upper_cmd;
      std::transform(cmd.begin(), cmd.end(), std::back_inserter(upper_cmd),
                     [](unsigned char c){ return std::toupper(c); });

      if (!all_macros.empty() || !history.empty()) {
        uint16_t index = 0;
        for (const auto &m : history) {
          if (m.rfind(upper_cmd, 0) == 0 || m.rfind(cmd, 0) == 0) {
            lv_table_set_cell_value(macro_list, index++, 0, m.c_str());
          }
        }

        for (const auto &m : all_macros) {
          if (m.rfind(upper_cmd, 0) == 0 || m.rfind(cmd, 0) == 0) {
            lv_table_set_cell_value(macro_list, index++, 0, m.c_str());
          }
        }

        lv_table_set_row_cnt(macro_list, index);
      }
    }
  }

  if (code == LV_EVENT_READY) {
    spdlog::debug("keyboard ready");
    const char *cmd = lv_textarea_get_text(input);
    if (cmd == NULL || cmd[0] == 0) {
      return;
    }

    append_line(std::string("> ") + cmd);

    if (!output_lines.empty()) {
      lv_obj_scroll_to_view(output_lines.back(), LV_ANIM_OFF);
    }

    ws.gcode_script(cmd);

    if (!history.empty()) {
      const auto &front = history.front();

      if (front != std::string(cmd)) {
        if (history.size() >= 20) {
          history.pop_back();
        }
        history.push_front(cmd);

        json h = {
          {"namespace", "fluidd"}, // leverage history from fluidd
          {"key", "console.commandHistory"},
          {"value", history}
        };

        ws.send_jsonrpc("server.database.post_item", h);
      }
    }

    lv_textarea_set_text(input, "");

    uint32_t index = 0;
    for (const auto &m : history) {
      lv_table_set_cell_value(macro_list, index++, 0, m.c_str());
    }

    for (const auto &m : all_macros) {
      lv_table_set_cell_value(macro_list, index++, 0, m.c_str());
    }
  }
}

void ConsolePanel::handle_select_macro(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_VALUE_CHANGED) {
    uint16_t row;
    uint16_t col;

    lv_table_get_selected_cell(macro_list, &row, &col);
    const char *macro = lv_table_get_cell_value(macro_list, row, col);
    lv_textarea_set_text(input, macro);
  }
}

void ConsolePanel::handle_macros(json &j) {
  uint32_t index = 0;

  // TODO: this is a race condition
  auto &db_history = State::get_instance()->get_data("/console/commandHistory"_json_pointer);

  if (!db_history.is_null()) {
    history = db_history.template get<std::list<std::string>>();
  }

  if (j.contains("result")) {
    const auto &m = j["result"];
    for (const auto &el : m.items()) {
      all_macros.push_back(el.key());
    }
  }

  std::lock_guard<std::mutex> lock(lv_lock);
  for (const auto &m : history) {
    lv_table_set_cell_value(macro_list, index++, 0, m.c_str());
  }

  for (const auto &m : all_macros) {
    lv_table_set_cell_value(macro_list, index++, 0, m.c_str());
  }
}

void ConsolePanel::handle_macro_response(json &j) {
  // Runs in the websocket thread.  Do NOT touch LVGL here and do NOT take lv_lock.
  if (!j.contains("params")) return;

  std::string local;
  for (auto &l : j["params"]) {
    local += l.template get<std::string>();
    local += "\n";
  }
  if (local.empty()) return;

  std::lock_guard<std::mutex> lock(pending_mutex);
  pending_buffer += local;
}

void ConsolePanel::append_line(const std::string &line) {
  lv_obj_t *lbl = lv_label_create(output);
  lv_obj_set_width(lbl, LV_PCT(100));
  lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_font(lbl, &dejavusans_mono_14, LV_STATE_DEFAULT);
  lv_label_set_text(lbl, line.c_str());
  output_lines.push_back(lbl);

  while (output_lines.size() > MAX_LINES) {
    lv_obj_del(output_lines.front());
    output_lines.pop_front();
  }
}

void ConsolePanel::flush_output() {
  // Runs in the LVGL thread via lv_timer_handler(); lv_lock is already held there.
  std::string to_append;
  {
    std::lock_guard<std::mutex> lock(pending_mutex);
    if (pending_buffer.empty()) return;
    to_append.swap(pending_buffer);
  }

  size_t start = 0;
  while (start < to_append.size()) {
    size_t end = to_append.find('\n', start);
    if (end == std::string::npos) end = to_append.size();
    append_line(to_append.substr(start, end - start));
    if (end == to_append.size()) break;
    start = end + 1;
  }

  if (!output_lines.empty()) {
    lv_obj_scroll_to_view(output_lines.back(), LV_ANIM_OFF);
  }
}

void ConsolePanel::handle_send_macro(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_SHORT_CLICKED) {
    lv_event_send(input, LV_EVENT_READY, this);
  }
}

void ConsolePanel::handle_clear_input(lv_event_t *e) {
  lv_textarea_set_text(input, "");
}
