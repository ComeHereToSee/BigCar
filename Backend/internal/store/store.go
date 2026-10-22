// Package store implements the in-game store: offers, daily deals, real-money
// gold packs (through payments providers) and the season pass.
package store

import (
	"context"
	"crypto/rand"
	"crypto/sha256"
	"encoding/hex"
	"errors"
	"log/slog"
	"net/http"
	"sort"
	"time"

	"github.com/comeheretosee/bigcar/backend/internal/apperr"
	"github.com/comeheretosee/bigcar/backend/internal/data"
	"github.com/comeheretosee/bigcar/backend/internal/payments"
	"github.com/comeheretosee/bigcar/backend/internal/profile"
	"github.com/comeheretosee/bigcar/backend/internal/storage"
)

// Service implements the store endpoints.
type Service struct {
	gd        *data.GameData
	st        storage.Store
	providers *payments.Registry
	now       func() time.Time
	log       *slog.Logger
}

// NewService creates the store service.
func NewService(gd *data.GameData, st storage.Store, providers *payments.Registry, now func() time.Time, log *slog.Logger) *Service {
	if now == nil {
		now = time.Now
	}
	if log == nil {
		log = slog.Default()
	}
	return &Service{gd: gd, st: st, providers: providers, now: now, log: log}
}

// Deal is one daily deal.
type Deal struct {
	Slot      int        `json:"slot"`
	Item      data.Item  `json:"item"`
	BasePrice data.Price `json:"basePrice"`
	Price     data.Price `json:"price"`
	Discount  float64    `json:"discount"`
}

// DailyDeals returns the deals of a UTC day. Algorithm (deterministic):
//
//	rng := PCG32(initstate = day, initseq = 54)
//	idx := [0, 1, ..., len(pool)-1]
//	for i in 0 ..< min(slots, len(pool)):
//	    j := i + rng.bounded(len(pool) - i)        // partial Fisher–Yates
//	    swap(idx[i], idx[j])
//	    discount := discounts[rng.bounded(len(discounts))]
//	    deal i = pool[idx[i]] at roundTo100(basePrice.credits * (1 - discount))
func DailyDeals(gd *data.GameData, day int64) []Deal {
	cfg := gd.Store.Daily
	n := len(cfg.Pool)
	slots := cfg.Slots
	if slots > n {
		slots = n
	}
	if slots <= 0 || len(cfg.Discounts) == 0 {
		return []Deal{}
	}
	rng := newPCG32(uint64(day), DailyDealStream)
	idx := make([]int, n)
	for i := range idx {
		idx[i] = i
	}
	deals := make([]Deal, 0, slots)
	for i := 0; i < slots; i++ {
		j := i + int(rng.bounded(uint32(n-i)))
		idx[i], idx[j] = idx[j], idx[i]
		disc := cfg.Discounts[rng.bounded(uint32(len(cfg.Discounts)))]
		item := cfg.Pool[idx[i]]
		base, _ := gd.ItemBasePrice(item)
		deals = append(deals, Deal{
			Slot: i, Item: data.Item{Type: item.Type, ID: item.ID}, BasePrice: base, Discount: disc,
			Price: data.CreditsPrice(data.RoundTo100(float64(base.Amount) * (1 - disc))),
		})
	}
	return deals
}

// OfferView is an offer as shown to a player.
type OfferView struct {
	ID        string      `json:"id"`
	Section   string      `json:"section"`
	Featured  bool        `json:"featured"`
	Items     []data.Item `json:"items"`
	Price     data.Price  `json:"price"`
	Limit     int         `json:"limit"`
	Purchased int         `json:"purchased"`
	Tag       *string     `json:"tag"`
	End       *time.Time  `json:"end"`
}

// DealView is a daily deal as shown to a player.
type DealView struct {
	Deal
	Purchased bool `json:"purchased"`
}

// DailyView is the daily section of GET /v1/store.
type DailyView struct {
	Day       int64      `json:"day"`
	ExpiresAt time.Time  `json:"expiresAt"`
	Deals     []DealView `json:"deals"`
}

