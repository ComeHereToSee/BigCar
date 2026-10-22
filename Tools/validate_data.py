#!/usr/bin/env python3
"""BigCar content validator.

Enforces docs/DATA_SCHEMA.md for everything under Content/BigCar/Data: JSON syntax, the top-level version,
ids, enums, numeric ranges, cross references between files, arena geometry, campaign integrity, store / season pass
integrity and localization coverage (every referenced key must exist in zh-Hans and en).

Usage:  python3 Tools/validate_data.py [--data-dir PATH] [--strict] [--quiet]

Exit code 0 = no errors (warnings allowed, unless --strict), 1 = errors found.
Standard library only (Python 3.11+).
"""
from __future__ import annotations

import argparse
import json
import math
import re
import sys
from collections import Counter
from pathlib import Path

# ---------------------------------------------------------------------------------------------------------------
# Schema constants (keep in sync with docs/DATA_SCHEMA.md)

ID_RE = re.compile(r"^[a-z0-9_]+$")
COLOR_RE = re.compile(r"^#[0-9A-Fa-f]{6}$")
LOC_KEY_RE = re.compile(r"^[a-z][a-z0-9_]*(\.[a-z0-9_]+)+$")
ISO_RE = re.compile(r"^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z$")
PLACEHOLDER_RE = re.compile(r"\{\d+\}")

LOC_PREFIXES = ("ui.", "veh.", "brand.", "equip.", "upg.", "cos.", "arena.", "mode.", "camp.", "dlg.", "store.", "rank.", "ai.")
LOC_FIELDS = ("name", "description", "title", "text")
ENGINE_OWNED_FILES = {"graphics.json"}          # schema owned by the engine team; only syntax + version checked here
ENGINE_OWNED_PREFIX = "ui."                     # keys owned by the engine team (Localization/*/ui.json)
REQUIRED_FILES = ("brands", "vehicles", "upgrades", "equipment", "cosmetics", "arenas", "modes", "ai", "campaign",
                  "progression", "store")
LANGS = ("zh-Hans", "en")

VEHICLE_CLASSES = {"compact", "hatchback", "sedan", "wagon", "coupe", "muscle", "sports", "supercar", "hypercar", "luxury",
                   "suv", "pickup", "offroad", "van", "truck"}
DRIVETRAINS = {"fwd", "rwd", "awd"}
ENGINE_LAYOUTS = {"front", "mid", "rear"}
ARMOR_ZONES = ("front", "rear", "left", "right", "top", "bottom")
UPGRADE_CATEGORIES = ("engine", "transmission", "suspension", "tires", "brakes", "chassis", "armor", "weight", "nitro")
MUL_MODS = {"powerMul", "torqueMul", "gripMul", "brakeMul", "steerMul", "topSpeedMul", "massMul", "toughnessMul",
            "ramPowerMul", "boostAccelMul", "boostRegenMul", "downforceMul", "dragMul", "suspensionMul", "wheelHpMul",
            "engineHpMul", "selfDamageMul", "sideDamageMul", "rearDamageMul"}
# additive mods and their allowed ranges (DATA_SCHEMA "Units of the additive keys")
ADD_MODS = {"massAddKg": (-300, 600), "boostCapacityAdd": (0, 100), "cgHeightAdd": (-0.1, 0.1), "launchPower": (0, 3),
            "tireDamage": (0, 3), "repairPerSec": (0.005, 0.02), "knockback": (0, 5)}
ARMOR_MODS = {"armorFront", "armorRear", "armorLeft", "armorRight", "armorTop", "armorBottom", "armorAll"}
EQUIP_SLOTS = ("front", "rear", "side", "roof", "wheels", "core", "ability")
RARITIES = {"common", "rare", "epic", "legendary"}
SHAPES = {"none", "ram_bar", "wedge", "plow", "spikes", "cage", "side_plates", "side_blades", "rear_bumper",
          "rear_spikes", "roof_armor", "wheel_spikes", "ballast"}
ABILITIES = ("nitro_burst", "hydraulic_jump", "ram_charge", "brace", "field_repair", "shock_bumper")
RANGED_KEYS = {"range", "projectile", "projectiles", "ammo", "fireRate"}
FINISHES = {"gloss", "metallic", "matte", "chrome", "pearl", "carbon"}
COSMETIC_CATS = {"paints": "paint", "wheels": "wheels", "sparks": "sparks", "horns": "horn", "titles": "title", "banners": "banner"}
THEMES = {"junkyard", "stadium", "docks", "desert", "snow", "city", "industrial", "highway", "rooftop", "quarry",
          "airfield", "neon"}
ARENA_SHAPES = {"rect", "circle", "octagon"}
SURFACES = {"asphalt", "concrete", "dirt", "sand", "metal", "ice", "grass", "snow"}
PRESETS = {"day", "sunset", "dusk", "night", "overcast", "storm"}
PROP_TYPES = {"ramp", "block", "cylinder", "jump_pad", "boost_pad", "spinner", "crusher", "pit", "crate_stack", "barrel",
              "wreck", "container", "fence", "hill_zone", "light"}
NON_SOLID_PROPS = {"hill_zone", "light"}
DESTRUCTIBLE_PROPS = {"crate_stack", "barrel", "fence", "wreck"}
WIN_CONDITIONS = {"last_standing", "score", "takedowns", "hill_time", "survive", "objective"}
REQUIRED_MODES = ("derby", "takedown", "team_takedown", "team_elimination", "ring_out", "king_of_hill", "bounty",
                  "survival", "destruction", "boss", "explore")
CAMPAIGN_ONLY_MODES = ("survival", "destruction", "boss", "explore")
REQUIRED_AI = ("rookie", "regular", "veteran", "elite", "boss")
AI_FIELDS = ("skill", "aggression", "reaction", "aimLead", "flankBias", "abilityUse", "boostUse", "retreatHealth",
             "mistakeRate", "focusPlayer")
OBJECTIVE_TYPES = {"win", "takedowns", "time_under", "no_wreck", "damage_under", "score", "hits_of_kind", "survive",
                   "destroy_props", "hill_time", "collect", "boss"}
VALUED_OBJECTIVES = {"takedowns", "time_under", "damage_under", "score", "hits_of_kind", "survive", "destroy_props",
                     "hill_time", "collect"}
IMPACT_KINDS = ("head_on", "t_bone", "side_swipe", "rear_end", "launch", "crush", "pit", "glance")
ITEM_TYPES = {"vehicle", "equipment", "paint", "wheels", "sparks", "horn", "title", "banner", "currency"}
CURRENCIES = {"credits", "gold"}
RANK_ORDER = ("bronze", "silver", "gold", "platinum", "diamond", "master", "crash_king")
SCORE_KEYS = IMPACT_KINDS + ("takedown", "assist", "ring_out", "revenge", "multi_takedown", "airtime_per_sec",
                             "hill_per_sec", "survival_per_sec", "prop_destroyed", "combo_step", "combo_max",
                             "combo_window", "min_damage_for_points")
MATCH_KEYS = ("participationCredits", "participationXp", "creditsPerScore", "xpPerScore", "winMultiplier",
              "rankedMultiplier", "maxCredits", "maxXp")
STORE_SECTIONS = {"featured", "vehicles", "equipment", "cosmetics", "bundles"}
TIER_PRICE_BANDS = {1: (0, 40_000), 2: (40_000, 120_000), 3: (120_000, 350_000), 4: (350_000, 1_200_000),
                    5: (1_500_000, 4_000_000)}
WALL_MARGIN = 1.0      # props must stay this far inside the walls / platform edge (m)
SPAWN_MARGIN = 4.0     # spawns must stay this far inside (m)
SPAWN_CLEARANCE = 2.5  # free space around a spawn point (m)


# ---------------------------------------------------------------------------------------------------------------
# Reporting

class Report:
    def __init__(self) -> None:
        self.errors: list[str] = []
        self.warnings: list[str] = []

    def err(self, where: str, msg: str) -> None:
        self.errors.append(f"{where}: {msg}")

    def warn(self, where: str, msg: str) -> None:
        self.warnings.append(f"{where}: {msg}")


R = Report()


def is_num(v) -> bool:
    return isinstance(v, (int, float)) and not isinstance(v, bool) and math.isfinite(v)


def is_int(v) -> bool:
    return isinstance(v, int) and not isinstance(v, bool)


def num(where: str, obj: dict, key: str, lo=None, hi=None, required=True, integer=False, default=None):
    """Validates a numeric field. Returns the value (or default)."""
    if not isinstance(obj, dict) or key not in obj:
        if required:
            R.err(where, f"missing '{key}'")
        return default
    v = obj[key]
    if not (is_int(v) if integer else is_num(v)):
        R.err(where, f"'{key}' must be {'an integer' if integer else 'a number'}, got {v!r}")
        return default
    if lo is not None and v < lo:
        R.err(where, f"'{key}' = {v} is below {lo}")
    if hi is not None and v > hi:
        R.err(where, f"'{key}' = {v} is above {hi}")
    return v


