#include <algorithm>
#include <iostream>
#include <sstream>

#include <google/protobuf/compiler/code_generator.h>
#include <google/protobuf/compiler/plugin.h>
#include <google/protobuf/compiler/plugin.pb.h>
#include <google/protobuf/descriptor.pb.h>
#include <google/protobuf/io/printer.h>
#include <google/protobuf/io/zero_copy_stream.h>
#include <google/protobuf/stubs/stl_util.h>

using namespace google::protobuf;
using namespace std::literals;

class DummyCodeGenerator : public compiler::CodeGenerator {
public:
	DummyCodeGenerator();
	virtual ~DummyCodeGenerator();
	virtual bool Generate(
		const FileDescriptor* file,
		const std::string& parameter,
		compiler::GeneratorContext* context,
		std::string* error) const;

	bool GenerateHeader(const FileDescriptor* file, compiler::GeneratorContext* context, std::string* error) const;
	bool GenerateImpl(const FileDescriptor* file, compiler::GeneratorContext* context, std::string* error) const;

	void EmitStructure(const std::map<std::string, std::string>& global_args, const Descriptor* d, io::Printer& printer) const;
	void EmitEstimateSize(const std::map<std::string, std::string>& message_args, const Descriptor* m, io::Printer& printer) const;
	void EmitEncode(const std::map<std::string, std::string>& message_args, const Descriptor* m, io::Printer& printer) const;
	void EmitDecode(const std::map<std::string, std::string>& message_args, const Descriptor* m, io::Printer& printer) const;
};

DummyCodeGenerator::DummyCodeGenerator() {}

DummyCodeGenerator::~DummyCodeGenerator() {}

