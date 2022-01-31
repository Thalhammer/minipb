#pragma once
#include <cstddef>
#include <cstring>
#include <string>
#include <type_traits>

namespace minipb {
    /**
     * \brief Error code enum returned by the majority of minipb functions.
     */
	enum class result {
        /// No error
		ok,
        /// An unspecified general error occurred
		general_error,
        /// Not enough space to fit the message. Use estimate_size() to size your buffer.
		out_of_space,
        /// Returned by write_at in case a untouched location is written (should never happen).
		invalid_position,
        /// Not enough memory to fullfil the requested operation
		out_of_memory,
        /// The provided protobuf message is invalid or does not match the message schema.
		invalid_input,
	};

    /**
     * \brief Base class for an output stream.
     */
	class output_stream {
	protected:
		~output_stream() = default;

	public:
        /**
         * \brief Return the current position (usually the number of bytes written so far).
         * 
         * This value is later passed to write_at. The returned value should be related to the bytes written (i.e. calling write with x bytes
         * should result in the returned value increasing by x), but is not required to start at 0.
         * \return The current stream position.
         */
		virtual size_t position() const noexcept = 0;
        /**
         * \brief Write a number of bytes to the stream.
         * \param data The data to write to the stream.
         * \param data_size The size of the data in bytes.
         * \return A result code. Everything other than result::ok will cancel the encoding.
         */
		virtual result write(const void* data, size_t data_size) noexcept = 0;
        /**
         * \brief Overwrite a number of bytes at the specified position.
         * 
         * Due to the way protobuf messages are encoded it is necessary to place the length of certain data fields in the stream.
         * Minipb does this by estimating the space needed for the length field, writing the data and later patching the bytes that
         * correspond to the length field. This is allows saving a lot of memory (since the data does not need to get buffered) if the output
         * stream is seekable. This function will never expand the stream, meaning all the bytes between pos - (pos + data_size) have been
         * touched by write before.
         * 
         * \param pos A position return by position().
         * \param data The data to write to the stream.
         * \param data_size The size of the data in bytes.
         * \return A result code. Everything other than result::ok will cancel the encoding.
         */
		virtual result write_at(size_t pos, const void* data, size_t data_size) noexcept = 0;
	};

    /**
     * \brief Output stream using a user provided memory block for storage
     */
	class array_output_stream final : public output_stream {
		unsigned char* m_start;
		unsigned char* m_end;
		unsigned char* m_current;

	public:
        /**
         * \brief Initialize the stream with the specified data block of array_size bytes.
         * \param array A writable memory location of array_size bytes.
         * \param array_size The size of the memory in bytes.
         */
		array_output_stream(void* array, size_t array_size) noexcept
			: m_start{reinterpret_cast<unsigned char*>(array)}, m_end{m_start + array_size}, m_current{m_start} {}
        /**
         * \brief Construct a stream from a C style array.
         * \note The array type only used for size calculation and is otherwise ignored and the memory gets treated as raw bytes.
         * \param data A C style array
         */
		template <typename T, size_t N>
		array_output_stream(T (&data)[N]) noexcept : array_output_stream(data, sizeof(T) * N) {
			static_assert(std::is_pod<T>::value, "Array needs to only contain pod types");
		}
        /**
         * \brief Get the number of bytes used so far.
         * \return The number of bytes used.
         */
		size_t bytes_used() const noexcept { return m_current - m_start; }
        /**
         * \brief Get the number of bytes still available.
         * \return The number of bytes remaining for writing.
         */
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
        /**
         * \brief Reset the stream by putting the iterator at the start of the array.
         * \note The buffer data is not cleared.
         */
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
        /**
         * \brief Construct a new stream around the container.
         * \param container A C++ container type used as backend storage.
         */
		container_output_stream(T& container) noexcept
			: m_container{container}, m_base_size{container.size()} {}
        /**
         * \brief Get the number of bytes used so far.
         * \return The number of bytes used.
         */
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
		
        /**
         * \brief Reset the stream by putting the iterator at the start of the array.
         * \note The backend container gets resized to the size it had at the time it was passed to the stream constructor.
         */
        void reset() noexcept {
			m_offset = 0;
			m_container.resize(m_base_size);
		}
	};

    /**
     * \brief Base class for an input stream.
     */
	class input_stream {
	protected:
		~input_stream() = default;

