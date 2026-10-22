package httpapi

import (
	"net/http"
	"strconv"

	"github.com/comeheretosee/bigcar/backend/internal/apperr"
	"github.com/comeheretosee/bigcar/backend/internal/ladder"
	"github.com/comeheretosee/bigcar/backend/internal/party"
	"github.com/comeheretosee/bigcar/backend/internal/servers"
)

// --- party ---

type partyView struct {
	PartyID  string         `json:"partyId"`
	Code     string         `json:"code"`
	LeaderID string         `json:"leaderId"`
	Members  []party.Member `json:"members"`
	TicketID *string        `json:"ticketId"`
}

type partyResponse struct {
	Party *partyView `json:"party"`
}

func (a *API) partyResponse(p party.Party, ok bool) partyResponse {
	if !ok {
		return partyResponse{}
	}
	v := &partyView{PartyID: p.ID, Code: p.Code, LeaderID: p.LeaderID, Members: p.Members}
	if v.Members == nil {
		v.Members = []party.Member{}
	}
	if id := a.Matchmaker.PartyTicketID(p.ID, p.LeaderID); id != "" {
		v.TicketID = &id
	}
	return partyResponse{Party: v}
}

func (a *API) displayName(r *http.Request) (string, error) {
	p, err := a.loadProfile(r.Context(), playerID(r))
	if err != nil {
		return "", err
	}
	return p.DisplayName, nil
}

func (a *API) getParty(w http.ResponseWriter, r *http.Request) error {
	writeJSON(w, http.StatusOK, a.partyResponse(a.Parties.Get(playerID(r))))
	return nil
}

func (a *API) createParty(w http.ResponseWriter, r *http.Request) error {
	name, err := a.displayName(r)
	if err != nil {
		return err
	}
	p, err := a.Parties.Create(playerID(r), name)
	if err != nil {
		return err
	}
	a.Matchmaker.ValidateTickets()
	writeJSON(w, http.StatusOK, a.partyResponse(p, true))
	return nil
}

func (a *API) joinParty(w http.ResponseWriter, r *http.Request) error {
	var req struct {
		Code string `json:"code"`
	}
	if _, err := readJSON(r, &req); err != nil {
		return err
	}
	if err := required("code", req.Code); err != nil {
		return err
	}
	name, err := a.displayName(r)
	if err != nil {
		return err
	}
	p, err := a.Parties.Join(playerID(r), name, req.Code)
	if err != nil {
		return err
	}
	a.Matchmaker.ValidateTickets()
	writeJSON(w, http.StatusOK, a.partyResponse(p, true))
	return nil
}

func (a *API) leaveParty(w http.ResponseWriter, r *http.Request) error {
	a.Parties.Leave(playerID(r))
	a.Matchmaker.ValidateTickets()
	writeJSON(w, http.StatusOK, partyResponse{})
	return nil
}

func (a *API) readyParty(w http.ResponseWriter, r *http.Request) error {
	var req struct {
		Ready *bool `json:"ready"`
	}
	if _, err := readJSON(r, &req); err != nil {
		return err
	}
	if req.Ready == nil {
		return apperr.New(apperr.BadRequest, "ready is required")
	}
	p, err := a.Parties.SetReady(playerID(r), *req.Ready)
	if err != nil {
		return err
	}
	a.Matchmaker.ValidateTickets()
	writeJSON(w, http.StatusOK, a.partyResponse(p, true))
	return nil
}

func (a *API) kickParty(w http.ResponseWriter, r *http.Request) error {
	var req struct {
		PlayerID string `json:"playerId"`
	}
	if _, err := readJSON(r, &req); err != nil {
		return err
	}
	if err := required("playerId", req.PlayerID); err != nil {
		return err
	}
	p, err := a.Parties.Kick(playerID(r), req.PlayerID)
	if err != nil {
		return err
	}
	a.Matchmaker.ValidateTickets()
	writeJSON(w, http.StatusOK, a.partyResponse(p, true))
	return nil
}

// --- matchmaking ---

func (a *API) createTicket(w http.ResponseWriter, r *http.Request) error {
	var req struct {
		Queue  string `json:"queue"`
		Region string `json:"region"`
	}
	if _, err := readJSON(r, &req); err != nil {
		return err
	}
	if err := required("queue", req.Queue); err != nil {
		return err
	}
	t, err := a.Matchmaker.CreateTicket(r.Context(), playerID(r), req.Queue, req.Region)
	if err != nil {
		return err
	}
	writeJSON(w, http.StatusOK, map[string]any{"ticket": t})
	return nil
}

