// Package data loads and validates the game data JSON files described in
// docs/DATA_SCHEMA.md.
//
// Only the fields the backend actually uses are modelled; every other field is
// ignored so that the game team can extend the files without breaking the
// backend (forward compatibility). The fields that are modelled are validated
// strictly (referential integrity, ranges), because prices and rewards are
// money.
package data

import (
	"encoding/json"
	"fmt"
	"math"
	"time"
)

// Currency is a wallet currency.
type Currency string

// Currencies.
const (
	Credits Currency = "credits"
	Gold    Currency = "gold"
)

// Price is a price object with exactly one key: {"credits": n} or {"gold": n}.
type Price struct {
	Currency Currency
	Amount   int64
}

// CreditsPrice is a convenience constructor.
func CreditsPrice(n int64) Price { return Price{Currency: Credits, Amount: n} }

// GoldPrice is a convenience constructor.
func GoldPrice(n int64) Price { return Price{Currency: Gold, Amount: n} }

// IsZero reports whether the price was never set.
func (p Price) IsZero() bool { return p.Currency == "" }

// IsFree reports whether the price is {"credits": 0}: such items are owned by
// every player implicitly.
func (p Price) IsFree() bool { return p.Currency == Credits && p.Amount == 0 }

// MarshalJSON renders the single-key object.
func (p Price) MarshalJSON() ([]byte, error) {
	if p.Currency == "" {
		return []byte("null"), nil
	}
	return json.Marshal(map[string]int64{string(p.Currency): p.Amount})
}

// UnmarshalJSON parses the single-key object. Amounts must be non-negative
// integers (a float with no fractional part such as 1000.0 is accepted).
func (p *Price) UnmarshalJSON(b []byte) error {
	if string(b) == "null" {
		*p = Price{}
		return nil
	}
	var m map[string]float64
	if err := json.Unmarshal(b, &m); err != nil {
		return fmt.Errorf("price must be an object like {\"credits\": 100}: %w", err)
	}
	if len(m) != 1 {
		return fmt.Errorf("price must have exactly one key (credits or gold), got %d", len(m))
	}
	for k, v := range m {
		c := Currency(k)
		if c != Credits && c != Gold {
			return fmt.Errorf("unknown price currency %q", k)
		}
		if v < 0 || v != math.Trunc(v) || v > 1e15 {
			return fmt.Errorf("price amount must be a non-negative integer, got %v", v)
		}
		*p = Price{Currency: c, Amount: int64(v)}
	}
	return nil
}

// Item types used by store offers, rewards and grants.
const (
	ItemVehicle   = "vehicle"
	ItemEquipment = "equipment"
	ItemPaint     = "paint"
	ItemWheels    = "wheels"
	ItemSparks    = "sparks"
	ItemHorn      = "horn"
	ItemTitle     = "title"
	ItemBanner    = "banner"
	ItemCurrency  = "currency"
)

// CosmeticItemTypes lists the item types stored in inventory.cosmetics.
var CosmeticItemTypes = []string{ItemPaint, ItemWheels, ItemSparks, ItemHorn, ItemTitle, ItemBanner}

// IsCosmeticType reports whether t is one of the cosmetic item types.
func IsCosmeticType(t string) bool {
	for _, c := range CosmeticItemTypes {
		if c == t {
			return true
		}
	}
	return false
}

// Item is {"type", "id", "amount"?}. Amount is only meaningful for currency.
type Item struct {
	Type   string `json:"type"`
	ID     string `json:"id"`
	Amount int64  `json:"amount,omitempty"`
}

func (it Item) String() string {
	if it.Type == ItemCurrency {
		return fmt.Sprintf("%s/%s x%d", it.Type, it.ID, it.Amount)
	}
	return it.Type + "/" + it.ID
}

