#pragma once
#include <cstddef>
#include <cstring>
#include <string>
#include <type_traits>

namespace minipb {
	enum class result {
		ok,
		general_error,
		out_of_space,
		invalid_position,
		out_of_memory,
		invalid_input,
	};

	class output_stream {
	protected:
		~output_stream() = default;

	public:
		virtual size_t position() const noexcept = 0;
		virtual result write(const void* data, size_t data_size) noexcept = 0;
		virtual result write_at(size_t pos, const void* data, size_t data_size) noexcept = 0;
	};

	class array_output_stream final : public output_stream {
		unsigned char* m_start;
		unsigned char* m_end;
		unsigned char* m_current;

	public:
		array_output_stream(void* array, size_t array_size) noexcept
			: m_start{reinterpret_cast<unsigned char*>(array)}, m_end{m_start + array_size}, m_current{m_start} {}
		template <typename T, size_t N>
		array_output_stream(T (&data)[N]) noexcept : array_output_stream(data, sizeof(T) * N) {
			static_assert(std::is_pod<T>::value, "Array needs to only contain pod types");
		}
		size_t bytes_used() const noexcept { return m_current - m_start; }
		size_t bytes_available() const noexcept { return m_end - m_current; }
		size_t position() const noexcept override { return m_current - m_start; }
		result write(const void* data, size_t data_size) noexcept override {
			if (data_size > bytes_available()) return result::out_of_space;
			memcpy(m_current, data, data_size);
			m_current += data_size;
			return result::ok;
		}
		result write_at(size_t pos, const void* data, size_t data_size) noexcept override {
			if (pos + data_size > bytes_used()) return result::invalid_position;
			memcpy(m_start + pos, data, data_size);
			return result::ok;
		}
		void reset() noexcept { m_current = m_start; }
	};

	/**
     * \brief Container output stream for all containers supporting resize(size_t), size() and non const data().
     * 
     * The element type/size is detected from the pointer returned by data() and used with resize() to treat the
     * container as a chunk of raw memory. The result might have extra zero bytes at the end if the element size
     * is more than one.
     */
	template <typename T>
	class container_output_stream final : public output_stream {
		T& m_container;
		size_t m_base_size;
		size_t m_offset{0};
		using element_type = typename std::remove_const<typename std::remove_cv<typename std::remove_reference<decltype(*std::declval<T&>().data())>::type>::type>::type;
		static_assert(std::is_pod<element_type>::value, "Container needs to only contain pod types");

	public:
		container_output_stream(T& container) noexcept
			: m_container{container}, m_base_size{container.size()} {}
		size_t bytes_used() const noexcept { return m_offset; }
		size_t position() const noexcept override { return m_offset; }
		result write(const void* data, size_t data_size) noexcept override {
			try {
				if ((m_container.size() - m_base_size) * sizeof(element_type) < (m_offset + data_size)) {
					m_container.resize(((m_offset + data_size + sizeof(element_type) - 1) / sizeof(element_type)) + m_base_size);
				}
				memcpy(reinterpret_cast<unsigned char*>(const_cast<element_type*>(m_container.data()) + m_base_size) + m_offset, data, data_size);
				m_offset += data_size;
				return result::ok;
			} catch (...) {
				return result::general_error;
			}
		}
		result write_at(size_t pos, const void* data, size_t data_size) noexcept override {
			if (pos + data_size > bytes_used()) return result::invalid_position;
			memcpy(reinterpret_cast<unsigned char*>(const_cast<element_type*>(m_container.data()) + m_base_size) + pos, data, data_size);
			return result::ok;
		}
		void reset() noexcept {
			m_offset = 0;
			m_container.resize(m_base_size);
		}
	};

	class input_stream {
	protected:
		~input_stream() = default;

	public:
		virtual result read(void* data, size_t data_size) noexcept = 0;
		virtual result skip(size_t data_size) noexcept = 0;
		virtual size_t peek(void*, size_t) noexcept { return 0; }
		virtual size_t bytes_available() const noexcept = 0;
	};

