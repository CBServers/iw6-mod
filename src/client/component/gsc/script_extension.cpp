#include <std_include.hpp>
#include "loader/component_loader.hpp"
#include "game/game.hpp"
#include "game/dvars.hpp"

#include "game/scripting/functions.hpp"

#include <utils/hook.hpp>
#include <utils/string.hpp>

#include "component/console.hpp"
#include "component/notifies.hpp"
#include "component/command.hpp"

#include "script_extension.hpp"
#include "script_error.hpp"
#include "script_loading.hpp"

namespace gsc
{
	std::uint16_t function_id_start = 0x25D;
	std::uint16_t method_id_start = 0x8429;

	void* func_table[0x1000];

	namespace
	{
		#define RVA(ptr) static_cast<std::uint32_t>(reinterpret_cast<std::size_t>(ptr) - 0x140000000)

		struct gsc_error : public std::runtime_error
		{
			using std::runtime_error::runtime_error;
		};

		std::unordered_map<std::uint16_t, game::BuiltinFunction> functions;

		bool force_error_print = false;
		std::optional<std::string> gsc_error_msg;

		std::vector<devmap_entry> devmap_entries{};

		std::optional<devmap_entry> get_devmap_entry(const std::uint8_t* codepos)
		{
			const auto itr = std::ranges::find_if(devmap_entries, [codepos](const devmap_entry& entry) -> bool
			{
				return codepos >= entry.bytecode && codepos < entry.bytecode + entry.size;
			});

			if (itr != devmap_entries.end())
			{
				return *itr;
			}

			return {};
		}

		std::optional<std::pair<std::uint16_t, std::uint16_t>> get_line_and_col_for_codepos(const std::uint8_t* codepos)
		{
			const auto entry = get_devmap_entry(codepos);

			if (!entry.has_value())
			{
				return {};
			}

			std::optional<std::pair<std::uint16_t, std::uint16_t>> best_line_info{};
			std::uint32_t best_codepos = 0;

			assert(codepos >= entry->bytecode);
			const std::uint32_t codepos_offset = static_cast<std::uint32_t>(codepos - entry->bytecode);

			for (const auto& instruction : entry->devmap)
			{
				if (instruction.codepos > codepos_offset)
				{
					continue;
				}

				if (best_line_info.has_value() && codepos_offset - instruction.codepos > codepos_offset - best_codepos)
				{
					continue;
				}

				best_line_info = { { instruction.line, instruction.col } };
				best_codepos = instruction.codepos;
			}

			return best_line_info;
		}

		unsigned int scr_get_function_stub(const char** p_name, int* type)
		{
			const auto result = game::Scr_GetFunction(p_name, type);

			for (const auto& [name, func] : functions)
			{
				game::Scr_RegisterFunction(func, 0, name);
			}

			return result;
		}

		void execute_custom_function(game::BuiltinFunction function)
		{
			auto error = false;

			try
			{
				function();
			}
			catch (const std::exception& ex)
			{
				error = true;
				force_error_print = true;
				gsc_error_msg = ex.what();
			}

			if (error)
			{
				game::Scr_ErrorInternal();
			}
		}

		void vm_call_builtin_function(const std::uint32_t index)
		{
			const auto func = reinterpret_cast<game::BuiltinFunction>(scripting::get_function_by_index(index));

			const auto custom =  functions.contains(static_cast<std::uint16_t>(index));
			if (!custom)
			{
				func();
			}
			else
			{
				execute_custom_function(func);
			}
		}

		void builtin_call_error(const std::string& error)
		{
			const auto pos = game::scr_function_stack->pos;
			const auto function_id = *reinterpret_cast<std::uint16_t*>(reinterpret_cast<std::size_t>(pos - 2));

			if (function_id > 0x1000)
			{
				console::warn("in call to builtin method \"%s\"%s", gsc_ctx->meth_name(function_id).data(), error.data());
			}
			else
			{
				console::warn("in call to builtin function \"%s\"%s", gsc_ctx->func_name(function_id).data(), error.data());
			}
		}

		std::optional<std::string> get_opcode_name(const std::uint8_t opcode)
		{
			try
			{
				const auto index = gsc_ctx->opcode_enum(opcode);
				return {gsc_ctx->opcode_name(index)};
			}
			catch (...)
			{
				return {};
			}
		}