// SeasonPassView is the season pass section of GET /v1/store.
type SeasonPassView struct {
	ID             string            `json:"id"`
	Name           string            `json:"name"`
	Start          time.Time         `json:"start"`
	End            time.Time         `json:"end"`
	PremiumPrice   data.Price        `json:"premiumPrice"`
	XPPerTier      int64             `json:"xpPerTier"`
	Tiers          []data.SeasonTier `json:"tiers"`
	PlayerXP       int64             `json:"playerXp"`
	PlayerTier     int               `json:"playerTier"`
	Premium        bool              `json:"premium"`
	ClaimedFree    []int             `json:"claimedFree"`
	ClaimedPremium []int             `json:"claimedPremium"`
}

// View is the GET /v1/store response.
type View struct {
	ServerTime time.Time       `json:"serverTime"`
	Offers     []OfferView     `json:"offers"`
	Daily      DailyView       `json:"daily"`
	GoldPacks  []data.GoldPack `json:"goldPacks"`
	SeasonPass SeasonPassView  `json:"seasonPass"`
}

// PlayerTier is the number of season pass tiers unlocked: tier n requires
// n × xpPerTier season XP.
func PlayerTier(sp *data.SeasonPass, xp int64) int {
	if sp.XPPerTier <= 0 || xp <= 0 {
		return 0
	}
	t := xp / sp.XPPerTier
	if t > int64(len(sp.Tiers)) {
		t = int64(len(sp.Tiers))
	}
	return int(t)
}

func nonNil(items []data.Item) []data.Item {
	if items == nil {
		return []data.Item{}
	}
	return items
}

// View builds the store as seen by player p at the current time.
func (s *Service) View(p *profile.Profile) View {
	now := s.now().UTC()
	v := View{ServerTime: now.Truncate(time.Second), Offers: []OfferView{}, GoldPacks: s.gd.Store.GoldPacks}
	if v.GoldPacks == nil {
		v.GoldPacks = []data.GoldPack{}
	}
	for _, o := range s.gd.Store.Offers {
		if !o.ActiveAt(now) {
			continue
		}
		ov := OfferView{
			ID: o.ID, Section: o.Section, Featured: o.Featured, Items: nonNil(o.Items), Price: o.Price,
			Limit: o.Limit, Purchased: p.Purchases[o.ID], End: o.End,
		}
		if o.Tag != "" {
			tag := o.Tag
			ov.Tag = &tag
		}
		v.Offers = append(v.Offers, ov)
	}
	day := profile.DayNumber(now)
	v.Daily = DailyView{Day: day, ExpiresAt: time.Unix((day+1)*86400, 0).UTC(), Deals: []DealView{}}
	for _, d := range DailyDeals(s.gd, day) {
		v.Daily.Deals = append(v.Daily.Deals, DealView{Deal: d, Purchased: p.Purchases[profile.DailyPurchaseKey(day, d.Slot)] > 0})
	}
	sp := &s.gd.Store.SeasonPass
	tiers := make([]data.SeasonTier, len(sp.Tiers))
	for i, t := range sp.Tiers {
		tiers[i] = data.SeasonTier{Free: nonNil(t.Free), Premium: nonNil(t.Premium)}
	}
	v.SeasonPass = SeasonPassView{
		ID: sp.ID, Name: sp.Name, Start: sp.Start, End: sp.End, PremiumPrice: sp.PremiumPrice, XPPerTier: sp.XPPerTier,
		Tiers: tiers, PlayerXP: p.SeasonPass.XP, PlayerTier: PlayerTier(sp, p.SeasonPass.XP), Premium: p.SeasonPass.Premium,
		ClaimedFree: p.SeasonPass.ClaimedFree, ClaimedPremium: p.SeasonPass.ClaimedPremium,
	}
	return v
}

// PurchaseRequest is the body of POST /v1/store/purchase.
type PurchaseRequest struct {
	OfferID   string `json:"offerId"`
	DailySlot *int   `json:"dailySlot"`
}