	class array_input_stream final : public input_stream {
		unsigned char* m_start;
		unsigned char* m_end;
		unsigned char* m_current;

	public:
		array_input_stream(void* array, size_t array_size) noexcept
			: m_start{reinterpret_cast<unsigned char*>(array)}, m_end{m_start + array_size}, m_current{m_start} {}
		template <typename T, size_t N>
		array_input_stream(T (&data)[N]) noexcept : array_input_stream(data, sizeof(T) * N) {
			static_assert(std::is_pod<T>::value, "Array needs to only contain pod types");
		}
		size_t bytes_used() const noexcept { return m_current - m_start; }
		size_t bytes_available() const noexcept override { return m_end - m_current; }
		result read(void* data, size_t data_size) noexcept override {
			if (data_size > bytes_available()) return result::out_of_space;
			memcpy(data, m_current, data_size);
			m_current += data_size;
			return result::ok;
		}
		result skip(size_t data_size) noexcept override {
			if (data_size > bytes_available()) return result::out_of_space;
			m_current += data_size;
			return result::ok;
		}
		size_t peek(void* data, size_t data_size) noexcept override {
			if (data_size > bytes_available()) data_size = bytes_available();
			memcpy(data, m_current, data_size);
			return data_size;
		}
		void reset() noexcept { m_current = m_start; }
	};

	/**
     * \brief Container input stream for all containers supporting size() and data().
     * 
     * The element type/size is detected from the pointer returned by data() and used with size() to treat the
     * container as a chunk of raw memory.
     */
	class container_input_stream final : public input_stream {
		array_input_stream m_array;

	public:
		template <typename T, typename element_type = typename std::remove_const<typename std::remove_cv<typename std::remove_reference<decltype(*std::declval<T&>().data())>::type>::type>::type>
		container_input_stream(T& container) noexcept
			: m_array{const_cast<element_type*>(container.data()), container.size() * sizeof(element_type)} {
			static_assert(std::is_pod<element_type>::value, "Container needs to only contain pod types");
		}
		size_t bytes_used() const noexcept { return m_array.bytes_used(); }
		size_t bytes_available() const noexcept override { return m_array.bytes_available(); }
		result read(void* data, size_t data_size) noexcept override { return m_array.read(data, data_size); }
		result skip(size_t data_size) noexcept override { return m_array.skip(data_size); }
		size_t peek(void* data, size_t data_size) noexcept override { return m_array.peek(data, data_size); }
		void reset() noexcept { m_array.reset(); }
	};

	class subset_input_stream final : public input_stream {
		input_stream& m_parent;
		size_t m_length;
		size_t m_position{0};

	public:
		subset_input_stream(input_stream& parent, size_t len) noexcept
			: m_parent{parent}, m_length{parent.bytes_available()} {
			if (len < m_length) m_length = len;
		}
		size_t bytes_available() const noexcept override { return m_length - m_position; }
		result read(void* data, size_t data_size) noexcept override {
			if (data_size > bytes_available()) return result::out_of_space;
			auto res = m_parent.read(data, data_size);
			if (res == result::ok) m_position += data_size;
			return res;
		}
		result skip(size_t data_size) noexcept override {
			if (data_size > bytes_available()) return result::out_of_space;
			auto res = m_parent.skip(data_size);
			if (res == result::ok) m_position += data_size;
			return res;
		}
		size_t peek(void* data, size_t data_size) noexcept override {
			if (data_size > bytes_available()) data_size = bytes_available();
			return m_parent.peek(data, data_size);
		}
	};

	enum class wire_type {
		varint = 0,
		fixed64 = 1,
		length_blob = 2,
		group_start = 3,
		group_end = 4,
		fixed32 = 5,
	};

	class encoder final {
		output_stream& m_stream;

