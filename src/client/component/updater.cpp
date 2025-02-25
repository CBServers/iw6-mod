#include <std_include.hpp>
#include "loader/component_loader.hpp"
#include "updater.hpp"
#include "filesystem.hpp"

#include "game/game.hpp"

#include <utils/flags.hpp>
#include <utils/io.hpp>
#include <updater/_updater.hpp>

namespace updater
{
	bool has_iw6()
	{
		if (!utils::io::file_exists("iw6sp64_ship.exe") && !utils::io::file_exists("iw6mp64_ship.exe"))
		{
			game::show_error(
				"Can't find a valid iw6sp64_ship.exe or iw6mp64_ship.exe. Make sure you put iw6x.exe in your Ghosts installation folder.");
			return false;
		}

		return true;
	}

	void update()
	{
		if (utils::flags::has_flag("noupdate"))
		{
			return;
		}

		try
		{
			if (has_iw6())
			{
				run(filesystem::get_binary_directory());
			}
		}
		catch (update_cancelled&)
		{
			TerminateProcess(GetCurrentProcess(), 0);
		}
		catch (...)
		{
		}
	}

	class component final : public component_interface
	{
	public:

		component()
		{
			this->update_thread_ = std::thread([this]
			{
				update();
			});
		}

		void post_start() override
		{
			join();
		}

		void pre_destroy() override
		{
			join();
		}

		void post_unpack() override
		{
			join();
		}

	private:
		std::thread update_thread_{};

		void join()
		{
			if (this->update_thread_.joinable())
			{
				this->update_thread_.join();
			}
		}
	};
}

REGISTER_COMPONENT(updater::component)