static size_t varint_size(size_t v) {
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

static size_t header_size(size_t field_id) {
	return varint_size(field_id << 3);
}

static std::map<std::string, std::string> combine(std::map<std::string, std::string> a, std::initializer_list<std::pair<std::string, std::string>> b) {
	for (auto& e : b)
		a.emplace(e);
	return a;
}

bool DummyCodeGenerator::Generate(const FileDescriptor* file, const std::string&, compiler::GeneratorContext* context, std::string* error) const {
	if (!GenerateHeader(file, context, error)) return false;
	if (!GenerateImpl(file, context, error)) return false;
	return true;
}

void DummyCodeGenerator::EmitStructure(const std::map<std::string, std::string>& global_args, const Descriptor* m, io::Printer& printer) const {
	// clang-format off
    auto message_args = combine(global_args,
    {
    	{"MSG_NAME", m->name()},
    	{"MSG_NAME_FULL", m->full_name()},
    });
	// clang-format on

	printer.Print(message_args, "struct $MSG_NAME$ {\n");
	printer.Indent();
	printer.Print(message_args, R"(size_t estimate_size() const noexcept;
::minipb::result encode(::minipb::msg_builder& b) const noexcept;
::minipb::result decode(::minipb::msg_parser& p) noexcept;

)");

	for (int f = 0; f < m->field_count(); f++) {
		auto fd = m->field(f);
		std::string cpp_typename = "";
		switch (fd->cpp_type()) {
		case FieldDescriptor::CPPTYPE_INT32: cpp_typename = "int32_t"; break;
		case FieldDescriptor::CPPTYPE_INT64: cpp_typename = "int64_t"; break;
		case FieldDescriptor::CPPTYPE_UINT32: cpp_typename = "uint32_t"; break;
		case FieldDescriptor::CPPTYPE_UINT64: cpp_typename = "uint64_t"; break;
		case FieldDescriptor::CPPTYPE_DOUBLE: cpp_typename = "double"; break;
		case FieldDescriptor::CPPTYPE_FLOAT: cpp_typename = "float"; break;
		case FieldDescriptor::CPPTYPE_BOOL: cpp_typename = "bool"; break;
		case FieldDescriptor::CPPTYPE_ENUM: throw std::logic_error("Not implemented"); break;
		case FieldDescriptor::CPPTYPE_STRING: cpp_typename = "std::string"; break;
		case FieldDescriptor::CPPTYPE_MESSAGE: cpp_typename = "std::unique_ptr<" + fd->message_type()->name() + ">"; break;
		}
		// clang-format off
        auto field_args = combine(message_args, {
            {"TYPENAME", fd->type_name()},
            {"CPP_TYPE", cpp_typename},
            {"NAME", fd->name()},
            {"CAMELCASE_NAME", fd->camelcase_name()},
        });
		// clang-format on
		if (fd->is_repeated())
			printer.Print(field_args, "std::vector<$CPP_TYPE$> $NAME${};\n");
		else
			printer.Print(field_args, "$CPP_TYPE$ $NAME${};\n");
	}
	printer.Outdent();
	printer.Print(message_args, "};\n\n");
}

void DummyCodeGenerator::EmitEstimateSize(const std::map<std::string, std::string>& message_args, const Descriptor* m, io::Printer& printer) const {
	printer.Print(message_args, "size_t $MSG_NAME$::estimate_size() const noexcept {\n");
	printer.Indent();
	printer.Print("size_t size {0};\n");
	std::map<size_t, std::string> multi;
	size_t fixed{0};
	for (int f = 0; f < m->field_count(); f++) {
		auto fd = m->field(f);
		auto hsize = header_size(fd->number());
		// clang-format off
        auto field_args = combine(message_args,
        {
            {"FIELD_NAME", fd->name()},
            {"HSIZE", std::to_string(hsize)},
        });
		// clang-format on
		if (fd->is_repeated()) {
			switch (fd->type()) {
			// Fixed 8 bytes
			case FieldDescriptor::TYPE_DOUBLE:
			case FieldDescriptor::TYPE_FIXED64:
			case FieldDescriptor::TYPE_SFIXED64:
				multi[8 + hsize] += " + this->" + fd->name() + ".size()";
				break;
			// Fixed 4 bytes
			case FieldDescriptor::TYPE_FLOAT:
			case FieldDescriptor::TYPE_FIXED32:
			case FieldDescriptor::TYPE_SFIXED32:
				multi[4 + hsize] += " + this->" + fd->name() + ".size()";
				break;
			// Varints
			case FieldDescriptor::TYPE_INT64:
			case FieldDescriptor::TYPE_UINT64:
			case FieldDescriptor::TYPE_INT32:
			case FieldDescriptor::TYPE_UINT32:
			case FieldDescriptor::TYPE_BOOL:
			case FieldDescriptor::TYPE_ENUM:
			case FieldDescriptor::TYPE_SINT32:
			case FieldDescriptor::TYPE_SINT64:
				multi[10 + hsize] += " + this->" + fd->name() + ".size()";
				break;
			// Length delim
			case FieldDescriptor::TYPE_STRING:
			case FieldDescriptor::TYPE_BYTES:
				multi[10 + hsize] += " + this->" + fd->name() + ".size()";
				printer.Print(field_args, "for(auto& e : this->$FIELD_NAME$) size += e.size();\n");
				break;
			// Sub message
			case FieldDescriptor::TYPE_MESSAGE:
				printer.Print(field_args, "for(auto& e : this->$FIELD_NAME$) { if(e) size += e->estimate_size() + 10 + $HSIZE$; }\n");
				break;
			// Unsupported
			case FieldDescriptor::TYPE_GROUP:
				throw std::logic_error("unsupported");
			}
		} else {
			switch (fd->type()) {
			// Fixed 8 bytes
			case FieldDescriptor::TYPE_DOUBLE:
			case FieldDescriptor::TYPE_FIXED64:
			case FieldDescriptor::TYPE_SFIXED64:
				fixed += 8 + hsize;
				break;
			// Fixed 4 bytes
			case FieldDescriptor::TYPE_FLOAT:
			case FieldDescriptor::TYPE_FIXED32:
			case FieldDescriptor::TYPE_SFIXED32:
				fixed += 4 + hsize;
				break;
			// Varints
			case FieldDescriptor::TYPE_INT64:
			case FieldDescriptor::TYPE_UINT64:
			case FieldDescriptor::TYPE_INT32:
			case FieldDescriptor::TYPE_UINT32:
			case FieldDescriptor::TYPE_BOOL:
			case FieldDescriptor::TYPE_ENUM:
			case FieldDescriptor::TYPE_SINT32:
			case FieldDescriptor::TYPE_SINT64:
				fixed += 10 + hsize;
				break;
			// Length delim
			case FieldDescriptor::TYPE_STRING:
			case FieldDescriptor::TYPE_BYTES:
				fixed += 10 + hsize;
				printer.Print(field_args, "size += this->$FIELD_NAME$.size();\n");
				break;
			// Sub message
			case FieldDescriptor::TYPE_MESSAGE:
				printer.Print(field_args, "if(this->$FIELD_NAME$) size += this->$FIELD_NAME$->estimate_size() + 10 + $HSIZE$;\n");
				break;
			// Unsupported
			case FieldDescriptor::TYPE_GROUP:
				throw std::logic_error("unsupported");
			}
		}
	}
	for (auto& e : multi)
		printer.Print(("size += " + std::to_string(e.first) + " * (" + e.second.substr(3) + ");\n").c_str());
	printer.Print(("size += " + std::to_string(fixed) + ";\n").c_str());
	printer.Print("return size;\n");
	printer.Outdent();
	printer.Print("}\n\n");
}

void DummyCodeGenerator::EmitEncode(const std::map<std::string, std::string>& message_args, const Descriptor* m, io::Printer& printer) const {
	printer.Print(message_args, "::minipb::result $MSG_NAME$::encode(::minipb::msg_builder& b) const noexcept {\n");
	printer.Indent();
	for (int f = 0; f < m->field_count(); f++) {
		auto fd = m->field(f);
		auto hsize = header_size(fd->number());
		// clang-format off
        auto field_args = combine(message_args,
        {
            {"FIELD_NAME", "this->" + fd->name()},
            {"HSIZE", std::to_string(hsize)},
            {"FIELD_NUM", std::to_string(fd->number())},
            {"TYPE", fd->type_name()},
        });
		// clang-format on
        switch (fd->type()) {
		case FieldDescriptor::TYPE_DOUBLE:
		case FieldDescriptor::TYPE_FIXED64:
		case FieldDescriptor::TYPE_SFIXED64:
            if(fd->is_packed()) {
                printer.Print(field_args, "b.packed_fixed64_field($FIELD_NUM$, $FIELD_NAME$);\n");
                continue;
            }
            break;
		case FieldDescriptor::TYPE_FLOAT:
		case FieldDescriptor::TYPE_FIXED32:
		case FieldDescriptor::TYPE_SFIXED32:
            if(fd->is_packed()) {
                printer.Print(field_args, "b.packed_fixed32_field($FIELD_NUM$, $FIELD_NAME$);\n");
                continue;
            }
            break;
		case FieldDescriptor::TYPE_INT64:
		case FieldDescriptor::TYPE_UINT64:
		case FieldDescriptor::TYPE_INT32:
		case FieldDescriptor::TYPE_UINT32:
		case FieldDescriptor::TYPE_BOOL:
            if(fd->is_packed()) {
                printer.Print(field_args, "b.packed_varint_field($FIELD_NUM$, $FIELD_NAME$);\n");
                continue;
            }
            break;
		case FieldDescriptor::TYPE_SINT32:
		case FieldDescriptor::TYPE_SINT64:
            if(fd->is_packed()) {
                printer.Print(field_args, "b.packed_varint_signed_field($FIELD_NUM$, $FIELD_NAME$);\n");
                continue;
            }
            break;
        default: break;
        }

		if (fd->is_repeated()) {
			switch (fd->type()) {
			case FieldDescriptor::TYPE_MESSAGE:
			case FieldDescriptor::TYPE_STRING:
			case FieldDescriptor::TYPE_BYTES:
				printer.Print(field_args, "for(auto& e : $FIELD_NAME$) ");
				break;
			default: printer.Print(field_args, "for(auto e : $FIELD_NAME$) "); break;
			}
			field_args["FIELD_NAME"] = "e";
		}

		switch (fd->type()) {
		case FieldDescriptor::TYPE_DOUBLE:
		case FieldDescriptor::TYPE_FIXED64:
		case FieldDescriptor::TYPE_SFIXED64:
		case FieldDescriptor::TYPE_FLOAT:
		case FieldDescriptor::TYPE_FIXED32:
		case FieldDescriptor::TYPE_SFIXED32:
		case FieldDescriptor::TYPE_INT64:
		case FieldDescriptor::TYPE_UINT64:
		case FieldDescriptor::TYPE_INT32:
		case FieldDescriptor::TYPE_UINT32:
		case FieldDescriptor::TYPE_BOOL:
		case FieldDescriptor::TYPE_ENUM:
		case FieldDescriptor::TYPE_SINT32:
		case FieldDescriptor::TYPE_SINT64:
		case FieldDescriptor::TYPE_STRING:
			printer.Print(field_args, "b.$TYPE$_field($FIELD_NUM$, $FIELD_NAME$);\n");
			break;
		case FieldDescriptor::TYPE_MESSAGE:
			printer.Print(field_args, "{ if($FIELD_NAME$) b.$TYPE$_field($FIELD_NUM$, *$FIELD_NAME$); }\n");
			break;
		case FieldDescriptor::TYPE_BYTES:
			printer.Print(field_args, "b.string_field($FIELD_NUM$, $FIELD_NAME$);\n");
			break;
		// Unsupported
		case FieldDescriptor::TYPE_GROUP:
			throw std::logic_error("unsupported");
		}
	}
	printer.Print("return b.last_error();\n");
	printer.Outdent();
	printer.Print("}\n\n");
}

void DummyCodeGenerator::EmitDecode(const std::map<std::string, std::string>& message_args, const Descriptor* m, io::Printer& printer) const {
    printer.Print(message_args, "::minipb::result $MSG_NAME$::decode(::minipb::msg_parser& p) noexcept {\n");
	printer.Indent();
    printer.Print("minipb::result res = p.next_field();\nwhile (res == minipb::result::ok) {\n");
    printer.Indent();
    printer.Print("switch (p.field_id()) {\n");
    printer.Indent();
	for (int f = 0; f < m->field_count(); f++) {
		auto fd = m->field(f);
		auto hsize = header_size(fd->number());
		// clang-format off
        auto field_args = combine(message_args,
        {
            {"FIELD_NAME", "this->" + fd->name()},
            {"HSIZE", std::to_string(hsize)},
            {"FIELD_NUM", std::to_string(fd->number())},
            {"TYPE", fd->type_name()},
        });
		// clang-format on
        if(fd->type() == FieldDescriptor::TYPE_BYTES) field_args["TYPE"] = "string";
        if(fd->is_repeated()) field_args["TYPE"] = "repeated_" + field_args["TYPE"];
		switch (fd->type()) {
		case FieldDescriptor::TYPE_DOUBLE:
		case FieldDescriptor::TYPE_FIXED64:
		case FieldDescriptor::TYPE_SFIXED64:
		case FieldDescriptor::TYPE_FLOAT:
		case FieldDescriptor::TYPE_FIXED32:
		case FieldDescriptor::TYPE_SFIXED32:
		case FieldDescriptor::TYPE_INT64:
		case FieldDescriptor::TYPE_UINT64:
		case FieldDescriptor::TYPE_INT32:
		case FieldDescriptor::TYPE_UINT32:
		case FieldDescriptor::TYPE_BOOL:
		case FieldDescriptor::TYPE_ENUM:
		case FieldDescriptor::TYPE_SINT32:
		case FieldDescriptor::TYPE_SINT64:
		case FieldDescriptor::TYPE_STRING:
		case FieldDescriptor::TYPE_BYTES:
            printer.Print(field_args, "case $FIELD_NUM$: res = p.$TYPE$_field($FIELD_NAME$); break;\n");
			break;
		case FieldDescriptor::TYPE_MESSAGE: {
			printer.Print(field_args, "case $FIELD_NUM$: {\n");
            printer.Indent();
            auto name = JoinStrings(Split(fd->message_type()->full_name(), "."), "::");
            if(fd->is_repeated()) {
                printer.Print(field_args, ("auto e = std::make_unique<" + name + ">();\n").c_str());
                printer.Print("p.message_field(*e);\n");
                printer.Print(field_args, "$FIELD_NAME$.push_back(std::move(e));\n");
            } else {
                printer.Print(field_args, ("if(!$FIELD_NAME$) $FIELD_NAME$ = std::make_unique<" + name + ">();\n").c_str());
                printer.Print(field_args, "p.$TYPE$_field(*$FIELD_NAME$);\n");
            }
            printer.Outdent();
            printer.Print("} break;\n");
        } break;
		// Unsupported
		case FieldDescriptor::TYPE_GROUP:
			throw std::logic_error("unsupported");
		}
	}
    printer.Print("default: res = p.skip_field(); break;\n");
    printer.Outdent();
	printer.Print("}\nif (p.is_eof()) break;\nres = p.next_field();\n");
    printer.Outdent();
    printer.Print("}\nreturn res;\n");
	printer.Outdent();
	printer.Print("}\n\n");
}

bool DummyCodeGenerator::GenerateHeader(const FileDescriptor* file, compiler::GeneratorContext* context, std::string* error) const {
	std::unique_ptr<io::ZeroCopyOutputStream> output(context->Open(file->name() + ".h"));
	io::Printer printer(output.get(), '$');
	compiler::Version ver;
	context->GetCompilerVersion(&ver);

	auto ns = JoinStrings(Split(file->package(), "."), "::");

	std::map<std::string, std::string> global_args{
		{"FILE_NAME", file->name()},
		{"SCOPE_NAME", JoinStrings(Split(file->name(), "."), "_")},
		{"MINIPB_VERSION", "0.0.1"},
		{"PROTO_VERSION", std::to_string(ver.major()) + "." + std::to_string(ver.minor()) + "." + std::to_string(ver.patch()) + "-" + ver.suffix()},
		{"NAMESPACE", ns} //
	};
	printer.Print(global_args, R"(#ifndef MINIPB_GEN_$SCOPE_NAME$_INCLUDED
#define MINIPB_GEN_$SCOPE_NAME$_INCLUDED
/*
 * Generated by proto-minipb $MINIPB_VERSION$ compiled against protobuf $PROTO_VERSION$
 * from $FILE_NAME$
 */
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace minipb {
    enum class result;
    class msg_builder;
    class msg_parser;
}

)");
	if (!ns.empty()) {
		printer.Print(global_args, "namespace $NAMESPACE$ {\n");
		printer.Indent();
	}
	for (int i = 0; i < file->message_type_count(); i++)
	{
		const Descriptor* m = file->message_type(i);
		printer.Print(("struct " + m->name() + ";\n").c_str());
	}
	printer.Print("\n");

	for (int i = 0; i < file->message_type_count(); i++)
	{
		const Descriptor* m = file->message_type(i);
		EmitStructure(global_args, m, printer);
	}

	if (!ns.empty()) {
		printer.Outdent();
		printer.Print(global_args, "} // $NAMESPACE$\n");
	}
	printer.Print(global_args, "#endif // MINIPB_GEN_$SCOPE_NAME$_INCLUDED\n");

	if (printer.failed())
	{
		*error = "MinipbCodeGenerator detected write error.";
		return false;
	}
	return true;
}

bool DummyCodeGenerator::GenerateImpl(const FileDescriptor* file, compiler::GeneratorContext* context, std::string* error) const {
	std::unique_ptr<io::ZeroCopyOutputStream> output(context->Open(file->name() + ".cpp"));
	io::Printer printer(output.get(), '$');
	compiler::Version ver;
	context->GetCompilerVersion(&ver);

	auto ns = JoinStrings(Split(file->package(), "."), "::");

	std::map<std::string, std::string> global_args{
		{"FILE_NAME", file->name()},
		{"SCOPE_NAME", JoinStrings(Split(file->name(), "."), "_")},
		{"MINIPB_VERSION", "0.0.1"},
		{"PROTO_VERSION", std::to_string(ver.major()) + "." + std::to_string(ver.minor()) + "." + std::to_string(ver.patch()) + "-" + ver.suffix()},
		{"NAMESPACE", ns} //
	};
	printer.Print(global_args, R"(/*
 * Generated by proto-minipb $MINIPB_VERSION$ compiled against protobuf $PROTO_VERSION$
 * from $FILE_NAME$
 */
#include <cstddef>
#include <memory>
#include <string>
#include <vector>
#include <minipb/minipb.h>

)");
	if (!ns.empty()) {
		printer.Print(global_args, "namespace $NAMESPACE$ {\n");
		printer.Indent();
	}
	for (int i = 0; i < file->message_type_count(); i++)
	{
		const Descriptor* m = file->message_type(i);
		printer.Print(("struct " + m->name() + ";\n").c_str());
	}
	printer.Print("\n");

	for (int i = 0; i < file->message_type_count(); i++)
	{
		const Descriptor* m = file->message_type(i);
		EmitStructure(global_args, m, printer);
	}

	for (int i = 0; i < file->message_type_count(); i++)
	{
		const Descriptor* m = file->message_type(i);
		// clang-format off
        auto message_args = combine(global_args,
        {
            {"MSG_NAME", m->name()},
            {"MSG_NAME_FULL", m->full_name()},
        });
		// clang-format on

		EmitEstimateSize(message_args, m, printer);
		EmitEncode(message_args, m, printer);
        EmitDecode(message_args, m, printer);
	}

	if (!ns.empty()) {
		printer.Outdent();
		printer.Print(global_args, "} // $NAMESPACE$\n");
	}

	if (printer.failed())
	{
		*error = "MinipbCodeGenerator detected write error.";
		return false;
	}
	return true;
}

int main(int argc, char** argv) {
	DummyCodeGenerator generator;
	return compiler::PluginMain(argc, argv, &generator);
}