	public:
		encoder(output_stream& stream) noexcept
			: m_stream{stream} {}

		output_stream& stream() const noexcept { return m_stream; }

		result varint(uint64_t val) noexcept {
			uint8_t buf[10];
			auto i = varint_build(val, buf);
			return m_stream.write(buf, i);
		}

		result varint_signed(int64_t val) noexcept {
			if (val < 0)
				return varint(~(static_cast<uint64_t>(val) << 1));
			else
				return varint(static_cast<uint64_t>(val) << 1);
		}

		result field_header(uint64_t field_id, wire_type type) noexcept {
			return varint(field_id << 3 | static_cast<uint64_t>(type));
		}

		result fixed32(int32_t val) noexcept { return m_stream.write(&val, sizeof(val)); }
		result fixed32(uint32_t val) noexcept { return m_stream.write(&val, sizeof(val)); }
		result fixed32(float val) noexcept { return m_stream.write(&val, sizeof(val)); }

		result fixed64(int64_t val) noexcept { return m_stream.write(&val, sizeof(val)); }
		result fixed64(uint64_t val) noexcept { return m_stream.write(&val, sizeof(val)); }
		result fixed64(double val) noexcept { return m_stream.write(&val, sizeof(val)); }

		result fixed(const void* val, size_t len) noexcept { return m_stream.write(val, len); }

		static size_t varint_size(int64_t v) noexcept {
			if (v < (1 << 8)) return 1;
			if (v < (1 << 15)) return 2;
			if (v < (1 << 22)) return 3;
			if (v < (1 << 29)) return 4;
			if (v < (1l << 36)) return 5;
			if (v < (1l << 43)) return 7;
			if (v < (1l << 50)) return 8;
			if (v < (1l << 57)) return 9;
			return 10;
		}
		static size_t varint_build(int64_t val, uint8_t* buf) noexcept {
			buf[0] = val & 0x7f;
			val >>= 7;
			int i = 1;
			for (; val != 0; i++) {
				buf[i - 1] |= 0x80;
				buf[i] = val & 0x7f;
				val >>= 7;
			}
			return i;
		}
	};

	class msg_builder final {
		encoder m_encoder;
		result m_error{result::ok};

	public:
		msg_builder(output_stream& stream)
			: m_encoder{stream} {}

