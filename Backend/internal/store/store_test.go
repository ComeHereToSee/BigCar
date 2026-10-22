package store

import (
	"context"
	"errors"
	"io"
	"log/slog"
	"net/http"
	"reflect"
	"sync"
	"testing"
	"time"

	"github.com/comeheretosee/bigcar/backend/internal/apperr"
	"github.com/comeheretosee/bigcar/backend/internal/data"
	"github.com/comeheretosee/bigcar/backend/internal/data/datatest"
	"github.com/comeheretosee/bigcar/backend/internal/payments"
	"github.com/comeheretosee/bigcar/backend/internal/profile"
	"github.com/comeheretosee/bigcar/backend/internal/storage"
)

// TestPCG32ReferenceVector checks the generator against the output of the
// reference pcg32-demo (seed 42, stream 54).
func TestPCG32ReferenceVector(t *testing.T) {
	r := newPCG32(42, 54)
	want := []uint32{0xa15c02b7, 0x7b47f409, 0xba1d3330, 0x83d2f293, 0xbfa4784b, 0xcbed606e}
	for i, w := range want {
		if got := r.next(); got != w {
			t.Fatalf("output %d = %#x, want %#x", i, got, w)
		}
	}
}

func TestPCG32Bounded(t *testing.T) {
	r := newPCG32(7, DailyDealStream)
	counts := make([]int, 6)
	for i := 0; i < 60000; i++ {
		v := r.bounded(6)
		if v >= 6 {
			t.Fatalf("out of range %d", v)
		}
		counts[v]++
	}
	for i, c := range counts {
		if c < 9000 || c > 11000 {
			t.Errorf("bucket %d count %d looks biased", i, c)
		}
	}
}

func TestDailyDealsDeterministic(t *testing.T) {
	gd := datatest.Load(t)
	const day = 20748
	a, b := DailyDeals(gd, day), DailyDeals(gd, day)
	if !reflect.DeepEqual(a, b) {
		t.Fatal("same day must give the same deals")
	}
	if len(a) != gd.Store.Daily.Slots {
		t.Fatalf("got %d deals", len(a))
	}
	seen := map[string]bool{}
	for i, d := range a {
		if d.Slot != i {
			t.Errorf("slot %d", d.Slot)
		}
		if seen[d.Item.String()] {
			t.Errorf("duplicate item %s", d.Item)
		}
		seen[d.Item.String()] = true
		base, _ := gd.ItemBasePrice(d.Item)
		if d.BasePrice != base || d.Price.Currency != data.Credits ||
			d.Price.Amount != data.RoundTo100(float64(base.Amount)*(1-d.Discount)) {
			t.Errorf("deal %+v has wrong price", d)
		}
		found := false
		for _, disc := range gd.Store.Daily.Discounts {
			found = found || disc == d.Discount
		}
		if !found {
			t.Errorf("discount %v not configured", d.Discount)
		}
	}
	differs := false
	for d := int64(day + 1); d < day+10; d++ {
		differs = differs || !reflect.DeepEqual(a, DailyDeals(gd, d))
	}
	if !differs {
		t.Fatal("deals never change between days")
	}
}

// TestDailyDealsGolden pins the exact algorithm output so that accidental
// changes (which would desync clients that reproduce the deals) are caught.
func TestDailyDealsGolden(t *testing.T) {
	gd := datatest.Load(t)
	got := DailyDeals(gd, 20748)
	var ids []string
	var discounts []float64
	for _, d := range got {
		ids = append(ids, d.Item.ID)
		discounts = append(discounts, d.Discount)
	}
	// Cross-checked with an independent Python implementation of the
	// documented algorithm.
	wantIDs := []string{"paint_midnight", "roof_cage", "hoshino_supra", "wheels_mesh"}
	wantDisc := []float64{0.2, 0.2, 0.1, 0.2}
	if !reflect.DeepEqual(ids, wantIDs) || !reflect.DeepEqual(discounts, wantDisc) {
		t.Fatalf("day 20748 deals = %v %v, want %v %v", ids, discounts, wantIDs, wantDisc)
	}
}

type fakeClock struct {
	mu sync.Mutex
	t  time.Time
}

func (c *fakeClock) Now() time.Time { c.mu.Lock(); defer c.mu.Unlock(); return c.t }

