#include "switch_container.h"
#include "spdlog/spdlog.h"
#include "websocket_client.h"

SwitchContainer::SwitchContainer(lv_obj_t *parent,
                                 const char *text,
                                 void* user_data,
                                 KWebSocketClient &c,
                                 const std::string &prompt)
  : swt_cont(lv_obj_create(parent))
  , swt(lv_switch_create(swt_cont))
  , label(lv_label_create(swt_cont))
  , ws(c)
  , prompt_text(prompt)
{
    lv_obj_set_layout(swt_cont,    LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_flow(swt_cont, LV_FLEX_FLOW_ROW);
    lv_obj_clear_flag(swt_cont,    LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(swt_cont,     LV_PCT(100));
    lv_obj_set_height(swt_cont,    LV_SIZE_CONTENT);
    lv_obj_add_flag(swt_cont,      LV_OBJ_FLAG_CLICKABLE);

    lv_obj_add_event_cb(swt_cont, [](lv_event_t *e) {
        lv_event_code_t code = lv_event_get_code(e);
        if (code == LV_EVENT_SHORT_CLICKED) {
            ((SwitchContainer*)e->user_data)->handle_prompt();
        }
    }, LV_EVENT_SHORT_CLICKED, this);

    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_style_pad_left(label, 5, 0);
    lv_label_set_text(label, text);
    lv_obj_clear_flag(swt, LV_OBJ_FLAG_CLICKABLE);
}

SwitchContainer::~SwitchContainer() {
    lv_obj_del(label);
    lv_obj_del(swt);
    lv_obj_del(swt_cont);
}

lv_obj_t *SwitchContainer::get_container() {
    return swt_cont;
}

lv_obj_t *SwitchContainer::get_switch() {
    return swt;
}

void SwitchContainer::disable() {
    lv_obj_add_state(swt, LV_STATE_DISABLED);
    lv_obj_add_state(swt_cont, LV_STATE_DISABLED);
    lv_obj_add_state(label, LV_STATE_DISABLED);
}

void SwitchContainer::enable() {
    lv_obj_clear_state(swt, LV_STATE_DISABLED);
    lv_obj_clear_state(swt_cont, LV_STATE_DISABLED);
    lv_obj_clear_state(label, LV_STATE_DISABLED);
}

void SwitchContainer::off() {
    lv_obj_clear_state(swt,      LV_STATE_CHECKED);

    lv_obj_add_state(swt_cont, LV_STATE_DISABLED);
    lv_obj_add_state(label,    LV_STATE_DISABLED);
    lv_obj_add_state(swt,      LV_STATE_DISABLED);
}

void SwitchContainer::on() {
    lv_obj_add_state(swt,        LV_STATE_CHECKED);

    lv_obj_clear_state(swt_cont, LV_STATE_DISABLED);
    lv_obj_clear_state(label,    LV_STATE_DISABLED);
    lv_obj_clear_state(swt,      LV_STATE_DISABLED);
}

void SwitchContainer::on_exlude() {
    lv_obj_add_state(swt,        LV_STATE_CHECKED);

    lv_obj_clear_state(swt_cont, LV_STATE_DISABLED);
    lv_obj_clear_state(label,    LV_STATE_DISABLED);
    lv_obj_clear_state(swt,      LV_STATE_DISABLED);
    spdlog::debug("exclude object");
    ws.gcode_script(fmt::format("EXCLUDE_OBJECT NAME={}", lv_label_get_text(label)));
}

void SwitchContainer::hide() {
    lv_obj_add_flag(swt_cont, LV_OBJ_FLAG_HIDDEN);
}

void SwitchContainer::handle_prompt() {
    static const char * swts[] = {"Exclude", "Cancel", ""};

    lv_obj_t *mbox1 = lv_msgbox_create(NULL, "Exclude Object?/Исключить объект?", prompt_text.c_str(), swts, false);
    lv_obj_t *msg = ((lv_msgbox_t*)mbox1)->text;
    lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(msg, LV_PCT(100));
    lv_obj_center(msg);

    lv_obj_t *swtm = lv_msgbox_get_btns(mbox1);
    lv_btnmatrix_set_btn_ctrl(swtm, 0, LV_BTNMATRIX_CTRL_CHECKED);
    lv_btnmatrix_set_btn_ctrl(swtm, 1, LV_BTNMATRIX_CTRL_CHECKED);
    lv_obj_add_flag(swtm, LV_OBJ_FLAG_FLOATING);
    lv_obj_align(swtm, LV_ALIGN_BOTTOM_MID, 0, 0);

    auto hscale = (double)lv_disp_get_physical_ver_res(NULL) / 480.0;

    lv_obj_set_size(swtm, LV_PCT(90), 50 *hscale);
    lv_obj_set_size(mbox1, LV_PCT(50), LV_PCT(35));

    lv_obj_add_event_cb(mbox1, [](lv_event_t *e) {
        lv_obj_t *obj = lv_obj_get_parent(lv_event_get_target(e));
        uint32_t clicked_swt = lv_msgbox_get_active_btn(obj);
        if(clicked_swt == 0) {
            ((SwitchContainer*)e->user_data)->run_callback();
        }

        lv_msgbox_close(obj);
    }, LV_EVENT_VALUE_CHANGED, this);

    lv_obj_center(mbox1);
}

void SwitchContainer::run_callback() {
    on_exlude();
}
