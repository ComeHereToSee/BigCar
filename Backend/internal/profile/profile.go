// Package profile implements the online player profile document (shared JSON
// layout with the game's local save, see docs/BACKEND_API.md "Profile
// document") and all pure domain operations on it: garage, wallet, item
// grants, XP/levels, stats and ranked state.
//
// Functions in this package never touch storage; callers load a profile
// inside a storage transaction, mutate it here and save it.
package profile

import (
	"encoding/json"
	"fmt"
	"strings"
	"time"

	"github.com/comeheretosee/bigcar/backend/internal/data"
	"github.com/comeheretosee/bigcar/backend/internal/rating"
)

// Account defaults from docs/BACKEND_API.md.
const (
	SchemaVersion    = 1
	StartingCredits  = 25000
	StartingGold     = 0
	PlacementMatches = 5
)

// Profile is the profile document. JSON field names are part of the API
// contract and must not change.
type Profile struct {
	Version     int               `json:"version"`
	PlayerID    string            `json:"playerId"`
	DisplayName string            `json:"displayName"`
	Level       int               `json:"level"`
	XP          int64             `json:"xp"`
	Wallet      Wallet            `json:"wallet"`
	Garage      Garage            `json:"garage"`
	Inventory   Inventory         `json:"inventory"`
	Purchases   map[string]int    `json:"purchases"`
	SeasonPass  SeasonPass        `json:"seasonPass"`
	Ranked      map[string]Ranked `json:"ranked"`
	Stats       Stats             `json:"stats"`
}

// Wallet holds both currencies.
type Wallet struct {
	Credits int64 `json:"credits"`
	Gold    int64 `json:"gold"`
}

// Garage lists the owned vehicles.
type Garage struct {
	Selected string         `json:"selected"`
	Vehicles []VehicleState `json:"vehicles"`
}

// VehicleState is one owned vehicle. Missing upgrade keys mean level 0,
// missing equipment slots are empty, and an empty paint/wheels means the
// factory default.
type VehicleState struct {
	VehicleID string            `json:"vehicleId"`
	Upgrades  map[string]int    `json:"upgrades"`
	Equipment map[string]string `json:"equipment"`
	Paint     string            `json:"paint,omitempty"`
	Wheels    string            `json:"wheels,omitempty"`
}

// Inventory lists owned equipment and cosmetics (all cosmetic kinds share
// one list). Free items are owned implicitly and are not listed.
type Inventory struct {
	Equipment []string `json:"equipment"`
	Cosmetics []string `json:"cosmetics"`
}

// SeasonPass is the player's progress in the current season pass. Claimed
// tiers are 1-based.
type SeasonPass struct {
	Season         string `json:"season"`
	XP             int64  `json:"xp"`
	Premium        bool   `json:"premium"`
	ClaimedFree    []int  `json:"claimedFree"`
	ClaimedPremium []int  `json:"claimedPremium"`
}

// Ranked is the per-queue Glicko-2 state.
type Ranked struct {
	Rating         float64 `json:"rating"`
	RD             float64 `json:"rd"`
	Vol            float64 `json:"vol"`
	Wins           int     `json:"wins"`
	Losses         int     `json:"losses"`
	Tier           string  `json:"tier"`
	Division       int     `json:"division"`
	PlacementsLeft int     `json:"placementsLeft"`
}

// Glicko returns the rating triple.
func (r Ranked) Glicko() rating.Rating { return rating.Rating{R: r.Rating, RD: r.RD, Vol: r.Vol} }

// Stats are lifetime online match statistics.
type Stats struct {
	Matches     int64            `json:"matches"`
	Wins        int64            `json:"wins"`
	Takedowns   int64            `json:"takedowns"`
	Wrecked     int64            `json:"wrecked"`
	DamageDealt int64            `json:"damageDealt"`
	Impacts     map[string]int64 `json:"impacts"`
}

// New creates the profile of a new account: 25 000 credits, every starter
// vehicle, the first starter selected.
func New(playerID, displayName string, gd *data.GameData) *Profile {
	p := &Profile{
		Version:     SchemaVersion,
		PlayerID:    playerID,
		DisplayName: displayName,
		Level:       1,
		Wallet:      Wallet{Credits: StartingCredits, Gold: StartingGold},
		Inventory:   Inventory{Equipment: []string{}, Cosmetics: []string{}},
		Purchases:   map[string]int{},
		SeasonPass:  SeasonPass{Season: gd.Store.SeasonPass.ID, ClaimedFree: []int{}, ClaimedPremium: []int{}},
		Ranked:      map[string]Ranked{},
		Stats:       Stats{Impacts: map[string]int64{}},
		Garage:      Garage{Vehicles: []VehicleState{}},
	}
	for _, id := range gd.StarterVehicles() {
		p.addVehicle(id)
	}
	if len(p.Garage.Vehicles) > 0 {
		p.Garage.Selected = p.Garage.Vehicles[0].VehicleID
	}
	return p
}

