// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/sys_info.h"

#include <sys/types.h>
#include <sys/sysctl.h>

#include "base/logging.h"

namespace base {

int64 SysInfo::AmountOfPhysicalMemory() {
  int pages, page_size;
  size_t size = sizeof(pages);
  sysctlbyname("vm.stats.vm.v_page_count", &pages, &size, NULL, 0);
  sysctlbyname("vm.stats.vm.v_page_size", &page_size, &size, NULL, 0);
  if (pages == -1 || page_size == -1) {
    NOTREACHED();
    return 0;
  }
  return static_cast<int64>(pages) * page_size;
}

// static
int64 SysInfo::AmountOfAvailablePhysicalMemory() {
  int available_pages, page_size;
  size_t size = sizeof(available_pages);
  sysctlbyname("vm.stats.vm.v_free_count", &available_pages, &size, NULL, 0);
  sysctlbyname("vm.stats.vm.v_page_size", &page_size, &size, NULL, 0);
  if (available_pages == -1 || page_size == -1) {
    NOTREACHED();
    return 0;
  }
  return static_cast<int64>(available_pages) * page_size;
}

// static
size_t SysInfo::MaxSharedMemorySize() {
  size_t limit;
  size_t size = sizeof(limit);
  if (sysctlbyname("kern.ipc.shmmax", &limit, &size, NULL, 0) < 0) {
    NOTREACHED();
    return 0;
  }
  return limit;
}

// static
std::string SysInfo::CPUModelName() {
  int mib[] = { CTL_HW, HW_MODEL };
  char name[256];
  size_t size = arraysize(name);
  if (sysctl(mib, arraysize(mib), &name, &size, NULL, 0) == 0)
    return name;
  return std::string();
}

int SysInfo::NumberOfProcessors() {
  int mib[] = { CTL_HW, HW_NCPU };
  int ncpu;
  size_t size = sizeof(ncpu);
  if (sysctl(mib, arraysize(mib), &ncpu, &size, NULL, 0) == -1) {
    NOTREACHED();
    return 1;
  }
  return ncpu;
}

}  // namespace base
