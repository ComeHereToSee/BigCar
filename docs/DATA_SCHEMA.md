# BigCar Data Schema (contract)

All gameplay content is data-driven JSON under `Content/BigCar/Data/`. The same files are read by:

* the Unreal game (via the engine-agnostic `CrashCore` C++ library, `Source/CrashCore`),
* the CMake unit tests / headless battle simulator (`Tests/`, `Tools/crashsim`),
* the Go backend (`Backend/`, prices, store, rewards, matchmaking modes),
* the Python validator (`Tools/validate_data.py`).

This document is the single contract. When code and this document disagree, fix one of them in the same change.

General rules

* UTF-8, no comments, no trailing commas. Every file has `"version": 1` at the top level.
* Ids are `snake_case` ASCII (`^[a-z0-9_]+$`) and unique within their file (vehicles, equipment, cosmetics, offers… each have their own namespace, but please keep them globally unique anyway — the store references items by `type` + `id`).
* Distances are **meters**, masses **kg**, speeds **m/s** unless the field name says otherwise (`topSpeedKmh`), angles **degrees**, times **seconds**, power **kW**, torque **N·m**. Coordinates use the Unreal axis convention (X forward, Y right, Z up) but in meters (the game multiplies by 100).
* Player-visible text is never written inline. Fields named `name`, `description`, `title`, `text` hold **localization keys** that must exist in `Localization/<lang>/*.json`. Key prefixes: `ui.` (interface), `veh.`, `brand.`, `equip.`, `upg.`, `cos.`, `arena.`, `mode.`, `camp.`, `dlg.` (story dialogue), `store.`, `rank.`, `ai.`.
* Colors are `"#RRGGBB"` strings.
* Prices are objects `{ "credits": 1234 }` or `{ "gold": 99 }` (exactly one key). `credits` = soft currency earned by playing, `gold` = premium currency.

---------------------------------------------------------------------------------------------------

## brands.json

```json
{ "version": 1,
  "brands": [
    { "id": "fulmine", "name": "brand.fulmine", "country": "IT", "color": "#C8102E",
      "licensed": false, "reference": "Ferrari" }
  ] }
```

| field | type | notes |
|---|---|---|
| `licensed` | bool | `false` until a real trademark license exists. When `true`, the UI shows `vehicle.reference` / `brand.reference` instead of the fictional names. |
| `reference` | string | Real-world brand this fictional brand stands in for. **Internal only** — never displayed while `licensed` is false. |

## vehicles.json

```json
{ "version": 1,
  "vehicles": [
    {
      "id": "fulmine_f8r",
      "brand": "fulmine",
      "name": "veh.fulmine_f8r",
      "reference": "Ferrari F8 Tributo",
      "class": "supercar",
      "body": "supercar",
      "tier": 4,
      "price": { "credits": 450000 },
      "unlockLevel": 20,
      "starter": false,
      "drivetrain": "rwd",
      "engineLayout": "mid",
      "massKg": 1435,
      "powerKw": 530,
      "torqueNm": 770,
      "idleRpm": 1000,
      "redlineRpm": 8000,
      "gears": [3.13, 2.18, 1.63, 1.29, 1.03, 0.84, 0.69],
      "reverseRatio": 3.0,
      "finalDrive": 3.6,
      "topSpeedKmh": 340,
      "dimensions": { "length": 4.61, "width": 1.98, "height": 1.21, "wheelbase": 2.65,
                      "trackFront": 1.68, "trackRear": 1.65, "groundClearance": 0.11,
                      "wheelRadius": 0.35, "wheelWidth": 0.30 },
      "cgHeight": 0.42,
      "frontWeight": 0.42,
      "grip": 1.15,
      "brakeG": 1.2,
      "steerLockDeg": 34,
      "suspension": { "frequency": 1.9, "damping": 0.45, "travel": 0.12, "antiRoll": 0.6 },
      "dragCoefficient": 0.32,
      "downforce": 0.8,
      "toughness": 0.9,
      "armor": { "front": 0.05, "rear": 0.05, "left": 0.05, "right": 0.05, "top": 0.0, "bottom": 0.0 },
      "ramPower": 1.0,
      "boost": { "capacity": 100, "accel": 7.0, "regenPerSec": 6 },
      "colors": ["#C8102E", "#F2C200", "#111111"],
      "tags": ["popular", "luxury", "italian"]
    }
  ] }
```

