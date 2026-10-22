package profile_test

import (
	"encoding/json"
	"reflect"
	"strings"
	"testing"
	"time"

	"github.com/comeheretosee/bigcar/backend/internal/apperr"
	"github.com/comeheretosee/bigcar/backend/internal/data"
	"github.com/comeheretosee/bigcar/backend/internal/data/datatest"
	"github.com/comeheretosee/bigcar/backend/internal/profile"
)

// docExample is the profile document from docs/BACKEND_API.md.
const docExample = `{
  "version": 1,
  "playerId": "p_5c1d0a2e9b7f",
  "displayName": "Driver5c1d",
  "level": 12,
  "xp": 3400,
  "wallet": { "credits": 120000, "gold": 300 },
  "garage": {
    "selected": "hoshino_corolla",
    "vehicles": [
      { "vehicleId": "hoshino_corolla",
        "upgrades": { "engine": 2, "armor": 1 },
        "equipment": { "front": "front_bar_basic" },
        "paint": "paint_rosso", "wheels": "wheels_stock" }
    ]
  },
  "inventory": { "equipment": ["front_bar_basic"], "cosmetics": ["paint_rosso"] },
  "purchases": { "offer_starter_pack": 1 },
  "seasonPass": { "season": "season_1", "xp": 1200, "premium": false, "claimedFree": [1, 2], "claimedPremium": [] },
  "ranked": {
    "ranked_3v3": { "rating": 1500, "rd": 350, "vol": 0.06, "wins": 0, "losses": 0,
                    "tier": "bronze", "division": 3, "placementsLeft": 5 }
  },
  "stats": { "matches": 0, "wins": 0, "takedowns": 0, "wrecked": 0, "damageDealt": 0,
             "impacts": { "t_bone": 3, "launch": 1 } }
}`

// TestJSONLayoutMatchesContract round-trips the documented example: any
// renamed, missing or extra field would make the documents differ.
func TestJSONLayoutMatchesContract(t *testing.T) {
	var p profile.Profile
	dec := json.NewDecoder(strings.NewReader(docExample))
	dec.DisallowUnknownFields()
	if err := dec.Decode(&p); err != nil {
		t.Fatalf("doc example does not decode strictly: %v", err)
	}
	out, err := json.Marshal(&p)
	if err != nil {
		t.Fatal(err)
	}
	var want, got any
	_ = json.Unmarshal([]byte(docExample), &want)
	_ = json.Unmarshal(out, &got)
	if !reflect.DeepEqual(want, got) {
		t.Fatalf("round trip differs:\nwant %v\n got %v", want, got)
	}
}

func TestNewAccountDefaults(t *testing.T) {
	gd := datatest.Load(t)
	p := profile.New("p_5c1d0a2e9b7f", profile.DefaultDisplayName("p_5c1d0a2e9b7f"), gd)
	if p.DisplayName != "Driver5c1d" {
		t.Errorf("display name %q", p.DisplayName)
	}
	if p.Wallet != (profile.Wallet{Credits: 25000, Gold: 0}) {
		t.Errorf("wallet %+v", p.Wallet)
	}
	if p.Level != 1 || p.XP != 0 || p.Version != 1 {
		t.Errorf("level/xp/version %d/%d/%d", p.Level, p.XP, p.Version)
	}
	if len(p.Garage.Vehicles) != 2 || p.Garage.Selected != "hoshino_corolla" {
		t.Errorf("garage %+v", p.Garage)
	}
	if p.SeasonPass.Season != "season_1" {
		t.Errorf("season %q", p.SeasonPass.Season)
	}
	b, _ := json.Marshal(p)
	var m map[string]any
	_ = json.Unmarshal(b, &m)
	for _, k := range []string{"purchases", "ranked"} {
		if _, ok := m[k].(map[string]any); !ok {
			t.Errorf("%s must be an object, got %v", k, m[k])
		}
	}
	inv := m["inventory"].(map[string]any)
	if _, ok := inv["equipment"].([]any); !ok {
		t.Errorf("inventory.equipment must be a list: %v", inv["equipment"])
	}
}

