package httpapi

import (
	"net/http"
	"time"

	"github.com/comeheretosee/bigcar/backend/internal/data"
	"github.com/comeheretosee/bigcar/backend/internal/profile"
	"github.com/comeheretosee/bigcar/backend/internal/storage"
	"github.com/comeheretosee/bigcar/backend/internal/store"
)

// --- misc ---

func (a *API) healthz(w http.ResponseWriter, r *http.Request) error {
	writeJSON(w, http.StatusOK, map[string]bool{"ok": true})
	return nil
}

func (a *API) meta(w http.ResponseWriter, r *http.Request) error {
	writeJSON(w, http.StatusOK, map[string]any{
		"apiVersion": APIVersion,
		"dataHash":   a.Data.Hash,
		"serverTime": a.Now().UTC().Truncate(time.Second),
		"season":     a.Data.Store.SeasonPass.ID,
	})
	return nil
}

// --- auth ---

func (a *API) authGuest(w http.ResponseWriter, r *http.Request) error {
	var req struct {
		DeviceID string `json:"deviceId"`
	}
	if _, err := readJSON(r, &req); err != nil {
		return err
	}
	res, err := a.Auth.Guest(r.Context(), req.DeviceID)
	if err != nil {
		return err
	}
	writeJSON(w, http.StatusOK, res)
	return nil
}

type credentials struct {
	Username    string `json:"username"`
	Password    string `json:"password"`
	DisplayName string `json:"displayName"`
}

func (a *API) authRegister(w http.ResponseWriter, r *http.Request) error {
	var req credentials
	if _, err := readJSON(r, &req); err != nil {
		return err
	}
	res, err := a.Auth.Register(r.Context(), req.Username, req.Password, req.DisplayName)
	if err != nil {
		return err
	}
	writeJSON(w, http.StatusOK, res)
	return nil
}

func (a *API) authLogin(w http.ResponseWriter, r *http.Request) error {
	var req credentials
	if _, err := readJSON(r, &req); err != nil {
		return err
	}
	res, err := a.Auth.Login(r.Context(), req.Username, req.Password)
	if err != nil {
		return err
	}
	writeJSON(w, http.StatusOK, res)
	return nil
}

func (a *API) authLink(w http.ResponseWriter, r *http.Request) error {
	var req credentials
	if _, err := readJSON(r, &req); err != nil {
		return err
	}
	if err := a.Auth.Link(r.Context(), playerID(r), req.Username, req.Password); err != nil {
		return err
	}
	writeJSON(w, http.StatusOK, map[string]bool{"ok": true})
	return nil
}

// --- profile & garage ---

func (a *API) getProfile(w http.ResponseWriter, r *http.Request) error {
	p, err := a.loadProfile(r.Context(), playerID(r))
	if err != nil {
		return err
	}
	writeJSON(w, http.StatusOK, profileResponse{Profile: p})
	return nil
}

func (a *API) setName(w http.ResponseWriter, r *http.Request) error {
	var req struct {
		DisplayName string `json:"displayName"`
	}
	body, err := readJSON(r, &req)
	if err != nil {
		return err
	}
	var name string
	err = a.mutate(w, r, body, func(tx storage.Tx, p *profile.Profile) (any, error) {
		if err := profile.SetDisplayName(p, req.DisplayName); err != nil {
			return nil, err
		}
		name = p.DisplayName
		return profileResponse{Profile: p}, nil
	})
	if err == nil && name != "" {
		a.Parties.SetDisplayName(playerID(r), name)
	}
	return err
}

type vehicleRequest struct {
	VehicleID string `json:"vehicleId"`
	Category  string `json:"category"`
}

func (a *API) buyVehicle(w http.ResponseWriter, r *http.Request) error {
	var req vehicleRequest
	body, err := readJSON(r, &req)
	if err != nil {
		return err
	}
	if err := required("vehicleId", req.VehicleID); err != nil {
		return err
	}
	return a.mutate(w, r, body, func(tx storage.Tx, p *profile.Profile) (any, error) {
		return profileResponse{p}, profile.BuyVehicle(p, a.Data, req.VehicleID)
	})
}

func (a *API) selectVehicle(w http.ResponseWriter, r *http.Request) error {
	var req vehicleRequest
	body, err := readJSON(r, &req)
	if err != nil {
		return err
	}
	if err := required("vehicleId", req.VehicleID); err != nil {
		return err
	}
	return a.mutate(w, r, body, func(tx storage.Tx, p *profile.Profile) (any, error) {
		return profileResponse{p}, profile.SelectVehicle(p, a.Data, req.VehicleID)
	})
}

func (a *API) upgrade(w http.ResponseWriter, r *http.Request) error {
	var req vehicleRequest
	body, err := readJSON(r, &req)
	if err != nil {
		return err
	}
	if err := required("vehicleId", req.VehicleID); err != nil {
		return err
	}
	if err := required("category", req.Category); err != nil {
		return err
	}
	return a.mutate(w, r, body, func(tx storage.Tx, p *profile.Profile) (any, error) {
		_, err := profile.Upgrade(p, a.Data, req.VehicleID, req.Category)
		return profileResponse{p}, err
	})
}

