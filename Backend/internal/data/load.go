package data

import (
	"bytes"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io/fs"
	"os"
	"path/filepath"
	"sort"
)

// RequiredFiles are the data files the backend cannot start without.
var RequiredFiles = []string{
	"ai.json", "arenas.json", "cosmetics.json", "equipment.json", "modes.json",
	"progression.json", "store.json", "upgrades.json", "vehicles.json",
}

// OptionalFiles are loaded (and hashed) when present.
var OptionalFiles = []string{"brands.json"}

// Brand is the subset of brands.json used for referential checks.
type Brand struct {
	ID string `json:"id"`
}

// GameData is the validated, indexed game data set. It is immutable after
// Load/Parse and safe for concurrent use.
type GameData struct {
	Brands      []Brand
	Vehicles    []Vehicle
	Upgrades    Upgrades
	Equipment   []Equipment
	Cosmetics   Cosmetics
	Modes       []Mode
	Arenas      []Arena
	AI          []AIProfile
	Progression Progression
	Store       Store

	// Hash identifies the data set: hex(sha256(sha256sum-style manifest)),
	// see computeHash.
	Hash string
	// Files are the file names that were loaded, sorted.
	Files []string

	vehicles  map[string]*Vehicle
	equipment map[string]*Equipment
	cosmetics map[string]*Cosmetic
	modes     map[string]*Mode
	arenas    map[string]*Arena
	upgrades  map[string]*UpgradeCategory
	offers    map[string]*Offer
	goldPacks map[string]*GoldPack
	aiByID    map[string]*AIProfile
}

// Load reads and validates the data files in dir.
func Load(dir string) (*GameData, error) {
	files := map[string][]byte{}
	for _, name := range RequiredFiles {
		b, err := os.ReadFile(filepath.Join(dir, name))
		if err != nil {
			return nil, fmt.Errorf("data: read %s: %w", name, err)
		}
		files[name] = b
	}
	for _, name := range OptionalFiles {
		b, err := os.ReadFile(filepath.Join(dir, name))
		if errors.Is(err, fs.ErrNotExist) {
			continue
		}
		if err != nil {
			return nil, fmt.Errorf("data: read %s: %w", name, err)
		}
		files[name] = b
	}
	return Parse(files)
}

// Parse decodes and validates a data set given as file name → contents.
func Parse(files map[string][]byte) (*GameData, error) {
	gd := &GameData{}
	var errs []error
	decode := func(name string, v any) {
		b, ok := files[name]
		if !ok {
			errs = append(errs, fmt.Errorf("%s: missing", name))
			return
		}
		var hdr struct {
			Version *int `json:"version"`
		}
		if err := json.Unmarshal(b, &hdr); err != nil {
			errs = append(errs, fmt.Errorf("%s: %w", name, err))
			return
		}
		if hdr.Version == nil || *hdr.Version != 1 {
			errs = append(errs, fmt.Errorf("%s: top-level \"version\" must be 1", name))
			return
		}
		if err := json.Unmarshal(b, v); err != nil {
			errs = append(errs, fmt.Errorf("%s: %w", name, err))
		}
	}

	var vehiclesFile struct {
		Vehicles []Vehicle `json:"vehicles"`
	}
	var equipmentFile struct {
		Equipment []Equipment `json:"equipment"`
	}
	var modesFile struct {
		Modes []Mode `json:"modes"`
	}
	var arenasFile struct {
		Arenas []Arena `json:"arenas"`
	}
	var aiFile struct {
		Profiles []AIProfile `json:"profiles"`
	}
	decode("vehicles.json", &vehiclesFile)
	decode("upgrades.json", &gd.Upgrades)
	decode("equipment.json", &equipmentFile)
	decode("cosmetics.json", &gd.Cosmetics)
	decode("modes.json", &modesFile)
	decode("arenas.json", &arenasFile)
	decode("ai.json", &aiFile)
	decode("progression.json", &gd.Progression)
	decode("store.json", &gd.Store)
	if _, ok := files["brands.json"]; ok {
		var brandsFile struct {
			Brands []Brand `json:"brands"`
		}
		decode("brands.json", &brandsFile)
		gd.Brands = brandsFile.Brands
	}
	if len(errs) > 0 {
		return nil, fmt.Errorf("data: %w", errors.Join(errs...))
	}
	gd.Vehicles = vehiclesFile.Vehicles
	gd.Equipment = equipmentFile.Equipment
	gd.Modes = modesFile.Modes
	gd.Arenas = arenasFile.Arenas
	gd.AI = aiFile.Profiles

	gd.index()
	if err := gd.validate(); err != nil {
		return nil, fmt.Errorf("data: invalid game data: %w", err)
	}
	gd.Hash, gd.Files = computeHash(files)
	return gd, nil
}

// computeHash returns hex(sha256(manifest)) where manifest is exactly what
// `sha256sum` prints for the loaded files in sorted name order
// ("<hex sha256>  <file name>\n" per file). Reproduce with:
//
//	cd Content/BigCar/Data && sha256sum ai.json arenas.json brands.json ... | sha256sum
func computeHash(files map[string][]byte) (string, []string) {
	names := make([]string, 0, len(files))
	for n := range files {
		names = append(names, n)
	}
	sort.Strings(names)
	var manifest bytes.Buffer
	for _, n := range names {
		sum := sha256.Sum256(files[n])
		manifest.WriteString(hex.EncodeToString(sum[:]))
		manifest.WriteString("  ")
		manifest.WriteString(n)
		manifest.WriteString("\n")
	}
	sum := sha256.Sum256(manifest.Bytes())
	return hex.EncodeToString(sum[:]), names
}

