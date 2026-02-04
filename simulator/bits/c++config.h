#include_next <bits/c++config.h>

// See https://github.com/zephyrproject-rtos/zephyr/issues/40023 for why

#ifdef _GLIBCXX_HAVE_LINUX_FUTEX
#undef _GLIBCXX_HAVE_LINUX_FUTEX
#endif
