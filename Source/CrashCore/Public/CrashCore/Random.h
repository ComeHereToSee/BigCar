// Copyright BigCar Team. All Rights Reserved.
//
// Deterministic PCG32 (O'Neill, pcg32_random_r / XSH-RR) so simulations and AI are reproducible across platforms.
#pragma once

#include <cstdint>

namespace crash
{
class Pcg32
{
public:
	explicit Pcg32(uint64_t seed, uint64_t sequence = 0x14057b7ef767814fULL) { Seed(seed, sequence); }

	void Seed(uint64_t seed, uint64_t sequence = 0x14057b7ef767814fULL)
	{
		state_ = 0u;
		inc_ = (sequence << 1u) | 1u;
		NextU32();
		state_ += seed;
		NextU32();
	}

	uint32_t NextU32()
	{
		const uint64_t old = state_;
		state_ = old * 6364136223846793005ULL + inc_;
		const uint32_t xorShifted = static_cast<uint32_t>(((old >> 18u) ^ old) >> 27u);
		const uint32_t rot = static_cast<uint32_t>(old >> 59u);
		return (xorShifted >> rot) | (xorShifted << ((~rot + 1u) & 31u));
	}

	// Uniform in [0, bound) without modulo bias.
	uint32_t NextBounded(uint32_t bound)
	{
		if (bound == 0)
		{
			return 0;
		}
		const uint32_t threshold = (~bound + 1u) % bound;
		for (;;)
		{
			const uint32_t r = NextU32();
			if (r >= threshold)
			{
				return r % bound;
			}
		}
	}

	// Uniform in [0, 1).
	float NextFloat() { return static_cast<float>(NextU32() >> 8) * (1.0f / 16777216.0f); }
	float Range(float lo, float hi) { return lo + (hi - lo) * NextFloat(); }
	bool Chance(float probability) { return NextFloat() < probability; }

private:
	uint64_t state_ = 0;
	uint64_t inc_ = 1;
};

} // namespace crash
