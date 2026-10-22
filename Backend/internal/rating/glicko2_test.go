package rating

import (
	"math"
	"testing"
)

func near(a, b, tol float64) bool { return math.Abs(a-b) <= tol }

// TestGlickmanExample reproduces the worked example in Glickman's Glicko-2
// paper: a 1500/200/0.06 player beats a 1400/30 player and loses to 1550/100
// and 1700/300 players (τ = 0.5).
func TestGlickmanExample(t *testing.T) {
	p := Rating{R: 1500, RD: 200, Vol: 0.06}
	results := []Result{
		{Opponent: Rating{R: 1400, RD: 30}, Score: 1},
		{Opponent: Rating{R: 1550, RD: 100}, Score: 0},
		{Opponent: Rating{R: 1700, RD: 300}, Score: 0},
	}
	got := Update(p, results, 0.5)
	if !near(got.R, 1464.06, 0.01) {
		t.Errorf("rating = %.4f, want ≈1464.06", got.R)
	}
	if !near(got.RD, 151.52, 0.01) {
		t.Errorf("RD = %.4f, want ≈151.52", got.RD)
	}
	if !near(got.Vol, 0.05999, 0.00001) {
		t.Errorf("volatility = %.6f, want ≈0.05999", got.Vol)
	}
}

func TestIntermediateValues(t *testing.T) {
	// Paper step 3/4 values: v ≈ 1.7785, Δ ≈ -0.4834. The paper rounds g and E
	// to four decimals before summing, so allow a small tolerance.
	mu := 0.0
	ops := []struct{ r, rd, s float64 }{{1400, 30, 1}, {1550, 100, 0}, {1700, 300, 0}}
	var vInv, sum float64
	for _, o := range ops {
		muJ, phiJ := (o.r-1500)/scale, o.rd/scale
		e := expected(mu, muJ, phiJ)
		vInv += g(phiJ) * g(phiJ) * e * (1 - e)
		sum += g(phiJ) * (o.s - e)
	}
	v := 1 / vInv
	if !near(v, 1.7785, 0.001) {
		t.Errorf("v = %.5f", v)
	}
	if !near(v*sum, -0.4834, 0.001) {
		t.Errorf("delta = %.5f", v*sum)
	}
}

func TestUpdateProperties(t *testing.T) {
	d := Default()
	tests := []struct {
		name  string
		score float64
		check func(t *testing.T, r Rating)
	}{
		{"win raises", 1, func(t *testing.T, r Rating) {
			if r.R <= d.R {
				t.Errorf("rating %v did not rise", r.R)
			}
		}},
		{"loss lowers", 0, func(t *testing.T, r Rating) {
			if r.R >= d.R {
				t.Errorf("rating %v did not fall", r.R)
			}
		}},
		{"draw between equals keeps rating", 0.5, func(t *testing.T, r Rating) {
			if !near(r.R, d.R, 1e-9) {
				t.Errorf("rating %v changed", r.R)
			}
		}},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			r := Update(d, []Result{{Opponent: d, Score: tc.score}}, Tau)
			if r.RD >= d.RD {
				t.Errorf("RD %v should shrink after a game", r.RD)
			}
			tc.check(t, r)
		})
	}

	// Symmetry: winner gains what the loser loses for identical players.
	w := Update(d, []Result{{Opponent: d, Score: 1}}, Tau)
	l := Update(d, []Result{{Opponent: d, Score: 0}}, Tau)
	if !near(w.R-1500, 1500-l.R, 1e-6) {
		t.Errorf("asymmetric update: +%v / -%v", w.R-1500, 1500-l.R)
	}

	// No games: RD grows, rating unchanged.
	idle := Update(Rating{R: 1600, RD: 100, Vol: 0.06}, nil, Tau)
	if idle.R != 1600 || idle.RD <= 100 {
		t.Errorf("idle update = %+v", idle)
	}
}

func TestLargeUpsetConverges(t *testing.T) {
	// Exercises the "Δ² > φ² + v" branch of the volatility bracket.
	p := Rating{R: 1200, RD: 50, Vol: 0.06}
	r := Update(p, []Result{{Opponent: Rating{R: 2400, RD: 30}, Score: 1}}, Tau)
	if math.IsNaN(r.R) || math.IsNaN(r.Vol) || r.R <= p.R {
		t.Fatalf("bad upset result %+v", r)
	}
}

func TestVirtualOpponent(t *testing.T) {
	v := VirtualOpponent([]Rating{{R: 1400, RD: 30, Vol: 0.06}, {R: 1600, RD: 40, Vol: 0.06}})
	if v.R != 1500 {
		t.Errorf("mean rating = %v", v.R)
	}
	if !near(v.RD, math.Sqrt((900+1600)/2.0), 1e-9) {
		t.Errorf("RMS RD = %v", v.RD)
	}
	if VirtualOpponent(nil) != Default() {
		t.Error("empty team should be default")
	}
}

func TestSoftReset(t *testing.T) {
	tests := []struct{ in, want Rating }{
		{Rating{R: 1900, RD: 60, Vol: 0.06}, Rating{R: 1700, RD: 200, Vol: 0.06}},
		{Rating{R: 1300, RD: 250, Vol: 0.05}, Rating{R: 1400, RD: 250, Vol: 0.05}},
		{Default(), Rating{R: 1500, RD: 350, Vol: 0.06}},
	}
	for _, tc := range tests {
		if got := SoftReset(tc.in); got != tc.want {
			t.Errorf("SoftReset(%+v) = %+v, want %+v", tc.in, got, tc.want)
		}
	}
}
