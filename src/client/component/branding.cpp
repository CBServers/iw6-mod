#include <std_include.hpp>
#include "loader/component_loader.hpp"
#include "localized_strings.hpp"
#include "scheduler.hpp"
#include "game/game.hpp"

#include <utils/hook.hpp>
#include <utils/string.hpp>

#include "version.hpp"

namespace branding
{
	namespace
	{
		utils::hook::detour ui_get_formatted_build_number_hook;

		void dvar_set_string_stub(game::dvar_t* dvar, const char* string)
		{
			game::Dvar_SetString(dvar, utils::string::va("iw6-mod %s (game %s)", VERSION, string));
		}

		const char* ui_get_formatted_build_number_stub()
		{
			const auto* const build_num = ui_get_formatted_build_number_hook.invoke<const char*>();

			return utils::string::va("%s (%s)", VERSION, build_num);
		}
	}

	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			if (game::environment::is_dedi())
			{
				return;
			}

			localized_strings::override("LUA_MENU_LEGAL_COPYRIGHT", "iw6-mod: " VERSION " by AlterWare.\n");

			utils::hook::call(SELECT_VALUE(0x1403BDABA, 0x140414424), dvar_set_string_stub);
			ui_get_formatted_build_number_hook.create(
				SELECT_VALUE(0x140415FD0, 0x1404D7C00), ui_get_formatted_build_number_stub);

			scheduler::loop([]()
			{
				const auto x = 3;
				const auto y = 0;
				const auto scale = 0.5f;
				float color[4] = {0.666f, 0.666f, 0.666f, 0.666f};
				const auto* text = "iw6-mod: " VERSION;

				auto* font = game::R_RegisterFont("fonts/normalfont");
				if (!font) return;

				game::R_AddCmdDrawText(text, std::numeric_limits<int>::max(), font, static_cast<float>(x),
				                       y + static_cast<float>(font->pixelHeight) * scale,
				                       scale, scale, 0.0f, color, 0);
			}, scheduler::pipeline::renderer);
		}
	};
} // namespace branding

REGISTER_COMPONENT(branding::component)