func (a *API) currentTicket(w http.ResponseWriter, r *http.Request) error {
	writeJSON(w, http.StatusOK, map[string]any{"ticket": a.Matchmaker.Current(playerID(r))})
	return nil
}

func (a *API) cancelTicket(w http.ResponseWriter, r *http.Request) error {
	t, err := a.Matchmaker.Cancel(playerID(r), r.PathValue("ticketId"))
	if err != nil {
		return err
	}
	writeJSON(w, http.StatusOK, map[string]any{"ticket": t})
	return nil
}

// --- ladder ---

func queryInt(r *http.Request, name string, def int) (int, error) {
	s := r.URL.Query().Get(name)
	if s == "" {
		return def, nil
	}
	n, err := strconv.Atoi(s)
	if err != nil {
		return 0, apperr.New(apperr.BadRequest, "%s must be an integer", name)
	}
	return n, nil
}

func (a *API) ladderPage(w http.ResponseWriter, r *http.Request) error {
	offset, err := queryInt(r, "offset", 0)
	if err != nil {
		return err
	}
	limit, err := queryInt(r, "limit", ladder.DefaultLimit)
	if err != nil {
		return err
	}
	page, err := a.Ladder.Page(r.Context(), r.PathValue("queue"), offset, limit)
	if err != nil {
		return err
	}
	writeJSON(w, http.StatusOK, page)
	return nil
}

func (a *API) ladderMe(w http.ResponseWriter, r *http.Request) error {
	me, err := a.Ladder.Me(r.Context(), playerID(r), r.PathValue("queue"))
	if err != nil {
		return err
	}
	writeJSON(w, http.StatusOK, me)
	return nil
}

func (a *API) currentSeason(w http.ResponseWriter, r *http.Request) error {
	writeJSON(w, http.StatusOK, a.Ladder.CurrentSeason())
	return nil
}

// --- dedicated servers ---

// serverID resolves the reporting server: the "serverId" body field or the
// X-Server-Id header (they must agree when both are present).
func serverID(r *http.Request, fromBody string) (string, error) {
	h := r.Header.Get("X-Server-Id")
	switch {
	case fromBody != "" && h != "" && fromBody != h:
		return "", apperr.New(apperr.BadRequest, "serverId and X-Server-Id disagree")
	case fromBody != "":
		return fromBody, nil
	default:
		return h, nil
	}
}

func (a *API) serverHeartbeat(w http.ResponseWriter, r *http.Request) error {
	var hb servers.Heartbeat
	if _, err := readJSON(r, &hb); err != nil {
		return err
	}
	asg, err := a.Registry.Heartbeat(hb)
	if err != nil {
		return err
	}
	writeJSON(w, http.StatusOK, map[string]any{"assignment": asg})
	return nil
}

type serverMatchRequest struct {
	MatchID  string `json:"matchId"`
	ServerID string `json:"serverId"`
	Reason   string `json:"reason"`
}

func (a *API) serverMatchStart(w http.ResponseWriter, r *http.Request) error {
	var req serverMatchRequest
	if _, err := readJSON(r, &req); err != nil {
		return err
	}
	sid, err := serverID(r, req.ServerID)
	if err != nil {
		return err
	}
	if err := a.Registry.MatchStart(sid, req.MatchID); err != nil {
		return err
	}
	writeJSON(w, http.StatusOK, map[string]bool{"ok": true})
	return nil
}

func (a *API) serverMatchAbort(w http.ResponseWriter, r *http.Request) error {
	var req serverMatchRequest
	if _, err := readJSON(r, &req); err != nil {
		return err
	}
	sid, err := serverID(r, req.ServerID)
	if err != nil {
		return err
	}
	if err := a.Registry.Abort(sid, req.MatchID, req.Reason); err != nil {
		return err
	}
	writeJSON(w, http.StatusOK, map[string]bool{"ok": true})
	return nil
}

func (a *API) serverMatchResult(w http.ResponseWriter, r *http.Request) error {
	body, err := readBody(r, maxResultBodyBytes)
	if err != nil {
		return err
	}
	var res servers.MatchResult
	if err := decodeBody(body, &res, false); err != nil {
		return err
	}
	sid, err := serverID(r, res.ServerID)
	if err != nil {
		return err
	}
	rewards, err := a.Results.Process(r.Context(), sid, res)
	if err != nil {
		return err
	}
	if rewards == nil {
		rewards = []servers.Reward{}
	}
	writeJSON(w, http.StatusOK, map[string]any{"rewards": rewards})
	return nil
}