// DefaultDisplayName derives "Driver5c1d" from "p_5c1d0a2e9b7f".
func DefaultDisplayName(playerID string) string {
	suffix := strings.TrimPrefix(playerID, "p_")
	if len(suffix) > 4 {
		suffix = suffix[:4]
	}
	return "Driver" + suffix
}

// Clone returns a deep copy.
func (p *Profile) Clone() *Profile {
	b, err := json.Marshal(p)
	if err != nil {
		panic(fmt.Sprintf("profile: marshal: %v", err))
	}
	var c Profile
	if err := json.Unmarshal(b, &c); err != nil {
		panic(fmt.Sprintf("profile: unmarshal: %v", err))
	}
	c.fillNil()
	return &c
}

// fillNil replaces nil collections so the JSON never contains null lists/maps.
func (p *Profile) fillNil() {
	if p.Purchases == nil {
		p.Purchases = map[string]int{}
	}
	if p.Ranked == nil {
		p.Ranked = map[string]Ranked{}
	}
	if p.Stats.Impacts == nil {
		p.Stats.Impacts = map[string]int64{}
	}
	if p.Inventory.Equipment == nil {
		p.Inventory.Equipment = []string{}
	}
	if p.Inventory.Cosmetics == nil {
		p.Inventory.Cosmetics = []string{}
	}
	if p.Garage.Vehicles == nil {
		p.Garage.Vehicles = []VehicleState{}
	}
	if p.SeasonPass.ClaimedFree == nil {
		p.SeasonPass.ClaimedFree = []int{}
	}
	if p.SeasonPass.ClaimedPremium == nil {
		p.SeasonPass.ClaimedPremium = []int{}
	}
	for i := range p.Garage.Vehicles {
		v := &p.Garage.Vehicles[i]
		if v.Upgrades == nil {
			v.Upgrades = map[string]int{}
		}
		if v.Equipment == nil {
			v.Equipment = map[string]string{}
		}
	}
}

// DailyPurchaseKey is the purchases key recording that the daily deal in
// slot was bought on UTC day.
func DailyPurchaseKey(day int64, slot int) string { return fmt.Sprintf("daily_%d_%d", day, slot) }

// Normalize brings a stored profile up to date with the current game data and
// time. It reports whether anything semantically changed (and the profile
// therefore needs to be saved):
//
//   - season rollover: a new season pass id resets the pass progress and
//     soft-resets every ranked rating (r = 1500 + (r-1500)/2, RD = max(RD, 200));
//     wins/losses restart and 5 placement matches are required again;
//   - daily-deal purchase markers from previous UTC days are pruned;
//   - starter vehicles added to the data after the account was created are
//     granted;
//   - an invalid garage selection is repaired.
func Normalize(p *Profile, gd *data.GameData, now time.Time) bool {
	p.fillNil()
	changed := false
	if p.Version == 0 {
		p.Version = SchemaVersion
		changed = true
	}
	if p.Level < 1 {
		p.Level = 1
		changed = true
	}
	if season := gd.Store.SeasonPass.ID; p.SeasonPass.Season != season {
		p.SeasonPass = SeasonPass{Season: season, ClaimedFree: []int{}, ClaimedPremium: []int{}}
		for q, r := range p.Ranked {
			reset := rating.SoftReset(r.Glicko())
			tier, div := gd.PlacementRank()
			p.Ranked[q] = Ranked{
				Rating: reset.R, RD: reset.RD, Vol: reset.Vol,
				Tier: tier, Division: div, PlacementsLeft: PlacementMatches,
			}
		}
		changed = true
	}
	today := DayNumber(now)
	prefix := fmt.Sprintf("daily_%d_", today)
	for k := range p.Purchases {
		if strings.HasPrefix(k, "daily_") && !strings.HasPrefix(k, prefix) {
			delete(p.Purchases, k)
			changed = true
		}
	}
	for _, id := range gd.StarterVehicles() {
		if p.Vehicle(id) == nil {
			p.addVehicle(id)
			changed = true
		}
	}
	if _, ok := gd.Vehicle(p.Garage.Selected); !ok || p.Vehicle(p.Garage.Selected) == nil {
		for _, v := range p.Garage.Vehicles {
			if _, ok := gd.Vehicle(v.VehicleID); ok {
				p.Garage.Selected = v.VehicleID
				changed = true
				break
			}
		}
	}
	return changed
}

