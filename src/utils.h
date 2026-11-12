#ifndef __K_UTILS_H__
#define __K_UTILS_H__

#include "hv/json.hpp"
#include "lvgl/lvgl.h"
#include <vector>
#include <map>
#include <functional>
#include <algorithm>
#include <utility>

using json = nlohmann::json;

namespace KUtils {
  bool is_homed();
  bool is_running_local();
  std::string get_root_path(const std::string root_name);

  // path, width
  std::pair<std::string, size_t> get_thumbnail(const std::string &gcode_file, json &j, double scale);

  std::string download_file(const std::string &root,
                            const std::string &fname,
                            const std::string &dest);

  std::vector<std::string> get_interfaces();
  std::string interface_ip(const std::string &interface);
  std::string get_wifi_interface();

  template <typename Out>
  void split(const std::string &s, char delim, Out result);

  std::vector<std::string> split(const std::string &s, char delim);

  std::string get_obj_name(const std::string &id);
  std::string to_title(std::string s);
  std::string end_time(int64_t seconds_to_add);
  std::string eta_string(int64_t s);
  size_t bytes_to_mb(size_t s);

  template<typename T, typename U> void sort_map_values(std::map<T, U> v,
                                                        std::vector<U> &out_vect,
                                                        std::function<bool(U&, U&)> sorter) {
    for (auto &el : v) {
      out_vect.push_back(el.second);
    }

    std::sort(out_vect.begin(), out_vect.end(), sorter);
  };

  std::map<std::string, std::map<std::string, std::string>> parse_macros(const json &m);

  // LVGL decodes a PNG to 4 bytes per pixel and keeps it in its image cache
  // until the entry is evicted, even when no image shows it any more.

  // lv_img_set_src(), then drop the decoded copy of the file `img` showed
  // before (if any) from the image cache.
  void set_img_src(lv_obj_t *img, const void *src);

  // Drop the decoded copy of the file `img` shows from the image cache, e.g.
  // when its panel is closed. It is decoded again when next drawn.
  void uncache_img(lv_obj_t *img);

  // LVGL's spinners animate forever from creation, hidden or not. Show a
  // spinner with spinner_show() (starts its two arc animations, like
  // lv_spinner_create(parent, time_ms, arc_len)) and hide it with
  // spinner_hide() (stops them). Call spinner_hide() right after creating one.
  void spinner_show(lv_obj_t *spinner, uint32_t time_ms = 1000, uint16_t arc_len = 60);
  void spinner_hide(lv_obj_t *spinner);

};

#endif // __K_UTILS_H__