// Purchase buys an offer or a daily deal for p (inside a profile
// transaction) and returns the granted items.
func (s *Service) Purchase(p *profile.Profile, req PurchaseRequest) ([]data.Item, error) {
	switch {
	case req.OfferID != "" && req.DailySlot == nil:
		return s.buyOffer(p, req.OfferID)
	case req.OfferID == "" && req.DailySlot != nil:
		return s.buyDaily(p, *req.DailySlot)
	default:
		return nil, apperr.New(apperr.BadRequest, "specify exactly one of offerId or dailySlot")
	}
}

func (s *Service) buyOffer(p *profile.Profile, id string) ([]data.Item, error) {
	o, ok := s.gd.Offer(id)
	if !ok || !o.ActiveAt(s.now()) {
		return nil, apperr.New(apperr.NotFound, "offer %q is not available", id)
	}
	if o.Limit > 0 && p.Purchases[o.ID] >= o.Limit {
		return nil, apperr.New(apperr.LimitReached, "offer %q can be bought %d time(s)", o.ID, o.Limit)
	}
	if profile.AllOwned(p, s.gd, o.Items) {
		return nil, apperr.New(apperr.AlreadyOwned, "you already own everything in this offer")
	}
	if err := p.Pay(o.Price); err != nil {
		return nil, err
	}
	p.Purchases[o.ID]++
	return profile.GrantAll(p, s.gd, o.Items), nil
}

func (s *Service) buyDaily(p *profile.Profile, slot int) ([]data.Item, error) {
	day := profile.DayNumber(s.now())
	deals := DailyDeals(s.gd, day)
	if slot < 0 || slot >= len(deals) {
		return nil, apperr.New(apperr.NotFound, "no daily deal in slot %d", slot)
	}
	d := deals[slot]
	key := profile.DailyPurchaseKey(day, slot)
	if p.Purchases[key] > 0 {
		return nil, apperr.New(apperr.LimitReached, "daily deal already bought today")
	}
	if p.Owns(s.gd, d.Item) {
		return nil, apperr.New(apperr.AlreadyOwned, "you already own %s", d.Item.ID)
	}
	if err := p.Pay(d.Price); err != nil {
		return nil, err
	}
	p.Purchases[key] = 1
	return profile.Grant(p, s.gd, d.Item), nil
}

// BuyPremium buys the premium season pass.
func (s *Service) BuyPremium(p *profile.Profile) error {
	sp := &s.gd.Store.SeasonPass
	if !s.now().Before(sp.End) {
		return apperr.New(apperr.Forbidden, "the season has ended")
	}
	if p.SeasonPass.Premium {
		return apperr.New(apperr.AlreadyOwned, "premium pass already owned")
	}
	if err := p.Pay(sp.PremiumPrice); err != nil {
		return err
	}
	p.SeasonPass.Premium = true
	return nil
}

// ClaimRequest is the body of POST /v1/seasonpass/claim.
type ClaimRequest struct {
	Tier  int    `json:"tier"`
	Track string `json:"track"`
}

// Claim grants the rewards of a reached season pass tier (1-based).
func (s *Service) Claim(p *profile.Profile, req ClaimRequest) ([]data.Item, error) {
	sp := &s.gd.Store.SeasonPass
	if req.Track != "free" && req.Track != "premium" {
		return nil, apperr.New(apperr.BadRequest, "track must be \"free\" or \"premium\"")
	}
	if req.Tier < 1 || req.Tier > len(sp.Tiers) {
		return nil, apperr.New(apperr.BadRequest, "tier must be 1-%d", len(sp.Tiers))
	}
	if req.Tier > PlayerTier(sp, p.SeasonPass.XP) {
		return nil, apperr.New(apperr.LevelLocked, "season pass tier %d not reached", req.Tier)
	}
	tier := sp.Tiers[req.Tier-1]
	claimed, items := &p.SeasonPass.ClaimedFree, tier.Free
	if req.Track == "premium" {
		if !p.SeasonPass.Premium {
			return nil, apperr.New(apperr.NotOwned, "premium pass not owned")
		}
		claimed, items = &p.SeasonPass.ClaimedPremium, tier.Premium
	}
	for _, c := range *claimed {
		if c == req.Tier {
			return nil, apperr.New(apperr.Conflict, "tier %d (%s) already claimed", req.Tier, req.Track)
		}
	}
	*claimed = append(*claimed, req.Tier)
	sort.Ints(*claimed)
	return profile.GrantAll(p, s.gd, items), nil
}

