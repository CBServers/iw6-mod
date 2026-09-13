#include <std_include.hpp>
#include "loader/component_loader.hpp"
#include "game/game.hpp"

#include "friends.hpp"
#include "console.hpp"
#include "discord.hpp"
#include "dvars.hpp"
#include "ipc.hpp"
#include "nat.hpp"
#include "scheduler.hpp"
#include "toast.hpp"

#include <utils/concurrency.hpp>
#include <utils/hook.hpp>
#include <utils/string.hpp>

#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>

// In-game friends list backed by the CB launcher snapshot; the ISteamFriends proxy feeds it to the native list.
namespace friends
{
	namespace
	{
		// Native friends list for controller 0: +0x18 stored xuid, +2040 refresh flag, +2044 start time, +2048 next index.
		constexpr auto NATIVE_LIST = 0x147DC0C90;
		constexpr auto NATIVE_REFRESH = 0x1404FBA50;

		struct store
		{
			std::vector<friend_record> list;
			std::unordered_map<unsigned long long, size_t> index;
			std::optional<std::vector<friend_record>> pending;
			std::unordered_map<std::string, unsigned int> account_ids;
			unsigned int next_account_id{1000001};
		};

		utils::concurrency::container<store>& get_store()
		{
			static utils::concurrency::container<store> instance;
			return instance;
		}

		std::atomic_bool snapshot_pending{false};
		bool refresh_pending{false};

		utils::hook::detour is_friend_joinable_hook;
		utils::hook::detour is_friend_invitable_hook;
		utils::hook::detour join_online_friend_hook;
		utils::hook::detour invite_online_friend_hook;
		utils::hook::detour friend_presence_hook;

		// account_instance 1, account_type 1 (individual), universe 1 (public)
		unsigned long long make_steam_id_bits(const unsigned int account_id)
		{
			return (1ull << 56) | (1ull << 52) | (1ull << 32) | account_id;
		}

		// Friends.SetOnlineFriendStoredXUID leaves the chosen friend here; the action backends read it.
		bool get_selected(const int controller, friend_record& out)
		{
			return controller == 0 && find_friend(*reinterpret_cast<unsigned long long*>(NATIVE_LIST + 0x18), out);
		}

		std::string localize_map(const std::string& map)
		{
			const auto* localized = game::UI_LocalizeMapname(map.data());
			return localized && *localized ? localized : map;
		}

		std::string localize_gametype(const std::string& gametype)
		{
			const auto* localized = game::UI_LocalizeGametype(gametype.data());
			return localized && *localized ? localized : gametype;
		}

		std::string presence_text(const friend_record& record)
		{
			if (record.in_game)
			{
				if (record.same_match)
				{
					return "In your match";
				}

				if (record.mode == "sp")
				{
					return "Playing Campaign";
				}

				if (!record.map.empty() && !record.gametype.empty())
				{
					return utils::string::va("Playing %s on %s", localize_gametype(record.gametype).data(),
					                         localize_map(record.map).data());
				}

				return record.mode == "ext" ? "Playing Extinction" : "In Menus";
			}

			if (!record.game_id.empty())
			{
				return "In another game";
			}

			switch (persona_state(record))
			{
			case 1:
				return "Online";
			case 2:
			case 3:
				return "Away";
			default:
				return "Offline";
			}
		}

		// Built through rapidjson so a hostile id can't splice the line protocol.
		std::string json_line(const char* type, const std::string& friend_id)
		{
			rapidjson::Document doc;
			doc.SetObject();
			auto& allocator = doc.GetAllocator();
			doc.AddMember(rapidjson::StringRef("type"), rapidjson::StringRef(type), allocator);
			rapidjson::Value id;
			id.SetString(friend_id.data(), static_cast<rapidjson::SizeType>(friend_id.size()), allocator);
			doc.AddMember(rapidjson::StringRef("friendId"), id, allocator);

			rapidjson::StringBuffer buffer;
			rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
			doc.Accept(writer);
			return std::string(buffer.GetString(), buffer.GetSize());
		}

		// Invites only deliver when our own match is reachable, or is a closed hosted match the invite will open.
		bool can_invite(const friend_record& record)
		{
			return !record.same_match && (discord::get_join_transport().has_value() || nat::can_open_to_friends());
		}

		// Main thread. Commits a pending snapshot, then rebuilds the native list from the proxy in one pass.
		void update_native_list()
		{
			const auto committed = snapshot_pending.exchange(false) && get_store().access<bool>([](store& s)
			{
				if (!s.pending)
				{
					return false;
				}

				s.list = std::move(*s.pending);
				s.pending.reset();
				s.index.clear();
				for (size_t i = 0; i < s.list.size(); ++i)
				{
					s.index[s.list[i].steam_id_bits] = i;
				}

				return true;
			});

			if (committed)
			{
				refresh_pending = true;
			}

			auto* list = reinterpret_cast<std::uint8_t*>(NATIVE_LIST);
			if (!refresh_pending && !list[2040])
			{
				return;
			}

			list[2040] = 1;
			*reinterpret_cast<int*>(list + 2044) = 0;
			*reinterpret_cast<int*>(list + 2048) = 0;
			utils::hook::invoke<void>(NATIVE_REFRESH, 0);

			// A walk stamps its start time but leaves the flag set with the cache off; unstamped means LUI isn't up yet.
			if (*reinterpret_cast<int*>(list + 2044) != 0)
			{
				list[2040] = 0;
				refresh_pending = false;
			}
		}