func newTestService(t *testing.T, providers ...payments.Provider) (*Service, storage.Store, *fakeClock) {
	gd := datatest.Load(t)
	st := storage.NewMemory()
	clock := &fakeClock{t: time.Date(2026, 11, 15, 12, 0, 0, 0, time.UTC)}
	if len(providers) == 0 {
		providers = []payments.Provider{&payments.Sandbox{WebhookSecret: []byte("whsec")}}
	}
	quiet := slog.New(slog.NewTextHandler(io.Discard, nil))
	return NewService(gd, st, payments.NewRegistry(providers...), clock.Now, quiet), st, clock
}

func newPlayer(t *testing.T, st storage.Store, s *Service, id string, setup func(p *profile.Profile)) {
	t.Helper()
	p := profile.New(id, "P"+id, s.gd)
	if setup != nil {
		setup(p)
	}
	err := st.InTx(context.Background(), func(tx storage.Tx) error {
		if err := tx.CreateAccount(context.Background(), storage.Account{PlayerID: id, DeviceID: "dev" + id}); err != nil {
			return err
		}
		return tx.PutProfile(context.Background(), p)
	})
	if err != nil {
		t.Fatal(err)
	}
}

func intp(n int) *int { return &n }

func TestPurchaseOffers(t *testing.T) {
	s, _, _ := newTestService(t)
	gd := s.gd
	tests := []struct {
		name    string
		setup   func(p *profile.Profile)
		req     PurchaseRequest
		code    string
		granted []data.Item
		check   func(t *testing.T, p *profile.Profile)
	}{
		{"gold offer", func(p *profile.Profile) { p.Wallet.Gold = 300 }, PurchaseRequest{OfferID: "offer_starter_pack"}, "",
			[]data.Item{{Type: "equipment", ID: "front_wedge_steel"}, {Type: "paint", ID: "paint_rosso"},
				{Type: "currency", ID: "credits", Amount: 10000}},
			func(t *testing.T, p *profile.Profile) {
				if p.Wallet.Gold != 0 || p.Wallet.Credits != 35000 || p.Purchases["offer_starter_pack"] != 1 {
					t.Errorf("wallet %+v purchases %v", p.Wallet, p.Purchases)
				}
			}},
		{"insufficient gold", func(p *profile.Profile) { p.Wallet.Gold = 299 }, PurchaseRequest{OfferID: "offer_starter_pack"},
			apperr.InsufficientFunds, nil, nil},
		{"limit reached", func(p *profile.Profile) { p.Wallet.Gold = 1000; p.Purchases["offer_starter_pack"] = 1 },
			PurchaseRequest{OfferID: "offer_starter_pack"}, apperr.LimitReached, nil, nil},
		{"partially owned converts duplicates", func(p *profile.Profile) {
			p.Wallet.Gold = 300
			p.Inventory.Cosmetics = append(p.Inventory.Cosmetics, "paint_rosso")
		}, PurchaseRequest{OfferID: "offer_starter_pack"}, "",
			[]data.Item{{Type: "equipment", ID: "front_wedge_steel"}, {Type: "currency", ID: "credits", Amount: 1000},
				{Type: "currency", ID: "credits", Amount: 10000}}, nil},
		{"all owned", func(p *profile.Profile) {
			p.Wallet.Gold = 5000
			profile.Grant(p, gd, data.Item{Type: "vehicle", ID: "fulmine_f8r"})
		}, PurchaseRequest{OfferID: "offer_fulmine_f8r"}, apperr.AlreadyOwned, nil, nil},
		{"unlimited credits offer twice", func(p *profile.Profile) { p.Wallet.Gold = 200; p.Purchases["offer_credits_50k"] = 7 },
			PurchaseRequest{OfferID: "offer_credits_50k"}, "", []data.Item{{Type: "currency", ID: "credits", Amount: 50000}}, nil},
		{"credits priced offer", nil, PurchaseRequest{OfferID: "offer_paint_midnight"}, "",
			[]data.Item{{Type: "paint", ID: "paint_midnight"}}, func(t *testing.T, p *profile.Profile) {
				if p.Wallet.Credits != 20500 {
					t.Errorf("credits %d", p.Wallet.Credits)
				}
			}},
		{"expired offer", nil, PurchaseRequest{OfferID: "offer_expired"}, apperr.NotFound, nil, nil},
		{"future offer", func(p *profile.Profile) { p.Wallet.Gold = 1000 }, PurchaseRequest{OfferID: "offer_halloween"},
			"", []data.Item{{Type: "banner", ID: "banner_flames"}}, nil}, // 2026-11-15 is after the window → see below
		{"unknown offer", nil, PurchaseRequest{OfferID: "nope"}, apperr.NotFound, nil, nil},
		{"both fields", nil, PurchaseRequest{OfferID: "offer_credits_50k", DailySlot: intp(0)}, apperr.BadRequest, nil, nil},
		{"no fields", nil, PurchaseRequest{}, apperr.BadRequest, nil, nil},
	}
	// offer_halloween ends 2026-11-02, so at the test clock it is unavailable.
	tests[8].code, tests[8].granted = apperr.NotFound, nil

	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			p := profile.New("p_1", "x", gd)
			if tc.setup != nil {
				tc.setup(p)
			}
			before := p.Clone()
			granted, err := s.Purchase(p, tc.req)
			if apperr.CodeOf(err) != tc.code {
				t.Fatalf("code %q (%v), want %q", apperr.CodeOf(err), err, tc.code)
			}
			if err != nil {
				if !reflect.DeepEqual(p, before) {
					t.Fatal("profile modified by failed purchase")
				}
				return
			}
			if !reflect.DeepEqual(granted, tc.granted) {
				t.Fatalf("granted %v, want %v", granted, tc.granted)
			}
			if tc.check != nil {
				tc.check(t, p)
			}
		})
	}
}

