// Copyright BigCar Team. All Rights Reserved.
//
// HTTP client for the BigCar backend (docs/BACKEND_API.md): guest/named accounts, the online profile (garage,
// wallet), the store, parties (up to 3), matchmaking tickets and the ranked ladder.
#pragma once

#include "Containers/Ticker.h"
#include "CoreMinimal.h"
#include "CrashCore/Profile.h"
#include "Engine/DeveloperSettings.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "BCOnlineSubsystem.generated.h"

namespace crash
{
class JsonValue;
}

UCLASS(Config = Engine, DefaultConfig, meta = (DisplayName = "BigCar Online"))
class BIGCAR_API UBCOnlineSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(Config, EditAnywhere, Category = "Backend")
	FString BackendUrl = TEXT("http://127.0.0.1:8080");

	UPROPERTY(Config, EditAnywhere, Category = "Backend")
	float RequestTimeoutSeconds = 10.0f;
};

UENUM()
enum class EBCOnlineState : uint8
{
	Offline,
	Connecting,
	LoggedIn,
	Error
};

struct FBCStoreOffer
{
	FString Id;
	FString Section;
	FString Tag;
	bool bFeatured = false;
	TArray<crash::ItemRef> Items;
	crash::Price Price;
	crash::Price BasePrice;
	int32 Limit = 0;
	int32 Purchased = 0;
	bool bDaily = false;
	int32 Slot = -1;
	float Discount = 0.0f;
};

struct FBCGoldPack
{
	FString Id;
	int64 Gold = 0;
	int64 Bonus = 0;
	double PriceCny = 0.0;
	double PriceUsd = 0.0;
};

struct FBCSeasonTier
{
	TArray<crash::ItemRef> Free;
	TArray<crash::ItemRef> Premium;
};

struct FBCStoreState
{
	bool bValid = false;
	TArray<FBCStoreOffer> Offers;
	TArray<FBCStoreOffer> Daily;
	FString DailyExpires;
	TArray<FBCGoldPack> GoldPacks;
	FString SeasonId;
	FString SeasonName;
	crash::Price PremiumPrice;
	int64 XpPerTier = 1000;
	TArray<FBCSeasonTier> Tiers;
	int64 PlayerXp = 0;
	int32 PlayerTier = 0;
	bool bPremium = false;
	TArray<int32> ClaimedFree;
	TArray<int32> ClaimedPremium;
};

struct FBCPartyMember
{
	FString PlayerId;
	FString Name;
	bool bReady = false;
};

struct FBCPartyState
{
	bool bInParty = false;
	FString PartyId;
	FString Code;
	FString LeaderId;
	TArray<FBCPartyMember> Members;
};

struct FBCTicketState
{
	FString TicketId;
	FString Queue;
	FString Status; // searching, matched, cancelled, expired
	FString Reason;
	float WaitSec = 0.0f;
	float EstimatedWaitSec = 0.0f;
	bool bMatched = false;
	FString MatchId;
	FString Address;
	FString JoinToken;
	FString Mode;
	FString Arena;
	int32 Team = 0;
};

struct FBCLadderEntry
{
	int32 Rank = 0;
	FString PlayerId;
	FString Name;
	FString Tier;
	int32 Division = 1;
	int32 Wins = 0;
	int32 Losses = 0;
	double Rating = 0.0;
};

DECLARE_MULTICAST_DELEGATE(FBCOnlineEvent);

