package ladder

import (
	"context"
	"fmt"
	"testing"

	"github.com/comeheretosee/bigcar/backend/internal/apperr"
	"github.com/comeheretosee/bigcar/backend/internal/data/datatest"
	"github.com/comeheretosee/bigcar/backend/internal/profile"
	"github.com/comeheretosee/bigcar/backend/internal/storage"
)

func TestLadder(t *testing.T) {
	gd := datatest.Load(t)
	st := storage.NewMemory()
	ctx := context.Background()
	// 102 players at 2300 (crash_king rating) with descending ratings, plus
	// one placement player.
	add := func(id string, r float64, placements int) {
		p := profile.New(id, "N"+id, gd)
		p.Ranked["ranked_3v3"] = profile.Ranked{Rating: r, RD: 60, Vol: 0.06, Wins: 7, Losses: 3, PlacementsLeft: placements}
		err := st.InTx(ctx, func(tx storage.Tx) error {
			if err := tx.CreateAccount(ctx, storage.Account{PlayerID: id, DeviceID: "d" + id}); err != nil {
				return err
			}
			return tx.PutProfile(ctx, p)
		})
		if err != nil {
			t.Fatal(err)
		}
	}
	for i := 0; i < 102; i++ {
		add(fmt.Sprintf("p_%03d", i), 2400-float64(i), 0)
	}
	add("p_new", 2500, 3)

	svc := NewService(st, gd, []string{"ranked_1v1", "ranked_3v3"})
	page, err := svc.Page(ctx, "ranked_3v3", 0, 50)
	if err != nil {
		t.Fatal(err)
	}
	if page.Total != 102 || len(page.Entries) != 50 || page.Season != "season_1" || page.Queue != "ranked_3v3" {
		t.Fatalf("page %d/%d %s", page.Total, len(page.Entries), page.Season)
	}
	first := page.Entries[0]
	if first.Rank != 1 || first.PlayerID != "p_000" || first.Tier != "crash_king" || first.Wins != 7 || first.DisplayName != "Np_000" {
		t.Fatalf("first %+v", first)
	}
	page, _ = svc.Page(ctx, "ranked_3v3", 99, 3)
	got := ""
	for _, e := range page.Entries {
		got += fmt.Sprintf("%d:%s:%s ", e.Rank, e.PlayerID, e.Tier)
	}
	// Rank 101 and 102 have crash_king ratings but are outside the top 100.
	if got != "100:p_099:crash_king 101:p_100:master 102:p_101:master " {
		t.Fatalf("top-100 rule: %s", got)
	}

	me, err := svc.Me(ctx, "p_100", "ranked_3v3")
	if err != nil || me.Entry == nil || me.Entry.Rank != 101 || me.Entry.Tier != "master" || me.PlacementsLeft != 0 {
		t.Fatalf("me %+v %v", me, err)
	}
	me, _ = svc.Me(ctx, "p_new", "ranked_3v3")
	if me.Entry != nil || me.PlacementsLeft != 3 {
		t.Fatalf("placement me %+v", me)
	}
	me, _ = svc.Me(ctx, "p_000", "ranked_1v1")
	if me.Entry != nil || me.PlacementsLeft != 5 {
		t.Fatalf("never played %+v", me)
	}

	for _, tc := range []struct {
		queue         string
		offset, limit int
		code          string
	}{
		{"casual_ffa", 0, 10, apperr.NotFound},
		{"ranked_3v3", -1, 10, apperr.BadRequest},
		{"ranked_3v3", 0, 0, apperr.BadRequest},
		{"ranked_3v3", 0, 101, apperr.BadRequest},
	} {
		if _, err := svc.Page(ctx, tc.queue, tc.offset, tc.limit); apperr.CodeOf(err) != tc.code {
			t.Errorf("%+v: %v", tc, err)
		}
	}
	if _, err := svc.Me(ctx, "p_000", "nope"); apperr.CodeOf(err) != apperr.NotFound {
		t.Errorf("me unknown queue: %v", err)
	}
	if s := svc.CurrentSeason(); s.ID != "season_1" || s.Name != "store.season_1" || !s.Start.Before(s.End) {
		t.Fatalf("season %+v", s)
	}
}
