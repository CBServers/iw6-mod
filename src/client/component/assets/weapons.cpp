#include <std_include.hpp>
#include "loader/component_loader.hpp"
#include "game/game.hpp"

#include "component/console.hpp"

#include <utils/hook.hpp>

namespace weapons
{
	namespace
	{
		void g_setup_level_weapon_def_stub()
		{
			game::G_SetupLevelWeaponDef();

			// The count on most maps is well below 200
			std::array<game::WeaponCompleteDef*, 256> weapons{};
			const auto count = game::DB_GetAllXAssetOfType_FastFile(game::ASSET_TYPE_WEAPON, (void**)weapons.data(), static_cast<int>(weapons.max_size()));

			std::sort(weapons.begin(), weapons.begin() + count, [](game::WeaponCompleteDef* weapon1, game::WeaponCompleteDef* weapon2)
			{
				assert(weapon1->szInternalName);
				assert(weapon2->szInternalName);

				return std::strcmp(weapon1->szInternalName, weapon2->szInternalName) < 0;
			});

#ifdef _DEBUG
			console::info("Found %i weapons to precache\n", count);
#endif

			for (auto i = 0; i < count; ++i)
			{
#ifdef _DEBUG
				console::info("Precaching weapon \"%s\"\n", weapons[i]->szInternalName);
#endif
				(void)game::G_GetWeaponForName(weapons[i]->szInternalName);
			}
		}
	}

	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			if (game::environment::is_sp()) return;

			// Kill Scr_PrecacheItem (We are going to do this from code)
			utils::hook::nop(0x1403BDB10, 4);
			utils::hook::set<std::uint8_t>(0x1403BDB10, 0xC3);

			// Load weapons from the DB
			utils::hook::call(0x14039F382, g_setup_level_weapon_def_stub);
			utils::hook::call(0x1403AE31A, g_setup_level_weapon_def_stub);
		}
	};
}

REGISTER_COMPONENT(weapons::component)