// CheckoutRequest is the body of POST /v1/store/gold/checkout.
type CheckoutRequest struct {
	PackID   string `json:"packId"`
	Provider string `json:"provider"`
}

// CheckoutResponse is the checkout response.
type CheckoutResponse struct {
	OrderID     string           `json:"orderId"`
	Status      string           `json:"status"`
	RedirectURL string           `json:"redirectUrl"`
	Profile     *profile.Profile `json:"profile,omitempty"`
}

// orderIDFor derives the order id. With an Idempotency-Key the id is a hash of
// (player, key), so a retried checkout finds its first order instead of
// creating a second one.
func orderIDFor(playerID, idemKey string) (string, error) {
	if idemKey != "" {
		sum := sha256.Sum256([]byte(playerID + "\x00" + idemKey))
		return "o_" + hex.EncodeToString(sum[:12]), nil
	}
	b := make([]byte, 12)
	if _, err := rand.Read(b); err != nil {
		return "", err
	}
	return "o_" + hex.EncodeToString(b), nil
}

// Checkout starts a real-money gold purchase. The sandbox provider completes
// synchronously and the gold is credited before returning; other providers
// return a pending order and a redirect URL, and credit happens on the
// webhook.
func (s *Service) Checkout(ctx context.Context, playerID string, req CheckoutRequest, idemKey string) (CheckoutResponse, error) {
	pack, ok := s.gd.GoldPack(req.PackID)
	if !ok {
		return CheckoutResponse{}, apperr.New(apperr.NotFound, "unknown gold pack %q", req.PackID)
	}
	prov, ok := s.providers.Get(req.Provider)
	if !ok {
		return CheckoutResponse{}, apperr.New(apperr.BadRequest, "payment provider %q is not available", req.Provider)
	}
	if len(idemKey) > storage.MaxIdempotencyKeyLen {
		return CheckoutResponse{}, apperr.New(apperr.BadRequest, "Idempotency-Key too long")
	}
	orderID, err := orderIDFor(playerID, idemKey)
	if err != nil {
		return CheckoutResponse{}, err
	}
	now := s.now().UTC()
	order := storage.Order{
		ID: orderID, PlayerID: playerID, Provider: prov.Name(), PackID: pack.ID, Gold: pack.Gold + pack.Bonus,
		Status: storage.OrderPending, CreatedAt: now, UpdatedAt: now,
	}
	var existing *storage.Order
	err = s.st.InTx(ctx, func(tx storage.Tx) error {
		existing = nil
		err := tx.CreateOrder(ctx, order)
		if errors.Is(err, storage.ErrConflict) {
			o, err := tx.GetOrder(ctx, orderID, false)
			if err != nil {
				return err
			}
			existing = &o
			return nil
		}
		return err
	})
	if err != nil {
		return CheckoutResponse{}, err
	}
	if existing != nil { // idempotent retry
		if existing.PlayerID != playerID || existing.PackID != pack.ID || existing.Provider != prov.Name() {
			return CheckoutResponse{}, apperr.New(apperr.Conflict, "Idempotency-Key was already used for a different request")
		}
		return s.orderResponse(ctx, *existing)
	}

	res, err := prov.Checkout(ctx, payments.CheckoutRequest{
		OrderID: orderID, PlayerID: playerID, PackID: pack.ID, Gold: order.Gold, Prices: pack.Prices,
	})
	if err != nil {
		s.log.Error("payment checkout failed", "provider", prov.Name(), "order", orderID, "err", err)
		_ = s.st.InTx(ctx, func(tx storage.Tx) error {
			o, err := tx.GetOrder(ctx, orderID, true)
			if err != nil || o.Status != storage.OrderPending {
				return err
			}
			o.Status, o.UpdatedAt = storage.OrderFailed, s.now().UTC()
			return tx.UpdateOrder(ctx, o)
		})
		return CheckoutResponse{}, apperr.New(apperr.Internal, "payment provider unavailable")
	}
	switch res.Status {
	case payments.StatusCompleted:
		var p *profile.Profile
		err = s.st.InTx(ctx, func(tx storage.Tx) error {
			var err error
			p, err = s.completeOrder(ctx, tx, orderID, res.ExternalID)
			return err
		})
		if err != nil {
			return CheckoutResponse{}, err
		}
		return CheckoutResponse{OrderID: orderID, Status: storage.OrderCompleted, Profile: p}, nil
	default:
		err = s.st.InTx(ctx, func(tx storage.Tx) error {
			o, err := tx.GetOrder(ctx, orderID, true)
			if err != nil {
				return err
			}
			o.ExternalID, o.RedirectURL, o.UpdatedAt = res.ExternalID, res.RedirectURL, s.now().UTC()
			return tx.UpdateOrder(ctx, o)
		})
		if err != nil {
			return CheckoutResponse{}, err
		}
		return CheckoutResponse{OrderID: orderID, Status: storage.OrderPending, RedirectURL: res.RedirectURL}, nil
	}
}

