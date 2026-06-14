#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach-o/dyld.h>
#include <mach-o/dyld_images.h>
#include <mach-o/loader.h>
#include <libproc.h>

class MachProcess {
public:
  MachProcess() = default;
  ~MachProcess() { Detach(); }

  MachProcess(const MachProcess &) = delete;
  MachProcess &operator=(const MachProcess &) = delete;


  bool AttachRoblox() {
    pid_t pid = FindPidByName("RobloxPlayer");
    if (pid != 0) {
      return Attach(pid);
    }
    return false;
  }

  bool Attach(pid_t pid) {
    pid_ = pid;
    kern_return_t kr = task_for_pid(mach_task_self(), pid, &task_);
    if (kr != KERN_SUCCESS) {
      task_ = MACH_PORT_NULL;
      return false;
    }

    if (!getBaseAddress()) {
      Detach();
      return false;
    }

    char name_buf[256] = {0};
    if (proc_name(pid_, name_buf, sizeof(name_buf)) > 0) {
      process_name_ = name_buf;
    } else {
      process_name_ = "RobloxPlayer";
    }

    return true;
  }

  void Detach() {
    if (task_ != MACH_PORT_NULL) {
      mach_port_deallocate(mach_task_self(), task_);
      task_ = MACH_PORT_NULL;
    }
    pid_ = 0;
    base_ = 0;
    slide_ = 0;
    process_name_.clear();
  }

  bool IsAttached() const { return task_ != MACH_PORT_NULL; }


  template <typename T> bool Read(uintptr_t addr, T &out) const {
    return ReadBytes(addr, &out, sizeof(T));
  }

  bool ReadBytes(uintptr_t addr, void *buf, size_t size) const {
    if (!task_ || !addr || !size)
      return false;
    mach_vm_size_t out_size = size;
    kern_return_t kr = mach_vm_read_overwrite(
        task_, (mach_vm_address_t)addr, (mach_vm_size_t)size,
        (mach_vm_address_t)buf, &out_size);
    return kr == KERN_SUCCESS;
  }


  template <typename T> bool Write(uintptr_t addr, const T &value) const {
    return WriteBytes(addr, &value, sizeof(T));
  }

  bool WriteBytes(uintptr_t addr, const void *buf, size_t size) const {
    if (!task_ || !addr || !size)
      return false;
    kern_return_t kr = mach_vm_write(task_, (mach_vm_address_t)addr,
                                     (vm_offset_t)buf,
                                     (mach_msg_type_number_t)size);
    return kr == KERN_SUCCESS;
  }


  uintptr_t Allocate(size_t size) const {
    if (!task_ || !size)
      return 0;
    mach_vm_address_t addr = 0;
    kern_return_t kr =
        mach_vm_allocate(task_, &addr, (mach_vm_size_t)size, VM_FLAGS_ANYWHERE);
    if (kr != KERN_SUCCESS)
      return 0;
    mach_vm_protect(task_, addr, (mach_vm_size_t)size, FALSE,
                    VM_PROT_READ | VM_PROT_WRITE);
    return (uintptr_t)addr;
  }


  uintptr_t Rebase(uintptr_t preferred_va) const {
    return preferred_va + slide_;
  }

  pid_t pid() const { return pid_; }
  const std::string &process_name() const { return process_name_; }
  uintptr_t base() const { return base_; }
  intptr_t slide() const { return slide_; }

private:
  static pid_t FindPidByName(const char *target_name) {
    int pids[8192];
    int byte_count = proc_listallpids(pids, sizeof(pids));
    int count = byte_count / sizeof(int);

    pid_t best_pid = 0;
    for (int i = 0; i < count; ++i) {
      if (pids[i] <= 0)
        continue;
      char name_buf[256] = {0};
      if (proc_name(pids[i], name_buf, sizeof(name_buf)) > 0) {
        if (strcmp(target_name, name_buf) == 0) {
          best_pid = pids[i];
        }
      }
    }
    return best_pid;
  }

  bool getBaseAddress() {
    struct task_dyld_info dyld_info;
    mach_msg_type_number_t count = TASK_DYLD_INFO_COUNT;
    kern_return_t kr =
        task_info(task_, TASK_DYLD_INFO, (task_info_t)&dyld_info, &count);
    if (kr != KERN_SUCCESS)
      return false;

    struct dyld_all_image_infos all_infos;
    if (!ReadBytes(dyld_info.all_image_info_addr, &all_infos,
                   sizeof(all_infos)))
      return false;

    if (all_infos.infoArrayCount == 0)
      return false;

    size_t entry_count = all_infos.infoArrayCount;
    if (entry_count > 4096)
      entry_count = 4096;

    std::vector<struct dyld_image_info> entries(entry_count);
    if (!ReadBytes((uintptr_t)all_infos.infoArray, entries.data(),
                   entry_count * sizeof(struct dyld_image_info)))
      return false;

    for (size_t i = 0; i < entry_count; ++i) {
      uintptr_t addr = (uintptr_t)entries[i].imageLoadAddress;
      uintptr_t path_ptr = (uintptr_t)entries[i].imageFilePath;
      if (addr < 0x1000 || path_ptr < 0x1000)
        continue;

      char path_buf[512] = {0};
      if (!ReadBytes(path_ptr, path_buf, 511))
        continue;
      path_buf[511] = 0;

      std::string path(path_buf);
      if (path.find("Roblox") == std::string::npos &&
          path.find("roblox") == std::string::npos)
        continue;

      if (path.find(".dylib") != std::string::npos ||
          path.find(".framework") != std::string::npos)
        continue;

      struct mach_header_64 header;
      if (!ReadBytes(addr, &header, sizeof(header)))
        continue;
      if (header.magic != MH_MAGIC_64)
        continue;

      base_ = addr;
      slide_ = getSlideFromHeader(addr);
      return true;
    }

    for (size_t i = 0; i < entry_count; ++i) {
      uintptr_t addr = (uintptr_t)entries[i].imageLoadAddress;
      if (addr < 0x1000)
        continue;

      struct mach_header_64 header;
      if (!ReadBytes(addr, &header, sizeof(header)))
        continue;

      if (header.magic == MH_MAGIC_64 && header.filetype == MH_EXECUTE &&
          addr < 0x300000000ULL) {
        base_ = addr;
slide_ = getSlideFromHeader(addr);
        return true;
      }
    }

    return false;
  }

  intptr_t getSlideFromHeader(uintptr_t header_addr) {
    struct mach_header_64 header;
    if (!ReadBytes(header_addr, &header, sizeof(header)))
      return (intptr_t)(header_addr - 0x100000000ULL);

    uintptr_t cmd_addr = header_addr + sizeof(struct mach_header_64);
    for (uint32_t i = 0; i < header.ncmds; ++i) {
      struct load_command lc;
      if (!ReadBytes(cmd_addr, &lc, sizeof(lc)))
        break;

      if (lc.cmd == LC_SEGMENT_64) {
        struct segment_command_64 seg;
        if (ReadBytes(cmd_addr, &seg, sizeof(seg))) {
          char segname[17] = {0};
          memcpy(segname, seg.segname, 16);
          if (strcmp(segname, "__TEXT") == 0) {
            return (intptr_t)(header_addr - seg.vmaddr);
          }
        }
      }
      cmd_addr += lc.cmdsize;
    }

    return (intptr_t)(header_addr - 0x100000000ULL);
  }

  mach_port_t task_ = MACH_PORT_NULL;
  pid_t pid_ = 0;
  uintptr_t base_ = 0;
  intptr_t slide_ = 0;
  std::string process_name_;
};