func newProfile(t *testing.T, gd *data.GameData) *profile.Profile {
	t.Helper()
	return profile.New("p_000000000001", "Tester", gd)
}

func code(err error) string { return apperr.CodeOf(err) }

func TestBuyVehicle(t *testing.T) {
	gd := datatest.Load(t)
	tests := []struct {
		name    string
		setup   func(p *profile.Profile)
		vehicle string
		code    string
		credits int64
	}{
		{"ok", nil, "hoshino_supra", "", 5000},
		{"unknown", nil, "nope", apperr.NotFound, 25000},
		{"starter already owned", nil, "hoshino_corolla", apperr.AlreadyOwned, 25000},
		{"level locked", func(p *profile.Profile) { p.Wallet.Credits = 1e6 }, "ironhorse_f150r", apperr.LevelLocked, 1e6},
		{"level ok", func(p *profile.Profile) { p.Wallet.Credits = 1e6; p.Level = 5 }, "ironhorse_f150r", "", 1e6 - 90000},
		{"insufficient", func(p *profile.Profile) { p.Wallet.Credits = 19999 }, "hoshino_supra", apperr.InsufficientFunds, 19999},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			p := newProfile(t, gd)
			if tc.setup != nil {
				tc.setup(p)
			}
			err := profile.BuyVehicle(p, gd, tc.vehicle)
			if code(err) != tc.code {
				t.Fatalf("code = %q (%v), want %q", code(err), err, tc.code)
			}
			if p.Wallet.Credits != tc.credits {
				t.Errorf("credits = %d, want %d", p.Wallet.Credits, tc.credits)
			}
			if err == nil && p.Vehicle(tc.vehicle) == nil {
				t.Error("vehicle not added")
			}
		})
	}
}

func TestUpgradeSequenceAndCosts(t *testing.T) {
	gd := datatest.Load(t)
	p := newProfile(t, gd)
	p.Wallet.Credits = 10_000_000
	if err := profile.BuyVehicle(p, gd, "hoshino_supra"); err != nil { // tier 3 → ×2.5
		t.Fatal(err)
	}
	want := []int64{10000, 22500, 40000, 65000, 100000} // engine costs × 2.5
	for i, w := range want {
		before := p.Wallet.Credits
		cost, err := profile.Upgrade(p, gd, "hoshino_supra", "engine")
		if err != nil {
			t.Fatalf("level %d: %v", i+1, err)
		}
		if cost != w || before-p.Wallet.Credits != w {
			t.Fatalf("level %d cost %d, want %d", i+1, cost, w)
		}
		if p.Vehicle("hoshino_supra").Upgrades["engine"] != i+1 {
			t.Fatalf("level not stored")
		}
	}
	if _, err := profile.Upgrade(p, gd, "hoshino_supra", "engine"); code(err) != apperr.MaxLevel {
		t.Fatalf("6th upgrade: %v", err)
	}

	errs := []struct {
		name, vehicle, cat, code string
		credits                  int64
	}{
		{"not owned", "fulmine_f8r", "engine", apperr.NotOwned, 1e6},
		{"unknown vehicle", "nope", "engine", apperr.NotFound, 1e6},
		{"unknown category", "hoshino_corolla", "wings", apperr.BadRequest, 1e6},
		{"insufficient", "hoshino_corolla", "engine", apperr.InsufficientFunds, 3999},
	}
	for _, tc := range errs {
		t.Run(tc.name, func(t *testing.T) {
			p := newProfile(t, gd)
			p.Wallet.Credits = tc.credits
			if _, err := profile.Upgrade(p, gd, tc.vehicle, tc.cat); code(err) != tc.code {
				t.Fatalf("code %q (%v), want %q", code(err), err, tc.code)
			}
			if p.Wallet.Credits != tc.credits {
				t.Fatal("wallet changed on failure")
			}
		})
	}
}

