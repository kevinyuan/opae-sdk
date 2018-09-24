// Copyright(c) 2017, Intel Corporation
//
// Redistribution  and  use  in source  and  binary  forms,  with  or  without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of  source code  must retain the  above copyright notice,
//   this list of conditions and the following disclaimer.
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
// * Neither the name  of Intel Corporation  nor the names of its contributors
//   may be used to  endorse or promote  products derived  from this  software
//   without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING,  BUT NOT LIMITED TO,  THE
// IMPLIED WARRANTIES OF  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED.  IN NO EVENT  SHALL THE COPYRIGHT OWNER  OR CONTRIBUTORS BE
// LIABLE  FOR  ANY  DIRECT,  INDIRECT,  INCIDENTAL,  SPECIAL,  EXEMPLARY,  OR
// CONSEQUENTIAL  DAMAGES  (INCLUDING,  BUT  NOT LIMITED  TO,  PROCUREMENT  OF
// SUBSTITUTE GOODS OR SERVICES;  LOSS OF USE,  DATA, OR PROFITS;  OR BUSINESS
// INTERRUPTION)  HOWEVER CAUSED  AND ON ANY THEORY  OF LIABILITY,  WHETHER IN
// CONTRACT,  STRICT LIABILITY,  OR TORT  (INCLUDING NEGLIGENCE  OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,  EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
/*
 * test-system.cpp
 */

#include "test_system.h"
#include <iostream>
#include <stdarg.h>
#include <unistd.h>
#include <algorithm>
#include <cstring>
#include <fstream>
#include "c_test_system.h"
#include "test_utils.h"

#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#include <dlfcn.h>
#include <sys/stat.h>

void *__builtin_return_address(unsigned level);

// hijack malloc
static bool _invalidate_malloc = false;
static uint32_t _invalidate_malloc_after = 0;
static const char *_invalidate_malloc_when_called_from = nullptr;
void *malloc(size_t size) {
  if (_invalidate_malloc) {
    if (!_invalidate_malloc_when_called_from) {
      if (!_invalidate_malloc_after) {
        _invalidate_malloc = false;
        return nullptr;
      }

      --_invalidate_malloc_after;

    } else {
      void *caller = __builtin_return_address(0);
      int res;
      Dl_info info;

      dladdr(caller, &info);
      if (!info.dli_sname)
        res = 1;
      else
        res = strcmp(info.dli_sname, _invalidate_malloc_when_called_from);

      if (!_invalidate_malloc_after && !res) {
        _invalidate_malloc = false;
        _invalidate_malloc_when_called_from = nullptr;
        return nullptr;
      } else if (!res)
        --_invalidate_malloc_after;
    }
  }
  return __libc_malloc(size);
}

// hijack calloc
static bool _invalidate_calloc = false;
static uint32_t _invalidate_calloc_after = 0;
static const char * _invalidate_calloc_when_called_from = nullptr;
void *calloc(size_t nmemb, size_t size) {
  if (_invalidate_calloc) {

    if (!_invalidate_calloc_when_called_from) {

        if (!_invalidate_calloc_after) {
          _invalidate_calloc = false;
          return nullptr;
        }

        --_invalidate_calloc_after;

    } else {
        void *caller = __builtin_return_address(0);
        int res;
        Dl_info info;

        dladdr(caller, &info);
        if (!info.dli_sname)
            res = 1;
        else
            res = strcmp(info.dli_sname, _invalidate_calloc_when_called_from);

        if (!_invalidate_calloc_after && !res) {
          _invalidate_calloc = false;
          _invalidate_calloc_when_called_from = nullptr;
          return nullptr;
        } else if (!res)
            --_invalidate_calloc_after;

    }

  }
  return __libc_calloc(nmemb, size);
}

