// Copyright BigCar Team. All Rights Reserved.

#include "CrashCore/Json.h"

#include <cmath>
#include <cstdio>
#include <limits>

namespace crash
{
namespace
{
const std::string kEmptyString;
const std::vector<JsonValue> kEmptyArray;
const std::vector<JsonValue::Member> kEmptyObject;

class JsonParser
{
public:
	explicit JsonParser(std::string_view text) : text_(text) {}

	bool Parse(JsonValue& out, JsonError* error)
	{
		SkipWhitespace();
		if (!ParseValue(out, 0))
		{
			FillError(error);
			return false;
		}
		SkipWhitespace();
		if (pos_ != text_.size())
		{
			Fail("unexpected trailing characters");
			FillError(error);
			return false;
		}
		return true;
	}

private:
	static constexpr int kMaxDepth = 128;

	void FillError(JsonError* error) const
	{
		if (!error)
		{
			return;
		}
		error->message = message_;
		int line = 1;
		int column = 1;
		for (size_t i = 0; i < errorPos_ && i < text_.size(); ++i)
		{
			if (text_[i] == '\n')
			{
				++line;
				column = 1;
			}
			else
			{
				++column;
			}
		}
		error->line = line;
		error->column = column;
	}

	bool Fail(const char* message)
	{
		if (message_.empty())
		{
			message_ = message;
			errorPos_ = pos_;
		}
		return false;
	}

	void SkipWhitespace()
	{
		while (pos_ < text_.size())
		{
			const char c = text_[pos_];
			if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
			{
				++pos_;
			}
			else
			{
				break;
			}
		}
	}

	bool Consume(std::string_view literal)
	{
		if (text_.substr(pos_, literal.size()) == literal)
		{
			pos_ += literal.size();
			return true;
		}
		return false;
	}

	bool ParseValue(JsonValue& out, int depth)
	{
		if (depth > kMaxDepth)
		{
			return Fail("nesting too deep");
		}
		if (pos_ >= text_.size())
		{
			return Fail("unexpected end of input");
		}
		const char c = text_[pos_];
		switch (c)
		{
		case '{':
			return ParseObject(out, depth);
		case '[':
			return ParseArray(out, depth);
		case '"':
		{
			std::string s;
			if (!ParseString(s))
			{
				return false;
			}
			out = JsonValue(std::move(s));
			return true;
		}
		case 't':
			if (Consume("true"))
			{
				out = JsonValue(true);
				return true;
			}
			return Fail("invalid literal");
		case 'f':
			if (Consume("false"))
			{
				out = JsonValue(false);
				return true;
			}
			return Fail("invalid literal");
		case 'n':
			if (Consume("null"))
			{
				out = JsonValue();
				return true;
			}
			return Fail("invalid literal");
		default:
			if (c == '-' || (c >= '0' && c <= '9'))
			{
				return ParseNumber(out);
			}
			return Fail("unexpected character");
		}
	}

	bool ParseObject(JsonValue& out, int depth)
	{
		++pos_; // '{'
		out = JsonValue::MakeObject();
		SkipWhitespace();
		if (pos_ < text_.size() && text_[pos_] == '}')
		{
			++pos_;
			return true;
		}
		while (true)
		{
			SkipWhitespace();
			if (pos_ >= text_.size() || text_[pos_] != '"')
			{
				return Fail("expected object key");
			}
			std::string key;
			if (!ParseString(key))
			{
				return false;
			}
			SkipWhitespace();
			if (pos_ >= text_.size() || text_[pos_] != ':')
			{
				return Fail("expected ':'");
			}
			++pos_;
			SkipWhitespace();
			JsonValue value;
			if (!ParseValue(value, depth + 1))
			{
				return false;
			}
			if (out.Has(key))
			{
				return Fail("duplicate object key");
			}
			out.Set(key, std::move(value));
			SkipWhitespace();
			if (pos_ >= text_.size())
			{
				return Fail("unterminated object");
			}
			if (text_[pos_] == ',')
			{
				++pos_;
				continue;
			}
			if (text_[pos_] == '}')
			{
				++pos_;
				return true;
			}
			return Fail("expected ',' or '}'");
		}
	}

