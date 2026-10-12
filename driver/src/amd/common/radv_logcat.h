/*
 * Copyright 2026 JimVulkan
 * SPDX-License-Identifier: MIT
 */

/*
 * radv_logcat.h -- verbose Xclipse bring-up trace (logcat tag RADV_XCLIPSE), printed at
 * debug.radv_xclipse_log level 2. See ac_xclipse_log.h.
 */
#ifndef RADV_LOGCAT_H
#define RADV_LOGCAT_H

#include "ac_xclipse_log.h"
#include "util/macros.h"

#ifdef __cplusplus
extern "C" {
#endif

void radv_log_msg(const char *fmt, ...);

#ifdef __cplusplus
}
#endif

/* Test at the call site: otherwise the draw path prepares dozens of variadic arguments even
 * while logging is off. Keep the check in radv_log_msg too for its direct callers. These four
 * legacy macros are all verbose bring-up traces; unconditional errors use AC_XCLIPSE_LOGP. */
#define RADV_LOGI(fmt, ...)                                                                        \
   do {                                                                                          \
      if (unlikely(ac_xclipse_log_level() >= 2))                                                  \
         radv_log_msg(fmt, ##__VA_ARGS__);                                                         \
   } while (0)
#define RADV_LOGE(fmt, ...) RADV_LOGI(fmt, ##__VA_ARGS__)
#define RADV_LOGW(fmt, ...) RADV_LOGI(fmt, ##__VA_ARGS__)
#define RADV_LOGD(fmt, ...) RADV_LOGI(fmt, ##__VA_ARGS__)

#endif /* RADV_LOGCAT_H */
