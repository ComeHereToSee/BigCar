-- BigCar backend schema v1.

CREATE TABLE accounts (
    player_id     TEXT PRIMARY KEY,
    device_id     TEXT UNIQUE,
    username      TEXT UNIQUE,
    password_hash TEXT NOT NULL DEFAULT '',
    created_at    TIMESTAMPTZ NOT NULL
);

-- The profile document is stored as JSONB exactly as served by the API.
-- version is bumped on every write and checked by updates.
CREATE TABLE profiles (
    player_id  TEXT PRIMARY KEY REFERENCES accounts (player_id) ON DELETE CASCADE,
    version    BIGINT NOT NULL,
    doc        JSONB NOT NULL,
    updated_at TIMESTAMPTZ NOT NULL
);

-- Ladder projection of profiles.doc->'ranked', maintained on every profile
-- write, so leaderboard queries can use an index.
CREATE TABLE ratings (
    queue           TEXT NOT NULL,
    player_id       TEXT NOT NULL REFERENCES accounts (player_id) ON DELETE CASCADE,
    season          TEXT NOT NULL,
    display_name    TEXT NOT NULL,
    rating          DOUBLE PRECISION NOT NULL,
    rd              DOUBLE PRECISION NOT NULL,
    wins            INTEGER NOT NULL,
    losses          INTEGER NOT NULL,
    tier            TEXT NOT NULL,
    division        INTEGER NOT NULL,
    placements_left INTEGER NOT NULL,
    updated_at      TIMESTAMPTZ NOT NULL,
    PRIMARY KEY (queue, player_id)
);

CREATE INDEX ratings_ladder_idx ON ratings (queue, season, rating DESC, player_id)
    WHERE placements_left = 0;

CREATE TABLE orders (
    order_id     TEXT PRIMARY KEY,
    player_id    TEXT NOT NULL REFERENCES accounts (player_id) ON DELETE CASCADE,
    provider     TEXT NOT NULL,
    pack_id      TEXT NOT NULL,
    gold         BIGINT NOT NULL,
    status       TEXT NOT NULL,
    external_id  TEXT NOT NULL DEFAULT '',
    redirect_url TEXT NOT NULL DEFAULT '',
    created_at   TIMESTAMPTZ NOT NULL,
    updated_at   TIMESTAMPTZ NOT NULL
);

CREATE INDEX orders_player_idx ON orders (player_id, created_at);

CREATE TABLE idempotency_keys (
    player_id    TEXT NOT NULL,
    key          TEXT NOT NULL,
    request_hash TEXT NOT NULL,
    status       INTEGER NOT NULL,
    body         BYTEA NOT NULL,
    created_at   TIMESTAMPTZ NOT NULL,
    PRIMARY KEY (player_id, key)
);

CREATE INDEX idempotency_keys_created_idx ON idempotency_keys (created_at);

CREATE TABLE match_results (
    match_id   TEXT PRIMARY KEY,
    server_id  TEXT NOT NULL,
    queue      TEXT NOT NULL,
    result     JSONB NOT NULL,
    rewards    JSONB NOT NULL,
    created_at TIMESTAMPTZ NOT NULL
);
