package httpapi_test

import (
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"log/slog"
	"net/http"
	"net/http/httptest"
	"strings"
	"sync"
	"testing"
	"time"

	"github.com/comeheretosee/bigcar/backend/internal/auth"
	"github.com/comeheretosee/bigcar/backend/internal/data"
	"github.com/comeheretosee/bigcar/backend/internal/data/datatest"
	"github.com/comeheretosee/bigcar/backend/internal/httpapi"
	"github.com/comeheretosee/bigcar/backend/internal/ladder"
	"github.com/comeheretosee/bigcar/backend/internal/matchmaking"
	"github.com/comeheretosee/bigcar/backend/internal/party"
	"github.com/comeheretosee/bigcar/backend/internal/payments"
	"github.com/comeheretosee/bigcar/backend/internal/profile"
	"github.com/comeheretosee/bigcar/backend/internal/servers"
	"github.com/comeheretosee/bigcar/backend/internal/storage"
	"github.com/comeheretosee/bigcar/backend/internal/storage/storagetest"
	"github.com/comeheretosee/bigcar/backend/internal/store"
)

const serverKey = "test-server-key"

type fakeClock struct {
	mu sync.Mutex
	t  time.Time
}

func (c *fakeClock) Now() time.Time { c.mu.Lock(); defer c.mu.Unlock(); return c.t }
func (c *fakeClock) Add(d time.Duration) {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.t = c.t.Add(d)
}

type testEnv struct {
	t     *testing.T
	srv   *httptest.Server
	clock *fakeClock
	gd    *data.GameData
	mm    *matchmaking.Service
	reg   *servers.Registry
	st    storage.Store
}

type envOptions struct {
	authLimit, playerLimit *httpapi.Limiter
	dev                    bool
	store                  storage.Store // nil = in-memory
}

func newTestEnv(t *testing.T, opt envOptions) *testEnv {
	t.Helper()
	gd := datatest.Load(t)
	clock := &fakeClock{t: time.Date(2026, 11, 15, 12, 0, 0, 0, time.UTC)}
	log := slog.New(slog.NewTextHandler(io.Discard, nil))
	st := opt.store
	if st == nil {
		st = storage.NewMemory()
	}
	authSvc, err := auth.NewService(st, gd, auth.NewSigner([]byte("test-token-secret"), auth.TokenTTL, clock.Now),
		auth.Hasher{Iterations: 1000}, clock.Now)
	if err != nil {
		t.Fatal(err)
	}
	shop := store.NewService(gd, st, payments.NewRegistry(&payments.Sandbox{WebhookSecret: []byte("whsec")}), clock.Now, log)
	parties := party.NewManager(nil)
	reg := servers.NewRegistry(clock.Now, log)
	mm, err := matchmaking.New(matchmaking.Config{Data: gd, Store: st, Parties: parties, Allocator: reg, Now: clock.Now, Log: log})
	if err != nil {
		t.Fatal(err)
	}
	reg.SetListener(mm)
	if opt.playerLimit == nil {
		opt.playerLimit = httpapi.NewLimiter(1e6, 1e6, nil)
	}
	if opt.authLimit == nil {
		opt.authLimit = httpapi.NewLimiter(1e6, 1e6, nil)
	}
	api := httpapi.New(httpapi.Deps{
		Data: gd, Store: st, Auth: authSvc, Shop: shop, Parties: parties, Matchmaker: mm, Registry: reg,
		Results:   servers.NewResultProcessor(reg, st, gd, clock.Now, log),
		Ladder:    ladder.NewService(st, gd, matchmaking.RankedQueueIDs()),
		ServerKey: serverKey, Dev: opt.dev, Log: log, Now: clock.Now,
		PlayerLimit: opt.playerLimit, AuthLimit: opt.authLimit,
	})
	srv := httptest.NewServer(api.Handler())
	t.Cleanup(srv.Close)
	return &testEnv{t: t, srv: srv, clock: clock, gd: gd, mm: mm, reg: reg, st: st}
}

type resp struct {
	status int
	header http.Header
	body   []byte
}

func (r resp) decode(t *testing.T, v any) {
	t.Helper()
	if err := json.Unmarshal(r.body, v); err != nil {
		t.Fatalf("decode %s: %v", r.body, err)
	}
}

