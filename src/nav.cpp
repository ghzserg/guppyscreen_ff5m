#include "nav.h"

#include "button_container.h"
#include "websocket_client.h"  // _()
#include "spdlog/spdlog.h"

LV_FONT_DECLARE(materialdesign_font_40);
LV_IMG_DECLARE(emergency);

namespace {

constexpr lv_coord_t BAR_WIDTH = 60;
constexpr int SLOTS = 5;  // the tabs, then Stop

void plain(lv_obj_t *obj) {
  lv_obj_set_style_pad_all(obj, 0, 0);
  lv_obj_set_style_radius(obj, 0, 0);
  lv_obj_set_style_border_width(obj, 0, 0);
  lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t *slot_button(lv_obj_t *bar, int slot) {
  lv_obj_t *btn = lv_obj_create(bar);
  plain(btn);
  lv_obj_set_size(btn, BAR_WIDTH, Nav::slot_height());
  lv_obj_set_pos(btn, 0, slot * Nav::slot_height());
  lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_STATE_CHECKED);
  lv_obj_set_style_bg_color(btn, lv_palette_main(LV_PALETTE_GREY), LV_STATE_CHECKED);
  // A press shows at once (Stop above all, when it doesn't ask first).
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_set_style_bg_color(btn, lv_palette_darken(LV_PALETTE_GREY, 2), LV_STATE_PRESSED);
  lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
  return btn;
}

lv_obj_t *centred_img(lv_obj_t *parent, const lv_img_dsc_t *src, lv_coord_t size) {
  lv_obj_t *img = lv_img_create(parent);
  lv_img_set_src(img, src);
  lv_img_set_zoom(img, (uint16_t)(256 * size / src->header.w));
  lv_obj_center(img);
  return img;
}

}  // namespace

Nav &Nav::get() {
  static Nav *instance = new Nav();
  return *instance;
}

Nav::Nav()
  : bar_(lv_obj_create(lv_scr_act()))
  , content_(lv_obj_create(lv_scr_act()))
  , stop_(NULL)
{
  plain(bar_);
  lv_obj_set_pos(bar_, 0, 0);
  lv_obj_set_size(bar_, BAR_WIDTH, LV_PCT(100));

  plain(content_);
  lv_obj_set_pos(content_, BAR_WIDTH, 0);
  lv_obj_set_size(content_, content_width(), LV_PCT(100));

  stop_ = slot_button(bar_, SLOTS - 1);
  centred_img(stop_, &emergency, 48);
  lv_obj_add_event_cb(stop_, &Nav::stop_clicked, LV_EVENT_SHORT_CLICKED, this);
}

lv_obj_t *Nav::bar() { return bar_; }

lv_coord_t Nav::content_width() {
  return lv_disp_get_hor_res(NULL) - BAR_WIDTH;
}
lv_obj_t *Nav::content() { return content_; }

lv_coord_t Nav::slot_height() {
  return lv_disp_get_ver_res(NULL) / SLOTS;
}

lv_coord_t Nav::slot_top(Tab t) {
  return (int)t * slot_height();
}

lv_obj_t *Nav::add_button(Tab t) {
  lv_obj_t *btn = slot_button(bar_, (int)t);
  // set_attention(): USER_1 outweighs CHECKED and PRESSED, so each pairing
  // gets its own shade.
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_STATE_USER_1);
  lv_obj_set_style_bg_color(btn, lv_palette_main(LV_PALETTE_ORANGE), LV_STATE_USER_1);
  lv_obj_set_style_bg_color(btn, lv_palette_darken(LV_PALETTE_ORANGE, 1),
                            LV_STATE_USER_1 | LV_STATE_CHECKED);
  lv_obj_set_style_bg_color(btn, lv_palette_darken(LV_PALETTE_ORANGE, 3),
                            LV_STATE_USER_1 | LV_STATE_PRESSED);
  lv_obj_add_event_cb(btn, &Nav::tab_clicked, LV_EVENT_SHORT_CLICKED, (void *)(intptr_t)t);
  buttons[t] = btn;
  return btn;
}

lv_obj_t *Nav::add_screen() {
  lv_obj_t *screen = lv_obj_create(content_);
  plain(screen);
  lv_obj_set_size(screen, LV_PCT(100), LV_PCT(100));
  return screen;
}

lv_obj_t *Nav::add_tab(Tab t, const char *glyph) {
  lv_obj_t *root = add_screen();
  roots[t] = root;

  lv_obj_t *label = lv_label_create(add_button(t));
  lv_obj_set_style_text_font(label, &materialdesign_font_40, 0);
  lv_label_set_text(label, glyph);
  lv_obj_center(label);
  return root;
}

void Nav::add_tab(Tab t, lv_obj_t *root, const lv_img_dsc_t *icon) {
  roots[t] = root;
  centred_img(add_button(t), icon, 40);
}

