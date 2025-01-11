#include <std_include.hpp>
#include "loader/component_loader.hpp"
#include "game/game.hpp"

#include "console.hpp"

#include <utils/hook.hpp>

namespace security
{
	namespace
	{
		utils::hook::detour ui_replace_directive_hook;

		void set_cached_playerdata_stub(const int localclient, const int index1, const int index2)
		{
			if (index1 >= 0 && index1 < 18 && index2 >= 0 && index2 < 42)
			{
				reinterpret_cast<void(*)(int, int, int)>(0x1405834B0)(localclient, index1, index2);
			}
		}

		void sv_execute_client_message_stub(game::mp::client_t* client, game::msg_t* msg)
		{
			if ((client->reliableSequence - client->reliableAcknowledge) < 0)
			{
				client->reliableAcknowledge = client->reliableSequence;
				console::info("Negative reliableAcknowledge from %s - cl->reliableSequence is %i, reliableAcknowledge is %i\n",
				              client->name, client->reliableSequence, client->reliableAcknowledge);
				return;
			}

			utils::hook::invoke<void>(0x140472500, client, msg);
		}

		void ui_replace_directive_stub(const int local_client_num, const char* src_string, char* dst_string, const int dst_buffer_size)
		{
			assert(src_string);
			if (!src_string)
			{
				return;
			}

			assert(dst_string);
			if (!dst_string)
			{
				return;
			}

			assert(dst_buffer_size > 0);
			if (dst_buffer_size <= 0)
			{
				return;
			}

			constexpr std::size_t MAX_HUDELEM_TEXT_LEN = 0x100;
			if (std::strlen(src_string) > MAX_HUDELEM_TEXT_LEN)
			{
				return;
			}

			ui_replace_directive_hook.invoke<void>(local_client_num, src_string, dst_string, dst_buffer_size);
		}
	}

	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			if (game::environment::is_sp()) return;

			// Patch vulnerability in PlayerCards_SetCachedPlayerData
			utils::hook::call(0x140287C5C, set_cached_playerdata_stub);

			// It is possible to make the server hang if left unchecked
			utils::hook::call(0x14047A29A, sv_execute_client_message_stub);

			ui_replace_directive_hook.create(0x1404D8A00, ui_replace_directive_stub);
		}
	};
}

REGISTER_COMPONENT(security::component)