| field | type | rules / defaults |
|---|---|---|
| `class` | enum | `compact, hatchback, sedan, wagon, coupe, muscle, sports, supercar, hypercar, luxury, suv, pickup, offroad, van, truck` |
| `body` | enum | Procedural body style used until a real mesh exists. Same enum as `class`. Defaults to `class`. |
| `tier` | int 1–5 | 1 economy, 2 mainstream, 3 performance/heavy, 4 super/luxury, 5 hyper/flagship |
| `price` | price | Base garage price. Starter cars: `{ "credits": 0 }` and `"starter": true`. |
| `unlockLevel` | int ≥1 | Player level required to buy. |
| `drivetrain` | enum | `fwd, rwd, awd` |
| `engineLayout` | enum | `front, mid, rear` (decides which damage zone hosts the engine: front→`front`, mid→`rear` 50% + sides 25% each, rear→`rear`) |
| `gears` | float[] | Forward ratios, 1–10 entries, strictly decreasing (EVs may use a single ratio, e.g. `[9.0]`). |
| `dimensions` | object | All > 0. `wheelbase < length`, `track* < width`. |
| `cgHeight` | m | Center of mass height above ground. Default `0.35 * height + groundClearance`. |
| `frontWeight` | 0.3–0.7 | Static front axle load fraction. |
| `grip` | 0.6–1.6 | Tire friction coefficient (street ~1.0, semi-slick ~1.2). |
| `brakeG` | 0.5–1.6 | Max braking deceleration in g. Default `0.9 * grip`. |
| `suspension.frequency` | Hz 0.8–3.0 | Ride frequency. `damping` ratio 0.2–0.9, `travel` m, `antiRoll` 0–1.5. |
| `dragCoefficient` | 0.2–0.8 | |
| `downforce` | ≥0 | Downforce coefficient (N per (m/s)² ≈ `downforce * 1.0`). |
| `toughness` | 0.5–2.5 | Multiplies zone hit points. Zone HP formula is in `Stats` (see below). |
| `armor.*` | 0–0.6 | Fraction of incoming damage removed on that zone (before equipment). Missing zones = 0.05. |
| `ramPower` | 0.5–2.0 | How much damage the **front** deals (bull bars, heavy frames > 1). |
| `boost` | object | `capacity` units, `accel` m/s² added while boosting (1 unit/0.01s → 100 units ≈ 1 s), `regenPerSec` units. |
| `colors` | color[] | First is the factory paint. |
| `tags` | string[] | Free form: `popular, luxury, classic, ev, japanese, german, american, italian, british, chinese, korean, swedish, french, offroad, heavy, drift…` |

Derived values (computed by code — do **not** put them in data):

* Zone hit points: `hp(zone) = toughness * (400 + 0.35 * massKg) * zoneWeight(zone)` with zone weights `front 1.0, rear 0.9, left 0.8, right 0.8, top 0.7, bottom 0.8`, `engine 0.7`, each wheel `0.35`.
* Combat Rating (CR, used for matchmaking brackets and campaign restrictions):
  `CR = 100 * tier + 10 * (sum of upgrade levels) + sum(equipment.cr for each equipped item)`.

## upgrades.json

Garage performance upgrades bought with credits, per vehicle.

```json
{ "version": 1,
  "tierCostMultiplier": [1.0, 1.6, 2.5, 4.0, 6.0],
  "categories": [
    { "id": "engine", "name": "upg.engine", "description": "upg.engine.desc",
      "levels": [
        { "cost": 4000,  "mods": { "powerMul": 1.06, "torqueMul": 1.05 } },
        { "cost": 9000,  "mods": { "powerMul": 1.12, "torqueMul": 1.10 } }
      ] }
  ] }
```

* `categories[].id` ∈ `engine, transmission, suspension, tires, brakes, chassis, armor, weight, nitro` (all nine must exist).
* `levels` has exactly 5 entries (stage 1–5). Each level's `mods` are the **total** effect at that level (not cumulative on top of the previous level).
* Price of level `L` for a vehicle of tier `T`: `roundTo100(levels[L-1].cost * tierCostMultiplier[T-1])` credits. `roundTo100(x) = floor((x + 50) / 100) * 100`.

## Stat modifiers (`mods`)

Shared by upgrades and equipment. Unknown keys are a validation error.

