// Ported with permission from my goat doodlum (github.com/doodlum/FO4LightingFixes).
#include <Modules/AdModuleLightPriority.h>
#include <Core/AdUtils.h>

#include <RE/B/BSFadeNode.h>
#include <RE/B/BSLight.h>
#include <RE/B/BSShaderPropertyLightData.h>
#include <RE/N/NiLight.h>
#include <RE/S/ShadowSceneNode.h>

// An object lit by more lights than a pass holds gets the wrong ones.
//
// A pass holds the sun plus four. Candidates are weighed by REL::ID(2318430):
//
//     weight = (|lightPos - bound.center| - bound.radius) / light.radius
//
// so smaller means more strongly lit. The attach sorts descending, and
// CreateActiveLightList scans forward taking the first four, so the slots go to
// the four least influential lights.
//
// Fix: keep the engine's metric and output contract, but pick the most
// influential entries, emitted in list order (sun in slot 0). Lists that already
// fit, and anything unexpected, fall back to the original. The list itself is
// untouched.

namespace Addictol
{
	namespace lightPriorityDetail
	{
		using CreateActiveLightList_t = uint32_t (*)(RE::BSShaderPropertyLightData*, RE::BSLight**, uint32_t, RE::ShadowSceneNode*, bool);

		// Only has to be big enough to rank one node's list.
		constexpr size_t kMaxRank = 64;

		// A non-finite weight would break the comparator's strict weak ordering.
		constexpr float kRankLast = std::numeric_limits<float>::max();

		struct Ranked
		{
			RE::BSLight* light;
			float weight;
			uint32_t index;
		};

		static CreateActiveLightList_t s_createActiveLightList{ nullptr };

		// REL::ID(2318430) recomputed; the engine's weights are freed by now.
		[[nodiscard]] static bool Weigh(const RE::NiLight& a_light, const RE::NiBound& a_bound, float& a_out) noexcept
		{
			const auto radius = a_light.radius.x;
			if (!(radius > 0.0f))
			{
				a_out = kRankLast;
				return true;
			}

			const auto distance = std::sqrt(a_light.world.translate.GetSquaredDistance(a_bound.center));
			const auto weight = (distance - a_bound.fRadius) / radius;
			if (std::isnan(weight))
				return false;

			a_out = std::clamp(weight, -kRankLast, kRankLast);
			return true;
		}

		static uint32_t HKCreateActiveLightList(RE::BSShaderPropertyLightData* a_list, RE::BSLight** a_out,
			uint32_t a_max, RE::ShadowSceneNode* a_scene, bool a_alt) noexcept
		{
			const auto original = [&] {
				return s_createActiveLightList(a_list, a_out, a_max, a_scene, a_alt);
			};

			if (!a_list || !a_out || !a_scene || a_max < 2)
				return original();

			const auto& list = a_list->lightList;
			const auto slots = a_max - 1;  // slot 0 belongs to the sun
			if (list.size() <= slots || list.size() > kMaxRank)
				return original();

			// Every engine caller passes a BSFadeNode's embedded list; non-finite
			// bounds would poison every weight.
			const auto* node = reinterpret_cast<const RE::BSFadeNode*>(
				reinterpret_cast<const std::byte*>(a_list) - offsetof(RE::BSFadeNode, lightData));
			const auto& bound = node->worldBound;
			if (!std::isfinite(bound.center.x) || !std::isfinite(bound.center.y) ||
				!std::isfinite(bound.center.z) || !std::isfinite(bound.fRadius))
				return original();

			std::array<Ranked, kMaxRank> ranked{};
			size_t n = 0;
			for (uint32_t i = 0; i < list.size(); ++i)
			{
				auto* light = list[i];
				// The original lets nulls consume a slot; do not reproduce that.
				if (!light)
					return original();

				const auto* source = light->GetSourceLight();
				if (!source)
					return original();
				if (source->GetAppCulled())
					continue;  // skipped without spending a slot, as the original does

				float weight{};
				if (!Weigh(*source, bound, weight))
					return original();
				ranked[n++] = { light, weight, i };
			}
			if (n <= slots)
				return original();

			std::partial_sort(ranked.begin(), ranked.begin() + slots, ranked.begin() + n,
				[](const Ranked& a_lhs, const Ranked& a_rhs) { return a_lhs.weight < a_rhs.weight; });
			std::sort(ranked.begin(), ranked.begin() + slots,
				[](const Ranked& a_lhs, const Ranked& a_rhs) { return a_lhs.index < a_rhs.index; });

			a_out[0] = a_alt ?
				reinterpret_cast<RE::BSLight*>(a_scene->directionalShadowLight) :
				a_scene->sunLight;
			for (uint32_t i = 0; i < slots; ++i)
				a_out[i + 1] = ranked[i].light;
			return a_max;
		}
	}

	ModuleLightPriority::ModuleLightPriority() :
		Module("Light Priority", &bFixesLightPriority)
	{}

	bool ModuleLightPriority::DoQuery() const noexcept
	{
		if (IsModDLLPresent("LightingFixes.dll"))
		{
			Skip("standalone 'LightingFixes.dll' is installed"sv);
			return false;
		}

		return true;
	}

	bool ModuleLightPriority::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		// CreateActiveLightList: OG 0x28B9CF0, NG 0x20E3430, AE 0x2240540.
		// Identical on all three: sun at ssn+0x1F8/0x208, culled flag at light+0x108 bit 0.
		const auto target = RE::ID::BSShaderPropertyLightData::CreateActiveLightList.address();
		const auto original = RELEX::TryDetourJump(target,
			reinterpret_cast<uintptr_t>(&lightPriorityDetail::HKCreateActiveLightList), {
				0x48, 0x89, 0x5C, 0x24, 0x08, 0x80, 0x7C, 0x24, 0x28, 0x00, 0x45, 0x8B, 0xD8, 0x48, 0x8B, 0xDA,
				0x4C, 0x8B, 0xD1, 0x75, 0x09, 0x49, 0x8B, 0x81, 0xF8, 0x01, 0x00, 0x00, 0xEB, 0x07, 0x49, 0x8B,
				0x81, 0x08, 0x02, 0x00, 0x00, 0x48, 0x89, 0x02
			});
		if (!original)
		{
			Skip("unexpected bytes or conflicting patch"sv);
			return false;
		}

		lightPriorityDetail::s_createActiveLightList = reinterpret_cast<lightPriorityDetail::CreateActiveLightList_t>(original);
		return true;
	}
}
