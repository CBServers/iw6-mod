#include <std_include.hpp>
#include "loader/component_loader.hpp"
#include "game/game.hpp"

#include "console.hpp"
#include "nat.hpp"
#include "network.hpp"
#include "scheduler.hpp"
#include "upnp.hpp"

#include <utils/string.hpp>

#include <natupnp.h>

#include <mutex>

namespace upnp
{
	namespace
	{
		constexpr auto MAPPING_DESCRIPTION = L"IW6x (CBServers)";

		game::dvar_t* nat_upnp{};

		// Guards the mapping state below; the COM work itself runs on the async pipeline.
		std::mutex state_mutex;
		uint32_t generation{}; // bumped on close/reject so a late async map can't resurrect a mapping
		bool attempt_in_flight{};
		bool mapping_rejected{}; // verified useless this session (CGNAT/double NAT or reassigned port)
		uint16_t mapped_port{};
		std::string external_ip{}; // router-reported WAN IP, may be empty on routers that don't expose it

		// COM must be initialized per-thread; RPC_E_CHANGED_MODE means it already is (as STA), which is fine.
		struct com_scope
		{
			HRESULT hr;
			com_scope() : hr(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
			~com_scope() { if (SUCCEEDED(hr)) CoUninitialize(); }
		};

		// Blocking (SSDP discovery can take seconds); async pipeline or shutdown only.
		CComPtr<IStaticPortMappingCollection> get_mapping_collection()
		{
			CComPtr<IUPnPNAT> nat_com{};
			if (FAILED(CoCreateInstance(__uuidof(UPnPNAT), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&nat_com))) ||
				!nat_com)
			{
				console::warn("[upnp] failed to create UPnP COM client\n");
				return {};
			}

			CComPtr<IStaticPortMappingCollection> mappings{};
			if (FAILED(nat_com->get_StaticPortMappingCollection(&mappings)) || !mappings)
			{
				console::info("[upnp] no UPnP gateway found (disabled on the router?)\n");
				return {};
			}

			return mappings;
		}

		// Mappings have no lease; a crash leaks them forever, so drop any prior entry of ours first.
		void remove_stale_mappings(IStaticPortMappingCollection* mappings)
		{
			CComPtr<IUnknown> enum_unknown{};
			if (FAILED(mappings->get__NewEnum(&enum_unknown)) || !enum_unknown)
			{
				return;
			}

			CComPtr<IEnumVARIANT> enumerator{};
			if (FAILED(enum_unknown->QueryInterface(&enumerator)) || !enumerator)
			{
				return;
			}

			std::vector<std::pair<long, CComBSTR>> stale;
			VARIANT item;
			VariantInit(&item);
			while (enumerator->Next(1, &item, nullptr) == S_OK)
			{
				CComPtr<IStaticPortMapping> mapping{};
				if (item.vt == VT_DISPATCH && item.pdispVal &&
					SUCCEEDED(item.pdispVal->QueryInterface(&mapping)) && mapping)
				{
					CComBSTR description{}, protocol{};
					long port{};
					if (SUCCEEDED(mapping->get_Description(&description)) && description &&
						wcscmp(description, MAPPING_DESCRIPTION) == 0 &&
						SUCCEEDED(mapping->get_ExternalPort(&port)) &&
						SUCCEEDED(mapping->get_Protocol(&protocol)) && protocol)
					{
						stale.emplace_back(port, protocol);
					}
				}

				VariantClear(&item);
			}

			for (const auto& [port, protocol] : stale)
			{
				console::info("[upnp] removing stale mapping for port %ld\n", port);
				mappings->Remove(port, protocol);
			}
		}

		void do_unmap(const uint16_t port)
		{
			const com_scope com{};
			const auto mappings = get_mapping_collection();
			if (mappings)
			{
				mappings->Remove(port, CComBSTR(L"UDP"));
				console::info("[upnp] removed mapping for UDP port %hu\n", port);
			}
		}