| key | combine | meaning |
|---|---|---|
| `powerMul`, `torqueMul`, `gripMul`, `brakeMul`, `steerMul`, `topSpeedMul`, `massMul`, `toughnessMul`, `ramPowerMul`, `boostAccelMul`, `boostRegenMul`, `downforceMul`, `dragMul`, `suspensionMul`, `wheelHpMul`, `engineHpMul`, `selfDamageMul`, `sideDamageMul`, `rearDamageMul` | multiply | multiplicative, default 1 |
| `massAddKg`, `boostCapacityAdd`, `cgHeightAdd` (m, negative lowers), `launchPower`, `tireDamage`, `repairPerSec`, `knockback` | add | additive, default 0 |
| `armorFront`, `armorRear`, `armorLeft`, `armorRight`, `armorTop`, `armorBottom`, `armorAll` | add | added to the zone armor fraction; final armor is clamped to `[0, 0.75]` |

Units of the additive keys: `launchPower` 0–3 (wedge strength, 1.0 = steel wedge), `tireDamage` 0–3 (1.0 ≈ shreds 120 wheel hp per hard side hit), `repairPerSec` = **fraction** of max hp restored per second (0.005–0.02), `knockback` m/s added to shock knockback (0–5), `cgHeightAdd` meters (−0.1–0.1), `boostCapacityAdd` boost units (0–100), `massAddKg` kg (−300–600).

## equipment.json

Melee-only add-ons. **Ranged equipment is forbidden by design** (no projectiles, no guns, no missiles, no mines, no lasers, nothing that damages at a distance). The validator enforces `contactOnly: true` and rejects `range`, `projectile`, `ammo` keys.

```json
{ "version": 1,
  "equipment": [
    { "id": "front_wedge_steel", "slot": "front", "name": "equip.front_wedge_steel",
      "description": "equip.front_wedge_steel.desc", "rarity": "rare",
      "contactOnly": true,
      "geometry": { "shape": "wedge", "length": 0.7, "angleDeg": 16, "color": "#5A5F66" },
      "mods": { "launchPower": 1.0, "massAddKg": 85, "armorFront": 0.10 },
      "ability": null,
      "cr": 25,
      "price": { "credits": 30000 },
      "unlockLevel": 8,
      "allowedClasses": [] }
  ] }
```

| field | rules |
|---|---|
| `slot` | `front, rear, side, roof, wheels, core, ability` — a build holds at most one item per slot. |
| `rarity` | `common, rare, epic, legendary` |
| `geometry.shape` | Physical shape spawned on the car (affects real collisions): `none, ram_bar, wedge, plow, spikes, cage, side_plates, side_blades, rear_bumper, rear_spikes, roof_armor, wheel_spikes, ballast`. `length` (m) how far it sticks out, `angleDeg` for wedge/plow slope, optional `color`. |
| `ability` | `null` or one of `nitro_burst, hydraulic_jump, ram_charge, brace, field_repair, shock_bumper`. Only items in slot `ability` may set it. |
| `cr` | int ≥0, contribution to Combat Rating. |
| `allowedClasses` | empty = all classes. |

Ability tuning (code constants, documented for designers):

| ability | effect | cooldown |
|---|---|---|
| `nitro_burst` | Instantly refills 50 boost and +40% boost accel for 2 s | 14 s |
| `hydraulic_jump` | Upward velocity change 6.5 m/s (+`jumpForce`) | 8 s |
| `ram_charge` | 2.5 s: front damage ×1.6, +30% engine force | 12 s |
| `brace` | 3 s: incoming damage ×0.5 | 15 s |
| `field_repair` | Restores 25% of every zone over 5 s | 30 s |
| `shock_bumper` | Next contact within 3 s adds a 7 m/s knockback to the other car | 10 s |

## cosmetics.json

```json
{ "version": 1,
  "paints":   [ { "id": "paint_rosso", "name": "cos.paint_rosso", "color": "#C8102E", "finish": "gloss", "rarity": "common", "price": { "credits": 2000 } } ],
  "wheels":   [ { "id": "wheels_stock", "name": "cos.wheels_stock", "color": "#2B2B2B", "spokes": 5, "rarity": "common", "price": { "credits": 0 } } ],
  "sparks":   [ { "id": "sparks_default", "name": "cos.sparks_default", "color": "#FFB84D", "rarity": "common", "price": { "credits": 0 } } ],
  "horns":    [ { "id": "horn_default", "name": "cos.horn_default", "frequencies": [415, 520], "rarity": "common", "price": { "credits": 0 } } ],
  "titles":   [ { "id": "title_rookie", "name": "cos.title_rookie", "rarity": "common", "price": { "credits": 0 } } ],
  "banners":  [ { "id": "banner_default", "name": "cos.banner_default", "colors": ["#222222", "#FF6A00"], "rarity": "common", "price": { "credits": 0 } } ]
}
```

