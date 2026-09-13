#include <std_include.hpp>
#include "loader/component_loader.hpp"
#include "game/game.hpp"

#include "command.hpp"
#include "console.hpp"
#include "nat.hpp"
#include "network.hpp"
#include "party.hpp"
#include "scheduler.hpp"
#include "toast.hpp"
#include "upnp.hpp"

#include "game/ui_scripting/execution.hpp"

#include <utils/cryptography.hpp>
#include <utils/string.hpp>

#include <atomic>
#include <mutex>

#include <ws2tcpip.h>
#include <iphlpapi.h>
#pragma comment(lib, "iphlpapi.lib")

namespace nat
{
	namespace
	{
		game::dvar_t* rendezvous_ip{};
		game::dvar_t* rendezvous_port{};
		game::dvar_t* nat_open_dvar{};
		game::dvar_t* auto_open_dvar{};

		constexpr auto JOINED_TOKEN_GRACE = 15s;

		// All state below is touched only on the main thread, so no locking is needed.
		bool hosting_enabled{}; // host opted in via nat_host; mirrored into the nat_open dvar
		bool auto_open_applied{}; // nat_autoOpen fires once per match, so a later manual close sticks
		std::string host_token{}; // non-empty while hosting
		std::string hosted_token{}; // last host_token, retained across a close so the match keeps its identity
		std::string joined_token{}; // token of the punched session we joined; cleared when we leave
		std::chrono::steady_clock::time_point joined_token_deadline{};
		std::string observed_public_endpoint{}; // our public endpoint, reflected by the rendezvous

		struct punch_attempt
		{
			bool active = false;
			bool joining = false; // true => we issue "connect" on success
			bool connected = false;
			std::string token{};
			std::string fallback_address{}; // joiner-only: tried on timeout
			std::vector<game::netadr_s> candidates{};
			std::vector<game::netadr_s> rejected{}; // candidates that turned out to be our own game
			std::chrono::steady_clock::time_point deadline{};
			std::chrono::steady_clock::time_point next_rendezvous_retry{}; // joiner: privJoin until candidates arrive
		};

		punch_attempt punch{};

		// The rendezvous DNS result, resolved off-thread; guarded by rendezvous_mutex.
		std::mutex rendezvous_mutex;
		std::string rendezvous_key;     // "host:port" the cache was resolved for
		std::string rendezvous_numeric; // resolved "ip:port", empty if resolution failed
		std::atomic_bool rendezvous_resolving{false};

		// A listen-server private match: a local server is running, we're in-game (not the
		// frontend menu, where SV_Loaded can also be true), and we're not a dedicated server.
		bool is_hosting()
		{
			return game::environment::is_mp() && game::SV_Loaded() && game::CL_IsCgameInitialized();
		}

		// Blocking; async pipeline only.
		std::string resolve_ipv4(const std::string& host)
		{
			addrinfo hints{};
			hints.ai_family = AF_INET;
			hints.ai_socktype = SOCK_DGRAM;

			addrinfo* result = nullptr;
			if (getaddrinfo(host.data(), nullptr, &hints, &result) != 0 || !result)
			{
				return {};
			}

			char buffer[INET_ADDRSTRLEN]{};
			inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(result->ai_addr)->sin_addr, buffer, sizeof(buffer));
			freeaddrinfo(result);
			return buffer;
		}

		// Non-blocking: uses the cached DNS result and kicks an async resolve when it's missing/stale.
		bool get_rendezvous_server(game::netadr_s& address)
		{
			if (!rendezvous_ip || !rendezvous_port)
			{
				return false;
			}

			const std::string key = utils::string::va("%s:%s",
				rendezvous_ip->current.string, rendezvous_port->current.string);

			std::string numeric;
			{
				std::lock_guard<std::mutex> lock(rendezvous_mutex);
				if (rendezvous_key == key)
				{
					numeric = rendezvous_numeric;
				}
			}

			if (numeric.empty())
			{
				if (!rendezvous_resolving.exchange(true))
				{
					scheduler::once([key]
					{
						const auto sep = key.rfind(':');
						const auto ip = resolve_ipv4(key.substr(0, sep));

						std::lock_guard<std::mutex> lock(rendezvous_mutex);
						rendezvous_key = key;
						rendezvous_numeric = ip.empty() ? std::string{} : ip + key.substr(sep);
						rendezvous_resolving = false;
					}, scheduler::pipeline::async);
				}
				return false;
			}

			address = network::address_from_string(numeric);
			return address.type != game::NA_BAD;
		}