		result double_field(int64_t field_id, double value) noexcept {
			if (m_error != result::ok) return m_error;
			m_error = m_encoder.field_header(field_id, wire_type::fixed64);
			if (m_error == result::ok) m_error = m_encoder.fixed64(value);
			return m_error;
		}
		result float_field(int64_t field_id, float value) noexcept {
			if (m_error != result::ok) return m_error;
			m_error = m_encoder.field_header(field_id, wire_type::fixed32);
			if (m_error == result::ok) m_error = m_encoder.fixed32(value);
			return m_error;
		}
		result int32_field(int64_t field_id, int32_t value) noexcept {
			if (m_error != result::ok) return m_error;
			m_error = m_encoder.field_header(field_id, wire_type::varint);
			if (m_error == result::ok) m_error = m_encoder.varint(static_cast<uint32_t>(value));
			return m_error;
		}
		result int64_field(int64_t field_id, int64_t value) noexcept {
			if (m_error != result::ok) return m_error;
			m_error = m_encoder.field_header(field_id, wire_type::varint);
			if (m_error == result::ok) m_error = m_encoder.varint(static_cast<uint64_t>(value));
			return m_error;
		}
		result uint32_field(int64_t field_id, uint32_t value) noexcept {
			if (m_error != result::ok) return m_error;
			m_error = m_encoder.field_header(field_id, wire_type::varint);
			if (m_error == result::ok) m_error = m_encoder.varint(value);
			return m_error;
		}
		result uint64_field(int64_t field_id, uint64_t value) noexcept {
			if (m_error != result::ok) return m_error;
			m_error = m_encoder.field_header(field_id, wire_type::varint);
			if (m_error == result::ok) m_error = m_encoder.varint(value);
			return m_error;
		}
		result sint32_field(int64_t field_id, int32_t value) noexcept {
			if (m_error != result::ok) return m_error;
			m_error = m_encoder.field_header(field_id, wire_type::varint);
			if (m_error == result::ok) m_error = m_encoder.varint_signed(value);
			return m_error;
		}
		result sint64_field(int64_t field_id, int64_t value) noexcept {
			if (m_error != result::ok) return m_error;
			m_error = m_encoder.field_header(field_id, wire_type::varint);
			if (m_error == result::ok) m_error = m_encoder.varint_signed(value);
			return m_error;
		}
		result fixed32_field(int64_t field_id, uint32_t value) noexcept {
			if (m_error != result::ok) return m_error;
			m_error = m_encoder.field_header(field_id, wire_type::fixed32);
			if (m_error == result::ok) m_error = m_encoder.fixed32(value);
			return m_error;
		}
		result fixed64_field(int64_t field_id, uint64_t value) noexcept {
			if (m_error != result::ok) return m_error;
			m_error = m_encoder.field_header(field_id, wire_type::fixed64);
			if (m_error == result::ok) m_error = m_encoder.fixed64(value);
			return m_error;
		}
		result sfixed32_field(int64_t field_id, int32_t value) noexcept {
			if (m_error != result::ok) return m_error;
			m_error = m_encoder.field_header(field_id, wire_type::fixed32);
			if (m_error == result::ok) m_error = m_encoder.fixed32(value);
			return m_error;
		}
		result sfixed64_field(int64_t field_id, int64_t value) noexcept {
			if (m_error != result::ok) return m_error;
			m_error = m_encoder.field_header(field_id, wire_type::fixed64);
			if (m_error == result::ok) m_error = m_encoder.fixed64(value);
			return m_error;
		}
		result bool_field(int64_t field_id, bool value) noexcept {
			if (m_error != result::ok) return m_error;
			m_error = m_encoder.field_header(field_id, wire_type::varint);
			if (m_error == result::ok) m_error = m_encoder.varint(value ? 1 : 0);
			return m_error;
		}
		result string_field(int64_t field_id, const char* value, size_t len) noexcept {
			if (m_error != result::ok) return m_error;
			m_error = m_encoder.field_header(field_id, wire_type::length_blob);
			if (m_error == result::ok) m_error = m_encoder.varint(len);
			if (m_error == result::ok) m_error = m_encoder.fixed(value, len);
			return m_error;
		}
		result string_field(int64_t field_id, const std::string& value) noexcept {
			if (m_error != result::ok) return m_error;
			m_error = string_field(field_id, value.c_str(), value.size());
			return m_error;
		}

		template <typename T>
		result message_field(int64_t field_id, const T& msg) noexcept {
			if (m_error != result::ok) return m_error;
			// This gives us a worst case estimate of the blob size
			auto size = msg.estimate_size();
			if (size == 0) size = SIZE_MAX;
			auto dummy_size = encoder::varint_size(size);
			uint8_t dummy_varint[10] = {};
			// We write the object header
			m_error = m_encoder.field_header(field_id, wire_type::length_blob);
			if (m_error != result::ok) return m_error;
			// note down the current position
			auto pos = m_encoder.stream().position();
			// write a dummy varint based on the estimated size
			m_error = m_encoder.fixed(dummy_varint, dummy_size);
			if (m_error != result::ok) return m_error;
			// and hand of encoding to the message type
			m_error = msg.encode(*this);
			if (m_error != result::ok) return m_error;
			// after it is done, we calculate the size difference
			auto real_size = m_encoder.stream().position() - (pos + dummy_size);
			if (real_size > size) return minipb::result::general_error;
			// Build our real size and patch it to the dummy size
			encoder::varint_build(real_size, dummy_varint);
			for (size_t i = 0; i < dummy_size - 1; i++)
				dummy_varint[i] |= 0x80;
			// Patch out our dummy size
			m_error = m_encoder.stream().write_at(pos, dummy_varint, dummy_size);
			return m_error;
		}

