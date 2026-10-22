# BigCar Backend API (contract, v1)

The backend (`Backend/`, Go) is the authority for online accounts, the online garage/wallet, the store, parties,
matchmaking, the ranked ladder and dedicated-server allocation. Single-player campaign and LAN/custom games run fully
offline on a local profile and never call it.

* Transport: HTTPS + JSON (`Content-Type: application/json; charset=utf-8`). Base path `/v1`.
* Auth (players): `Authorization: Bearer <token>`. Tokens are HMAC-SHA256 signed (`v1.<b64url(payload)>.<b64url(sig)>`, payload `{"pid": "...", "exp": unix}`), 7-day TTL.
* Auth (dedicated servers): `X-Server-Key: <secret>` (shared secret from config / env `BIGCAR_SERVER_KEY`).
* Errors: non-2xx with body `{"error": {"code": "snake_case_code", "message": "human readable"}}`.
  Common codes: `bad_request, unauthorized, forbidden, not_found, conflict, insufficient_funds, not_owned, already_owned, level_locked, max_level, invalid_slot, limit_reached, party_full, not_leader, not_ready, rate_limited, internal`.
* All money-changing POSTs accept an optional `Idempotency-Key` header; a repeated key returns the first response.
* Game data (prices, store, progression, modes) is loaded at startup from the same JSON files the game uses
  (`--data-dir`, default `../Content/BigCar/Data`), see `docs/DATA_SCHEMA.md`.

## Profile document

Every garage/store endpoint returns `{"profile": Profile}`. The JSON layout is shared with the game's local save
(`CrashCore/Profile`), so the client parses both with the same code.

```json
{
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
}
```

* Missing `upgrades` keys = level 0. Missing `equipment` slots = empty.
* Cosmetics/equipment with price `{ "credits": 0 }` are implicitly owned by everyone.
* New accounts get `wallet = {credits: 25000, gold: 0}`, every `starter: true` vehicle, the first starter selected.

## Auth

| method | path | body | response |
|---|---|---|---|
| POST | `/v1/auth/guest` | `{"deviceId": "..."}` (8–128 chars) | `{"token", "playerId", "displayName", "isNew"}` |
| POST | `/v1/auth/register` | `{"username", "password", "displayName"}` (username 3–24 `[a-z0-9_]`, password ≥ 8) | same as above |
| POST | `/v1/auth/login` | `{"username", "password"}` | same as above |
| POST | `/v1/auth/link` (auth) | `{"username", "password"}` — upgrade a guest account to a named one | `{"ok": true}` |

Passwords: PBKDF2-HMAC-SHA256, 210 000 iterations, 16-byte salt.

## Profile & garage (auth)

| method | path | body | notes |
|---|---|---|---|
| GET | `/v1/profile` | – | |
| POST | `/v1/profile/name` | `{"displayName"}` | 2–16 chars, trimmed |
| POST | `/v1/garage/buy-vehicle` | `{"vehicleId"}` | credits price, `unlockLevel` check |
| POST | `/v1/garage/select` | `{"vehicleId"}` | must own |
| POST | `/v1/garage/upgrade` | `{"vehicleId", "category"}` | next level, cost per DATA_SCHEMA formula, max 5 |
| POST | `/v1/garage/buy-equipment` | `{"itemId"}` | credits price, `unlockLevel` |
| POST | `/v1/garage/equip` | `{"vehicleId", "slot", "itemId"}` | `itemId: null` unequips; slot must match item; `allowedClasses` |
| POST | `/v1/garage/buy-cosmetic` | `{"itemId"}` | credits price |
| POST | `/v1/garage/customize` | `{"vehicleId", "paint", "wheels"}` | either may be null = unchanged; must own |

## Store (auth)

| method | path | body | response |
|---|---|---|---|
| GET | `/v1/store` | – | see below |
| POST | `/v1/store/purchase` | `{"offerId"}` or `{"dailySlot": n}` | `{"profile", "granted": [Item]}` |
| POST | `/v1/store/gold/checkout` | `{"packId", "provider": "sandbox"}` | `{"orderId", "status": "pending" \| "completed", "redirectUrl", "profile"?}` |
| POST | `/v1/payments/webhook/{provider}` | provider specific, `X-Signature: hex(hmac_sha256(secret, body))` | `{"ok": true}` |
| POST | `/v1/seasonpass/buy-premium` | – | `{"profile"}` |
| POST | `/v1/seasonpass/claim` | `{"tier": n, "track": "free" \| "premium"}` | `{"profile", "granted"}` |

