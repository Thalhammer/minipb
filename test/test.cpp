#include <gtest/gtest.h>
#include <minipb/minipb.h>
#include <sample.proto.h>

TEST(MinipbTest, ArrayOutputStream) {
	char buf[16];
	memset(buf, 0xff, sizeof(buf));
	minipb::array_output_stream stream{buf};
	ASSERT_EQ(stream.bytes_used(), 0);
	ASSERT_EQ(stream.bytes_available(), 16);
	ASSERT_EQ(stream.position(), 0);

	ASSERT_EQ(stream.write("\0", 1), minipb::result::ok);
	ASSERT_EQ(stream.bytes_used(), 1);
	ASSERT_EQ(stream.bytes_available(), 15);
	ASSERT_EQ(stream.position(), 1);
	ASSERT_EQ(buf[0], '\0');

	ASSERT_EQ(stream.write_at(0, "\x01", 1), minipb::result::ok);
	ASSERT_EQ(stream.bytes_used(), 1);
	ASSERT_EQ(stream.bytes_available(), 15);
	ASSERT_EQ(stream.position(), 1);
	ASSERT_EQ(buf[0], '\x01');

	ASSERT_EQ(stream.write_at(1, "\x02", 1), minipb::result::invalid_position);
	ASSERT_EQ(stream.bytes_used(), 1);
	ASSERT_EQ(stream.bytes_available(), 15);
	ASSERT_EQ(stream.position(), 1);
	ASSERT_EQ(buf[0], '\x01');
	ASSERT_EQ(buf[1], '\xff');

	ASSERT_EQ(stream.write("\x02", 1), minipb::result::ok);
	ASSERT_EQ(stream.bytes_used(), 2);
	ASSERT_EQ(stream.bytes_available(), 14);
	ASSERT_EQ(stream.position(), 2);
	ASSERT_EQ(buf[0], '\x01');
	ASSERT_EQ(buf[1], '\x02');

	ASSERT_EQ(stream.write_at(1, "\x03", 1), minipb::result::ok);
	ASSERT_EQ(stream.bytes_used(), 2);
	ASSERT_EQ(stream.bytes_available(), 14);
	ASSERT_EQ(stream.position(), 2);
	ASSERT_EQ(buf[0], '\x01');
	ASSERT_EQ(buf[1], '\x03');

	ASSERT_EQ(stream.write_at(0, "\x03", 1), minipb::result::ok);
	ASSERT_EQ(stream.bytes_used(), 2);
	ASSERT_EQ(stream.bytes_available(), 14);
	ASSERT_EQ(stream.position(), 2);
	ASSERT_EQ(buf[0], '\x03');
	ASSERT_EQ(buf[1], '\x03');

	ASSERT_EQ(stream.write("0123456789ABCDEF", 16), minipb::result::out_of_space);
	ASSERT_EQ(stream.bytes_used(), 2);
	ASSERT_EQ(stream.bytes_available(), 14);
	ASSERT_EQ(stream.position(), 2);
	ASSERT_EQ(buf[0], '\x03');
	ASSERT_EQ(buf[1], '\x03');

	ASSERT_EQ(stream.write("0123456789ABCD", 14), minipb::result::ok);
	ASSERT_EQ(stream.bytes_used(), 16);
	ASSERT_EQ(stream.bytes_available(), 0);
	ASSERT_EQ(stream.position(), 16);
	ASSERT_EQ(buf[0], '\x03');
	ASSERT_EQ(buf[1], '\x03');

	ASSERT_EQ(stream.write("\x03", 1), minipb::result::out_of_space);
	ASSERT_EQ(stream.bytes_used(), 16);
	ASSERT_EQ(stream.bytes_available(), 0);
	ASSERT_EQ(stream.position(), 16);
	ASSERT_EQ(buf[0], '\x03');
	ASSERT_EQ(buf[1], '\x03');
}

