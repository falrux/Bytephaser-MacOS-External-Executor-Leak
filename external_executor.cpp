#include "memory/offsets.h"
#include "luau/bytecode.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <mach/mach.h>
#include <unistd.h>
#include <signal.h>

#include <CoreGraphics/CoreGraphics.h>

#include "memory/machvm.hpp"
#include "roblox/roblox.hpp"


static bool ReadFile(const std::string &path, std::vector<uint8_t> &out) {
  std::ifstream in(path, std::ios::binary);
  if (!in)
    return false;
  in.seekg(0, std::ios::end);
  auto size = in.tellg();
  if (size < 0)
    return false;
  in.seekg(0, std::ios::beg);
  out.resize(static_cast<size_t>(size));
  if (size > 0 && !in.read(reinterpret_cast<char *>(out.data()), size))
    return false;
  return true;
}

static bool CompileLUAU(const std::string &source,
                                          std::vector<uint8_t> &bytecode,
                                          std::string *error_message = nullptr) {
  auto result = physics_compiler::compile_and_sign(source);
  if (!result.ok) {
    if (error_message) {
      *error_message = result.error;
    }
    return false;
  }

  if (result.bytecode.empty()) {
    if (error_message) {
      *error_message = "compiled bytecode is empty";
    }
    return false;
  }

  bytecode.assign(result.bytecode.begin(), result.bytecode.end());
  return true;
}

static void RobloxKeyPress(pid_t pid, CGKeyCode key) {
  CGEventRef down = CGEventCreateKeyboardEvent(NULL, key, true);
  CGEventRef up = CGEventCreateKeyboardEvent(NULL, key, false);
  if (down && up) {
    CGEventPostToPid(pid, down);
    usleep(12000);
    CGEventPostToPid(pid, up);
  }
  if (down) CFRelease(down);
  if (up) CFRelease(up);
}

static constexpr CGKeyCode kVK_Escape = 53;

int main(int, char **) {
  std::string init_path = "init.lua";
  
  if (access(init_path.c_str(), F_OK) != 0) {
    std::cerr << "init.lua not found\n";
    return 1;
  }

  MachProcess proc;
  std::cout << "External Executor v1\n\n";

  std::cout << "Attaching...\n";

  if (!proc.AttachRoblox()) {
    std::cerr << "Roblox not found\n";
    return 1;
  }

  std::cout << "Attached to " << proc.process_name() << "\n";

  InstanceWalker walker(proc);

  auto ctx = walker.FindScriptContext();
  if (!ctx.sc) {
    std::cerr << "Failed to find scriptctx\n";
    return 1;
  }

  std::vector<uint8_t> init_raw;
  ReadFile(init_path, init_raw);

  std::string init_str(init_raw.begin(), init_raw.end());
  std::string wrapped_init = "script.Parent = nil\ntask.spawn(function()\n" + init_str + "\nend)\nwhile true do task.wait(9e9) end";

  std::vector<uint8_t> bytecode;
  if (!CompileLUAU(wrapped_init, bytecode)) {
    std::cerr << "Failed to compile init.lua\n";
    return 1;
  }
  
  std::cout << "Swapping bytecode...\n";

  uintptr_t AEP_inst = walker.FindFirstChildByPath(ctx.dm, "CoreGui/RobloxGui/Modules/AvatarEditorPrompts");
  walker.WriteBytecode(AEP_inst, bytecode.data(), bytecode.size());

  uintptr_t iscore_addr = AEP_inst + Offsets::Execution::isCoreScript;
  proc.Write(iscore_addr, 0x2);

  uintptr_t requirebp_addr = ctx.sc + Offsets::Execution::RequireBypass;
  proc.Write(requirebp_addr, 0x1);

  uintptr_t plm_inst = walker.FindFirstChildByPath(ctx.dm, "CoreGui/RobloxGui/Modules/PlayerList/PlayerListManager");
  uintptr_t plm_this = plm_inst + 0x8;

  uintptr_t original_plm = 0;
  proc.Read(plm_this, original_plm);
 
  if (!proc.Write(plm_this, AEP_inst)) {
    std::cerr << "Failed to write PLM pointer\n";
    return 69;
  }

  std::cout << "Triggering (esc)...\n";

  RobloxKeyPress(proc.pid(), kVK_Escape);
  usleep(50000);
  RobloxKeyPress(proc.pid(), kVK_Escape);
  usleep(120000);

  proc.Write(plm_this, original_plm);
  proc.Write(plm_this, plm_inst);

  std::cout << "Executed init script.\n";
  return 0;
}