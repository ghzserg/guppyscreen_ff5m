#ifndef GUPPY_PREVIEW_ACCESS_H
#define GUPPY_PREVIEW_ACCESS_H

// Which HTTP requests guppy_preview answers. POST /file writes files in the
// working tree, so only the preview's own page may use it:
// - Host must name this machine: localhost, [::1] or an IPv4 address, with
//   the preview's port. A web site can point its own name at 127.0.0.1 (DNS
//   rebinding); its requests then carry that name.
// - Origin, sent by browsers with a POST, must be that same host: another
//   web page open in the browser can't use the preview.

#include <string>

namespace preview_access {

inline bool is_ipv4(const std::string &s) {
  int dots = 0, digits = 0;
  for (char c : s) {
    if (c == '.') {
      if (digits == 0) return false;
      dots++;
      digits = 0;
    } else if (c >= '0' && c <= '9') {
      if (++digits > 3) return false;
    } else {
      return false;
    }
  }
  return dots == 3 && digits > 0;
}

// `host` and `origin`: the request's Host and Origin headers ("" if absent).
inline bool allowed(const std::string &host, const std::string &origin, int port) {
  const std::string suffix = ":" + std::to_string(port);
  if (host.size() <= suffix.size() || host.compare(host.size() - suffix.size(), suffix.size(), suffix) != 0) {
    return false;
  }
  const std::string name = host.substr(0, host.size() - suffix.size());
  if (name != "localhost" && name != "[::1]" && !is_ipv4(name)) {
    return false;
  }
  return origin.empty() || origin == "http://" + host;
}

}  // namespace preview_access

#endif  // GUPPY_PREVIEW_ACCESS_H