		uint16_t get_local_port()
		{
			if (const auto bound = network::get_bound_port())
			{
				return bound;
			}

			const auto port = game::Dvar_GetInt("net_port");
			if (port >= 1024 && port <= 65535)
			{
				return static_cast<uint16_t>(port);
			}

			return 27016;
		}

		std::string make_address(const std::string& host, const uint16_t port)
		{
			if (host.empty() || port < 1024)
			{
				return {};
			}

			const auto parsed = network::address_from_string(utils::string::va("%s:%hu", host.data(), port));
			if (!network::is_connectable_address(parsed))
			{
				return {};
			}

			return network::address_to_string(parsed);
		}

		std::string get_local_candidate()
		{
			const auto ip = get_local_ip();
			if (ip.empty())
			{
				return {};
			}

			return ip + ":" + std::to_string(get_local_port());
		}

		// A Radmin (26.x) / Hamachi (25.x) address, if present; these VPNs bypass NAT.
		std::string get_vpn_candidate()
		{
			ULONG length = 15000;
			std::vector<unsigned char> buffer(length);
			auto* adapter_info = reinterpret_cast<IP_ADAPTER_INFO*>(buffer.data());

			if (GetAdaptersInfo(adapter_info, &length) == ERROR_BUFFER_OVERFLOW)
			{
				buffer.resize(length);
				adapter_info = reinterpret_cast<IP_ADAPTER_INFO*>(buffer.data());
			}

			if (GetAdaptersInfo(adapter_info, &length) != NO_ERROR)
			{
				return {};
			}

			std::string radmin_ip;
			std::string hamachi_ip;

			for (auto* adapter = adapter_info; adapter; adapter = adapter->Next)
			{
				const std::string ip = adapter->IpAddressList.IpAddress.String;
				if (ip == "0.0.0.0" || ip.empty())
				{
					continue;
				}

				if (ip.rfind("26.", 0) == 0)
				{
					radmin_ip = ip;
				}
				else if (ip.rfind("25.", 0) == 0)
				{
					hamachi_ip = ip;
				}
			}

			const auto& vpn_ip = !radmin_ip.empty() ? radmin_ip : hamachi_ip;
			return vpn_ip.empty() ? std::string{} : make_address(vpn_ip, get_local_port());
		}

		struct endpoint_candidates
		{
			std::string lan;
			std::string vpn;
		};

		// LAN/VPN probes (socket + adapter enumeration) are syscall-heavy; refresh at most once a minute.
		const endpoint_candidates& get_candidates()
		{
			static endpoint_candidates cached{};
			static std::chrono::steady_clock::time_point expiry{};

			const auto now = std::chrono::steady_clock::now();
			if (now >= expiry)
			{
				cached.lan = get_local_candidate();
				cached.vpn = get_vpn_candidate();
				expiry = now + 60s;
			}

			return cached;
		}

		std::string generate_token()
		{
			uint8_t data[8]{};
			utils::cryptography::random::get_data(data, sizeof(data));
			return utils::string::dump_hex(std::string(reinterpret_cast<char*>(data), sizeof(data)), "");
		}

		void send_to_rendezvous(const std::string& command, const std::string& token)
		{
			game::netadr_s addr{};
			if (!get_rendezvous_server(addr))
			{
				console::warn("[nat] could not resolve rendezvous server\n");
				return;
			}

			auto data = token;
			const auto& candidates = get_candidates();
			if (!candidates.lan.empty())
			{
				data += " " + candidates.lan;
			}
			if (!candidates.vpn.empty())
			{
				data += " " + candidates.vpn;
			}

			network::send(addr, command, data);
		}

		// Punching a candidate carrying one of our own IPs acks our own punch and connects us to ourselves.
		bool is_own_address(const game::netadr_s& address)
		{
			const auto& own_candidates = get_candidates();
			for (const auto& own : {own_candidates.lan, own_candidates.vpn})
			{
				if (own.empty())
				{
					continue;
				}

				// IP match alone is enough; our own IP can never reach the peer, whatever the port.
				const auto parsed = network::address_from_string(own);
				if (network::is_ip_address(parsed) && network::is_ip_address(address) && !memcmp(parsed.ip, address.ip, 4))
				{
					return true;
				}
			}

			// Same public IP on a different port can be a real host behind our NAT (hairpin); same port is us.
			if (!observed_public_endpoint.empty())
			{
				const auto parsed = network::address_from_string(observed_public_endpoint);
				if (network::are_addresses_equal(parsed, address))
				{
					return true;
				}
			}

			return false;
		}

		bool is_rejected(const game::netadr_s& address)
		{
			return std::ranges::any_of(punch.rejected, [&](const game::netadr_s& rejected)
			{
				return network::are_addresses_equal(rejected, address);
			});
		}

