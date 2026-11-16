#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest/doctest.h"

#include <cstdio>
#include <cstdlib>

int main(int argc, char **argv) {
  doctest::Context context(argc, argv);
  int rc = context.run();
  std::fflush(NULL);
  // guppyscreen's singletons, LVGL and the libhv threads were never meant to
  // be torn down, so skip static destructors.
  std::_Exit(rc);
}