		template <typename T>
		result packed_fixed64_field(int64_t field_id, const T& value) noexcept {
			if (m_error != result::ok) return m_error;
			m_error = m_encoder.field_header(field_id, wire_type::length_blob);
			if (m_error == result::ok) m_error = m_encoder.varint(value.size() * 8);
			for(auto e : value) {
                if (m_error != result::ok) break;
                m_error = m_encoder.fixed64(e);
            }
			return m_error;
		}
		template <typename T>
		result packed_fixed32_field(int64_t field_id, const T& value) noexcept {
			if (m_error != result::ok) return m_error;
			m_error = m_encoder.field_header(field_id, wire_type::length_blob);
			if (m_error == result::ok) m_error = m_encoder.varint(value.size() * 4);
			for(auto e : value) {
                if (m_error != result::ok) break;
                m_error = m_encoder.fixed32(e);
            }
			return m_error;
		}
        template <typename T>
		result packed_varint_field(int64_t field_id, const T& value) noexcept {
			if (m_error != result::ok) return m_error;
			m_error = m_encoder.field_header(field_id, wire_type::length_blob);
			// Worst case estimate
            auto dummy_size = encoder::varint_size(10 * value.size());
			uint8_t dummy_varint[10] = {};
            // note down the current position
			auto pos = m_encoder.stream().position();
			// write a dummy varint based on the estimated size
			m_error = m_encoder.fixed(dummy_varint, dummy_size);
			if (m_error != result::ok) return m_error;
            // Write our data
			for(auto e : value) {
                if (m_error != result::ok) break;
                m_error = m_encoder.varint(e);
            }
            // after it is done, we calculate the size difference
			auto real_size = m_encoder.stream().position() - (pos + dummy_size);
			if (real_size > 10 * value.size()) return minipb::result::general_error;
			// Build our real size and patch it to the dummy size
			encoder::varint_build(real_size, dummy_varint);
			for (size_t i = 0; i < dummy_size - 1; i++)
				dummy_varint[i] |= 0x80;
			// Patch out our dummy size
			m_error = m_encoder.stream().write_at(pos, dummy_varint, dummy_size);
			return m_error;
		}
        template <typename T>
		result packed_varint_signed_field(int64_t field_id, const T& value) noexcept {
			if (m_error != result::ok) return m_error;
			m_error = m_encoder.field_header(field_id, wire_type::length_blob);
			// Worst case estimate
            auto dummy_size = encoder::varint_size(10 * value.size());
			uint8_t dummy_varint[10] = {};
            // note down the current position
			auto pos = m_encoder.stream().position();
			// write a dummy varint based on the estimated size
			m_error = m_encoder.fixed(dummy_varint, dummy_size);
			if (m_error != result::ok) return m_error;
            // Write our data
			for(auto e : value) {
                if (m_error != result::ok) break;
                m_error = m_encoder.varint_signed(e);
            }
            // after it is done, we calculate the size difference
			auto real_size = m_encoder.stream().position() - (pos + dummy_size);
			if (real_size > 10 * value.size()) return minipb::result::general_error;
			// Build our real size and patch it to the dummy size
			encoder::varint_build(real_size, dummy_varint);
			for (size_t i = 0; i < dummy_size - 1; i++)
				dummy_varint[i] |= 0x80;
			// Patch out our dummy size
			m_error = m_encoder.stream().write_at(pos, dummy_varint, dummy_size);
			return m_error;
		}

		result last_error() const noexcept { return m_error; }
	};

	class decoder final {
		input_stream& m_stream;

	public:
		decoder(input_stream& stream) noexcept
			: m_stream{stream} {}

		input_stream& stream() const noexcept { return m_stream; }