def typed(where: str, obj: dict, key: str, typ, required=True, nullable=False):
    if not isinstance(obj, dict) or key not in obj:
        if required:
            R.err(where, f"missing '{key}'")
        return None
    v = obj[key]
    if v is None and nullable:
        return None
    if typ is bool:
        ok = isinstance(v, bool)
    elif typ is int:
        ok = is_int(v)
    else:
        ok = isinstance(v, typ)
    if not ok:
        R.err(where, f"'{key}' has wrong type ({type(v).__name__}), expected {getattr(typ, '__name__', typ)}")
        return None
    return v


def enum(where: str, obj: dict, key: str, allowed, required=True):
    v = typed(where, obj, key, str, required)
    if v is not None and v not in allowed:
        R.err(where, f"'{key}' = {v!r} is not one of {sorted(allowed)}")
        return None
    return v


def color(where: str, v, what="color") -> None:
    if not isinstance(v, str) or not COLOR_RE.match(v):
        R.err(where, f"{what} {v!r} is not a #RRGGBB string")


def check_price(where: str, p, allow_zero=True) -> None:
    if not isinstance(p, dict) or len(p) != 1 or next(iter(p)) not in CURRENCIES:
        R.err(where, f"price {p!r} must be an object with exactly one key 'credits' or 'gold'")
        return
    amount = next(iter(p.values()))
    if not is_int(amount) or amount < 0 or (amount == 0 and not allow_zero):
        R.err(where, f"price amount {amount!r} must be a {'non-negative' if allow_zero else 'positive'} integer")


def loc_key(where: str, v, prefixes=LOC_PREFIXES, nullable=False) -> None:
    if v is None and nullable:
        return
    if not isinstance(v, str) or not LOC_KEY_RE.match(v):
        R.err(where, f"localization key {v!r} is malformed")
    elif not v.startswith(prefixes):
        R.err(where, f"localization key {v!r} must start with one of {', '.join(prefixes)}")


class Ids:
    """Per-file id uniqueness plus a global namespace for store-referencable items."""

    def __init__(self) -> None:
        self.global_items: dict[str, str] = {}

    def check(self, where: str, items, seen: set, global_ns: str | None = None):
        out = {}
        if not isinstance(items, list):
            R.err(where, "must be a list")
            return out
        for i, it in enumerate(items):
            w = f"{where}[{i}]"
            if not isinstance(it, dict):
                R.err(w, "must be an object")
                continue
            iid = it.get("id")
            if not isinstance(iid, str) or not ID_RE.match(iid):
                R.err(w, f"id {iid!r} must match ^[a-z0-9_]+$")
                continue
            if iid in seen:
                R.err(w, f"duplicate id '{iid}'")
            seen.add(iid)
            if global_ns:
                prev = self.global_items.get(iid)
                if prev and prev != global_ns:
                    R.err(w, f"id '{iid}' is also used by {prev} (store item ids must be globally unique)")
                self.global_items.setdefault(iid, global_ns)
            out[iid] = it
        return out


IDS = Ids()


# ---------------------------------------------------------------------------------------------------------------
# Loading

def _no_dup_pairs(where):
    def hook(pairs):
        d = {}
        for k, v in pairs:
            if k in d:
                R.err(where, f"duplicate JSON key '{k}'")
            d[k] = v
        return d
    return hook


def load_json(path: Path, rel: str):
    try:
        raw = path.read_bytes()
    except OSError as e:
        R.err(rel, f"cannot read: {e}")
        return None
    if raw.startswith(b"\xef\xbb\xbf"):
        R.err(rel, "file starts with a UTF-8 BOM")
        raw = raw[3:]
    try:
        text = raw.decode("utf-8")
    except UnicodeDecodeError as e:
        R.err(rel, f"not valid UTF-8: {e}")
        return None
    try:
        data = json.loads(text, object_pairs_hook=_no_dup_pairs(rel))
    except json.JSONDecodeError as e:
        R.err(rel, f"invalid JSON: {e}")
        return None
    if not isinstance(data, dict):
        R.err(rel, "top level must be an object")
        return None
    if data.get("version") != 1:
        R.err(rel, f"top-level 'version' must be 1, got {data.get('version')!r}")
    if "\t" in text.split("\n", 1)[0] or (len(text) > 200 and "\n" not in text.strip()):
        R.warn(rel, "file does not look pretty-printed (2-space indent expected)")
    return data


def load_localization(data_dir: Path):
    """Returns {lang: {key: text}} merged per language folder; reports duplicates across files."""
    loc_root = data_dir / "Localization"
    langs: dict[str, dict[str, str]] = {}
    origin: dict[str, dict[str, str]] = {}
    if not loc_root.is_dir():
        R.err("Localization", "folder is missing")
        return langs
    for lang_dir in sorted(p for p in loc_root.iterdir() if p.is_dir()):
        lang = lang_dir.name
        langs[lang] = {}
        origin[lang] = {}
        for f in sorted(lang_dir.glob("*.json")):
            rel = f"Localization/{lang}/{f.name}"
            d = load_json(f, rel)
            if d is None:
                continue
            if d.get("lang") != lang:
                R.err(rel, f"'lang' is {d.get('lang')!r}, expected '{lang}'")
            strings = d.get("strings")
            if not isinstance(strings, dict):
                R.err(rel, "'strings' must be an object")
                continue
            for k, v in strings.items():
                if not isinstance(k, str) or not LOC_KEY_RE.match(k):
                    R.err(rel, f"malformed key {k!r}")
                if not isinstance(v, str) or not v.strip():
                    R.err(rel, f"key '{k}' must map to a non-empty string")
                    continue
                if f.name != "ui.json" and k.startswith(ENGINE_OWNED_PREFIX):
                    R.warn(rel, f"key '{k}' uses the engine-owned 'ui.' prefix outside ui.json")
                if k in langs[lang]:
                    R.err(rel, f"duplicate key '{k}' (already defined in {origin[lang][k]})")
                    continue
                langs[lang][k] = v
                origin[lang][k] = f.name
    for lang in LANGS:
        if lang not in langs:
            R.err("Localization", f"language folder '{lang}' is missing")
    return langs


# ---------------------------------------------------------------------------------------------------------------
# Content files

def validate_brands(d) -> dict:
    brands = IDS.check("brands.json brands", d.get("brands"), set())
    for bid, b in brands.items():
        w = f"brands.json brand '{bid}'"
        loc_key(w, b.get("name"), ("brand.",))
        c = typed(w, b, "country", str)
        if c is not None and not re.match(r"^[A-Z]{2}$", c):
            R.err(w, f"country {c!r} must be an ISO 3166 alpha-2 code")
        color(w, b.get("color"))
        typed(w, b, "licensed", bool)
        ref = typed(w, b, "reference", str)
        if ref is not None and not ref.strip():
            R.err(w, "'reference' must name the real-world brand")
    return brands