		void print_callstack()
		{
			for (auto frame = game::scr_VmPub->function_frame; frame != game::scr_VmPub->function_frame_start; --frame)
			{
				const auto pos = frame == game::scr_VmPub->function_frame ? game::scr_function_stack->pos : frame->fs.pos;
				const auto function = find_function(frame->fs.pos);

				const char* location;
				if (function.has_value())
				{
					location = utils::string::va("function \"%s\" in file \"%s\"", function.value().first.data(), function.value().second.data());
				}
				else
				{
					location = utils::string::va("unknown location %p", pos);
				}

				const auto line_info = get_line_and_col_for_codepos(reinterpret_cast<const std::uint8_t*>(pos));
				if (line_info.has_value())
				{
					location = utils::string::va("%s line \"%d\" column \"%d\"", location, line_info->first, line_info->second);
				}

				console::warn("\tat %s\n", location);
			}
		}

		void vm_error_stub(const unsigned __int64 mark_pos)
		{
			if (!dvars::com_developer_script->current.enabled && !force_error_print)
			{
				game::LargeLocalResetToMark(mark_pos);
				return;
			}

			console::warn("******* script runtime error ********\n");
			const auto opcode_id = *reinterpret_cast<std::uint8_t*>(SELECT_VALUE(0x1455BE740, 0x144D57840));

			const std::string error = gsc_error_msg.has_value() ? std::format(": {}", gsc_error_msg.value()) : std::string();

			if ((opcode_id >= 0x1A && opcode_id <= 0x20) || (opcode_id >= 0xA8 && opcode_id <= 0xAE))
			{
				builtin_call_error(error);
			}
			else
			{
				const auto opcode = get_opcode_name(opcode_id);
				if (opcode.has_value())
				{
					console::warn("while processing instruction %s%s\n", opcode.value().data(), error.data());
				}
				else
				{
					console::warn("while processing instruction 0x%X%s\n", opcode_id, error.data());
				}
			}

			force_error_print = false;
			gsc_error_msg = {};

			print_callstack();
			console::warn("************************************\n");
			game::LargeLocalResetToMark(mark_pos);
		}

		void inc_in_param()
		{
			game::Scr_ClearOutParams();

			if (game::scr_VmPub->top == game::scr_VmPub->maxstack)
			{
				game::Sys_Error("Internal script stack overflow");
			}

			++game::scr_VmPub->top;
			++game::scr_VmPub->inparamcount;
		}

		void add_code_pos(const char* pos)
		{
			inc_in_param();
			game::scr_VmPub->top->type = game::VAR_FUNCTION;
			game::scr_VmPub->top->u.codePosValue = pos;
		}

		void scr_print()
		{
			for (auto i = 0u; i < game::Scr_GetNumParam(); ++i)
			{
				console::info("%s", game::Scr_GetString(i));
			}
		}

		void scr_print_ln()
		{
			for (auto i = 0u; i < game::Scr_GetNumParam(); ++i)
			{
				console::info("%s", game::Scr_GetString(i));
			}

			console::info("\n");
		}

		void assert_cmd()
		{
			if (!game::Scr_GetInt(0))
			{
				scr_error("Assert fail");
			}
		}

		void assert_ex_cmd()
		{
			if (!game::Scr_GetInt(0))
			{
				scr_error(utils::string::va("Assert fail: %s", game::Scr_GetString(1)));
			}
		}

		void assert_msg_cmd()
		{
			scr_error(utils::string::va("Assert fail: %s", game::Scr_GetString(0)));
		}

		void scr_cmd_is_dedicated_server()
		{
			game::Scr_AddInt(game::environment::is_dedi());
		}

		void scr_bot_auto_connect_enabled()
		{
			if (game::environment::is_dedi() && dvars::sv_botsAutoJoin->current.enabled)
			{
				game::Scr_AddInt(1); // 2 seems to be unused (incomplete GSC/game mode)
				return;
			}

			game::Scr_AddInt(1);
		}

