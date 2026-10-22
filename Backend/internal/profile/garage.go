package profile

import (
	"strings"
	"unicode"
	"unicode/utf8"

	"github.com/comeheretosee/bigcar/backend/internal/apperr"
	"github.com/comeheretosee/bigcar/backend/internal/data"
)

// ValidateDisplayName trims the name and checks the 2–16 character rule.
func ValidateDisplayName(name string) (string, error) {
	name = strings.TrimSpace(name)
	n := utf8.RuneCountInString(name)
	if !utf8.ValidString(name) || n < 2 || n > 16 {
		return "", apperr.New(apperr.BadRequest, "displayName must be 2-16 characters")
	}
	for _, r := range name {
		if unicode.IsControl(r) || r == unicode.ReplacementChar {
			return "", apperr.New(apperr.BadRequest, "displayName contains invalid characters")
		}
	}
	return name, nil
}

// SetDisplayName validates and sets the display name.
func SetDisplayName(p *Profile, name string) error {
	n, err := ValidateDisplayName(name)
	if err != nil {
		return err
	}
	p.DisplayName = n
	return nil
}

func (p *Profile) ownedVehicle(gd *data.GameData, id string) (*VehicleState, *data.Vehicle, error) {
	def, ok := gd.Vehicle(id)
	if !ok {
		return nil, nil, apperr.New(apperr.NotFound, "unknown vehicle %q", id)
	}
	v := p.Vehicle(id)
	if v == nil {
		return nil, nil, apperr.New(apperr.NotOwned, "vehicle %q is not owned", id)
	}
	return v, def, nil
}

func (p *Profile) checkLevel(required int) error {
	if p.Level < required {
		return apperr.New(apperr.LevelLocked, "requires level %d", required)
	}
	return nil
}

// BuyVehicle buys a vehicle at its garage price (unlockLevel applies).
func BuyVehicle(p *Profile, gd *data.GameData, vehicleID string) error {
	def, ok := gd.Vehicle(vehicleID)
	if !ok {
		return apperr.New(apperr.NotFound, "unknown vehicle %q", vehicleID)
	}
	if p.Vehicle(vehicleID) != nil {
		return apperr.New(apperr.AlreadyOwned, "vehicle %q is already owned", vehicleID)
	}
	if err := p.checkLevel(def.UnlockLevel); err != nil {
		return err
	}
	if err := garagePrice(def.Price); err != nil {
		return err
	}
	if err := p.Pay(def.Price); err != nil {
		return err
	}
	p.addVehicle(vehicleID)
	return nil
}

// SelectVehicle selects an owned vehicle.
func SelectVehicle(p *Profile, gd *data.GameData, vehicleID string) error {
	if _, _, err := p.ownedVehicle(gd, vehicleID); err != nil {
		return err
	}
	p.Garage.Selected = vehicleID
	return nil
}

// Upgrade buys the next stage of an upgrade category for an owned vehicle and
// returns the credits paid. Cost: roundTo100(levels[L-1].cost * tierCostMultiplier[tier-1]).
func Upgrade(p *Profile, gd *data.GameData, vehicleID, category string) (int64, error) {
	v, def, err := p.ownedVehicle(gd, vehicleID)
	if err != nil {
		return 0, err
	}
	cat, ok := gd.UpgradeCategory(category)
	if !ok {
		return 0, apperr.New(apperr.BadRequest, "unknown upgrade category %q", category)
	}
	next := v.Upgrades[category] + 1
	if next > data.MaxUpgradeLevel || next > len(cat.Levels) {
		return 0, apperr.New(apperr.MaxLevel, "%s is already at the maximum level", category)
	}
	cost, err := gd.UpgradeCost(category, next, def.Tier)
	if err != nil {
		return 0, apperr.New(apperr.Internal, "%v", err)
	}
	if err := p.Pay(data.CreditsPrice(cost)); err != nil {
		return 0, err
	}
	v.Upgrades[category] = next
	return cost, nil
}