		void add_candidate(const game::netadr_s& address)
		{
			if (!network::is_ip_address(address))
			{
				return;
			}

			if (is_own_address(address) || is_rejected(address))
			{
				console::info("[nat] ignoring own-address candidate %s\n", network::address_to_string(address).data());
				return;
			}

			for (const auto& existing : punch.candidates)
			{
				if (network::are_addresses_equal(existing, address))
				{
					return;
				}
			}

			punch.candidates.push_back(address);
		}

		void send_punch_round()
		{
			for (const auto& candidate : punch.candidates)
			{
				network::send(candidate, "punch", punch.token);
			}
		}

		void issue_connect(const std::string& address)
		{
			console::info("[nat] connecting to %s\n", address.data());

			// party::connect directly; skips a command-buffer round trip.
			const auto target = network::address_from_string(address);
			if (network::is_connectable_address(target))
			{
				party::connect(target);

				// Adopt the host's token as our match identity only once we actually connect, so a
				// punch that never lands can't mislabel the match we're still sitting in.
				if (punch.joining)
				{
					joined_token = punch.token;
					joined_token_deadline = std::chrono::steady_clock::now() + JOINED_TOKEN_GRACE;
				}
			}
			else
			{
				console::error("[nat] refusing to connect to unconnectable address %s\n", address.data());
			}
		}

		// Main thread, outside the frame's error trap, so this opens the error popup directly instead of Com_Error.
		void show_join_error()
		{
			constexpr auto message = "Could not reach the host. They may be on a restricted network (e.g. a mobile "
				"hotspot). Ask them to host on home Wi-Fi, port-forward, or use a VPN like Radmin.";

			console::error("[nat] %s\n", message);
			ui_scripting::leave_menu("popup_acceptinginvite");
			game::Com_SetErrorMessage(message, "MENU_ERROR");
			game::LUI_OpenMenu(0, "error_popmenu", 1, 0, 0);
		}

		void feed_candidates(const std::vector<std::string>& candidate_strings)
		{
			for (const auto& candidate : candidate_strings)
			{
				if (candidate.empty())
				{
					continue;
				}

				add_candidate(network::address_from_string(candidate));
			}
		}

		// Retires the joined session's identity once we're out of a match (there is no disconnect
		// hook). Grace-period based: map changes drop the in-game flag briefly, and losing the token
		// on every load screen would silently disable same-match detection for the rest of the session.
		void update_joined_session()
		{
			if (joined_token.empty())
			{
				return;
			}

			const auto now = std::chrono::steady_clock::now();
			if (punch.active || game::CL_IsCgameInitialized())
			{
				joined_token_deadline = now + JOINED_TOKEN_GRACE;
			}
			else if (now >= joined_token_deadline)
			{
				joined_token.clear();
			}
		}

		void punch_frame()
		{
			update_joined_session();

			if (!punch.active)
			{
				return;
			}

			// Retry privJoin (UDP loss / DNS still resolving) until the rendezvous answers with candidates.
			const auto now = std::chrono::steady_clock::now();
			if (punch.joining && punch.candidates.empty() && now >= punch.next_rendezvous_retry)
			{
				send_to_rendezvous("privJoin", punch.token);
				punch.next_rendezvous_retry = now + 1s;
			}

			if (std::chrono::steady_clock::now() > punch.deadline)
			{
				if (!punch.connected && punch.joining)
				{
					if (!punch.fallback_address.empty())
					{
						console::info("[nat] no direct path; trying fallback %s\n", punch.fallback_address.data());
						issue_connect(punch.fallback_address);
					}
					else
					{
						console::warn("[nat] join failed for token=%s (no direct path)\n", punch.token.data());
						show_join_error();
					}
				}

				punch.active = false;
				return;
			}

			send_punch_round();
		}

		void set_hosting_enabled(bool enabled)
		{
			hosting_enabled = enabled;
			if (enabled)
			{
				// Any open uses up this match's auto-open, so a later manual close isn't undone.
				auto_open_applied = true;
				upnp::ensure_mapped(); // retry a startup mapping the router was too slow for
			}

			if (nat_open_dvar)
			{
				game::Dvar_SetBool(nat_open_dvar, enabled);
			}

			if (!enabled)
			{
				host_token.clear();
				observed_public_endpoint.clear();
			}
		}

