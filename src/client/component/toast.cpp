#include <std_include.hpp>
#include "game/game.hpp"

#include "scheduler.hpp"
#include "toast.hpp"

#include "game/ui_scripting/execution.hpp"
#include "ui_scripting.hpp"

namespace toast
{
	void show(const std::string& kicker, const std::string& title, const std::string& body)
	{
		if (!game::environment::is_mp())
		{
			return;
		}

		// LUI ticks on the main thread in IW6; script values need the Lua state, so build them inside the task.
		const auto deadline = std::chrono::steady_clock::now() + 10s;
		scheduler::schedule([=]
		{
			if (std::chrono::steady_clock::now() > deadline)
			{
				return scheduler::cond_end;
			}

			if (!ui_scripting::lui_running())
			{
				return scheduler::cond_continue;
			}

			ui_scripting::notify("cb_toast", {{"kicker", kicker}, {"title", title}, {"body", body}});
			return scheduler::cond_end;
		}, scheduler::pipeline::main, 250ms);
	}

	std::string sanitize_name(const std::string& name)
	{
		std::string result;
		for (const auto c : name)
		{
			if ((c > 0x20 && c < 0x7F && c != '^') || (c == ' ' && !result.empty()))
			{
				result.push_back(c);
			}
		}

		result.resize(std::min<size_t>(result.size(), 32));
		while (!result.empty() && result.back() == ' ')
		{
			result.pop_back();
		}

		return result;
	}
}
