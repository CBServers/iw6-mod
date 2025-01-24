#include <std_include.hpp>
#include <game/game.hpp>

#include "sv_game.hpp"

#include <component/console.hpp>

#include <utils/string.hpp>

namespace game::engine
{
	char* SV_ExpandNewlines(char* in)
	{
		static char string[1024];

		unsigned int l = 0;
		while (*in && l < sizeof(string) - 3)
		{
			if (*in == '\n')
			{
				string[l++] = '\\';
				string[l++] = 'n';
			}
			else
			{
				if (*in != '\x14' && *in != '\x15')
				{
					string[l++] = *in;
				}
			}

			++in;
		}

		string[l] = '\0';
		return string;
	}

	void SV_CullIgnorableServerCommands(mp::client_t* client)
	{
		int to = client->reliableSent + 1;
		for (int from = to; from <= client->reliableSequence; ++from)
		{
			int from_index = from & 0x7F;
			assert(client->netBuf.reliableCommandInfo[from_index].time >= 0);
			if (client->netBuf.reliableCommandInfo[from_index].type)
			{
				int to_index = to & 0x7F;
				if (to_index != from_index)
				{
					client->netBuf.reliableCommandInfo[to_index] = client->netBuf.reliableCommandInfo[from_index];
				}

				++to;
			}
		}

		client->reliableSequence = to - 1;
	}

	void SV_DelayDropClient(mp::client_t* drop, const char* reason)
	{
		assert(drop);
		assert(reason);
		assert(drop->header.state != CS_FREE);
		if (drop->header.state == CS_ZOMBIE)
		{
#ifdef _DEBUG
			console::info("(drop->dropReason) = %s", drop->dropReason);
#endif
		}
		else if (!drop->dropReason)
		{
			drop->dropReason = reason;
		}
	}

	void SV_AddServerCommand(mp::client_t* client, svscmd_type type, const char* cmd)
	{
		static_assert(offsetof(mp::client_t, netBuf.reliableCommandInfo[0].cmd) == 0xC44);

		if (client->testClient == TC_BOT)
		{
			return;
		}

		if (client->reliableSequence - client->reliableAcknowledge < 64 && client->header.state == CS_ACTIVE || (SV_CullIgnorableServerCommands(client), type))
		{
			int len = static_cast<int>(std::strlen(cmd)) + 1;
			int to = SV_CanReplaceServerCommand(client, reinterpret_cast<const unsigned char*>(cmd), len);
			if (to < 0)
			{
				++client->reliableSequence;
			}
			else
			{
				int from = to + 1;
				while (from <= client->reliableSequence)
				{
					client->netBuf.reliableCommandInfo[to & 0x7F] = client->netBuf.reliableCommandInfo[from & 0x7F];
					++from;
					++to;
				}
			}

			if (client->reliableSequence - client->reliableAcknowledge == 129)
			{
#ifdef _DEBUG
				console::info("===== pending server commands =====\n");
				int i = 0;
				for (i = client->reliableAcknowledge + 1; i <= client->reliableSequence; ++i)
				{
					console::info("cmd %5d: %8d: %s\n", i, client->netBuf.reliableCommandInfo[i & 0x7F].time, client->netBuf.reliableCommandInfo[i & 0x7F].cmd);
				}
				console::info("cmd %5d: %8d: %s\n", i, *game::mp::serverTime, cmd);
#endif
				NET_OutOfBandPrint(NS_SERVER, &client->header.netchan.remoteAddress, "disconnect");
				SV_DelayDropClient(client, "EXE_SERVERCOMMANDOVERFLOW");
				type = SV_CMD_RELIABLE;
				cmd = utils::string::va("%c \"EXE_SERVERCOMMANDOVERFLOW\"", 'r');
			}

			int index = client->reliableSequence & 0x7F;
			MSG_WriteReliableCommandToBuffer(cmd, client->netBuf.reliableCommandInfo[index].cmd, sizeof(client->netBuf.reliableCommandInfo[index].cmd));
			client->netBuf.reliableCommandInfo[index].time = *game::mp::serverTime;
			client->netBuf.reliableCommandInfo[index].type = type;
		}
	}

	void SV_SendServerCommand(mp::client_t* cl, svscmd_type type, const char* fmt, ...)
	{
		mp::client_t* client;
		int j, len;
		va_list va;

		const auto server_command_buf_large = std::make_unique<char[]>(0x20000);

		va_start(va, fmt);
		len = vsnprintf_s(server_command_buf_large.get(), 0x20000, _TRUNCATE, fmt, va);
		va_end(va);

		assert(len >= 0);

		if (cl)
		{
			SV_AddServerCommand(cl, type, server_command_buf_large.get());
			return;
		}

		if (environment::is_dedi() && !std::strncmp(server_command_buf_large.get(), "print", 5))
		{
			console::info("broadcast: %s\n", SV_ExpandNewlines(server_command_buf_large.get()));
		}

		const auto* sv_maxclients = Dvar_FindVar("sv_maxclients");
		for (j = 0, client = mp::svs_clients; j < sv_maxclients->current.integer; j++, client++)
		{
			if (client->header.state < CS_CLIENTLOADING)
			{
				continue;
			}

			SV_AddServerCommand(client, type, server_command_buf_large.get());
		}
	}

	void SV_GameSendServerCommand(int clientNum, svscmd_type type, const char* text)
	{
		[[maybe_unused]] const auto* sv_maxclients = Dvar_FindVar("sv_maxclients");

		if (clientNum == -1)
		{
			SV_SendServerCommand(nullptr, type, "%s", text);
			return;
		}

		assert(sv_maxclients->current.integer >= 1 && sv_maxclients->current.integer <= 18);
		assert(static_cast<unsigned>(clientNum) < sv_maxclients->current.unsignedInt);
		SV_SendServerCommand(&mp::svs_clients[clientNum], type, "%s", text);
	}
}
