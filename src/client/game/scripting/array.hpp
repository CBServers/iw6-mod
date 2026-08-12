#pragma once
#include "game/game.hpp"
#include "entity.hpp"
#include "script_value.hpp"

namespace scripting
{
	class array final
	{
	public:
		array();
		array(unsigned int);
		array(std::vector<script_value>);
		array(std::unordered_map<std::string, script_value>);

		array(const array& other);
		array(array&& other) noexcept;
		~array();

		array& operator=(const array& other);
		array& operator=(array&& other) noexcept;

		std::vector<script_value> get_keys() const;
		unsigned int size() const;

		unsigned int push(const script_value&) const;

		script_value get(const script_value&) const;
		script_value get(const std::string&) const;
		script_value get(unsigned int) const;

		void set(const script_value&, const script_value&) const;
		void set(const std::string&, const script_value&) const;
		void set(unsigned int, const script_value&) const;

		unsigned int get_entity_id() const;
		unsigned int get_value_id(const std::string&) const;
		unsigned int get_value_id(unsigned int) const;

		entity get_raw() const;

	private:
		unsigned int id_{};

		void add() const;
		void release() const;
	};
}
