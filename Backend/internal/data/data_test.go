package data_test

import (
	"encoding/json"
	"errors"
	"io/fs"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/comeheretosee/bigcar/backend/internal/data"
	"github.com/comeheretosee/bigcar/backend/internal/data/datatest"
)

func TestLoadFixture(t *testing.T) {
	gd := datatest.Load(t)
	if got := gd.StarterVehicles(); len(got) != 2 || got[0] != "hoshino_corolla" {
		t.Fatalf("starters = %v", got)
	}
	if len(gd.Hash) != 64 {
		t.Fatalf("hash = %q", gd.Hash)
	}
	if _, ok := gd.Vehicle("fulmine_f8r"); !ok {
		t.Fatal("vehicle lookup failed")
	}
	if c, ok := gd.Cosmetic("wheels_stock"); !ok || c.Kind != data.ItemWheels || !c.Price.IsFree() {
		t.Fatalf("cosmetic lookup: %+v %v", c, ok)
	}
	if !gd.ItemExists(data.Item{Type: data.ItemPaint, ID: "paint_rosso"}) {
		t.Fatal("paint_rosso should exist")
	}
	if gd.ItemExists(data.Item{Type: data.ItemWheels, ID: "paint_rosso"}) {
		t.Fatal("type must match the cosmetic kind")
	}
}

func TestHashIsStableAndContentSensitive(t *testing.T) {
	files := datatest.Files(t)
	a, err := data.Parse(files)
	if err != nil {
		t.Fatal(err)
	}
	b, err := data.Parse(files)
	if err != nil {
		t.Fatal(err)
	}
	if a.Hash != b.Hash {
		t.Fatal("hash not deterministic")
	}
	files["ai.json"] = append(append([]byte{}, files["ai.json"]...), '\n')
	c, err := data.Parse(files)
	if err != nil {
		t.Fatal(err)
	}
	if c.Hash == a.Hash {
		t.Fatal("hash must change when file bytes change")
	}
}

func TestUnknownFieldsAreTolerated(t *testing.T) {
	files := datatest.Files(t)
	mutateJSON(t, files, "vehicles.json", func(m map[string]any) {
		m["futureTopLevel"] = map[string]any{"x": 1}
		v := m["vehicles"].([]any)[0].(map[string]any)
		v["hologramColor"] = "#00FF00"
	})
	mutateJSON(t, files, "store.json", func(m map[string]any) {
		o := m["offers"].([]any)[0].(map[string]any)
		o["badgeAnimation"] = "pulse"
	})
	if _, err := data.Parse(files); err != nil {
		t.Fatalf("unknown fields must be ignored: %v", err)
	}
}

