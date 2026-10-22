package data

import (
	"errors"
	"fmt"
	"regexp"
)

var idPattern = regexp.MustCompile(`^[a-z0-9_]+$`)

// VehicleClasses is the vehicle class enum.
var VehicleClasses = []string{
	"compact", "hatchback", "sedan", "wagon", "coupe", "muscle", "sports", "supercar",
	"hypercar", "luxury", "suv", "pickup", "offroad", "van", "truck",
}

// UpgradeCategoryIDs are the nine required upgrade categories.
var UpgradeCategoryIDs = []string{
	"engine", "transmission", "suspension", "tires", "brakes", "chassis", "armor", "weight", "nitro",
}

// EquipmentSlots are the build slots.
var EquipmentSlots = []string{"front", "rear", "side", "roof", "wheels", "core", "ability"}

// Abilities are the valid equipment abilities.
var Abilities = []string{"nitro_burst", "hydraulic_jump", "ram_charge", "brace", "field_repair", "shock_bumper"}

// RequiredModes are the mode ids that must exist.
var RequiredModes = []string{
	"derby", "takedown", "team_takedown", "team_elimination", "ring_out", "king_of_hill",
	"bounty", "survival", "destruction", "boss", "explore",
}

// RequiredAIProfiles are the ai profile ids that must exist.
var RequiredAIProfiles = []string{"rookie", "regular", "veteran", "elite", "boss"}

// RankIDs are the ranked tiers in ascending order.
var RankIDs = []string{"bronze", "silver", "gold", "platinum", "diamond", "master", "crash_king"}

// StoreSections are the offer sections.
var StoreSections = []string{"featured", "vehicles", "equipment", "cosmetics", "bundles"}

var winConditions = []string{"last_standing", "score", "takedowns", "hill_time", "survive", "objective"}

func contains(list []string, s string) bool {
	for _, x := range list {
		if x == s {
			return true
		}
	}
	return false
}

// validator collects errors; it caps the number of messages to keep startup
// logs readable for badly broken data.
type validator struct {
	errs []error
}

func (v *validator) add(format string, args ...any) {
	v.errs = append(v.errs, fmt.Errorf(format, args...))
}

func (v *validator) err() error {
	const maxErrs = 50
	if len(v.errs) > maxErrs {
		extra := len(v.errs) - maxErrs
		v.errs = append(v.errs[:maxErrs], fmt.Errorf("... and %d more", extra))
	}
	return errors.Join(v.errs...)
}

func (v *validator) id(where, id string) {
	if !idPattern.MatchString(id) {
		v.add("%s: id %q must match ^[a-z0-9_]+$", where, id)
	}
}

func (v *validator) price(where string, p Price, allowFree bool) {
	switch {
	case p.IsZero():
		v.add("%s: price missing", where)
	case p.Amount < 0:
		v.add("%s: price must not be negative", where)
	case p.Amount == 0 && !allowFree:
		v.add("%s: price must be positive", where)
	case p.Amount == 0 && p.Currency != Credits:
		v.add("%s: a free price must be {\"credits\": 0}", where)
	}
}

func (gd *GameData) validateItem(v *validator, where string, it Item) {
	switch {
	case it.Type == ItemCurrency:
		if it.ID != string(Credits) && it.ID != string(Gold) {
			v.add("%s: currency id must be credits or gold, got %q", where, it.ID)
		}
		if it.Amount <= 0 {
			v.add("%s: currency amount must be positive", where)
		}
	case it.Type == ItemVehicle || it.Type == ItemEquipment || IsCosmeticType(it.Type):
		if !gd.ItemExists(it) {
			v.add("%s: unknown %s %q", where, it.Type, it.ID)
		}
		if it.Amount != 0 && it.Amount != 1 {
			v.add("%s: amount is only allowed for currency items", where)
		}
	default:
		v.add("%s: unknown item type %q", where, it.Type)
	}
}

func (gd *GameData) validate() error {
	v := &validator{}
	gd.validateVehicles(v)
	gd.validateUpgrades(v)
	gd.validateEquipment(v)
	gd.validateCosmetics(v)
	gd.validateModes(v)
	gd.validateArenas(v)
	gd.validateAI(v)
	gd.validateProgression(v)
	gd.validateStore(v)
	return v.err()
}