		// Native gate is the Live sign-in, which iw6x never has.
		bool is_friend_joinable_stub(const int controller)
		{
			friend_record record{};
			if (get_selected(controller, record))
			{
				return record.joinable;
			}

			return is_friend_joinable_hook.invoke<bool>(controller);
		}

		bool is_friend_invitable_stub(const int controller)
		{
			friend_record record{};
			if (get_selected(controller, record))
			{
				return can_invite(record);
			}

			return is_friend_invitable_hook.invoke<bool>(controller);
		}

		// The launcher owns the join: it resolves the friend's transport and answers with a `connect` message.
		std::int64_t join_online_friend_stub(const int controller)
		{
			friend_record record{};
			if (!get_selected(controller, record))
			{
				return join_online_friend_hook.invoke<std::int64_t>(controller);
			}

			if (record.joinable)
			{
				console::info("[friends] join requested for '%s'\n", record.name.data());
				ipc::send_message(json_line("join-friend", record.id));
			}

			return 0;
		}

		// Main thread. Inviting is host consent, so a closed hosted match opens first.
		bool invite_online_friend_stub(const int controller)
		{
			friend_record record{};
			if (!get_selected(controller, record))
			{
				return invite_online_friend_hook.invoke<bool>(controller);
			}

			if (!can_invite(record))
			{
				return false;
			}

			const auto opens_match = nat::can_open_to_friends() && nat::open_to_friends();
			if (opens_match)
			{
				ipc::flush_presence();
			}

			console::info("[friends] invite requested for '%s'\n", record.name.data());
			ipc::send_message(json_line("invite", record.id));

			const auto name = toast::sanitize_name(record.name);
			toast::show("INVITE SENT", name.empty() ? "your friend" : name, opens_match ? "Match is now open to friends." : "");
			return true;
		}

		// Native text is only the persona state or "Call of Duty: Ghosts".
		std::int64_t friend_presence_stub(const unsigned int controller, const unsigned long long xuid, char* buffer,
		                                  const unsigned int size)
		{
			friend_record record{};
			if (!buffer || !size || !find_friend(xuid, record))
			{
				return friend_presence_hook.invoke<std::int64_t>(controller, xuid, buffer, size);
			}

			strncpy_s(buffer, size, presence_text(record).data(), _TRUNCATE);
			return 0;
		}

		// LUI's "live_connection" event and Engine.IsUserSignedInToLive gate the friends widget on the native Live flag.
		bool live_signed_in_stub()
		{
			return true;
		}
	}

	void apply_snapshot(std::vector<friend_record> entries)
	{
		get_store().access([&](store& s)
		{
			std::vector<friend_record> list;
			list.reserve(entries.size());

			std::unordered_set<std::string> seen;
			for (auto& record : entries)
			{
				if (!seen.emplace(record.id).second)
				{
					continue;
				}

				auto id = s.account_ids.find(record.id);
				if (id == s.account_ids.end())
				{
					id = s.account_ids.emplace(record.id, s.next_account_id++).first;
				}

				record.steam_id_bits = make_steam_id_bits(id->second);
				list.push_back(std::move(record));
			}

			s.pending = std::move(list);
		});

		snapshot_pending = true;
	}

	int get_count()
	{
		return get_store().access<int>([](const store& s)
		{
			return static_cast<int>(s.list.size());
		});
	}

	unsigned long long get_steam_id(const int index)
	{
		return get_store().access<unsigned long long>([&](const store& s)
		{
			return index >= 0 && static_cast<size_t>(index) < s.list.size() ? s.list[index].steam_id_bits : 0;
		});
	}

	bool find_friend(const unsigned long long steam_id_bits, friend_record& out)
	{
		return get_store().access<bool>([&](const store& s)
		{
			const auto entry = s.index.find(steam_id_bits);
			if (entry == s.index.end())
			{
				return false;
			}

			out = s.list[entry->second];
			return true;
		});
	}

	int persona_state(const friend_record& record)
	{
		if (record.status == "online") return 1;
		if (record.status == "dnd") return 2;
		if (record.status == "idle") return 3;
		return 0;
	}

	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			if (!game::environment::is_mp() || game::environment::is_dedi())
			{
				return;
			}

			// The native cache never drops removed friends or re-reads presence; the proxy is cheap to query live.
			dvars::override::register_int("friendsCacheSteamFriends", 0, 0, 1, game::DVAR_FLAG_NONE);

			utils::hook::call(0x1401EC328, live_signed_in_stub);
			utils::hook::call(0x1401F50B1, live_signed_in_stub);

			is_friend_joinable_hook.create(0x1405A0FE0, is_friend_joinable_stub);
			is_friend_invitable_hook.create(0x1405A0F20, is_friend_invitable_stub);
			join_online_friend_hook.create(0x1405A12C0, join_online_friend_stub);
			invite_online_friend_hook.create(0x1405A11B0, invite_online_friend_stub);
			friend_presence_hook.create(0x1404FACE0, friend_presence_stub);

			scheduler::loop(update_native_list, scheduler::pipeline::main);
		}
	};
}

REGISTER_COMPONENT(friends::component)