def validate_vehicles(d, brands) -> dict:
    vehicles = IDS.check("vehicles.json vehicles", d.get("vehicles"), set(), "vehicles.json")
    refs = Counter()
    for vid, v in vehicles.items():
        w = f"vehicles.json vehicle '{vid}'"
        if v.get("brand") not in brands:
            R.err(w, f"unknown brand {v.get('brand')!r}")
        loc_key(w, v.get("name"), ("veh.",))
        ref = typed(w, v, "reference", str)
        if ref:
            refs[ref] += 1
        cls = enum(w, v, "class", VEHICLE_CLASSES)
        if "body" in v:
            enum(w, v, "body", VEHICLE_CLASSES)
        tier = num(w, v, "tier", 1, 5, integer=True)
        check_price(w, v.get("price"))
        num(w, v, "unlockLevel", 1, None, integer=True)
        starter = typed(w, v, "starter", bool, required=False)
        price = v.get("price") if isinstance(v.get("price"), dict) else {}
        if starter and price != {"credits": 0}:
            R.err(w, "starter vehicles must cost { \"credits\": 0 }")
        if tier in TIER_PRICE_BANDS and "credits" in price and not starter:
            lo, hi = TIER_PRICE_BANDS[tier]
            if not lo <= price["credits"] <= hi:
                R.warn(w, f"price {price['credits']} is outside the tier {tier} band {lo}-{hi}")
        enum(w, v, "drivetrain", DRIVETRAINS)
        enum(w, v, "engineLayout", ENGINE_LAYOUTS)
        mass = num(w, v, "massKg", 300, 40000)
        num(w, v, "powerKw", 1, 3000)
        num(w, v, "torqueNm", 1, 30000)
        idle = num(w, v, "idleRpm", 0, 5000)
        red = num(w, v, "redlineRpm", 500, 30000)
        if is_num(idle) and is_num(red) and idle >= red:
            R.err(w, "idleRpm must be below redlineRpm")
        gears = v.get("gears")
        if not isinstance(gears, list) or not 1 <= len(gears) <= 10 or not all(is_num(g) and g > 0 for g in gears):
            R.err(w, "gears must be a list of 1-10 positive ratios")
        elif any(gears[i] >= gears[i - 1] for i in range(1, len(gears))):
            R.err(w, f"gears must be strictly decreasing: {gears}")
        num(w, v, "reverseRatio", 0.01, 30)
        num(w, v, "finalDrive", 0.01, 30)
        num(w, v, "topSpeedKmh", 10, 600)
        dims = v.get("dimensions")
        height = None
        if not isinstance(dims, dict):
            R.err(w, "missing 'dimensions' object")
        else:
            for k in ("length", "width", "height", "wheelbase", "trackFront", "trackRear", "groundClearance",
                      "wheelRadius", "wheelWidth"):
                x = num(w + " dimensions", dims, k, 0.0001, 50)
            L_, W_, H_ = dims.get("length"), dims.get("width"), dims.get("height")
            height = H_
            if is_num(dims.get("wheelbase")) and is_num(L_) and dims["wheelbase"] >= L_:
                R.err(w, "dimensions.wheelbase must be smaller than length")
            for k in ("trackFront", "trackRear"):
                if is_num(dims.get(k)) and is_num(W_) and dims[k] >= W_:
                    R.err(w, f"dimensions.{k} must be smaller than width")
            if is_num(dims.get("groundClearance")) and is_num(H_) and dims["groundClearance"] >= H_:
                R.err(w, "dimensions.groundClearance must be smaller than height")
            if is_num(dims.get("wheelRadius")) and is_num(H_) and dims["wheelRadius"] * 2 > H_:
                R.err(w, "wheel diameter exceeds the vehicle height")
        cg = num(w, v, "cgHeight", 0.01, 5, required=False)
        if is_num(cg) and is_num(height) and cg >= height:
            R.err(w, "cgHeight must be below the roof (height)")
        num(w, v, "frontWeight", 0.3, 0.7, required=False)
        num(w, v, "grip", 0.6, 1.6, required=False)
        num(w, v, "brakeG", 0.5, 1.6, required=False)
        num(w, v, "steerLockDeg", 15, 55, required=False)
        s = v.get("suspension")
        if isinstance(s, dict):
            num(w + " suspension", s, "frequency", 0.8, 3.0)
            num(w + " suspension", s, "damping", 0.2, 0.9)
            num(w + " suspension", s, "travel", 0.01, 1.0)
            num(w + " suspension", s, "antiRoll", 0, 1.5)
        elif "suspension" in v:
            R.err(w, "'suspension' must be an object")
        num(w, v, "dragCoefficient", 0.2, 0.8, required=False)
        num(w, v, "downforce", 0, None, required=False)
        num(w, v, "toughness", 0.5, 2.5, required=False)
        armor = v.get("armor", {})
        if not isinstance(armor, dict):
            R.err(w, "'armor' must be an object")
        else:
            for z, a in armor.items():
                if z not in ARMOR_ZONES:
                    R.err(w, f"unknown armor zone '{z}'")
                else:
                    num(w + " armor", armor, z, 0, 0.6)
        num(w, v, "ramPower", 0.5, 2.0, required=False)
        b = v.get("boost")
        if isinstance(b, dict):
            num(w + " boost", b, "capacity", 0, 1000)
            num(w + " boost", b, "accel", 0, 50)
            num(w + " boost", b, "regenPerSec", 0, 100)
        elif "boost" in v:
            R.err(w, "'boost' must be an object")
        cols = v.get("colors")
        if not isinstance(cols, list) or not cols:
            R.err(w, "'colors' must be a non-empty list (first = factory paint)")
        else:
            for c in cols:
                color(w, c)
        tags = v.get("tags", [])
        if not isinstance(tags, list) or not all(isinstance(t, str) and t for t in tags):
            R.err(w, "'tags' must be a list of strings")
        if mass is not None and cls in ("hypercar", "supercar") and mass > 2600:
            R.warn(w, f"unusually heavy {cls} ({mass} kg)")
    for ref, n in refs.items():
        if n > 1:
            R.warn("vehicles.json", f"{n} vehicles share the reference '{ref}'")
    if not any(v.get("starter") for v in vehicles.values()):
        R.err("vehicles.json", "at least one vehicle must be a starter")
    return vehicles


def validate_mods(where: str, mods) -> None:
    if not isinstance(mods, dict):
        R.err(where, "'mods' must be an object")
        return
    for k, v in mods.items():
        if not is_num(v):
            R.err(where, f"mod '{k}' must be a number")
            continue
        if k in MUL_MODS:
            if not 0.1 <= v <= 5.0:
                R.err(where, f"multiplier '{k}' = {v} outside 0.1-5.0")
        elif k in ADD_MODS:
            lo, hi = ADD_MODS[k]
            if not lo <= v <= hi:
                R.err(where, f"'{k}' = {v} outside the allowed range {lo}..{hi}")
        elif k in ARMOR_MODS:
            if not -0.75 <= v <= 0.75:
                R.err(where, f"armor mod '{k}' = {v} outside -0.75..0.75")
        else:
            R.err(where, f"unknown mod key '{k}'")


def validate_upgrades(d) -> dict:
    w = "upgrades.json"
    tcm = d.get("tierCostMultiplier")
    if not isinstance(tcm, list) or len(tcm) != 5 or not all(is_num(x) and x > 0 for x in tcm):
        R.err(w, "'tierCostMultiplier' must be 5 positive numbers (tiers 1-5)")
    cats = IDS.check(f"{w} categories", d.get("categories"), set())
    for cid, c in cats.items():
        cw = f"{w} category '{cid}'"
        if cid not in UPGRADE_CATEGORIES:
            R.err(cw, f"unknown category (allowed: {', '.join(UPGRADE_CATEGORIES)})")
        loc_key(cw, c.get("name"), ("upg.",))
        if "description" in c:
            loc_key(cw, c.get("description"), ("upg.",))
        levels = c.get("levels")
        if not isinstance(levels, list) or len(levels) != 5:
            R.err(cw, "'levels' must have exactly 5 entries")
            continue
        prev = 0
        for i, lv in enumerate(levels, 1):
            lw = f"{cw} level {i}"
            cost = num(lw, lv, "cost", 1, None, integer=True)
            if is_int(cost):
                if cost <= prev:
                    R.err(lw, "costs must increase with the level")
                prev = cost
            validate_mods(lw, lv.get("mods"))
            if isinstance(lv.get("mods"), dict) and not lv["mods"]:
                R.warn(lw, "level has no mods")
    for c in UPGRADE_CATEGORIES:
        if c not in cats:
            R.err(w, f"missing upgrade category '{c}'")
    return cats


def find_ranged(obj, path=""):
    if isinstance(obj, dict):
        for k, v in obj.items():
            if k in RANGED_KEYS:
                yield f"{path}.{k}" if path else k
            yield from find_ranged(v, f"{path}.{k}" if path else k)
    elif isinstance(obj, list):
        for i, v in enumerate(obj):
            yield from find_ranged(v, f"{path}[{i}]")


def validate_equipment(d) -> dict:
    items = IDS.check("equipment.json equipment", d.get("equipment"), set(), "equipment.json")
    slots, abilities = Counter(), Counter()
    for iid, e in items.items():
        w = f"equipment.json item '{iid}'"
        slot = enum(w, e, "slot", set(EQUIP_SLOTS))
        slots[slot] += 1
        loc_key(w, e.get("name"), ("equip.",))
        loc_key(w, e.get("description"), ("equip.",))
        enum(w, e, "rarity", RARITIES)
        if e.get("contactOnly") is not True:
            R.err(w, "'contactOnly' must be true (melee-only design: no ranged equipment)")
        for p in find_ranged(e):
            R.err(w, f"ranged field '{p}' is forbidden")
        g = e.get("geometry")
        if not isinstance(g, dict):
            R.err(w, "missing 'geometry' object")
        else:
            shape = enum(w + " geometry", g, "shape", SHAPES)
            num(w + " geometry", g, "length", 0, 2.0)
            if shape in ("wedge", "plow"):
                num(w + " geometry", g, "angleDeg", 1, 60)
            elif "angleDeg" in g:
                num(w + " geometry", g, "angleDeg", 0, 60)
            if "color" in g:
                color(w + " geometry", g["color"])
        validate_mods(w, e.get("mods"))
        ab = e.get("ability", None)
        if ab is not None:
            if ab not in ABILITIES:
                R.err(w, f"unknown ability {ab!r}")
            if slot != "ability":
                R.err(w, "only items in slot 'ability' may set 'ability'")
            abilities[ab] += 1
        elif slot == "ability":
            R.err(w, "items in slot 'ability' must set an ability")
        num(w, e, "cr", 0, None, integer=True)
        check_price(w, e.get("price"))
        num(w, e, "unlockLevel", 1, None, integer=True)
        ac = e.get("allowedClasses", [])
        if not isinstance(ac, list) or any(c not in VEHICLE_CLASSES for c in ac):
            R.err(w, f"'allowedClasses' must be a list of vehicle classes, got {ac!r}")
        mass_add = e.get("mods", {}).get("massAddKg", 0) if isinstance(e.get("mods"), dict) else 0
        armor_sum = sum(v for k, v in (e.get("mods") or {}).items() if k in ARMOR_MODS and is_num(v))
        if armor_sum >= 0.2 and is_num(mass_add) and mass_add <= 0:
            R.warn(w, "heavy armor without any weight penalty")
    for s in EQUIP_SLOTS:
        if not slots[s]:
            R.err("equipment.json", f"no item for slot '{s}'")
    for a in ABILITIES:
        if not abilities[a]:
            R.err("equipment.json", f"no ability item grants '{a}'")
    return items


