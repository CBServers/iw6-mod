#include <std_include.hpp>
#include "loader/component_loader.hpp"
#include "game/game.hpp"
#include "game/dvars.hpp"

#include "command.hpp"
#include "console.hpp"
#include "mods.hpp"

#include <utils/hook.hpp>
#include <utils/string.hpp>

namespace mods
{
	namespace
	{
		utils::hook::detour sys_create_file_hook;

		void db_build_os_path_from_source(const char* zone_name, game::FF_DIR source, int size, char* filename)
		{
			char user_map[MAX_PATH]{};

			switch (source)
			{
			case game::FFD_DEFAULT:
				(void)game::Com_sprintf(filename, size, "%s\\%s.ff", std::filesystem::current_path().string().c_str(), zone_name);
				break;
			case game::FFD_MOD_DIR:
				assert(mods::is_using_mods());

				(void)game::Com_sprintf(filename, size, "%s\\%s\\%s.ff", std::filesystem::current_path().string().c_str(), (*dvars::fs_gameDirVar)->current.string, zone_name);
				break;
			case game::FFD_USER_MAP:
				game::I_strncpyz(user_map, zone_name, sizeof(user_map));

				(void)game::Com_sprintf(filename, size, "%s\\%s\\%s\\%s.ff", std::filesystem::current_path().string().c_str(), "usermaps", user_map, zone_name);
				break;
			default:
				assert(false && "inconceivable");
				break;
			}
		}

		game::Sys_File sys_create_file_stub(const char* dir, const char* filename)
		{
			auto result = sys_create_file_hook.invoke<game::Sys_File>(dir, filename);

			if (result.handle != INVALID_HANDLE_VALUE)
			{
				return result;
			}

			if (!is_using_mods())
			{
				return result;
			}

			// .ff extension was added previously
			if (!std::strcmp(filename, "mod.ff") && mods::db_mod_file_exists())
			{
				char file_path[MAX_PATH]{};
				db_build_os_path_from_source("mod", game::FFD_MOD_DIR, sizeof(file_path), file_path);
				result.handle = game::Sys_OpenFileReliable(file_path);
			}

			return result;
		}

		void db_load_x_assets_stub(game::XZoneInfo* zone_info, unsigned int zone_count, game::DBSyncMode sync_mode)
		{
			std::vector<game::XZoneInfo> zones(zone_info, zone_info + zone_count);

			if (db_mod_file_exists())
			{
				zones.emplace_back("mod", game::DB_ZONE_COMMON | game::DB_ZONE_CUSTOM, 0);
			}

			game::DB_LoadXAssets(zones.data(), static_cast<unsigned int>(zones.size()), sync_mode);
		}

		const auto skip_extra_zones_stub = utils::hook::assemble([](utils::hook::assembler& a)
		{
			const auto skip = a.newLabel();
			const auto original = a.newLabel();

			a.pushad64();
			a.test(ebp, game::DB_ZONE_CUSTOM); // allocFlags
			a.jnz(skip);

			a.bind(original);
			a.popad64();
			a.mov(rdx, 0x140835F28);
			a.mov(rcx, rsi);
			a.call_aligned(strcmp);
			a.jmp(0x1403217C0);

			a.bind(skip);
			a.popad64();
			a.mov(r15d, 0x80);
			a.not_(r15d);
			a.and_(ebp, r15d);
			a.jmp(0x1403217F6);
		});