// Vehicle is the subset of vehicles.json used by the backend.
type Vehicle struct {
	ID          string   `json:"id"`
	Brand       string   `json:"brand"`
	Name        string   `json:"name"`
	Class       string   `json:"class"`
	Tier        int      `json:"tier"`
	Price       Price    `json:"price"`
	UnlockLevel int      `json:"unlockLevel"`
	Starter     bool     `json:"starter"`
	Colors      []string `json:"colors"`
}

// UpgradeLevel is one stage of an upgrade category.
type UpgradeLevel struct {
	Cost float64 `json:"cost"`
}

// UpgradeCategory is one of the nine upgrade categories.
type UpgradeCategory struct {
	ID     string         `json:"id"`
	Name   string         `json:"name"`
	Levels []UpgradeLevel `json:"levels"`
}

// Upgrades is upgrades.json.
type Upgrades struct {
	TierCostMultiplier []float64         `json:"tierCostMultiplier"`
	Categories         []UpgradeCategory `json:"categories"`
}

// Equipment is the subset of equipment.json used by the backend.
type Equipment struct {
	ID             string   `json:"id"`
	Slot           string   `json:"slot"`
	Name           string   `json:"name"`
	Rarity         string   `json:"rarity"`
	Ability        *string  `json:"ability"`
	CR             int      `json:"cr"`
	Price          Price    `json:"price"`
	UnlockLevel    int      `json:"unlockLevel"`
	AllowedClasses []string `json:"allowedClasses"`
}

// AllowsClass reports whether the equipment may be fitted to a vehicle class.
func (e *Equipment) AllowsClass(class string) bool {
	if len(e.AllowedClasses) == 0 {
		return true
	}
	for _, c := range e.AllowedClasses {
		if c == class {
			return true
		}
	}
	return false
}

// Cosmetic is one entry of any cosmetics.json list.
type Cosmetic struct {
	ID     string `json:"id"`
	Name   string `json:"name"`
	Rarity string `json:"rarity"`
	Price  Price  `json:"price"`
	// Kind is the item type (paint, wheels, ...); set by the loader.
	Kind string `json:"-"`
}

// Cosmetics is cosmetics.json.
type Cosmetics struct {
	Paints  []Cosmetic `json:"paints"`
	Wheels  []Cosmetic `json:"wheels"`
	Sparks  []Cosmetic `json:"sparks"`
	Horns   []Cosmetic `json:"horns"`
	Titles  []Cosmetic `json:"titles"`
	Banners []Cosmetic `json:"banners"`
}

// Mode is the subset of modes.json used by the backend.
type Mode struct {
	ID         string `json:"id"`
	Name       string `json:"name"`
	Teams      int    `json:"teams"`
	MinPlayers int    `json:"minPlayers"`
	MaxPlayers int    `json:"maxPlayers"`
	Win        string `json:"win"`
	RingOut    bool   `json:"ringOut"`
	Hill       bool   `json:"hill"`
	Online     bool   `json:"online"`
	Ranked     bool   `json:"ranked"`
}

// Spawn is an arena spawn point.
type Spawn struct {
	Pos  []float64 `json:"pos"`
	Yaw  float64   `json:"yaw"`
	Team *int      `json:"team"`
}

// Prop is an arena prop; only its type matters to the backend.
type Prop struct {
	Type string `json:"type"`
}

// Arena is the subset of arenas.json used by the backend (arena selection).
type Arena struct {
	ID     string  `json:"id"`
	Name   string  `json:"name"`
	Hub    bool    `json:"hub"`
	Walls  bool    `json:"walls"`
	Props  []Prop  `json:"props"`
	Spawns []Spawn `json:"spawns"`
}

// HasProp reports whether the arena contains a prop of the given type.
func (a *Arena) HasProp(t string) bool {
	for _, p := range a.Props {
		if p.Type == t {
			return true
		}
	}
	return false
}

// TeamSpawns counts spawns per team id.
func (a *Arena) TeamSpawns() map[int]int {
	out := map[int]int{}
	for _, s := range a.Spawns {
		if s.Team != nil {
			out[*s.Team]++
		}
	}
	return out
}