		// Blocking COM work; runs on the async pipeline.
		void map_port(const uint32_t gen, const uint16_t port)
		{
			bool mapped = false;
			bool rejected = false;
			std::string wan_ip{};

			{
				const com_scope com{};
				const auto mappings = get_mapping_collection();
				if (mappings)
				{
					remove_stale_mappings(mappings);

					const auto lan_ip = nat::get_local_ip();
					CComPtr<IStaticPortMapping> mapping{};
					const auto hr = lan_ip.empty()
						? E_FAIL
						: mappings->Add(port, CComBSTR(L"UDP"), port, CComBSTR(lan_ip.data()),
							VARIANT_TRUE, CComBSTR(MAPPING_DESCRIPTION), &mapping);
					if (FAILED(hr) || !mapping)
					{
						console::warn("[upnp] mapping UDP port %hu failed (0x%08lX)\n", port, static_cast<unsigned long>(hr));
					}
					else
					{
						// Some routers reassign on conflict; a shifted port doesn't back our advertised endpoint.
						long actual_port{};
						if (SUCCEEDED(mapping->get_ExternalPort(&actual_port)) && actual_port != port)
						{
							console::warn("[upnp] router reassigned external port to %ld; dropping useless mapping\n",
								actual_port);
							mappings->Remove(actual_port, CComBSTR(L"UDP"));
							rejected = true;
						}
						else
						{
							CComBSTR wan{};
							if (SUCCEEDED(mapping->get_ExternalIPAddress(&wan)) && wan)
							{
								wan_ip = utils::string::convert(std::wstring(wan.m_str));
							}

							console::info("[upnp] mapped UDP port %hu -> %s (WAN IP %s)\n", port, lan_ip.data(),
								wan_ip.empty() ? "unknown" : wan_ip.data());
							mapped = true;
						}
					}
				}
			}

			bool orphaned = false;
			{
				std::lock_guard<std::mutex> lock(state_mutex);
				attempt_in_flight = false;
				if (rejected)
				{
					mapping_rejected = true;
				}
				else if (mapped)
				{
					if (gen == generation)
					{
						mapped_port = port;
						external_ip = wan_ip;
					}
					else
					{
						orphaned = true; // closed while we were mapping
					}
				}
			}

			if (orphaned)
			{
				do_unmap(port);
			}
		}
	}

	void ensure_mapped()
	{
		if (!nat_upnp || !nat_upnp->current.enabled)
		{
			return;
		}

		const auto port = network::get_bound_port();
		if (!port)
		{
			return;
		}

		uint32_t gen{};
		{
			std::lock_guard<std::mutex> lock(state_mutex);
			if (attempt_in_flight || mapping_rejected || mapped_port)
			{
				return;
			}

			attempt_in_flight = true;
			gen = generation;
		}

		scheduler::once([gen, port]
		{
			map_port(gen, port);
		}, scheduler::pipeline::async);
	}

	void on_public_endpoint(const std::string& endpoint)
	{
		const auto ip = endpoint.substr(0, endpoint.find(':'));

		uint16_t port{};
		{
			std::lock_guard<std::mutex> lock(state_mutex);
			if (!mapped_port || external_ip.empty() || external_ip == ip)
			{
				return;
			}

			console::warn("[upnp] router WAN IP %s != observed public IP %s (CGNAT/double NAT?); dropping useless mapping\n",
				external_ip.data(), ip.data());
			mapping_rejected = true;
			port = mapped_port;
			mapped_port = 0;
			external_ip.clear();
			++generation;
		}

		scheduler::once([port]
		{
			do_unmap(port);
		}, scheduler::pipeline::async);
	}

	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			if (!game::environment::is_mp())
			{
				return;
			}

			scheduler::once([]
			{
				nat_upnp = game::Dvar_RegisterBool("nat_upnp", true, game::DVAR_FLAG_NONE,
					"Request a UPnP port mapping for the game port");
			}, scheduler::pipeline::main);

			// Map once the game socket is bound so the mapping is live before any punch attempt.
			scheduler::schedule([]
			{
				if (!nat_upnp || !network::get_bound_port())
				{
					return scheduler::cond_continue;
				}

				ensure_mapped();
				return scheduler::cond_end;
			}, scheduler::pipeline::main, 1s);
		}

		void pre_destroy() override
		{
			uint16_t port{};
			{
				std::lock_guard<std::mutex> lock(state_mutex);
				++generation; // an in-flight map won't adopt; next-launch stale cleanup covers its window
				port = mapped_port;
				mapped_port = 0;
			}

			if (port)
			{
				do_unmap(port);
			}
		}
	};
}

REGISTER_COMPONENT(upnp::component)