	public:
        /**
         * \brief Read a number of bytes from the stream.
         * \param data Buffer to place the read data into.
         * \param data_size Number of bytes that should be read.
         * \return A result code.
         * \note The function is expected to read exactly data_size bytes. If it can't (for example because theres not enough data in the file)
         * it should return result::out_of_space which fails the decode operation.
         */
		virtual result read(void* data, size_t data_size) noexcept = 0;
        /**
         * \brief Skip a number of bytes without reading them.
         * \param data_size The number of bytes to ignore.
         * \return A result code.
         * \note The function is expected to skip exactly data_size bytes. If it can't (for example because theres not enough data in the file)
         * it should return result::out_of_space which fails the decode operation.
         */
		virtual result skip(size_t data_size) noexcept = 0;
        /**
         * \brief Peek a number of bytes without removing them from the buffer.
         * \param data Buffer to place the read data into.
         * \param data_size Number of bytes that should be read.
         * \return The number of bytes read. This can be less then requested. An implementation that does not support peeking can always return 0.
         * \note If the returned size is less than data_size but not 0, the library assumes that eof is reached after the returned number of bytes and might fail
         * if it did not expect eof at that location. If you can not peek data_size bytes return 0 instead, which causes the library to fall back to single byte
         * reads or do a full read and cache the data. The size of data_size is usually very small (<32 bytes).
         */
		virtual size_t peek(void* data, size_t data_size) noexcept { static_cast<void>(data); static_cast<void>(data_size); return 0; }
        /**
         * \brief The remaining number of bytes available.
         * \return The number of bytes still available for reading.
         * \note Protobuf does not contain a code for end of data, which means the size of a message needs to be communicated in some other way.
         */
		virtual size_t bytes_available() const noexcept = 0;
	};

    /**
     * \brief Input stream using a user provided memory block for storage.
     */
	class array_input_stream final : public input_stream {
		const unsigned char* const m_start;
		const unsigned char* const m_end;
		const unsigned char* m_current;

	public:
        /**
         * \brief Initialize the stream with the specified data block of array_size bytes.
         * \param array A buffer array containing an encoded protobuf message.
         * \param array_size Size of the array in bytes.
         */
		array_input_stream(const void* array, size_t array_size) noexcept
			: m_start{reinterpret_cast<const unsigned char*>(array)}, m_end{m_start + array_size}, m_current{m_start} {}
        /**
         * \brief Construct a stream from a C style array.
         * \param data A C style array
         * \note The array type only used for size calculation and is otherwise ignored and the memory gets treated as raw bytes.
         */
		template <typename T, size_t N>
		array_input_stream(T (&data)[N]) noexcept : array_input_stream(data, sizeof(T) * N) {
			static_assert(std::is_pod<T>::value, "Array needs to only contain pod types");
		}
        /**
         * \brief Get the number of bytes used so far.
         * \return The number of bytes used.
         */
		size_t bytes_used() const noexcept { return m_current - m_start; }
        /**
         * \brief Get the number of bytes still available for reading.
         * \return The number of bytes remaining for reading.
         */
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
        /**
         * \brief Reset the stream by putting the iterator at the start of the array.
         */
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
        /**
         * \brief Construct a new input stream around the container.
         * \param container A C++ container type used as backend storage.
         */
		template <typename T, typename element_type = typename std::remove_const<typename std::remove_cv<typename std::remove_reference<decltype(*std::declval<T&>().data())>::type>::type>::type>
		container_input_stream(const T& container) noexcept
			: m_array{const_cast<element_type*>(container.data()), container.size() * sizeof(element_type)} {
			static_assert(std::is_pod<element_type>::value, "Container needs to only contain pod types");
		}
        /**
         * \brief Get the number of bytes used so far.
         * \return The number of bytes used.
         */
		size_t bytes_used() const noexcept { return m_array.bytes_used(); }
		size_t bytes_available() const noexcept override { return m_array.bytes_available(); }
		result read(void* data, size_t data_size) noexcept override { return m_array.read(data, data_size); }
		result skip(size_t data_size) noexcept override { return m_array.skip(data_size); }
		size_t peek(void* data, size_t data_size) noexcept override { return m_array.peek(data, data_size); }
        /**
         * \brief Reset the stream by putting the iterator at the start of the container.
         */
		void reset() noexcept { m_array.reset(); }
	};


	/**
     * \brief Container input stream that wraps a subset of a different input stream.
     */
	class subset_input_stream final : public input_stream {
		input_stream& m_parent;
		size_t m_length;
		size_t m_position{0};

	public:
        /**
         * \brief Construct a new subset stream.
         * \param parent The parent stream used for IO
         * \param len The maximum len available for reading. The actual size available
         * for reading is the less of this and the number of bytes available in the parent stream.
         */
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

