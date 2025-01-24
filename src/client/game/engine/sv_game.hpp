#pragma once

namespace game::engine
{
	void SV_GameSendServerCommand(int clientNum, svscmd_type type, const char* text);
}