func TestPurchaseDaily(t *testing.T) {
	s, _, clock := newTestService(t)
	gd := s.gd
	deals := DailyDeals(gd, profile.DayNumber(clock.Now()))
	p := profile.New("p_1", "x", gd)
	p.Wallet.Credits = 1_000_000
	p.Level = 50
	for _, d := range deals {
		granted, err := s.Purchase(p, PurchaseRequest{DailySlot: intp(d.Slot)})
		if err != nil {
			t.Fatalf("slot %d: %v", d.Slot, err)
		}
		if len(granted) != 1 || granted[0].ID != d.Item.ID {
			t.Fatalf("granted %v", granted)
		}
	}
	spent := int64(0)
	for _, d := range deals {
		spent += d.Price.Amount
	}
	if p.Wallet.Credits != 1_000_000-spent {
		t.Fatalf("credits %d, spent %d", p.Wallet.Credits, spent)
	}
	if _, err := s.Purchase(p, PurchaseRequest{DailySlot: intp(0)}); apperr.CodeOf(err) != apperr.LimitReached {
		t.Fatalf("second purchase: %v", err)
	}
	if _, err := s.Purchase(p, PurchaseRequest{DailySlot: intp(len(deals))}); apperr.CodeOf(err) != apperr.NotFound {
		t.Fatalf("bad slot: %v", err)
	}
	// A fresh player who already owns slot 0's item gets already_owned.
	q := profile.New("p_2", "y", gd)
	q.Wallet.Credits = 1_000_000
	profile.Grant(q, gd, deals[0].Item)
	if _, err := s.Purchase(q, PurchaseRequest{DailySlot: intp(0)}); apperr.CodeOf(err) != apperr.AlreadyOwned {
		t.Fatalf("owned: %v", err)
	}
	q.Wallet.Credits = 0
	if _, err := s.Purchase(q, PurchaseRequest{DailySlot: intp(1)}); apperr.CodeOf(err) != apperr.InsufficientFunds {
		t.Fatalf("poor: %v", err)
	}

	view := s.View(p)
	if view.Daily.Day != 20772 || !view.Daily.ExpiresAt.Equal(time.Date(2026, 11, 16, 0, 0, 0, 0, time.UTC)) {
		t.Fatalf("daily header %d %v", view.Daily.Day, view.Daily.ExpiresAt)
	}
	for _, d := range view.Daily.Deals {
		if !d.Purchased {
			t.Errorf("slot %d should be marked purchased", d.Slot)
		}
	}
}

func TestStoreView(t *testing.T) {
	s, _, _ := newTestService(t)
	p := profile.New("p_1", "x", s.gd)
	p.Purchases["offer_starter_pack"] = 1
	p.SeasonPass.XP = 3500
	v := s.View(p)
	var ids []string
	for _, o := range v.Offers {
		ids = append(ids, o.ID)
		if o.ID == "offer_starter_pack" && (o.Purchased != 1 || o.Tag == nil || *o.Tag != "store.tag.best_value") {
			t.Errorf("starter pack view %+v", o)
		}
		if o.ID == "offer_credits_50k" && o.Tag != nil {
			t.Errorf("null tag expected")
		}
	}
	want := []string{"offer_starter_pack", "offer_fulmine_f8r", "offer_credits_50k", "offer_paint_midnight"}
	if !reflect.DeepEqual(ids, want) {
		t.Fatalf("active offers %v", ids)
	}
	if v.SeasonPass.PlayerTier != 3 || v.SeasonPass.PlayerXP != 3500 || len(v.SeasonPass.Tiers) != 50 {
		t.Fatalf("season pass view %+v", v.SeasonPass)
	}
	if v.SeasonPass.Tiers[1].Premium == nil || v.SeasonPass.Tiers[1].Free == nil {
		t.Fatal("tier lists must not be null")
	}
	if len(v.GoldPacks) != 3 {
		t.Fatalf("gold packs %v", v.GoldPacks)
	}
}

