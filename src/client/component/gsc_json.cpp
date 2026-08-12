#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "game/game.hpp"
#include "game/scripting/array.hpp"
#include "game/scripting/entity.hpp"
#include "game/scripting/execution.hpp"
#include "game/scripting/script_value.hpp"

#include "gsc/script_extension.hpp"

#include <nlohmann/json.hpp>

namespace gsc_json
{
	namespace
	{
		nlohmann::json gsc_to_json(const scripting::script_value& value);

		nlohmann::json entity_to_json(const unsigned int id)
		{
			const scripting::array array(id);
			nlohmann::json obj;

			auto string_indexed = -1;
			const auto keys = array.get_keys();

			for (auto i = 0u; i < keys.size(); ++i)
			{
				const auto is_int = keys[i].is<int>();
				const auto is_string = keys[i].is<std::string>();

				if (string_indexed == -1)
				{
					string_indexed = is_string;
				}

				if (!string_indexed && is_int)
				{
					const auto index = keys[i].as<int>();
					obj[index] = gsc_to_json(array.get(static_cast<unsigned int>(index)));
				}
				else if (string_indexed && is_string)
				{
					const auto key = keys[i].as<std::string>();
					obj.emplace(key, gsc_to_json(array.get(key)));
				}
			}

			return obj;
		}

		nlohmann::json vector_to_json(const float* v)
		{
			nlohmann::json obj;
			obj.push_back(v[0]);
			obj.push_back(v[1]);
			obj.push_back(v[2]);
			return obj;
		}

		nlohmann::json gsc_to_json(const scripting::script_value& value)
		{
			const auto& variable = value.get_raw();
			const auto u = variable.u;

			switch (variable.type)
			{
			case game::VAR_UNDEFINED:
				return {};
			case game::VAR_INTEGER:
				return u.intValue;
			case game::VAR_FLOAT:
				return u.floatValue;
			case game::VAR_STRING:
			case game::VAR_ISTRING:
				return game::SL_ConvertToString(u.stringValue);
			case game::VAR_VECTOR:
				return vector_to_json(u.vectorValue);
			case game::VAR_POINTER:
				if (game::GetObjectType(u.uintValue) == game::VAR_ARRAY)
				{
					return entity_to_json(u.uintValue);
				}
				return "[struct]";
			case game::VAR_FUNCTION:
				return "[function]";
			default:
				return "[unknown]";
			}
		}

		scripting::script_value json_to_gsc(const nlohmann::json& obj)
		{
			switch (obj.type())
			{
			case nlohmann::detail::value_t::boolean:
				return obj.get<bool>() ? 1 : 0;
			case nlohmann::detail::value_t::number_integer:
			case nlohmann::detail::value_t::number_unsigned:
				return obj.get<int>();
			case nlohmann::detail::value_t::number_float:
				return obj.get<float>();
			case nlohmann::detail::value_t::string:
				return obj.get<std::string>();
			case nlohmann::detail::value_t::array:
			{
				const scripting::array arr;
				auto i = 0u;
				for (const auto& element : obj)
				{
					arr.set(i++, json_to_gsc(element));
				}
				return arr.get_raw();
			}
			case nlohmann::detail::value_t::object:
			{
				const scripting::array arr;
				for (const auto& [key, value] : obj.items())
				{
					arr.set(key, json_to_gsc(value));
				}
				return arr.get_raw();
			}
			default:
				return {};
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

			gsc::add_function("jsonparse", []
			{
				const std::string str = game::Scr_GetString(0);
				try
				{
					scripting::return_value(json_to_gsc(nlohmann::json::parse(str)));
				}
				catch (...)
				{
					scripting::return_value({});
				}
			});

			gsc::add_function("jsonserialize", []
			{
				const auto value = scripting::get_argument(0);
				auto indent = -1;
				if (game::Scr_GetNumParam() > 1)
				{
					indent = game::Scr_GetInt(1);
				}

				try
				{
					const auto str = gsc_to_json(value).dump(indent);
					game::Scr_AddString(str.data());
				}
				catch (...)
				{
					game::Scr_AddString("");
				}
			});
		}
	};
}

REGISTER_COMPONENT(gsc_json::component)