		result varint(uint64_t& val) noexcept {
			uint8_t buf[10]{};
			val = 0;
			auto peek_size = m_stream.peek(buf, sizeof(buf));
			if (peek_size == 0) { // Peek unsupported or no data
				for (size_t i = 0; i < 10; i++) {
					auto res = m_stream.read(&buf[0], 1);
					if (res != result::ok) return res;
					val |= static_cast<uint64_t>(buf[i] & 0x7f) << (i * 7);
					if ((buf[i] & 0x80) == 0) return res;
				}
			} else {
				for (size_t i = 0; i < peek_size; i++) {
					val |= static_cast<uint64_t>(buf[i] & 0x7f) << (i * 7);
					if ((buf[i] & 0x80) == 0) {
						return m_stream.skip(i + 1);
					}
				}
			}

			return result::invalid_input;
		}

		result varint_signed(int64_t& val) noexcept {
			uint64_t v;
			auto res = varint(v);
			if (res != result::ok) return res;
			if (v & 0x01)
				val = static_cast<int64_t>(~(v >> 1));
			else
				val = static_cast<int64_t>(v);
			return result::ok;
		}

		result field_header(uint64_t& field_id, wire_type& type) noexcept {
			uint64_t v;
			auto res = varint(v);
			if (res != result::ok) return res;
			field_id = v >> 3;
			type = static_cast<wire_type>(v & 0x7);
			return result::ok;
		}

		result fixed32(int32_t& val) noexcept { return m_stream.read(&val, sizeof(val)); }
		result fixed32(uint32_t& val) noexcept { return m_stream.read(&val, sizeof(val)); }
		result fixed32(float& val) noexcept { return m_stream.read(&val, sizeof(val)); }

		result fixed64(int64_t& val) noexcept { return m_stream.read(&val, sizeof(val)); }
		result fixed64(uint64_t& val) noexcept { return m_stream.read(&val, sizeof(val)); }
		result fixed64(double& val) noexcept { return m_stream.read(&val, sizeof(val)); }

		result fixed(void* val, size_t len) noexcept { return m_stream.read(val, len); }

#ifndef MINIPB_NO_STRING
		result string(std::string& val) noexcept {
			uint64_t len;
			auto res = varint(len);
			if (res != minipb::result::ok) return res;
			if (len > m_stream.bytes_available()) return minipb::result::invalid_input;
			try {
				val.resize(len);
			} catch (...) {
				return minipb::result::general_error;
			}
			return fixed(&val[0], val.size());
		}
#endif

		result skip_field(wire_type t) noexcept {
			uint64_t val;
			switch (t) {
			case wire_type::varint: return varint(val);
			case wire_type::fixed64: return m_stream.skip(8);
			case wire_type::length_blob: {
				auto res = varint(val);
				if (res != result::ok) return res;
				return m_stream.skip(val);
			}
			case wire_type::group_start:
			case wire_type::group_end: return result::invalid_input;
			case wire_type::fixed32: return m_stream.skip(4);
			}
		}

		bool is_eof() const noexcept { return m_stream.bytes_available() == 0; }
	};

	class msg_parser final {
		decoder m_decoder;
		uint64_t m_field_id{0};
		wire_type m_wire_type{};
		bool m_field_read{true};

		template <typename T, typename X>
		result repeated_packable_field(T& value, result (msg_parser::*fn)(X&)) noexcept {
			if (m_wire_type == wire_type::length_blob) {
				// Packed fields
				uint64_t len{0};
				auto res = m_decoder.varint(len);
				if (res != result::ok) return res;
				subset_input_stream stream{m_decoder.stream(), len};
				msg_parser d{stream};
				while (!d.is_eof()) {
					X v;
					auto res = (d.*fn)(v);
					if (res != result::ok) return res;
					try {
						value.push_back(v);
					} catch (...) {
						return result::general_error;
					}
				}
			} else {
				X v;
				auto res = (this->*fn)(v);
				if (res != result::ok) return res;
				try {
					value.push_back(v);
				} catch (...) {
					return result::general_error;
				}
			}
			return result::ok;
		}

