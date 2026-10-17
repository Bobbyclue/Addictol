#include <Modules/AdModuleInteriorNavCut.h>
#include <Core/AdUtils.h>

#include <RE/B/BGSEncounterZone.h>
#include <RE/D/DynamicNavmesh.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/T/TESCellAttachDetachEvent.h>
#include <RE/T/TESDataHandler.h>
#include <RE/T/TESObjectCELL.h>
#include <RE/T/TESObjectREFR.h>
#include <RE/W/Workshop.h>

namespace Addictol
{
	// Interior detach keeps workshop navmesh cuts, so they cut every interior.
	namespace interiorNavCutDetail
	{
		[[nodiscard]] static bool UpdateNavmeshCut(RE::TESObjectREFR& a_ref, bool a_attached)
		{
			auto* cell = a_ref.parentCell;
			if (!cell || !cell->IsInterior() || !RE::Workshop::IsWorkshopItem(a_ref))
				return false;

			auto* zone = cell->GetEncounterZone();
			if (!zone || !zone->IsWorkshop())
				return false;

			a_ref.UpdateDynamicNavmesh(a_attached && !a_ref.IsDeleted() && !a_ref.IsDisabled());
			return true;
		}

		static void QueueRebuild()
		{
			static std::atomic_bool queued{ false };
			if (queued.exchange(true))
				return;

			F4SE::GetTaskInterface()->AddTask([] {
				queued = false;
				if (auto* navmesh = RE::DynamicNavmesh::GetSingleton())
					navmesh->ForceUpdate();
			});
		}

		class CellAttachDetachSink :
			public RE::BSTEventSink<RE::TESCellAttachDetachEvent>
		{
		public:
			[[nodiscard]] static CellAttachDetachSink* GetSingleton()
			{
				static CellAttachDetachSink singleton;
				return std::addressof(singleton);
			}

			RE::BSEventNotifyControl ProcessEvent(const RE::TESCellAttachDetachEvent& a_event,
				RE::BSTEventSource<RE::TESCellAttachDetachEvent>*) override
			{
				if (auto* ref = a_event.refr.get(); ref && UpdateNavmeshCut(*ref, a_event.isAttaching))
					QueueRebuild();

				return RE::BSEventNotifyControl::kContinue;
			}
		};

		// Loading a save re-adds the cuts of every saved interior workshop item.
		static void ResetInteriorCuts()
		{
			auto* dataHandler = RE::TESDataHandler::GetSingleton();
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!dataHandler || !player)
				return;

			bool changed = false;
			for (auto* cell : dataHandler->interiorCells)
			{
				if (!cell)
					continue;

				std::vector<RE::NiPointer<RE::TESObjectREFR>> refs;
				{
					const RE::BSAutoLock lock{ cell->spinLock };
					refs.assign(cell->references.begin(), cell->references.end());
				}

				const bool attached = cell == player->parentCell;
				for (auto& ref : refs)
				{
					if (ref)
						changed |= UpdateNavmeshCut(*ref, attached);
				}
			}

			if (changed)
				QueueRebuild();
		}
	}

	ModuleInteriorNavCut::ModuleInteriorNavCut() :
		Module("Interior NavCut", &bFixesInteriorNavCut, { F4SE::MessagingInterface::kPostLoadGame })
	{}

	bool ModuleInteriorNavCut::DoQuery() const noexcept
	{
		if (IsModDLLPresent("Interior-NavCut-Fix.dll"))
		{
			Skip("standalone 'Interior-NavCut-Fix.dll' is installed"sv);
			return false;
		}

		return true;
	}

	bool ModuleInteriorNavCut::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (a_msg && a_msg->type == F4SE::MessagingInterface::kGameDataReady)
			RE::TESCellAttachDetachEvent::GetEventSource()->RegisterSink(interiorNavCutDetail::CellAttachDetachSink::GetSingleton());

		return true;
	}

	bool ModuleInteriorNavCut::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (a_msg && a_msg->type == F4SE::MessagingInterface::kPostLoadGame)
			interiorNavCutDetail::ResetInteriorCuts();

		return true;
	}
}