`GET /v1/store` response:

```json
{
  "serverTime": "2026-10-22T08:00:00Z",
  "offers": [ { "id", "section", "featured", "items": [Item], "price": Price, "limit", "purchased", "tag", "end" } ],
  "daily": { "day": 20383, "expiresAt": "2026-10-23T00:00:00Z",
             "deals": [ { "slot": 0, "item": Item, "basePrice": Price, "price": Price, "discount": 0.3, "purchased": false } ] },
  "goldPacks": [ { "id", "gold", "bonus", "prices": { "USD": 0.99, "CNY": 6 } } ],
  "seasonPass": { "id", "name", "start", "end", "premiumPrice", "xpPerTier", "tiers": [...],
                  "playerXp", "playerTier", "premium", "claimedFree", "claimedPremium" }
}
```

`Item = {"type": "vehicle|equipment|paint|wheels|sparks|horn|title|banner|currency", "id": "...", "amount": n?}`.
Granting an already-owned item converts it into credits worth 50% of its base price (never happens for offers whose items are all owned: those return `already_owned`).

Payment providers: `sandbox` (dev only, completes immediately). Production providers (Steam MicroTxn, Epic, WeChat Pay, Alipay, Apple/Google) plug in behind the `payments.Provider` interface; the store never trusts the client for real-money purchases.

## Party (auth)

Max 3 members (largest team size). Party JSON:
`{"partyId", "code" (6 chars A-Z0-9, no 0/O/1/I), "leaderId", "members": [{"playerId", "displayName", "ready"}], "ticketId": null}`.

| method | path | body |
|---|---|---|
| GET | `/v1/party` | → `{"party": Party \| null}` |
| POST | `/v1/party` | create → `{"party"}` (leaves any current party) |
| POST | `/v1/party/join` | `{"code"}` |
| POST | `/v1/party/leave` | – (leader leaving promotes the next member; empty party is deleted) |
| POST | `/v1/party/ready` | `{"ready": bool}` |
| POST | `/v1/party/kick` | `{"playerId"}` (leader only) |

## Matchmaking (auth)

Queues:

| queue | team size | teams | modes (rotated) | bots fill | ranked |
|---|---|---|---|---|---|
| `casual_ffa` | 1 | up to 8 players | derby, takedown, ring_out, bounty | yes, after 25 s | no |
| `casual_teams` | 3 | 2 | team_takedown, king_of_hill, team_elimination | yes, after 25 s | no |
| `ranked_1v1` | 1 | 2 | team_elimination | no | yes |
| `ranked_2v2` | 2 | 2 | team_elimination | no | yes |
| `ranked_3v3` | 3 | 2 | team_elimination | no | yes |

| method | path | body / response |
|---|---|---|
| POST | `/v1/matchmaking/tickets` | `{"queue", "region"?}` → `{"ticket"}` (empty region = any). In a party only the leader may queue, all members must be ready, party size ≤ team size. |
| GET | `/v1/matchmaking/current` | `{"ticket": Ticket \| null}` — works for every party member |
| DELETE | `/v1/matchmaking/tickets/{ticketId}` | cancel (any member) |

`Ticket = {"ticketId", "queue", "status": "searching" | "matched" | "cancelled" | "expired", "createdAt", "waitSec", "estimatedWaitSec", "reason"?, "match"?}`
`match = {"matchId", "address": "host:port", "joinToken", "team", "mode", "arena"}` (`joinToken` is per player).

Matchmaker (runs every second): tickets sorted by rating (ranked: queue rating; casual: hidden MMR = average queue rating or 1500), rating window `150 + 20 * waitSec` (ranked max 800, casual unlimited after 20 s). Parties are packed into teams (largest first), teams balanced by snake draft on average rating. A match needs an `idle` registered server in the ticket's region (any region after 30 s). Tickets expire after 10 minutes.

## Ladder

| method | path | response |
|---|---|---|
| GET | `/v1/ladder/{queue}?offset=0&limit=50` | `{"queue", "season", "total", "entries": [{"rank", "playerId", "displayName", "rating", "tier", "division", "wins", "losses"}]}` (placement players excluded) |
| GET | `/v1/ladder/{queue}/me` (auth) | `{"entry": Entry \| null, "placementsLeft"}` |
| GET | `/v1/seasons/current` | `{"id", "name", "start", "end"}` |