		bool fs_game_dir_domain_func(game::dvar_t* dvar, game::DvarValue new_value)
		{
			if (*new_value.string == '\0')
			{
				return true;
			}

			if (game::I_strnicmp(new_value.string, "mods", 4) != 0)
			{
				game::LiveStorage_StatsWriteNotNeeded(game::CONTROLLER_INDEX_0);
				console::error("ERROR: Invalid server value '%s' for '%s'\n", new_value.string, dvar->name);
				return false;
			}

			if (5 < std::strlen(new_value.string) && (new_value.string[4] == '\\' || new_value.string[4] == '/'))
			{
				const auto* s1 = std::strstr(new_value.string, "..");
				const auto* s2 = std::strstr(new_value.string, "::");
				if (s1 == nullptr && s2 == nullptr)
				{
					return true;
				}

				game::LiveStorage_StatsWriteNotNeeded(game::CONTROLLER_INDEX_0);
				console::error("ERROR: Invalid server value '%s' for '%s'\n", new_value.string, dvar->name);
				return false;
			}

			// Invalid path specified
			game::LiveStorage_StatsWriteNotNeeded(game::CONTROLLER_INDEX_0);
			console::error("ERROR: Invalid server value '%s' for '%s'\n", new_value.string, dvar->name);
			return false;
		}
	}

	bool is_using_mods()
	{
		return (*dvars::fs_gameDirVar) && *(*dvars::fs_gameDirVar)->current.string;
	}

	bool db_mod_file_exists()
	{
		if (!*(*dvars::fs_gameDirVar)->current.string)
		{
			return false;
		}

		char filename[MAX_PATH]{};
		db_build_os_path_from_source("mod", game::FFD_MOD_DIR, sizeof(filename), filename);

		if (auto zone_file = game::Sys_OpenFileReliable(filename); zone_file != INVALID_HANDLE_VALUE)
		{
			::CloseHandle(zone_file);
			return true;
		}

		return false;
	}

	class component final : public component_interface
	{
	public:
		static_assert(sizeof(game::Sys_File) == 8);

		void post_unpack() override
		{
			dvars::fs_gameDirVar = reinterpret_cast<game::dvar_t**>(SELECT_VALUE(0x145856D38, 0x147876000));

			// Remove DVAR_INIT from fs_game
			utils::hook::set<std::uint32_t>(SELECT_VALUE(0x14041C085 + 2, 0x1404DDA45 + 2), SELECT_VALUE(game::DVAR_FLAG_NONE, game::DVAR_FLAG_SERVERINFO));

			utils::hook::inject(SELECT_VALUE(0x14041C097 + 3, 0x1404DDA57 + 3), &fs_game_dir_domain_func);

			if (game::environment::is_sp())
			{
				return;
			}

			// Don't load eng_ + patch_ with loadzone
			utils::hook::nop(0x1403217B1, 15);
			utils::hook::jump(0x1403217B1, skip_extra_zones_stub, true);

			// Add custom zone paths
			sys_create_file_hook.create(game::Sys_CreateFile, sys_create_file_stub);

			// Load mod.ff
			utils::hook::call(0x1405E7113, db_load_x_assets_stub); // R_LoadGraphicsAssets According to myself but I don't remember where I got it from

			command::add("loadmod", [](const command::params& params) -> void
			{
				if (params.size() != 2)
				{
					console::info("USAGE: %s \"mods/<mod name>\"", params.get(0));
					return;
				}

				std::string mod_name = utils::string::to_lower(params.get(1));

				if (!mod_name.empty() && !mod_name.starts_with("mods/"))
				{
					mod_name = "mods/" + mod_name;
				}

				// change fs_game if needed
				if (mod_name != (*dvars::fs_gameDirVar)->current.string)
				{
					game::Dvar_SetString((*dvars::fs_gameDirVar), mod_name.c_str());
					command::execute("vid_restart\n");
				}
			});

			command::add("unloadmod", [](const command::params& params) -> void
			{
				if (*dvars::fs_gameDirVar == nullptr || *(*dvars::fs_gameDirVar)->current.string == '\0')
				{
					return;
				}

				game::Dvar_SetString(*dvars::fs_gameDirVar, "");
				command::execute("vid_restart\n");
			});

			// TODO: without a way to monitor all the ways fs_game can be changed there is no way to detect when we
			// should unregister the path from the internal filesystem we use
			// HINT: It could be done in fs_game_dir_domain_func, but I haven't tested if that's the best place to monitor for changes and register/unregister the mods folder
		}
	};
}

REGISTER_COMPONENT(mods::component)
