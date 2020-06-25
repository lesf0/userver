#include <formats/json/string_builder.hpp>

#include <rapidjson/document.h>
#include <rapidjson/memorybuffer.h>
#include <rapidjson/writer.h>

namespace formats::json::impl {

struct StringBuilder::Impl {
  rapidjson::MemoryBuffer buffer;
  rapidjson::Writer<rapidjson::MemoryBuffer> writer{buffer};

  Impl() = default;
};

// Explicit ctr for clang-tidy
StringBuilder::StringBuilder() : impl_() {}

StringBuilder::~StringBuilder() = default;

std::string StringBuilder::GetString() const {
  const auto& buffer = impl_->buffer;
  return std::string(buffer.GetBuffer(), buffer.GetBuffer() + buffer.GetSize());
}

void StringBuilder::WriteNull() { impl_->writer.Null(); }

void StringBuilder::WriteString(std::string_view value) {
  impl_->writer.String(value.data(), value.size());
}

void StringBuilder::WriteBool(bool value) { impl_->writer.Bool(value); }

void StringBuilder::WriteInt64(int64_t value) { impl_->writer.Int64(value); }

void StringBuilder::WriteUInt64(uint64_t value) { impl_->writer.Uint64(value); }

void StringBuilder::WriteDouble(double value) { impl_->writer.Double(value); }

void StringBuilder::Key(std::string_view sw) {
  impl_->writer.Key(sw.data(), sw.size());
}

void StringBuilder::WriteRawString(std::string_view value) {
  impl_->writer.RawValue(value.data(), value.size(), {});
}

void StringBuilder::WriteValue(const ::formats::json::Value& value) {
  value.GetNative().Accept(impl_->writer);
}

void WriteToStream(bool value, StringBuilder& sw) { sw.WriteBool(value); }

void WriteToStream(int64_t value, StringBuilder& sw) { sw.WriteInt64(value); }

void WriteToStream(uint64_t value, StringBuilder& sw) { sw.WriteUInt64(value); }

void WriteToStream(double value, StringBuilder& sw) { sw.WriteDouble(value); }

void WriteToStream(const char* value, StringBuilder& sw) {
  WriteToStream(std::string_view{value}, sw);
}

void WriteToStream(std::string_view value, StringBuilder& sw) {
  sw.WriteString(value);
}

void WriteToStream(const ::formats::json::Value& value, StringBuilder& sw) {
  sw.WriteValue(value);
}

void WriteToStream(const std::string& value, StringBuilder& sw) {
  WriteToStream(std::string_view(value), sw);
}

StringBuilder::ObjectGuard::ObjectGuard(StringBuilder& sw) : sw_(sw) {
  sw_.impl_->writer.StartObject();
}

StringBuilder::ObjectGuard::~ObjectGuard() { sw_.impl_->writer.EndObject(); }

StringBuilder::ArrayGuard::ArrayGuard(StringBuilder& sw) : sw_(sw) {
  sw_.impl_->writer.StartArray();
}

StringBuilder::ArrayGuard::~ArrayGuard() { sw_.impl_->writer.EndArray(); }

}  // namespace formats::json::impl