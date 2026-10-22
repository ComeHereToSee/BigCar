// Copyright BigCar Team. All Rights Reserved.
//
// Tiny dependency-free test framework (no exceptions, so it also builds with -fno-exceptions like Unreal).
#pragma once

#include <cmath>
#include <cstdio>
#include <functional>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

namespace bigcar_test
{
struct TestCase
{
	const char* name;
	std::function<void()> fn;
};

inline std::vector<TestCase>& Registry()
{
	static std::vector<TestCase> tests;
	return tests;
}

struct Context
{
	int failures = 0;
	int checks = 0;
	const char* current = "";
};

inline Context& Ctx()
{
	static Context c;
	return c;
}

struct Registrar
{
	Registrar(const char* name, std::function<void()> fn) { Registry().push_back({name, std::move(fn)}); }
};

inline void ReportFailure(const char* file, int line, const std::string& message)
{
	++Ctx().failures;
	std::fprintf(stderr, "  FAILED %s:%d [%s] %s\n", file, line, Ctx().current, message.c_str());
}

template <typename T> void Print(std::ostringstream& os, const T& v)
{
	if constexpr (std::is_enum_v<T>)
	{
		os << static_cast<long long>(v);
	}
	else
	{
		os << v;
	}
}

template <typename A, typename B> std::string Describe(const A& a, const B& b)
{
	std::ostringstream os;
	os << "(";
	Print(os, a);
	os << " vs ";
	Print(os, b);
	os << ")";
	return os.str();
}
} // namespace bigcar_test

#define BC_CONCAT_INNER(a, b) a##b
#define BC_CONCAT(a, b) BC_CONCAT_INNER(a, b)

#define TEST_CASE(name)                                                                                                                    \
	static void BC_CONCAT(bc_test_fn_, __LINE__)();                                                                                        \
	static bigcar_test::Registrar BC_CONCAT(bc_test_reg_, __LINE__)(name, &BC_CONCAT(bc_test_fn_, __LINE__));                              \
	static void BC_CONCAT(bc_test_fn_, __LINE__)()

#define CHECK(expr)                                                                                                                        \
	do                                                                                                                                     \
	{                                                                                                                                      \
		++bigcar_test::Ctx().checks;                                                                                                       \
		if (!(expr))                                                                                                                       \
		{                                                                                                                                  \
			bigcar_test::ReportFailure(__FILE__, __LINE__, "CHECK(" #expr ")");                                                            \
		}                                                                                                                                  \
	} while (0)

#define CHECK_MSG(expr, msg)                                                                                                               \
	do                                                                                                                                     \
	{                                                                                                                                      \
		++bigcar_test::Ctx().checks;                                                                                                       \
		if (!(expr))                                                                                                                       \
		{                                                                                                                                  \
			std::ostringstream bc_os_;                                                                                                     \
			bc_os_ << "CHECK(" #expr ") " << msg;                                                                                          \
			bigcar_test::ReportFailure(__FILE__, __LINE__, bc_os_.str());                                                                  \
		}                                                                                                                                  \
	} while (0)

#define CHECK_EQ(a, b)                                                                                                                     \
	do                                                                                                                                     \
	{                                                                                                                                      \
		++bigcar_test::Ctx().checks;                                                                                                       \
		if (!((a) == (b)))                                                                                                                 \
		{                                                                                                                                  \
			bigcar_test::ReportFailure(__FILE__, __LINE__, "CHECK_EQ(" #a ", " #b ") " + bigcar_test::Describe((a), (b)));                 \
		}                                                                                                                                  \
	} while (0)

#define CHECK_NEAR(a, b, tol)                                                                                                              \
	do                                                                                                                                     \
	{                                                                                                                                      \
		++bigcar_test::Ctx().checks;                                                                                                       \
		if (!(std::fabs(static_cast<double>(a) - static_cast<double>(b)) <= static_cast<double>(tol)))                                     \
		{                                                                                                                                  \
			bigcar_test::ReportFailure(__FILE__, __LINE__, "CHECK_NEAR(" #a ", " #b ") " + bigcar_test::Describe((a), (b)));               \
		}                                                                                                                                  \
	} while (0)

// Stops the current test case on failure.
#define REQUIRE(expr)                                                                                                                      \
	do                                                                                                                                     \
	{                                                                                                                                      \
		++bigcar_test::Ctx().checks;                                                                                                       \
		if (!(expr))                                                                                                                       \
		{                                                                                                                                  \
			bigcar_test::ReportFailure(__FILE__, __LINE__, "REQUIRE(" #expr ")");                                                          \
			return;                                                                                                                        \
		}                                                                                                                                  \
	} while (0)
