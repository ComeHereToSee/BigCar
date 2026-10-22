// Package rating implements the Glicko-2 rating system.
//
// Reference: Mark E. Glickman, "Example of the Glicko-2 system" (2013),
// http://www.glicko.net/glicko/glicko2.pdf. Step numbers in the comments refer
// to that paper. Each ranked match is one rating period for its players.
package rating

import "math"

// Glicko-2 constants used by BigCar (docs/BACKEND_API.md).
const (
	DefaultRating     = 1500.0
	DefaultRD         = 350.0
	DefaultVolatility = 0.06
	// Tau constrains the change in volatility over time.
	Tau = 0.5

	// scale converts between the Glicko and Glicko-2 scales (step 2).
	scale = 173.7178
	// epsilon is the convergence tolerance of the volatility iteration (step 5).
	epsilon = 0.000001
	// MinRD keeps a long-time player's deviation from collapsing to zero.
	MinRD = 30.0
)

// Rating is a player's Glicko rating on the original (1500-centred) scale.
type Rating struct {
	R   float64 // rating
	RD  float64 // rating deviation
	Vol float64 // volatility σ
}

// Default returns the initial rating 1500 / 350 / 0.06.
func Default() Rating { return Rating{R: DefaultRating, RD: DefaultRD, Vol: DefaultVolatility} }

// Result is one game in a rating period: the opponent's pre-period rating and
// the score (1 win, 0.5 draw, 0 loss).
type Result struct {
	Opponent Rating
	Score    float64
}

func g(phi float64) float64 {
	return 1 / math.Sqrt(1+3*phi*phi/(math.Pi*math.Pi))
}

func expected(mu, muJ, phiJ float64) float64 {
	return 1 / (1 + math.Exp(-g(phiJ)*(mu-muJ)))
}

// Update returns the player's rating after one rating period with the given
// results, using system constant tau. With no results only the RD grows
// (step 6 for a player who did not compete).
func Update(p Rating, results []Result, tau float64) Rating {
	// Step 2: convert to the Glicko-2 scale.
	mu := (p.R - DefaultRating) / scale
	phi := p.RD / scale
	sigma := p.Vol

	if len(results) == 0 {
		phiStar := math.Sqrt(phi*phi + sigma*sigma)
		return Rating{R: p.R, RD: math.Min(phiStar*scale, DefaultRD), Vol: sigma}
	}

	// Step 3: estimated variance v; step 4: estimated improvement Δ.
	var vInv, deltaSum float64
	for _, res := range results {
		muJ := (res.Opponent.R - DefaultRating) / scale
		phiJ := res.Opponent.RD / scale
		gj := g(phiJ)
		e := expected(mu, muJ, phiJ)
		vInv += gj * gj * e * (1 - e)
		deltaSum += gj * (res.Score - e)
	}
	v := 1 / vInv
	delta := v * deltaSum

	// Step 5: new volatility σ' by the Illinois algorithm.
	sigmaNew := newVolatility(phi, v, delta, sigma, tau)

	// Step 6: pre-rating-period value φ*.
	phiStar := math.Sqrt(phi*phi + sigmaNew*sigmaNew)

	// Step 7: new φ' and μ'.
	phiNew := 1 / math.Sqrt(1/(phiStar*phiStar)+1/v)
	muNew := mu + phiNew*phiNew*deltaSum

	// Step 8: back to the Glicko scale.
	rd := phiNew * scale
	rd = math.Max(MinRD, math.Min(rd, DefaultRD))
	return Rating{R: muNew*scale + DefaultRating, RD: rd, Vol: sigmaNew}
}

// newVolatility implements step 5 of the paper.
func newVolatility(phi, v, delta, sigma, tau float64) float64 {
	a := math.Log(sigma * sigma)
	delta2 := delta * delta
	phi2 := phi * phi
	f := func(x float64) float64 {
		ex := math.Exp(x)
		num := ex * (delta2 - phi2 - v - ex)
		den := 2 * (phi2 + v + ex) * (phi2 + v + ex)
		return num/den - (x-a)/(tau*tau)
	}

	// Step 5.2: initial bracket A, B.
	A := a
	var B float64
	if delta2 > phi2+v {
		B = math.Log(delta2 - phi2 - v)
	} else {
		k := 1.0
		for f(a-k*tau) < 0 {
			k++
		}
		B = a - k*tau
	}

	// Steps 5.3-5.4: Illinois iteration.
	fA, fB := f(A), f(B)
	for i := 0; math.Abs(B-A) > epsilon && i < 100; i++ {
		C := A + (A-B)*fA/(fB-fA)
		fC := f(C)
		if fC*fB <= 0 {
			A, fA = B, fB
		} else {
			fA /= 2
		}
		B, fB = C, fC
	}
	// Step 5.5.
	return math.Exp(A / 2)
}

// VirtualOpponent combines an enemy team into a single opponent for team
// games: the mean of their ratings and the root mean square of their RDs.
func VirtualOpponent(enemies []Rating) Rating {
	if len(enemies) == 0 {
		return Default()
	}
	var sumR, sumRD2, sumVol float64
	for _, e := range enemies {
		sumR += e.R
		sumRD2 += e.RD * e.RD
		sumVol += e.Vol
	}
	n := float64(len(enemies))
	return Rating{R: sumR / n, RD: math.Sqrt(sumRD2 / n), Vol: sumVol / n}
}

// SoftReset applies the season reset: r = 1500 + (r - 1500) * 0.5, RD = max(RD, 200).
func SoftReset(r Rating) Rating {
	return Rating{R: DefaultRating + (r.R-DefaultRating)*0.5, RD: math.Max(r.RD, 200), Vol: r.Vol}
}
