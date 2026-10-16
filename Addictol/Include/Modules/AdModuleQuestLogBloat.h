#pragma once

#include <Core/AdModule.h>

namespace Addictol
{
	class ModuleQuestLogBloat :
		public Module
	{
	public:
		ModuleQuestLogBloat();
		virtual ~ModuleQuestLogBloat() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}