def validate_cosmetics(d) -> dict:
    out: dict[str, dict] = {}
    seen: set = set()
    for cat, typ in COSMETIC_CATS.items():
        items = IDS.check(f"cosmetics.json {cat}", d.get(cat), seen, "cosmetics.json")
        out[typ] = items
        for iid, c in items.items():
            w = f"cosmetics.json {cat} '{iid}'"
            loc_key(w, c.get("name"), ("cos.",))
            enum(w, c, "rarity", RARITIES)
            check_price(w, c.get("price"))
            if cat in ("paints", "wheels", "sparks"):
                color(w, c.get("color"))
            if cat == "paints":
                enum(w, c, "finish", FINISHES)
            if cat == "wheels":
                num(w, c, "spokes", 0, 20, integer=True)
            if cat == "horns":
                fr = c.get("frequencies")
                if not isinstance(fr, list) or not 1 <= len(fr) <= 8 or not all(is_num(f) and 50 <= f <= 5000 for f in fr):
                    R.err(w, "'frequencies' must be 1-8 tones between 50 and 5000 Hz")
            if cat == "banners":
                cols = c.get("colors")
                if not isinstance(cols, list) or not cols:
                    R.err(w, "'colors' must be a non-empty list")
                else:
                    for col in cols:
                        color(w, col)
    for cat in d:
        if cat != "version" and cat not in COSMETIC_CATS:
            R.warn("cosmetics.json", f"unknown cosmetics category '{cat}'")
    return out


class Items:
    """Resolves reward / store item references."""

    def __init__(self, vehicles, equipment, cosmetics):
        self.tables = {"vehicle": vehicles, "equipment": equipment}
        self.tables.update(cosmetics)

    def get(self, typ, iid):
        return self.tables.get(typ, {}).get(iid)

    def check(self, where: str, item) -> None:
        if not isinstance(item, dict):
            R.err(where, f"item {item!r} must be an object")
            return
        typ = item.get("type")
        if typ not in ITEM_TYPES:
            R.err(where, f"item type {typ!r} is not one of {sorted(ITEM_TYPES)}")
            return
        if typ == "currency":
            if item.get("id") not in CURRENCIES:
                R.err(where, f"currency id {item.get('id')!r} must be 'credits' or 'gold'")
            amt = item.get("amount")
            if not is_int(amt) or amt <= 0:
                R.err(where, f"currency amount {amt!r} must be a positive integer")
            return
        if "amount" in item and item["amount"] != 1:
            R.warn(where, f"'{typ}' item '{item.get('id')}' has amount {item['amount']} (only currency stacks)")
        if self.get(typ, item.get("id")) is None:
            R.err(where, f"unknown {typ} '{item.get('id')}'")

    def check_list(self, where: str, items, allow_empty=True) -> None:
        if not isinstance(items, list):
            R.err(where, "items must be a list")
            return
        if not items and not allow_empty:
            R.err(where, "items must not be empty")
        for i, it in enumerate(items):
            self.check(f"{where} item {i}", it)


# ---------------------------------------------------------------------------------------------------------------
# Arenas

def prop_extent(p) -> float:
    """Horizontal bounding radius of a prop footprint (m)."""
    t = p.get("type")
    s = p.get("size")
    if t in ("ramp", "block", "crusher", "jump_pad", "boost_pad", "pit") and isinstance(s, list) and len(s) >= 2 \
            and all(is_num(x) for x in s[:2]):
        return math.hypot(s[0] / 2, s[1] / 2)
    if t in ("cylinder", "spinner", "hill_zone"):
        return p.get("radius", 0) if is_num(p.get("radius")) else 0
    if t == "crate_stack":
        c, e = p.get("count", 1), p.get("size", 1.2)
        return (e if is_num(e) else 1.2) * math.ceil(math.sqrt(c if is_int(c) and c > 0 else 1)) * 0.75
    if t == "barrel":
        c = p.get("count", 1)
        return 1.2 + 0.25 * (c if is_int(c) else 1)
    if t == "wreck":
        return 4.0 if p.get("class") in ("truck", "van", "pickup") else 3.0
    if t == "container":
        return math.hypot(6.1, 1.22)
    return 0.0


def seg_dist(px, py, ax, ay, bx, by) -> float:
    dx, dy = bx - ax, by - ay
    ll = dx * dx + dy * dy
    t = 0.0 if ll == 0 else max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / ll))
    return math.hypot(px - (ax + t * dx), py - (ay + t * dy))


class Bounds:
    """Playable floor. Octagons use the inscribed radius (size[0] is the corner-to-corner diameter) so that
    'inside' is conservative for both readings of the diameter."""

    def __init__(self, shape, size):
        self.shape = shape
        if shape == "rect":
            self.hx, self.hy = size[0] / 2, size[1] / 2
        else:
            r = size[0] / 2
            self.hx = self.hy = r * math.cos(math.radians(22.5)) if shape == "octagon" else r

    def inside(self, x, y, ext=0.0, margin=0.0) -> bool:
        if self.shape == "rect":
            return abs(x) + ext <= self.hx - margin + 1e-6 and abs(y) + ext <= self.hy - margin + 1e-6
        return math.hypot(x, y) + ext <= self.hx - margin + 1e-6


def vec(where, v, n, what="pos") -> bool:
    if not isinstance(v, list) or len(v) != n or not all(is_num(x) for x in v):
        R.err(where, f"'{what}' must be a list of {n} numbers, got {v!r}")
        return False
    return True


