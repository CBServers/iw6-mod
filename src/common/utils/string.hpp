#pragma once
#include "memory.hpp"
#include <cstdint>

template <class Type, size_t n>
constexpr auto ARRAY_COUNT(Type(&)[n]) { return n; }

namespace utils::string
{
	template <size_t Buffers, size_t MinBufferSize>
	class va_provider final
	{
	public:
		static_assert(Buffers != 0 && MinBufferSize != 0, "Buffers and MinBufferSize mustn't be 0");

		va_provider() : current_buffer_(0)
		{
		}

		char* get(const char* format, const va_list ap)
		{
			++this->current_buffer_ %= ARRAY_COUNT(this->string_pool_);
			auto entry = &this->string_pool_[this->current_buffer_];

			if (!entry->size_ || !entry->buffer_)
			{
				throw std::runtime_error("String pool not initialized");
			}

			while (true)
			{
				const int res = vsnprintf(entry->buffer_, entry->size_, format, ap);
				if (res > 0) break; // Success
				if (res == 0) return nullptr; // Error

				entry->double_size();
			}

			return entry->buffer_;
		}

	private:
		class entry final
		{
		public:
			explicit entry(const size_t size = MinBufferSize) : size_(size), buffer_(nullptr)
			{
				this->size_ = std::max<size_t>(this->size_, MinBufferSize);
				this->allocate();
			}

			~entry()
			{
				if (this->buffer_) memory::get_allocator()->free(this->buffer_);
				this->size_ = 0;
				this->buffer_ = nullptr;
			}

			void allocate()
			{
				if (this->buffer_) memory::get_allocator()->free(this->buffer_);
				this->buffer_ = memory::get_allocator()->allocate_array<char>(this->size_ + 1);
			}

			void double_size()
			{
				this->size_ *= 2;
				this->allocate();
			}

			size_t size_;
			char* buffer_;
		};

		size_t current_buffer_;
		entry string_pool_[Buffers];
	};

	[[nodiscard]] const char* va(const char* fmt, ...);

	[[nodiscard]] std::vector<std::string> split(const std::string& s, char delim);

	[[nodiscard]] std::string to_lower(const std::string& text);
	[[nodiscard]] std::string to_upper(const std::string& text);
	[[nodiscard]] bool starts_with(const std::string& text, const std::string& substring);
	[[nodiscard]] bool ends_with(const std::string& text, const std::string& substring);
	[[nodiscard]] bool compare(const std::string& lhs, const std::string& rhs);
	[[nodiscard]] bool is_numeric(const std::string& text);

	[[nodiscard]] std::string dump_hex(const std::string& data, const std::string& separator = " ");

	[[nodiscard]] std::string get_clipboard_data();

	void strip(const char* in, char* out, size_t max);

	[[nodiscard]] std::string convert(const std::wstring& wstr);
	[[nodiscard]] std::wstring convert(const std::string& str);

	[[nodiscard]] std::string replace(std::string str, const std::string& from, const std::string& to);
}