    /// The wiretype of a field
	enum class wire_type {
        // Integer stored in variable lenght encoding using 1-10 bytes
		varint = 0,
        // A fixed 64bit field
		fixed64 = 1,
        // A length delimited blob of data (string, bytes or submessage)
		length_blob = 2,
        // Deprecated group start
		group_start = 3,
        // Deprecated group end
		group_end = 4,
        // A fixed 32bit field
		fixed32 = 5,
	};

    /**
     * \brief Encoder class used to encode fields into a protobuf data stream.
     * \note This is a lowlevel class and should only be used if you need full control over
     * the emitted data. For normal operation use msg_builder or the generated message classes.
     */
	class encoder final {
		output_stream& m_stream;

	public:
        /**
         * \brief Construct a new encoder using the specified stream for data output.
         * \param stream Output stream used for the emitted data.
         */
		encoder(output_stream& stream) noexcept
			: m_stream{stream} {}

        /**
         * \brief Get the underlying data stream.
         * \return The data stream.
         */
		output_stream& stream() const noexcept { return m_stream; }

        /**
         * \brief Write an unsigned integer in varint encoding.
         * \param val The value to write.
         * \return result::ok or the error that occurred.
         */
		result varint(uint64_t val) noexcept {
			uint8_t buf[10];
			auto i = varint_build(val, buf);
			return m_stream.write(buf, i);
		}

        /**
         * \brief Write an signed integer in varint encoding (using zig zag encoding).
         * \param val The value to write.
         * \return result::ok or the error that occurred.
         */
		result varint_signed(int64_t val) noexcept {
			if (val < 0)
				return varint(~(static_cast<uint64_t>(val) << 1));
			else
				return varint(static_cast<uint64_t>(val) << 1);
		}

        /**
         * \brief Write a field header to the stream (field id & wire_type).
         * \param field_id The field id
         * \param type The wiretype of the data
         * \return result::ok or the error that occurred.
         */
		result field_header(uint64_t field_id, wire_type type) noexcept {
			return varint(field_id << 3 | static_cast<uint64_t>(type));
		}

        /**
         * \brief Write a int32_t in fixed32 encoding.
         * \param val The value to write.
         * \return result::ok or the error that occurred.
         */
		result fixed32(int32_t val) noexcept { return m_stream.write(&val, sizeof(val)); }

        /**
         * \brief Write a uint32_t in fixed32 encoding.
         * \param val The value to write.
         * \return result::ok or the error that occurred.
         */
		result fixed32(uint32_t val) noexcept { return m_stream.write(&val, sizeof(val)); }

        /**
         * \brief Write a float in fixed32 encoding.
         * \param val The value to write.
         * \return result::ok or the error that occurred.
         */
		result fixed32(float val) noexcept { return m_stream.write(&val, sizeof(val)); }

        /**
         * \brief Write a int64_t in fixed64 encoding.
         * \param val The value to write.
         * \return result::ok or the error that occurred.
         */
		result fixed64(int64_t val) noexcept { return m_stream.write(&val, sizeof(val)); }

        /**
         * \brief Write a uint64_t in fixed64 encoding.
         * \param val The value to write.
         * \return result::ok or the error that occurred.
         */
		result fixed64(uint64_t val) noexcept { return m_stream.write(&val, sizeof(val)); }

        /**
         * \brief Write a double in fixed64 encoding.
         * \param val The value to write.
         * \return result::ok or the error that occurred.
         */
		result fixed64(double val) noexcept { return m_stream.write(&val, sizeof(val)); }

        /**
         * \brief Write a fixed number of bytes to the stream.
         * \param val Pointer to the raw data about to be written.
         * \param len The size of the data to be written in bytes.
         * \return result::ok or the error that occurred.
         */
		result fixed(const void* val, size_t len) noexcept { return m_stream.write(val, len); }