def validate_arenas(d) -> dict:
    arenas = IDS.check("arenas.json arenas", d.get("arenas"), set())
    secret_ids: set = set()
    for aid, a in arenas.items():
        w = f"arenas.json arena '{aid}'"
        loc_key(w, a.get("name"), ("arena.",))
        loc_key(w, a.get("description"), ("arena.",))
        enum(w, a, "theme", THEMES)
        hub = typed(w, a, "hub", bool, required=False) or False
        shape = enum(w, a, "shape", ARENA_SHAPES)
        size = a.get("size")
        if not vec(w, size, 2, "size") or shape is None:
            continue
        if size[0] <= 0 or size[1] <= 0:
            R.err(w, "size must be positive")
            continue
        if shape != "rect" and size[0] != size[1]:
            R.warn(w, f"{shape} arenas use size[0] as diameter; size[1] = {size[1]} is ignored")
        across = max(size) if shape == "rect" else size[0]
        if hub and not 300 <= across <= 600:
            R.warn(w, f"hub is {across} m across (design range 300-600 m)")
        if not hub and not 80 <= across <= 220:
            R.warn(w, f"combat arena is {across} m across (design range 80-220 m)")
        walls = typed(w, a, "walls", bool)
        num(w, a, "wallHeight", 0, 30, required=False)
        elev = num(w, a, "elevation", 0, 200, default=0)
        kz = num(w, a, "killZ", -1000, 200, required=False)
        if is_num(kz) and is_num(elev) and kz >= elev:
            R.err(w, f"killZ ({kz}) must be below the floor elevation ({elev})")
        if walls is False and is_num(elev) and elev < 10:
            R.warn(w, "ring-out arena (walls false) should be elevated 10-40 m")
        fl = a.get("floor")
        if isinstance(fl, dict):
            enum(w + " floor", fl, "surface", SURFACES)
            color(w + " floor", fl.get("color"))
            num(w + " floor", fl, "friction", 0.1, 1.5)
        else:
            R.err(w, "missing 'floor' object")
        li = a.get("lighting")
        if isinstance(li, dict):
            enum(w + " lighting", li, "preset", PRESETS)
            num(w + " lighting", li, "fog", 0, 1, required=False)
            if "tint" in li:
                color(w + " lighting", li["tint"])
        else:
            R.err(w, "missing 'lighting' object")
        B = Bounds(shape, size)
        props = a.get("props", [])
        solids = []
        if not isinstance(props, list):
            R.err(w, "'props' must be a list")
            props = []
        for i, p in enumerate(props):
            pw = f"{w} prop {i}"
            t = enum(pw, p, "type", PROP_TYPES)
            if t is None or not vec(pw, p.get("pos"), 3):
                continue
            x, y, z = p["pos"]
            if "yaw" in p:
                num(pw, p, "yaw", -720, 720)
            if t in ("ramp", "block", "crusher"):
                if vec(pw, p.get("size"), 3, "size") and min(p["size"]) <= 0:
                    R.err(pw, "size must be positive")
            if t in ("jump_pad", "boost_pad", "pit"):
                if vec(pw, p.get("size"), 2, "size") and min(p["size"]) <= 0:
                    R.err(pw, "size must be positive")
            if t in ("jump_pad", "boost_pad"):
                num(pw, p, "strength", 0, 60)
            if t == "block":
                if "color" in p:
                    color(pw, p["color"])
                typed(pw, p, "soft", bool, required=False)
            if t == "cylinder":
                num(pw, p, "radius", 0.05, 100); num(pw, p, "height", 0.05, 200)
            if t == "spinner":
                num(pw, p, "radius", 0.5, 50); num(pw, p, "height", 0.1, 10); num(pw, p, "speed", -720, 720)
            if t == "crusher":
                num(pw, p, "period", 0.5, 60); num(pw, p, "travel", 0.1, 20)
            if t == "crate_stack":
                num(pw, p, "count", 1, 20, integer=True); num(pw, p, "size", 0.3, 5)
            if t == "barrel":
                num(pw, p, "count", 1, 8, integer=True)
            if t == "wreck":
                enum(pw, p, "class", VEHICLE_CLASSES)
            if t in ("wreck", "container", "cylinder") and "color" in p:
                color(pw, p["color"])
            if t == "fence":
                if vec(pw, p.get("to"), 3, "to") and not B.inside(p["to"][0], p["to"][1], 0, WALL_MARGIN):
                    R.err(pw, f"fence end {p['to']} is outside the arena")
            if t == "hill_zone":
                num(pw, p, "radius", 2, 60)
            if t == "light":
                color(pw, p.get("color")); num(pw, p, "intensity", 0, 1); num(pw, p, "radius", 1, 500)
            ext = 0.0 if t == "light" else prop_extent(p)
            if not B.inside(x, y, ext, WALL_MARGIN):
                R.err(pw, f"{t} at {p['pos']} (extent {ext:.1f} m) is outside the arena bounds")
            if t not in NON_SOLID_PROPS:
                solids.append(p)
        hills = sum(1 for p in props if isinstance(p, dict) and p.get("type") == "hill_zone")
        # spawns
        spawns = a.get("spawns")
        if not isinstance(spawns, list) or len(spawns) < 12:
            R.err(w, f"needs at least 12 spawns, has {len(spawns) if isinstance(spawns, list) else 0}")
            spawns = spawns if isinstance(spawns, list) else []
        teams = Counter()
        pts = []
        for i, s in enumerate(spawns):
            sw = f"{w} spawn {i}"
            if not isinstance(s, dict) or not vec(sw, s.get("pos"), 2):
                continue
            x, y = s["pos"]
            yaw = num(sw, s, "yaw", -720, 720, required=False, default=0)
            team = s.get("team")
            if team is not None:
                if team not in (0, 1):
                    R.err(sw, f"team must be 0 or 1 (or omitted), got {team!r}")
                teams[team] += 1
            if not B.inside(x, y, 0, SPAWN_MARGIN):
                R.err(sw, f"spawn {s['pos']} is outside the floor (or within {SPAWN_MARGIN} m of the edge)")
            for p in solids:
                px, py = p["pos"][0], p["pos"][1]
                if p["type"] == "fence" and isinstance(p.get("to"), list):
                    dd = seg_dist(x, y, px, py, p["to"][0], p["to"][1]) - 0.5
                else:
                    dd = math.hypot(x - px, y - py) - prop_extent(p)
                if dd < SPAWN_CLEARANCE:
                    R.err(sw, f"spawn {s['pos']} overlaps {p['type']} at {p['pos']}")
            if team is None and is_num(yaw) and math.hypot(x, y) > 1:
                to_center = math.degrees(math.atan2(-y, -x))
                diff = abs((yaw - to_center + 180) % 360 - 180)
                if diff > 60:
                    R.err(sw, f"FFA spawn faces {yaw}° but the arena center is at {to_center:.0f}°")
            pts.append((x, y, i))
        for i in range(len(pts)):
            for j in range(i + 1, len(pts)):
                if math.hypot(pts[i][0] - pts[j][0], pts[i][1] - pts[j][1]) < 5:
                    R.err(w, f"spawns {pts[i][2]} and {pts[j][2]} are closer than 5 m")
        if teams:
            for t in (0, 1):
                if teams[t] < 3:
                    R.err(w, f"team {t} has {teams[t]} spawns (team spawns need at least 3 per team)")
            # opposite sides: team centroids should be well apart
            c = {t: [s["pos"] for s in spawns if isinstance(s, dict) and s.get("team") == t] for t in (0, 1)}
            if c[0] and c[1]:
                c0 = (sum(p[0] for p in c[0]) / len(c[0]), sum(p[1] for p in c[0]) / len(c[0]))
                c1 = (sum(p[0] for p in c[1]) / len(c[1]), sum(p[1] for p in c[1]) / len(c[1]))
                if math.hypot(c0[0] - c1[0], c0[1] - c1[1]) < 0.5 * min(B.hx, B.hy):
                    R.err(w, "team spawns are not on opposite sides of the arena")
        # collectibles / secrets
        cols = a.get("collectibles", [])
        if not isinstance(cols, list) or len(cols) > 10:
            R.err(w, "'collectibles' must be a list of at most 10 positions")
            cols = []
        seen_c = set()
        for i, c in enumerate(cols):
            cw = f"{w} collectible {i}"
            if vec(cw, c, 3):
                if not B.inside(c[0], c[1], 0, WALL_MARGIN):
                    R.err(cw, f"{c} is outside the arena")
                if c[2] < 0:
                    R.err(cw, "z must be >= 0 (relative to the floor)")
                if tuple(c) in seen_c:
                    R.err(cw, "duplicate collectible position")
                seen_c.add(tuple(c))
        secrets = a.get("secrets", [])
        if not isinstance(secrets, list):
            R.err(w, "'secrets' must be a list")
            secrets = []
        for i, s in enumerate(secrets):
            sw = f"{w} secret {i}"
            sid = s.get("id") if isinstance(s, dict) else None
            if not isinstance(sid, str) or not ID_RE.match(sid):
                R.err(sw, f"secret id {sid!r} must match ^[a-z0-9_]+$")
            elif sid in secret_ids:
                R.err(sw, f"duplicate secret id '{sid}'")
            secret_ids.add(sid)
            if isinstance(s, dict) and vec(sw, s.get("pos"), 3):
                if not B.inside(s["pos"][0], s["pos"][1], 0, WALL_MARGIN):
                    R.err(sw, f"{s['pos']} is outside the arena")
                num(sw, s, "radius", 0.5, 100)
        if hub and (len(cols) < 5 or len(secrets) < 3):
            R.warn(w, "hub should offer plenty of collectibles and secrets for exploration")
        a["_hills"] = hills
        a["_destructibles"] = sum((p.get("count", 1) if p.get("type") in ("crate_stack", "barrel") else 1)
                                  for p in props if isinstance(p, dict) and p.get("type") in DESTRUCTIBLE_PROPS)
        a["_teamSpawns"] = dict(teams)
        a["_ffaSpawns"] = sum(1 for s in spawns if isinstance(s, dict) and s.get("team") is None)
    return arenas


# ---------------------------------------------------------------------------------------------------------------
# Modes / AI

def validate_modes(d) -> dict:
    modes = IDS.check("modes.json modes", d.get("modes"), set())
    for mid, m in modes.items():
        w = f"modes.json mode '{mid}'"
        loc_key(w, m.get("name"), ("mode.",))
        loc_key(w, m.get("description"), ("mode.",))
        teams = num(w, m, "teams", 0, 3, integer=True)
        if teams == 1:
            R.err(w, "'teams' must be 0 (free-for-all), 2 or 3")
        respawn = typed(w, m, "respawn", bool)
        num(w, m, "respawnDelay", 0, 60)
        lives = num(w, m, "lives", 0, 99, integer=True)
        num(w, m, "timeLimit", 0, 7200)
        num(w, m, "scoreLimit", 0, None, integer=True)
        rounds = num(w, m, "rounds", 1, 15, integer=True)
        rtw = num(w, m, "roundsToWin", 1, 15, integer=True)
        if is_int(rounds) and is_int(rtw):
            if rtw > rounds:
                R.err(w, "roundsToWin cannot exceed rounds")
            elif rtw * 2 <= rounds:
                R.warn(w, "roundsToWin should be a majority of rounds")
        win = enum(w, m, "win", WIN_CONDITIONS)
        ring = typed(w, m, "ringOut", bool)
        hill = typed(w, m, "hill", bool)
        bounty = typed(w, m, "bounty", bool)
        mn = num(w, m, "minPlayers", 1, 24, integer=True)
        mx = num(w, m, "maxPlayers", 1, 24, integer=True)
        if is_int(mn) and is_int(mx) and mn > mx:
            R.err(w, "minPlayers exceeds maxPlayers")
        online = typed(w, m, "online", bool)
        ranked = typed(w, m, "ranked", bool)
        if ranked and not online:
            R.err(w, "a ranked mode must be online")
        if hill and win != "hill_time":
            R.warn(w, "hill modes normally use win 'hill_time'")
        if win == "hill_time" and not hill:
            R.err(w, "win 'hill_time' requires hill: true")
        if win == "hill_time" and teams == 0:
            R.err(w, "win 'hill_time' counts team hill seconds and needs teams")
        if respawn is False and is_int(lives) and lives != 1:
            R.warn(w, "lives is ignored without respawn (use 1)")
        if respawn and is_int(lives) and lives == 1:
            R.err(w, "respawn with a single life never respawns")
    for r in REQUIRED_MODES:
        if r not in modes:
            R.err("modes.json", f"missing required mode '{r}'")
    for r in CAMPAIGN_ONLY_MODES:
        if r in modes and modes[r].get("online"):
            R.warn("modes.json", f"mode '{r}' is campaign-only and should have online: false")
    return modes


