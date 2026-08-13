#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "game/game.hpp"

#include <utils/hook.hpp>

namespace barrier_clips
{
	namespace
	{
		constexpr int MASK_PLAYER_CLIP = 0x10000;
		constexpr int MASK_BARRIER_CLIP = 0x400;
		constexpr int PMF_LADDER = 0x8;

		const game::dvar_t* bg_disableBarrierClips = nullptr;

		utils::hook::detour pmove_single_hook;

		void pmove_single_stub(game::pmove_t* pm)
		{
			if (bg_disableBarrierClips && bg_disableBarrierClips->current.enabled && pm != nullptr)
			{
				auto* ps = static_cast<game::mp::playerState_s*>(pm->ps);
				if (ps != nullptr && (ps->pm_flags & PMF_LADDER) == 0)
				{
					pm->tracemask &= ~MASK_PLAYER_CLIP;
					pm->tracemask |= MASK_BARRIER_CLIP;
				}
			}

			pmove_single_hook.invoke<void>(pm);
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

			pmove_single_hook.create(0x140226190, &pmove_single_stub);

			bg_disableBarrierClips = game::Dvar_RegisterBool("bg_disableBarrierClips", false,
				game::DVAR_FLAG_REPLICATED, "Disable player collision with out of bound barriers");
		}
	};
}

REGISTER_COMPONENT(barrier_clips::component)
