#pragma once
#include <cstddef>
#include <string>

/**
 * Container that is designed to hold pointers to guest data, which can
 * be sequentialized in various ways.
**/

namespace riscv
{
	struct Buffer
	{
		bool    is_sequential() const noexcept { return m_idx == 1; }
		const auto& first() const { return m_data[0]; }
		const char* c_str() const noexcept { return first().first; }
		size_t      size() const noexcept { return m_len; }

		size_t copy_to(char* dst, size_t dstlen);
		void   foreach(std::function<void(const char*, size_t)> cb);
		std::string to_string() const;
		char* to_buffer(char* dest) const;
		char* to_buffer() const;

		Buffer() = default;
		void append_page(const char* data, size_t len);

	private:
		std::array<std::pair<const char*, size_t>, 4> m_data = {};
		uint32_t m_len  = 0; /* Total length */
		uint32_t m_idx  = 0; /* Current array index */
	};

	inline size_t Buffer::copy_to(char* dst, size_t maxlen)
	{
		size_t len = 0;
		for (const auto& entry : m_data) {
			if (entry.second == 0) break;
			if (UNLIKELY(len + entry.second > maxlen)) break;
			std::copy(entry.first, entry.first + entry.second, &dst[len]);
			len += entry.second;
		}
		return len;
	}

	inline void Buffer::foreach(std::function<void(const char*, size_t)> cb)
	{
		for (const auto& entry : m_data) {
			if (entry.second == 0) break;
			cb(entry.first, entry.second);
		}
	}

	inline void Buffer::append_page(const char* buffer, size_t len)
	{
		assert(len <= Page::size());
		assert(m_idx < m_data.size());
		m_len += len;
		m_data.at(m_idx ++) = {buffer, len};
	}

	inline std::string Buffer::to_string() const
	{
		std::string result;
		result.reserve(this->m_len);
		for (size_t i = 0; i < m_idx; i++) {
			auto& entry = m_data[i];
			result.append(entry.first, entry.first + entry.second);
		}
		return result;
	}

	inline char* Buffer::to_buffer(char* buffer) const
	{
		char* dest = buffer;
		for (size_t i = 0; i < m_idx; i++) {
			auto& entry = m_data[i];
			std::copy(entry.first, entry.first + entry.second, dest);
			dest += entry.second;
		}
		return buffer;
	}
	inline char* Buffer::to_buffer() const
	{
		return to_buffer(new char[this->m_len]);
	}
}
