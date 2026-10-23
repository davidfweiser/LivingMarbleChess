#include "power.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cstdio>
#include <cstring>
#endif

namespace power {

bool saverOn() {
#if defined(_WIN32)
  SYSTEM_POWER_STATUS s;
  return GetSystemPowerStatus(&s) && s.SystemStatusFlag == 1;
#elif defined(__linux__)
  FILE* f = std::fopen("/sys/firmware/acpi/platform_profile", "r");
  if (!f) return false;
  char buf[32] = {};
  bool low = std::fgets(buf, sizeof(buf), f) && std::strncmp(buf, "low-power", 9) == 0;
  std::fclose(f);
  return low;
#else
  return false;
#endif
}

}  // namespace power
