// Copyright BigCar Team. All Rights Reserved.

#include "Online/BCServerSubsystem.h"

#include "CrashCore/Json.h"
#include "CrashCore/Stats.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/CommandLine.h"
#include "Misc/Guid.h"
#include "Misc/Parse.h"

int32 FBCAssignment::ExpectedPlayers() const
{
	int32 N = 0;
	for (const TArray<FBCAssignedPlayer>& T : Teams)
	{
		N += T.Num();
	}
	return N;
}

bool UBCServerSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return IsRunningDedicatedServer();
}

void UBCServerSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	const TCHAR* Cmd = FCommandLine::Get();
	BackendUrl = FPlatformMisc::GetEnvironmentVariable(TEXT("BIGCAR_BACKEND_URL"));
	ServerKey = FPlatformMisc::GetEnvironmentVariable(TEXT("BIGCAR_SERVER_KEY"));
	PublicAddress = FPlatformMisc::GetEnvironmentVariable(TEXT("BIGCAR_PUBLIC_ADDRESS"));
	Region = FPlatformMisc::GetEnvironmentVariable(TEXT("BIGCAR_REGION"));
	FParse::Value(Cmd, TEXT("BackendUrl="), BackendUrl);
	FParse::Value(Cmd, TEXT("ServerKey="), ServerKey);
	FParse::Value(Cmd, TEXT("PublicAddress="), PublicAddress);
	FParse::Value(Cmd, TEXT("Region="), Region);
	FParse::Value(Cmd, TEXT("Port="), Port);
	FParse::Value(Cmd, TEXT("ServerId="), ServerId);
	BackendUrl.RemoveFromEnd(TEXT("/"));
	if (ServerId.IsEmpty())
	{
		ServerId = TEXT("srv_") + FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(12).ToLower();
	}
	if (PublicAddress.IsEmpty())
	{
		PublicAddress = TEXT("127.0.0.1");
	}
	if (Region.IsEmpty())
	{
		Region = TEXT("local");
	}
	if (IsManaged())
	{
		UE_LOG(LogBigCar, Log, TEXT("Dedicated server %s registering with %s as %s:%d (%s)"), *ServerId, *BackendUrl, *PublicAddress, Port, *Region);
		TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UBCServerSubsystem::HeartbeatTick), 0.25f);
	}
	else
	{
		UE_LOG(LogBigCar, Log, TEXT("Dedicated server running unmanaged (no -BackendUrl/-ServerKey): LAN rules, URL options decide the match"));
	}
}

void UBCServerSubsystem::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	Super::Deinitialize();
}

UBCServerSubsystem* UBCServerSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UBCServerSubsystem>() : nullptr;
}

void UBCServerSubsystem::Post(const FString& Path, const FString& Body, TFunction<void(int32, const crash::JsonValue&)> Done)
{
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
	Req->SetURL(BackendUrl + Path);
	Req->SetVerb(TEXT("POST"));
	Req->SetHeader(TEXT("Content-Type"), TEXT("application/json; charset=utf-8"));
	Req->SetHeader(TEXT("X-Server-Key"), ServerKey);
	Req->SetHeader(TEXT("X-Server-Id"), ServerId);
	Req->SetContentAsString(Body);
	Req->SetTimeout(10.0f);
	TWeakObjectPtr<UBCServerSubsystem> WeakThis(this);
	Req->OnProcessRequestComplete().BindLambda([WeakThis, Done](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnected) {
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
			crash::ParseJson(std::string(reinterpret_cast<const char*>(Bytes.GetData()), static_cast<size_t>(Bytes.Num())), Json);
		}
		if (Done)
		{
			Done(Code, Json);
		}
	});
	Req->ProcessRequest();
}

bool UBCServerSubsystem::HeartbeatTick(float DeltaTime)
{
	const double Now = FPlatformTime::Seconds();
	if (ExitAt > 0.0 && Now >= ExitAt)
	{
		UE_LOG(LogBigCar, Log, TEXT("Match over, exiting (orchestrator restarts the server)"));
		FPlatformMisc::RequestExit(false);
		return false;
	}
	if (Now - LastHeartbeat < 5.0)
	{
		return true;
	}
	LastHeartbeat = Now;
	crash::JsonValue Body = crash::JsonValue::MakeObject();
	Body.Set("serverId", crash::JsonValue(BC::ToStd(ServerId)));
	Body.Set("address", crash::JsonValue(BC::ToStd(PublicAddress)));
	Body.Set("port", crash::JsonValue(Port));
	Body.Set("region", crash::JsonValue(BC::ToStd(Region)));
	Body.Set("version", crash::JsonValue("0.1.0"));
	Body.Set("state", crash::JsonValue(BC::ToStd(State)));
	if (bHasAssignment)
	{
		Body.Set("matchId", crash::JsonValue(BC::ToStd(CurrentAssignment.MatchId)));
	}
	Post(TEXT("/v1/servers/heartbeat"), BC::ToFString(Body.Dump()), [this](int32 Code, const crash::JsonValue& Json) {
		if (Code != 200)
		{
			UE_LOG(LogBigCar, Warning, TEXT("Heartbeat failed (%d)"), Code);
			return;
		}
		if (!bHasAssignment && Json["assignment"].IsObject())
		{
			ParseAssignment(Json["assignment"]);
		}
	});
	return true;
}