func TestSeasonPass(t *testing.T) {
	s, _, clock := newTestService(t)
	gd := s.gd
	p := profile.New("p_1", "x", gd)
	if err := s.BuyPremium(p); apperr.CodeOf(err) != apperr.InsufficientFunds {
		t.Fatalf("poor premium: %v", err)
	}
	if _, err := s.Claim(p, ClaimRequest{Tier: 1, Track: "free"}); apperr.CodeOf(err) != apperr.LevelLocked {
		t.Fatalf("tier 1 at 0 xp: %v", err)
	}
	p.SeasonPass.XP = 2000 // tiers 1-2 reached
	tests := []struct {
		name    string
		req     ClaimRequest
		code    string
		granted []data.Item
	}{
		{"free tier 1", ClaimRequest{1, "free"}, "", []data.Item{{Type: "currency", ID: "credits", Amount: 1100}}},
		{"free tier 1 again", ClaimRequest{1, "free"}, apperr.Conflict, nil},
		{"free tier 2 paint", ClaimRequest{2, "free"}, "", []data.Item{{Type: "paint", ID: "paint_rosso"}}},
		{"tier 3 locked", ClaimRequest{3, "free"}, apperr.LevelLocked, nil},
		{"premium without pass", ClaimRequest{1, "premium"}, apperr.NotOwned, nil},
		{"bad track", ClaimRequest{1, "gold"}, apperr.BadRequest, nil},
		{"tier 0", ClaimRequest{0, "free"}, apperr.BadRequest, nil},
		{"tier 51", ClaimRequest{51, "free"}, apperr.BadRequest, nil},
	}
	for _, tc := range tests {
		granted, err := s.Claim(p, tc.req)
		if apperr.CodeOf(err) != tc.code || (err == nil && !reflect.DeepEqual(granted, tc.granted)) {
			t.Errorf("%s: %v %v", tc.name, granted, err)
		}
	}
	p.Wallet.Gold = 1000
	if err := s.BuyPremium(p); err != nil || !p.SeasonPass.Premium || p.Wallet.Gold != 50 {
		t.Fatalf("buy premium: %v %+v", err, p.Wallet)
	}
	if err := s.BuyPremium(p); apperr.CodeOf(err) != apperr.AlreadyOwned {
		t.Fatalf("second premium: %v", err)
	}
	if g, err := s.Claim(p, ClaimRequest{2, "premium"}); err != nil || len(g) != 1 || g[0].Amount != 20 {
		t.Fatalf("premium claim %v %v", g, err)
	}
	if !reflect.DeepEqual(p.SeasonPass.ClaimedFree, []int{1, 2}) || !reflect.DeepEqual(p.SeasonPass.ClaimedPremium, []int{2}) {
		t.Fatalf("claimed %+v", p.SeasonPass)
	}
	clock.t = gd.Store.SeasonPass.End
	q := profile.New("p_2", "y", gd)
	q.Wallet.Gold = 5000
	if err := s.BuyPremium(q); apperr.CodeOf(err) != apperr.Forbidden {
		t.Fatalf("premium after season end: %v", err)
	}
}