func (r resp) errCode(t *testing.T) string {
	t.Helper()
	var e struct {
		Error struct {
			Code    string `json:"code"`
			Message string `json:"message"`
		} `json:"error"`
	}
	r.decode(t, &e)
	if e.Error.Message == "" {
		t.Fatalf("error without message: %s", r.body)
	}
	return e.Error.Code
}

func (e *testEnv) do(method, path, token string, body any, hdr map[string]string) resp {
	e.t.Helper()
	var rd io.Reader
	switch b := body.(type) {
	case nil:
	case string:
		rd = strings.NewReader(b)
	default:
		buf, err := json.Marshal(b)
		if err != nil {
			e.t.Fatal(err)
		}
		rd = bytes.NewReader(buf)
	}
	req, err := http.NewRequest(method, e.srv.URL+path, rd)
	if err != nil {
		e.t.Fatal(err)
	}
	if body != nil {
		req.Header.Set("Content-Type", "application/json")
	}
	if token != "" {
		req.Header.Set("Authorization", "Bearer "+token)
	}
	for k, v := range hdr {
		req.Header.Set(k, v)
	}
	res, err := e.srv.Client().Do(req)
	if err != nil {
		e.t.Fatal(err)
	}
	defer res.Body.Close()
	b, _ := io.ReadAll(res.Body)
	if ct := res.Header.Get("Content-Type"); res.StatusCode != http.StatusNoContent && ct != "application/json; charset=utf-8" {
		e.t.Fatalf("%s %s: content type %q", method, path, ct)
	}
	return resp{status: res.StatusCode, header: res.Header, body: b}
}

// ok performs a request that must succeed and decodes the response.
func (e *testEnv) ok(method, path, token string, body any, out any) resp {
	e.t.Helper()
	r := e.do(method, path, token, body, nil)
	if r.status != http.StatusOK {
		e.t.Fatalf("%s %s: %d %s", method, path, r.status, r.body)
	}
	if out != nil {
		r.decode(e.t, out)
	}
	return r
}

// fail performs a request that must fail with the given status and code.
func (e *testEnv) fail(method, path, token string, body any, status int, code string) {
	e.t.Helper()
	r := e.do(method, path, token, body, nil)
	if r.status != status || r.errCode(e.t) != code {
		e.t.Fatalf("%s %s: got %d %s, want %d %s", method, path, r.status, r.body, status, code)
	}
}

func (e *testEnv) server(path string, body any, hdr map[string]string) resp {
	e.t.Helper()
	h := map[string]string{"X-Server-Key": serverKey}
	for k, v := range hdr {
		h[k] = v
	}
	return e.do(http.MethodPost, path, "", body, h)
}

type authRes struct {
	Token       string `json:"token"`
	PlayerID    string `json:"playerId"`
	DisplayName string `json:"displayName"`
	IsNew       bool   `json:"isNew"`
}

type profileRes struct {
	Profile profile.Profile `json:"profile"`
	Granted []data.Item     `json:"granted"`
}

type ticketRes struct {
	Ticket *matchmaking.TicketView `json:"ticket"`
}

type partyRes struct {
	Party *struct {
		PartyID  string         `json:"partyId"`
		Code     string         `json:"code"`
		LeaderID string         `json:"leaderId"`
		Members  []party.Member `json:"members"`
		TicketID *string        `json:"ticketId"`
	} `json:"party"`
}

func TestEndToEnd(t *testing.T) {
	runEndToEnd(t, newTestEnv(t, envOptions{}))
}

// TestEndToEndPostgres runs the same flow on Postgres when
// BIGCAR_TEST_DATABASE_URL is set.
func TestEndToEndPostgres(t *testing.T) {
	runEndToEnd(t, newTestEnv(t, envOptions{store: storagetest.Postgres(t)}))
}