func TestValidationErrors(t *testing.T) {
	tests := []struct {
		name    string
		file    string
		mutate  func(m map[string]any)
		wantErr string
	}{
		{"bad version", "ai.json", func(m map[string]any) { m["version"] = 2 }, `"version" must be 1`},
		{"missing upgrade category", "upgrades.json", func(m map[string]any) {
			m["categories"] = m["categories"].([]any)[1:]
		}, `missing category "engine"`},
		{"wrong upgrade level count", "upgrades.json", func(m map[string]any) {
			c := m["categories"].([]any)[0].(map[string]any)
			c["levels"] = c["levels"].([]any)[:4]
		}, "exactly 5 levels"},
		{"store item does not exist", "store.json", func(m map[string]any) {
			o := m["offers"].([]any)[0].(map[string]any)
			o["items"] = []any{map[string]any{"type": "vehicle", "id": "nope"}}
		}, `unknown vehicle "nope"`},
		{"store item wrong cosmetic kind", "store.json", func(m map[string]any) {
			o := m["offers"].([]any)[0].(map[string]any)
			o["items"] = []any{map[string]any{"type": "wheels", "id": "paint_rosso"}}
		}, `unknown wheels "paint_rosso"`},
		{"zero offer price", "store.json", func(m map[string]any) {
			o := m["offers"].([]any)[0].(map[string]any)
			o["price"] = map[string]any{"gold": 0}
		}, "price must be positive"},
		{"negative price", "equipment.json", func(m map[string]any) {
			e := m["equipment"].([]any)[1].(map[string]any)
			e["price"] = map[string]any{"credits": -5}
		}, "non-negative integer"},
		{"two currency keys", "vehicles.json", func(m map[string]any) {
			v := m["vehicles"].([]any)[2].(map[string]any)
			v["price"] = map[string]any{"credits": 5, "gold": 1}
		}, "exactly one key"},
		{"starter not free", "vehicles.json", func(m map[string]any) {
			v := m["vehicles"].([]any)[0].(map[string]any)
			v["price"] = map[string]any{"credits": 10}
		}, "starter vehicles must cost"},
		{"no starter", "vehicles.json", func(m map[string]any) {
			for _, v := range m["vehicles"].([]any) {
				v.(map[string]any)["starter"] = false
			}
		}, "at least one vehicle"},
		{"bad class", "vehicles.json", func(m map[string]any) {
			m["vehicles"].([]any)[0].(map[string]any)["class"] = "spaceship"
		}, `unknown class "spaceship"`},
		{"unknown brand", "vehicles.json", func(m map[string]any) {
			m["vehicles"].([]any)[0].(map[string]any)["brand"] = "acme"
		}, `unknown brand "acme"`},
		{"ability in wrong slot", "equipment.json", func(m map[string]any) {
			m["equipment"].([]any)[0].(map[string]any)["ability"] = "brace"
		}, "only items in slot"},
		{"missing mode", "modes.json", func(m map[string]any) {
			m["modes"] = m["modes"].([]any)[1:]
		}, `missing required mode "derby"`},
		{"too few spawns", "arenas.json", func(m map[string]any) {
			a := m["arenas"].([]any)[0].(map[string]any)
			a["spawns"] = a["spawns"].([]any)[:5]
		}, "at least 12 spawns"},
		{"missing ai profile", "ai.json", func(m map[string]any) {
			m["profiles"] = m["profiles"].([]any)[:4]
		}, `missing required profile "boss"`},
		{"ranks out of order", "progression.json", func(m map[string]any) {
			r := m["ranks"].([]any)
			r[0], r[1] = r[1], r[0]
		}, `ranks[0] must be "bronze"`},
		{"too few season tiers", "store.json", func(m map[string]any) {
			sp := m["seasonPass"].(map[string]any)
			sp["tiers"] = sp["tiers"].([]any)[:10]
		}, "50-100 tiers"},
		{"daily pool currency", "store.json", func(m map[string]any) {
			d := m["daily"].(map[string]any)
			d["pool"] = append(d["pool"].([]any), map[string]any{"type": "currency", "id": "gold", "amount": 5})
		}, "cannot be daily deals"},
		{"daily discount out of range", "store.json", func(m map[string]any) {
			m["daily"].(map[string]any)["discounts"] = []any{0.2, 1.0}
		}, "must be in (0, 1)"},
		{"level reward bad item", "progression.json", func(m map[string]any) {
			lr := m["levelRewards"].([]any)[0].(map[string]any)
			lr["items"] = []any{map[string]any{"type": "currency", "id": "diamonds", "amount": 5}}
		}, "currency id must be credits or gold"},
		{"duplicate cosmetic id across kinds", "cosmetics.json", func(m map[string]any) {
			w := m["wheels"].([]any)[1].(map[string]any)
			w["id"] = "paint_rosso"
		}, "id already used in paints"},
		{"gold pack without gold", "store.json", func(m map[string]any) {
			m["goldPacks"].([]any)[0].(map[string]any)["gold"] = 0
		}, "gold must be positive"},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			files := datatest.Files(t)
			mutateJSON(t, files, tc.file, tc.mutate)
			_, err := data.Parse(files)
			if err == nil {
				t.Fatalf("expected error containing %q", tc.wantErr)
			}
			if !strings.Contains(err.Error(), tc.wantErr) {
				t.Fatalf("error %q does not contain %q", err, tc.wantErr)
			}
		})
	}
}