def validate_ai(d) -> dict:
    profiles = IDS.check("ai.json profiles", d.get("profiles"), set())
    for pid, p in profiles.items():
        w = f"ai.json profile '{pid}'"
        loc_key(w, p.get("name"), ("ai.",))
        for f in AI_FIELDS:
            if f == "reaction":
                num(w, p, f, 0.1, 1.0)
            else:
                num(w, p, f, 0, 1)
    for r in REQUIRED_AI:
        if r not in profiles:
            R.err("ai.json", f"missing required profile '{r}'")
    return profiles


# ---------------------------------------------------------------------------------------------------------------
# Campaign

def validate_objective(where, o, ctx) -> None:
    if not isinstance(o, dict):
        R.err(where, "objective must be an object")
        return
    t = o.get("type")
    if t not in OBJECTIVE_TYPES:
        R.err(where, f"objective type {t!r} is not one of {sorted(OBJECTIVE_TYPES)}")
        return
    val = o.get("value")
    if t in VALUED_OBJECTIVES:
        if not is_num(val) or val <= 0:
            R.err(where, f"'{t}' needs a positive 'value'")
            return
    elif "value" in o:
        R.warn(where, f"'{t}' does not use 'value'")
    if t == "hits_of_kind" and o.get("kind") not in IMPACT_KINDS:
        R.err(where, f"hits_of_kind needs 'kind' in {list(IMPACT_KINDS)}, got {o.get('kind')!r}")
    tl = ctx["timeLimit"]
    if t in ("time_under", "survive") and is_num(tl) and tl > 0 and val > tl:
        R.err(where, f"'{t}' value {val} exceeds the level time limit {tl}")
    if t == "damage_under" and val >= 100:
        R.err(where, "damage_under is a percentage below 100")
    if t == "collect" and val > ctx["collectibles"]:
        R.err(where, f"collect {val} exceeds the level's {ctx['collectibles']} collectibles")
    if t == "hill_time" and not ctx["hills"]:
        R.err(where, f"hill_time objective but arena '{ctx['arena']}' has no hill_zone")
    if t == "destroy_props" and val > ctx["destructibles"]:
        R.err(where, f"destroy_props {val} exceeds the {ctx['destructibles']} destructible props in '{ctx['arena']}'")
    if t == "boss" and not ctx["hasBoss"]:
        R.err(where, "boss objective but no opponent has boss: true")


def validate_car(where, c, vehicles, ai, equipment, upgrades) -> None:
    if not isinstance(c, dict):
        R.err(where, "must be an object")
        return
    veh = vehicles.get(c.get("vehicle"))
    if veh is None:
        R.err(where, f"unknown vehicle {c.get('vehicle')!r}")
    if c.get("ai") not in ai:
        R.err(where, f"unknown ai profile {c.get('ai')!r}")
    if not is_int(c.get("team")):
        R.err(where, "'team' must be an integer")
    loc_key(where, c.get("name"), ("camp.",), nullable=True)
    ups = c.get("upgrades", {})
    if not isinstance(ups, dict):
        R.err(where, "'upgrades' must be an object")
    else:
        for k, lv in ups.items():
            if k not in UPGRADE_CATEGORIES:
                R.err(where, f"unknown upgrade category '{k}'")
            if not is_int(lv) or not 0 <= lv <= 5:
                R.err(where, f"upgrade '{k}' level {lv!r} must be 0-5")
    eq = c.get("equipment", {})
    if not isinstance(eq, dict):
        R.err(where, "'equipment' must be an object")
    else:
        for slot, iid in eq.items():
            if slot not in EQUIP_SLOTS:
                R.err(where, f"unknown equipment slot '{slot}'")
                continue
            item = equipment.get(iid)
            if item is None:
                R.err(where, f"unknown equipment '{iid}'")
            elif item.get("slot") != slot:
                R.err(where, f"'{iid}' is a {item.get('slot')} item, not {slot}")
            elif item.get("allowedClasses") and veh and veh.get("class") not in item["allowedClasses"]:
                R.err(where, f"'{iid}' is not allowed on class '{veh.get('class')}'")
    typed(where, c, "boss", bool, required=False)


def validate_campaign(d, arenas, modes, vehicles, ai, equipment, upgrades, items) -> dict:
    stats = {"levels": 0, "lines": 0}
    rivals = IDS.check("campaign.json rivals", d.get("rivals"), set())
    for rid, r in rivals.items():
        w = f"campaign.json rival '{rid}'"
        loc_key(w, r.get("name"), ("camp.",))
        if "title" in r:
            loc_key(w, r.get("title"), ("camp.",))
        if r.get("vehicle") not in vehicles:
            R.err(w, f"unknown vehicle {r.get('vehicle')!r}")
        color(w, r.get("color"))
    chapters = IDS.check("campaign.json chapters", d.get("chapters"), set())
    level_ids: dict[str, int] = {}
    order = 0
    prev_unlock = -1
    for cid, ch in chapters.items():
        w = f"campaign.json chapter '{cid}'"
        loc_key(w, ch.get("name"), ("camp.",))
        loc_key(w, ch.get("description"), ("camp.",))
        hub = ch.get("hub")
        if hub is not None:
            if hub not in arenas:
                R.err(w, f"hub '{hub}' is not an arena")
            elif not arenas[hub].get("hub"):
                R.err(w, f"hub arena '{hub}' must have hub: true")
        us = num(w, ch, "unlockStars", 0, None, integer=True)
        if is_int(us):
            if us < prev_unlock:
                R.err(w, "chapter unlockStars must not decrease")
            if us > 3 * order:
                R.err(w, f"unlockStars {us} exceeds the {3 * order} stars available before this chapter")
            prev_unlock = us
        boss = ch.get("boss")
        if boss is not None and boss not in rivals:
            R.err(w, f"boss '{boss}' is not a rival")
        color(w, ch.get("color"))
        levels = ch.get("levels")
        if not isinstance(levels, list) or not levels:
            R.err(w, "'levels' must be a non-empty list")
            continue
        boss_seen = False
        for li, lv in enumerate(levels):
            if not isinstance(lv, dict):
                R.err(w, f"level {li} must be an object")
                continue
            lid = lv.get("id")
            lw = f"campaign.json level '{lid}'"
            if not isinstance(lid, str) or not ID_RE.match(lid):
                R.err(lw, "id must match ^[a-z0-9_]+$")
                continue
            if lid in level_ids:
                R.err(lw, "duplicate level id")
            level_ids[lid] = order
            stats["levels"] += 1
            loc_key(lw, lv.get("name"), ("camp.",))
            loc_key(lw, lv.get("description"), ("camp.",))
            arena = arenas.get(lv.get("arena"))
            if arena is None:
                R.err(lw, f"unknown arena {lv.get('arena')!r}")
                arena = {}
            mode = modes.get(lv.get("mode"))
            if mode is None:
                R.err(lw, f"unknown mode {lv.get('mode')!r}")
                mode = {}
            tl = num(lw, lv, "timeLimit", 0, 3600, default=0)
            mt = lv.get("maxTier")
            if mt is not None and (not is_int(mt) or not 1 <= mt <= 5):
                R.err(lw, f"maxTier {mt!r} must be null or 1-5")
            loaner = lv.get("loaner")
            if loaner is not None and loaner not in vehicles:
                R.err(lw, f"loaner '{loaner}' is not a vehicle")
            opps = lv.get("opponents", [])
            allies = lv.get("allies", [])
            if not isinstance(opps, list) or not isinstance(allies, list):
                R.err(lw, "'opponents' and 'allies' must be lists")
                continue
            for i, c in enumerate(opps):
                validate_car(f"{lw} opponent {i}", c, vehicles, ai, equipment, upgrades)
            for i, c in enumerate(allies):
                validate_car(f"{lw} ally {i}", c, vehicles, ai, equipment, upgrades)
                if isinstance(c, dict) and c.get("team") != 0:
                    R.err(f"{lw} ally {i}", "allies must be on team 0")
            if not opps and lv.get("mode") not in ("explore", "destruction"):
                R.err(lw, "level has no opponents")
            teams = mode.get("teams", 0)
            o_teams = [c.get("team") for c in opps if isinstance(c, dict)]
            if teams == 0:
                if sorted(o_teams) != list(range(1, len(o_teams) + 1)):
                    R.err(lw, f"free-for-all opponents need distinct teams 1..{len(o_teams)}, got {o_teams}")
                if allies:
                    R.err(lw, "free-for-all levels cannot have allies")
                cars = 1 + len(opps)
                cap = arena.get("_ffaSpawns") or len(arena.get("spawns", []))
                if arena and cars > cap:
                    R.err(lw, f"{cars} cars but arena '{lv.get('arena')}' has {cap} free-for-all spawns")
            elif is_int(teams):
                for t in o_teams:
                    if not is_int(t) or not 1 <= t < teams:
                        R.err(lw, f"opponent team {t!r} must be 1..{teams - 1} in a {teams}-team mode")
                per_team = Counter(o_teams)
                per_team[0] += 1 + len(allies)
                ts = arena.get("_teamSpawns") or {}
                total = len(arena.get("spawns", []))
                for t, n in per_team.items():
                    cap = ts.get(t, 0) if ts else total // max(teams, 1)
                    if arena and n > cap:
                        R.err(lw, f"team {t} has {n} cars but arena '{lv.get('arena')}' has {cap} spawns for it")
            if mode.get("hill") and arena and not arena.get("_hills"):
                R.err(lw, f"mode '{lv.get('mode')}' needs a hill_zone in arena '{lv.get('arena')}'")
            if mode.get("ringOut") and arena and arena.get("walls", True):
                R.err(lw, f"ring-out mode in walled arena '{lv.get('arena')}'")
            if arena.get("hub"):
                R.warn(lw, f"level uses hub arena '{lv.get('arena')}'")
            ncol = num(lw, lv, "collectibles", 0, 10, integer=True, default=0)
            if arena and is_int(ncol) and ncol > len(arena.get("collectibles", [])):
                R.err(lw, f"collectibles {ncol} exceeds the {len(arena.get('collectibles', []))} positions in '{lv.get('arena')}'")
            has_boss = any(isinstance(c, dict) and c.get("boss") for c in opps)
            if lv.get("mode") == "boss":
                boss_seen = True
                if not has_boss:
                    R.err(lw, "boss mode needs an opponent with boss: true")
            for c in opps:
                if isinstance(c, dict) and c.get("boss") and c.get("name") not in {r.get("name") for r in rivals.values()}:
                    R.warn(lw, "boss opponent should carry its rival's name key")
            ctx = {"timeLimit": tl, "collectibles": ncol if is_int(ncol) else 0, "hills": arena.get("_hills", 0),
                   "destructibles": arena.get("_destructibles", 0), "arena": lv.get("arena"), "hasBoss": has_boss}
            validate_objective(f"{lw} objective", lv.get("objective"), ctx)
            stars = lv.get("stars")
            if not isinstance(stars, list) or len(stars) != 3:
                R.err(lw, "must have exactly 3 stars")
            else:
                for i, s in enumerate(stars):
                    validate_objective(f"{lw} star {i + 1}", s, ctx)
            rw = lv.get("rewards")
            if not isinstance(rw, dict):
                R.err(lw, "missing 'rewards'")
            else:
                num(lw + " rewards", rw, "credits", 0, None, integer=True)
                num(lw + " rewards", rw, "xp", 0, None, integer=True)
                items.check_list(lw + " rewards", rw.get("items", []))
            st = lv.get("story")
            if not isinstance(st, dict):
                R.err(lw, "missing 'story'")
            else:
                for part in ("intro", "outro"):
                    keys = st.get(part, [])
                    if not isinstance(keys, list):
                        R.err(lw, f"story.{part} must be a list")
                        continue
                    for k in keys:
                        loc_key(f"{lw} story.{part}", k, ("dlg.",))
                    stats["lines"] += len(keys)
                    lo, hi = (2, 10) if lv.get("mode") == "boss" else (2, 6)
                    if not lo <= len(keys) <= hi:
                        R.warn(lw, f"story.{part} has {len(keys)} lines (expected {lo}-{hi})")
            un = lv.get("unlock")
            if not isinstance(un, dict):
                R.err(lw, "missing 'unlock'")
            else:
                ul = un.get("level")
                if ul is not None:
                    if ul not in level_ids or ul == lid:
                        R.err(lw, f"unlock.level '{ul}' must be an earlier level")
                stars_req = num(lw + " unlock", un, "stars", 0, None, integer=True)
                if is_int(stars_req) and stars_req > 3 * order:
                    R.err(lw, f"unlock.stars {stars_req} exceeds the {3 * order} stars available before it")
            order += 1
        if boss and not boss_seen:
            R.warn(w, "chapter has a boss rival but no boss level")
    return stats