	bool ParseArray(JsonValue& out, int depth)
	{
		++pos_; // '['
		out = JsonValue::MakeArray();
		SkipWhitespace();
		if (pos_ < text_.size() && text_[pos_] == ']')
		{
			++pos_;
			return true;
		}
		while (true)
		{
			SkipWhitespace();
			JsonValue value;
			if (!ParseValue(value, depth + 1))
			{
				return false;
			}
			out.Push(std::move(value));
			SkipWhitespace();
			if (pos_ >= text_.size())
			{
				return Fail("unterminated array");
			}
			if (text_[pos_] == ',')
			{
				++pos_;
				continue;
			}
			if (text_[pos_] == ']')
			{
				++pos_;
				return true;
			}
			return Fail("expected ',' or ']'");
		}
	}

	static void AppendUtf8(std::string& out, uint32_t cp)
	{
		if (cp < 0x80)
		{
			out.push_back(static_cast<char>(cp));
		}
		else if (cp < 0x800)
		{
			out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
			out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
		}
		else if (cp < 0x10000)
		{
			out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
			out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
			out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
		}
		else
		{
			out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
			out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
			out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
			out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
		}
	}

	bool ParseHex4(uint32_t& out)
	{
		if (pos_ + 4 > text_.size())
		{
			return Fail("truncated unicode escape");
		}
		uint32_t value = 0;
		for (int i = 0; i < 4; ++i)
		{
			const char h = text_[pos_++];
			value <<= 4;
			if (h >= '0' && h <= '9')
			{
				value |= static_cast<uint32_t>(h - '0');
			}
			else if (h >= 'a' && h <= 'f')
			{
				value |= static_cast<uint32_t>(h - 'a' + 10);
			}
			else if (h >= 'A' && h <= 'F')
			{
				value |= static_cast<uint32_t>(h - 'A' + 10);
			}
			else
			{
				return Fail("invalid unicode escape");
			}
		}
		out = value;
		return true;
	}

	bool ParseString(std::string& out)
	{
		++pos_; // opening quote
		while (pos_ < text_.size())
		{
			const char c = text_[pos_++];
			if (c == '"')
			{
				return true;
			}
			if (static_cast<unsigned char>(c) < 0x20)
			{
				return Fail("control character in string");
			}
			if (c != '\\')
			{
				out.push_back(c);
				continue;
			}
			if (pos_ >= text_.size())
			{
				break;
			}
			const char e = text_[pos_++];
			switch (e)
			{
			case '"':
				out.push_back('"');
				break;
			case '\\':
				out.push_back('\\');
				break;
			case '/':
				out.push_back('/');
				break;
			case 'b':
				out.push_back('\b');
				break;
			case 'f':
				out.push_back('\f');
				break;
			case 'n':
				out.push_back('\n');
				break;
			case 'r':
				out.push_back('\r');
				break;
			case 't':
				out.push_back('\t');
				break;
			case 'u':
			{
				uint32_t cp = 0;
				if (!ParseHex4(cp))
				{
					return false;
				}
				if (cp >= 0xD800 && cp <= 0xDBFF)
				{
					uint32_t low = 0;
					if (!Consume("\\u") || !ParseHex4(low) || low < 0xDC00 || low > 0xDFFF)
					{
						return Fail("invalid surrogate pair");
					}
					cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
				}
				else if (cp >= 0xDC00 && cp <= 0xDFFF)
				{
					return Fail("unpaired low surrogate");
				}
				AppendUtf8(out, cp);
				break;
			}
			default:
				return Fail("invalid escape");
			}
		}
		return Fail("unterminated string");
	}

