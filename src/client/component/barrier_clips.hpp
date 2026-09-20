#pragma once

namespace barrier_clips
{
	// server-side: set a client's authoritative bg_disableBarrierClips preference
	void set_client_pref(int client_num, bool value);
}
