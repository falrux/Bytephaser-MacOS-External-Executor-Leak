#pragma once
#include <cstdint>

namespace Offsets {

	namespace Addresses {
		constexpr uintptr_t TaskScheduler = 0x1063e9280;
	}

	namespace TaskScheduler {
		constexpr uintptr_t JobsBegin = 0xd0;
		constexpr uintptr_t JobsEnd = 0xd8;
	}

	namespace Job {
		constexpr uintptr_t Name = 0x18;
	}

	namespace WHSJ {
		constexpr uintptr_t ToScriptContext = 0x1A0;
	}

	namespace Instance {
		constexpr uintptr_t ClassName = 0x18;
		constexpr uintptr_t Parent = 0x70;
		constexpr uintptr_t ChildrenStart = 0x78;
		constexpr uintptr_t ChildrenEnd = 0x80;
		constexpr uintptr_t Name = 0xb0;
		constexpr uintptr_t StringValue = 0xd0;
	}

	namespace LocalScript {
		constexpr uintptr_t Bytecode = 0x198;
	}

	namespace ModuleScript {
		constexpr uintptr_t Bytecode = 0x140;
	}

	namespace ByteCode {
		constexpr uintptr_t Size = 0x18;
		constexpr uintptr_t Data = 0x20;
	}

	namespace StringValue {
		constexpr uintptr_t Value = 0xd0; // use this for when your're making a bridge, you can communicate between lua init and external through strings
	}

	namespace Execution {
		constexpr uintptr_t isCoreScript = 0x168;
		constexpr uintptr_t RequireBypass = 0x880;
	}

}
