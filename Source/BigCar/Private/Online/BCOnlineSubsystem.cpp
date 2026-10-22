// Copyright BigCar Team. All Rights Reserved.

#include "Online/BCOnlineSubsystem.h"

#include "Core/BigCarTypes.h"
#include "CrashCore/Json.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

namespace BCOnlineDetail
{
FString IdentityPath()
{
	return FPaths::ProjectSavedDir() / TEXT("BigCar/online.json");
}

crash::Price ParsePriceJson(const crash::JsonValue& V)
{
	crash::Price P;
	crash::ParsePrice(V, P);
	return P;
}

TArray<crash::ItemRef> ParseItems(const crash::JsonValue& Arr)
{
	TArray<crash::ItemRef> Out;
	for (const crash::JsonValue& V : Arr.Items())
	{
		crash::ItemRef Item;
		if (crash::ParseItemRef(V, Item))
		{
			Out.Add(Item);
		}
	}
	return Out;
}

FString S(const crash::JsonValue& V)
{
	return BC::ToFString(V.AsString());
}
} // namespace BCOnlineDetail

void UBCOnlineSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	const UBCOnlineSettings* Settings = GetDefault<UBCOnlineSettings>();
	BaseUrl = Settings->BackendUrl;
	Timeout = Settings->RequestTimeoutSeconds;
	FString Override = FPlatformMisc::GetEnvironmentVariable(TEXT("BIGCAR_BACKEND_URL"));
	FParse::Value(FCommandLine::Get(), TEXT("BackendUrl="), Override);
	if (!Override.IsEmpty())
	{
		BaseUrl = Override;
	}
	BaseUrl.RemoveFromEnd(TEXT("/"));
	LoadIdentity();
	PollHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UBCOnlineSubsystem::PollTick), 0.5f);
}

void UBCOnlineSubsystem::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(PollHandle);
	Super::Deinitialize();
}

UBCOnlineSubsystem* UBCOnlineSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UBCOnlineSubsystem>() : nullptr;
}

void UBCOnlineSubsystem::LoadIdentity()
{
	FString Text;
	if (FFileHelper::LoadFileToString(Text, *BCOnlineDetail::IdentityPath()))
	{
		crash::JsonValue Json;
		if (crash::ParseJson(BC::ToStd(Text), Json))
		{
			DeviceId = BCOnlineDetail::S(Json["deviceId"]);
			MyUsername = BCOnlineDetail::S(Json["username"]);
		}
	}
	if (DeviceId.IsEmpty())
	{
		DeviceId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
		SaveIdentity();
	}
}