func strp(s string) *string { return &s }

func TestEquipmentAndEquip(t *testing.T) {
	gd := datatest.Load(t)
	p := newProfile(t, gd)
	p.Wallet.Credits = 1e6

	if err := profile.BuyEquipment(p, gd, "front_bar_basic"); code(err) != apperr.AlreadyOwned {
		t.Fatalf("free equipment is implicitly owned: %v", err)
	}
	if err := profile.BuyEquipment(p, gd, "front_wedge_steel"); code(err) != apperr.LevelLocked {
		t.Fatalf("wedge needs level 8: %v", err)
	}
	if err := profile.BuyEquipment(p, gd, "roof_cage"); err != nil {
		t.Fatal(err)
	}
	if err := profile.BuyEquipment(p, gd, "roof_cage"); code(err) != apperr.AlreadyOwned {
		t.Fatalf("second buy: %v", err)
	}

	tests := []struct {
		name, vehicle, slot string
		item                *string
		code                string
	}{
		{"free item", "hoshino_corolla", "front", strp("front_bar_basic"), ""},
		{"wrong slot", "hoshino_corolla", "rear", strp("front_bar_basic"), apperr.InvalidSlot},
		{"unknown slot", "hoshino_corolla", "trunk", strp("front_bar_basic"), apperr.InvalidSlot},
		{"not owned", "hoshino_corolla", "side", strp("side_plates_light"), apperr.NotOwned},
		{"class not allowed", "hoshino_corolla", "roof", strp("roof_cage"), apperr.Forbidden},
		{"class allowed", "ironhorse_ranger", "roof", strp("roof_cage"), ""},
		{"vehicle not owned", "fulmine_f8r", "front", strp("front_bar_basic"), apperr.NotOwned},
		{"unknown item", "hoshino_corolla", "front", strp("front_laser"), apperr.NotFound},
		{"unequip", "ironhorse_ranger", "roof", nil, ""},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			err := profile.Equip(p, gd, tc.vehicle, tc.slot, tc.item)
			if code(err) != tc.code {
				t.Fatalf("code %q (%v), want %q", code(err), err, tc.code)
			}
		})
	}
	if got := p.Vehicle("hoshino_corolla").Equipment["front"]; got != "front_bar_basic" {
		t.Errorf("front = %q", got)
	}
	if _, ok := p.Vehicle("ironhorse_ranger").Equipment["roof"]; ok {
		t.Error("roof should be unequipped")
	}
}

func TestCosmeticsAndCustomize(t *testing.T) {
	gd := datatest.Load(t)
	p := newProfile(t, gd)
	if err := profile.BuyCosmetic(p, gd, "wheels_stock"); code(err) != apperr.AlreadyOwned {
		t.Fatalf("free wheels owned implicitly: %v", err)
	}
	if err := profile.BuyCosmetic(p, gd, "paint_rosso"); err != nil {
		t.Fatal(err)
	}
	if p.Wallet.Credits != 23000 {
		t.Errorf("credits %d", p.Wallet.Credits)
	}
	tests := []struct {
		name          string
		paint, wheels *string
		code          string
		wantPaint     string
		wantWheels    string
	}{
		{"paint", strp("paint_rosso"), nil, "", "paint_rosso", ""},
		{"wheels keep paint", nil, strp("wheels_stock"), "", "paint_rosso", "wheels_stock"},
		{"unowned paint", strp("paint_midnight"), nil, apperr.NotOwned, "paint_rosso", "wheels_stock"},
		{"wheels id as paint", strp("wheels_stock"), nil, apperr.NotFound, "paint_rosso", "wheels_stock"},
		{"reset paint", strp(""), nil, "", "", "wheels_stock"},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			err := profile.Customize(p, gd, "hoshino_corolla", tc.paint, tc.wheels)
			if code(err) != tc.code {
				t.Fatalf("code %q (%v)", code(err), err)
			}
			v := p.Vehicle("hoshino_corolla")
			if v.Paint != tc.wantPaint || v.Wheels != tc.wantWheels {
				t.Fatalf("paint/wheels = %q/%q", v.Paint, v.Wheels)
			}
		})
	}
}

