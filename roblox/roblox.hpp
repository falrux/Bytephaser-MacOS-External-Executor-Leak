#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <sstream>

#include "memory/offsets.h"

class MachProcess;

class InstanceWalker {
public:
  explicit InstanceWalker(const MachProcess &proc) : proc_(proc) {}


  std::string ReadRBXString(uintptr_t address) const {
    // Skidded function creds pluggstiant99
    if (address <= 0x1000)
      return "";

    unsigned char first_byte = 0;
    if (proc_.Read(address, first_byte)) {
      bool is_long = (first_byte & 1) != 0;

      if (is_long) {
        size_t str_size = 0;
        uintptr_t data_ptr = 0;
        proc_.Read(address + 8, str_size);
        proc_.Read(address + 16, data_ptr);

        if (data_ptr > 0x10000 && str_size > 0 && str_size < 10000) {
          std::vector<char> buf(str_size + 1, 0);
          if (proc_.ReadBytes(data_ptr, buf.data(), str_size)) {
            if (isprint((unsigned char)buf[0])) {
              return std::string(buf.data(), str_size);
            }
          }
        }
      } else {
        size_t sso_size = first_byte >> 1;
        if (sso_size > 0 && sso_size <= 22) {
          std::vector<char> buf(sso_size + 1, 0);
          if (proc_.ReadBytes(address + 1, buf.data(), sso_size)) {
            if (isprint((unsigned char)buf[0])) {
              return std::string(buf.data(), sso_size);
            }
          }
        }
      }
    }

    uintptr_t cstr_ptr = 0;
    if (proc_.Read(address, cstr_ptr) && cstr_ptr > 0x1000) {
      char buf[256] = {0};
      if (proc_.ReadBytes(cstr_ptr, buf, 255)) {
        buf[255] = 0;
        if (isprint((unsigned char)buf[0]))
          return std::string(buf);
      }
    }

    {
      char buf[256] = {0};
      if (proc_.ReadBytes(address, buf, 255)) {
        buf[255] = 0;
        if (isprint((unsigned char)buf[0]))
          return std::string(buf);
      }
    }

    return "";
  }

  bool WriteRBXString(uintptr_t address, const std::string &value) const {
    if (address <= 0x1000) return false;
    uintptr_t data_ptr = proc_.Allocate(value.size() + 1);
    if (!data_ptr) return false;
    proc_.WriteBytes(data_ptr, value.data(), value.size() + 1);
    
    uint64_t cap_val = value.size() + 1;
    cap_val = (cap_val & ~1ULL) | 1ULL;
    
    proc_.Write(address, cap_val);
    proc_.Write(address + 8, static_cast<uint64_t>(value.size()));
    proc_.Write(address + 16, data_ptr);
    return true;
  }

  bool SetStringValue(uintptr_t string_value_inst, const std::string& text) const {
       return WriteRBXString(string_value_inst + Offsets::StringValue::Value, text);
  }


  std::string GetInstanceName(uintptr_t inst) const {
    if (!inst)
      return "";
    uintptr_t name_addr = 0;
    if (proc_.Read(inst + Offsets::Instance::Name, name_addr) &&
        name_addr > 0x1000) {
      std::string s = ReadRBXString(name_addr);
      if (!s.empty())
        return s;
    }
    return ReadRBXString(inst + Offsets::Instance::Name);
  }

  std::string GetInstanceClassName(uintptr_t inst) const {
    if (!inst)
      return "";
    uintptr_t class_desc = 0;
    if (proc_.Read(inst + Offsets::Instance::ClassName, class_desc) &&
        class_desc > 0x1000) {
      uintptr_t class_name_addr = 0;
      if (proc_.Read(class_desc + 0x8, class_name_addr) &&
          class_name_addr > 0x1000) {
        std::string s = ReadRBXString(class_name_addr);
        if (!s.empty())
          return s;
      }
      std::string s = ReadRBXString(class_desc);
      if (!s.empty())
        return s;
    }
    return ReadRBXString(inst + Offsets::Instance::ClassName);
  }