func TestMissingRequiredFile(t *testing.T) {
	files := datatest.Files(t)
	delete(files, "store.json")
	if _, err := data.Parse(files); err == nil || !strings.Contains(err.Error(), "store.json: missing") {
		t.Fatalf("err = %v", err)
	}
}

func TestRoundTo100(t *testing.T) {
	tests := []struct {
		in   float64
		want int64
	}{
		{0, 0}, {49, 0}, {50, 100}, {149.99, 100}, {150, 200}, {4000, 4000},
		{4000 * 1.6, 6400}, {9000 * 1.6, 14400}, {16000 * 2.5, 40000}, {2531 * 1.6, 4000},
		{2550, 2600}, {2549.999999, 2500},
	}
	for _, tc := range tests {
		if got := data.RoundTo100(tc.in); got != tc.want {
			t.Errorf("RoundTo100(%v) = %d, want %d", tc.in, got, tc.want)
		}
	}
}

func TestUpgradeCost(t *testing.T) {
	gd := datatest.Load(t)
	tests := []struct {
		cat   string
		level int
		tier  int
		want  int64
	}{
		{"engine", 1, 1, 4000},     // 4000 * 1.0
		{"engine", 2, 2, 14400},    // 9000 * 1.6
		{"engine", 5, 5, 240000},   // 40000 * 6.0
		{"tires", 3, 2, 14400},     // 9000 * 1.6
		{"brakes", 2, 2, 7200},     // 4500 * 1.6
		{"armor", 4, 3, 55000},     // 22000 * 2.5
		{"weight", 3, 4, 42000},    // 10500 * 4.0
		{"suspension", 1, 2, 4000}, // 2500 * 1.6
		{"nitro", 4, 2, 32800},     // 20500 * 1.6
	}
	for _, tc := range tests {
		got, err := gd.UpgradeCost(tc.cat, tc.level, tc.tier)
		if err != nil {
			t.Fatalf("%s L%d T%d: %v", tc.cat, tc.level, tc.tier, err)
		}
		if got != tc.want {
			t.Errorf("%s L%d T%d = %d, want %d", tc.cat, tc.level, tc.tier, got, tc.want)
		}
	}
	for _, bad := range []struct {
		cat         string
		level, tier int
	}{{"engine", 0, 1}, {"engine", 6, 1}, {"engine", 1, 0}, {"engine", 1, 6}, {"wings", 1, 1}} {
		if _, err := gd.UpgradeCost(bad.cat, bad.level, bad.tier); err == nil {
			t.Errorf("expected error for %+v", bad)
		}
	}
}

func TestXPToNext(t *testing.T) {
	gd := datatest.Load(t)
	tests := []struct {
		level int
		want  int64
	}{
		{1, 1000}, {2, 1060}, {3, 1124}, {4, 1191}, {10, 1689}, {12, 1898},
	}
	for _, tc := range tests {
		if got := gd.XPToNext(tc.level); got != tc.want {
			t.Errorf("XPToNext(%d) = %d, want %d", tc.level, got, tc.want)
		}
	}
}

func TestMatchReward(t *testing.T) {
	gd := datatest.Load(t)
	tests := []struct {
		name           string
		score          float64
		winner, ranked bool
		credits, xp    int64
	}{
		{"zero", 0, false, false, 800, 150},
		{"score", 1000, false, false, 2300, 950},
		{"winner", 1000, true, false, 3450, 1425},
		{"ranked winner", 1000, true, true, 4313, 1781},    // 3450*1.25=4312.5 → 4313; 1425*1.25=1781.25
		{"capped", 1e6, true, true, 40000, 20000},          // caps
		{"ranked loser", 2000, false, true, 4750, 2188},    // 3800*1.25; 1750*1.25=2187.5 → 2188
		{"fractional score", 333, false, false, 1300, 416}, // 800+499.5=1299.5 → 1300; 150+266.4
		{"winner small", 10, true, false, 1223, 237},       // 815*1.5=1222.5 → 1223; 158*1.5=237
		{"loser ranked zero", 0, false, true, 1000, 188},   // 800*1.25; 150*1.25=187.5 → 188
		{"winner ranked zero", 0, true, true, 1500, 281},   // 1200*1.25; 225*1.25=281.25
		{"big score not capped", 20000, false, false, 30800, 16150},
	}
	for _, tc := range tests {
		c, x := gd.MatchReward(tc.score, tc.winner, tc.ranked)
		if c != tc.credits || x != tc.xp {
			t.Errorf("%s: got (%d, %d), want (%d, %d)", tc.name, c, x, tc.credits, tc.xp)
		}
	}
}