func TestSelectAndName(t *testing.T) {
	gd := datatest.Load(t)
	p := newProfile(t, gd)
	if err := profile.SelectVehicle(p, gd, "ironhorse_ranger"); err != nil || p.Garage.Selected != "ironhorse_ranger" {
		t.Fatalf("select: %v", err)
	}
	if err := profile.SelectVehicle(p, gd, "hoshino_supra"); code(err) != apperr.NotOwned {
		t.Fatalf("select unowned: %v", err)
	}
	for _, tc := range []struct {
		in, want, code string
	}{
		{"  Speedy  ", "Speedy", ""},
		{"A", "", apperr.BadRequest},
		{"ab", "ab", ""},
		{"撞车王", "撞车王", ""},
		{"1234567890123456", "1234567890123456", ""},
		{"12345678901234567", "", apperr.BadRequest},
		{"bad\x00name", "", apperr.BadRequest},
	} {
		got, err := profile.ValidateDisplayName(tc.in)
		if code(err) != tc.code || got != tc.want {
			t.Errorf("ValidateDisplayName(%q) = %q, %v", tc.in, got, err)
		}
	}
}

func TestGrantAndDuplicates(t *testing.T) {
	gd := datatest.Load(t)
	p := newProfile(t, gd)
	tests := []struct {
		name string
		item data.Item
		want []data.Item
	}{
		{"new paint", data.Item{Type: "paint", ID: "paint_rosso"}, []data.Item{{Type: "paint", ID: "paint_rosso"}}},
		{"duplicate paint → 50% credits", data.Item{Type: "paint", ID: "paint_rosso"},
			[]data.Item{{Type: "currency", ID: "credits", Amount: 1000}}},
		{"new vehicle", data.Item{Type: "vehicle", ID: "fulmine_f8r"}, []data.Item{{Type: "vehicle", ID: "fulmine_f8r"}}},
		{"duplicate vehicle", data.Item{Type: "vehicle", ID: "fulmine_f8r"},
			[]data.Item{{Type: "currency", ID: "credits", Amount: 225000}}},
		{"free item duplicate grants nothing", data.Item{Type: "wheels", ID: "wheels_stock"}, nil},
		{"gold", data.Item{Type: "currency", ID: "gold", Amount: 70}, []data.Item{{Type: "currency", ID: "gold", Amount: 70}}},
		{"unknown", data.Item{Type: "paint", ID: "nope"}, nil},
	}
	for _, tc := range tests {
		got := profile.Grant(p, gd, tc.item)
		if !reflect.DeepEqual(got, tc.want) {
			t.Errorf("%s: got %v, want %v", tc.name, got, tc.want)
		}
	}
	if p.Wallet.Credits != 25000+1000+225000 || p.Wallet.Gold != 70 {
		t.Errorf("wallet %+v", p.Wallet)
	}
	if p.Vehicle("fulmine_f8r") == nil {
		t.Error("vehicle not granted")
	}
}

