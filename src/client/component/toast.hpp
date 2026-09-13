#pragma once

#include <string>

namespace toast
{
	// Non-modal notice drawn by ui_scripts/cb_toast; any thread, waits out a Lua VM restart (map load) for a few seconds.
	void show(const std::string& kicker, const std::string& title, const std::string& body = {});

	// Remote-controlled name going into LUI: printable ASCII, no color codes, capped; empty when nothing is left.
	std::string sanitize_name(const std::string& name);
}