		const char* get_code_pos(const int index)
		{
			if (static_cast<unsigned int>(index) >= game::scr_VmPub->outparamcount)
			{
				scr_error("Scr_GetCodePos: index is out of range");
				return "";
			}

			const auto* value = &game::scr_VmPub->top[-index];

			if (value->type != game::VAR_FUNCTION)
			{
				scr_error("Scr_GetCodePos requires a function as parameter");
				return "";
			}

			return value->u.codePosValue;
		}
	}

	void add_devmap_entry(std::uint8_t* codepos, std::size_t size, const std::string& name, xsk::gsc::buffer devmap_buf)
	{
		std::vector<dev_map_instruction> devmap{};
		const auto* devmap_ptr = reinterpret_cast<const dev_map*>(devmap_buf.data);

		devmap.resize(devmap_ptr->num_instructions);
		std::memcpy(devmap.data(), devmap_ptr->instructions, sizeof(dev_map_instruction) * devmap_ptr->num_instructions);

		devmap_entries.emplace_back(codepos, size, name, std::move(devmap));
	}

	void clear_devmap()
	{
		devmap_entries.clear();
	}

	void add_function(const std::string& name, game::BuiltinFunction function)
	{
		++function_id_start;
		functions[function_id_start] = function;
		gsc_ctx->func_add(name, function_id_start);
	}

	void scr_error(const char* error)
	{
		force_error_print = true;
		gsc_error_msg = error;

		game::Scr_ErrorInternal();
	}

	class extension final : public component_interface
	{
	public:
		void post_unpack() override
		{
			utils::hook::set<game::BuiltinFunction>(SELECT_VALUE(0x14086F468, 0x1409E6CE8), scr_print);
			utils::hook::set<game::BuiltinFunction>(SELECT_VALUE(0x14086F480, 0x1409E6D00), scr_print_ln);

			utils::hook::set<std::uint32_t>(SELECT_VALUE(0x1403D353B + 1, 0x14042E33B + 1), 0x1000); // Scr_RegisterFunction

			utils::hook::set<std::uint32_t>(SELECT_VALUE(0x1403D3542 + 4, 0x14042E342 + 4), RVA(&func_table)); // Scr_RegisterFunction
			// utils::hook::set<std::uint32_t>(SELECT_VALUE(0x1403E0BDD + 3, 0x14043BBBE + 3), RVA(&func_table)); // VM_Execute_0
			utils::hook::inject(SELECT_VALUE(0x1403D38E4 + 3, 0x14042E734 + 3), &func_table); // Scr_BeginLoadScripts

			utils::hook::nop(SELECT_VALUE(0x1403E0BDD + 5, 0x14043BBBE + 5), 2);
			utils::hook::call(SELECT_VALUE(0x1403E0BDD, 0x14043BBBE), vm_call_builtin_function);

			utils::hook::call(SELECT_VALUE(0x1403D391F, 0x14042E76F), scr_get_function_stub);

			utils::hook::call(SELECT_VALUE(0x1403E1ED0, 0x14043CEB1), vm_error_stub);

			add_function("replacefunc", []
			{
				if (scr_get_type(0) != game::VAR_FUNCTION || scr_get_type(1) != game::VAR_FUNCTION)
				{
					throw gsc_error("Parameter must be a function");
				}

				notifies::set_gsc_hook(get_code_pos(0), get_code_pos(1));
			});

			add_function("executecommand", []
			{
				const auto* cmd = game::Scr_GetString(0);
				command::execute(cmd);
			});

			if (game::environment::is_sp())
			{
				return;
			}

			utils::hook::set<game::BuiltinFunction>(0x1409E6E38, assert_ex_cmd);
			utils::hook::set<game::BuiltinFunction>(0x1409E6E50, assert_msg_cmd);
			utils::hook::set<game::BuiltinFunction>(0x1409E6E20, assert_cmd);

			utils::hook::set<game::BuiltinFunction>(0x1409E94D0, scr_cmd_is_dedicated_server);

			dvars::sv_botsAutoJoin = game::Dvar_RegisterBool("sv_botsAutoJoin", false, game::DVAR_FLAG_NONE, "");
			utils::hook::set<game::BuiltinFunction>(0x1409E92F0, scr_bot_auto_connect_enabled);
		}
	};
}

REGISTER_COMPONENT(gsc::extension)
