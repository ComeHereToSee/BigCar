package party

import (
	"strings"
	"testing"

	"github.com/comeheretosee/bigcar/backend/internal/apperr"
)

func TestCodeFormat(t *testing.T) {
	m := NewManager(nil)
	for i := 0; i < 200; i++ {
		p, err := m.Create("p_"+strings.Repeat("x", i%5)+string(rune('a'+i%26))+string(rune('a'+i/26)), "n")
		if err != nil {
			t.Fatal(err)
		}
		if len(p.Code) != CodeLength {
			t.Fatalf("code %q", p.Code)
		}
		for _, c := range p.Code {
			if !strings.ContainsRune(CodeAlphabet, c) || strings.ContainsRune("0O1I", c) {
				t.Fatalf("code %q has invalid char %q", p.Code, c)
			}
		}
	}
}

func TestPartyLifecycle(t *testing.T) {
	m := NewManager(nil)
	a, err := m.Create("a", "Alice")
	if err != nil {
		t.Fatal(err)
	}
	if a.LeaderID != "a" || len(a.Members) != 1 || !a.Members[0].Ready {
		t.Fatalf("created %+v", a)
	}
	steps := []struct {
		name string
		do   func() (Party, error)
		code string
		ids  string
		lead string
	}{
		{"b joins (lower-case code)", func() (Party, error) { return m.Join("b", "Bob", strings.ToLower(a.Code)) }, "", "a,b", "a"},
		{"b joins again is a no-op", func() (Party, error) { return m.Join("b", "Bob", a.Code) }, "", "a,b", "a"},
		{"c joins", func() (Party, error) { return m.Join("c", "Cid", a.Code) }, "", "a,b,c", "a"},
		{"d cannot join a full party", func() (Party, error) { return m.Join("d", "Dee", a.Code) }, apperr.PartyFull, "", ""},
		{"unknown code", func() (Party, error) { return m.Join("d", "Dee", "ZZZZZZ") }, apperr.NotFound, "", ""},
		{"b cannot kick", func() (Party, error) { return m.Kick("b", "c") }, apperr.NotLeader, "", ""},
		{"leader cannot kick self", func() (Party, error) { return m.Kick("a", "a") }, apperr.BadRequest, "", ""},
		{"kick stranger", func() (Party, error) { return m.Kick("a", "zz") }, apperr.NotFound, "", ""},
		{"leader kicks c", func() (Party, error) { return m.Kick("a", "c") }, "", "a,b", "a"},
		{"b ready", func() (Party, error) { return m.SetReady("b", true) }, "", "a,b", "a"},
	}
	for _, s := range steps {
		p, err := s.do()
		if apperr.CodeOf(err) != s.code {
			t.Fatalf("%s: %v, want %q", s.name, err, s.code)
		}
		if err == nil && (strings.Join(p.MemberIDs(), ",") != s.ids || p.LeaderID != s.lead) {
			t.Fatalf("%s: members %v leader %s", s.name, p.MemberIDs(), p.LeaderID)
		}
	}
	if _, ok := m.Get("c"); ok {
		t.Fatal("kicked player still in party")
	}
	// Leader leaves: b is promoted (and counts as ready).
	m.Leave("a")
	p, ok := m.Get("b")
	if !ok || p.LeaderID != "b" || len(p.Members) != 1 || !p.Members[0].Ready {
		t.Fatalf("after leader left: %+v", p)
	}
	// Last member leaves: party and code are gone.
	m.Leave("b")
	if _, ok := m.Get("b"); ok {
		t.Fatal("empty party not deleted")
	}
	if _, err := m.Join("x", "X", p.Code); apperr.CodeOf(err) != apperr.NotFound {
		t.Fatalf("code of deleted party still works: %v", err)
	}
	m.Leave("nobody") // no-op
	if _, err := m.SetReady("nobody", true); apperr.CodeOf(err) != apperr.NotFound {
		t.Fatalf("ready outside party: %v", err)
	}
}

func TestCreateAndJoinLeaveCurrentParty(t *testing.T) {
	m := NewManager(nil)
	p1, _ := m.Create("a", "A")
	_, _ = m.Join("b", "B", p1.Code)
	p2, _ := m.Create("b", "B") // b leaves p1 and leads p2
	if got, _ := m.Get("a"); len(got.Members) != 1 {
		t.Fatalf("b still in p1: %+v", got)
	}
	_, _ = m.Join("a", "A", p2.Code) // a's solo party is deleted
	if _, err := m.Join("c", "C", p1.Code); apperr.CodeOf(err) != apperr.NotFound {
		t.Fatalf("p1 should be gone: %v", err)
	}
	got, _ := m.Get("a")
	if got.ID != p2.ID || got.LeaderID != "b" {
		t.Fatalf("a not in p2: %+v", got)
	}
}

func TestRevisionBumps(t *testing.T) {
	m := NewManager(nil)
	p, _ := m.Create("a", "A")
	rev := func() uint64 { r, _ := m.Revision(p.ID); return r }
	r0 := rev()
	_, _ = m.Join("b", "B", p.Code)
	r1 := rev()
	_, _ = m.SetReady("b", false) // unchanged → no bump
	r2 := rev()
	_, _ = m.SetReady("b", true)
	r3 := rev()
	m.SetDisplayName("b", "Bobby") // cosmetic → no bump
	r4 := rev()
	if !(r1 > r0 && r2 == r1 && r3 > r2 && r4 == r3) {
		t.Fatalf("revisions %d %d %d %d %d", r0, r1, r2, r3, r4)
	}
	if got, _ := m.Get("b"); got.Members[1].DisplayName != "Bobby" {
		t.Fatal("display name not refreshed")
	}
}