namespace opae {
namespace testing {

static const char *dev_pattern =
    R"regex(/dev/intel-fpga-\(fme\|port\)\.\([0-9]\+\))regex";
static const char *sysclass_pattern =
    R"regex(/sys/class/fpga/intel-fpga-dev\.\([0-9]\+\))regex";

mock_object::mock_object(const std::string &devpath,
                         const std::string &sysclass, uint32_t device_id,
                         type_t type)
    : devpath_(devpath),
      sysclass_(sysclass),
      device_id_(device_id),
      type_(type) {}

int mock_fme::ioctl(int request, va_list argp) {
  (void)request;
  (void)argp;
  return 0;
}

int mock_port::ioctl(int request, va_list argp) {
  (void)request;
  (void)argp;
  return 0;
}

#define ASSERT_FN(fn)                              \
  do {                                             \
    if (fn == nullptr) {                           \
      throw std::runtime_error(#fn " not loaded"); \
    }                                              \
  } while (false);

test_device test_device::unknown() {
  return test_device{.fme_guid = "C544CE5C-F630-44E1-8551-59BD87AF432E",
                     .afu_guid = "C544CE5C-F630-44E1-8551-59BD87AF432E",
                     .segment = 0x1919,
                     .bus = 0x0A,
                     .device = 9,
                     .function = 5,
                     .socket_id = 9,
                     .num_slots = 9,
                     .bbs_id = 9,
                     .bbs_version = {0xFF, 0xFF, 0xFF},
                     .state = FPGA_ACCELERATOR_ASSIGNED,
                     .num_mmio = 0,
                     .num_interrupts = 0xf,
                     .fme_object_id = 9,
                     .port_object_id = 9,
                     .vendor_id = 0x1234,
                     .device_id = 0x1234,
                     .fme_num_errors = 0x1234,
                     .port_num_errors = 0x1234};
}

typedef std::map<std::string, test_platform> platform_db;

static platform_db PLATFORMS = {
    {"skx-p-1s",
     test_platform{.mock_sysfs = "mock_sys_tmp-1socket-nlb0.tar.gz",
                   .devices = {test_device{
                       .fme_guid = "1A422218-6DBA-448E-B302-425CBCDE1406",
                       .afu_guid = "D8424DC4-A4A3-C413-F89E-433683F9040B",
                       .segment = 0x0,
                       .bus = 0x5e,
                       .device = 0,
                       .function = 0,
                       .socket_id = 0,
                       .num_slots = 1,
                       .bbs_id = 0x63000023b637277,
                       .bbs_version = {6, 3, 0},
                       .state = FPGA_ACCELERATOR_UNASSIGNED,
                       .num_mmio = 0x2,
                       .num_interrupts = 0,
                       .fme_object_id = 0xf500000,
                       .port_object_id = 0xf400000,
                       .vendor_id = 0x8086,
                       .device_id = 0xbcc0,
                       .fme_num_errors = 9,
                       .port_num_errors = 3}}}}};

test_platform test_platform::get(const std::string &key) {
  return PLATFORMS[key];
}

bool test_platform::exists(const std::string &key) {
  return PLATFORMS.find(key) != PLATFORMS.end();
}

std::vector<std::string> test_platform::keys(bool sorted) {
  std::vector<std::string> keys(PLATFORMS.size());
  std::transform(
      PLATFORMS.begin(), PLATFORMS.end(), keys.begin(),
      [](const std::pair<std::string, test_platform> &it) { return it.first; });
  if (sorted) {
    std::sort(keys.begin(), keys.end());
  }

  return keys;
}

test_system *test_system::instance_ = nullptr;

test_system::test_system() : root_("") {
  open_ = (open_func)dlsym(RTLD_NEXT, "open");
  open_create_ = (open_create_func)open_;
  fopen_ = (fopen_func)dlsym(RTLD_NEXT, "fopen");
  close_ = (close_func)dlsym(RTLD_NEXT, "close");
  ioctl_ = (ioctl_func)dlsym(RTLD_NEXT, "ioctl");
  opendir_ = (opendir_func)dlsym(RTLD_NEXT, "opendir");
  readlink_ = (readlink_func)dlsym(RTLD_NEXT, "readlink");
  xstat_ = (__xstat_func)dlsym(RTLD_NEXT, "__xstat");
  lstat_ = (__xstat_func)dlsym(RTLD_NEXT, "__lxstat");
  scandir_ = (scandir_func)dlsym(RTLD_NEXT, "scandir");
}

test_system *test_system::instance() {
  if (test_system::instance_ == nullptr) {
    test_system::instance_ = new test_system();
  }
  return test_system::instance_;
}

std::string test_system::prepare_syfs(const test_platform &platform) {
  std::string tmpsysfs = "tmpsysfs-XXXXXX";
  if (platform.mock_sysfs != nullptr) {
    tmpsysfs = mkdtemp(const_cast<char *>(tmpsysfs.c_str()));
    std::string cmd = "tar xzf " + std::string(platform.mock_sysfs) + " -C " +
                      tmpsysfs + " --strip 1";
    std::system(cmd.c_str());
    root_ = tmpsysfs;
    return tmpsysfs;
  }
  return "/";
}

void test_system::remove_sysfs() {
  if (root_.find("tmpsysfs") != std::string::npos) {
    struct stat st;
    if (stat(root_.c_str(), &st)) {
      std::cerr << "Error stat'ing root dir (" << root_ << "):" << strerror(errno) << "\n";
      return;
    }
    if (S_ISDIR(st.st_mode)){
      auto cmd = "rm -rf " + root_;
      std::system(cmd.c_str());
    }
  }
}


void test_system::set_root(const char *root) { root_ = root; }

std::string test_system::get_sysfs_path(const std::string &src) {
  auto it = registered_files_.find(src);
  if (it != registered_files_.end()) {
    return it->second;
  }
  if (src.find("/sys") == 0 || src.find("/dev/intel-fpga") == 0) {
    if (!root_.empty() && root_.size() > 1) {
      return root_ + src;
    }
  }
  return src;
}

void test_system::initialize() {
  ASSERT_FN(open_);
  ASSERT_FN(open_create_);
  ASSERT_FN(fopen_);
  ASSERT_FN(close_);
  ASSERT_FN(ioctl_);
  ASSERT_FN(readlink_);
  ASSERT_FN(xstat_);
  ASSERT_FN(lstat_);
  ASSERT_FN(scandir_);
}

void test_system::finalize() {
  for (auto kv : fds_) {
    if (kv.second) {
      delete kv.second;
      kv.second = nullptr;
    }
  }
  remove_sysfs();
  root_ = "";
  fds_.clear();
  for (auto kv : registered_files_) {
    unlink(kv.second.c_str());
  }
}

bool test_system::register_ioctl_handler(int request, ioctl_handler_t h) {
  bool alhready_registered =
      ioctl_handlers_.find(request) != ioctl_handlers_.end();
  ioctl_handlers_[request] = h;
  return alhready_registered;
}

FILE *test_system::register_file(const std::string &path) {
  auto it = registered_files_.find(path);
  if (it == registered_files_.end()) {
    registered_files_[path] =
        "/tmp/testfile" + std::to_string(registered_files_.size());
  }

  auto fptr = fopen(path.c_str(), "w+");
  return fptr;
}

uint32_t get_device_id(const std::string &sysclass) {
  uint32_t res(0);
  std::ifstream fs;
  fs.open(sysclass + "/device/device");
  if (fs.is_open()) {
    std::string line;
    std::getline(fs, line);
    fs.close();
    return std::stoul(line, 0, 16);
  }
  return res;
}

int test_system::open(const std::string &path, int flags) {
  std::string syspath = get_sysfs_path(path);
  int fd = open_(syspath.c_str(), flags);
  auto r1 = regex<>::create(sysclass_pattern);
  auto r2 = regex<>::create(dev_pattern);
  match_t::ptr_t m;

  // check if we are opening a driver attribute file
  // or a device file to save the fd in an internal map
  // this can be used later, (especially in ioctl)
  if (r1 && (m = r1->match(path))) {
    // path matches /sys/class/fpga/intel-fpga-dev\..*
    // we are opening a driver attribute file
    auto sysclass_path = m->group(0);
    auto device_id = get_device_id(get_sysfs_path(sysclass_path));
    fds_[fd] = new mock_object(path, sysclass_path, device_id);
  } else if (r2 && (m = r2->match(path))) {
    // path matches /dev/intel-fpga-(fme|port)\..*
    // we are opening a device
    auto sysclass_path = "/sys/class/fpga/intel-fpga-dev." + m->group(2);
    auto device_id = get_device_id(get_sysfs_path(sysclass_path));
    if (m->group(1) == "fme") {
      fds_[fd] = new mock_fme(path, sysclass_path, device_id);
    } else if (m->group(1) == "port") {
      fds_[fd] = new mock_port(path, sysclass_path, device_id);
    }
  }
  return fd;
}

int test_system::open(const std::string &path, int flags, mode_t mode) {
  std::string syspath = get_sysfs_path(path);
  int fd = open_create_(syspath.c_str(), flags, mode);
  if (syspath.find(root_) == 0) {
    std::map<int, mock_object *>::iterator it = fds_.find(fd);
    if (it != fds_.end())
        delete it->second;          
    fds_[fd] = new mock_object(path, "", 0);
  }
  return fd;
}

FILE *test_system::fopen(const std::string &path, const std::string &mode) {
  std::string syspath = get_sysfs_path(path);
  return fopen_(syspath.c_str(), mode.c_str());
}

int test_system::close(int fd)
{
  std::map<int, mock_object *>::iterator it = fds_.find(fd);
  if (it != fds_.end()) {
    delete it->second;
    fds_.erase(it);
  }
  return close_(fd);
}

int test_system::ioctl(int fd, unsigned long request, va_list argp) {
  auto mock_it = fds_.find(fd);
  if (mock_it == fds_.end()) {
    char *arg = va_arg(argp, char *);
    return ioctl_(fd, request, arg);
  }

  auto handler_it = ioctl_handlers_.find(request);
  if (handler_it != ioctl_handlers_.end()) {
    return handler_it->second(mock_it->second, request, argp);
  }
  return mock_it->second->ioctl(request, argp);
}

DIR *test_system::opendir(const char *path) {
  std::string syspath = get_sysfs_path(path);
  return opendir_(syspath.c_str());
}

ssize_t test_system::readlink(const char *path, char *buf, size_t bufsize) {
  std::string syspath = get_sysfs_path(path);
  return readlink_(syspath.c_str(), buf, bufsize);
}

int test_system::xstat(int ver, const char *path, struct stat *buf) {
  std::string syspath = get_sysfs_path(path);
  return xstat_(ver, syspath.c_str(), buf);
}

int test_system::lstat(int ver, const char *path, struct stat *buf) {
  std::string syspath = get_sysfs_path(path);
  return lstat_(ver, syspath.c_str(), buf);
}

int test_system::scandir(const char *dirp, struct dirent ***namelist,
                         filter_func filter, compare_func cmp) {
  std::string syspath = get_sysfs_path(dirp);
  return scandir_(syspath.c_str(), namelist, filter, cmp);
}

void test_system::invalidate_malloc(uint32_t after,
                                    const char *when_called_from) {
  _invalidate_malloc = true;
  _invalidate_malloc_after = after;
  _invalidate_malloc_when_called_from = when_called_from;
}

void test_system::invalidate_calloc(uint32_t after, const char *when_called_from) {
  _invalidate_calloc = true;
  _invalidate_calloc_after = after;
  _invalidate_calloc_when_called_from = when_called_from;
}

}  // end of namespace testing
}  // end of namespace opae

// C functions

int opae_test_open(const char *path, int flags) {
  return opae::testing::test_system::instance()->open(path, flags);
}

int opae_test_open_create(const char *path, int flags, mode_t mode) {
  return opae::testing::test_system::instance()->open(path, flags, mode);
}

FILE *opae_test_fopen(const char *path, const char *mode) {
  return opae::testing::test_system::instance()->fopen(path, mode);
}

int opae_test_close(int fd) {
  return opae::testing::test_system::instance()->close(fd);
}

int opae_test_ioctl(int fd, unsigned long request, va_list argp) {
  return opae::testing::test_system::instance()->ioctl(fd, request, argp);
}

DIR *opae_test_opendir(const char *name) {
  return opae::testing::test_system::instance()->opendir(name);
}

ssize_t opae_test_readlink(const char *path, char *buf, size_t bufsize) {
  return opae::testing::test_system::instance()->readlink(path, buf, bufsize);
}

int opae_test_xstat(int ver, const char *path, struct stat *buf) {
  return opae::testing::test_system::instance()->xstat(ver, path, buf);
}

int opae_test_lstat(int ver, const char *path, struct stat *buf) {
  return opae::testing::test_system::instance()->lstat(ver, path, buf);
}

int opae_test_scandir(const char *dirp, struct dirent ***namelist,
                      filter_func filter, compare_func cmp) {
  return opae::testing::test_system::instance()->scandir(dirp, namelist, filter,
                                                         cmp);
}