UCLASS()
class BIGCAR_API UBCOnlineSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	using FDone = TFunction<void(bool bOk, const FString& Error)>;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	static UBCOnlineSubsystem* Get(const UObject* WorldContext);

	FString BackendUrl() const { return BaseUrl; }
	EBCOnlineState State() const { return OnlineState; }
	bool IsLoggedIn() const { return OnlineState == EBCOnlineState::LoggedIn; }
	const FString& LastError() const { return Error; }
	const FString& PlayerId() const { return MyPlayerId; }
	const FString& Username() const { return MyUsername; }

	// ---- Accounts
	void LoginGuest(FDone Done = nullptr);
	void Login(const FString& User, const FString& Password, FDone Done = nullptr);
	void Register(const FString& User, const FString& Password, const FString& DisplayName, FDone Done = nullptr);
	void Logout();

	// ---- Profile (online garage + wallet)
	bool HasProfile() const { return bHasProfile; }
	const crash::Profile& Profile() const { return OnlineProfile; }
	void RefreshProfile(FDone Done = nullptr);
	// POSTs a garage operation (e.g. "/v1/garage/upgrade") and replaces the profile with the response.
	void GarageOp(const FString& Path, const crash::JsonValue& Body, FDone Done);

	// ---- Store
	void FetchStore(FDone Done = nullptr);
	const FBCStoreState& Store() const { return StoreState; }
	void PurchaseOffer(const FString& OfferId, FDone Done);
	void PurchaseDaily(int32 Slot, FDone Done);
	void BuyGold(const FString& PackId, FDone Done);
	void BuySeasonPremium(FDone Done);
	void ClaimSeasonTier(int32 Tier, bool bPremium, FDone Done);

	// ---- Party
	const FBCPartyState& Party() const { return PartyState; }
	bool IsPartyLeader() const { return PartyState.bInParty && PartyState.LeaderId == MyPlayerId; }
	void RefreshParty(FDone Done = nullptr);
	void CreateParty(FDone Done = nullptr);
	void JoinParty(const FString& Code, FDone Done = nullptr);
	void LeaveParty(FDone Done = nullptr);
	void SetReady(bool bReady, FDone Done = nullptr);

	// ---- Matchmaking
	void StartQueue(const FString& Queue, FDone Done = nullptr);
	void CancelQueue();
	bool IsQueued() const { return !TicketState.TicketId.IsEmpty() && TicketState.Status == TEXT("searching"); }
	const FBCTicketState& Ticket() const { return TicketState; }

	// ---- Ladder
	void FetchLadder(const FString& Queue, FDone Done = nullptr);
	const TArray<FBCLadderEntry>& Ladder() const { return LadderEntries; }
	const FString& LadderQueue() const { return LadderQueueId; }

	FBCOnlineEvent OnStateChanged;
	FBCOnlineEvent OnProfileChanged;
	FBCOnlineEvent OnStoreChanged;
	FBCOnlineEvent OnPartyChanged;
	FBCOnlineEvent OnTicketChanged;
	FBCOnlineEvent OnLadderChanged;

private:
	using FResponse = TFunction<void(int32 Code, const crash::JsonValue& Json)>;
	void Request(const FString& Verb, const FString& Path, const FString& Body, FResponse Done);
	void HandleAuth(int32 Code, const crash::JsonValue& Json, FDone Done);
	void ApplyProfile(const crash::JsonValue& ProfileJson);
	void ApplyParty(const crash::JsonValue& PartyJson);
	void ApplyTicket(const crash::JsonValue& TicketJson);
	void ApplyStore(const crash::JsonValue& Json);
	void SetState(EBCOnlineState NewState, const FString& NewError = FString());
	bool PollTick(float DeltaTime);
	void TravelToMatch();
	void LoadIdentity();
	void SaveIdentity() const;
	static FString ErrorOf(int32 Code, const crash::JsonValue& Json);

	FString BaseUrl;
	float Timeout = 10.0f;
	EBCOnlineState OnlineState = EBCOnlineState::Offline;
	FString Error;
	FString Token;
	FString MyPlayerId;
	FString MyUsername;
	FString DeviceId;
	bool bHasProfile = false;
	crash::Profile OnlineProfile;
	FBCStoreState StoreState;
	FBCPartyState PartyState;
	FBCTicketState TicketState;
	TArray<FBCLadderEntry> LadderEntries;
	FString LadderQueueId;
	FTSTicker::FDelegateHandle PollHandle;
	double LastPartyPoll = 0.0;
	double LastTicketPoll = 0.0;
	bool bTravelling = false;
};
