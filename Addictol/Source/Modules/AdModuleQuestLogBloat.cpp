#include <Modules/AdModuleQuestLogBloat.h>
#include <Core/AdUtils.h>

namespace Addictol
{
	namespace questLogBloatDetail
	{
		// AddQuestLogEntry +0x6C, item already logged
		inline constexpr std::initializer_list<uint8_t> kAddEntryOG{
			0x0F, 0xB6, 0x88, 0xF4, 0x00, 0x00, 0x00,	// movzx ecx, byte ptr [rax+0F4h]
			0xC0, 0xE9, 0x03,							// shr cl, 3
			0xF6, 0xC1, 0x01,							// test cl, 1
			0x0F, 0x84, 0xB1, 0x00, 0x00, 0x00 };		// je +0x130 ; return false
		inline constexpr std::initializer_list<uint8_t> kAddEntryNG{
			0x0F, 0xB6, 0x88, 0xF4, 0x00, 0x00, 0x00,	// movzx ecx, byte ptr [rax+0F4h]
			0xC0, 0xE9, 0x03,							// shr cl, 3
			0xF6, 0xC1, 0x01,							// test cl, 1
			0x0F, 0x84, 0x9D, 0x00, 0x00, 0x00 };		// je +0x11C ; return false

		// Repeatable stages skip the duplicate node but keep the nextQuest check.
		inline constexpr std::initializer_list<uint8_t> kAddEntryPatchOG{
			0xF6, 0x80, 0xF4, 0x00, 0x00, 0x00, 0x08,	// test byte ptr [rax+0F4h], 8
			0x0F, 0x84, 0xB7, 0x00, 0x00, 0x00,			// je +0x130 ; return false
			0xEB, 0x7E,									// jmp +0xF9 ; post-insert
			0x0F, 0x1F, 0x40, 0x00 };					// nop
		inline constexpr std::initializer_list<uint8_t> kAddEntryPatchNG{
			0xF6, 0x80, 0xF4, 0x00, 0x00, 0x00, 0x08,	// test byte ptr [rax+0F4h], 8
			0x0F, 0x84, 0xA3, 0x00, 0x00, 0x00,			// je +0x11C ; return false
			0xEB, 0x6A,									// jmp +0xE5 ; post-insert
			0x0F, 0x1F, 0x40, 0x00 };					// nop

		// LoadGame +0x943 (OG) / +0xD1A (NG/AE), questLog tail walk
		inline constexpr std::initializer_list<uint8_t> kLoadGameOG{
			0x4C, 0x39, 0x6B, 0x08,						// cmp [rbx+8], r13
			0x48, 0x8D, 0x4B, 0x08,						// lea rcx, [rbx+8]
			0x74, 0x10,									// je +0x95D ; tail
			0x0F, 0x1F, 0x00,							// nop
			0x48, 0x8B, 0x19,							// mov rbx, [rcx]
			0x4C, 0x39, 0x6B, 0x08,						// cmp [rbx+8], r13
			0x48, 0x8D, 0x4B, 0x08,						// lea rcx, [rbx+8]
			0x75, 0xF3 };								// jne +0x950
		inline constexpr std::initializer_list<uint8_t> kLoadGameNG{
			0x48, 0x8B, 0x4B, 0x08,						// mov rcx, [rbx+8]
			0x48, 0x85, 0xC9,							// test rcx, rcx
			0x74, 0x0C,									// je +0xD2F ; tail
			0x48, 0x8B, 0xD9,							// mov rbx, rcx
			0x48, 0x8B, 0x49, 0x08,						// mov rcx, [rcx+8]
			0x48, 0x85, 0xC9,							// test rcx, rcx
			0x75, 0xF4 };								// jne +0xD23

		// Drop items already in the list so bloated saves load in linear time.
		inline constexpr std::initializer_list<uint8_t> kLoadGamePatchOG{
			0x4C, 0x39, 0x33,							// cmp [rbx], r14
			0x74, 0x6A,									// je +0x9B2 ; next entry
			0x48, 0x8B, 0x4B, 0x08,						// mov rcx, [rbx+8]
			0x48, 0x85, 0xC9,							// test rcx, rcx
			0x74, 0x0C,									// je +0x95D ; tail
			0x48, 0x8B, 0xD9,							// mov rbx, rcx
			0xEB, 0xED,									// jmp +0x943
			0x0F, 0x1F, 0x80, 0x00, 0x00, 0x00, 0x00 };	// nop
		inline constexpr std::initializer_list<uint8_t> kLoadGamePatchNG{
			0x4C, 0x39, 0x33,							// cmp [rbx], r14
			0x74, 0x65,									// je +0xD84 ; next entry
			0x48, 0x8B, 0x4B, 0x08,						// mov rcx, [rbx+8]
			0x48, 0x85, 0xC9,							// test rcx, rcx
			0x74, 0x07,									// je +0xD2F ; tail
			0x48, 0x8B, 0xD9,							// mov rbx, rcx
			0xEB, 0xED,									// jmp +0xD1A
			0x66, 0x90 };								// nop
	}

	ModuleQuestLogBloat::ModuleQuestLogBloat() :
		Module("Quest Log Bloat", &bFixesQuestLogBloat)
	{}

	bool ModuleQuestLogBloat::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		using namespace questLogBloatDetail;

		const auto addEntryTarget = REL::Relocation{ REL::ID{ 272757, 2232994, 2232994 }, REL::Offset{ 0x6C } }.address();
		const auto loadGameTarget = REL::Relocation{ REL::ID{ 11049, 2233029, 2233029 }, REL::Offset{ 0x943, 0xD1A, 0xD1A } }.address();
		const bool isOG = RELEX::IsRuntimeOG();

		// Validate both sites first so a mismatch never leaves a half-patch.
		if (!RELEX::Validate(addEntryTarget, isOG ? kAddEntryOG : kAddEntryNG))
		{
			Skip("unexpected bytes at PlayerCharacter::AddQuestLogEntry"sv);
			return false;
		}

		if (!RELEX::Validate(loadGameTarget, isOG ? kLoadGameOG : kLoadGameNG))
		{
			Skip("unexpected bytes at PlayerCharacter::LoadGame quest log walk"sv);
			return false;
		}

		RELEX::WriteSafe(addEntryTarget, isOG ? kAddEntryPatchOG : kAddEntryPatchNG);
		RELEX::WriteSafe(loadGameTarget, isOG ? kLoadGamePatchOG : kLoadGamePatchNG);
		return true;
	}
}