func (gd *GameData) validateVehicles(v *validator) {
	brands := map[string]bool{}
	for _, b := range gd.Brands {
		brands[b.ID] = true
	}
	seen := map[string]bool{}
	starters := 0
	if len(gd.Vehicles) == 0 {
		v.add("vehicles.json: no vehicles")
	}
	for i, veh := range gd.Vehicles {
		where := fmt.Sprintf("vehicles.json: vehicles[%d] (%s)", i, veh.ID)
		v.id(where, veh.ID)
		if seen[veh.ID] {
			v.add("%s: duplicate id", where)
		}
		seen[veh.ID] = true
		if !contains(VehicleClasses, veh.Class) {
			v.add("%s: unknown class %q", where, veh.Class)
		}
		if veh.Tier < 1 || veh.Tier > 5 {
			v.add("%s: tier must be 1-5", where)
		}
		if veh.UnlockLevel < 1 {
			v.add("%s: unlockLevel must be >= 1", where)
		}
		v.price(where, veh.Price, veh.Starter)
		if veh.Starter {
			starters++
			if !veh.Price.IsFree() {
				v.add("%s: starter vehicles must cost {\"credits\": 0}", where)
			}
		}
		if len(gd.Brands) > 0 && !brands[veh.Brand] {
			v.add("%s: unknown brand %q", where, veh.Brand)
		}
	}
	if starters == 0 {
		v.add("vehicles.json: at least one vehicle must have \"starter\": true")
	}
}

func (gd *GameData) validateUpgrades(v *validator) {
	u := gd.Upgrades
	if len(u.TierCostMultiplier) != 5 {
		v.add("upgrades.json: tierCostMultiplier must have 5 entries (one per tier)")
	}
	for i, m := range u.TierCostMultiplier {
		if m <= 0 {
			v.add("upgrades.json: tierCostMultiplier[%d] must be positive", i)
		}
	}
	seen := map[string]bool{}
	for i, c := range u.Categories {
		where := fmt.Sprintf("upgrades.json: categories[%d] (%s)", i, c.ID)
		if !contains(UpgradeCategoryIDs, c.ID) {
			v.add("%s: unknown category id", where)
		}
		if seen[c.ID] {
			v.add("%s: duplicate id", where)
		}
		seen[c.ID] = true
		if len(c.Levels) != 5 {
			v.add("%s: must have exactly 5 levels, has %d", where, len(c.Levels))
		}
		for j, l := range c.Levels {
			if l.Cost <= 0 {
				v.add("%s: levels[%d].cost must be positive", where, j)
			}
		}
	}
	for _, id := range UpgradeCategoryIDs {
		if !seen[id] {
			v.add("upgrades.json: missing category %q", id)
		}
	}
}

func (gd *GameData) validateEquipment(v *validator) {
	seen := map[string]bool{}
	for i, e := range gd.Equipment {
		where := fmt.Sprintf("equipment.json: equipment[%d] (%s)", i, e.ID)
		v.id(where, e.ID)
		if seen[e.ID] {
			v.add("%s: duplicate id", where)
		}
		seen[e.ID] = true
		if !contains(EquipmentSlots, e.Slot) {
			v.add("%s: unknown slot %q", where, e.Slot)
		}
		if e.Ability != nil {
			if !contains(Abilities, *e.Ability) {
				v.add("%s: unknown ability %q", where, *e.Ability)
			}
			if e.Slot != "ability" {
				v.add("%s: only items in slot \"ability\" may set an ability", where)
			}
		}
		if e.CR < 0 {
			v.add("%s: cr must be >= 0", where)
		}
		if e.UnlockLevel < 1 {
			v.add("%s: unlockLevel must be >= 1", where)
		}
		v.price(where, e.Price, true)
		for _, c := range e.AllowedClasses {
			if !contains(VehicleClasses, c) {
				v.add("%s: allowedClasses contains unknown class %q", where, c)
			}
		}
	}
}

func (gd *GameData) validateCosmetics(v *validator) {
	// inventory.cosmetics is one flat list, so ids must be unique across kinds.
	seen := map[string]string{}
	for _, list := range gd.cosmeticLists() {
		for i, c := range *list.items {
			where := fmt.Sprintf("cosmetics.json: %s[%d] (%s)", list.file, i, c.ID)
			v.id(where, c.ID)
			if other, dup := seen[c.ID]; dup {
				v.add("%s: id already used in %s", where, other)
			}
			seen[c.ID] = list.file
			v.price(where, c.Price, true)
		}
	}
}