Rating: Glicko-2 (τ = 0.5, initial 1500 / RD 350 / σ 0.06). In team games each player is rated against a single
virtual opponent with the enemy team's mean rating and RMS RD. 5 placement matches. Tier/division from
`progression.json` `ranks`; `crash_king` additionally requires top-100 of the queue. Season reset: `r = 1500 + (r − 1500) * 0.5`, `RD = max(RD, 200)`.

## Dedicated servers (`X-Server-Key`)

One match per server process: boot → `idle` → assignment → play → report → exit (orchestrator restarts it).

| method | path | body | response |
|---|---|---|---|
| POST | `/v1/servers/heartbeat` | `{"serverId", "address", "port", "region", "version", "state": "idle" \| "reserved" \| "in_match", "matchId"?}` every 5 s; servers silent for 20 s are dropped | `{"assignment": Assignment \| null}` |
| POST | `/v1/servers/match-start` | `{"serverId", "matchId"}` | `{"ok": true}` |
| POST | `/v1/servers/match-result` | MatchResult (+ `serverId`) | `{"rewards": [...]}` |
| POST | `/v1/servers/match-abort` | `{"serverId", "matchId", "reason"}` | `{"ok": true}`; the match's tickets become `cancelled` with `reason: "server_aborted"` and players re-queue |

```json
Assignment = {
  "matchId": "m_...", "queue": "ranked_3v3", "mode": "team_elimination", "arena": "steel_dome",
  "ranked": true, "teamSize": 3, "fillWithBots": false, "botAi": "regular", "maxWaitSec": 60,
  "teams": [ [ { "playerId", "displayName", "joinToken", "rating",
                 "loadout": { "vehicleId", "upgrades": {}, "equipment": {}, "paint", "wheels" } } ], [ ... ] ]
}
MatchResult = {
  "matchId", "durationSec", "winnerTeam": 0,
  "players": [ { "playerId", "team", "score", "takedowns", "assists", "wrecked", "damageDealt",
                 "placement", "disconnected": false, "impacts": { "t_bone": 2 } } ]
}
reward = { "playerId", "credits", "xp", "levelBefore", "levelAfter", "ratingBefore", "ratingAfter", "tier", "division" }
```

The backend validates results: only the assigned server may report, once per match, only listed players; scores are clamped
(`score ≤ 200000`, `takedowns ≤ 100`). Rewards use the `progression.json` formula; disconnected players get no win bonus
and count as a loss in ranked.

Clients join a dedicated server with `open <address>?MatchId=<id>?JoinToken=<token>?PlayerId=<pid>`; the server checks the
token against its assignment.

## Misc

* `GET /healthz` → `{"ok": true}`; `GET /v1/meta` → `{"apiVersion": 1, "dataHash", "serverTime", "season"}`.
* Rate limiting: 20 req/s per player (burst 40), auth endpoints 5 req/s per IP.
* Config (flags / env): `--addr :8080`, `--data-dir`, `--database-url` (`BIGCAR_DATABASE_URL`, Postgres; empty = in-memory),
  `--token-secret` (`BIGCAR_TOKEN_SECRET`), `--server-key` (`BIGCAR_SERVER_KEY`), `--dev` (`BIGCAR_DEV=1`: sandbox payments, permissive CORS).

## Clarifications (implemented behaviour)

* Server calls other than heartbeat identify the server with a `serverId` body field (or `X-Server-Id` header); only the
  assigned server may start/report/abort its match. A server acknowledges an assignment by heartbeating its `matchId`;
  not acknowledging within 30 s, reporting idle afterwards, or going silent for 20 s aborts the match (`server_lost`).
* `xp` in the profile is progress inside the current level (carried over on level-up, 0 at max level) — same rule as the
  game's local save (`CrashCore/Profile.cpp`).
* The party leader counts as ready; any party change cancels a searching party ticket (`party_changed`).
* Duplicate grants convert to 50% of the base price (rounded down) in the base price's currency.
* Free-for-all assignments use one-player teams; empty team arrays are bot slots. Casual queues always `fillWithBots`.
* Season = `store.json` `seasonPass`; a season change soft-resets ratings the next time the profile loads.
* HTTP statuses: 400 bad_request/invalid_slot, 401, 402 insufficient_funds, 403 forbidden/not_owned/level_locked/not_leader,
  404, 409 other business errors, 429, 500.