func TestSandboxCheckoutIdempotent(t *testing.T) {
	s, st, _ := newTestService(t)
	ctx := context.Background()
	newPlayer(t, st, s, "p_1", nil)
	r1, err := s.Checkout(ctx, "p_1", CheckoutRequest{PackID: "gold_m", Provider: "sandbox"}, "key-1")
	if err != nil || r1.Status != "completed" || r1.Profile == nil || r1.Profile.Wallet.Gold != 550 {
		t.Fatalf("checkout %+v %v", r1, err)
	}
	r2, err := s.Checkout(ctx, "p_1", CheckoutRequest{PackID: "gold_m", Provider: "sandbox"}, "key-1")
	if err != nil || r2.OrderID != r1.OrderID || r2.Profile.Wallet.Gold != 550 {
		t.Fatalf("retry %+v %v", r2, err)
	}
	if _, err := s.Checkout(ctx, "p_1", CheckoutRequest{PackID: "gold_s", Provider: "sandbox"}, "key-1"); apperr.CodeOf(err) != apperr.Conflict {
		t.Fatalf("reused key: %v", err)
	}
	r3, err := s.Checkout(ctx, "p_1", CheckoutRequest{PackID: "gold_s", Provider: "sandbox"}, "")
	if err != nil || r3.OrderID == r1.OrderID || r3.Profile.Wallet.Gold != 650 {
		t.Fatalf("second order %+v %v", r3, err)
	}
	for _, tc := range []struct {
		req  CheckoutRequest
		code string
	}{
		{CheckoutRequest{PackID: "gold_xxl", Provider: "sandbox"}, apperr.NotFound},
		{CheckoutRequest{PackID: "gold_s", Provider: "steam"}, apperr.BadRequest},
	} {
		if _, err := s.Checkout(ctx, "p_1", tc.req, ""); apperr.CodeOf(err) != tc.code {
			t.Errorf("%+v: %v", tc.req, err)
		}
	}
}

// pendingProvider simulates a real provider that completes via webhook.
type pendingProvider struct {
	payments.Sandbox
	fail bool
}

func (p *pendingProvider) Name() string { return "testpay" }
func (p *pendingProvider) Checkout(ctx context.Context, req payments.CheckoutRequest) (payments.CheckoutResult, error) {
	if p.fail {
		return payments.CheckoutResult{}, errors.New("provider down")
	}
	return payments.CheckoutResult{Status: payments.StatusPending, RedirectURL: "https://pay.example/" + req.OrderID, ExternalID: "ext-" + req.OrderID}, nil
}

func TestWebhookFlow(t *testing.T) {
	prov := &pendingProvider{Sandbox: payments.Sandbox{WebhookSecret: []byte("whsec")}}
	s, st, _ := newTestService(t, prov)
	ctx := context.Background()
	newPlayer(t, st, s, "p_1", nil)
	res, err := s.Checkout(ctx, "p_1", CheckoutRequest{PackID: "gold_l", Provider: "testpay"}, "")
	if err != nil || res.Status != "pending" || res.Profile != nil || res.RedirectURL != "https://pay.example/"+res.OrderID {
		t.Fatalf("checkout %+v %v", res, err)
	}
	gold := func() int64 {
		p, _ := storage.ReadProfile(ctx, st, "p_1")
		return p.Wallet.Gold
	}
	if gold() != 0 {
		t.Fatal("gold credited before payment")
	}
	send := func(body string, secret string) error {
		h := http.Header{}
		h.Set(payments.SignatureHeader, payments.Sign([]byte(secret), []byte(body)))
		return s.HandleWebhook(ctx, "testpay", h, []byte(body))
	}
	body := `{"orderId":"` + res.OrderID + `","status":"completed","externalId":"tx-9"}`
	if err := send(body, "wrong"); apperr.CodeOf(err) != apperr.Unauthorized {
		t.Fatalf("bad signature: %v", err)
	}
	if err := send(body, "whsec"); err != nil {
		t.Fatal(err)
	}
	if err := send(body, "whsec"); err != nil { // provider retry
		t.Fatal(err)
	}
	if gold() != 1200 {
		t.Fatalf("gold = %d, want 1200 exactly once", gold())
	}
	if err := send(`{"orderId":"o_missing","status":"completed"}`, "whsec"); apperr.CodeOf(err) != apperr.NotFound {
		t.Fatalf("unknown order: %v", err)
	}
	if err := s.HandleWebhook(ctx, "nope", http.Header{}, []byte(body)); apperr.CodeOf(err) != apperr.NotFound {
		t.Fatalf("unknown provider: %v", err)
	}
	// A failed payment never credits.
	res2, _ := s.Checkout(ctx, "p_1", CheckoutRequest{PackID: "gold_s", Provider: "testpay"}, "")
	if err := send(`{"orderId":"`+res2.OrderID+`","status":"failed"}`, "whsec"); err != nil {
		t.Fatal(err)
	}
	if gold() != 1200 {
		t.Fatalf("failed payment credited: %d", gold())
	}
	prov.fail = true
	if _, err := s.Checkout(ctx, "p_1", CheckoutRequest{PackID: "gold_s", Provider: "testpay"}, ""); apperr.CodeOf(err) != apperr.Internal {
		t.Fatalf("provider failure: %v", err)
	}
}