// DayNumber is the UTC day number floor(unixSeconds / 86400).
func DayNumber(t time.Time) int64 {
	s := t.Unix()
	d := s / 86400
	if s < 0 && s%86400 != 0 {
		d--
	}
	return d
}

// Vehicle returns the owned vehicle state or nil.
func (p *Profile) Vehicle(id string) *VehicleState {
	for i := range p.Garage.Vehicles {
		if p.Garage.Vehicles[i].VehicleID == id {
			return &p.Garage.Vehicles[i]
		}
	}
	return nil
}

func (p *Profile) addVehicle(id string) {
	p.Garage.Vehicles = append(p.Garage.Vehicles, VehicleState{
		VehicleID: id, Upgrades: map[string]int{}, Equipment: map[string]string{},
	})
}

func containsStr(list []string, s string) bool {
	for _, x := range list {
		if x == s {
			return true
		}
	}
	return false
}

// OwnsEquipment reports whether the player owns an equipment item (free items
// are owned by everyone).
func (p *Profile) OwnsEquipment(gd *data.GameData, id string) bool {
	if e, ok := gd.EquipmentItem(id); ok && e.Price.IsFree() {
		return true
	}
	return containsStr(p.Inventory.Equipment, id)
}

// OwnsCosmetic reports whether the player owns a cosmetic (free items are
// owned by everyone).
func (p *Profile) OwnsCosmetic(gd *data.GameData, id string) bool {
	if c, ok := gd.Cosmetic(id); ok && c.Price.IsFree() {
		return true
	}
	return containsStr(p.Inventory.Cosmetics, id)
}

// Owns reports whether the player owns a non-currency item. Currency is never
// "owned".
func (p *Profile) Owns(gd *data.GameData, it data.Item) bool {
	switch {
	case it.Type == data.ItemVehicle:
		return p.Vehicle(it.ID) != nil
	case it.Type == data.ItemEquipment:
		return p.OwnsEquipment(gd, it.ID)
	case data.IsCosmeticType(it.Type):
		return p.OwnsCosmetic(gd, it.ID)
	}
	return false
}

// RankedState returns the queue's ranked state, or the initial state
// (1500 / 350 / 0.06, 5 placements) when the player never played it.
func (p *Profile) RankedState(gd *data.GameData, queue string) Ranked {
	if r, ok := p.Ranked[queue]; ok {
		return r
	}
	return NewRanked(gd)
}

// NewRanked is the initial ranked state of a queue.
func NewRanked(gd *data.GameData) Ranked {
	d := rating.Default()
	tier, div := gd.PlacementRank()
	return Ranked{Rating: d.R, RD: d.RD, Vol: d.Vol, Tier: tier, Division: div, PlacementsLeft: PlacementMatches}
}

// HiddenMMR is the casual matchmaking rating: the average of the player's
// ranked queue ratings, or 1500 when the player has none.
func (p *Profile) HiddenMMR() float64 {
	if len(p.Ranked) == 0 {
		return rating.DefaultRating
	}
	var sum float64
	for _, r := range p.Ranked {
		sum += r.Rating
	}
	return sum / float64(len(p.Ranked))
}

// Loadout is the build a player brings into an online match.
type Loadout struct {
	VehicleID string            `json:"vehicleId"`
	Upgrades  map[string]int    `json:"upgrades"`
	Equipment map[string]string `json:"equipment"`
	Paint     string            `json:"paint,omitempty"`
	Wheels    string            `json:"wheels,omitempty"`
}

// Loadout returns a copy of the selected vehicle's build.
func (p *Profile) Loadout() Loadout {
	v := p.Vehicle(p.Garage.Selected)
	if v == nil {
		return Loadout{VehicleID: p.Garage.Selected, Upgrades: map[string]int{}, Equipment: map[string]string{}}
	}
	l := Loadout{
		VehicleID: v.VehicleID, Upgrades: map[string]int{}, Equipment: map[string]string{},
		Paint: v.Paint, Wheels: v.Wheels,
	}
	for k, n := range v.Upgrades {
		l.Upgrades[k] = n
	}
	for k, id := range v.Equipment {
		l.Equipment[k] = id
	}
	return l
}