`finish` ∈ `gloss, metallic, matte, chrome, pearl, carbon`. Items with `credits: 0` are owned by every player from the start.

## arenas.json

Arenas are built procedurally at runtime from this description (no hand-made maps needed; artists can later replace an arena by giving it a `"level"` asset path).

```json
{ "version": 1,
  "arenas": [
    { "id": "rust_bowl", "name": "arena.rust_bowl", "description": "arena.rust_bowl.desc",
      "theme": "junkyard", "hub": false,
      "shape": "circle", "size": [140, 140], "wallHeight": 3.5, "walls": true,
      "elevation": 0, "killZ": -30,
      "floor": { "surface": "dirt", "color": "#6B5B45", "friction": 0.95 },
      "lighting": { "preset": "sunset", "fog": 0.02, "tint": "#FFD9B0" },
      "props": [
        { "type": "ramp", "pos": [20, 0, 0], "yaw": 90, "size": [8, 12, 2.5] }
      ],
      "spawns": [ { "pos": [-50, 0], "yaw": 0, "team": 0 } ],
      "collectibles": [ [30, 30, 0.5] ],
      "secrets": [ { "id": "rust_bowl_s1", "pos": [55, -40, 0], "radius": 6 } ] }
  ] }
```

| field | rules |
|---|---|
| `theme` | `junkyard, stadium, docks, desert, snow, city, industrial, highway, rooftop, quarry, airfield, neon` (drives prop colors, sky) |
| `hub` | `true` for chapter free-roam exploration maps. |
| `shape` | `rect` (size = [x, y] full extents), `circle` (size[0] = diameter), `octagon` (size[0] = diameter). |
| `walls` | `false` for ring-out arenas (cars can fall off). |
| `elevation` | Floor height in meters (ring-out platforms use 10–40). |
| `floor.surface` | `asphalt, concrete, dirt, sand, metal, ice, grass, snow` |
| `lighting.preset` | `day, sunset, dusk, night, overcast, storm` |
| `spawns` | ≥ 12 entries. `team` optional: 0, 1 (team spawns, ≥ 3 each) or omitted (FFA). `pos` is [x, y]; z is the floor. |
| `collectibles` | 0–10 positions [x, y, z] (z relative to floor). Campaign levels use the first N. |
| `secrets` | Hub/exploration discoveries (drive into the radius). |

Prop types (`pos` is [x, y, z], z relative to the floor, `yaw` degrees, default 0):

| type | extra fields |
|---|---|
| `ramp` | `size` [width, length, height]; rises toward local +X |
| `block` | `size` [x, y, z], optional `color`, `soft` (bool: tire wall, low impact damage) |
| `cylinder` | `radius`, `height`, optional `color` |
| `jump_pad` | `size` [x, y], `strength` (upward m/s) |
| `boost_pad` | `size` [x, y], `strength` (forward m/s added along yaw) |
| `spinner` | `radius` (arm length), `height`, `speed` (deg/s) — rotating hazard arm |
| `crusher` | `size` [x, y, z], `period` (s), `travel` (m) — vertical piston |
| `pit` | `size` [x, y] — hole / kill zone in the floor |
| `crate_stack` | `count` (1–20), `size` (crate edge m) — destructible physics crates |
| `barrel` | `count` (1–8) — explosive barrels (explode on hard impact: radial push + damage within 8 m. Environmental, not player equipment.) |
| `wreck` | `class` (vehicle class for the shape), optional `color` — heavy physics car wreck |
| `container` | optional `color` — 12.2 × 2.44 × 2.6 m static container |
| `fence` | `to` [x, y, z] end point — breakable fence line |
| `hill_zone` | `radius` — King-of-the-Hill capture zone |
| `light` | `color`, `intensity` (0–1), `radius` (m) |

## modes.json

