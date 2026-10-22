// Copyright BigCar Team. All Rights Reserved.
//
// Dedicated-server side of the backend protocol (docs/BACKEND_API.md "Dedicated servers"): heartbeats, match
// assignments, join-token validation, match start/result/abort. One match per server process.
#pragma once

#include "Containers/Ticker.h"
#include "Core/BigCarTypes.h"
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "BCServerSubsystem.generated.h"

namespace crash
{
class JsonValue;
}

struct FBCAssignedPlayer
{
	FString PlayerId;
	FString Name;
	FString JoinToken;
	int32 Team = 0;
	double Rating = 1500.0;
	FBCVehicleBuild Loadout;
};

struct FBCAssignment
{
	FString MatchId;
	FString Queue;
	FString Mode;
	FString Arena;
	FString BotAi = TEXT("regular");
	bool bRanked = false;
	bool bFillWithBots = false;
	int32 TeamSize = 1;
	float MaxWaitSec = 60.0f;
	TArray<TArray<FBCAssignedPlayer>> Teams;

	int32 ExpectedPlayers() const;
};

UCLASS()
class BIGCAR_API UBCServerSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	static UBCServerSubsystem* Get(const UObject* WorldContext);

	// True when this server is registered with a backend (otherwise it is a plain LAN/listen-style server).
	bool IsManaged() const { return !BackendUrl.IsEmpty() && !ServerKey.IsEmpty(); }
	bool HasAssignment() const { return bHasAssignment; }
	const FBCAssignment& Assignment() const { return CurrentAssignment; }
	const FBCAssignedPlayer* ValidateJoin(const FString& MatchId, const FString& PlayerId, const FString& Token) const;

	void ReportStart();
	void ReportResult(const crash::JsonValue& Result, TFunction<void(bool bOk, const crash::JsonValue& Response)> Done);
	void ReportAbort(const FString& Reason);
	// Ends the process after the match (the orchestrator restarts it).
	void ShutdownAfterMatch(float DelaySeconds);

private:
	bool HeartbeatTick(float DeltaTime);
	void Post(const FString& Path, const FString& Body, TFunction<void(int32, const crash::JsonValue&)> Done);
	void ParseAssignment(const crash::JsonValue& Json);

	FString BackendUrl;
	FString ServerKey;
	FString ServerId;
	FString PublicAddress;
	FString Region;
	int32 Port = 7777;
	FString State = TEXT("idle");
	bool bHasAssignment = false;
	FBCAssignment CurrentAssignment;
	FTSTicker::FDelegateHandle TickHandle;
	double LastHeartbeat = 0.0;
	double ExitAt = 0.0;
};
