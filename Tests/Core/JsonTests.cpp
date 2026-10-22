// Copyright BigCar Team. All Rights Reserved.

#include "CrashCore/Json.h"
#include "TestFramework.h"

using namespace crash;

TEST_CASE("json: parse scalars, arrays, objects")
{
	JsonValue v;
	JsonError err;
	REQUIRE(ParseJson(R"({"a": 1, "b": [true, false, null], "c": {"d": "x"}, "e": -2.5e2, "f": 0.125})", v, &err));
	CHECK(v.IsObject());
	CHECK_EQ(v["a"].AsInt(), 1);
	CHECK(v["b"].IsArray());
	CHECK_EQ(v["b"].Size(), 3u);
	CHECK(v["b"][0].AsBool());
	CHECK(v["b"][2].IsNull());
	CHECK_EQ(v["c"]["d"].AsString(), std::string("x"));
	CHECK_NEAR(v["e"].AsDouble(), -250.0, 1e-9);
	CHECK_NEAR(v["f"].AsDouble(), 0.125, 1e-12);
	CHECK(v["missing"].IsNull());
	CHECK(v["missing"]["deeper"].IsNull());
}

TEST_CASE("json: unicode escapes and utf8 passthrough")
{
	JsonValue v;
	REQUIRE(ParseJson(R"({"zh": "撞击之王", "esc": "中文 🚗 \"q\" \\ \n"})", v));
	CHECK_EQ(v["zh"].AsString(), std::string("撞击之王"));
	CHECK_EQ(v["esc"].AsString(), std::string("中文 🚗 \"q\" \\ \n"));
	JsonValue round;
	REQUIRE(ParseJson(v.Dump(), round));
	CHECK_EQ(round["esc"].AsString(), v["esc"].AsString());
}

TEST_CASE("json: errors report position")
{
	JsonValue v;
	JsonError err;
	CHECK(!ParseJson("{\n  \"a\": 1,\n  \"b\": }", v, &err));
	CHECK_EQ(err.line, 3);
	CHECK(!ParseJson("[1, 2,]", v, &err));
	CHECK(!ParseJson("{\"a\": 1} trailing", v, &err));
	CHECK(!ParseJson("{\"a\": 1, \"a\": 2}", v, &err)); // duplicate keys are rejected
	CHECK(!ParseJson("\"unterminated", v, &err));
	CHECK(!ParseJson("01", v, &err));
}

TEST_CASE("json: dump round trip keeps order and numbers")
{
	JsonValue obj = JsonValue::MakeObject();
	obj.Set("z", JsonValue(1));
	obj.Set("a", JsonValue(2.5));
	obj.Set("big", JsonValue(static_cast<int64_t>(4000000000LL)));
	JsonValue arr = JsonValue::MakeArray();
	arr.Push(JsonValue("x"));
	arr.Push(JsonValue(false));
	obj.Set("arr", std::move(arr));
	const std::string compact = obj.Dump();
	CHECK_EQ(compact, std::string(R"({"z":1,"a":2.5,"big":4000000000,"arr":["x",false]})"));
	JsonValue parsed;
	REQUIRE(ParseJson(obj.Dump(true), parsed));
	CHECK_EQ(parsed.Dump(), compact);
	CHECK(parsed.Remove("a"));
	CHECK(!parsed.Has("a"));
}
