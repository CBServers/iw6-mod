#pragma once

namespace game::engine
{
	void SV_SendServerCommand(mp::client_t* cl, svscmd_type type, const char* fmt, ...);
	void SV_GameSendServerCommand(int clientNum, svscmd_type type, const char* text);
	void SV_ReconnectClients(int savepersist);
}
