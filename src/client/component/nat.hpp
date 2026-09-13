#pragma once

#include <string>
#include "game/game.hpp"

namespace nat
{
	// The primary adapter's LAN IP via the UDP-connect trick, "" on failure (async-safe).
	std::string get_local_ip();

	// The active host token, or "" when not hosting (session lifecycle is internal).
	std::string current_token();

	// Same, but retained while the match is merely closed to friends; dropped when the match ends.
	// Identity has to outlive a close or members stop recognising the host as being in their match.
	std::string hosted_session_token();

	// Token of the punched session we joined, "" when we didn't join one. Lets a joiner derive the
	// same match identity as the host.
	std::string joined_session_token();

	// Host of a private match that is still closed to friends (the pause-menu toggle is off).
	bool can_open_to_friends();

	// Open the hosted private match to friends now (same as the toggle), registering with the rendezvous at once.
	bool open_to_friends();

	// The host's reachable endpoint ("ip:port") for the join-secret fallback, or "".
	std::string get_host_endpoint();

	// The rendezvous server the joiner punches through (defaults if dvars not yet registered).
	void get_rendezvous(std::string& host, int& port);

	// Joiner: punch toward the host; on failure connect fallback_address, else error.
	void begin_join(const std::string& token, const std::string& fallback_address);

	// The connect query reached our own game (same-NAT hairpin): drop that candidate and keep punching.
	// False when there is no punch to resume (main thread only).
	bool on_self_connect(const game::netadr_s& target);
}
