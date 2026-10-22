package profile

import (
	"math"

	"github.com/comeheretosee/bigcar/backend/internal/apperr"
	"github.com/comeheretosee/bigcar/backend/internal/data"
)

// Pay deducts a price from the wallet or fails with insufficient_funds.
func (p *Profile) Pay(price data.Price) error {
	if price.Amount < 0 {
		return apperr.New(apperr.Internal, "negative price")
	}
	switch price.Currency {
	case data.Credits:
		if p.Wallet.Credits < price.Amount {
			return apperr.New(apperr.InsufficientFunds, "not enough credits (need %d, have %d)", price.Amount, p.Wallet.Credits)
		}
		p.Wallet.Credits -= price.Amount
	case data.Gold:
		if p.Wallet.Gold < price.Amount {
			return apperr.New(apperr.InsufficientFunds, "not enough gold (need %d, have %d)", price.Amount, p.Wallet.Gold)
		}
		p.Wallet.Gold -= price.Amount
	default:
		return apperr.New(apperr.Internal, "price has no currency")
	}
	return nil
}

// Credit adds currency to the wallet, saturating instead of overflowing.
func (p *Profile) Credit(c data.Currency, amount int64) {
	if amount <= 0 {
		return
	}
	add := func(v *int64) {
		if *v > math.MaxInt64-amount {
			*v = math.MaxInt64
			return
		}
		*v += amount
	}
	switch c {
	case data.Credits:
		add(&p.Wallet.Credits)
	case data.Gold:
		add(&p.Wallet.Gold)
	}
}

// DuplicateValue is what an already-owned item converts into: 50% of its base
// price (rounded down) in the base price's currency. Nothing for free items.
func DuplicateValue(gd *data.GameData, it data.Item) (data.Item, bool) {
	base, ok := gd.ItemBasePrice(it)
	if !ok || base.Amount/2 <= 0 {
		return data.Item{}, false
	}
	return data.Item{Type: data.ItemCurrency, ID: string(base.Currency), Amount: base.Amount / 2}, true
}

// Grant gives one item to the player and returns what was actually granted:
// the item itself, or — when it is already owned — its duplicate conversion
// (50% of the base price as currency). Unknown items grant nothing.
func Grant(p *Profile, gd *data.GameData, it data.Item) []data.Item {
	if it.Type == data.ItemCurrency {
		if it.Amount <= 0 || (it.ID != string(data.Credits) && it.ID != string(data.Gold)) {
			return nil
		}
		p.Credit(data.Currency(it.ID), it.Amount)
		return []data.Item{it}
	}
	if !gd.ItemExists(it) {
		return nil
	}
	if p.Owns(gd, it) {
		if conv, ok := DuplicateValue(gd, it); ok {
			p.Credit(data.Currency(conv.ID), conv.Amount)
			return []data.Item{conv}
		}
		return nil
	}
	switch {
	case it.Type == data.ItemVehicle:
		p.addVehicle(it.ID)
	case it.Type == data.ItemEquipment:
		p.Inventory.Equipment = append(p.Inventory.Equipment, it.ID)
	case data.IsCosmeticType(it.Type):
		p.Inventory.Cosmetics = append(p.Inventory.Cosmetics, it.ID)
	}
	return []data.Item{{Type: it.Type, ID: it.ID}}
}

// GrantAll grants items in order and returns everything granted.
func GrantAll(p *Profile, gd *data.GameData, items []data.Item) []data.Item {
	out := []data.Item{}
	for _, it := range items {
		out = append(out, Grant(p, gd, it)...)
	}
	return out
}

// AllOwned reports whether every item in the list is an ownable item the
// player already owns (an offer like that is rejected with already_owned).
// Lists containing currency are never "all owned".
func AllOwned(p *Profile, gd *data.GameData, items []data.Item) bool {
	if len(items) == 0 {
		return false
	}
	for _, it := range items {
		if it.Type == data.ItemCurrency || !p.Owns(gd, it) {
			return false
		}
	}
	return true
}

// AddXP adds player XP and processes level-ups. Profile.xp is the progress
// within the current level: reaching level n+1 consumes XPToNext(n) and
// grants progression.json levelRewards for the new level. At maxLevel the
// progress stays at 0. Returns the granted level rewards.
func AddXP(p *Profile, gd *data.GameData, xp int64) []data.Item {
	granted := []data.Item{}
	maxLevel := gd.Progression.Player.MaxLevel
	if xp > 0 && p.Level < maxLevel {
		p.XP += xp
	}
	for p.Level < maxLevel {
		need := gd.XPToNext(p.Level)
		if p.XP < need {
			break
		}
		p.XP -= need
		p.Level++
		granted = append(granted, GrantAll(p, gd, gd.LevelRewardsFor(p.Level))...)
	}
	if p.Level >= maxLevel {
		p.Level = maxLevel
		p.XP = 0
	}
	return granted
}

// AddSeasonXP adds season pass XP while the season is running.
func AddSeasonXP(p *Profile, sp *data.SeasonPass, xp int64, active bool) {
	if !active || xp <= 0 || p.SeasonPass.Season != sp.ID {
		return
	}
	p.SeasonPass.XP += xp
}

// MatchStats is one player's sanitized match line.
type MatchStats struct {
	Won         bool
	Takedowns   int64
	Wrecked     int64
	DamageDealt int64
	Impacts     map[string]int64
}

// RecordMatch updates lifetime stats.
func (p *Profile) RecordMatch(s MatchStats) {
	p.Stats.Matches++
	if s.Won {
		p.Stats.Wins++
	}
	p.Stats.Takedowns += s.Takedowns
	p.Stats.Wrecked += s.Wrecked
	p.Stats.DamageDealt += s.DamageDealt
	if p.Stats.Impacts == nil {
		p.Stats.Impacts = map[string]int64{}
	}
	for k, n := range s.Impacts {
		p.Stats.Impacts[k] += n
	}
}