func TestRankFor(t *testing.T) {
	gd := datatest.Load(t)
	tests := []struct {
		rating   float64
		ladder   int
		tier     string
		division int
	}{
		{-50, 0, "bronze", 3},
		{0, 0, "bronze", 3},
		{399, 0, "bronze", 3},
		{400, 0, "bronze", 2},
		{800, 0, "bronze", 1},
		{1199.9, 0, "bronze", 1},
		{1200, 0, "silver", 3},
		{1500, 0, "gold", 2}, // 1400..1600 split in 3: [1400,1466.7) d3, [1466.7,1533.3) d2
		{1599, 0, "gold", 1},
		{2000, 0, "master", 1},
		{2199, 0, "master", 1},
		{2300, 0, "master", 1},   // crash_king rating but not on ladder
		{2300, 101, "master", 1}, // outside top 100
		{2300, 100, "crash_king", 1},
		{2300, 1, "crash_king", 1},
		{2100, 1, "master", 1}, // top-1 but below crash_king minRating
	}
	for _, tc := range tests {
		tier, div := gd.RankFor(tc.rating, tc.ladder)
		if tier != tc.tier || div != tc.division {
			t.Errorf("RankFor(%v, %d) = %s %d, want %s %d", tc.rating, tc.ladder, tier, div, tc.tier, tc.division)
		}
	}
	if tier, div := gd.PlacementRank(); tier != "bronze" || div != 3 {
		t.Errorf("PlacementRank = %s %d", tier, div)
	}
}

func TestOfferActiveWindow(t *testing.T) {
	gd := datatest.Load(t)
	o, _ := gd.Offer("offer_halloween")
	for _, tc := range []struct {
		at   string
		want bool
	}{
		{"2026-10-24T23:59:59Z", false}, {"2026-10-25T00:00:00Z", true},
		{"2026-11-01T23:59:59Z", true}, {"2026-11-02T00:00:00Z", false},
	} {
		if got := o.ActiveAt(mustTime(t, tc.at)); got != tc.want {
			t.Errorf("ActiveAt(%s) = %v", tc.at, got)
		}
	}
}

// TestRealDataDir validates the game's real data set when it is present. The
// data is authored in parallel by the content team, so the test is skipped
// while the directory or any required file is missing.
func TestRealDataDir(t *testing.T) {
	dir := filepath.Join("..", "..", "..", "Content", "BigCar", "Data")
	if _, err := os.Stat(dir); errors.Is(err, fs.ErrNotExist) {
		t.Skipf("real data dir %s not present", dir)
	}
	for _, name := range data.RequiredFiles {
		if _, err := os.Stat(filepath.Join(dir, name)); errors.Is(err, fs.ErrNotExist) {
			t.Skipf("real data file %s not present yet", name)
		}
	}
	gd, err := data.Load(dir)
	if err != nil {
		t.Fatalf("real game data is invalid:\n%v", err)
	}
	t.Logf("real data OK: %d vehicles, %d offers, hash %s", len(gd.Vehicles), len(gd.Store.Offers), gd.Hash)
}

func mutateJSON(t *testing.T, files map[string][]byte, name string, fn func(map[string]any)) {
	t.Helper()
	var m map[string]any
	if err := json.Unmarshal(files[name], &m); err != nil {
		t.Fatal(err)
	}
	fn(m)
	b, err := json.Marshal(m)
	if err != nil {
		t.Fatal(err)
	}
	files[name] = b
}

func mustTime(t *testing.T, s string) time.Time {
	t.Helper()
	ts, err := time.Parse(time.RFC3339, s)
	if err != nil {
		t.Fatal(err)
	}
	return ts
}
