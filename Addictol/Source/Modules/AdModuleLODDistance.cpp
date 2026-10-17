#include <Modules/AdModuleLODDistance.h>
#include <Core/AdUtils.h>

#include <RE/B/BSTimer.h>
#include <RE/L/LOADED_CELL_DATA.h>

namespace Addictol
{
	// Runtime enables re-arm the cell LOD fade gate, flashing its object LOD.
	namespace lodDistanceDetail
	{
		constexpr size_t s_incArmOffset = 0x64;
		constexpr size_t s_decStoreOffset = 0xBB;

		constexpr uint64_t s_fadeArmed = 0xFFFFFFFF;

		[[nodiscard]] static bool IsObjectLODHidden(const RE::LOADED_CELL_DATA* a_data) noexcept
		{
			const auto now = RE::BSTimer::GetSingleton()->QLastTime();
			return now > a_data->visibleDistantFadeInTime && a_data->combinedObjectsAttached.load_unchecked() != 0;
		}

		static void ArmFadeIn(RE::LOADED_CELL_DATA* a_data) noexcept
		{
			// Once hidden, a loaded cell's object LOD stays hidden.
			if (!IsObjectLODHidden(a_data))
				a_data->visibleDistantFadeInTime = s_fadeArmed;
		}

		static void StoreFadeIn(uint64_t a_time, RE::LOADED_CELL_DATA* a_data) noexcept
		{
			if (a_data->visibleDistantFadeInTime == s_fadeArmed)
				a_data->visibleDistantFadeInTime = a_time;
		}
	}

	ModuleLODDistance::ModuleLODDistance() :
		Module("LOD Distance", &bFixesLODDistance)
	{}

	bool ModuleLODDistance::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		using namespace lodDistanceDetail;

		const auto arm = REL::Relocation<uintptr_t>{ RE::ID::TESObjectCELL::IncQueuedDistantRefCount, s_incArmOffset }.address();
		const auto store = REL::Relocation<uintptr_t>{ RE::ID::TESObjectCELL::DecQueuedDistantRefCount, s_decStoreOffset }.address();

		// mov eax, 0FFFFFFFFh; mov [rcx+1F8h], rax
		if (!RELEX::Validate(arm, { 0xB8, 0xFF, 0xFF, 0xFF, 0xFF, 0x48, 0x89, 0x81, 0xF8, 0x01, 0x00, 0x00 }))
		{
			Skip("IncQueuedDistantRefCount bytes do not match; unsupported runtime or another plugin owns the site"sv);
			return false;
		}

		// mov [rdx+1F8h], rcx
		if (!RELEX::Validate(store, { 0x48, 0x89, 0x8A, 0xF8, 0x01, 0x00, 0x00 }))
		{
			Skip("DecQueuedDistantRefCount bytes do not match; unsupported runtime or another plugin owns the site"sv);
			return false;
		}

		auto& trampoline = REL::GetTrampoline();
		trampoline.write_call<5>(arm, ArmFadeIn);
		RELEX::WriteSafeNop(arm + 5, 7);
		trampoline.write_call<5>(store, StoreFadeIn);
		RELEX::WriteSafeNop(store + 5, 2);

		return true;
	}
}