# ---------------------------------------------------------------------------------------------------------------
# Progression / store

def validate_progression(d, items) -> None:
    w = "progression.json"
    p = d.get("player")
    max_level = 100
    if not isinstance(p, dict):
        R.err(w, "missing 'player'")
    else:
        max_level = num(w + " player", p, "maxLevel", 2, 1000, integer=True, default=100)
        num(w + " player", p, "xpBase", 1, None)
        num(w + " player", p, "xpGrowth", 1.0, 2.0)
    prev = 1
    for i, lr in enumerate(d.get("levelRewards", []) or []):
        lw = f"{w} levelRewards[{i}]"
        lvl = num(lw, lr, "level", 2, max_level, integer=True)
        if is_int(lvl):
            if lvl <= prev:
                R.err(lw, "levelRewards must be sorted by level without duplicates")
            prev = lvl
        items.check_list(lw, lr.get("items"), allow_empty=False)
    m = d.get("match")
    if not isinstance(m, dict):
        R.err(w, "missing 'match'")
    else:
        for k in MATCH_KEYS:
            num(w + " match", m, k, 1.0 if k.endswith("Multiplier") else 0, None)
    s = d.get("score")
    if not isinstance(s, dict):
        R.err(w, "missing 'score'")
    else:
        for k in SCORE_KEYS:
            num(w + " score", s, k, 1.0 if k == "combo_max" else 0, None)
        for k in s:
            if k not in SCORE_KEYS:
                R.warn(w + " score", f"unknown key '{k}'")
    ranks = d.get("ranks")
    if not isinstance(ranks, list):
        R.err(w, "missing 'ranks'")
        return
    ids = [r.get("id") for r in ranks if isinstance(r, dict)]
    if tuple(ids) != RANK_ORDER:
        R.err(w, f"ranks must be {list(RANK_ORDER)} in this order, got {ids}")
    prev = -1
    for i, r in enumerate(ranks):
        rw = f"{w} rank '{r.get('id') if isinstance(r, dict) else i}'"
        loc_key(rw, r.get("name"), ("rank.",))
        mr = num(rw, r, "minRating", 0, 10000, integer=True)
        if is_int(mr):
            if i == 0 and mr != 0:
                R.err(rw, "the first rank must start at minRating 0")
            if mr <= prev:
                R.err(rw, "ranks must be sorted by strictly increasing minRating")
            prev = mr
        num(rw, r, "divisions", 1, 5, integer=True)
        color(rw, r.get("color"))


def iso(where, v, nullable=True):
    if v is None and nullable:
        return None
    if not isinstance(v, str) or not ISO_RE.match(v):
        R.err(where, f"timestamp {v!r} must be 'YYYY-MM-DDTHH:MM:SSZ'")
        return None
    return v


def validate_store(d, items) -> dict:
    w = "store.json"
    stats = {}
    for p in find_keys(d, {"random", "lootbox", "loot_box", "weights", "chance", "probability", "odds"}):
        R.err(w, f"randomized rewards are not allowed ('{p}')")
    offers = IDS.check(f"{w} offers", d.get("offers"), set(), "store.json offers")
    sections = Counter()
    for oid, o in offers.items():
        ow = f"{w} offer '{oid}'"
        sec = enum(ow, o, "section", STORE_SECTIONS)
        sections[sec] += 1
        typed(ow, o, "featured", bool)
        items.check_list(ow, o.get("items"), allow_empty=False)
        check_price(ow, o.get("price"), allow_zero=False)
        num(ow, o, "limit", 0, None, integer=True)
        s, e = iso(ow, o.get("start")), iso(ow, o.get("end"))
        if s and e and s >= e:
            R.err(ow, "start must be before end")
        if o.get("tag") is not None:
            loc_key(ow, o.get("tag"), ("store.",))
    for s in STORE_SECTIONS:
        if not sections[s]:
            R.warn(w, f"no offers in section '{s}'")
    packs = IDS.check(f"{w} goldPacks", d.get("goldPacks"), set(), "store.json goldPacks")
    for pid, p in packs.items():
        pw = f"{w} gold pack '{pid}'"
        num(pw, p, "gold", 1, None, integer=True)
        num(pw, p, "bonus", 0, None, integer=True)
        prices = p.get("prices")
        if not isinstance(prices, dict) or "USD" not in prices or "CNY" not in prices:
            R.err(pw, "'prices' must include USD and CNY")
        else:
            for cur, amt in prices.items():
                if not re.match(r"^[A-Z]{3}$", cur) or not is_num(amt) or amt <= 0:
                    R.err(pw, f"price {cur}={amt!r} must be a positive amount in an ISO 4217 currency")
    daily = d.get("daily")
    if not isinstance(daily, dict):
        R.err(w, "missing 'daily'")
    else:
        slots = num(w + " daily", daily, "slots", 1, 20, integer=True, default=0)
        disc = daily.get("discounts")
        if not isinstance(disc, list) or not disc or not all(is_num(x) and 0 < x < 1 for x in disc):
            R.err(w + " daily", "'discounts' must be a non-empty list of fractions between 0 and 1")
        pool = daily.get("pool", [])
        items.check_list(w + " daily pool", pool, allow_empty=False)
        seen = set()
        for i, it in enumerate(pool if isinstance(pool, list) else []):
            if not isinstance(it, dict):
                continue
            key = (it.get("type"), it.get("id"))
            if key in seen:
                R.err(f"{w} daily pool item {i}", f"duplicate entry {key}")
            seen.add(key)
            base = items.get(it.get("type"), it.get("id"))
            if it.get("type") == "currency" or base is None:
                if it.get("type") == "currency":
                    R.err(f"{w} daily pool item {i}", "currency cannot be a daily deal")
                continue
            if not isinstance(base.get("price"), dict) or base["price"].get("credits", 0) <= 0:
                R.err(f"{w} daily pool item {i}", f"'{it.get('id')}' needs a positive credits base price")
        if isinstance(pool, list) and is_int(slots) and len(pool) < slots:
            R.err(w + " daily", "pool is smaller than the number of slots")
        if isinstance(pool, list) and len(pool) < 25:
            R.warn(w + " daily", f"pool has only {len(pool)} entries (25+ recommended)")
        stats["pool"] = len(pool) if isinstance(pool, list) else 0
    sp = d.get("seasonPass")
    if not isinstance(sp, dict):
        R.err(w, "missing 'seasonPass'")
    else:
        sw = f"{w} seasonPass"
        sid = sp.get("id")
        if not isinstance(sid, str) or not ID_RE.match(sid):
            R.err(sw, f"id {sid!r} must match ^[a-z0-9_]+$")
        loc_key(sw, sp.get("name"), ("store.",))
        s, e = iso(sw, sp.get("start"), False), iso(sw, sp.get("end"), False)
        if s and e and s >= e:
            R.err(sw, "start must be before end")
        check_price(sw, sp.get("premiumPrice"), allow_zero=False)
        num(sw, sp, "xpPerTier", 1, None, integer=True)
        tiers = sp.get("tiers")
        if not isinstance(tiers, list) or not 50 <= len(tiers) <= 100:
            R.err(sw, "'tiers' must have 50-100 entries")
            tiers = tiers if isinstance(tiers, list) else []
        for i, t in enumerate(tiers, 1):
            if not isinstance(t, dict):
                R.err(f"{sw} tier {i}", "must be an object")
                continue
            items.check_list(f"{sw} tier {i} free", t.get("free", []))
            items.check_list(f"{sw} tier {i} premium", t.get("premium", []))
        stats["tiers"] = len(tiers)
    stats["offers"] = len(offers)
    stats["packs"] = len(packs)
    return stats