	bool ParseNumber(JsonValue& out)
	{
		const size_t start = pos_;
		bool negative = false;
		if (text_[pos_] == '-')
		{
			negative = true;
			++pos_;
		}
		if (pos_ >= text_.size() || text_[pos_] < '0' || text_[pos_] > '9')
		{
			return Fail("invalid number");
		}
		// Accumulate up to 19 significant digits exactly, track the decimal exponent for the rest.
		uint64_t mantissa = 0;
		int digits = 0;
		int exponent10 = 0;
		if (text_[pos_] == '0')
		{
			++pos_;
		}
		else
		{
			while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9')
			{
				if (digits < 19)
				{
					mantissa = mantissa * 10 + static_cast<uint64_t>(text_[pos_] - '0');
					++digits;
				}
				else
				{
					++exponent10;
				}
				++pos_;
			}
		}
		if (pos_ < text_.size() && text_[pos_] == '.')
		{
			++pos_;
			if (pos_ >= text_.size() || text_[pos_] < '0' || text_[pos_] > '9')
			{
				return Fail("invalid number fraction");
			}
			while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9')
			{
				if (digits < 19)
				{
					mantissa = mantissa * 10 + static_cast<uint64_t>(text_[pos_] - '0');
					++digits;
					--exponent10;
				}
				++pos_;
			}
		}
		if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E'))
		{
			++pos_;
			bool expNegative = false;
			if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-'))
			{
				expNegative = text_[pos_] == '-';
				++pos_;
			}
			if (pos_ >= text_.size() || text_[pos_] < '0' || text_[pos_] > '9')
			{
				return Fail("invalid number exponent");
			}
			int expValue = 0;
			while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9')
			{
				if (expValue < 10000)
				{
					expValue = expValue * 10 + (text_[pos_] - '0');
				}
				++pos_;
			}
			exponent10 += expNegative ? -expValue : expValue;
		}
		(void)start;
		double value = static_cast<double>(mantissa);
		if (exponent10 != 0 && mantissa != 0)
		{
			// Split the scaling to keep intermediate values finite.
			if (exponent10 < -300)
			{
				value *= std::pow(10.0, -300);
				exponent10 += 300;
			}
			value *= std::pow(10.0, exponent10);
		}
		if (!std::isfinite(value))
		{
			return Fail("number out of range");
		}
		out = JsonValue(negative ? -value : value);
		return true;
	}