  void GetChildrenPointers(uintptr_t inst, uintptr_t &begin_ptr,
                           uintptr_t &end_ptr) const {
    begin_ptr = 0;
    end_ptr = 0;
    if (!inst)
      return;

    uintptr_t children_obj = 0;
    proc_.Read(inst + Offsets::Instance::ChildrenStart, children_obj);

    if (children_obj > 0x1000) {
      proc_.Read(children_obj, begin_ptr);
      proc_.Read(children_obj + 8, end_ptr);
    } else {
      proc_.Read(inst + Offsets::Instance::ChildrenStart, begin_ptr);
      proc_.Read(inst + Offsets::Instance::ChildrenEnd, end_ptr);
    }
  }

  uintptr_t FindFirstChild(uintptr_t inst,
                            const std::string &target_name) const {
    uintptr_t begin_ptr, end_ptr;
    GetChildrenPointers(inst, begin_ptr, end_ptr);
    if (begin_ptr <= 0x1000 || end_ptr <= 0x1000)
      return 0;

    for (uintptr_t it = begin_ptr; it < end_ptr; it += 16) {
      uintptr_t child = 0;
      proc_.Read(it, child);
      if (!child || child < 0x10000)
        continue;
      std::string c_name = GetInstanceName(child);
      if (!c_name.empty() &&
          c_name.find(target_name) != std::string::npos) {
        return child;
      }
    }
    return 0;
  }

  uintptr_t FindFirstChildOfClass(uintptr_t inst,
                                   const std::string &target_class) const {
    uintptr_t begin_ptr, end_ptr;
    GetChildrenPointers(inst, begin_ptr, end_ptr);
    if (begin_ptr <= 0x1000 || end_ptr <= 0x1000)
      return 0;

    for (uintptr_t it = begin_ptr; it < end_ptr; it += 16) {
      uintptr_t child = 0;
      proc_.Read(it, child);
      if (!child || child < 0x10000)
        continue;
      std::string c_class = GetInstanceClassName(child);
      if (!c_class.empty() && c_class == target_class) {
        return child;
      }
    }
    return 0;
  }


  uintptr_t FindFirstChildByPath(uintptr_t dm, const std::string &path) const {
    uintptr_t current = dm;
    std::istringstream stream(path);
    std::string segment;
    bool is_first = true;

    while (std::getline(stream, segment, '/')) {
      if (segment.empty())
        continue;

      uintptr_t next = 0;
      if (is_first) {
        next = FindFirstChildOfClass(current, segment);
        if (!next)
          next = FindFirstChild(current, segment);
        is_first = false;
      } else {
        next = FindFirstChild(current, segment);
        if (!next)
          next = FindFirstChildOfClass(current, segment);
      }

      if (!next) {
        return 0;
      }
      current = next;
    }
    return current;
  }


  void ListChildren(uintptr_t inst, const std::string &) const {
    uintptr_t begin_ptr, end_ptr;
    GetChildrenPointers(inst, begin_ptr, end_ptr);
    if (begin_ptr <= 0x1000 || end_ptr <= 0x1000) {
      return;
    }

    for (uintptr_t it = begin_ptr; it < end_ptr; it += 16) {
      uintptr_t child = 0;
      proc_.Read(it, child);
      if (!child || child < 0x10000)
        continue;
    }
  }


  uintptr_t GetBytecodeOffset(uintptr_t script_inst) const {
    std::string class_name = GetInstanceClassName(script_inst);
    if (class_name == "ModuleScript")
      return Offsets::ModuleScript::Bytecode;
    if (class_name == "LocalScript")
      return Offsets::LocalScript::Bytecode;
    return Offsets::ModuleScript::Bytecode;
  }