type itemRequest struct {
	ItemID string `json:"itemId"`
}

func (a *API) buyEquipment(w http.ResponseWriter, r *http.Request) error {
	var req itemRequest
	body, err := readJSON(r, &req)
	if err != nil {
		return err
	}
	if err := required("itemId", req.ItemID); err != nil {
		return err
	}
	return a.mutate(w, r, body, func(tx storage.Tx, p *profile.Profile) (any, error) {
		return profileResponse{p}, profile.BuyEquipment(p, a.Data, req.ItemID)
	})
}

func (a *API) equip(w http.ResponseWriter, r *http.Request) error {
	var req struct {
		VehicleID string  `json:"vehicleId"`
		Slot      string  `json:"slot"`
		ItemID    *string `json:"itemId"`
	}
	body, err := readJSON(r, &req)
	if err != nil {
		return err
	}
	if err := required("vehicleId", req.VehicleID); err != nil {
		return err
	}
	if err := required("slot", req.Slot); err != nil {
		return err
	}
	return a.mutate(w, r, body, func(tx storage.Tx, p *profile.Profile) (any, error) {
		return profileResponse{p}, profile.Equip(p, a.Data, req.VehicleID, req.Slot, req.ItemID)
	})
}

func (a *API) buyCosmetic(w http.ResponseWriter, r *http.Request) error {
	var req itemRequest
	body, err := readJSON(r, &req)
	if err != nil {
		return err
	}
	if err := required("itemId", req.ItemID); err != nil {
		return err
	}
	return a.mutate(w, r, body, func(tx storage.Tx, p *profile.Profile) (any, error) {
		return profileResponse{p}, profile.BuyCosmetic(p, a.Data, req.ItemID)
	})
}

func (a *API) customize(w http.ResponseWriter, r *http.Request) error {
	var req struct {
		VehicleID string  `json:"vehicleId"`
		Paint     *string `json:"paint"`
		Wheels    *string `json:"wheels"`
	}
	body, err := readJSON(r, &req)
	if err != nil {
		return err
	}
	if err := required("vehicleId", req.VehicleID); err != nil {
		return err
	}
	return a.mutate(w, r, body, func(tx storage.Tx, p *profile.Profile) (any, error) {
		return profileResponse{p}, profile.Customize(p, a.Data, req.VehicleID, req.Paint, req.Wheels)
	})
}

// --- store ---

func (a *API) getStore(w http.ResponseWriter, r *http.Request) error {
	p, err := a.loadProfile(r.Context(), playerID(r))
	if err != nil {
		return err
	}
	writeJSON(w, http.StatusOK, a.Shop.View(p))
	return nil
}

func (a *API) purchase(w http.ResponseWriter, r *http.Request) error {
	var req store.PurchaseRequest
	body, err := readJSON(r, &req)
	if err != nil {
		return err
	}
	return a.mutate(w, r, body, func(tx storage.Tx, p *profile.Profile) (any, error) {
		granted, err := a.Shop.Purchase(p, req)
		return grantResponse{Profile: p, Granted: granted}, err
	})
}

func (a *API) goldCheckout(w http.ResponseWriter, r *http.Request) error {
	var req store.CheckoutRequest
	if _, err := readJSON(r, &req); err != nil {
		return err
	}
	if err := required("packId", req.PackID); err != nil {
		return err
	}
	if err := required("provider", req.Provider); err != nil {
		return err
	}
	res, err := a.Shop.Checkout(r.Context(), playerID(r), req, r.Header.Get("Idempotency-Key"))
	if err != nil {
		return err
	}
	writeJSON(w, http.StatusOK, res)
	return nil
}

func (a *API) paymentWebhook(w http.ResponseWriter, r *http.Request) error {
	body, err := readBody(r, maxBodyBytes)
	if err != nil {
		return err
	}
	if err := a.Shop.HandleWebhook(r.Context(), r.PathValue("provider"), r.Header, body); err != nil {
		return err
	}
	writeJSON(w, http.StatusOK, map[string]bool{"ok": true})
	return nil
}

func (a *API) buyPremium(w http.ResponseWriter, r *http.Request) error {
	body, err := readBody(r, maxBodyBytes)
	if err != nil {
		return err
	}
	return a.mutate(w, r, body, func(tx storage.Tx, p *profile.Profile) (any, error) {
		return profileResponse{p}, a.Shop.BuyPremium(p)
	})
}

func (a *API) claimSeasonPass(w http.ResponseWriter, r *http.Request) error {
	var req store.ClaimRequest
	body, err := readJSON(r, &req)
	if err != nil {
		return err
	}
	return a.mutate(w, r, body, func(tx storage.Tx, p *profile.Profile) (any, error) {
		granted, err := a.Shop.Claim(p, req)
		if granted == nil {
			granted = []data.Item{}
		}
		return grantResponse{Profile: p, Granted: granted}, err
	})
}