// BuyEquipment buys an equipment item (unlockLevel applies). Equipment is an
// unlock: once owned it can be fitted to any number of vehicles.
func BuyEquipment(p *Profile, gd *data.GameData, itemID string) error {
	def, ok := gd.EquipmentItem(itemID)
	if !ok {
		return apperr.New(apperr.NotFound, "unknown equipment %q", itemID)
	}
	if p.OwnsEquipment(gd, itemID) {
		return apperr.New(apperr.AlreadyOwned, "equipment %q is already owned", itemID)
	}
	if err := p.checkLevel(def.UnlockLevel); err != nil {
		return err
	}
	if err := garagePrice(def.Price); err != nil {
		return err
	}
	if err := p.Pay(def.Price); err != nil {
		return err
	}
	p.Inventory.Equipment = append(p.Inventory.Equipment, itemID)
	return nil
}

// Equip fits (itemID != nil) or clears (itemID == nil) a slot of an owned
// vehicle. The item's slot must match and the vehicle class must be allowed.
func Equip(p *Profile, gd *data.GameData, vehicleID, slot string, itemID *string) error {
	v, def, err := p.ownedVehicle(gd, vehicleID)
	if err != nil {
		return err
	}
	validSlot := false
	for _, s := range data.EquipmentSlots {
		if s == slot {
			validSlot = true
		}
	}
	if !validSlot {
		return apperr.New(apperr.InvalidSlot, "unknown slot %q", slot)
	}
	if itemID == nil || *itemID == "" {
		delete(v.Equipment, slot)
		return nil
	}
	item, ok := gd.EquipmentItem(*itemID)
	if !ok {
		return apperr.New(apperr.NotFound, "unknown equipment %q", *itemID)
	}
	if !p.OwnsEquipment(gd, item.ID) {
		return apperr.New(apperr.NotOwned, "equipment %q is not owned", item.ID)
	}
	if item.Slot != slot {
		return apperr.New(apperr.InvalidSlot, "equipment %q goes into slot %q, not %q", item.ID, item.Slot, slot)
	}
	if !item.AllowsClass(def.Class) {
		return apperr.New(apperr.Forbidden, "equipment %q cannot be fitted to a %s", item.ID, def.Class)
	}
	v.Equipment[slot] = item.ID
	return nil
}

// BuyCosmetic buys a cosmetic of any kind.
func BuyCosmetic(p *Profile, gd *data.GameData, itemID string) error {
	def, ok := gd.Cosmetic(itemID)
	if !ok {
		return apperr.New(apperr.NotFound, "unknown cosmetic %q", itemID)
	}
	if p.OwnsCosmetic(gd, itemID) {
		return apperr.New(apperr.AlreadyOwned, "cosmetic %q is already owned", itemID)
	}
	if err := garagePrice(def.Price); err != nil {
		return err
	}
	if err := p.Pay(def.Price); err != nil {
		return err
	}
	p.Inventory.Cosmetics = append(p.Inventory.Cosmetics, itemID)
	return nil
}

// Customize sets paint and/or wheels of an owned vehicle. nil leaves a value
// unchanged; "" restores the factory default.
func Customize(p *Profile, gd *data.GameData, vehicleID string, paint, wheels *string) error {
	v, _, err := p.ownedVehicle(gd, vehicleID)
	if err != nil {
		return err
	}
	check := func(id *string, kind string) error {
		if id == nil || *id == "" {
			return nil
		}
		c, ok := gd.Cosmetic(*id)
		if !ok || c.Kind != kind {
			return apperr.New(apperr.NotFound, "unknown %s %q", kind, *id)
		}
		if !p.OwnsCosmetic(gd, *id) {
			return apperr.New(apperr.NotOwned, "%s %q is not owned", kind, *id)
		}
		return nil
	}
	if err := check(paint, data.ItemPaint); err != nil {
		return err
	}
	if err := check(wheels, data.ItemWheels); err != nil {
		return err
	}
	if paint != nil {
		v.Paint = *paint
	}
	if wheels != nil {
		v.Wheels = *wheels
	}
	return nil
}

// garagePrice rejects items the garage does not sell: it takes credits only;
// gold-priced items come from store offers or rewards.
func garagePrice(price data.Price) error {
	if price.Currency != data.Credits && price.Amount > 0 {
		return apperr.New(apperr.Forbidden, "not sold in the garage (store or rewards only)")
	}
	return nil
}