def find_keys(obj, keys, path=""):
    if isinstance(obj, dict):
        for k, v in obj.items():
            p = f"{path}.{k}" if path else k
            if k in keys:
                yield p
            yield from find_keys(v, keys, p)
    elif isinstance(obj, list):
        for i, v in enumerate(obj):
            yield from find_keys(v, keys, f"{path}[{i}]")


# ---------------------------------------------------------------------------------------------------------------
# Localization coverage

def referenced_keys(obj, out: dict, src: str):
    """Collects localization keys from name/description/title/text/tag fields and campaign story lists."""
    if isinstance(obj, dict):
        for k, v in obj.items():
            if k in LOC_FIELDS + ("tag",) and isinstance(v, str):
                out.setdefault(v, src)
            elif k in ("intro", "outro") and isinstance(v, list):
                for x in v:
                    if isinstance(x, str):
                        out.setdefault(x, src)
            else:
                referenced_keys(v, out, src)
    elif isinstance(obj, list):
        for v in obj:
            referenced_keys(v, out, src)


def validate_localization(langs, refs: dict) -> dict:
    zh, en = langs.get("zh-Hans", {}), langs.get("en", {})
    speakers: dict[str, str] = {}
    for key in sorted(k for k in set(zh) | set(en) if k.startswith("dlg.")):
        sp = {}
        for lang, table in (("zh-Hans", zh), ("en", en)):
            text = table.get(key)
            if text is None:
                continue
            if "|" not in text:
                R.err(f"Localization/{lang}", f"dialogue '{key}' must be formatted 'Speaker|Line'")
                continue
            speaker, line = text.split("|", 1)
            if not speaker.strip() or not line.strip():
                R.err(f"Localization/{lang}", f"dialogue '{key}' has an empty speaker or line")
            sp[lang] = speaker.strip()
            if LOC_KEY_RE.match(speaker.strip()) and speaker.strip().startswith(LOC_PREFIXES):
                speakers.setdefault(speaker.strip(), key)
        if len(set(sp.values())) > 1:
            R.warn("Localization", f"dialogue '{key}' has different speakers per language: {sp}")
    needed = dict(refs)
    for s, k in speakers.items():
        needed.setdefault(s, f"speaker of {k}")
    for key, src in sorted(needed.items()):
        if key.startswith(ENGINE_OWNED_PREFIX):
            continue
        for lang, table in (("zh-Hans", zh), ("en", en)):
            if key not in table:
                R.err(f"Localization/{lang}", f"missing key '{key}' (referenced by {src})")
    for key in sorted(set(en) - set(zh)):
        R.err("Localization/zh-Hans", f"missing key '{key}' (present in en; zh-Hans is the primary language)")
    for key in sorted(set(zh) & set(en)):
        a, b = sorted(PLACEHOLDER_RE.findall(zh[key])), sorted(PLACEHOLDER_RE.findall(en[key]))
        if a != b:
            R.err("Localization", f"placeholders differ for '{key}': zh-Hans {a} vs en {b}")
    unused = [k for k in zh if k not in needed and not k.startswith(ENGINE_OWNED_PREFIX)]
    if unused:
        R.warn("Localization", f"{len(unused)} unreferenced keys, e.g. {', '.join(sorted(unused)[:8])}")
    return {"zh": len(zh), "en": len(en), "refs": len(needed)}


# ---------------------------------------------------------------------------------------------------------------
# Main

def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="Validate BigCar game data against docs/DATA_SCHEMA.md")
    default_dir = Path(__file__).resolve().parent.parent / "Content" / "BigCar" / "Data"
    ap.add_argument("--data-dir", type=Path, default=default_dir, help=f"data folder (default: {default_dir})")
    ap.add_argument("--strict", action="store_true", help="treat warnings as errors")
    ap.add_argument("--quiet", action="store_true", help="do not list warnings, only count them")
    args = ap.parse_args(argv)
    data_dir: Path = args.data_dir
    if not data_dir.is_dir():
        print(f"error: data dir {data_dir} does not exist", file=sys.stderr)
        return 1

    files: dict[str, dict] = {}
    for name in REQUIRED_FILES:
        p = data_dir / f"{name}.json"
        if not p.exists():
            R.err(f"{name}.json", "required file is missing")
            files[name] = {}
            continue
        files[name] = load_json(p, f"{name}.json") or {}
    for p in sorted(data_dir.glob("*.json")):
        if p.stem not in REQUIRED_FILES:
            load_json(p, p.name)  # engine-owned or extra files: syntax + version only
    langs = load_localization(data_dir)

    brands = validate_brands(files["brands"])
    vehicles = validate_vehicles(files["vehicles"], brands)
    upgrades = validate_upgrades(files["upgrades"])
    equipment = validate_equipment(files["equipment"])
    cosmetics = validate_cosmetics(files["cosmetics"])
    items = Items(vehicles, equipment, cosmetics)
    arenas = validate_arenas(files["arenas"])
    modes = validate_modes(files["modes"])
    ai = validate_ai(files["ai"])
    camp = validate_campaign(files["campaign"], arenas, modes, vehicles, ai, equipment, upgrades, items)
    validate_progression(files["progression"], items)
    store = validate_store(files["store"], items)

    refs: dict[str, str] = {}
    for name, d in files.items():
        referenced_keys({k: v for k, v in d.items()}, refs, f"{name}.json")
    for a in arenas.values():
        for k in [k for k in a if k.startswith("_")]:
            del a[k]
    loc = validate_localization(langs, refs)

    hubs = sum(1 for a in arenas.values() if a.get("hub"))
    cos_counts = ", ".join(f"{len(v)} {k}" for k, v in cosmetics.items())
    print(f"BigCar data validator — {data_dir}")
    print(f"  brands {len(brands)}, vehicles {len(vehicles)} ({sum(1 for v in vehicles.values() if v.get('starter'))} starters), "
          f"upgrade categories {len(upgrades)}, equipment {len(equipment)}")
    print(f"  cosmetics: {cos_counts}")
    print(f"  arenas {len(arenas)} ({len(arenas) - hubs} combat, {hubs} hubs), modes {len(modes)}, ai profiles {len(ai)}")
    print(f"  campaign: {len(files['campaign'].get('chapters', []))} chapters, {camp['levels']} levels, "
          f"{camp['lines']} dialogue lines, {len(files['campaign'].get('rivals', []))} rivals")
    print(f"  store: {store.get('offers', 0)} offers, {store.get('packs', 0)} gold packs, daily pool {store.get('pool', 0)}, "
          f"season pass tiers {store.get('tiers', 0)}")
    print(f"  localization: zh-Hans {loc['zh']} keys, en {loc['en']} keys, {loc['refs']} referenced")
    if R.warnings:
        print(f"\n{len(R.warnings)} warning(s)")
        if not args.quiet:
            for x in R.warnings:
                print(f"  WARN  {x}")
    if R.errors:
        print(f"\n{len(R.errors)} error(s)")
        for x in R.errors:
            print(f"  ERROR {x}")
    failed = bool(R.errors) or (args.strict and bool(R.warnings))
    print("\nFAILED" if failed else "\nOK — no errors")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