		// is_hosting() excludes the frontend menu, where SV_Loaded is also true.
		void update_host_session()
		{
			if (!is_hosting())
			{
				// Left the match: the identity dies with it, unlike a mere close to friends.
				hosted_token.clear();
				auto_open_applied = false;
			}
			else if (!hosting_enabled && !auto_open_applied && auto_open_dvar && auto_open_dvar->current.enabled)
			{
				// Opted in from the private match lobby's match settings before starting.
				set_hosting_enabled(true);
				toast::show("OPEN TO FRIENDS", "Friends can now join this match.");
			}

			if (is_hosting() && hosting_enabled)
			{
				if (host_token.empty())
				{
					host_token = generate_token();
					hosted_token = host_token;
					console::info("[nat] opened private match to friends, token=%s\n", host_token.data());
				}

				send_to_rendezvous("privRegister", host_token); // register + keepalive
			}
			else if (hosting_enabled || !host_token.empty())
			{
				// Toggled off, match ended, or returned to menu: close + reset the toggle.
				if (!host_token.empty())
				{
					console::info("[nat] closing private match, dropping token=%s\n", host_token.data());
				}

				set_hosting_enabled(false);
			}
		}
	}

	std::string get_local_ip()
	{
		std::string ip;
		const SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (sock != INVALID_SOCKET)
		{
			sockaddr_in target{};
			target.sin_family = AF_INET;
			target.sin_port = htons(53);
			inet_pton(AF_INET, "8.8.8.8", &target.sin_addr);

			if (connect(sock, reinterpret_cast<sockaddr*>(&target), sizeof(target)) == 0)
			{
				sockaddr_in local{};
				int length = sizeof(local);
				if (getsockname(sock, reinterpret_cast<sockaddr*>(&local), &length) == 0)
				{
					char buffer[INET_ADDRSTRLEN]{};
					inet_ntop(AF_INET, &local.sin_addr, buffer, sizeof(buffer));
					ip = buffer;
				}
			}

			closesocket(sock);
		}

		return ip;
	}

	std::string current_token()
	{
		return host_token;
	}

	bool can_open_to_friends()
	{
		return is_hosting() && !hosting_enabled;
	}

	bool open_to_friends()
	{
		if (!is_hosting())
		{
			return false;
		}

		// A repeated open-match must not re-run registration.
		if (!hosting_enabled)
		{
			set_hosting_enabled(true);
			update_host_session();
		}

		return true;
	}

	std::string hosted_session_token()
	{
		return hosted_token;
	}

	std::string joined_session_token()
	{
		return joined_token;
	}

	std::string get_host_endpoint()
	{
		// Gated on host_token so the advertised endpoint and join-secret token agree.
		if (host_token.empty())
		{
			return {};
		}

		// Fallback priority when punching fails: public (port-forward) > VPN > LAN.
		if (!observed_public_endpoint.empty())
		{
			return observed_public_endpoint;
		}

		const auto& candidates = get_candidates();
		return !candidates.vpn.empty() ? candidates.vpn : candidates.lan;
	}

	void get_rendezvous(std::string& host, int& port)
	{
		host = rendezvous_ip ? rendezvous_ip->current.string : "master.cbservers.xyz";
		port = rendezvous_port ? atoi(rendezvous_port->current.string) : 20810;
	}

	bool on_self_connect(const game::netadr_s& target)
	{
		// Only a punched join can be resumed; a fallback or manual connect has nowhere else to go.
		if (!punch.joining || !punch.connected || punch.token.empty())
		{
			return false;
		}

		console::info("[nat] %s answered with our own xuid (hairpin to our own port); resuming punch\n",
			network::address_to_string(target).data());

		punch.rejected.push_back(target);
		std::erase_if(punch.candidates, [&](const game::netadr_s& candidate)
		{
			return network::are_addresses_equal(candidate, target);
		});

		if (network::are_addresses_equal(network::address_from_string(punch.fallback_address), target))
		{
			punch.fallback_address.clear();
		}

		// issue_connect adopted the host's token; we never actually joined.
		joined_token.clear();

		punch.connected = false;
		punch.active = true;
		punch.deadline = std::chrono::steady_clock::now() + 10s;
		send_punch_round();
		return true;
	}

	void begin_join(const std::string& token, const std::string& fallback_address)
	{
		joined_token.clear(); // re-adopted in issue_connect once the join actually lands
		punch = punch_attempt{};
		punch.active = true;
		punch.joining = true;
		punch.token = token;
		punch.fallback_address = fallback_address;
		punch.deadline = std::chrono::steady_clock::now() + 12s;
		punch.next_rendezvous_retry = std::chrono::steady_clock::now() + 1s;

		console::info("[nat] joining token=%s (fallback=%s)\n", token.data(),
			fallback_address.empty() ? "none" : fallback_address.data());
		send_to_rendezvous("privJoin", token);
	}

	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			if (game::environment::is_sp() || game::environment::is_dedi())
			{
				return;
			}

			scheduler::once([]
			{
				rendezvous_ip = game::Dvar_RegisterString("rendezvousServerIP", "master.cbservers.xyz",
					game::DVAR_FLAG_NONE, "IP of the private-game rendezvous server");
				rendezvous_port = game::Dvar_RegisterString("rendezvousServerPort", "20810",
					game::DVAR_FLAG_NONE, "Port of the private-game rendezvous server");
				nat_open_dvar = game::Dvar_RegisterBool("nat_open", false, game::DVAR_FLAG_NONE,
					"Allow friends to join this private match");
				auto_open_dvar = game::Dvar_RegisterBool("nat_autoOpen", false, game::DVAR_FLAG_NONE,
					"Open a hosted private match to friends automatically once it starts");

				game::netadr_s warm{};
				get_rendezvous_server(warm); // kick the async DNS resolve so first use hits the cache
			}, scheduler::pipeline::main);

			network::on("privRegisterAck", [](const game::netadr_s&, const std::string& data)
			{
				// The rendezvous reflects our observed public endpoint (STUN-style).
				if (const auto parsed = network::address_from_string(data);
					network::is_connectable_address(parsed))
				{
					observed_public_endpoint = network::address_to_string(parsed);
					upnp::on_public_endpoint(observed_public_endpoint);
				}
			});

			// privPeer payload: "<token> <cand1> <cand2> ..."
			network::on("privPeer", [](const game::netadr_s&, const std::string& data)
			{
				const auto fields = utils::string::split(data, ' ');
				if (fields.empty())
				{
					return;
				}

				const auto& token = fields[0];
				const std::vector<std::string> candidates(fields.begin() + 1, fields.end());

				if (punch.active && punch.joining && punch.token == token)
				{
					// Joiner: feed the host's candidates into our active attempt.
					feed_candidates(candidates);
					send_punch_round();
				}
				else if (!host_token.empty() && token == host_token)
				{
					// Host: punch toward the joiner so our NAT opens (no connect); merge if already punching.
					if (!punch.active || punch.joining)
					{
						punch = punch_attempt{};
						punch.active = true;
						punch.joining = false;
						punch.token = token;
					}
					punch.deadline = std::chrono::steady_clock::now() + 10s;
					feed_candidates(candidates);
					send_punch_round();
				}
			});

			network::on("privReject", [](const game::netadr_s&, const std::string& data)
			{
				console::warn("[nat] privReject: %s\n", data.data());
				if (punch.active && punch.joining)
				{
					// Session gone: fall straight to the direct path if we have one.
					punch.deadline = std::chrono::steady_clock::now();
				}
			});

			// Ack the observed source address, not the claimed one (symmetric NAT).
			network::on("punch", [](const game::netadr_s& from, const std::string& token)
			{
				network::send(from, "punchAck", token);

				if (punch.active && punch.token == token)
				{
					add_candidate(from);
				}
			});

			network::on("punchAck", [](const game::netadr_s& from, const std::string& token)
			{
				if (!punch.active || punch.token != token || punch.connected)
				{
					return;
				}

				// Candidates fed before the rendezvous reflected our endpoint slip past add_candidate.
				if (is_own_address(from) || is_rejected(from))
				{
					console::info("[nat] ignoring punchAck from own address %s\n", network::address_to_string(from).data());
					return;
				}

				punch.connected = true;
				punch.active = false;

				const auto endpoint = network::address_to_string(from);
				if (punch.joining)
				{
					console::info("[nat] direct path open to %s\n", endpoint.data());
					issue_connect(endpoint);
				}
				else
				{
					console::info("[nat] host path open to %s\n", endpoint.data());
				}
			});

			scheduler::loop(punch_frame, scheduler::pipeline::main, 250ms);

			// Host session register/keepalive/teardown, driven purely by game state.
			scheduler::loop(update_host_session, scheduler::pipeline::main, 5s);

			// Toggle whether the current private match is open to friends.
			command::add("nat_host", [](const command::params&)
			{
				if (!is_hosting())
				{
					console::warn("[nat] not hosting a match; cannot open to friends\n");
					return;
				}

				set_hosting_enabled(!hosting_enabled);
				console::info("[nat] match is now %s to friends\n", hosting_enabled ? "OPEN" : "CLOSED");
			});

			// Manual join for debugging.
			command::add("nat_join", [](const command::params& params)
			{
				if (params.size() < 2)
				{
					console::info("[nat] usage: nat_join <token>\n");
					return;
				}

				begin_join(params.get(1), {});
			});
		}
	};
}

REGISTER_COMPONENT(nat::component)