func (gd *GameData) validateModes(v *validator) {
	seen := map[string]bool{}
	for i, m := range gd.Modes {
		where := fmt.Sprintf("modes.json: modes[%d] (%s)", i, m.ID)
		v.id(where, m.ID)
		if seen[m.ID] {
			v.add("%s: duplicate id", where)
		}
		seen[m.ID] = true
		if m.Teams != 0 && m.Teams != 2 && m.Teams != 3 {
			v.add("%s: teams must be 0, 2 or 3", where)
		}
		if m.Win != "" && !contains(winConditions, m.Win) {
			v.add("%s: unknown win condition %q", where, m.Win)
		}
	}
	for _, id := range RequiredModes {
		if !seen[id] {
			v.add("modes.json: missing required mode %q", id)
		}
	}
}

func (gd *GameData) validateArenas(v *validator) {
	seen := map[string]bool{}
	playable := 0
	for i, a := range gd.Arenas {
		where := fmt.Sprintf("arenas.json: arenas[%d] (%s)", i, a.ID)
		v.id(where, a.ID)
		if seen[a.ID] {
			v.add("%s: duplicate id", where)
		}
		seen[a.ID] = true
		if a.Hub {
			continue
		}
		playable++
		if len(a.Spawns) < 12 {
			v.add("%s: needs at least 12 spawns, has %d", where, len(a.Spawns))
		}
		ts := a.TeamSpawns()
		if len(ts) > 0 && (ts[0] < 3 || ts[1] < 3) {
			v.add("%s: team spawns need at least 3 entries for team 0 and team 1", where)
		}
		for t := range ts {
			if t != 0 && t != 1 {
				v.add("%s: spawn team must be 0 or 1, got %d", where, t)
			}
		}
	}
	if playable == 0 {
		v.add("arenas.json: no non-hub arenas")
	}
}

func (gd *GameData) validateAI(v *validator) {
	for _, id := range RequiredAIProfiles {
		if !gd.HasAIProfile(id) {
			v.add("ai.json: missing required profile %q", id)
		}
	}
}

func (gd *GameData) validateProgression(v *validator) {
	p := gd.Progression
	if p.Player.MaxLevel < 1 {
		v.add("progression.json: player.maxLevel must be >= 1")
	}
	if p.Player.XPBase <= 0 {
		v.add("progression.json: player.xpBase must be positive")
	}
	if p.Player.XPGrowth < 1 {
		v.add("progression.json: player.xpGrowth must be >= 1")
	}
	for i, lr := range p.LevelRewards {
		where := fmt.Sprintf("progression.json: levelRewards[%d]", i)
		if lr.Level < 2 || lr.Level > p.Player.MaxLevel {
			v.add("%s: level must be within 2..maxLevel", where)
		}
		for j, it := range lr.Items {
			gd.validateItem(v, fmt.Sprintf("%s.items[%d]", where, j), it)
		}
	}
	m := p.Match
	for name, val := range map[string]float64{
		"participationCredits": m.ParticipationCredits, "participationXp": m.ParticipationXP,
		"creditsPerScore": m.CreditsPerScore, "xpPerScore": m.XPPerScore,
	} {
		if val < 0 {
			v.add("progression.json: match.%s must be >= 0", name)
		}
	}
	for name, val := range map[string]float64{
		"winMultiplier": m.WinMultiplier, "rankedMultiplier": m.RankedMultiplier,
		"maxCredits": m.MaxCredits, "maxXp": m.MaxXP,
	} {
		if val <= 0 {
			v.add("progression.json: match.%s must be positive", name)
		}
	}
	if len(p.Ranks) != len(RankIDs) {
		v.add("progression.json: ranks must be exactly %v", RankIDs)
		return
	}
	for i, r := range p.Ranks {
		if r.ID != RankIDs[i] {
			v.add("progression.json: ranks[%d] must be %q (sorted by minRating), got %q", i, RankIDs[i], r.ID)
		}
		if r.Divisions < 1 {
			v.add("progression.json: ranks[%d] (%s): divisions must be >= 1", i, r.ID)
		}
		if i > 0 && r.MinRating <= p.Ranks[i-1].MinRating {
			v.add("progression.json: ranks must be sorted by strictly increasing minRating (%s)", r.ID)
		}
	}
}