func runEndToEnd(t *testing.T, e *testEnv) {

	// --- misc ---
	var health map[string]bool
	e.ok("GET", "/healthz", "", nil, &health)
	var meta struct {
		APIVersion int       `json:"apiVersion"`
		DataHash   string    `json:"dataHash"`
		ServerTime time.Time `json:"serverTime"`
		Season     string    `json:"season"`
	}
	e.ok("GET", "/v1/meta", "", nil, &meta)
	if !health["ok"] || meta.APIVersion != 1 || meta.DataHash != e.gd.Hash || meta.Season != "season_1" ||
		!meta.ServerTime.Equal(e.clock.Now()) {
		t.Fatalf("meta %+v", meta)
	}

	// --- accounts ---
	var a, b, a2 authRes
	e.ok("POST", "/v1/auth/guest", "", map[string]string{"deviceId": "device-alpha-001"}, &a)
	e.ok("POST", "/v1/auth/guest", "", map[string]string{"deviceId": "device-bravo-002"}, &b)
	e.ok("POST", "/v1/auth/guest", "", map[string]string{"deviceId": "device-alpha-001"}, &a2)
	if !a.IsNew || !b.IsNew || a2.IsNew || a2.PlayerID != a.PlayerID || a.Token == "" {
		t.Fatalf("guest logins %+v %+v %+v", a, b, a2)
	}
	e.fail("POST", "/v1/auth/guest", "", map[string]string{"deviceId": "short"}, 400, "bad_request")
	e.fail("GET", "/v1/profile", "", nil, 401, "unauthorized")
	e.fail("GET", "/v1/profile", "garbage", nil, 401, "unauthorized")

	// --- garage ---
	var p profileRes
	e.ok("GET", "/v1/profile", a.Token, nil, &p)
	if p.Profile.Wallet.Credits != 25000 || p.Profile.PlayerID != a.PlayerID || p.Profile.Garage.Selected != "hoshino_corolla" {
		t.Fatalf("new profile %+v", p.Profile)
	}
	buy := e.do("POST", "/v1/garage/buy-vehicle", a.Token, map[string]string{"vehicleId": "hoshino_supra"},
		map[string]string{"Idempotency-Key": "buy-supra-1"})
	replay := e.do("POST", "/v1/garage/buy-vehicle", a.Token, map[string]string{"vehicleId": "hoshino_supra"},
		map[string]string{"Idempotency-Key": "buy-supra-1"})
	if buy.status != 200 || replay.status != 200 || !bytes.Equal(buy.body, replay.body) ||
		replay.header.Get("Idempotent-Replayed") != "true" {
		t.Fatalf("idempotent buy: %d %s / %d %s", buy.status, buy.body, replay.status, replay.body)
	}
	buy.decode(t, &p)
	if p.Profile.Wallet.Credits != 5000 {
		t.Fatalf("credits after supra %d", p.Profile.Wallet.Credits)
	}
	e.fail("POST", "/v1/garage/buy-vehicle", a.Token, map[string]string{"vehicleId": "hoshino_supra"}, 409, "already_owned")
	e.fail("POST", "/v1/garage/upgrade", a.Token, map[string]string{"vehicleId": "hoshino_supra", "category": "engine"}, 402, "insufficient_funds")
	e.ok("POST", "/v1/garage/upgrade", a.Token, map[string]string{"vehicleId": "hoshino_corolla", "category": "engine"}, &p)
	if p.Profile.Wallet.Credits != 1000 || p.Profile.Vehicle("hoshino_corolla").Upgrades["engine"] != 1 {
		t.Fatalf("after upgrade %+v", p.Profile.Wallet)
	}
	e.fail("POST", "/v1/garage/upgrade", a.Token, map[string]string{"vehicleId": "hoshino_corolla"}, 400, "bad_request")
	e.fail("POST", "/v1/garage/equip", a.Token, map[string]any{"vehicleId": "hoshino_corolla", "slot": "rear", "itemId": "front_bar_basic"}, 400, "invalid_slot")
	e.fail("POST", "/v1/garage/buy-equipment", a.Token, map[string]string{"itemId": "front_wedge_steel"}, 403, "level_locked")

	// Gold via the sandbox provider, then a gold offer.
	var co store.CheckoutResponse
	e.ok("POST", "/v1/store/gold/checkout", a.Token, map[string]string{"packId": "gold_l", "provider": "sandbox"}, &co)
	if co.Status != "completed" || co.Profile == nil || co.Profile.Wallet.Gold != 1200 {
		t.Fatalf("checkout %+v", co)
	}
	// A provider retry of the webhook does not credit twice; a forged one is rejected.
	whBody := fmt.Sprintf(`{"orderId":%q,"status":"completed"}`, co.OrderID)
	wh := e.do("POST", "/v1/payments/webhook/sandbox", "", whBody,
		map[string]string{"X-Signature": payments.Sign([]byte("whsec"), []byte(whBody))})
	forged := e.do("POST", "/v1/payments/webhook/sandbox", "", whBody, map[string]string{"X-Signature": "00"})
	if wh.status != 200 || forged.status != 401 || forged.errCode(t) != "unauthorized" {
		t.Fatalf("webhooks %d %s / %d %s", wh.status, wh.body, forged.status, forged.body)
	}
	e.ok("POST", "/v1/store/purchase", a.Token, map[string]string{"offerId": "offer_starter_pack"}, &p)
	if p.Profile.Wallet.Gold != 900 || p.Profile.Wallet.Credits != 11000 || len(p.Granted) != 3 {
		t.Fatalf("starter pack %+v granted %v", p.Profile.Wallet, p.Granted)
	}
	e.fail("POST", "/v1/store/purchase", a.Token, map[string]string{"offerId": "offer_starter_pack"}, 409, "limit_reached")
	e.ok("POST", "/v1/garage/equip", a.Token, map[string]any{"vehicleId": "hoshino_corolla", "slot": "front", "itemId": "front_wedge_steel"}, &p)
	e.ok("POST", "/v1/garage/customize", a.Token, map[string]any{"vehicleId": "hoshino_corolla", "paint": "paint_rosso", "wheels": nil}, &p)
	e.ok("POST", "/v1/garage/select", a.Token, map[string]string{"vehicleId": "hoshino_corolla"}, &p)
	e.ok("POST", "/v1/profile/name", a.Token, map[string]string{"displayName": "  Ace  "}, &p)
	if p.Profile.DisplayName != "Ace" {
		t.Fatalf("name %q", p.Profile.DisplayName)
	}

	var storeView store.View
	e.ok("GET", "/v1/store", a.Token, nil, &storeView)
	if len(storeView.Daily.Deals) != 4 || storeView.SeasonPass.ID != "season_1" || len(storeView.GoldPacks) != 3 {
		t.Fatalf("store view %+v", storeView.Daily)
	}

	// --- party ---
	var pa, pb partyRes
	e.ok("POST", "/v1/party", a.Token, nil, &pa)
	e.ok("POST", "/v1/party/join", b.Token, map[string]string{"code": pa.Party.Code}, &pb)
	if len(pb.Party.Members) != 2 || pb.Party.LeaderID != a.PlayerID || pb.Party.Members[0].DisplayName != "Ace" {
		t.Fatalf("party %+v", pb.Party)
	}
	e.fail("POST", "/v1/matchmaking/tickets", a.Token, map[string]string{"queue": "ranked_2v2"}, 409, "not_ready")
	e.fail("POST", "/v1/party/kick", b.Token, map[string]string{"playerId": a.PlayerID}, 403, "not_leader")
	e.ok("POST", "/v1/party/ready", b.Token, map[string]bool{"ready": true}, &pb)
	e.fail("POST", "/v1/matchmaking/tickets", b.Token, map[string]string{"queue": "ranked_1v1"}, 403, "not_leader")
	e.fail("POST", "/v1/matchmaking/tickets", a.Token, map[string]string{"queue": "ranked_1v1"}, 400, "bad_request")
	var tr ticketRes
	e.ok("POST", "/v1/matchmaking/tickets", a.Token, map[string]string{"queue": "ranked_2v2"}, &tr)
	e.ok("GET", "/v1/party", b.Token, nil, &pb)
	if pb.Party.TicketID == nil || *pb.Party.TicketID != tr.Ticket.TicketID {
		t.Fatalf("party ticket %+v", pb.Party)
	}
	var tb ticketRes
	e.ok("GET", "/v1/matchmaking/current", b.Token, nil, &tb)
	if tb.Ticket == nil || tb.Ticket.TicketID != tr.Ticket.TicketID {
		t.Fatalf("member current %+v", tb.Ticket)
	}
	// B leaves: the party ticket is cancelled, the party dissolves to A alone.
	e.ok("POST", "/v1/party/leave", b.Token, nil, &pb)
	if pb.Party != nil {
		t.Fatalf("after leave %+v", pb.Party)
	}
	e.ok("GET", "/v1/matchmaking/current", a.Token, nil, &tr)
	if tr.Ticket.Status != "cancelled" || tr.Ticket.Reason != "party_changed" {
		t.Fatalf("ticket after leave %+v", tr.Ticket)
	}
	e.ok("POST", "/v1/party/leave", a.Token, nil, &pa)

	// --- ranked 1v1 ---
	rewardsA := map[string]int64{}
	for round := 1; round <= 5; round++ {
		var ta, tb ticketRes
		e.ok("POST", "/v1/matchmaking/tickets", a.Token, map[string]string{"queue": "ranked_1v1", "region": "eu"}, &ta)
		e.ok("POST", "/v1/matchmaking/tickets", b.Token, map[string]string{"queue": "ranked_1v1", "region": "eu"}, &tb)
		if ta.Ticket.Status != "searching" {
			t.Fatalf("round %d ticket %+v", round, ta.Ticket)
		}
		sid := fmt.Sprintf("eu-%d", round)
		hb := map[string]any{"serverId": sid, "address": "203.0.113.7", "port": 7000 + round, "region": "eu",
			"version": "1.0.0", "state": "idle"}
		if r := e.do("POST", "/v1/servers/heartbeat", "", hb, map[string]string{"X-Server-Key": "wrong"}); r.status != 401 {
			t.Fatalf("bad server key: %d", r.status)
		}
		var hbRes struct {
			Assignment *servers.Assignment `json:"assignment"`
		}
		e.server("/v1/servers/heartbeat", hb, nil).decode(t, &hbRes)
		if hbRes.Assignment != nil {
			t.Fatal("assignment before matchmaking")
		}
		// The rating gap grows every round, so the window (150 + 20/s) needs
		// a few seconds to cover it; the server keeps heartbeating meanwhile.
		for i := 0; i < 40; i++ {
			e.clock.Add(time.Second)
			e.server("/v1/servers/heartbeat", hb, nil)
			e.mm.Tick(e.clock.Now())
			e.ok("GET", "/v1/matchmaking/current", a.Token, nil, &ta)
			if ta.Ticket.Status != "searching" {
				break
			}
		}
		e.ok("GET", "/v1/matchmaking/current", b.Token, nil, &tb)
		if ta.Ticket.Status != "matched" || tb.Ticket.Match == nil || ta.Ticket.Match.MatchID != tb.Ticket.Match.MatchID {
			t.Fatalf("round %d not matched: %+v / %+v", round, ta.Ticket, tb.Ticket)
		}
		if ta.Ticket.Match.Address != fmt.Sprintf("203.0.113.7:%d", 7000+round) || ta.Ticket.Match.Mode != "team_elimination" {
			t.Fatalf("match info %+v", ta.Ticket.Match)
		}
		matchID := ta.Ticket.Match.MatchID

		e.server("/v1/servers/heartbeat", hb, nil).decode(t, &hbRes)
		asg := hbRes.Assignment
		if asg == nil || asg.MatchID != matchID || asg.Queue != "ranked_1v1" || !asg.Ranked || len(asg.Teams) != 2 {
			t.Fatalf("assignment %+v", asg)
		}
		tokens := map[string]string{}
		for _, team := range asg.Teams {
			for _, pl := range team {
				tokens[pl.PlayerID] = pl.JoinToken
				if pl.PlayerID == a.PlayerID {
					l := pl.Loadout
					if l.VehicleID != "hoshino_corolla" || l.Upgrades["engine"] != 1 || l.Equipment["front"] != "front_wedge_steel" ||
						l.Paint != "paint_rosso" || pl.DisplayName != "Ace" {
						t.Fatalf("loadout %+v", pl)
					}
				}
			}
		}
		if tokens[a.PlayerID] != ta.Ticket.Match.JoinToken || tokens[b.PlayerID] != tb.Ticket.Match.JoinToken {
			t.Fatal("join tokens differ between ticket and assignment")
		}

		hb["state"], hb["matchId"] = "reserved", matchID
		e.server("/v1/servers/heartbeat", hb, nil)
		if r := e.server("/v1/servers/match-start", map[string]string{"matchId": matchID}, map[string]string{"X-Server-Id": sid}); r.status != 200 {
			t.Fatalf("match-start %d %s", r.status, r.body)
		}
		result := map[string]any{
			"matchId": matchID, "durationSec": 180, "winnerTeam": ta.Ticket.Match.Team,
			"players": []map[string]any{
				{"playerId": a.PlayerID, "team": ta.Ticket.Match.Team, "score": 1000, "takedowns": 2, "assists": 0,
					"wrecked": 0, "damageDealt": 1800.5, "placement": 1, "disconnected": false, "impacts": map[string]int{"t_bone": 2}},
				{"playerId": b.PlayerID, "team": tb.Ticket.Match.Team, "score": 400, "takedowns": 0, "assists": 0,
					"wrecked": 2, "damageDealt": 700, "placement": 2, "disconnected": false, "impacts": map[string]int{}},
			},
		}
		if r := e.server("/v1/servers/match-result", result, map[string]string{"X-Server-Id": "intruder"}); r.status != 403 {
			t.Fatalf("result from another server: %d %s", r.status, r.body)
		}
		var rr struct {
			Rewards []servers.Reward `json:"rewards"`
		}
		r := e.server("/v1/servers/match-result", result, map[string]string{"X-Server-Id": sid})
		if r.status != 200 {
			t.Fatalf("match-result %d %s", r.status, r.body)
		}
		r.decode(t, &rr)
		if dup := e.server("/v1/servers/match-result", result, map[string]string{"X-Server-Id": sid}); dup.status != 409 {
			t.Fatalf("duplicate result %d", dup.status)
		}
		for _, rw := range rr.Rewards {
			if rw.PlayerID == a.PlayerID {
				rewardsA["credits"] += rw.Credits
				if *rw.RatingAfter <= *rw.RatingBefore {
					t.Fatalf("winner rating did not rise: %+v", rw)
				}
				if round == 1 && (rw.Credits != 4313 || rw.XP != 1781 || *rw.RatingBefore != 1500) {
					t.Fatalf("round 1 reward %+v", rw)
				}
			} else if *rw.RatingAfter >= *rw.RatingBefore {
				t.Fatalf("loser rating did not fall: %+v", rw)
			}
		}
		if round == 1 {
			var lad ladder.Page
			e.ok("GET", "/v1/ladder/ranked_1v1", "", nil, &lad)
			var me ladder.Me
			e.ok("GET", "/v1/ladder/ranked_1v1/me", a.Token, nil, &me)
			if lad.Total != 0 || me.Entry != nil || me.PlacementsLeft != 4 {
				t.Fatalf("placement ladder %+v / %+v", lad, me)
			}
		}
		e.clock.Add(5 * time.Minute)
	}

	// --- results reflected in profiles and the ladder ---
	e.ok("GET", "/v1/profile", a.Token, nil, &p)
	ra := p.Profile.Ranked["ranked_1v1"]
	if p.Profile.Stats.Matches != 5 || p.Profile.Stats.Wins != 5 || p.Profile.Stats.Impacts["t_bone"] != 10 ||
		ra.Wins != 5 || ra.PlacementsLeft != 0 || ra.Rating <= 1500 || p.Profile.Level < 2 {
		t.Fatalf("profile after 5 wins: stats %+v ranked %+v level %d", p.Profile.Stats, ra, p.Profile.Level)
	}
	if p.Profile.SeasonPass.XP == 0 {
		t.Fatal("no season pass xp")
	}
	var lad ladder.Page
	e.ok("GET", "/v1/ladder/ranked_1v1?offset=0&limit=10", "", nil, &lad)
	if lad.Total != 2 || len(lad.Entries) != 2 || lad.Entries[0].PlayerID != a.PlayerID || lad.Entries[0].Rank != 1 ||
		lad.Entries[0].DisplayName != "Ace" || lad.Entries[1].Losses != 5 || lad.Season != "season_1" {
		t.Fatalf("ladder %+v", lad)
	}
	if lad.Entries[0].Tier != ra.Tier || lad.Entries[0].Division != ra.Division {
		t.Fatalf("ladder tier %s %d vs profile %s %d", lad.Entries[0].Tier, lad.Entries[0].Division, ra.Tier, ra.Division)
	}
	var me ladder.Me
	e.ok("GET", "/v1/ladder/ranked_1v1/me", b.Token, nil, &me)
	if me.Entry == nil || me.Entry.Rank != 2 || me.PlacementsLeft != 0 {
		t.Fatalf("me %+v", me)
	}
	e.fail("GET", "/v1/ladder/casual_ffa", "", nil, 404, "not_found")
	e.fail("GET", "/v1/ladder/ranked_1v1?limit=abc", "", nil, 400, "bad_request")
	var season ladder.Season
	e.ok("GET", "/v1/seasons/current", "", nil, &season)
	if season.ID != "season_1" {
		t.Fatalf("season %+v", season)
	}

	// --- season pass with the XP earned ---
	e.ok("POST", "/v1/seasonpass/claim", a.Token, map[string]any{"tier": 1, "track": "free"}, &p)
	if len(p.Granted) != 1 || !contains(p.Profile.SeasonPass.ClaimedFree, 1) {
		t.Fatalf("claim %+v", p)
	}
	e.fail("POST", "/v1/seasonpass/claim", a.Token, map[string]any{"tier": 1, "track": "premium"}, 403, "not_owned")
	e.ok("POST", "/v1/seasonpass/buy-premium", a.Token, nil, nil)
	e.ok("POST", "/v1/seasonpass/claim", a.Token, map[string]any{"tier": 1, "track": "premium"}, &p)

	// --- abort path (two fresh players with equal ratings) ---
	var c, d authRes
	e.ok("POST", "/v1/auth/guest", "", map[string]string{"deviceId": "device-charlie-03"}, &c)
	e.ok("POST", "/v1/auth/guest", "", map[string]string{"deviceId": "device-delta-0004"}, &d)
	e.ok("POST", "/v1/matchmaking/tickets", c.Token, map[string]string{"queue": "ranked_1v1"}, nil)
	e.ok("POST", "/v1/matchmaking/tickets", d.Token, map[string]string{"queue": "ranked_1v1"}, nil)
	hb := map[string]any{"serverId": "eu-abort", "address": "203.0.113.9", "port": 7777, "region": "eu", "state": "idle"}
	e.server("/v1/servers/heartbeat", hb, nil)
	e.clock.Add(time.Second)
	e.mm.Tick(e.clock.Now())
	var tc ticketRes
	e.ok("GET", "/v1/matchmaking/current", c.Token, nil, &tc)
	if tc.Ticket.Match == nil {
		t.Fatalf("abort pair not matched: %+v", tc.Ticket)
	}
	abort := e.server("/v1/servers/match-abort", map[string]string{"matchId": tc.Ticket.Match.MatchID, "reason": "crash",
		"serverId": "eu-abort"}, nil)
	if abort.status != 200 {
		t.Fatalf("abort %d %s", abort.status, abort.body)
	}
	var td ticketRes
	e.ok("GET", "/v1/matchmaking/current", d.Token, nil, &td)
	if td.Ticket.Status != "cancelled" || td.Ticket.Reason != "server_aborted" || td.Ticket.Match != nil {
		t.Fatalf("after abort %+v", td.Ticket)
	}
	// Players re-queue after an abort; a searching ticket can be cancelled
	// by its owner only.
	var cancel ticketRes
	e.ok("POST", "/v1/matchmaking/tickets", c.Token, map[string]string{"queue": "ranked_1v1"}, &cancel)
	e.fail("DELETE", "/v1/matchmaking/tickets/"+cancel.Ticket.TicketID, d.Token, nil, 404, "not_found")
	e.ok("DELETE", "/v1/matchmaking/tickets/"+cancel.Ticket.TicketID, c.Token, nil, &cancel)
	if cancel.Ticket.Status != "cancelled" || cancel.Ticket.Reason != "cancelled" {
		t.Fatalf("cancel %+v", cancel.Ticket)
	}
	// The aborted match never produced a result.
	if r := e.server("/v1/servers/match-result", map[string]any{"matchId": "m_unknown"}, map[string]string{"X-Server-Id": "eu-abort"}); r.status != 404 {
		t.Fatalf("result for unknown match %d", r.status)
	}
}