template <typename T>
void container_output_test() {
	using element_type = typename std::remove_const<typename std::remove_cv<typename std::remove_reference<decltype(*std::declval<T&>().data())>::type>::type>::type;
	T buf{};
	minipb::container_output_stream<T> stream{buf};
	ASSERT_EQ(stream.bytes_used(), 0);
	ASSERT_EQ(stream.position(), 0);

	ASSERT_EQ(stream.write("\0", 1), minipb::result::ok);
	ASSERT_EQ(stream.bytes_used(), 1);
	ASSERT_EQ(stream.position(), 1);
	ASSERT_EQ(memcmp(buf.data(), "\0", 1), 0);
	ASSERT_GE(buf.size() * sizeof(element_type), 1);

	ASSERT_EQ(stream.write_at(0, "\x01", 1), minipb::result::ok);
	ASSERT_EQ(stream.bytes_used(), 1);
	ASSERT_EQ(stream.position(), 1);
	ASSERT_EQ(memcmp(buf.data(), "\x01", 1), 0);
	ASSERT_GE(buf.size() * sizeof(element_type), 1);

	ASSERT_EQ(stream.write_at(1, "\x02", 1), minipb::result::invalid_position);
	ASSERT_EQ(stream.bytes_used(), 1);
	ASSERT_EQ(stream.position(), 1);
	ASSERT_EQ(memcmp(buf.data(), "\x01", 1), 0);
	ASSERT_GE(buf.size() * sizeof(element_type), 1);

	ASSERT_EQ(stream.write("\x02", 1), minipb::result::ok);
	ASSERT_EQ(stream.bytes_used(), 2);
	ASSERT_EQ(stream.position(), 2);
	ASSERT_EQ(memcmp(buf.data(), "\x01\x02", 2), 0);
	ASSERT_GE(buf.size() * sizeof(element_type), 2);

	ASSERT_EQ(stream.write_at(1, "\x03", 1), minipb::result::ok);
	ASSERT_EQ(stream.bytes_used(), 2);
	ASSERT_EQ(stream.position(), 2);
	ASSERT_EQ(memcmp(buf.data(), "\x01\x03", 2), 0);
	ASSERT_GE(buf.size() * sizeof(element_type), 2);

	ASSERT_EQ(stream.write_at(0, "\x03", 1), minipb::result::ok);
	ASSERT_EQ(stream.bytes_used(), 2);
	ASSERT_EQ(stream.position(), 2);
	ASSERT_EQ(memcmp(buf.data(), "\x03\x03", 2), 0);
	ASSERT_GE(buf.size() * sizeof(element_type), 2);

	ASSERT_EQ(stream.write("0123456789ABCD", 14), minipb::result::ok);
	ASSERT_EQ(stream.bytes_used(), 16);
	ASSERT_EQ(stream.position(), 16);
	ASSERT_EQ(memcmp(buf.data(), "\x03\x03\0\1\2\3\4\5\6\7\x08\x09\x0A\x0B\x0C\x0D", 2), 0);
	ASSERT_GE(buf.size() * sizeof(element_type), 16);

	// Check for out of memory
	ASSERT_EQ(stream.write("0123456789ABCD", SIZE_MAX / 2), minipb::result::general_error);
}

TEST(MinipbTest, ContainerOutputStream) {
	container_output_test<std::string>();
	container_output_test<std::wstring>();
	container_output_test<std::vector<uint8_t>>();
	container_output_test<std::vector<char>>();
	container_output_test<std::vector<uint16_t>>();
	container_output_test<std::vector<int16_t>>();
	container_output_test<std::vector<uint32_t>>();
	container_output_test<std::vector<int32_t>>();
}

template <typename T>
void container_input_test() {
	T buf{};
	minipb::container_output_stream<T> stream{buf};
    // TODO: We should test if everything works
}

TEST(MinipbTest, ContainerInputStream) {
	container_input_test<std::string>();
	container_input_test<std::wstring>();
	container_input_test<std::vector<uint8_t>>();
	container_input_test<std::vector<char>>();
	container_input_test<std::vector<uint16_t>>();
	container_input_test<std::vector<int16_t>>();
	container_input_test<std::vector<uint32_t>>();
	container_input_test<std::vector<int32_t>>();
}