        /**
         * \brief Get the size in bytes required to store a varint with the specified value.
         * \param v The varint to size
         * \return The required space in bytes (1 - 10)
         */
		static size_t varint_size(uint64_t v) noexcept {
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

        /**
         * \brief Serialize the varint into the specified buffer.
         * \param val The varint to serialize
         * \param buf A buffer large enough to store the encoded varint.
         * \return The used space in bytes (1 - 10)
         */
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

    /**
     * \brief Helper class for building a message from individual fields
     */
	class msg_builder final {
		encoder m_encoder;
		result m_error{result::ok};

	public:
        /**
         * \brief Construct a new message builder for the specified output stream.
         * \param stream The output stream to use
         */
		msg_builder(output_stream& stream)
			: m_encoder{stream} {}

        /**
         * \brief Emit a double field to the stream.
         * \param field_id The id of the field.
         * \param value The value of the field.
         * \return A value of result if the operation succeeded.
         */
		result double_field(int64_t field_id, double value) noexcept {
			if (m_error != result::ok) return m_error;
			m_error = m_encoder.field_header(field_id, wire_type::fixed64);
			if (m_error == result::ok) m_error = m_encoder.fixed64(value);
			return m_error;
		}
        /**
         * \brief Emit a float field to the stream.
         * \param field_id The id of the field.
         * \param value The value of the field.
         * \return A value of result if the operation succeeded.
         */
		result float_field(int64_t field_id, float value) noexcept {
			if (m_error != result::ok) return m_error;
			m_error = m_encoder.field_header(field_id, wire_type::fixed32);
			if (m_error == result::ok) m_error = m_encoder.fixed32(value);
			return m_error;
		}
        /**
         * \brief Emit a int32 field to the stream.
         * \param field_id The id of the field.
         * \param value The value of the field.
         * \return A value of result if the operation succeeded.
         */
		result int32_field(int64_t field_id, int32_t value) noexcept {
			if (m_error != result::ok) return m_error;
			m_error = m_encoder.field_header(field_id, wire_type::varint);
			if (m_error == result::ok) m_error = m_encoder.varint(static_cast<uint32_t>(value));
			return m_error;
		}
        /**
         * \brief Emit a int64 field to the stream.
         * \param field_id The id of the field.
         * \param value The value of the field.
         * \return A value of result if the operation succeeded.
         */
		result int64_field(int64_t field_id, int64_t value) noexcept {
			if (m_error != result::ok) return m_error;
			m_error = m_encoder.field_header(field_id, wire_type::varint);
			if (m_error == result::ok) m_error = m_encoder.varint(static_cast<uint64_t>(value));
			return m_error;
		}
        /**
         * \brief Emit a unsigend int32 field to the stream.
         * \param field_id The id of the field.
         * \param value The value of the field.
         * \return A value of result if the operation succeeded.
         */
		result uint32_field(int64_t field_id, uint32_t value) noexcept {
			if (m_error != result::ok) return m_error;
			m_error = m_encoder.field_header(field_id, wire_type::varint);
			if (m_error == result::ok) m_error = m_encoder.varint(value);
			return m_error;
		}
        /**
         * \brief Emit a unsigend int64 field to the stream.
         * \param field_id The id of the field.
         * \param value The value of the field.
         * \return A value of result if the operation succeeded.
         */
		result uint64_field(int64_t field_id, uint64_t value) noexcept {
			if (m_error != result::ok) return m_error;
			m_error = m_encoder.field_header(field_id, wire_type::varint);
			if (m_error == result::ok) m_error = m_encoder.varint(value);
			return m_error;
		}
        /**
         * \brief Emit a signed int32 field to the stream.
         * \param field_id The id of the field.
         * \param value The value of the field.
         * \return A value of result if the operation succeeded.
         */
		result sint32_field(int64_t field_id, int32_t value) noexcept {
			if (m_error != result::ok) return m_error;
			m_error = m_encoder.field_header(field_id, wire_type::varint);
			if (m_error == result::ok) m_error = m_encoder.varint_signed(value);
			return m_error;
		}
        /**
         * \brief Emit a signed int64 field to the stream.
         * \param field_id The id of the field.
         * \param value The value of the field.
         * \return A value of result if the operation succeeded.
         */
		result sint64_field(int64_t field_id, int64_t value) noexcept {
			if (m_error != result::ok) return m_error;
			m_error = m_encoder.field_header(field_id, wire_type::varint);
			if (m_error == result::ok) m_error = m_encoder.varint_signed(value);
			return m_error;
		}
        /**
         * \brief Emit a fixed int32 field to the stream.
         * \param field_id The id of the field.
         * \param value The value of the field.
         * \return A value of result if the operation succeeded.
         */
		result fixed32_field(int64_t field_id, uint32_t value) noexcept {
			if (m_error != result::ok) return m_error;
			m_error = m_encoder.field_header(field_id, wire_type::fixed32);
			if (m_error == result::ok) m_error = m_encoder.fixed32(value);
			return m_error;
		}
        /**
         * \brief Emit a fixed int64 field to the stream.
         * \param field_id The id of the field.
         * \param value The value of the field.
         * \return A value of result if the operation succeeded.
         */
		result fixed64_field(int64_t field_id, uint64_t value) noexcept {
			if (m_error != result::ok) return m_error;
			m_error = m_encoder.field_header(field_id, wire_type::fixed64);
			if (m_error == result::ok) m_error = m_encoder.fixed64(value);
			return m_error;
		}
        /**
         * \brief Emit a signed fixed int32 field to the stream.
         * \param field_id The id of the field.
         * \param value The value of the field.
         * \return A value of result if the operation succeeded.
         */
		result sfixed32_field(int64_t field_id, int32_t value) noexcept {
			if (m_error != result::ok) return m_error;
			m_error = m_encoder.field_header(field_id, wire_type::fixed32);
			if (m_error == result::ok) m_error = m_encoder.fixed32(value);
			return m_error;
		}
        /**
         * \brief Emit a signed fixed int64 field to the stream.
         * \param field_id The id of the field.
         * \param value The value of the field.
         * \return A value of result if the operation succeeded.
         */
		result sfixed64_field(int64_t field_id, int64_t value) noexcept {
			if (m_error != result::ok) return m_error;
			m_error = m_encoder.field_header(field_id, wire_type::fixed64);
			if (m_error == result::ok) m_error = m_encoder.fixed64(value);
			return m_error;
		}
        /**
         * \brief Emit a bool field to the stream.
         * \param field_id The id of the field.
         * \param value The value of the field.
         * \return A value of result if the operation succeeded.
         */
		result bool_field(int64_t field_id, bool value) noexcept {
			if (m_error != result::ok) return m_error;
			m_error = m_encoder.field_header(field_id, wire_type::varint);
			if (m_error == result::ok) m_error = m_encoder.varint(value ? 1 : 0);
			return m_error;
		}
        /**
         * \brief Emit a string/bytes field to the stream.
         * \param field_id The id of the field.
         * \param value The value of the field.
         * \param len The length of value in bytes.
         * \return A value of result if the operation succeeded.
         */
		result string_field(int64_t field_id, const void* value, size_t len) noexcept {
			if (m_error != result::ok) return m_error;
			m_error = m_encoder.field_header(field_id, wire_type::length_blob);
			if (m_error == result::ok) m_error = m_encoder.varint(len);
			if (m_error == result::ok) m_error = m_encoder.fixed(value, len);
			return m_error;
		}
        /**
         * \brief Emit a string/bytes field to the stream.
         * \param field_id The id of the field.
         * \param value The value of the field.
         * \return A value of result if the operation succeeded.
         */
		result string_field(int64_t field_id, const std::string& value) noexcept {
			if (m_error != result::ok) return m_error;
			m_error = string_field(field_id, value.c_str(), value.size());
			return m_error;
		}

        /**
         * \brief Emit a message field to the stream.
         * \param field_id The id of the field.
         * \param msg The message to emit.
         * \tparam T The message type to accept.
         * \return A value of result if the operation succeeded.
         * \note This function is designed to work in conjunction with the code generated by the generator,
         * however it can also be used with custom classes that follow the same interface. At the very minimum it
         * needs an `size_t estimate_size()` function returning an upper bound of the encoded size or 0 if it is unknown
         * or expensive to calculate, as well as an `result encode(msg_builder&)` function that serializes the message into
         * the provided builder.
         */
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

        /**
         * \brief Emit a block of packed 64bit values (double, int64_t or uint64_t).
         * 
         * The function is compatible with any container type that is range iterable
         * (i.e. provides `begin()` and `end()`) and has a `size()` function returning the number
         * of elements in the container. This is true for all stl containers except maps.
         * \param field_id The id of the field.
         * \param value A container type providing the values.
         * \return A value of result if the operation succeeded.
         */
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

        /**
         * \brief Emit a block of packed 32bit values (float, int32_t or uint32_t).
         * 
         * The function is compatible with any container type that is range iterable
         * (i.e. provides `begin()` and `end()`) and has a `size()` function returning the number
         * of elements in the container. This is true for all stl containers except maps.
         * \param field_id The id of the field.
         * \param value A container type providing the values.
         * \return A value of result if the operation succeeded.
         */
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

        /**
         * \brief Emit a block of packed varint values.
         * 
         * The function is compatible with any container type that is range iterable
         * (i.e. provides `begin()` and `end()`) and has a `size()` function returning the number
         * of elements in the container. This is true for all stl containers except maps.
         * \param field_id The id of the field.
         * \param value A container type providing the values.
         * \return A value of result if the operation succeeded.
         */
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

        /**
         * \brief Emit a block of packed varint values using zig zag encoding.
         * 
         * The function is compatible with any container type that is range iterable
         * (i.e. provides `begin()` and `end()`) and has a `size()` function returning the number
         * of elements in the container. This is true for all stl containers except maps.
         * \param field_id The id of the field.
         * \param value A container type providing the values.
         * \return A value of result if the operation succeeded.
         */
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

        /**
         * \brief Return the last error produced
         * \return The last error produced or result::ok if none occurred
         */
		result last_error() const noexcept { return m_error; }
	};

    /**
     * \brief Decoder class used to decode fields from a protobuf data stream.
     * \note This is a lowlevel class and should only be used if you need full control over
     * the read data. For normal operation use msg_parser or the generated message classes.
     */
	class decoder final {
		input_stream& m_stream;

	public:
        /**
         * \brief Construct a new decoder using the specified stream for data input.
         * \param stream Input stream used for reading data.
         */
		decoder(input_stream& stream) noexcept
			: m_stream{stream} {}

        /**
         * \brief Get the underlying stream
         * \return The stream used for reading data
         */
		input_stream& stream() const noexcept { return m_stream; }

        /**
         * \brief Read an unsigned varint value.
         * \param val Variable to store the result into
         * \return Result code
         */
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

        /**
         * \brief Read an signed varint value (using zig zag encoding).
         * \param val Variable to store the result into
         * \return Result code
         */
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

        /**
         * \brief Read header field
         * \param field_id Variable to store the decoded field id into
         * \param type Variable to store the decoded wire type into
         * \return Result code
         */
		result field_header(uint64_t& field_id, wire_type& type) noexcept {
			uint64_t v;
			auto res = varint(v);
			if (res != result::ok) return res;
			field_id = v >> 3;
			type = static_cast<wire_type>(v & 0x7);
			return result::ok;
		}

        /**
         * \brief Read an fixed int32_t.
         * \param val Variable to store the result into
         * \return Result code
         */
		result fixed32(int32_t& val) noexcept { return m_stream.read(&val, sizeof(val)); }

        /**
         * \brief Read an fixed uint32_t.
         * \param val Variable to store the result into
         * \return Result code
         */
		result fixed32(uint32_t& val) noexcept { return m_stream.read(&val, sizeof(val)); }

        /**
         * \brief Read an fixed float.
         * \param val Variable to store the result into
         * \return Result code
         */
		result fixed32(float& val) noexcept { return m_stream.read(&val, sizeof(val)); }

        /**
         * \brief Read an fixed int64_t.
         * \param val Variable to store the result into
         * \return Result code
         */
		result fixed64(int64_t& val) noexcept { return m_stream.read(&val, sizeof(val)); }

        /**
         * \brief Read an fixed uint64_t.
         * \param val Variable to store the result into
         * \return Result code
         */
		result fixed64(uint64_t& val) noexcept { return m_stream.read(&val, sizeof(val)); }

        /**
         * \brief Read an fixed double.
         * \param val Variable to store the result into
         * \return Result code
         */
		result fixed64(double& val) noexcept { return m_stream.read(&val, sizeof(val)); }

        /**
         * \brief Read an fixed block of data.
         * \param val Variable to store the result into
         * \param len Number of bytes to read into val
         * \return Result code
         */
		result fixed(void* val, size_t len) noexcept { return m_stream.read(val, len); }

        /**
         * \brief Read a length delimited string/bytes from the stream.
         * \param val Variable to store the result into
         * \return Result code
         */
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

        /**
         * \brief Skip a field of type t ignoring its content.
         * \param t wire_type of the field
         * \return Result code
         */
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

        /**
         * \brief Check if we reached the end of the stream
         * \return true if we are done
         */
		bool is_eof() const noexcept { return m_stream.bytes_available() == 0; }
	};

    /**
     * \brief Class providing an interface for parsing a encoded protobuf message.
     */
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
        /**
         * \brief Construct a new msg_parser using the specified stream for input.
         * \param stream The input stream
         */
		msg_parser(input_stream& stream) noexcept
			: m_decoder{stream} {}

        /**
         * \brief Advance to the next field
         * \return Result code
         */
		result next_field() noexcept {
			result res = result::ok;
			if (!m_field_read) res = m_decoder.skip_field(m_wire_type);
			if (res != result::ok) return res;
			res = m_decoder.field_header(m_field_id, m_wire_type);
			m_field_read = false;
			return res;
		}
        /**
         * \brief Get the wire_type of the current field
         * \return wire_type of the current field
         */
		wire_type field_type() const noexcept { return m_wire_type; }
        /**
         * \brief Get the id of the current field
         * \return id of the current field
         */
		uint64_t field_id() const noexcept { return m_field_id; }

        /**
         * \brief Get the current field as a double
         * \param value Variable to store the result in
         * \return Result code
         */
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

        /**
         * \brief Get the current field as a float
         * \param value Variable to store the result in
         * \return Result code
         */
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

        /**
         * \brief Get the current field as a int32_t
         * \param value Variable to store the result in
         * \return Result code
         */
		result int32_field(int32_t& value) noexcept {
			m_field_read = true;
			uint64_t v;
			auto res = m_decoder.varint(v);
			if (res == result::ok) value = static_cast<int32_t>(v);
			return res;
		}

        /**
         * \brief Get the current field as a int64_t
         * \param value Variable to store the result in
         * \return Result code
         */
		result int64_field(int64_t& value) noexcept {
			m_field_read = true;
			uint64_t v;
			auto res = m_decoder.varint(v);
			if (res == result::ok) value = static_cast<int64_t>(v);
			return res;
		}

        /**
         * \brief Get the current field as a uint32_t
         * \param value Variable to store the result in
         * \return Result code
         */
		result uint32_field(uint32_t& value) noexcept {
			m_field_read = true;
			uint64_t v;
			auto res = m_decoder.varint(v);
			if (res == result::ok) value = static_cast<uint32_t>(v);
			return res;
		}

        /**
         * \brief Get the current field as a uint64_t
         * \param value Variable to store the result in
         * \return Result code
         */
		result uint64_field(uint64_t& value) noexcept {
			m_field_read = true;
			return m_decoder.varint(value);
		}

        /**
         * \brief Get the current field as a int32_t (zig zag encoding)
         * \param value Variable to store the result in
         * \return Result code
         */
		result sint32_field(int32_t& value) noexcept {
			m_field_read = true;
			int64_t v;
			auto res = m_decoder.varint_signed(v);
			if (res == result::ok) value = static_cast<int32_t>(v);
			return res;
		}

        /**
         * \brief Get the current field as a int64_t (zig zag encoding)
         * \param value Variable to store the result in
         * \return Result code
         */
		result sint64_field(int64_t& value) noexcept {
			m_field_read = true;
			return m_decoder.varint_signed(value);
		}

        /**
         * \brief Get the current field as a uint32_t (fixed32 encoding)
         * \param value Variable to store the result in
         * \return Result code
         */
		result fixed32_field(uint32_t& value) noexcept {
			m_field_read = true;
			return m_decoder.fixed32(value);
		}

        /**
         * \brief Get the current field as a uint64_t (fixed64 encoding)
         * \param value Variable to store the result in
         * \return Result code
         */
		result fixed64_field(uint64_t& value) noexcept {
			m_field_read = true;
			return m_decoder.fixed64(value);
		}

        /**
         * \brief Get the current field as a int32_t (fixed32 encoding)
         * \param value Variable to store the result in
         * \return Result code
         */
		result sfixed32_field(int32_t& value) noexcept {
			m_field_read = true;
			return m_decoder.fixed32(value);
		}

        /**
         * \brief Get the current field as a int64_t (fixed64 encoding)
         * \param value Variable to store the result in
         * \return Result code
         */
		result sfixed64_field(int64_t& value) noexcept {
			m_field_read = true;
			return m_decoder.fixed64(value);
		}

        /**
         * \brief Get the current field as a bool
         * \param value Variable to store the result in
         * \return Result code
         */
		result bool_field(bool& value) noexcept {
			m_field_read = true;
			uint64_t v;
			auto res = m_decoder.varint(v);
			if (res == result::ok) value = v != 0;
			return res;
		}

        /**
         * \brief Get the current field as a string
         * \param value Variable to store the result in
         * \param len The maximum size of bytes writeable. Excess bytes get skiped.
         * Filled with the actual number of bytes read on return.
         * \return Result code
         */
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

        /**
         * \brief Get the current field as a string
         * \param value Variable to store the result in
         * \return Result code
         */
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

        /**
         * \brief Get the current field as a message
         * \param msg The message to parse into.
         * \return Result code
         * \note This function is designed to work in conjunction with the code generated by the generator,
         * however it can also be used with custom classes that follow the same interface. At the very minimum it
         * needs an `result decode(msg_parser&)` function that deserializes the message into the provided parser.
         */
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

        /**
         * \brief Get the current field as a repeated double
         * \param value Variable to store the result in (needs to support push_back)
         * \return Result code
         */
		template <typename T>
		result repeated_double_field(T& value) noexcept {
			m_field_read = true;
			return repeated_packable_field<T, double>(value, &msg_parser::double_field);
		}

        /**
         * \brief Get the current field as a repeated float
         * \param value Variable to store the result in (needs to support push_back)
         * \return Result code
         */
		template <typename T>
		result repeated_float_field(T& value) noexcept {
			m_field_read = true;
			return repeated_packable_field<T, float>(value, &msg_parser::float_field);
		}

        /**
         * \brief Get the current field as a repeated int32
         * \param value Variable to store the result in (needs to support push_back)
         * \return Result code
         */
		template <typename T>
		result repeated_int32_field(T& value) noexcept {
			m_field_read = true;
			return repeated_packable_field<T, int32_t>(value, &msg_parser::int32_field);
		}

        /**
         * \brief Get the current field as a repeated int64
         * \param value Variable to store the result in (needs to support push_back)
         * \return Result code
         */
		template <typename T>
		result repeated_int64_field(T& value) noexcept {
			m_field_read = true;
			return repeated_packable_field<T, int64_t>(value, &msg_parser::int64_field);
		}

        /**
         * \brief Get the current field as a repeated uint32
         * \param value Variable to store the result in (needs to support push_back)
         * \return Result code
         */
		template <typename T>
		result repeated_uint32_field(T& value) noexcept {
			m_field_read = true;
			return repeated_packable_field<T, uint32_t>(value, &msg_parser::uint32_field);
		}

        /**
         * \brief Get the current field as a repeated uint64
         * \param value Variable to store the result in (needs to support push_back)
         * \return Result code
         */
		template <typename T>
		result repeated_uint64_field(T& value) noexcept {
			m_field_read = true;
			return repeated_packable_field<T, uint64_t>(value, &msg_parser::uint64_field);
		}

        /**
         * \brief Get the current field as a repeated signed int32
         * \param value Variable to store the result in (needs to support push_back)
         * \return Result code
         */
		template <typename T>
		result repeated_sint32_field(T& value) noexcept {
			m_field_read = true;
			return repeated_packable_field<T, int32_t>(value, &msg_parser::sint32_field);
		}

        /**
         * \brief Get the current field as a repeated signed int64
         * \param value Variable to store the result in (needs to support push_back)
         * \return Result code
         */
		template <typename T>
		result repeated_sint64_field(T& value) noexcept {
			m_field_read = true;
			return repeated_packable_field<T, int64_t>(value, &msg_parser::sint64_field);
		}

        /**
         * \brief Get the current field as a repeated fixed32
         * \param value Variable to store the result in (needs to support push_back)
         * \return Result code
         */
		template <typename T>
		result repeated_fixed32_field(T& value) noexcept {
			m_field_read = true;
			return repeated_packable_field<T, uint32_t>(value, &msg_parser::fixed32_field);
		}

        /**
         * \brief Get the current field as a repeated fixed64
         * \param value Variable to store the result in (needs to support push_back)
         * \return Result code
         */
		template <typename T>
		result repeated_fixed64_field(T& value) noexcept {
			m_field_read = true;
			return repeated_packable_field<T, uint64_t>(value, &msg_parser::fixed64_field);
		}

        /**
         * \brief Get the current field as a repeated signed fixed32
         * \param value Variable to store the result in (needs to support push_back)
         * \return Result code
         */
		template <typename T>
		result repeated_sfixed32_field(T& value) noexcept {
			m_field_read = true;
			return repeated_packable_field<T, int32_t>(value, &msg_parser::sfixed32_field);
		}

        /**
         * \brief Get the current field as a repeated signed fixed64
         * \param value Variable to store the result in (needs to support push_back)
         * \return Result code
         */
		template <typename T>
		result repeated_sfixed64_field(T& value) noexcept {
			m_field_read = true;
			return repeated_packable_field<T, int64_t>(value, &msg_parser::sfixed64_field);
		}

        /**
         * \brief Get the current field as a repeated bool
         * \param value Variable to store the result in (needs to support push_back)
         * \return Result code
         */
		template <typename T>
		result repeated_bool_field(T& value) noexcept {
			m_field_read = true;
			return repeated_packable_field<T, bool>(value, &msg_parser::bool_field);
		}

        /**
         * \brief Get the current field as a repeated string
         * \param value Variable to store the result in (needs to support push_back)
         * \return Result code
         */
		template <typename T>
		result repeated_string_field(T& value) noexcept {
			m_field_read = true;
			return repeated_packable_field<T, std::string>(value, &msg_parser::string_field);
		}

        /**
         * \brief Skip the current field, ignoring its content
         * \return Result code
         */
		result skip_field() noexcept {
			m_field_read = true;
			return m_decoder.skip_field(m_wire_type);
		}

        /**
         * \brief Check if we reached eof yet
         * \return true if there is no more data to read
         */
		bool is_eof() const noexcept { return m_decoder.is_eof(); }
	};

} // namespace minipb