// AIProfile is the subset of ai.json used by the backend.
type AIProfile struct {
	ID string `json:"id"`
}

// PlayerProgression is progression.json "player".
type PlayerProgression struct {
	MaxLevel int     `json:"maxLevel"`
	XPBase   float64 `json:"xpBase"`
	XPGrowth float64 `json:"xpGrowth"`
}

// LevelReward is granted when a player reaches Level.
type LevelReward struct {
	Level int    `json:"level"`
	Items []Item `json:"items"`
}

// MatchRewards is progression.json "match".
type MatchRewards struct {
	ParticipationCredits float64 `json:"participationCredits"`
	ParticipationXP      float64 `json:"participationXp"`
	CreditsPerScore      float64 `json:"creditsPerScore"`
	XPPerScore           float64 `json:"xpPerScore"`
	WinMultiplier        float64 `json:"winMultiplier"`
	RankedMultiplier     float64 `json:"rankedMultiplier"`
	MaxCredits           float64 `json:"maxCredits"`
	MaxXP                float64 `json:"maxXp"`
}

// Rank is one ranked tier.
type Rank struct {
	ID        string  `json:"id"`
	Name      string  `json:"name"`
	MinRating float64 `json:"minRating"`
	Divisions int     `json:"divisions"`
	Color     string  `json:"color"`
}

// Progression is progression.json.
type Progression struct {
	Player       PlayerProgression `json:"player"`
	LevelRewards []LevelReward     `json:"levelRewards"`
	Match        MatchRewards      `json:"match"`
	Ranks        []Rank            `json:"ranks"`
}

// Offer is a store offer.
type Offer struct {
	ID       string     `json:"id"`
	Section  string     `json:"section"`
	Featured bool       `json:"featured"`
	Items    []Item     `json:"items"`
	Price    Price      `json:"price"`
	Limit    int        `json:"limit"`
	Start    *time.Time `json:"start"`
	End      *time.Time `json:"end"`
	Tag      string     `json:"tag"`
}

// ActiveAt reports whether the offer is on sale at t (start inclusive, end exclusive).
func (o *Offer) ActiveAt(t time.Time) bool {
	if o.Start != nil && t.Before(*o.Start) {
		return false
	}
	if o.End != nil && !t.Before(*o.End) {
		return false
	}
	return true
}

// GoldPack is a real-money gold pack.
type GoldPack struct {
	ID     string             `json:"id"`
	Gold   int64              `json:"gold"`
	Bonus  int64              `json:"bonus"`
	Prices map[string]float64 `json:"prices"`
}

// Daily is the daily deals configuration.
type Daily struct {
	Slots     int       `json:"slots"`
	Discounts []float64 `json:"discounts"`
	Pool      []Item    `json:"pool"`
}

// SeasonTier is one season pass tier.
type SeasonTier struct {
	Free    []Item `json:"free"`
	Premium []Item `json:"premium"`
}

// SeasonPass is the season pass configuration. It also defines the current
// ranked season (id, name, start, end).
type SeasonPass struct {
	ID           string       `json:"id"`
	Name         string       `json:"name"`
	Start        time.Time    `json:"start"`
	End          time.Time    `json:"end"`
	PremiumPrice Price        `json:"premiumPrice"`
	XPPerTier    int64        `json:"xpPerTier"`
	Tiers        []SeasonTier `json:"tiers"`
}

// ActiveAt reports whether the season runs at t.
func (s *SeasonPass) ActiveAt(t time.Time) bool {
	return !t.Before(s.Start) && t.Before(s.End)
}

// Store is store.json.
type Store struct {
	Offers     []Offer    `json:"offers"`
	GoldPacks  []GoldPack `json:"goldPacks"`
	Daily      Daily      `json:"daily"`
	SeasonPass SeasonPass `json:"seasonPass"`
}
