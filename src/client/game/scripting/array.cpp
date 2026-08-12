#include <std_include.hpp>
#include "array.hpp"

namespace scripting
{
	namespace
	{
		unsigned int make_array()
		{
			unsigned int index = 0;
			const auto variable = game::AllocVariable(&index);
			variable->w.type = game::VAR_ARRAY;
			variable->u.f.prev = 0;
			variable->u.f.next = 0;
			return index;
		}
	}

	array::array(const unsigned int id)
		: id_(id)
	{
		this->add();
	}

	array::array(const array& other) : array(other.id_)
	{
	}

	array::array(array&& other) noexcept
	{
		this->id_ = other.id_;
		other.id_ = 0;
	}

	array::array()
	{
		this->id_ = make_array();
	}

	array::array(std::vector<script_value> values)
	{
		this->id_ = make_array();
		for (const auto& value : values)
		{
			this->push(value);
		}
	}

	array::array(std::unordered_map<std::string, script_value> values)
	{
		this->id_ = make_array();
		for (const auto& value : values)
		{
			this->set(value.first, value.second);
		}
	}

	array::~array()
	{
		this->release();
	}

	array& array::operator=(const array& other)
	{
		if (&other != this)
		{
			this->release();
			this->id_ = other.id_;
			this->add();
		}
		return *this;
	}

	array& array::operator=(array&& other) noexcept
	{
		if (&other != this)
		{
			this->release();
			this->id_ = other.id_;
			other.id_ = 0;
		}
		return *this;
	}

	void array::add() const
	{
		if (this->id_)
		{
			game::AddRefToValue(game::VAR_POINTER, {static_cast<int>(this->id_)});
		}
	}

	void array::release() const
	{
		if (this->id_)
		{
			game::RemoveRefToValue(game::VAR_POINTER, {static_cast<int>(this->id_)});
		}
	}

	std::vector<script_value> array::get_keys() const
	{
		std::vector<script_value> result;

		const auto offset = 0xC800 * (this->id_ & 1);
		auto current = game::scr_VarGlob->objectVariableChildren[this->id_].firstChild;

		for (auto i = offset + current; current; i = offset + current)
		{
			const auto var = game::scr_VarGlob->childVariableValue[i];

			if (var.type == game::VAR_UNDEFINED)
			{
				current = var.nextSibling;
				continue;
			}

			const auto string_value = static_cast<unsigned int>(
				static_cast<unsigned char>(var.name_lo) + (var.k.keys.name_hi << 8));
			const auto* str = game::SL_ConvertToString(string_value);

			script_value key;
			if (string_value < 0x40000 && str)
			{
				key = str;
			}
			else
			{
				key = static_cast<int>((string_value - 0x800000) & 0xFFFFFF);
			}

			result.push_back(key);

			current = var.nextSibling;
		}

		return result;
	}

	unsigned int array::size() const
	{
		return static_cast<unsigned int>(this->get_keys().size());
	}

	unsigned int array::push(const script_value& value) const
	{
		const auto index = this->size();
		this->set(index, value);
		return index;
	}

	script_value array::get(const script_value& key) const
	{
		if (key.is<int>())
		{
			return this->get(static_cast<unsigned int>(key.as<int>()));
		}
		return this->get(key.as<std::string>());
	}

	script_value array::get(const std::string& key) const
	{
		const auto string_value = game::SL_GetString(key.data(), 0);
		const auto variable_id = game::FindVariable(this->id_, string_value);
		if (!variable_id)
		{
			return {};
		}

		const auto value = game::scr_VarGlob->childVariableValue[variable_id + 0xC800 * (this->id_ & 1)];
		game::VariableValue variable{};
		variable.u = value.u.u;
		variable.type = value.type;
		return variable;
	}

	script_value array::get(const unsigned int index) const
	{
		const auto variable_id = game::FindVariable(this->id_, (index - 0x800000) & 0xFFFFFF);
		if (!variable_id)
		{
			return {};
		}

		const auto value = game::scr_VarGlob->childVariableValue[variable_id + 0xC800 * (this->id_ & 1)];
		game::VariableValue variable{};
		variable.u = value.u.u;
		variable.type = value.type;
		return variable;
	}

	void array::set(const script_value& key, const script_value& value) const
	{
		if (key.is<int>())
		{
			this->set(static_cast<unsigned int>(key.as<int>()), value);
		}
		else
		{
			this->set(key.as<std::string>(), value);
		}
	}

	void array::set(const std::string& key, const script_value& value) const
	{
		const auto& value_raw = value.get_raw();
		const auto variable_id = this->get_value_id(key);
		if (!variable_id)
		{
			return;
		}

		const auto variable = &game::scr_VarGlob->childVariableValue[variable_id + 0xC800 * (this->id_ & 1)];
		game::AddRefToValue(value_raw.type, value_raw.u);
		game::RemoveRefToValue(variable->type, variable->u.u);

		variable->type = static_cast<char>(value_raw.type);
		variable->u.u = value_raw.u;
	}

	void array::set(const unsigned int index, const script_value& value) const
	{
		const auto& value_raw = value.get_raw();
		const auto variable_id = this->get_value_id(index);
		if (!variable_id)
		{
			return;
		}

		const auto variable = &game::scr_VarGlob->childVariableValue[variable_id + 0xC800 * (this->id_ & 1)];
		game::AddRefToValue(value_raw.type, value_raw.u);
		game::RemoveRefToValue(variable->type, variable->u.u);

		variable->type = static_cast<char>(value_raw.type);
		variable->u.u = value_raw.u;
	}

	unsigned int array::get_entity_id() const
	{
		return this->id_;
	}

	unsigned int array::get_value_id(const std::string& key) const
	{
		const auto string_value = game::SL_GetString(key.data(), 0);
		const auto variable_id = game::FindVariable(this->id_, string_value);
		if (!variable_id)
		{
			return game::GetNewVariable(this->id_, string_value);
		}
		return variable_id;
	}

	unsigned int array::get_value_id(const unsigned int index) const
	{
		const auto variable_id = game::FindVariable(this->id_, (index - 0x800000) & 0xFFFFFF);
		if (!variable_id)
		{
			return game::GetNewArrayVariable(this->id_, index);
		}
		return variable_id;
	}

	entity array::get_raw() const
	{
		return entity(this->id_);
	}
}