void Nav::set_root(Tab t, lv_obj_t *root) {
  if (roots[t] == root) {
    return;
  }
  roots[t] = root;
  if (t == active_) {
    // The old root is no longer a root: if it was on top, it is closed
    // (with its leave action) like any open screen.
    close_open_screens_and_show(root);
  }
}

lv_obj_t *Nav::root(Tab t) const {
  return roots[t];
}

void Nav::activate(Tab t) {
  if (roots[t] == NULL) {
    spdlog::warn("nav: tab {} has no screen", (int)t);
    return;
  }
  if (t != active_ && roots[active_] != NULL) {
    leave(roots[active_]);
  }
  active_ = t;
  close_open_screens_and_show(roots[t]);
  highlight();
}

Nav::Tab Nav::active() const {
  return active_;
}

void Nav::set_attention(Tab t, bool on) {
  if (buttons[t] == NULL) return;
  if (on) {
    lv_obj_add_state(buttons[t], LV_STATE_USER_1);
  } else {
    lv_obj_clear_state(buttons[t], LV_STATE_USER_1);
  }
}

void Nav::on_leave(lv_obj_t *screen, std::function<void()> action) {
  leave_actions[screen] = std::move(action);
}

void Nav::add_dialog(lv_obj_t *dialog) {
  dialogs.push_back(dialog);
}

bool Nav::dialog_open() const {
  int top = top_root_index();
  for (lv_obj_t *d : dialogs) {
    if ((int)lv_obj_get_index(d) > top) {
      return true;
    }
  }
  return false;
}

void Nav::set_stop(std::function<void()> stop) {
  stop_action = std::move(stop);
}

lv_obj_t *Nav::tab_button(Tab t) {
  return buttons[t];
}

lv_obj_t *Nav::stop_button() {
  return stop_;
}

void Nav::start() {
  std::vector<lv_obj_t *> others;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(content_); i++) {
    lv_obj_t *child = lv_obj_get_child(content_, i);
    bool is_root = false;
    for (lv_obj_t *r : roots) is_root = is_root || r == child;
    if (!is_root) others.push_back(child);
  }
  for (lv_obj_t *o : others) {
    lv_obj_move_background(o);
  }
  activate(HOME);
}

int Nav::top_root_index() const {
  int top = -1;
  for (lv_obj_t *r : roots) {
    if (r != NULL && (int)lv_obj_get_index(r) > top) {
      top = lv_obj_get_index(r);
    }
  }
  return top;
}

bool Nav::is_dialog(lv_obj_t *obj) const {
  for (lv_obj_t *d : dialogs) {
    if (d == obj) return true;
  }
  return false;
}

void Nav::leave(lv_obj_t *screen) {
  auto it = leave_actions.find(screen);
  if (it != leave_actions.end()) {
    it->second();
  }
}

void Nav::close_open_screens_and_show(lv_obj_t *root) {
  int top = top_root_index();
  std::vector<lv_obj_t *> open_dialogs;  // bottom to top
  std::vector<lv_obj_t *> open_screens;  // top to bottom
  uint32_t n = lv_obj_get_child_cnt(content_);
  for (int i = (int)n - 1; i > top; i--) {
    lv_obj_t *child = lv_obj_get_child(content_, i);
    if (is_dialog(child)) {
      open_dialogs.insert(open_dialogs.begin(), child);
    } else {
      open_screens.push_back(child);
    }
  }
  for (lv_obj_t *s : open_screens) {
    leave(s);
    lv_obj_move_background(s);
  }
  lv_obj_move_foreground(root);
  for (lv_obj_t *d : open_dialogs) {
    lv_obj_move_foreground(d);
  }
}

void Nav::highlight() {
  for (int t = 0; t < TAB_COUNT; t++) {
    if (buttons[t] == NULL) continue;
    if (t == active_) {
      lv_obj_add_state(buttons[t], LV_STATE_CHECKED);
    } else {
      lv_obj_clear_state(buttons[t], LV_STATE_CHECKED);
    }
  }
}

void Nav::tab_clicked(lv_event_t *e) {
  Nav &nav = get();
  if (nav.dialog_open()) {
    spdlog::debug("nav: a dialog is open, tab tap ignored");
    return;
  }
  nav.activate((Tab)(intptr_t)lv_event_get_user_data(e));
}

void Nav::stop_clicked(lv_event_t *e) {
  Nav &nav = *(Nav *)lv_event_get_user_data(e);
  auto stop = [&nav]() {
    spdlog::debug("emergency stop pressed");
    if (nav.stop_action) nav.stop_action();
  };
  if (ButtonContainer::prompt_emergency_stop()) {
    ButtonContainer::confirm(_("Do you want to emergency stop?"), stop);
  } else {
    stop();
  }
}
