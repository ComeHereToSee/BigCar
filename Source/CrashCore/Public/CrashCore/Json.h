// Copyright BigCar Team. All Rights Reserved.
//
// Minimal JSON DOM + parser + writer. Locale independent, exception free (Unreal builds without exceptions).
// Objects keep insertion order so saved files diff nicely.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace crash
{
class JsonValue
{
public:
	enum class Type : uint8_t
	{
		Null,
		Bool,
		Number,
		String,
		Array,
		Object
	};

	using Member = std::pair<std::string, JsonValue>;

	JsonValue() = default;
	JsonValue(std::nullptr_t) {}
	JsonValue(bool value) : type_(Type::Bool), bool_(value) {}
	JsonValue(int value) : type_(Type::Number), number_(static_cast<double>(value)) {}
	JsonValue(int64_t value) : type_(Type::Number), number_(static_cast<double>(value)) {}
	JsonValue(uint32_t value) : type_(Type::Number), number_(static_cast<double>(value)) {}
	JsonValue(uint64_t value) : type_(Type::Number), number_(static_cast<double>(value)) {}
	JsonValue(float value) : type_(Type::Number), number_(static_cast<double>(value)) {}
	JsonValue(double value) : type_(Type::Number), number_(value) {}
	JsonValue(const char* value) : type_(Type::String), string_(value ? value : "") {}
	JsonValue(std::string value) : type_(Type::String), string_(std::move(value)) {}
	JsonValue(std::string_view value) : type_(Type::String), string_(value) {}

	static JsonValue MakeArray()
	{
		JsonValue v;
		v.type_ = Type::Array;
		return v;
	}
	static JsonValue MakeObject()
	{
		JsonValue v;
		v.type_ = Type::Object;
		return v;
	}
	static const JsonValue& Null();

	Type GetType() const { return type_; }
	bool IsNull() const { return type_ == Type::Null; }
	bool IsBool() const { return type_ == Type::Bool; }
	bool IsNumber() const { return type_ == Type::Number; }
	bool IsString() const { return type_ == Type::String; }
	bool IsArray() const { return type_ == Type::Array; }
	bool IsObject() const { return type_ == Type::Object; }

	bool AsBool(bool fallback = false) const { return type_ == Type::Bool ? bool_ : fallback; }
	double AsDouble(double fallback = 0.0) const { return type_ == Type::Number ? number_ : fallback; }
	float AsFloat(float fallback = 0.0f) const { return type_ == Type::Number ? static_cast<float>(number_) : fallback; }
	int AsInt(int fallback = 0) const;
	int64_t AsInt64(int64_t fallback = 0) const;
	const std::string& AsString() const;
	std::string AsString(std::string_view fallback) const { return type_ == Type::String ? string_ : std::string(fallback); }

	// Arrays
	size_t Size() const;
	const JsonValue& operator[](size_t index) const;
	// Disambiguates literal indices like v[0] (0 is also a null pointer constant).
	const JsonValue& operator[](int index) const { return index < 0 ? Null() : (*this)[static_cast<size_t>(index)]; }
	const std::vector<JsonValue>& Items() const;
	JsonValue& Push(JsonValue value);

	// Objects
	bool Has(std::string_view key) const;
	const JsonValue& operator[](std::string_view key) const;
	const JsonValue& operator[](const char* key) const { return (*this)[std::string_view(key)]; }
	const std::vector<Member>& Members() const;
	// Inserts or replaces a member. Converts a null value into an object.
	JsonValue& Set(std::string_view key, JsonValue value);
	JsonValue* Find(std::string_view key);
	bool Remove(std::string_view key);

	std::string Dump(bool pretty = false) const;

private:
	void DumpTo(std::string& out, bool pretty, int indent) const;

	Type type_ = Type::Null;
	bool bool_ = false;
	double number_ = 0.0;
	std::string string_;
	std::vector<JsonValue> array_;
	std::vector<Member> object_;
};

struct JsonError
{
	std::string message;
	int line = 0;
	int column = 0;

	std::string ToString() const;
};

// Parses UTF-8 JSON text. Returns false and fills error on failure.
bool ParseJson(std::string_view text, JsonValue& out, JsonError* error = nullptr);

// Escapes a string as a JSON string literal (with quotes).
std::string JsonQuote(std::string_view text);

} // namespace crash