```json
{ "version": 1,
  "modes": [
    { "id": "derby", "name": "mode.derby", "description": "mode.derby.desc",
      "teams": 0, "respawn": false, "respawnDelay": 0, "lives": 1,
      "timeLimit": 300, "scoreLimit": 0, "rounds": 1, "roundsToWin": 1,
      "win": "last_standing", "ringOut": false, "hill": false, "bounty": false,
      "minPlayers": 2, "maxPlayers": 12, "online": true, "ranked": false }
  ] }
```

* `teams`: 0 = free-for-all, 2 or 3 = number of teams.
* `win` ∈ `last_standing` (last car/team alive), `score` (highest score at time/score limit), `takedowns` (most takedowns), `hill_time` (team hill seconds), `survive` (survive until time ends — campaign), `objective` (campaign level objectives decide).
* Required mode ids: `derby, takedown, team_takedown, team_elimination, ring_out, king_of_hill, bounty, survival, destruction, boss, explore`.

## ai.json

```json
{ "version": 1,
  "profiles": [
    { "id": "rookie", "name": "ai.rookie", "skill": 0.3, "aggression": 0.5, "reaction": 0.6,
      "aimLead": 0.3, "flankBias": 0.2, "abilityUse": 0.2, "boostUse": 0.3,
      "retreatHealth": 0.0, "mistakeRate": 0.25, "focusPlayer": 0.6 }
  ] }
```

All values 0–1 except `reaction` (seconds between re-plans, 0.1–1.0). Required ids: `rookie, regular, veteran, elite, boss`.

## campaign.json

```json
{ "version": 1,
  "rivals": [ { "id": "rival_rust", "name": "camp.rival_rust", "title": "camp.rival_rust.title",
                "vehicle": "ironhorse_f150r", "color": "#B7410E" } ],
  "chapters": [
    { "id": "ch1", "name": "camp.ch1", "description": "camp.ch1.desc",
      "hub": "hub_ch1", "unlockStars": 0, "boss": "rival_rust", "color": "#B7410E",
      "levels": [
        { "id": "ch1_l1", "name": "camp.ch1_l1", "description": "camp.ch1_l1.desc",
          "arena": "rust_bowl", "mode": "derby", "timeLimit": 240,
          "maxTier": 1, "loaner": null,
          "opponents": [
            { "vehicle": "hoshino_corolla", "ai": "rookie", "team": 1, "name": null,
              "upgrades": { "engine": 1 }, "equipment": { "front": "front_bar_basic" }, "boss": false }
          ],
          "allies": [],
          "objective": { "type": "win" },
          "stars": [ { "type": "win" }, { "type": "takedowns", "value": 3 }, { "type": "time_under", "value": 120 } ],
          "collectibles": 2,
          "rewards": { "credits": 4000, "xp": 300, "items": [ { "type": "paint", "id": "paint_rust" } ] },
          "story": { "intro": ["dlg.ch1_l1.intro.1"], "outro": ["dlg.ch1_l1.outro.1"] },
          "unlock": { "level": null, "stars": 0 } }
      ] }
  ] }
```

* Chapter `levels` are played in order; `unlock.level` = id of the level that must be completed first (`null` = previous level in the chapter, or nothing for the first level). `unlock.stars` = total campaign stars required.
* `maxTier`: highest vehicle tier the player may bring (`null` = any). `loaner`: vehicle id forced for this level (story levels) or `null`.
* `opponents[].team`: in FFA levels use distinct teams 1..n (player is team 0). In team levels the player's team is 0 and allies are listed in `allies` with `team: 0`.
* Objective / star `type` ∈ `win`, `takedowns` (value), `time_under` (value s), `no_wreck`, `damage_under` (value = % of HP lost), `score` (value), `hits_of_kind` (`kind` + `value`, kinds below), `survive` (value s), `destroy_props` (value), `hill_time` (value s), `collect` (value collectibles), `boss`.
* Exactly 3 `stars`. `collectibles` ≤ number of arena collectible positions.
* `story.intro/outro`: ordered dialogue keys. Dialogue strings are formatted `"Speaker|Line"` in the localization file (`Speaker` may be a key like `camp.rival_rust` or plain name).
* Reward `items[].type` ∈ `vehicle, equipment, paint, wheels, sparks, horn, title, banner, currency` (currency uses `id` = `credits`/`gold` and `amount`).