  std::vector<uint8_t> ReadBytecode(uintptr_t script_inst) const {
    uintptr_t offsets_to_try[] = {
        GetBytecodeOffset(script_inst),
        Offsets::ModuleScript::Bytecode,
        Offsets::LocalScript::Bytecode,
    };

    for (uintptr_t bytecode_offset : offsets_to_try) {
      if (bytecode_offset == 0)
        continue;

      uintptr_t bc_struct = 0;
      proc_.Read(script_inst + bytecode_offset, bc_struct);
      if (!bc_struct || bc_struct < 0x10000)
        continue;

      int bc_size = 0;
      proc_.Read(bc_struct + Offsets::ByteCode::Size, bc_size);
      if (bc_size <= 0 || bc_size > 10 * 1024 * 1024)
        continue;

      uintptr_t bc_data_ptr = 0;
      proc_.Read(bc_struct + Offsets::ByteCode::Data, bc_data_ptr);
      if (!bc_data_ptr || bc_data_ptr < 0x10000)
        continue;

      std::vector<uint8_t> bytecode(bc_size);
      if (!proc_.ReadBytes(bc_data_ptr, bytecode.data(), bc_size))
        continue;
      return bytecode;
    }

    return {};
  }

  bool WriteBytecode(uintptr_t script_inst, const uint8_t *data,
                     size_t data_size) const {
    uintptr_t bytecode_offset = GetBytecodeOffset(script_inst);

    uintptr_t bc_struct = 0;
    proc_.Read(script_inst + bytecode_offset, bc_struct);
    if (!bc_struct || bc_struct < 0x10000) {
      uintptr_t alt_offset = (bytecode_offset == Offsets::ModuleScript::Bytecode)
                                  ? Offsets::LocalScript::Bytecode
                                  : Offsets::ModuleScript::Bytecode;
      proc_.Read(script_inst + alt_offset, bc_struct);
      if (!bc_struct || bc_struct < 0x10000) {
        bc_struct = proc_.Allocate(0x140);
        if (!bc_struct) {
          return false;
        }
        
        if (!proc_.Write(script_inst + bytecode_offset, bc_struct)) {
          return false;
        }
      } else {
        bytecode_offset = alt_offset;
      }
    }

    uintptr_t old_data_ptr = 0;
    proc_.Read(bc_struct + Offsets::ByteCode::Data, old_data_ptr);

    uintptr_t new_buf = proc_.Allocate(data_size);
    if (!new_buf) {
      return false;
    }

    if (!proc_.WriteBytes(new_buf, data, data_size)) {
      return false;
    }

    if (!proc_.Write(bc_struct + Offsets::ByteCode::Data, new_buf)) {
      return false;
    }

    int new_size = static_cast<int>(data_size);
    if (!proc_.Write(bc_struct + Offsets::ByteCode::Size, new_size)) {
      return false;
    }
    return true;
  }


  struct Context {
    uintptr_t sc = 0;
    uintptr_t dm = 0;
  };

  Context FindScriptContext() const {
    uintptr_t ts_global = proc_.Rebase(Offsets::Addresses::TaskScheduler);
    uintptr_t ts = 0;
    if (!proc_.Read(ts_global, ts) || !ts) {
      return {};
    }

    uintptr_t jobs_begin = 0, jobs_end = 0;
    proc_.Read(ts + Offsets::TaskScheduler::JobsBegin, jobs_begin);
    proc_.Read(ts + Offsets::TaskScheduler::JobsEnd, jobs_end);

    if (!jobs_begin || jobs_end <= jobs_begin) {
      return {};
    }

    for (uintptr_t it = jobs_begin; it < jobs_end; it += 8) {
      uintptr_t job = 0;
      if (!proc_.Read(it, job) || !job || job < 0x10000)
        continue;

      std::string job_name = ReadRBXString(job + Offsets::Job::Name);
      if (job_name != "WaitingHybridScriptsJob")
        continue;

      uintptr_t sc = 0;
      proc_.Read(job + Offsets::WHSJ::ToScriptContext, sc);
      if (!sc) {
        continue;
      }

      uintptr_t dm = 0;
      proc_.Read(sc + Offsets::Instance::Parent, dm);
      if (!dm)
        continue;

      std::string dm_name = GetInstanceName(dm);
      if (dm_name.find("Ugc") != std::string::npos) {
        return {sc, dm};
      }
    }

    return {};
  }

private:
  const MachProcess &proc_;
};