TEST(MinipbTest, Encoder) {
	std::string buf;
	minipb::container_output_stream<std::string> stream{buf};
	minipb::encoder enc{stream};

	ASSERT_EQ(enc.varint(1), minipb::result::ok);
	ASSERT_EQ(memcmp(buf.data(), "\x01", 1), 0);
	ASSERT_EQ(buf.size(), 1);
	stream.reset();
	ASSERT_EQ(enc.varint(127), minipb::result::ok);
	ASSERT_EQ(memcmp(buf.data(), "\x7f", 1), 0);
	ASSERT_EQ(buf.size(), 1);
	stream.reset();
	ASSERT_EQ(enc.varint(128), minipb::result::ok);
	ASSERT_EQ(memcmp(buf.data(), "\x80\x01", 2), 0);
	ASSERT_EQ(buf.size(), 2);
	stream.reset();
	ASSERT_EQ(enc.varint(150), minipb::result::ok);
	ASSERT_EQ(memcmp(buf.data(), "\x96\x01", 2), 0);
	ASSERT_EQ(buf.size(), 2);
	stream.reset();
	ASSERT_EQ(enc.varint(300), minipb::result::ok);
	ASSERT_EQ(memcmp(buf.data(), "\xac\x02", 2), 0);
	ASSERT_EQ(buf.size(), 2);
	stream.reset();
	ASSERT_EQ(enc.varint(0x1000), minipb::result::ok);
	ASSERT_EQ(memcmp(buf.data(), "\x80\x20", 2), 0);
	ASSERT_EQ(buf.size(), 2);
	stream.reset();
	ASSERT_EQ(enc.varint(0x10000), minipb::result::ok);
	ASSERT_EQ(memcmp(buf.data(), "\x80\x80\x04", 3), 0);
	ASSERT_EQ(buf.size(), 3);
	stream.reset();
}

TEST(MinipbTest, Builder) {
	std::string buf;
	{
	    minipb::container_output_stream<std::string> stream{buf};
	    minipb::msg_builder b{stream};
		test::message_b msg{};
		msg.field1 = "Hello world";
		msg.field2 = std::make_unique<test::message_a>();
		msg.field2->field1.push_back(12345);
		msg.field2->field2 = 6789;
		msg.field3 = 1.0f;
		ASSERT_EQ(msg.encode(b), minipb::result::ok);
	}
    {
        minipb::container_input_stream stream{buf};
        minipb::msg_parser p{stream};
        test::message_b msg{};
        ASSERT_EQ(msg.decode(p), minipb::result::ok);
        ASSERT_EQ(msg.field1, "Hello world");
        ASSERT_TRUE(msg.field2);
        ASSERT_EQ(msg.field2->field1.size(), 1);
        ASSERT_EQ(msg.field2->field1[0], 12345);
        ASSERT_EQ(msg.field2->field2, 6789);
        ASSERT_FLOAT_EQ(msg.field3, 1.0f);
    }
}

TEST(MinipbTest, Parser) {
	uint8_t buf[] = {0x0a, 0x0b, 0x48, 0x65, 0x6c, 0x6c, 0x6f, 0x20, 0x77, 0x6f, 0x72, 0x6c, 0x64, 0x12, 0x06, 0x08, 0xb9, 0x60, 0x10, 0x85, 0x35, 0x1d, 0x00, 0x00, 0x80, 0x3f};
	minipb::array_input_stream stream{buf};
	minipb::msg_parser p{stream};

	test::message_b msg{};
	ASSERT_EQ(msg.decode(p), minipb::result::ok);
	ASSERT_EQ(msg.field1, "Hello world");
	ASSERT_TRUE(msg.field2);
	ASSERT_EQ(msg.field2->field1.size(), 1);
	ASSERT_EQ(msg.field2->field1[0], 12345);
	ASSERT_EQ(msg.field2->field2, 6789);
	ASSERT_FLOAT_EQ(msg.field3, 1.0f);
}