func contains(list []int, n int) bool {
	for _, x := range list {
		if x == n {
			return true
		}
	}
	return false
}

func TestErrorsAndRouting(t *testing.T) {
	e := newTestEnv(t, envOptions{})
	e.fail("GET", "/v1/nope", "", nil, 404, "not_found")
	e.fail("GET", "/v1/auth/guest", "", nil, 404, "not_found") // wrong method
	e.fail("POST", "/v1/auth/guest", "", "{not json", 400, "bad_request")
	e.fail("POST", "/v1/auth/guest", "", "", 400, "bad_request")
	e.fail("POST", "/v1/auth/guest", "", `{"deviceId":"device-1234"} {"x":1}`, 400, "bad_request")
	e.fail("POST", "/v1/auth/guest", "", `{"deviceId":"`+strings.Repeat("x", 70000)+`"}`, 400, "bad_request")
	// Unknown request fields are ignored (forward compatibility).
	e.ok("POST", "/v1/auth/guest", "", map[string]any{"deviceId": "device-1234", "platform": "steam"}, nil)
	e.fail("POST", "/v1/servers/heartbeat", "", map[string]any{"serverId": "s1"}, 401, "unauthorized")
	if r := e.server("/v1/servers/heartbeat", map[string]any{"serverId": "s1"}, nil); r.status != 400 || r.errCode(t) != "bad_request" {
		t.Fatalf("invalid heartbeat %d %s", r.status, r.body)
	}
	if r := e.server("/v1/servers/match-start", map[string]any{"matchId": "m_x", "serverId": "a"},
		map[string]string{"X-Server-Id": "b"}); r.status != 400 {
		t.Fatalf("disagreeing server ids %d", r.status)
	}
	r := e.do("GET", "/healthz", "", nil, nil)
	if r.header.Get("X-Request-Id") == "" || r.header.Get("Access-Control-Allow-Origin") != "" {
		t.Fatalf("headers %v", r.header)
	}
}

