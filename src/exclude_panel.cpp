#include "exclude_panel.h"
#include "state.h"
#include "spdlog/spdlog.h"

LV_IMG_DECLARE(refresh_img);
LV_IMG_DECLARE(back);

ExcludePanel::ExcludePanel(KWebSocketClient &c, std::mutex &l)
  : NotifyConsumer(l)
  , ws(c)
  , cont(lv_obj_create(lv_scr_act()))
  , exclude_cont(lv_obj_create(cont))
  , back_btn(cont, &back, _("Back") /* "Назад" */, &ExcludePanel::_handle_callback, this)
{
  background();
  lv_obj_set_style_pad_all(cont, 0, 0);
  lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_center(exclude_cont);
  lv_obj_set_size(exclude_cont, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_flow(exclude_cont, LV_FLEX_FLOW_COLUMN);

  lv_obj_align(back_btn.get_container(), LV_ALIGN_BOTTOM_RIGHT, 0, -20);

  ws.register_notify_update(this);
}

ExcludePanel::~ExcludePanel() {
  reset();
  if (cont != NULL) {
    lv_obj_del(cont);
    cont = NULL;
  }
  ws.unregister_notify_update(this);
}

void ExcludePanel::reset() {
    for (auto item : switches) {
        delete item;
    }

    switches.clear();
}

void ExcludePanel::init() {
    // Создаем контейнер для элементов
    lv_obj_set_layout(exclude_cont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(exclude_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(exclude_cont, LV_PCT(100));

    State *s = State::get_instance();
    auto v = s->get_data("/printer_state/exclude_object"_json_pointer);
    if (!v.is_null()) {
        using json = hv::Json;  // Пространство имен libhv для JSON
        std::vector<std::string> excluded_list;

        // Получение списков
        const json& excluded_objects = v["excluded_objects"];
        const json& objects = v["objects"];

        // Конвертируем excluded_objects в вектор строк
        for (const auto& name_json : excluded_objects) {
            excluded_list.push_back(name_json.get<std::string>());
        }

        // Цикл создания переключателей
        for (const auto& obj : objects) {
            std::string name;
            if (obj.contains("name")) {
                name = obj["name"].get<std::string>();
            } else {
                continue;
            }

            // Группируем переключатель и метку в контейнер
            SwitchContainer* item = new SwitchContainer(exclude_cont, name.c_str(), this, ws, name);

            // Проверка на исключение
            if (std::find(excluded_list.begin(), excluded_list.end(), name) != excluded_list.end()) {
                item->off();
            } else {
                item->on();
            }
            switches.push_back(item);
        }
        excluded_list.clear();
    }
}

void ExcludePanel::foreground() {
  lv_obj_move_foreground(cont);
}

void ExcludePanel::background() {
  lv_obj_move_background(cont);
}

void ExcludePanel::consume(json &j) {
    std::lock_guard<std::mutex> lock(lv_lock);

    auto v = j["/params/0/exclude_object/objects"_json_pointer];
    if (!v.is_null()) {
        reset();

        // Цикл создания переключателей
        for (const auto& obj : v) {
            std::string name;
            if (obj.contains("name")) {
                name = obj["name"].get<std::string>();
            } else {
                continue;
            }

            // Группируем переключатель и метку в контейнер
            SwitchContainer* item = new SwitchContainer(exclude_cont, name.c_str(), this, ws, name);
            item->on();
            switches.push_back(item);
        }
    }

    v = j["/params/0/exclude_object/excluded_objects"_json_pointer];
    if (!v.is_null()) {
        std::vector<std::string> excluded_list;

        for (const auto& name_json : v) {
            excluded_list.push_back(name_json.get<std::string>());
        }

        for (auto item : switches) {
            if (std::find(excluded_list.begin(), excluded_list.end(), item->get_name()) != excluded_list.end()) {
                item->off();
            } else {
                item->on();
            }
        }

        excluded_list.clear();
  }
}

void ExcludePanel::handle_callback(lv_event_t *e) {
  lv_obj_t *btn = lv_event_get_current_target(e);

  if (btn == back_btn.get_container()) {
    background();
    return;
  }
}
