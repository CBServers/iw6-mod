#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "barrier_clips.hpp"

#include "game/game.hpp"

#include <utils/hook.hpp>

namespace barrier_clips
{
	namespace
	{
		constexpr int MASK_PLAYER_CLIP = 0x10000;
		constexpr int MASK_BARRIER_CLIP = 0x400;
		constexpr int PMF_LADDER = 0x8;

		constexpr auto MAX_CLIENTS = 18;

		const game::dvar_t* bg_disableBarrierClips = nullptr;
		const game::dvar_t* bg_disableBarrierClipsClient = nullptr;
		const game::dvar_t* sv_running = nullptr;

		// authoritative per-client preference, kept in sync server-side (see set_client_pref)
		bool client_pref[MAX_CLIENTS] = {};

		utils::hook::detour pmove_single_hook;

		bool enabled(game::mp::playerState_s* ps)
		{
			if (bg_disableBarrierClips != nullptr && bg_disableBarrierClips->current.enabled)
			{
				return true; // server master switch forces the barrier clips off for everyone
			}

			if (sv_running == nullptr)
			{
				sv_running = game::Dvar_FindVar("sv_running");
			}

			if (sv_running != nullptr && sv_running->current.enabled)
			{
				// running the authoritative sim: honour this client's own preference
				return ps->clientNum >= 0 && ps->clientNum < MAX_CLIENTS && client_pref[ps->clientNum];
			}

			// remote client: only ever predicts the local player, so use our own local preference
			return bg_disableBarrierClipsClient != nullptr && bg_disableBarrierClipsClient->current.enabled;
		}

		void pmove_single_stub(game::pmove_t* pm)
		{
			if (pm != nullptr)
			{
				auto* ps = static_cast<game::mp::playerState_s*>(pm->ps);
				if (ps != nullptr && enabled(ps) && (ps->pm_flags & PMF_LADDER) == 0)
				{
					pm->tracemask &= ~MASK_PLAYER_CLIP;
					pm->tracemask |= MASK_BARRIER_CLIP;
				}
			}

			pmove_single_hook.invoke<void>(pm);
		}
	}

	void set_client_pref(const int client_num, const bool value)
	{
		if (client_num >= 0 && client_num < MAX_CLIENTS)
		{
			client_pref[client_num] = value;
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

			// server master switch, applies to everyone
			bg_disableBarrierClips = game::Dvar_RegisterBool("bg_disableBarrierClips", false,
				game::DVAR_FLAG_REPLICATED, "Disable player collision with out of bound barriers");

			// per-client opt-in; pushed by the server via `self setclientdvar("bg_disableBarrierClipsClient", 1)`
			// SCRIPTINFO so setclientdvar accepts it without relaxing the engine's check for every other dvar
			bg_disableBarrierClipsClient = game::Dvar_RegisterBool("bg_disableBarrierClipsClient", false,
				game::DVAR_FLAG_SCRIPTINFO, "Disable player collision with out of bound barriers for this client");
		}
	};
}

REGISTER_COMPONENT(barrier_clips::component)