func TestRateLimits(t *testing.T) {
	clock := &fakeClock{t: time.Unix(0, 0)}
	e := newTestEnv(t, envOptions{
		authLimit:   httpapi.NewLimiter(5, 10, clock.Now),
		playerLimit: httpapi.NewLimiter(20, 40, clock.Now),
	})
	statuses := map[int]int{}
	var token string
	for i := 0; i < 11; i++ {
		r := e.do("POST", "/v1/auth/guest", "", map[string]string{"deviceId": "device-rate-1"}, nil)
		statuses[r.status]++
		if r.status == 200 && token == "" {
			var ar authRes
			r.decode(t, &ar)
			token = ar.Token
		}
		if r.status == 429 && (r.errCode(t) != "rate_limited" || r.header.Get("Retry-After") == "") {
			t.Fatalf("429 body %s", r.body)
		}
	}
	if statuses[200] != 10 || statuses[429] != 1 {
		t.Fatalf("auth limiter: %v", statuses)
	}
	clock.Add(time.Second) // +5 tokens
	for i := 0; i < 5; i++ {
		if r := e.do("POST", "/v1/auth/guest", "", map[string]string{"deviceId": "device-rate-1"}, nil); r.status != 200 {
			t.Fatalf("after refill %d", r.status)
		}
	}
	statuses = map[int]int{}
	for i := 0; i < 41; i++ {
		statuses[e.do("GET", "/v1/profile", token, nil, nil).status]++
	}
	if statuses[200] != 40 || statuses[429] != 1 {
		t.Fatalf("player limiter: %v", statuses)
	}
}

