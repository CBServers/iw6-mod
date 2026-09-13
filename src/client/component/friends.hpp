#pragma once

namespace friends
{
	struct friend_record
	{
		unsigned long long steam_id_bits{0}; // synthetic, session-sticky per launcher id
		std::string id;       // launcher-side id (discord snowflake or cb_<hex>)
		std::string name;
		std::string status;   // online / idle / dnd / offline
		std::string game_id;  // launcher game id the friend is in, e.g. iw6x / boiii
		bool in_game{false};  // running iw6x
		bool joinable{false};
		bool same_match{false};
		std::string mode;     // mp / ext / sp
		std::string map;      // raw mapname
		std::string gametype; // raw gametype
	};

	// Any thread; the main thread commits it and rebuilds the native friends list.
	void apply_snapshot(std::vector<friend_record> entries);

	// Committed list, read by the ISteamFriends proxy.
	[[nodiscard]] int get_count();
	[[nodiscard]] unsigned long long get_steam_id(int index);
	[[nodiscard]] bool find_friend(unsigned long long steam_id_bits, friend_record& out);

	// Steam persona state: 0 offline, 1 online, 2 busy, 3 away.
	[[nodiscard]] int persona_state(const friend_record& record);
}