func (gd *GameData) validateStore(v *validator) {
	s := gd.Store
	seen := map[string]bool{}
	for i, o := range s.Offers {
		where := fmt.Sprintf("store.json: offers[%d] (%s)", i, o.ID)
		v.id(where, o.ID)
		if seen[o.ID] {
			v.add("%s: duplicate id", where)
		}
		seen[o.ID] = true
		if !contains(StoreSections, o.Section) {
			v.add("%s: unknown section %q", where, o.Section)
		}
		if len(o.Items) == 0 {
			v.add("%s: no items", where)
		}
		for j, it := range o.Items {
			gd.validateItem(v, fmt.Sprintf("%s.items[%d]", where, j), it)
		}
		v.price(where, o.Price, false)
		if o.Limit < 0 {
			v.add("%s: limit must be >= 0", where)
		}
		if o.Start != nil && o.End != nil && !o.Start.Before(*o.End) {
			v.add("%s: start must be before end", where)
		}
	}
	packs := map[string]bool{}
	for i, gp := range s.GoldPacks {
		where := fmt.Sprintf("store.json: goldPacks[%d] (%s)", i, gp.ID)
		v.id(where, gp.ID)
		if packs[gp.ID] {
			v.add("%s: duplicate id", where)
		}
		packs[gp.ID] = true
		if gp.Gold <= 0 {
			v.add("%s: gold must be positive", where)
		}
		if gp.Bonus < 0 {
			v.add("%s: bonus must be >= 0", where)
		}
		if len(gp.Prices) == 0 {
			v.add("%s: prices missing", where)
		}
		for cur, amt := range gp.Prices {
			if len(cur) != 3 || amt <= 0 {
				v.add("%s: price %s=%v must be a positive amount with an ISO 4217 code", where, cur, amt)
			}
		}
	}

	d := s.Daily
	if d.Slots < 1 {
		v.add("store.json: daily.slots must be >= 1")
	}
	if len(d.Discounts) == 0 {
		v.add("store.json: daily.discounts must not be empty")
	}
	for i, disc := range d.Discounts {
		if disc <= 0 || disc >= 1 {
			v.add("store.json: daily.discounts[%d] must be in (0, 1)", i)
		}
	}
	if len(d.Pool) == 0 {
		v.add("store.json: daily.pool must not be empty")
	}
	poolSeen := map[string]bool{}
	for i, it := range d.Pool {
		where := fmt.Sprintf("store.json: daily.pool[%d]", i)
		if it.Type == ItemCurrency {
			v.add("%s: currency items cannot be daily deals", where)
			continue
		}
		gd.validateItem(v, where, it)
		if poolSeen[it.String()] {
			v.add("%s: duplicate pool entry %s", where, it)
		}
		poolSeen[it.String()] = true
		if p, ok := gd.ItemBasePrice(it); ok && (p.Currency != Credits || p.Amount <= 0) {
			v.add("%s: daily deal items need a positive credits base price", where)
		}
	}

	sp := s.SeasonPass
	v.id("store.json: seasonPass", sp.ID)
	if sp.Start.IsZero() || sp.End.IsZero() || !sp.Start.Before(sp.End) {
		v.add("store.json: seasonPass start/end must be RFC 3339 times with start < end")
	}
	v.price("store.json: seasonPass.premiumPrice", sp.PremiumPrice, false)
	if sp.XPPerTier <= 0 {
		v.add("store.json: seasonPass.xpPerTier must be positive")
	}
	if len(sp.Tiers) < 50 || len(sp.Tiers) > 100 {
		v.add("store.json: seasonPass must have 50-100 tiers, has %d", len(sp.Tiers))
	}
	for i, t := range sp.Tiers {
		for j, it := range t.Free {
			gd.validateItem(v, fmt.Sprintf("store.json: seasonPass.tiers[%d].free[%d]", i, j), it)
		}
		for j, it := range t.Premium {
			gd.validateItem(v, fmt.Sprintf("store.json: seasonPass.tiers[%d].premium[%d]", i, j), it)
		}
	}
}