func TestDevCORS(t *testing.T) {
	e := newTestEnv(t, envOptions{dev: true})
	req, _ := http.NewRequest(http.MethodOptions, e.srv.URL+"/v1/profile", nil)
	res, err := e.srv.Client().Do(req)
	if err != nil {
		t.Fatal(err)
	}
	res.Body.Close()
	if res.StatusCode != http.StatusNoContent || res.Header.Get("Access-Control-Allow-Origin") != "*" ||
		!strings.Contains(res.Header.Get("Access-Control-Allow-Headers"), "Idempotency-Key") {
		t.Fatalf("preflight %d %v", res.StatusCode, res.Header)
	}
}

func TestConcurrentPurchasesOverHTTP(t *testing.T) {
	runConcurrentPurchases(t, newTestEnv(t, envOptions{}))
}

func TestConcurrentPurchasesOverHTTPPostgres(t *testing.T) {
	runConcurrentPurchases(t, newTestEnv(t, envOptions{store: storagetest.Postgres(t)}))
}

func runConcurrentPurchases(t *testing.T, e *testEnv) {
	var a authRes
	e.ok("POST", "/v1/auth/guest", "", map[string]string{"deviceId": "device-concurrent"}, &a)
	// The same cosmetic bought by 10 concurrent requests is charged once.
	var wg sync.WaitGroup
	codes := make([]int, 10)
	for i := range codes {
		wg.Add(1)
		go func(i int) {
			defer wg.Done()
			req, _ := http.NewRequest("POST", e.srv.URL+"/v1/garage/buy-cosmetic", strings.NewReader(`{"itemId":"paint_rosso"}`))
			req.Header.Set("Authorization", "Bearer "+a.Token)
			res, err := e.srv.Client().Do(req)
			if err != nil {
				t.Error(err)
				return
			}
			res.Body.Close()
			codes[i] = res.StatusCode
		}(i)
	}
	wg.Wait()
	ok := 0
	for _, c := range codes {
		if c == 200 {
			ok++
		}
	}
	var p profileRes
	e.ok("GET", "/v1/profile", a.Token, nil, &p)
	if ok != 1 || p.Profile.Wallet.Credits != 23000 {
		t.Fatalf("ok=%d credits=%d codes=%v", ok, p.Profile.Wallet.Credits, codes)
	}
}