func TestAddXPLevels(t *testing.T) {
	gd := datatest.Load(t)
	tests := []struct {
		name          string
		level         int
		xp, add       int64
		wantLevel     int
		wantXP        int64
		wantGrantedN  int
		wantCredits   int64
		wantGoldAfter int64
	}{
		{"no level", 1, 0, 999, 1, 999, 0, 25000, 0},
		{"exact level", 1, 0, 1000, 2, 0, 1, 30000, 0},
		{"carry over", 1, 500, 600, 2, 100, 1, 30000, 0},
		// 1000 + 1060 + 1124 + 1191 = 4375 → level 5 with 25 left; rewards 2,3,5
		{"multi level", 1, 0, 4400, 5, 25, 4, 30000, 50},
		{"max level stays 0", 50, 0, 100000, 50, 0, 0, 25000, 0},
		{"reach max", 49, 0, 1e9, 50, 0, 0, 25000, 0},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			p := newProfile(t, gd)
			p.Level, p.XP = tc.level, tc.xp
			granted := profile.AddXP(p, gd, tc.add)
			if p.Level != tc.wantLevel || p.XP != tc.wantXP {
				t.Fatalf("level/xp = %d/%d, want %d/%d", p.Level, p.XP, tc.wantLevel, tc.wantXP)
			}
			if len(granted) != tc.wantGrantedN {
				t.Fatalf("granted %v", granted)
			}
			if p.Wallet.Credits != tc.wantCredits || p.Wallet.Gold != tc.wantGoldAfter {
				t.Fatalf("wallet %+v", p.Wallet)
			}
		})
	}
}

func TestNormalizeSeasonRollover(t *testing.T) {
	gd := datatest.Load(t)
	now := time.Date(2026, 11, 15, 12, 0, 0, 0, time.UTC)
	p := newProfile(t, gd)
	if profile.Normalize(p, gd, now) {
		t.Fatal("fresh profile should not change")
	}
	p.SeasonPass = profile.SeasonPass{Season: "season_0", XP: 5000, Premium: true, ClaimedFree: []int{1}}
	p.Ranked["ranked_1v1"] = profile.Ranked{Rating: 1900, RD: 60, Vol: 0.06, Wins: 30, Losses: 10, Tier: "diamond", Division: 1}
	p.Purchases[profile.DailyPurchaseKey(profile.DayNumber(now)-1, 0)] = 1
	p.Purchases[profile.DailyPurchaseKey(profile.DayNumber(now), 2)] = 1
	if !profile.Normalize(p, gd, now) {
		t.Fatal("expected change")
	}
	if p.SeasonPass.Season != "season_1" || p.SeasonPass.XP != 0 || p.SeasonPass.Premium {
		t.Errorf("season pass not reset: %+v", p.SeasonPass)
	}
	r := p.Ranked["ranked_1v1"]
	if r.Rating != 1700 || r.RD != 200 || r.PlacementsLeft != 5 || r.Wins != 0 || r.Tier != "bronze" {
		t.Errorf("ranked not soft reset: %+v", r)
	}
	if len(p.Purchases) != 1 {
		t.Errorf("old daily markers not pruned: %v", p.Purchases)
	}
}

func TestDayNumber(t *testing.T) {
	// floor(unix / 86400). Note: the GET /v1/store example in BACKEND_API.md
	// pairs serverTime 2026-10-22 with day 20383, which is really 2025-10-22.
	tests := []struct {
		at   time.Time
		want int64
	}{
		{time.Date(2026, 10, 22, 8, 0, 0, 0, time.UTC), 20748},
		{time.Date(2025, 10, 22, 23, 59, 59, 0, time.UTC), 20383},
		{time.Unix(0, 0), 0},
		{time.Unix(-1, 0), -1},
	}
	for _, tc := range tests {
		if d := profile.DayNumber(tc.at); d != tc.want {
			t.Errorf("DayNumber(%v) = %d, want %d", tc.at, d, tc.want)
		}
	}
}

func TestRecordMatch(t *testing.T) {
	gd := datatest.Load(t)
	p := newProfile(t, gd)
	p.RecordMatch(profile.MatchStats{Won: true, Takedowns: 3, Wrecked: 1, DamageDealt: 1500, Impacts: map[string]int64{"t_bone": 2}})
	p.RecordMatch(profile.MatchStats{Takedowns: 1, Impacts: map[string]int64{"t_bone": 1, "launch": 1}})
	s := p.Stats
	if s.Matches != 2 || s.Wins != 1 || s.Takedowns != 4 || s.Wrecked != 1 || s.DamageDealt != 1500 ||
		s.Impacts["t_bone"] != 3 || s.Impacts["launch"] != 1 {
		t.Fatalf("stats %+v", s)
	}
}