func (gd *GameData) index() {
	gd.vehicles = map[string]*Vehicle{}
	for i := range gd.Vehicles {
		v := &gd.Vehicles[i]
		if v.UnlockLevel == 0 {
			v.UnlockLevel = 1
		}
		gd.vehicles[v.ID] = v
	}
	gd.equipment = map[string]*Equipment{}
	for i := range gd.Equipment {
		e := &gd.Equipment[i]
		if e.UnlockLevel == 0 {
			e.UnlockLevel = 1
		}
		gd.equipment[e.ID] = e
	}
	gd.cosmetics = map[string]*Cosmetic{}
	for _, list := range gd.cosmeticLists() {
		for i := range *list.items {
			c := &(*list.items)[i]
			c.Kind = list.kind
			if _, dup := gd.cosmetics[c.ID]; !dup {
				gd.cosmetics[c.ID] = c
			}
		}
	}
	gd.modes = map[string]*Mode{}
	for i := range gd.Modes {
		gd.modes[gd.Modes[i].ID] = &gd.Modes[i]
	}
	gd.arenas = map[string]*Arena{}
	for i := range gd.Arenas {
		gd.arenas[gd.Arenas[i].ID] = &gd.Arenas[i]
	}
	gd.upgrades = map[string]*UpgradeCategory{}
	for i := range gd.Upgrades.Categories {
		gd.upgrades[gd.Upgrades.Categories[i].ID] = &gd.Upgrades.Categories[i]
	}
	gd.offers = map[string]*Offer{}
	for i := range gd.Store.Offers {
		gd.offers[gd.Store.Offers[i].ID] = &gd.Store.Offers[i]
	}
	gd.goldPacks = map[string]*GoldPack{}
	for i := range gd.Store.GoldPacks {
		gd.goldPacks[gd.Store.GoldPacks[i].ID] = &gd.Store.GoldPacks[i]
	}
	gd.aiByID = map[string]*AIProfile{}
	for i := range gd.AI {
		gd.aiByID[gd.AI[i].ID] = &gd.AI[i]
	}
}

type cosmeticList struct {
	kind  string
	file  string
	items *[]Cosmetic
}

func (gd *GameData) cosmeticLists() []cosmeticList {
	return []cosmeticList{
		{ItemPaint, "paints", &gd.Cosmetics.Paints},
		{ItemWheels, "wheels", &gd.Cosmetics.Wheels},
		{ItemSparks, "sparks", &gd.Cosmetics.Sparks},
		{ItemHorn, "horns", &gd.Cosmetics.Horns},
		{ItemTitle, "titles", &gd.Cosmetics.Titles},
		{ItemBanner, "banners", &gd.Cosmetics.Banners},
	}
}

// Vehicle looks up a vehicle by id.
func (gd *GameData) Vehicle(id string) (*Vehicle, bool) { v, ok := gd.vehicles[id]; return v, ok }

// EquipmentItem looks up equipment by id.
func (gd *GameData) EquipmentItem(id string) (*Equipment, bool) {
	e, ok := gd.equipment[id]
	return e, ok
}

// Cosmetic looks up a cosmetic of any kind by id.
func (gd *GameData) Cosmetic(id string) (*Cosmetic, bool) { c, ok := gd.cosmetics[id]; return c, ok }

// Mode looks up a mode by id.
func (gd *GameData) Mode(id string) (*Mode, bool) { m, ok := gd.modes[id]; return m, ok }

// Arena looks up an arena by id.
func (gd *GameData) Arena(id string) (*Arena, bool) { a, ok := gd.arenas[id]; return a, ok }

// UpgradeCategory looks up an upgrade category by id.
func (gd *GameData) UpgradeCategory(id string) (*UpgradeCategory, bool) {
	c, ok := gd.upgrades[id]
	return c, ok
}

// Offer looks up a store offer by id.
func (gd *GameData) Offer(id string) (*Offer, bool) { o, ok := gd.offers[id]; return o, ok }

// GoldPack looks up a gold pack by id.
func (gd *GameData) GoldPack(id string) (*GoldPack, bool) { p, ok := gd.goldPacks[id]; return p, ok }

// HasAIProfile reports whether ai.json defines the profile.
func (gd *GameData) HasAIProfile(id string) bool { _, ok := gd.aiByID[id]; return ok }

// StarterVehicles returns the ids of all starter vehicles in file order.
func (gd *GameData) StarterVehicles() []string {
	var out []string
	for _, v := range gd.Vehicles {
		if v.Starter {
			out = append(out, v.ID)
		}
	}
	return out
}

// ItemBasePrice returns the catalogue price of a non-currency item.
func (gd *GameData) ItemBasePrice(it Item) (Price, bool) {
	switch {
	case it.Type == ItemVehicle:
		if v, ok := gd.vehicles[it.ID]; ok {
			return v.Price, true
		}
	case it.Type == ItemEquipment:
		if e, ok := gd.equipment[it.ID]; ok {
			return e.Price, true
		}
	case IsCosmeticType(it.Type):
		if c, ok := gd.cosmetics[it.ID]; ok && c.Kind == it.Type {
			return c.Price, true
		}
	}
	return Price{}, false
}

// ItemExists reports whether an item reference resolves.
func (gd *GameData) ItemExists(it Item) bool {
	if it.Type == ItemCurrency {
		return (it.ID == string(Credits) || it.ID == string(Gold)) && it.Amount > 0
	}
	_, ok := gd.ItemBasePrice(it)
	return ok
}