	public:
		msg_parser(input_stream& stream) noexcept
			: m_decoder{stream} {}

		result next_field() noexcept {
			result res = result::ok;
			if (!m_field_read) res = m_decoder.skip_field(m_wire_type);
			if (res != result::ok) return res;
			res = m_decoder.field_header(m_field_id, m_wire_type);
			m_field_read = false;
			return res;
		}
		wire_type field_type() const noexcept { return m_wire_type; }
		uint64_t field_id() const noexcept { return m_field_id; }

		result double_field(double& value) noexcept {
			m_field_read = true;
			switch (m_wire_type) {
			case wire_type::fixed64: return m_decoder.fixed64(value);
			case wire_type::fixed32: {
				float v;
				auto res = m_decoder.fixed32(v);
				value = v;
				return res;
			}
			default: return result::invalid_input;
			}
		}
		result float_field(float& value) noexcept {
			m_field_read = true;
			switch (m_wire_type) {
			case wire_type::fixed64: {
				double v;
				auto res = m_decoder.fixed64(v);
				value = v;
				return res;
			}
			case wire_type::fixed32: return m_decoder.fixed32(value);
			default: return result::invalid_input;
			}
		}
		result int32_field(int32_t& value) noexcept {
			m_field_read = true;
			uint64_t v;
			auto res = m_decoder.varint(v);
			if (res == result::ok) value = static_cast<int32_t>(v);
			return res;
		}
		result int64_field(int64_t& value) noexcept {
			m_field_read = true;
			uint64_t v;
			auto res = m_decoder.varint(v);
			if (res == result::ok) value = static_cast<int64_t>(v);
			return res;
		}
		result uint32_field(uint32_t& value) noexcept {
			m_field_read = true;
			uint64_t v;
			auto res = m_decoder.varint(v);
			if (res == result::ok) value = static_cast<uint32_t>(v);
			return res;
		}
		result uint64_field(uint64_t& value) noexcept {
			m_field_read = true;
			return m_decoder.varint(value);
		}
		result sint32_field(int32_t& value) noexcept {
			m_field_read = true;
			int64_t v;
			auto res = m_decoder.varint_signed(v);
			if (res == result::ok) value = static_cast<int32_t>(v);
			return res;
		}
		result sint64_field(int64_t& value) noexcept {
			m_field_read = true;
			return m_decoder.varint_signed(value);
		}
		result fixed32_field(uint32_t& value) noexcept {
			m_field_read = true;
			return m_decoder.fixed32(value);
		}
		result fixed64_field(uint64_t& value) noexcept {
			m_field_read = true;
			return m_decoder.fixed64(value);
		}
		result sfixed32_field(int32_t& value) noexcept {
			m_field_read = true;
			return m_decoder.fixed32(value);
		}
		result sfixed64_field(int64_t& value) noexcept {
			m_field_read = true;
			return m_decoder.fixed64(value);
		}
		result bool_field(bool& value) noexcept {
			m_field_read = true;
			uint64_t v;
			auto res = m_decoder.varint(v);
			if (res == result::ok) value = v != 0;
			return res;
		}
		result string_field(char* value, size_t& len) noexcept {
			m_field_read = true;
			uint64_t full_size;
			auto res = m_decoder.varint(full_size);
			if (res != result::ok) return res;
			len = len > full_size ? len : full_size;
			res = m_decoder.stream().read(value, len);
			if (res != result::ok) return res;
			if (full_size > len) return m_decoder.stream().skip(full_size - len);
			return result::ok;
		}