	std::string_view text_;
	size_t pos_ = 0;
	std::string message_;
	size_t errorPos_ = 0;
};

void AppendIndent(std::string& out, int indent)
{
	out.append(static_cast<size_t>(indent) * 2, ' ');
}

void AppendNumber(std::string& out, double value)
{
	if (!std::isfinite(value))
	{
		out += "0";
		return;
	}
	const double rounded = std::round(value);
	if (rounded == value && std::fabs(value) < 9.0e15)
	{
		char buffer[32];
		std::snprintf(buffer, sizeof(buffer), "%lld", static_cast<long long>(rounded));
		out += buffer;
		return;
	}
	char buffer[40];
	std::snprintf(buffer, sizeof(buffer), "%.9g", value);
	for (char* p = buffer; *p; ++p)
	{
		if (*p == ',')
		{
			*p = '.'; // guard against non-C numeric locales
		}
	}
	out += buffer;
}
} // namespace

const JsonValue& JsonValue::Null()
{
	static const JsonValue kNull;
	return kNull;
}

int JsonValue::AsInt(int fallback) const
{
	if (type_ != Type::Number)
	{
		return fallback;
	}
	const double clamped = std::fmax(std::fmin(number_, static_cast<double>(std::numeric_limits<int>::max())),
	                                 static_cast<double>(std::numeric_limits<int>::min()));
	return static_cast<int>(std::llround(clamped));
}

int64_t JsonValue::AsInt64(int64_t fallback) const
{
	if (type_ != Type::Number)
	{
		return fallback;
	}
	const double clamped = std::fmax(std::fmin(number_, 9.0e18), -9.0e18);
	return static_cast<int64_t>(std::llround(clamped));
}

const std::string& JsonValue::AsString() const
{
	return type_ == Type::String ? string_ : kEmptyString;
}

size_t JsonValue::Size() const
{
	if (type_ == Type::Array)
	{
		return array_.size();
	}
	if (type_ == Type::Object)
	{
		return object_.size();
	}
	return 0;
}

const JsonValue& JsonValue::operator[](size_t index) const
{
	if (type_ == Type::Array && index < array_.size())
	{
		return array_[index];
	}
	return Null();
}

const std::vector<JsonValue>& JsonValue::Items() const
{
	return type_ == Type::Array ? array_ : kEmptyArray;
}

JsonValue& JsonValue::Push(JsonValue value)
{
	if (type_ != Type::Array)
	{
		*this = MakeArray();
	}
	array_.push_back(std::move(value));
	return array_.back();
}

bool JsonValue::Has(std::string_view key) const
{
	if (type_ != Type::Object)
	{
		return false;
	}
	for (const Member& m : object_)
	{
		if (m.first == key)
		{
			return true;
		}
	}
	return false;
}

const JsonValue& JsonValue::operator[](std::string_view key) const
{
	if (type_ == Type::Object)
	{
		for (const Member& m : object_)
		{
			if (m.first == key)
			{
				return m.second;
			}
		}
	}
	return Null();
}

const std::vector<JsonValue::Member>& JsonValue::Members() const
{
	return type_ == Type::Object ? object_ : kEmptyObject;
}

JsonValue& JsonValue::Set(std::string_view key, JsonValue value)
{
	if (type_ != Type::Object)
	{
		*this = MakeObject();
	}
	for (Member& m : object_)
	{
		if (m.first == key)
		{
			m.second = std::move(value);
			return m.second;
		}
	}
	object_.emplace_back(std::string(key), std::move(value));
	return object_.back().second;
}

JsonValue* JsonValue::Find(std::string_view key)
{
	if (type_ != Type::Object)
	{
		return nullptr;
	}
	for (Member& m : object_)
	{
		if (m.first == key)
		{
			return &m.second;
		}
	}
	return nullptr;
}

bool JsonValue::Remove(std::string_view key)
{
	if (type_ != Type::Object)
	{
		return false;
	}
	for (auto it = object_.begin(); it != object_.end(); ++it)
	{
		if (it->first == key)
		{
			object_.erase(it);
			return true;
		}
	}
	return false;
}

std::string JsonQuote(std::string_view text)
{
	std::string out;
	out.reserve(text.size() + 2);
	out.push_back('"');
	for (const char c : text)
	{
		switch (c)
		{
		case '"':
			out += "\\\"";
			break;
		case '\\':
			out += "\\\\";
			break;
		case '\n':
			out += "\\n";
			break;
		case '\r':
			out += "\\r";
			break;
		case '\t':
			out += "\\t";
			break;
		case '\b':
			out += "\\b";
			break;
		case '\f':
			out += "\\f";
			break;
		default:
			if (static_cast<unsigned char>(c) < 0x20)
			{
				char buffer[8];
				std::snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
				out += buffer;
			}
			else
			{
				out.push_back(c);
			}
			break;
		}
	}
	out.push_back('"');
	return out;
}

std::string JsonValue::Dump(bool pretty) const
{
	std::string out;
	DumpTo(out, pretty, 0);
	if (pretty)
	{
		out.push_back('\n');
	}
	return out;
}

void JsonValue::DumpTo(std::string& out, bool pretty, int indent) const
{
	switch (type_)
	{
	case Type::Null:
		out += "null";
		break;
	case Type::Bool:
		out += bool_ ? "true" : "false";
		break;
	case Type::Number:
		AppendNumber(out, number_);
		break;
	case Type::String:
		out += JsonQuote(string_);
		break;
	case Type::Array:
	{
		if (array_.empty())
		{
			out += "[]";
			break;
		}
		// Arrays of scalars stay on one line for readability.
		bool scalarOnly = true;
		for (const JsonValue& v : array_)
		{
			if (v.IsArray() || v.IsObject())
			{
				scalarOnly = false;
				break;
			}
		}
		out.push_back('[');
		for (size_t i = 0; i < array_.size(); ++i)
		{
			if (i > 0)
			{
				out.push_back(',');
				if (pretty && scalarOnly)
				{
					out.push_back(' ');
				}
			}
			if (pretty && !scalarOnly)
			{
				out.push_back('\n');
				AppendIndent(out, indent + 1);
			}
			array_[i].DumpTo(out, pretty, indent + 1);
		}
		if (pretty && !scalarOnly)
		{
			out.push_back('\n');
			AppendIndent(out, indent);
		}
		out.push_back(']');
		break;
	}
	case Type::Object:
	{
		if (object_.empty())
		{
			out += "{}";
			break;
		}
		out.push_back('{');
		for (size_t i = 0; i < object_.size(); ++i)
		{
			if (i > 0)
			{
				out.push_back(',');
			}
			if (pretty)
			{
				out.push_back('\n');
				AppendIndent(out, indent + 1);
			}
			out += JsonQuote(object_[i].first);
			out.push_back(':');
			if (pretty)
			{
				out.push_back(' ');
			}
			object_[i].second.DumpTo(out, pretty, indent + 1);
		}
		if (pretty)
		{
			out.push_back('\n');
			AppendIndent(out, indent);
		}
		out.push_back('}');
		break;
	}
	}
}

std::string JsonError::ToString() const
{
	return "line " + std::to_string(line) + ", column " + std::to_string(column) + ": " + message;
}

bool ParseJson(std::string_view text, JsonValue& out, JsonError* error)
{
	// Skip a UTF-8 BOM if present.
	if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB &&
	    static_cast<unsigned char>(text[2]) == 0xBF)
	{
		text.remove_prefix(3);
	}
	JsonParser parser(text);
	return parser.Parse(out, error);
}

} // namespace crash
