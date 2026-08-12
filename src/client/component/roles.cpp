#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "game/game.hpp"

#include "gsc/script_extension.hpp"
#include "scripting.hpp"
#include "roles.hpp"

#include <cstdio>

namespace roles
{
	namespace
	{
		constexpr int max_clients = 18;

		struct role_entry
		{
			char name[64];
			int value;
			bool valid;
		};

		role_entry role_cache[max_clients] = {};
	}

	const char* get_role_tag(const int client_num)
	{
		if (client_num < 0 || client_num >= max_clients)
		{
			return nullptr;
		}

		const auto& e = role_cache[client_num];
		if (!(e.valid && e.value > 0 && e.name[0]))
		{
			return nullptr;
		}

		static char tag[96];
		snprintf(tag, sizeof(tag), "^7[%s^7]", e.name);
		return tag;
	}

	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			if (game::environment::is_sp())
			{
				return;
			}

			gsc::add_function("exportrole", []
			{
				const int cn = game::Scr_GetInt(0);
				if (cn < 0 || cn >= max_clients)
				{
					return;
				}

				const char* name = game::Scr_GetString(1);
				const int value = game::Scr_GetNumParam() > 2 ? game::Scr_GetInt(2) : 0;

				auto& e = role_cache[cn];
				if (name)
				{
					strncpy_s(e.name, name, _TRUNCATE);
				}
				else
				{
					e.name[0] = '\0';
				}
				e.value = value;
				e.valid = true;
			});

			scripting::on_shutdown([](int, int)
			{
				for (auto& e : role_cache)
				{
					e = {};
				}
			});
		}
	};
}

REGISTER_COMPONENT(roles::component)
