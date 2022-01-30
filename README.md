# MiniPB implementation
This is minipb, a small header only protobuf writer/reader implemented for C++11 and relying only on the STL.

## Why ?
Because all protobuf libraries I found fall in two categories:
* High performance but bloated and complicated
* Embedded (no dynamic allocations, fixed string widths)

MiniPB tries to hit somewhat of a middleground. It is implemented in a single header with no dependencies except the stl.
The generated code is relatively compact (~4 lines of C++ per protobuf line) and the compiled binary is small enough to be
considered noise in any reasonably sized C++ App. It's not the fastest protobuf library out there, but I have written it with
performance in mind, so I better hope its not the slowest either. It does use dynamic memory where needed (allocating strings
and submessages), but avoids them otherwise. It doesn't implement all of the features protobuf supports (e.g. enums, oneof, ...),
but everything important (scalars, messages, string and bytes as well as repeated and packed variants of them) are supported.
As far as I know it is fully standards compliant for the features it supports.

## Compiling proto files
Minipb comes with a plugin for protoc similar to grpc, which can be used to generate implementation files for a proto source.

## Example
Given the following proto file
```proto
syntax = "proto3";

message my_message {
    string field1 = 1;
    my_message field2 = 2;
    repeated float field3 = 3;
}
```
Minipb will generate the following c++ type in the header file
```cpp
struct my_message {
  size_t estimate_size() const noexcept;
  ::minipb::result encode(::minipb::msg_builder& b) const noexcept;
  ::minipb::result decode(::minipb::msg_parser& p) noexcept;

  std::string field1{};
  std::unique_ptr<my_message> field2{};
  std::vector<float> field3{};
};
```

## Generated code
And an additional C++ Source file with the implementations of those functions.
```cpp
// Header contents is repeated here
size_t my_message::estimate_size() const noexcept {
  size_t size {0};
  size += this->field1.size();
  if(this->field2) size += this->field2->estimate_size() + 10 + 1;
  size += 5 * (this->field3.size());
  size += 11;
  return size;
}

::minipb::result my_message::encode(::minipb::msg_builder& b) const noexcept {
  b.string_field(1, this->field1);
  { if(this->field2) b.message_field(2, *this->field2); }
  b.packed_fixed32_field(3, this->field3);
  return b.last_error();
}

::minipb::result my_message::decode(::minipb::msg_parser& p) noexcept {
  minipb::result res = p.next_field();
  while (res == minipb::result::ok) {
    switch (p.field_id()) {
      case 1: res = p.string_field(this->field1); break;
      case 2: {
        if(!this->field2) this->field2 = std::make_unique<my_message>();
        p.message_field(*this->field2);
      } break;
      case 3: res = p.repeated_float_field(this->field3); break;
      default: res = p.skip_field(); break;
    }
    if (p.is_eof()) break;
    res = p.next_field();
  }
  return res;
}
```
Note that there are no virtual functions or inheritance. Since all needed information is available at compile time there is no need for them.
The function `estimate_size()` returns a worst case estimate for the serialized size of the message with its current contents and is used for
all messages to support serialization. If you consider implementing it yourself, you can simply return 0 and it will still work (but might increase
the serialized size). The function can (and usually will) return more than is actually needed for the message, but the message is guaranteed to fit
in the returned space. You can therefore use it to (stack-) allocate a buffer to hold the serialized message.

The function `encode()` can be used to - surprise - encode the message into its serialized form. The only reason it can fail is if theres not enough
memory left in the buffer provided or the backing container failed to reallocate (out of memory). Assuming you provided at least `estimate_size()` bytes,
this function is not supposed to fail.

The last function `decode()` is used to decode a serialized message and fill the struct with its information. Assuming the provided buffer contains a
complete and valid protobuf message this should not fail, however it might in case of a schema missmatch or otherwise bad input data. In case an error
is returned, the function might leave the message struct in an partially filled state.

## Extending
By default the libary can use both preallocated raw arrays, as well as selected stl containers for both input and output.
However you can add a custom implementation in order to support whatever datatype/device you need.
```cpp
class input_stream {
protected:
	~input_stream() = default;
public:
	virtual result read(void* data, size_t data_size) noexcept = 0;
	virtual result skip(size_t data_size) noexcept = 0;
	virtual size_t peek(void*, size_t) noexcept { return 0; }
	virtual size_t bytes_available() const noexcept = 0;
};
```
`read()` is used to read the given amount of data into the buffer. If the requested amount exceeds the available data an error should be returned.
Minipb will then bubble up the error all the way to user code. `skip()` is similar to read but ignores the data. `peek` can be implemented in case
the data provider supports it and looking ahead without actually reading the data is cheap (e.g. in a contigous array). Peek data will not be removed
from the stream and needs to get returned at a subsequent `read()` or skipped. Peeks do not accumulate, meaning that two calls to `peek()` without
an intermediate `read()` or `skip()` should return the same data. If `peek()` is not supported by the backend, you can return 0, otherwise return the
size of the read data. If peek returns less than data_size, but not 0 the library assumes eof after the returned size. In case 0 is returned minipb
will fall back to doing (potentially lots of) single byte reads. A pattern that can be seen often is the library doing a peek for 10 bytes (max size of a varint)
followed by a skip of the actual varint size. This avoids having to do up to 10 single byte reads. `bytes_available()` should return the remaining number
of bytes left in the serialized message. Because protobuf has no indication of record end, minipb will try to parse data until bytes_available() is 0.

```cpp
class output_stream {
protected:
	~output_stream() = default;
public:
	virtual size_t position() const noexcept = 0;
	virtual result write(const void* data, size_t data_size) noexcept = 0;
	virtual result write_at(size_t pos, const void* data, size_t data_size) noexcept = 0;
};
```
The output stream is used when serializing a message to a byte stream. It consists of 3 functions, all of which need to get implemented. The `position()` function
should return the total number of bytes written using this stream. It is used to calculate sizes and as a marker for calling `write_at()`. The function `write()`
is called whenever the library wants to emit some data. The function should write all the provided data or return an error if it can't. `write_at()` is used to
overwrite a previously written block of data. This is needed since the exact size of a submessage is not known until after it has been serialized in order to
patch the length field in the header. Protobuf supports an alternate method of delimiting messages called "groups" which would allow us to skip the patching,
however they have already been deprecated when protobuf was publically released and as a result many implementations don't support them. I might provide a option
to use them instead of length delimited messages in the future, allowing for true buffer less forward only serialization at the cost of compatibility.