void UBCServerSubsystem::ParseAssignment(const crash::JsonValue& Json)
{
	FBCAssignment A;
	A.MatchId = BC::ToFString(Json["matchId"].AsString());
	A.Queue = BC::ToFString(Json["queue"].AsString());
	A.Mode = BC::ToFString(Json["mode"].AsString());
	A.Arena = BC::ToFString(Json["arena"].AsString());
	A.bRanked = Json["ranked"].AsBool(false);
	A.bFillWithBots = Json["fillWithBots"].AsBool(false);
	A.BotAi = BC::ToFString(Json["botAi"].AsString("regular"));
	A.TeamSize = Json["teamSize"].AsInt(1);
	A.MaxWaitSec = Json["maxWaitSec"].AsFloat(60.0f);
	int32 TeamIndex = 0;
	for (const crash::JsonValue& Team : Json["teams"].Items())
	{
		TArray<FBCAssignedPlayer> Players;
		for (const crash::JsonValue& P : Team.Items())
		{
			FBCAssignedPlayer Player;
			Player.PlayerId = BC::ToFString(P["playerId"].AsString());
			Player.Name = BC::ToFString(P["displayName"].AsString());
			Player.JoinToken = BC::ToFString(P["joinToken"].AsString());
			Player.Rating = P["rating"].AsDouble(1500.0);
			Player.Team = TeamIndex;
			crash::VehicleBuild Build;
			if (crash::VehicleBuild::FromJson(P["loadout"], Build))
			{
				Player.Loadout = FBCVehicleBuild::FromCrash(Build);
			}
			Players.Add(Player);
		}
		A.Teams.Add(MoveTemp(Players));
		++TeamIndex;
	}
	CurrentAssignment = MoveTemp(A);
	bHasAssignment = true;
	State = TEXT("reserved");
	LastHeartbeat = 0.0; // acknowledge right away
	UE_LOG(LogBigCar, Log, TEXT("Assignment %s: %s on %s, %d players"), *CurrentAssignment.MatchId, *CurrentAssignment.Mode,
		   *CurrentAssignment.Arena, CurrentAssignment.ExpectedPlayers());
}

const FBCAssignedPlayer* UBCServerSubsystem::ValidateJoin(const FString& MatchId, const FString& PlayerId, const FString& Token) const
{
	if (!bHasAssignment || MatchId != CurrentAssignment.MatchId)
	{
		return nullptr;
	}
	for (const TArray<FBCAssignedPlayer>& Team : CurrentAssignment.Teams)
	{
		for (const FBCAssignedPlayer& P : Team)
		{
			if (P.PlayerId == PlayerId && !Token.IsEmpty() && P.JoinToken == Token)
			{
				return &P;
			}
		}
	}
	return nullptr;
}

void UBCServerSubsystem::ReportStart()
{
	if (!IsManaged() || !bHasAssignment)
	{
		return;
	}
	State = TEXT("in_match");
	crash::JsonValue Body = crash::JsonValue::MakeObject();
	Body.Set("serverId", crash::JsonValue(BC::ToStd(ServerId)));
	Body.Set("matchId", crash::JsonValue(BC::ToStd(CurrentAssignment.MatchId)));
	Post(TEXT("/v1/servers/match-start"), BC::ToFString(Body.Dump()), [](int32, const crash::JsonValue&) {});
}

void UBCServerSubsystem::ReportResult(const crash::JsonValue& Result, TFunction<void(bool, const crash::JsonValue&)> Done)
{
	if (!IsManaged() || !bHasAssignment)
	{
		if (Done)
		{
			Done(false, crash::JsonValue());
		}
		return;
	}
	crash::JsonValue Body = Result;
	Body.Set("serverId", crash::JsonValue(BC::ToStd(ServerId)));
	Body.Set("matchId", crash::JsonValue(BC::ToStd(CurrentAssignment.MatchId)));
	Post(TEXT("/v1/servers/match-result"), BC::ToFString(Body.Dump()), [Done](int32 Code, const crash::JsonValue& Json) {
		if (Done)
		{
			Done(Code == 200, Json);
		}
	});
}

void UBCServerSubsystem::ReportAbort(const FString& Reason)
{
	if (!IsManaged() || !bHasAssignment)
	{
		return;
	}
	crash::JsonValue Body = crash::JsonValue::MakeObject();
	Body.Set("serverId", crash::JsonValue(BC::ToStd(ServerId)));
	Body.Set("matchId", crash::JsonValue(BC::ToStd(CurrentAssignment.MatchId)));
	Body.Set("reason", crash::JsonValue(BC::ToStd(Reason)));
	Post(TEXT("/v1/servers/match-abort"), BC::ToFString(Body.Dump()), [](int32, const crash::JsonValue&) {});
}

void UBCServerSubsystem::ShutdownAfterMatch(float DelaySeconds)
{
	ExitAt = FPlatformTime::Seconds() + DelaySeconds;
}