Impact kinds (used by scoring, stars, stats): `head_on, t_bone, side_swipe, rear_end, launch, crush, pit, glance`.

## progression.json

```json
{ "version": 1,
  "player": { "maxLevel": 100, "xpBase": 1000, "xpGrowth": 1.06 },
  "levelRewards": [ { "level": 2, "items": [ { "type": "currency", "id": "credits", "amount": 5000 } ] } ],
  "match": { "participationCredits": 800, "participationXp": 150, "creditsPerScore": 1.5,
             "xpPerScore": 0.8, "winMultiplier": 1.5, "rankedMultiplier": 1.25,
             "maxCredits": 40000, "maxXp": 20000 },
  "score": { "head_on": 250, "t_bone": 400, "side_swipe": 120, "rear_end": 120, "launch": 500,
             "crush": 600, "pit": 300, "glance": 0, "takedown": 1000, "assist": 350, "ring_out": 800,
             "revenge": 300, "multi_takedown": 500, "airtime_per_sec": 40, "hill_per_sec": 20,
             "survival_per_sec": 5, "prop_destroyed": 50, "combo_step": 0.2, "combo_max": 2.0,
             "combo_window": 4.0, "min_damage_for_points": 25 },
  "ranks": [ { "id": "bronze", "name": "rank.bronze", "minRating": 0, "divisions": 3, "color": "#B08D57" } ]
}
```

* XP needed to go from level `n` to `n+1`: `round(xpBase * xpGrowth^(n-1))`.
* Match rewards: `c = participationCredits + score * creditsPerScore`; `×winMultiplier` if winner; `×rankedMultiplier` if ranked; `min(round(c), maxCredits)`. Same for XP with the xp fields.
* `ranks` sorted by `minRating` ascending: `bronze, silver, gold, platinum, diamond, master, crash_king` (crash_king = top 100 of the season and rating ≥ its minRating). Division `d` (1 = highest) within a tier: split the tier's rating span evenly.

## store.json

```json
{ "version": 1,
  "offers": [
    { "id": "offer_fulmine_f8r", "section": "vehicles", "featured": true,
      "items": [ { "type": "vehicle", "id": "fulmine_f8r" } ],
      "price": { "gold": 1800 }, "limit": 1, "start": null, "end": null, "tag": "store.tag.new" }
  ],
  "goldPacks": [ { "id": "gold_s", "gold": 100, "bonus": 0, "prices": { "USD": 0.99, "CNY": 6 } } ],
  "daily": { "slots": 6, "discounts": [0.1, 0.2, 0.3, 0.5],
             "pool": [ { "type": "equipment", "id": "front_wedge_steel" } ] },
  "seasonPass": { "id": "season_1", "name": "store.season_1", "start": "2026-11-01T00:00:00Z",
                  "end": "2027-01-31T00:00:00Z", "premiumPrice": { "gold": 950 }, "xpPerTier": 1000,
                  "tiers": [ { "free": [ { "type": "currency", "id": "credits", "amount": 3000 } ],
                               "premium": [ { "type": "paint", "id": "paint_season1" } ] } ] }
}
```

* `section` ∈ `featured, vehicles, equipment, cosmetics, bundles`.
* `limit`: max purchases per player (0 = unlimited; vehicles/cosmetics are implicitly limited to 1 because you can only own one).
* Daily deals: for UTC day number `d = floor(unixSeconds / 86400)`, take `slots` distinct pool entries chosen by a PCG32 seeded with `d` (algorithm in the backend), each with a discount from `discounts`. Price = item base `price` × (1 − discount), credits only, rounded to 100.
* Season pass: 50–100 tiers, `free` and `premium` reward lists (may be empty).
* Real-money gold packs are sold only through a payment provider (see `docs/BACKEND_API.md`). **No randomized paid rewards (loot boxes).**

## graphics.json

Owned by the engine team (`Source/BigCar`), describes graphics settings → console variables. See the file for its own schema; it is consumed only by `CrashCore/Graphics`.

## Localization

`Localization/<lang>/*.json`, each `{ "version": 1, "lang": "zh-Hans", "strings": { "key": "text" } }`. All files in a language folder are merged; duplicate keys are an error. `zh-Hans` is the primary language and must contain every key; `en` should contain every key (missing keys fall back to `zh-Hans`). Placeholders use `{0}`, `{1}`.