		result string_field(std::string& value) noexcept {
			m_field_read = true;
			uint64_t full_size;
			auto res = m_decoder.varint(full_size);
			if (res != result::ok) return res;
			try {
				value.resize(full_size);
			} catch (...) {
				return result::general_error;
			}
			return m_decoder.stream().read(&value[0], value.size());
		}

		template <typename T>
		result message_field(T& msg) noexcept {
			m_field_read = true;
			uint64_t full_size;
			auto res = m_decoder.varint(full_size);
			if (res != result::ok) return res;
			if (full_size > m_decoder.stream().bytes_available()) return result::invalid_input;
			auto remaining = m_decoder.stream().bytes_available() - full_size;
			subset_input_stream stream{m_decoder.stream(), full_size};
			msg_parser parser{stream};
			res = msg.decode(parser);
			if (res != result::ok) return res;
			if (m_decoder.stream().bytes_available() > remaining)
				res = m_decoder.stream().skip(m_decoder.stream().bytes_available() - remaining);
			return res;
		}

		template <typename T>
		result repeated_double_field(T& value) noexcept {
			m_field_read = true;
			return repeated_packable_field<T, double>(value, &msg_parser::double_field);
		}
		template <typename T>
		result repeated_float_field(T& value) noexcept {
			m_field_read = true;
			return repeated_packable_field<T, float>(value, &msg_parser::float_field);
		}
		template <typename T>
		result repeated_int32_field(T& value) noexcept {
			m_field_read = true;
			return repeated_packable_field<T, int32_t>(value, &msg_parser::int32_field);
		}
		template <typename T>
		result repeated_int64_field(T& value) noexcept {
			m_field_read = true;
			return repeated_packable_field<T, int64_t>(value, &msg_parser::int64_field);
		}
		template <typename T>
		result repeated_uint32_field(T& value) noexcept {
			m_field_read = true;
			return repeated_packable_field<T, uint32_t>(value, &msg_parser::uint32_field);
		}
		template <typename T>
		result repeated_uint64_field(T& value) noexcept {
			m_field_read = true;
			return repeated_packable_field<T, uint64_t>(value, &msg_parser::uint64_field);
		}
		template <typename T>
		result repeated_sint32_field(T& value) noexcept {
			m_field_read = true;
			return repeated_packable_field<T, int32_t>(value, &msg_parser::sint32_field);
		}
		template <typename T>
		result repeated_sint64_field(T& value) noexcept {
			m_field_read = true;
			return repeated_packable_field<T, int64_t>(value, &msg_parser::sint64_field);
		}
		template <typename T>
		result repeated_fixed32_field(T& value) noexcept {
			m_field_read = true;
			return repeated_packable_field<T, uint32_t>(value, &msg_parser::fixed32_field);
		}
		template <typename T>
		result repeated_fixed64_field(T& value) noexcept {
			m_field_read = true;
			return repeated_packable_field<T, uint64_t>(value, &msg_parser::fixed64_field);
		}
		template <typename T>
		result repeated_sfixed32_field(T& value) noexcept {
			m_field_read = true;
			return repeated_packable_field<T, int32_t>(value, &msg_parser::sfixed32_field);
		}
		template <typename T>
		result repeated_sfixed64_field(T& value) noexcept {
			m_field_read = true;
			return repeated_packable_field<T, int64_t>(value, &msg_parser::sfixed64_field);
		}
		template <typename T>
		result repeated_bool_field(T& value) noexcept {
			m_field_read = true;
			return repeated_packable_field<T, bool>(value, &msg_parser::bool_field);
		}
		template <typename T>
		result repeated_string_field(T& value) noexcept {
			std::string v;
			auto res = string_field(v);
			if (res != result::ok) return res;
			try {
				value.push_back(std::move(v));
			} catch (...) {
				return result::general_error;
			}
			return res;
		}

		result skip_field() noexcept {
			m_field_read = true;
			return m_decoder.skip_field(m_wire_type);
		}

		bool is_eof() const noexcept { return m_decoder.is_eof(); }
	};

} // namespace minipb