package store

// pcg32 is O'Neill's PCG32 generator, variant PCG-XSH-RR with 64-bit state
// and 32-bit output, exactly as in the reference implementation
// pcg_basic.c (pcg32_random_r / pcg32_srandom_r / pcg32_boundedrand_r):
//
//	multiplier  = 6364136223846793005
//	increment   = (initseq << 1) | 1
//	seeding     : state = 0; step(); state += initstate; step()
//	output      : xorshifted = uint32(((old >> 18) ^ old) >> 27)
//	              rot        = uint32(old >> 59)
//	              result     = (xorshifted >> rot) | (xorshifted << ((-rot) & 31))
//	bounded(n)  : threshold = (-n) % n (uint32 arithmetic); draw r until
//	              r >= threshold; return r % n
//
// Daily deals seed it with initstate = UTC day number and initseq = 54 (the
// reference demo's stream), so any client can reproduce the deals.
type pcg32 struct {
	state uint64
	inc   uint64
}

const (
	pcgMultiplier = 6364136223846793005
	// DailyDealStream is the PCG32 initseq used for daily deals.
	DailyDealStream = 54
)

func newPCG32(initState, initSeq uint64) *pcg32 {
	r := &pcg32{state: 0, inc: initSeq<<1 | 1}
	r.next()
	r.state += initState
	r.next()
	return r
}

func (r *pcg32) next() uint32 {
	old := r.state
	r.state = old*pcgMultiplier + r.inc
	xorshifted := uint32(((old >> 18) ^ old) >> 27)
	rot := uint32(old >> 59)
	return (xorshifted >> rot) | (xorshifted << ((-rot) & 31))
}

// bounded returns a uniformly distributed number in [0, bound) without
// modulo bias. bound must be > 0.
func (r *pcg32) bounded(bound uint32) uint32 {
	threshold := -bound % bound
	for {
		if x := r.next(); x >= threshold {
			return x % bound
		}
	}
}