func (s *Service) orderResponse(ctx context.Context, o storage.Order) (CheckoutResponse, error) {
	resp := CheckoutResponse{OrderID: o.ID, Status: o.Status, RedirectURL: o.RedirectURL}
	if o.Status == storage.OrderCompleted {
		p, err := storage.ReadProfile(ctx, s.st, o.PlayerID)
		if err != nil {
			return CheckoutResponse{}, err
		}
		resp.Profile = p
	}
	return resp, nil
}

// completeOrder credits an order's gold exactly once. Lock order: order row,
// then profile row (MutateProfile only locks profiles, so no cycle exists).
func (s *Service) completeOrder(ctx context.Context, tx storage.Tx, orderID, externalID string) (*profile.Profile, error) {
	o, err := tx.GetOrder(ctx, orderID, true)
	if errors.Is(err, storage.ErrNotFound) {
		return nil, apperr.New(apperr.NotFound, "unknown order")
	}
	if err != nil {
		return nil, err
	}
	p, err := tx.GetProfile(ctx, o.PlayerID, true)
	if err != nil {
		return nil, err
	}
	if o.Status == storage.OrderCompleted {
		return p, nil // already credited (webhook retry or duplicate notification)
	}
	profile.Normalize(p, s.gd, s.now())
	p.Credit(data.Gold, o.Gold)
	if err := tx.PutProfile(ctx, p); err != nil {
		return nil, err
	}
	o.Status, o.UpdatedAt = storage.OrderCompleted, s.now().UTC()
	if externalID != "" {
		o.ExternalID = externalID
	}
	if err := tx.UpdateOrder(ctx, o); err != nil {
		return nil, err
	}
	s.log.Info("gold order completed", "order", o.ID, "player", o.PlayerID, "gold", o.Gold, "provider", o.Provider)
	return p, nil
}

// HandleWebhook verifies and applies a provider notification.
func (s *Service) HandleWebhook(ctx context.Context, providerName string, header http.Header, body []byte) error {
	prov, ok := s.providers.Get(providerName)
	if !ok {
		return apperr.New(apperr.NotFound, "unknown payment provider")
	}
	ev, err := prov.ParseWebhook(header, body)
	switch {
	case errors.Is(err, payments.ErrBadSignature):
		return apperr.New(apperr.Unauthorized, "invalid signature")
	case err != nil:
		return apperr.New(apperr.BadRequest, "invalid webhook payload")
	}
	return s.st.InTx(ctx, func(tx storage.Tx) error {
		o, err := tx.GetOrder(ctx, ev.OrderID, true)
		if errors.Is(err, storage.ErrNotFound) {
			return apperr.New(apperr.NotFound, "unknown order")
		}
		if err != nil {
			return err
		}
		if o.Provider != prov.Name() {
			return apperr.New(apperr.Forbidden, "order belongs to another provider")
		}
		switch ev.Status {
		case payments.StatusCompleted:
			_, err := s.completeOrder(ctx, tx, o.ID, ev.ExternalID)
			return err
		case payments.StatusFailed:
			if o.Status != storage.OrderPending {
				return nil
			}
			o.Status, o.UpdatedAt = storage.OrderFailed, s.now().UTC()
			return tx.UpdateOrder(ctx, o)
		}
		return nil
	})
}