void UBCOnlineSubsystem::SaveIdentity() const
{
	crash::JsonValue Json = crash::JsonValue::MakeObject();
	Json.Set("deviceId", crash::JsonValue(BC::ToStd(DeviceId)));
	Json.Set("username", crash::JsonValue(BC::ToStd(MyUsername)));
	FFileHelper::SaveStringToFile(BC::ToFString(Json.Dump(true)), *BCOnlineDetail::IdentityPath(),
								  FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

void UBCOnlineSubsystem::SetState(EBCOnlineState NewState, const FString& NewError)
{
	OnlineState = NewState;
	Error = NewError;
	OnStateChanged.Broadcast();
}

FString UBCOnlineSubsystem::ErrorOf(int32 Code, const crash::JsonValue& Json)
{
	const crash::JsonValue& E = Json["error"];
	if (E.IsObject())
	{
		return BCOnlineDetail::S(E["code"]);
	}
	return Code == 0 ? TEXT("network") : FString::Printf(TEXT("http_%d"), Code);
}

void UBCOnlineSubsystem::Request(const FString& Verb, const FString& Path, const FString& Body, FResponse Done)
{
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
	Req->SetURL(BaseUrl + Path);
	Req->SetVerb(Verb);
	Req->SetHeader(TEXT("Content-Type"), TEXT("application/json; charset=utf-8"));
	Req->SetHeader(TEXT("Accept"), TEXT("application/json"));
	if (!Token.IsEmpty())
	{
		Req->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + Token);
	}
	if (Verb == TEXT("POST"))
	{
		Req->SetHeader(TEXT("Idempotency-Key"), FGuid::NewGuid().ToString(EGuidFormats::Digits));
		Req->SetContentAsString(Body.IsEmpty() ? FString(TEXT("{}")) : Body);
	}
	Req->SetTimeout(Timeout);
	TWeakObjectPtr<UBCOnlineSubsystem> WeakThis(this);
	Req->OnProcessRequestComplete().BindLambda(
		[WeakThis, Done](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnected) {
			if (!WeakThis.IsValid())
			{
				return;
			}
			crash::JsonValue Json;
			int32 Code = 0;
			if (bConnected && Response.IsValid())
			{
				Code = Response->GetResponseCode();
				const TArray<uint8>& Bytes = Response->GetContent();
				const std::string Text(reinterpret_cast<const char*>(Bytes.GetData()), static_cast<size_t>(Bytes.Num()));
				crash::ParseJson(Text, Json);
			}
			if (Done)
			{
				Done(Code, Json);
			}
		});
	Req->ProcessRequest();
}

// ---------------------------------------------------------------------------------------------
// Accounts

void UBCOnlineSubsystem::HandleAuth(int32 Code, const crash::JsonValue& Json, FDone Done)
{
	if (Code != 200 || !Json["token"].IsString())
	{
		SetState(EBCOnlineState::Error, ErrorOf(Code, Json));
		if (Done)
		{
			Done(false, Error);
		}
		return;
	}
	Token = BCOnlineDetail::S(Json["token"]);
	MyPlayerId = BCOnlineDetail::S(Json["playerId"]);
	SetState(EBCOnlineState::LoggedIn);
	SaveIdentity();
	RefreshProfile(Done);
	RefreshParty();
}

void UBCOnlineSubsystem::LoginGuest(FDone Done)
{
	SetState(EBCOnlineState::Connecting);
	crash::JsonValue Body = crash::JsonValue::MakeObject();
	Body.Set("deviceId", crash::JsonValue(BC::ToStd(DeviceId)));
	Request(TEXT("POST"), TEXT("/v1/auth/guest"), BC::ToFString(Body.Dump()),
			[this, Done](int32 Code, const crash::JsonValue& Json) { HandleAuth(Code, Json, Done); });
}

void UBCOnlineSubsystem::Login(const FString& User, const FString& Password, FDone Done)
{
	SetState(EBCOnlineState::Connecting);
	MyUsername = User;
	crash::JsonValue Body = crash::JsonValue::MakeObject();
	Body.Set("username", crash::JsonValue(BC::ToStd(User)));
	Body.Set("password", crash::JsonValue(BC::ToStd(Password)));
	Request(TEXT("POST"), TEXT("/v1/auth/login"), BC::ToFString(Body.Dump()),
			[this, Done](int32 Code, const crash::JsonValue& Json) { HandleAuth(Code, Json, Done); });
}

void UBCOnlineSubsystem::Register(const FString& User, const FString& Password, const FString& DisplayName, FDone Done)
{
	SetState(EBCOnlineState::Connecting);
	MyUsername = User;
	crash::JsonValue Body = crash::JsonValue::MakeObject();
	Body.Set("username", crash::JsonValue(BC::ToStd(User)));
	Body.Set("password", crash::JsonValue(BC::ToStd(Password)));
	Body.Set("displayName", crash::JsonValue(BC::ToStd(DisplayName)));
	Request(TEXT("POST"), TEXT("/v1/auth/register"), BC::ToFString(Body.Dump()),
			[this, Done](int32 Code, const crash::JsonValue& Json) { HandleAuth(Code, Json, Done); });
}

void UBCOnlineSubsystem::Logout()
{
	CancelQueue();
	Token.Reset();
	MyPlayerId.Reset();
	bHasProfile = false;
	PartyState = FBCPartyState();
	TicketState = FBCTicketState();
	StoreState = FBCStoreState();
	SetState(EBCOnlineState::Offline);
	OnProfileChanged.Broadcast();
	OnPartyChanged.Broadcast();
}

// ---------------------------------------------------------------------------------------------
// Profile

void UBCOnlineSubsystem::ApplyProfile(const crash::JsonValue& ProfileJson)
{
	crash::Profile Parsed;
	if (crash::Profile::FromJson(ProfileJson, Parsed))
	{
		OnlineProfile = MoveTemp(Parsed);
		bHasProfile = true;
		OnProfileChanged.Broadcast();
	}
}

void UBCOnlineSubsystem::RefreshProfile(FDone Done)
{
	Request(TEXT("GET"), TEXT("/v1/profile"), FString(), [this, Done](int32 Code, const crash::JsonValue& Json) {
		const bool bOk = Code == 200 && Json["profile"].IsObject();
		if (bOk)
		{
			ApplyProfile(Json["profile"]);
		}
		if (Done)
		{
			Done(bOk, bOk ? FString() : ErrorOf(Code, Json));
		}
	});
}

void UBCOnlineSubsystem::GarageOp(const FString& Path, const crash::JsonValue& Body, FDone Done)
{
	if (!IsLoggedIn())
	{
		if (Done)
		{
			Done(false, TEXT("unauthorized"));
		}
		return;
	}
	Request(TEXT("POST"), Path, BC::ToFString(Body.Dump()), [this, Done](int32 Code, const crash::JsonValue& Json) {
		const bool bOk = Code == 200 && Json["profile"].IsObject();
		if (bOk)
		{
			ApplyProfile(Json["profile"]);
		}
		if (Done)
		{
			Done(bOk, bOk ? FString() : ErrorOf(Code, Json));
		}
	});
}

// ---------------------------------------------------------------------------------------------
// Store

void UBCOnlineSubsystem::ApplyStore(const crash::JsonValue& Json)
{
	using namespace BCOnlineDetail;
	FBCStoreState Store;
	Store.bValid = true;
	for (const crash::JsonValue& O : Json["offers"].Items())
	{
		FBCStoreOffer Offer;
		Offer.Id = S(O["id"]);
		Offer.Section = S(O["section"]);
		Offer.Tag = S(O["tag"]);
		Offer.bFeatured = O["featured"].AsBool(false);
		Offer.Items = ParseItems(O["items"]);
		Offer.Price = ParsePriceJson(O["price"]);
		Offer.Limit = O["limit"].AsInt(0);
		Offer.Purchased = O["purchased"].AsInt(0);
		Store.Offers.Add(MoveTemp(Offer));
	}
	const crash::JsonValue& Daily = Json["daily"];
	Store.DailyExpires = S(Daily["expiresAt"]);
	for (const crash::JsonValue& D : Daily["deals"].Items())
	{
		FBCStoreOffer Offer;
		Offer.bDaily = true;
		Offer.Slot = D["slot"].AsInt(-1);
		Offer.Id = FString::Printf(TEXT("daily_%d"), Offer.Slot);
		Offer.Section = TEXT("daily");
		crash::ItemRef Item;
		if (crash::ParseItemRef(D["item"], Item))
		{
			Offer.Items.Add(Item);
		}
		Offer.Price = ParsePriceJson(D["price"]);
		Offer.BasePrice = ParsePriceJson(D["basePrice"]);
		Offer.Discount = D["discount"].AsFloat(0.0f);
		Offer.Purchased = D["purchased"].AsBool(false) ? 1 : 0;
		Offer.Limit = 1;
		Store.Daily.Add(MoveTemp(Offer));
	}
	for (const crash::JsonValue& G : Json["goldPacks"].Items())
	{
		FBCGoldPack Pack;
		Pack.Id = S(G["id"]);
		Pack.Gold = G["gold"].AsInt64(0);
		Pack.Bonus = G["bonus"].AsInt64(0);
		Pack.PriceCny = G["prices"]["CNY"].AsDouble(0.0);
		Pack.PriceUsd = G["prices"]["USD"].AsDouble(0.0);
		Store.GoldPacks.Add(Pack);
	}
	const crash::JsonValue& Pass = Json["seasonPass"];
	Store.SeasonId = S(Pass["id"]);
	Store.SeasonName = S(Pass["name"]);
	Store.PremiumPrice = ParsePriceJson(Pass["premiumPrice"]);
	Store.XpPerTier = Pass["xpPerTier"].AsInt64(1000);
	for (const crash::JsonValue& T : Pass["tiers"].Items())
	{
		FBCSeasonTier Tier;
		Tier.Free = ParseItems(T["free"]);
		Tier.Premium = ParseItems(T["premium"]);
		Store.Tiers.Add(MoveTemp(Tier));
	}
	Store.PlayerXp = Pass["playerXp"].AsInt64(0);
	Store.PlayerTier = Pass["playerTier"].AsInt(0);
	Store.bPremium = Pass["premium"].AsBool(false);
	for (const crash::JsonValue& V : Pass["claimedFree"].Items())
	{
		Store.ClaimedFree.Add(V.AsInt());
	}
	for (const crash::JsonValue& V : Pass["claimedPremium"].Items())
	{
		Store.ClaimedPremium.Add(V.AsInt());
	}
	StoreState = MoveTemp(Store);
	OnStoreChanged.Broadcast();
}

void UBCOnlineSubsystem::FetchStore(FDone Done)
{
	Request(TEXT("GET"), TEXT("/v1/store"), FString(), [this, Done](int32 Code, const crash::JsonValue& Json) {
		const bool bOk = Code == 200;
		if (bOk)
		{
			ApplyStore(Json);
		}
		if (Done)
		{
			Done(bOk, bOk ? FString() : ErrorOf(Code, Json));
		}
	});
}

void UBCOnlineSubsystem::PurchaseOffer(const FString& OfferId, FDone Done)
{
	crash::JsonValue Body = crash::JsonValue::MakeObject();
	Body.Set("offerId", crash::JsonValue(BC::ToStd(OfferId)));
	GarageOp(TEXT("/v1/store/purchase"), Body, [this, Done](bool bOk, const FString& Err) {
		FetchStore();
		if (Done)
		{
			Done(bOk, Err);
		}
	});
}

void UBCOnlineSubsystem::PurchaseDaily(int32 Slot, FDone Done)
{
	crash::JsonValue Body = crash::JsonValue::MakeObject();
	Body.Set("dailySlot", crash::JsonValue(Slot));
	GarageOp(TEXT("/v1/store/purchase"), Body, [this, Done](bool bOk, const FString& Err) {
		FetchStore();
		if (Done)
		{
			Done(bOk, Err);
		}
	});
}

void UBCOnlineSubsystem::BuyGold(const FString& PackId, FDone Done)
{
	crash::JsonValue Body = crash::JsonValue::MakeObject();
	Body.Set("packId", crash::JsonValue(BC::ToStd(PackId)));
	Body.Set("provider", crash::JsonValue("sandbox"));
	Request(TEXT("POST"), TEXT("/v1/store/gold/checkout"), BC::ToFString(Body.Dump()),
			[this, Done](int32 Code, const crash::JsonValue& Json) {
				const bool bOk = Code == 200;
				if (Json["profile"].IsObject())
				{
					ApplyProfile(Json["profile"]);
				}
				const FString Status = BCOnlineDetail::S(Json["status"]);
				if (Done)
				{
					Done(bOk, bOk ? (Status == TEXT("completed") ? FString() : FString(TEXT("pending"))) : ErrorOf(Code, Json));
				}
			});
}

void UBCOnlineSubsystem::BuySeasonPremium(FDone Done)
{
	GarageOp(TEXT("/v1/seasonpass/buy-premium"), crash::JsonValue::MakeObject(), [this, Done](bool bOk, const FString& Err) {
		FetchStore();
		if (Done)
		{
			Done(bOk, Err);
		}
	});
}

void UBCOnlineSubsystem::ClaimSeasonTier(int32 Tier, bool bPremium, FDone Done)
{
	crash::JsonValue Body = crash::JsonValue::MakeObject();
	Body.Set("tier", crash::JsonValue(Tier));
	Body.Set("track", crash::JsonValue(bPremium ? "premium" : "free"));
	GarageOp(TEXT("/v1/seasonpass/claim"), Body, [this, Done](bool bOk, const FString& Err) {
		FetchStore();
		if (Done)
		{
			Done(bOk, Err);
		}
	});
}

// ---------------------------------------------------------------------------------------------
// Party

void UBCOnlineSubsystem::ApplyParty(const crash::JsonValue& PartyJson)
{
	using namespace BCOnlineDetail;
	FBCPartyState P;
	if (PartyJson.IsObject())
	{
		P.bInParty = true;
		P.PartyId = S(PartyJson["partyId"]);
		P.Code = S(PartyJson["code"]);
		P.LeaderId = S(PartyJson["leaderId"]);
		for (const crash::JsonValue& M : PartyJson["members"].Items())
		{
			FBCPartyMember Member;
			Member.PlayerId = S(M["playerId"]);
			Member.Name = S(M["displayName"]);
			Member.bReady = M["ready"].AsBool(false);
			P.Members.Add(Member);
		}
	}
	PartyState = MoveTemp(P);
	OnPartyChanged.Broadcast();
}

void UBCOnlineSubsystem::RefreshParty(FDone Done)
{
	if (!IsLoggedIn())
	{
		return;
	}
	Request(TEXT("GET"), TEXT("/v1/party"), FString(), [this, Done](int32 Code, const crash::JsonValue& Json) {
		if (Code == 200)
		{
			ApplyParty(Json["party"]);
		}
		if (Done)
		{
			Done(Code == 200, ErrorOf(Code, Json));
		}
	});
}

void UBCOnlineSubsystem::CreateParty(FDone Done)
{
	Request(TEXT("POST"), TEXT("/v1/party"), FString(), [this, Done](int32 Code, const crash::JsonValue& Json) {
		if (Code == 200)
		{
			ApplyParty(Json["party"]);
		}
		if (Done)
		{
			Done(Code == 200, Code == 200 ? FString() : ErrorOf(Code, Json));
		}
	});
}

void UBCOnlineSubsystem::JoinParty(const FString& Code, FDone Done)
{
	crash::JsonValue Body = crash::JsonValue::MakeObject();
	Body.Set("code", crash::JsonValue(BC::ToStd(Code.ToUpper())));
	Request(TEXT("POST"), TEXT("/v1/party/join"), BC::ToFString(Body.Dump()), [this, Done](int32 HttpCode, const crash::JsonValue& Json) {
		if (HttpCode == 200)
		{
			ApplyParty(Json["party"]);
		}
		if (Done)
		{
			Done(HttpCode == 200, HttpCode == 200 ? FString() : ErrorOf(HttpCode, Json));
		}
	});
}

void UBCOnlineSubsystem::LeaveParty(FDone Done)
{
	Request(TEXT("POST"), TEXT("/v1/party/leave"), FString(), [this, Done](int32 Code, const crash::JsonValue& Json) {
		ApplyParty(crash::JsonValue());
		if (Done)
		{
			Done(Code == 200, Code == 200 ? FString() : ErrorOf(Code, Json));
		}
	});
}

void UBCOnlineSubsystem::SetReady(bool bReady, FDone Done)
{
	crash::JsonValue Body = crash::JsonValue::MakeObject();
	Body.Set("ready", crash::JsonValue(bReady));
	Request(TEXT("POST"), TEXT("/v1/party/ready"), BC::ToFString(Body.Dump()), [this, Done](int32 Code, const crash::JsonValue& Json) {
		if (Code == 200)
		{
			ApplyParty(Json["party"]);
		}
		if (Done)
		{
			Done(Code == 200, Code == 200 ? FString() : ErrorOf(Code, Json));
		}
	});
}

// ---------------------------------------------------------------------------------------------
// Matchmaking

void UBCOnlineSubsystem::ApplyTicket(const crash::JsonValue& TicketJson)
{
	using namespace BCOnlineDetail;
	FBCTicketState T;
	if (TicketJson.IsObject())
	{
		T.TicketId = S(TicketJson["ticketId"]);
		T.Queue = S(TicketJson["queue"]);
		T.Status = S(TicketJson["status"]);
		T.Reason = S(TicketJson["reason"]);
		T.WaitSec = TicketJson["waitSec"].AsFloat(0.0f);
		T.EstimatedWaitSec = TicketJson["estimatedWaitSec"].AsFloat(0.0f);
		const crash::JsonValue& M = TicketJson["match"];
		if (M.IsObject())
		{
			T.bMatched = true;
			T.MatchId = S(M["matchId"]);
			T.Address = S(M["address"]);
			T.JoinToken = S(M["joinToken"]);
			T.Mode = S(M["mode"]);
			T.Arena = S(M["arena"]);
			T.Team = M["team"].AsInt(0);
		}
	}
	const bool bNewMatch = T.bMatched && !TicketState.bMatched;
	TicketState = MoveTemp(T);
	OnTicketChanged.Broadcast();
	if (bNewMatch)
	{
		TravelToMatch();
	}
}

void UBCOnlineSubsystem::StartQueue(const FString& Queue, FDone Done)
{
	crash::JsonValue Body = crash::JsonValue::MakeObject();
	Body.Set("queue", crash::JsonValue(BC::ToStd(Queue)));
	Request(TEXT("POST"), TEXT("/v1/matchmaking/tickets"), BC::ToFString(Body.Dump()),
			[this, Done](int32 Code, const crash::JsonValue& Json) {
				if (Code == 200)
				{
					ApplyTicket(Json["ticket"]);
				}
				if (Done)
				{
					Done(Code == 200, Code == 200 ? FString() : ErrorOf(Code, Json));
				}
			});
}

void UBCOnlineSubsystem::CancelQueue()
{
	if (TicketState.TicketId.IsEmpty())
	{
		return;
	}
	Request(TEXT("DELETE"), TEXT("/v1/matchmaking/tickets/") + TicketState.TicketId, FString(), [](int32, const crash::JsonValue&) {});
	TicketState = FBCTicketState();
	OnTicketChanged.Broadcast();
}

void UBCOnlineSubsystem::TravelToMatch()
{
	if (bTravelling || TicketState.Address.IsEmpty())
	{
		return;
	}
	UGameInstance* GI = GetGameInstance();
	APlayerController* PC = GI ? GI->GetFirstLocalPlayerController() : nullptr;
	if (!PC)
	{
		return;
	}
	bTravelling = true;
	const FString Url = FString::Printf(TEXT("%s?MatchId=%s?JoinToken=%s?PlayerId=%s"), *TicketState.Address, *TicketState.MatchId,
										*TicketState.JoinToken, *MyPlayerId);
	UE_LOG(LogBigCar, Log, TEXT("Match found (%s on %s), travelling to %s"), *TicketState.Mode, *TicketState.Arena, *TicketState.Address);
	PC->ClientTravel(Url, TRAVEL_Absolute);
}

bool UBCOnlineSubsystem::PollTick(float DeltaTime)
{
	if (!IsLoggedIn())
	{
		return true;
	}
	const double Now = FPlatformTime::Seconds();
	if ((IsQueued() || PartyState.bInParty) && Now - LastTicketPoll > 2.0)
	{
		LastTicketPoll = Now;
		Request(TEXT("GET"), TEXT("/v1/matchmaking/current"), FString(), [this](int32 Code, const crash::JsonValue& Json) {
			if (Code == 200)
			{
				ApplyTicket(Json["ticket"]);
			}
		});
	}
	if (PartyState.bInParty && Now - LastPartyPoll > 3.0)
	{
		LastPartyPoll = Now;
		RefreshParty();
	}
	if (bTravelling && !TicketState.bMatched)
	{
		bTravelling = false;
	}
	return true;
}

// ---------------------------------------------------------------------------------------------
// Ladder

void UBCOnlineSubsystem::FetchLadder(const FString& Queue, FDone Done)
{
	LadderQueueId = Queue;
	Request(TEXT("GET"), FString::Printf(TEXT("/v1/ladder/%s?offset=0&limit=50"), *Queue), FString(),
			[this, Done](int32 Code, const crash::JsonValue& Json) {
				using namespace BCOnlineDetail;
				if (Code == 200)
				{
					LadderEntries.Reset();
					for (const crash::JsonValue& E : Json["entries"].Items())
					{
						FBCLadderEntry Entry;
						Entry.Rank = E["rank"].AsInt(0);
						Entry.PlayerId = S(E["playerId"]);
						Entry.Name = S(E["displayName"]);
						Entry.Tier = S(E["tier"]);
						Entry.Division = E["division"].AsInt(1);
						Entry.Wins = E["wins"].AsInt(0);
						Entry.Losses = E["losses"].AsInt(0);
						Entry.Rating = E["rating"].AsDouble(0.0);
						LadderEntries.Add(Entry);
					}
					OnLadderChanged.Broadcast();
				}
				if (Done)
				{
					Done(Code == 200, Code == 200 ? FString() : ErrorOf(Code, Json));
				}
			});
}
