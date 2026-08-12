#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "game/game.hpp"
#include "game/scripting/entity.hpp"
#include "game/scripting/execution.hpp"

#include "gsc/script_extension.hpp"
#include "scheduler.hpp"
#include "scripting.hpp"

#include <utils/http.hpp>

#include <unordered_set>

namespace gsc_http
{
	namespace
	{
		constexpr std::size_t max_result = 0x5000;

		std::unordered_set<unsigned int> active_handles;

		void return_handle(const scripting::entity& handle)
		{
			scripting::return_value(handle);
		}

		void finish(const unsigned int handle_id, const std::optional<std::string>& data, const std::string& error)
		{
			scheduler::once([handle_id, data, error]
			{
				if (!active_handles.contains(handle_id))
				{
					return;
				}
				active_handles.erase(handle_id);

				const scripting::entity handle{handle_id};
				if (data.has_value())
				{
					const auto& body = data.value();
					scripting::notify(handle, "done",
						{body.size() > max_result ? body.substr(0, max_result) : body, true});
				}
				else
				{
					scripting::notify(handle, "done", {std::string{}, false, error.empty() ? "Unknown error" : error});
				}
			}, scheduler::pipeline::server);
		}

		scripting::entity dispatch(const std::string& url, const std::string& method, const std::string& body,
			const utils::http::headers& headers)
		{
			const auto handle = scripting::call("spawnstruct", {}).as<scripting::entity>();
			const auto handle_id = handle.get_entity_id();
			active_handles.insert(handle_id);

			scheduler::once([handle_id, url, method, body, headers]
			{
				std::optional<std::string> data;
				std::string error;
				try
				{
					data = (method == "POST")
						? utils::http::post_data(url, body, headers)
						: utils::http::get_data(url, headers);
				}
				catch (const std::exception& e)
				{
					error = e.what();
				}
				finish(handle_id, data, error);
			}, scheduler::pipeline::async);

			return handle;
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

			scripting::on_shutdown([](int, int)
			{
				active_handles.clear();
			});

			const auto http_get = []
			{
				const std::string url = game::Scr_GetString(0);
				return_handle(dispatch(url, "GET", {}, {}));
			};
			gsc::add_function("httpget", http_get);
			gsc::add_function("curl", http_get);
			gsc::add_function("http::get", http_get);

			gsc::add_function("httppost", []
			{
				const std::string url = game::Scr_GetString(0);
				const std::string body = game::Scr_GetNumParam() > 1 ? game::Scr_GetString(1) : "";
				return_handle(dispatch(url, "POST", body, {}));
			});

			gsc::add_function("http::request", []
			{
				const std::string url = game::Scr_GetString(0);
				std::string body{};
				std::string method = "GET";

				if (game::Scr_GetNumParam() > 1)
				{
					const scripting::entity options{game::Scr_GetObject(1)};
					const auto body_val = options.get("body");
					if (body_val.is<std::string>())
					{
						body = body_val.as<std::string>();
						method = "POST";
					}
				}

				return_handle(dispatch(url, method, body, {}));
			});
		}
	};
}

REGISTER_COMPONENT(gsc_http::component)
