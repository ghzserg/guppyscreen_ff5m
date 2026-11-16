// preview/access.h: which HTTP requests the browser preview answers. Its
// POST /file writes files in the working tree, so only its own page may
// use it: no other web page (Origin) and no other host name (DNS rebinding).

#include "doctest/doctest.h"

#include "preview/access.h"

TEST_CASE("preview answers its own page") {
  CHECK(preview_access::allowed("localhost:8080", "", 8080));                       // GET, no Origin
  CHECK(preview_access::allowed("localhost:8080", "http://localhost:8080", 8080));  // POST from the page
  CHECK(preview_access::allowed("127.0.0.1:18080", "http://127.0.0.1:18080", 18080));
  CHECK(preview_access::allowed("[::1]:8080", "http://[::1]:8080", 8080));
}

TEST_CASE("preview refuses other pages and host names") {
  // Another web page posting to the preview.
  CHECK_FALSE(preview_access::allowed("127.0.0.1:8080", "https://evil.example", 8080));
  CHECK_FALSE(preview_access::allowed("localhost:8080", "http://localhost:9000", 8080));
  CHECK_FALSE(preview_access::allowed("localhost:8080", "null", 8080));
  // A name of another site resolving to 127.0.0.1 (DNS rebinding).
  CHECK_FALSE(preview_access::allowed("evil.example:8080", "", 8080));
  CHECK_FALSE(preview_access::allowed("evil.example:8080", "http://evil.example:8080", 8080));
  // Other port, no port, no Host.
  CHECK_FALSE(preview_access::allowed("localhost:9000", "", 8080));
  CHECK_FALSE(preview_access::allowed("localhost", "", 8080));
  CHECK_FALSE(preview_access::allowed("", "", 8080));
}

TEST_CASE("preview answers an IP address such as the WSL one") {
  // With --host 0.0.0.0 the page may be opened at the WSL address.
  // DNS rebinding needs a name, so any IPv4 address is safe.
  CHECK(preview_access::allowed("172.20.1.5:8080", "http://172.20.1.5:8080", 8080));
  CHECK_FALSE(preview_access::allowed("172.20.1.5.evil.example:8080", "", 8080));
  CHECK_FALSE(preview_access::allowed("1.2.3:8080", "", 8080));
}
