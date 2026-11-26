#ifndef __MINI_PRINT_STATUS__
#define __MINI_PRINT_STATUS__

#include "lvgl/lvgl.h"
#include <string>
#include "websocket_client.h"

class MiniPrintStatus {
 public:
  MiniPrintStatus(lv_obj_t *parent,
                  lv_event_cb_t cb,
                  void* user_data,
                    KWebSocketClient &ws);

  ~MiniPrintStatus();

  void set_printing(bool printing);
  lv_obj_t *get_container();

  void update_eta(const std::string &eta_str);
  void update_status(std::string &status_str);
  void update_progress(int p);
  void update_img(const std::string &img_path, size_t twidth);
  void reset();
  // Выводит сохранённые ETA, статус и прогресс (неизменившиеся не перерисовываются).
  // Обычные обновления выводятся не чаще раза в секунду.
  void show(bool immediately = false);
#ifdef GUPPY_FF5M
  void update_clock(int hour, int minute);
  void set_clock_enabled(bool enabled);
#endif

 private:
  KWebSocketClient &ws;
  lv_obj_t *cont;
  lv_obj_t *progress_bar;
  lv_obj_t *thumb;
  lv_obj_t *status_label;
#ifdef GUPPY_FF5M
  lv_obj_t *clock_hours;
  lv_obj_t *clock_minutes;
  lv_obj_t *clock_colon;
  bool clock_enabled = true;
#endif
  std::string status;
  std::string eta;
  int progress = 0;
  uint32_t shown_at = 0;
  bool changed = false;  // ETA, статус или прогресс ещё не выведены
};

#endif //__MINI_PRINT_STATUS__
