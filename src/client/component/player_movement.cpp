#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "game/game.hpp"

#include <utils/hook.hpp>

namespace player_movement
{
	namespace
	{
		constexpr int SURF_LADDER = 0x8;

		const game::dvar_t* bg_bunnyHopAuto = nullptr;
		const game::dvar_t* bg_climbAnything = nullptr;
		const game::dvar_t* bg_omnimovement = nullptr;

		constexpr auto INPUT_STATE_BASE = 0x1419DE6D0;
		constexpr auto INPUT_STATE_STRIDE = 0x2BC;
		constexpr auto INPUT_BACK_ACTIVE = 0x4C;
		constexpr auto INPUT_SPRINT_ACTIVE = 0x254;
		constexpr auto INPUT_SPRINT_PRESSED = 0x255;
		constexpr unsigned int CMD_BUTTON_SPRINT = 0x2;

		utils::hook::detour jump_check_hook;
		utils::hook::detour pm_update_sprint_hook;
		utils::hook::detour cl_keymove_hook;

		void* cl_keymove_stub(unsigned int local_client, game::usercmd_s* cmd)
		{
			const auto result = cl_keymove_hook.invoke<void*>(local_client, cmd);

			if (bg_omnimovement && bg_omnimovement->current.enabled && cmd != nullptr)
			{
				auto* input = reinterpret_cast<unsigned char*>(INPUT_STATE_BASE)
					+ INPUT_STATE_STRIDE * static_cast<size_t>(local_client);

				const auto back_held = input[INPUT_BACK_ACTIVE] != 0;
				const auto sprint_held = input[INPUT_SPRINT_ACTIVE] != 0 || input[INPUT_SPRINT_PRESSED] != 0;

				if (back_held && sprint_held)
				{
					cmd->buttons |= CMD_BUTTON_SPRINT;
					input[INPUT_SPRINT_PRESSED] = 0;
				}
			}

			return result;
		}

		char clamp_intent(int forwardmove, int rightmove)
		{
			const auto abs_forward = forwardmove < 0 ? -forwardmove : forwardmove;
			const auto abs_right = rightmove < 0 ? -rightmove : rightmove;
			auto intent = abs_forward > abs_right ? abs_forward : abs_right;
			if (intent > 127)
			{
				intent = 127;
			}
			return static_cast<char>(intent);
		}

		void pm_update_sprint_stub(game::pmove_t* pm, void* pml)
		{
			if (bg_omnimovement && bg_omnimovement->current.enabled && pm != nullptr)
			{
				const auto saved = pm->cmd.forwardmove;
				pm->cmd.forwardmove = clamp_intent(saved, pm->cmd.rightmove);
				pm_update_sprint_hook.invoke<void>(pm, pml);
				pm->cmd.forwardmove = saved;
				return;
			}

			pm_update_sprint_hook.invoke<void>(pm, pml);
		}

		char jump_check_stub(game::pmove_t* pm, void* pml)
		{
			if (bg_bunnyHopAuto && bg_bunnyHopAuto->current.enabled && pm != nullptr)
			{
				auto* old_buttons = reinterpret_cast<int*>(reinterpret_cast<char*>(pm) + 0x48);
				const auto saved = *old_buttons;
				*old_buttons &= ~0x400;
				const auto result = jump_check_hook.invoke<char>(pm, pml);
				*old_buttons = saved;
				return result;
			}

			return jump_check_hook.invoke<char>(pm, pml);
		}

		void pm_ladder_trace_stub(game::pmove_t* pm, game::trace_t* trace, const float* start,
			const float* end, const game::Bounds* bounds, int pass_entity_num, int content_mask)
		{
			game::PM_playerTrace(pm, trace, start, end, bounds, pass_entity_num, content_mask);

			if (bg_climbAnything && bg_climbAnything->current.enabled && trace != nullptr)
			{
				trace->surfaceFlags |= SURF_LADDER;
			}
		}
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

			jump_check_hook.create(0x140212F60, &jump_check_stub);
			pm_update_sprint_hook.create(0x140224380, &pm_update_sprint_stub);
			cl_keymove_hook.create(0x1402C0E10, &cl_keymove_stub);

			utils::hook::call(0x14021F99E, pm_ladder_trace_stub);
			utils::hook::call(0x14021FA84, pm_ladder_trace_stub);

			bg_bunnyHopAuto = game::Dvar_RegisterBool("bg_bunnyHopAuto", false,
				game::DVAR_FLAG_REPLICATED, "Automatically jump on landing while holding the jump key");

			bg_climbAnything = game::Dvar_RegisterBool("bg_climbAnything", false,
				game::DVAR_FLAG_REPLICATED, "Treat any surface as a ladder");

			bg_omnimovement = game::Dvar_RegisterBool("bg_omnimovement", false,
				game::DVAR_FLAG_REPLICATED, "Sprint in any direction");

			const auto sprint_strafe_stub = utils::hook::assemble([](utils::hook::assembler& a)
			{
				const auto stock = a.newLabel();
				const auto skip = a.newLabel();

				a.mov(rax, qword_ptr(reinterpret_cast<int64_t>(&bg_omnimovement)));
				a.test(rax, rax);
				a.jz(stock);
				a.cmp(byte_ptr(rax, 0x10), 0);
				a.jz(stock);
				a.jmp(0x14022587F);

				a.bind(stock);
				a.test(dword_ptr(rsi, 0x0C), 0x4000);
				a.jz(skip);
				a.movsx(eax, byte_ptr(rdi, 0x25));
				a.jmp(0x140225869);

				a.bind(skip);
				a.jmp(0x14022587F);
			});

			utils::hook::nop(0x14022585C, 13);
			utils::hook::jump(0x14022585C, sprint_strafe_stub, true);
		}
	};
}

REGISTER_COMPONENT(player_movement::component)
