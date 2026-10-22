// Copyright BigCar Team. All Rights Reserved.

#include "TestFramework.h"

#include <chrono>
#include <cstring>

int main(int argc, char** argv)
{
	const char* filter = argc > 1 ? argv[1] : nullptr;
	int ran = 0;
	int failedCases = 0;
	const auto start = std::chrono::steady_clock::now();
	for (const bigcar_test::TestCase& t : bigcar_test::Registry())
	{
		if (filter && std::strstr(t.name, filter) == nullptr)
		{
			continue;
		}
		bigcar_test::Ctx().current = t.name;
		const int before = bigcar_test::Ctx().failures;
		const auto caseStart = std::chrono::steady_clock::now();
		t.fn();
		const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - caseStart).count();
		const bool ok = bigcar_test::Ctx().failures == before;
		std::printf("[%s] %s (%.1f ms)\n", ok ? " OK " : "FAIL", t.name, ms);
		failedCases += ok ? 0 : 1;
		++ran;
	}
	const double total = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
	std::printf("\n%d test cases, %d checks, %d failed cases, %d failed checks (%.2f s)\n", ran, bigcar_test::Ctx().checks, failedCases,
	            bigcar_test::Ctx().failures, total);
	return failedCases == 0 && ran > 0 ? 0 : 1;
}
