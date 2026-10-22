// Copyright BigCar Team. All Rights Reserved.

#include "Gameplay/BCMatchGameMode.h"

#include "Arena/BCArenaBuilder.h"
#include "Arena/BCArenaProps.h"
#include "Components/BoxComponent.h"
#include "Core/BCDataSubsystem.h"
#include "Core/BCProfileSubsystem.h"
#include "Core/BCSettingsSubsystem.h"
#include "CrashCore/Impact.h"
#include "CrashCore/Json.h"
#include "Engine/World.h"
#include "Gameplay/BCAIController.h"
#include "Gameplay/BCGameState.h"
#include "Gameplay/BCPlayerController.h"
#include "Gameplay/BCPlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Base64.h"
#include "Online/BCServerSubsystem.h"
#include "Vehicle/BCVehicleDamage.h"
#include "Vehicle/BCVehicleMovement.h"
#include "Vehicle/BCVehiclePawn.h"

namespace BCMatchDetail
{
EBCMatchPhase ToPhase(crash::MatchPhase P)
{
	switch (P)
	{
	case crash::MatchPhase::Countdown:
		return EBCMatchPhase::Countdown;
	case crash::MatchPhase::Playing:
		return EBCMatchPhase::Playing;
	case crash::MatchPhase::RoundEnd:
		return EBCMatchPhase::RoundEnd;
	case crash::MatchPhase::Finished:
		return EBCMatchPhase::Finished;
	default:
		return EBCMatchPhase::Waiting;
	}
}

uint64 PairKey(const AActor* A, const AActor* B)
{
	const uint32 Ia = A ? A->GetUniqueID() : 0;
	const uint32 Ib = B ? B->GetUniqueID() : 0;
	return (static_cast<uint64>(FMath::Min(Ia, Ib)) << 32) | FMath::Max(Ia, Ib);
}

crash::ImpactBody MakeBody(const ABCVehiclePawn* V)
{
	crash::ImpactBody B;
	B.id = V->GetParticipantId();
	B.isVehicle = true;
	const FTransform Xf = V->GetActorTransform();
	B.position = BC::ToCrash(Xf.GetLocation());
	B.frame = BC::FrameFromQuat(Xf.GetRotation());
	B.velocity = BC::ToCrash(V->GetPreImpactVelocity());
	B.mass = V->GetStats().massKg;
	B.halfExtents = BC::ToCrash(V->GetHalfExtentsCm());
	B.stats = &V->GetStats();
	const crash::AbilityEffects Fx = V->GetVehicleMovement()->Ability().Effects();
	B.ramCharge = Fx.ramCharge;
	B.brace = Fx.brace;
	B.shockBumper = Fx.shockArmed;
	return B;
}
} // namespace BCMatchDetail

ABCMatchGameMode::ABCMatchGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	GameStateClass = ABCGameState::StaticClass();
	PlayerStateClass = ABCPlayerState::StaticClass();
	PlayerControllerClass = ABCPlayerController::StaticClass();
	DefaultPawnClass = nullptr; // vehicles are spawned explicitly with their build
	bUseSeamlessTravel = false;
}

void ABCMatchGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);
	ModeId = UGameplayStatics::ParseOption(Options, TEXT("Mode"));
	ArenaId = UGameplayStatics::ParseOption(Options, TEXT("Arena"));
	const FString Bots = UGameplayStatics::ParseOption(Options, TEXT("Bots"));
	BotCount = Bots.IsEmpty() ? 5 : FMath::Clamp(FCString::Atoi(*Bots), 0, 11);
	const FString Ai = UGameplayStatics::ParseOption(Options, TEXT("AI"));
	BotAi = Ai.IsEmpty() ? FString(TEXT("regular")) : Ai;
	const FString Time = UGameplayStatics::ParseOption(Options, TEXT("Time"));
	TimeLimitOverride = Time.IsEmpty() ? 0.0f : FCString::Atof(*Time);
	const UBCServerSubsystem* Server = UBCServerSubsystem::Get(this);
	bWaitingForAssignment = Server && Server->IsManaged();
}

void ABCMatchGameMode::ConfigureMatch()
{
	const UBCDataSubsystem* Data = UBCDataSubsystem::Get(this);
	if (!Data)
	{
		return;
	}
	const crash::GameDatabase& Db = Data->Db();
	Mode = Db.FindMode(BC::ToStd(ModeId));
	if (!Mode)
	{
		ModeId = TEXT("derby");
		Mode = Db.FindMode("derby");
	}
	if (ArenaId.IsEmpty() || !Db.FindArena(BC::ToStd(ArenaId)))
	{
		// Pick a suitable arena for the mode.
		for (const crash::ArenaDef& A : Db.Arenas())
		{
			if (A.hub)
			{
				continue;
			}
			const bool bRingOk = !Mode || Mode->ringOut == !A.walls;
			const bool bHillOk = !Mode || !Mode->hill || A.FindHill();
			if (bRingOk && bHillOk)
			{
				ArenaId = BC::ToFString(A.id);
				break;
			}
		}
	}
}

void ABCMatchGameMode::BeginPlay()
{
	Super::BeginPlay();
	if (!bWaitingForAssignment)
	{
		ConfigureMatch();
		StartMatch();
	}
}

void ABCMatchGameMode::StartMatch()
{
	if (bStarted)
	{
		return;
	}
	const UBCDataSubsystem* Data = UBCDataSubsystem::Get(this);
	if (!Data || !Mode)
	{
		UE_LOG(LogBigCar, Error, TEXT("Cannot start match: data or mode missing"));
		return;
	}
	bStarted = true;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Arena = GetWorld()->SpawnActor<ABCArenaBuilder>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	Arena->Build(ArenaId, true);

	ABCGameState* GS = GetGameState<ABCGameState>();
	if (GS)
	{
		GS->SetArena(Arena);
		GS->ModeId = ModeId;
		GS->ArenaId = ArenaId;
		GS->bNightArena = Arena->IsNight();
		GS->TeamCount = Mode->teams;
		GS->HillCenter = Arena->HillCenter();
		GS->HillRadius = Mode->hill ? Arena->HillRadius() : 0.0f;
	}

	Rules = MakeUnique<crash::MatchRules>(*Mode, Data->Db().Progression().score, TimeLimitOverride);
	// Players who logged in before the arena existed.
	for (const TPair<int32, TWeakObjectPtr<AController>>& P : Participants)
	{
		if (AController* C = P.Value.Get())
		{
			const ABCPlayerState* PS = C->GetPlayerState<ABCPlayerState>();
			Rules->AddParticipant(P.Key, PS ? PS->Team : P.Key, PS && PS->bBot);
			if (!C->GetPawn())
			{
				SpawnVehicleFor(C);
			}
		}
	}
	SpawnBots();
	if (bWaitingForAssignment)
	{
		// Online: wait until every assigned player joined (or time out), then count down.
		bWaitingForPlayers = true;
		WaitingSince = FPlatformTime::Seconds();
	}
	else if (!DelayCountdown())
	{
		Rules->Start(GetWorld()->GetTimeSeconds(), 3.0f);
	}
}

// ---------------------------------------------------------------------------------------------
// Players

void ABCMatchGameMode::PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage)
{
	Super::PreLogin(Options, Address, UniqueId, ErrorMessage);
	if (!ErrorMessage.IsEmpty())
	{
		return;
	}
	const UBCServerSubsystem* Server = UBCServerSubsystem::Get(this);
	if (Server && Server->IsManaged())
	{
		const FString MatchId = UGameplayStatics::ParseOption(Options, TEXT("MatchId"));
		const FString Token = UGameplayStatics::ParseOption(Options, TEXT("JoinToken"));
		const FString PlayerId = UGameplayStatics::ParseOption(Options, TEXT("PlayerId"));
		if (!Server->ValidateJoin(MatchId, PlayerId, Token))
		{
			ErrorMessage = TEXT("invalid_join_token");
		}
	}
	else if (bFinished)
	{
		ErrorMessage = TEXT("match_finished");
	}
}

FString ABCMatchGameMode::InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId, const FString& Options,
										const FString& Portal)
{
	const FString Result = Super::InitNewPlayer(NewPlayerController, UniqueId, Options, Portal);
	ABCPlayerState* PS = NewPlayerController ? NewPlayerController->GetPlayerState<ABCPlayerState>() : nullptr;
	if (!PS)
	{
		return Result;
	}
	int32 Team = -1;
	const UBCServerSubsystem* Server = UBCServerSubsystem::Get(this);
	if (Server && Server->IsManaged())
	{
		const FBCAssignedPlayer* Assigned = Server->ValidateJoin(UGameplayStatics::ParseOption(Options, TEXT("MatchId")),
																 UGameplayStatics::ParseOption(Options, TEXT("PlayerId")),
																 UGameplayStatics::ParseOption(Options, TEXT("JoinToken")));
		if (Assigned)
		{
			PS->BackendPlayerId = Assigned->PlayerId;
			PS->SetPlayerName(Assigned->Name);
			PS->Build = Assigned->Loadout;
			Team = Assigned->Team;
		}
	}
	else
	{
		// LAN/listen clients pass their build in the travel URL (base64 JSON).
		const FString Encoded = UGameplayStatics::ParseOption(Options, TEXT("Build"));
		FString Decoded;
		if (!Encoded.IsEmpty() && FBase64::Decode(Encoded, Decoded))
		{
			crash::JsonValue Json;
			crash::VehicleBuild Build;
			if (crash::ParseJson(BC::ToStd(Decoded), Json) && crash::VehicleBuild::FromJson(Json, Build))
			{
				const UBCDataSubsystem* Data = UBCDataSubsystem::Get(this);
				if (Data && crash::ValidateBuild(Data->Db(), Build).empty())
				{
					PS->Build = FBCVehicleBuild::FromCrash(Build);
				}
			}
		}
	}
	if (!PS->Build.IsValid() && NewPlayerController->IsLocalController())
	{
		PS->Build = BuildForPlayer(NewPlayerController);
	}
	if (Team < 0)
	{
		Team = TeamForNewPlayer();
	}
	PS->Team = Team;
	return Result;
}

FBCVehicleBuild ABCMatchGameMode::BuildForPlayer(APlayerController* PC) const
{
	if (const UBCProfileSubsystem* Profile = UBCProfileSubsystem::Get(this))
	{
		return Profile->SelectedBuild();
	}
	return FBCVehicleBuild();
}

int32 ABCMatchGameMode::TeamForNewPlayer() const
{
	const int32 Teams = Mode ? Mode->teams : 0;
	if (Teams < 2)
	{
		return NextParticipantId; // FFA: everyone on their own team
	}
	TArray<int32> Counts;
	Counts.Init(0, Teams);
	for (const TPair<int32, TWeakObjectPtr<AController>>& P : Participants)
	{
		const AController* C = P.Value.Get();
		const ABCPlayerState* PS = C ? C->GetPlayerState<ABCPlayerState>() : nullptr;
		if (PS && Counts.IsValidIndex(PS->Team))
		{
			++Counts[PS->Team];
		}
	}
	int32 Best = 0;
	for (int32 i = 1; i < Teams; ++i)
	{
		if (Counts[i] < Counts[Best])
		{
			Best = i;
		}
	}
	return Best;
}

int32 ABCMatchGameMode::RegisterParticipant(AController* Controller, int32 Team, bool bBot)
{
	const int32 Id = NextParticipantId++;
	Participants.Add(Id, Controller);
	if (ABCPlayerState* PS = Controller->GetPlayerState<ABCPlayerState>())
	{
		PS->ParticipantId = Id;
		PS->bBot = bBot;
		if (Mode && Mode->teams < 2 && PS->Team < 0)
		{
			PS->Team = Id;
		}
		Team = PS->Team;
	}
	if (Rules)
	{
		Rules->AddParticipant(Id, Team, bBot);
	}
	return Id;
}

void ABCMatchGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	ABCPlayerState* PS = NewPlayer->GetPlayerState<ABCPlayerState>();
	const int32 Team = PS ? PS->Team : -1;
	RegisterParticipant(NewPlayer, Team, false);
	if (bStarted)
	{
		SpawnVehicleFor(NewPlayer);
	}
}

void ABCMatchGameMode::Logout(AController* Exiting)
{
	if (const ABCPlayerState* PS = Exiting ? Exiting->GetPlayerState<ABCPlayerState>() : nullptr)
	{
		if (Rules)
		{
			Rules->SetConnected(PS->ParticipantId, false);
		}
	}
	if (APawn* Pawn = Exiting ? Exiting->GetPawn() : nullptr)
	{
		Pawn->Destroy();
	}
	Super::Logout(Exiting);
}

FTransform ABCMatchGameMode::PickSpawn(int32 Team, const FBCVehicleBuild& Build)
{
	TArray<FTransform> Spawns = Arena ? Arena->SpawnTransforms(Mode && Mode->teams >= 2 ? Team : -1) : TArray<FTransform>{FTransform::Identity};
	if (Spawns.IsEmpty())
	{
		Spawns.Add(FTransform(FVector(0.0, 0.0, 300.0)));
	}
	// Furthest from every other car.
	FTransform Best = Spawns[0];
	float BestScore = -1.0f;
	for (const FTransform& S : Spawns)
	{
		float Nearest = 1.0e9f;
		for (const TPair<int32, TWeakObjectPtr<AController>>& P : Participants)
		{
			const AController* C = P.Value.Get();
			if (const APawn* Pawn = C ? C->GetPawn() : nullptr)
			{
				Nearest = FMath::Min(Nearest, static_cast<float>(FVector::Dist2D(Pawn->GetActorLocation(), S.GetLocation())));
			}
		}
		const float Score = Nearest + FMath::FRandRange(0.0f, 300.0f);
		if (Score > BestScore)
		{
			BestScore = Score;
			Best = S;
		}
	}
	// Lift so the wheels settle onto the floor.
	float Lift = 120.0f;
	if (const UBCDataSubsystem* Data = UBCDataSubsystem::Get(this))
	{
		const crash::EffectiveStats Stats = crash::ComputeEffectiveStats(Data->Db(), Build.ToCrash());
		if (Stats.def)
		{
			Lift = crash::MakeSimSetup(Stats).bodyCenterHeight * 100.0f + 25.0f;
		}
	}
	Best.AddToTranslation(FVector(0.0f, 0.0f, Lift));
	return Best;
}

ABCVehiclePawn* ABCMatchGameMode::SpawnVehicleFor(AController* Controller)
{
	if (!Controller || !Arena)
	{
		return nullptr;
	}
	ABCPlayerState* PS = Controller->GetPlayerState<ABCPlayerState>();
	FBCVehicleBuild Build = PS ? PS->Build : FBCVehicleBuild();
	if (!Build.IsValid())
	{
		if (APlayerController* PC = Cast<APlayerController>(Controller))
		{
			Build = BuildForPlayer(PC);
		}
		if (!Build.IsValid())
		{
			Build = RandomBotBuild(1, 0.3f);
		}
		if (PS)
		{
			PS->Build = Build;
		}
	}
	const int32 Team = PS ? PS->Team : -1;
	const FTransform Spawn = PickSpawn(Team, Build);
	ABCVehiclePawn* Vehicle = GetWorld()->SpawnActorDeferred<ABCVehiclePawn>(ABCVehiclePawn::StaticClass(), Spawn, Controller, nullptr,
																			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Vehicle)
	{
		return nullptr;
	}
	Vehicle->InitVehicle(Build, Team, false);
	Vehicle->SetParticipantId(PS ? PS->ParticipantId : -1);
	Vehicle->FinishSpawning(Spawn);
	if (Arena && Arena->Def())
	{
		Vehicle->GetVehicleMovement()->SetSurfaceFriction(Arena->Def()->floorFriction);
	}
	if (APawn* Old = Controller->GetPawn())
	{
		Controller->UnPossess();
		Old->SetLifeSpan(4.0f);
	}
	Controller->Possess(Vehicle);
	if (PS)
	{
		PS->bEliminated = false;
		PS->RespawnIn = 0.0f;
	}
	if (PS)
	{
		WreckHandled.Remove(PS->ParticipantId);
	}
	return Vehicle;
}

FBCVehicleBuild ABCMatchGameMode::RandomBotBuild(int32 AroundTier, float Skill) const
{
	const UBCDataSubsystem* Data = UBCDataSubsystem::Get(this);
	if (!Data || Data->Db().Vehicles().empty())
	{
		return FBCVehicleBuild();
	}
	const crash::GameDatabase& Db = Data->Db();
	TArray<const crash::VehicleDef*> Pool;
	for (const crash::VehicleDef& V : Db.Vehicles())
	{
		if (FMath::Abs(V.tier - AroundTier) <= 1)
		{
			Pool.Add(&V);
		}
	}
	if (Pool.Num() == 0)
	{
		Pool.Add(&Db.Vehicles().front());
	}
	const crash::VehicleDef* Pick = Pool[FMath::RandRange(0, Pool.Num() - 1)];
	crash::VehicleBuild B = crash::MakeDefaultBuild(Db, Pick->id);
	const int32 MaxLevel = FMath::Clamp(FMath::RoundToInt(Skill * 5.0f), 0, crash::kMaxUpgradeLevel);
	for (int& Level : B.upgrades)
	{
		Level = FMath::RandRange(FMath::Max(0, MaxLevel - 2), MaxLevel);
	}
	// Random equipment per slot (most bots carry a front weapon).
	for (int32 Slot = 0; Slot < crash::kEquipSlotCount; ++Slot)
	{
		if (FMath::FRand() > (Slot == 0 ? 0.85f : 0.45f) * (0.4f + Skill))
		{
			continue;
		}
		TArray<const crash::EquipmentDef*> Items;
		for (const crash::EquipmentDef& E : Db.Equipment())
		{
			if (static_cast<int32>(E.slot) == Slot && E.AllowsClass(Pick->vehicleClass))
			{
				Items.Add(&E);
			}
		}
		if (Items.Num() > 0)
		{
			B.equipment[static_cast<size_t>(Slot)] = Items[FMath::RandRange(0, Items.Num() - 1)]->id;
		}
	}
	if (!Pick->colors.empty())
	{
		B.paint.clear();
	}
	return FBCVehicleBuild::FromCrash(B);
}

AController* ABCMatchGameMode::AddBot(const FBCVehicleBuild& Build, int32 Team, const FString& AiProfile, const FString& Name, bool bBoss)
{
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ABCAIController* Bot = GetWorld()->SpawnActor<ABCAIController>(Params);
	if (!Bot)
	{
		return nullptr;
	}
	Bot->SetProfile(AiProfile);
	if (ABCPlayerState* PS = Bot->GetPlayerState<ABCPlayerState>())
	{
		PS->SetPlayerName(Name);
		PS->Build = Build;
		PS->Team = Team;
		PS->bBot = true;
	}
	RegisterParticipant(Bot, Team, true);
	if (ABCVehiclePawn* Vehicle = SpawnVehicleFor(Bot))
	{
		Vehicle->SetBossCar(bBoss);
	}
	return Bot;
}

void ABCMatchGameMode::SpawnBots()
{
	const UBCDataSubsystem* Data = UBCDataSubsystem::Get(this);
	if (!Data || !Mode)
	{
		return;
	}
	if (bWaitingForAssignment)
	{
		return; // online bots are added when the wait for players ends
	}
	// Bots match the human's tier.
	int32 Tier = 1;
	const FBCVehicleBuild PlayerBuild = BuildForPlayer(nullptr);
	if (const crash::VehicleDef* V = Data->Db().FindVehicle(BC::ToStd(PlayerBuild.VehicleId)))
	{
		Tier = V->tier;
	}
	const crash::AIProfileDef* Profile = Data->Db().FindAIProfile(BC::ToStd(BotAi));
	const float Skill = Profile ? Profile->skill : 0.5f;
	const int32 Humans = Participants.Num();
	const int32 Count = FMath::Clamp(BotCount, 0, FMath::Max(0, Mode->maxPlayers - Humans));
	// 铁头, 火花, 夜枭, 狂牛, 碎骨, 钢拳 as universal character names so the literals do not depend on the compiler's source charset.
	static const TCHAR* BotNames[] = {TEXT("铁头"), TEXT("Rusty"), TEXT("火花"), TEXT("Bulldozer"), TEXT("夜枭"),
									  TEXT("Nitro"), TEXT("狂牛"), TEXT("Wrecker"), TEXT("碎骨"), TEXT("Crash Kid"),
									  TEXT("钢拳"), TEXT("Mad Max")};
	for (int32 i = 0; i < Count; ++i)
	{
		const int32 Team = Mode->teams >= 2 ? TeamForNewPlayer() : NextParticipantId;
		AddBot(RandomBotBuild(Tier, Skill), Team, BotAi, BotNames[i % UE_ARRAY_COUNT(BotNames)], false);
	}
}

// ---------------------------------------------------------------------------------------------
// Per-frame

void ABCMatchGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Online: wait for the backend assignment, then for the players.
	if (bWaitingForAssignment && !bStarted)
	{
		const UBCServerSubsystem* Server = UBCServerSubsystem::Get(this);
		if (Server && Server->HasAssignment())
		{
			ModeId = Server->Assignment().Mode;
			ArenaId = Server->Assignment().Arena;
			BotAi = Server->Assignment().BotAi;
			ConfigureMatch();
			StartMatch();
		}
		return;
	}
	if (!Rules)
	{
		return;
	}
	if (bWaitingForPlayers)
	{
		UBCServerSubsystem* Server = UBCServerSubsystem::Get(this);
		const int32 Expected = Server ? Server->Assignment().ExpectedPlayers() : 0;
		const bool bTimeout = FPlatformTime::Seconds() - WaitingSince > (Server ? Server->Assignment().MaxWaitSec : 30.0f);
		if (Participants.Num() >= Expected || bTimeout)
		{
			bWaitingForPlayers = false;
			if (Server && Server->Assignment().bFillWithBots)
			{
				const int32 Slots = Mode ? Mode->maxPlayers : 6;
				const int32 Fill = FMath::Max(0, FMath::Min(Slots, Mode && Mode->teams >= 2 ? Server->Assignment().TeamSize * Mode->teams : 8) - Participants.Num());
				for (int32 i = 0; i < Fill; ++i)
				{
					const int32 Team = Mode && Mode->teams >= 2 ? TeamForNewPlayer() : NextParticipantId;
					AddBot(RandomBotBuild(2, 0.5f), Team, BotAi, FString::Printf(TEXT("Bot %d"), i + 1), false);
				}
			}
			if (Participants.Num() == 0 && Server)
			{
				Server->ReportAbort(TEXT("no_players"));
				Server->ShutdownAfterMatch(1.0f);
				return;
			}
			Rules->Start(GetWorld()->GetTimeSeconds(), 5.0f);
			if (Server)
			{
				Server->ReportStart();
			}
		}
		SyncState();
		return;
	}

	const float Now = GetWorld()->GetTimeSeconds();
	Rules->Update(Now);
	if (Rules->TakeRoundReset())
	{
		for (const TPair<int32, TWeakObjectPtr<AController>>& P : Participants)
		{
			if (AController* C = P.Value.Get())
			{
				SpawnVehicleFor(C);
			}
		}
	}
	HandleRespawns();
	UpdateHill(DeltaSeconds);
	CheckOutOfBounds();
	TickMode(DeltaSeconds);

	// Survival scoring.
	if (Mode && Mode->win == crash::WinCondition::Survive && Rules->CanDrive())
	{
		for (const crash::MatchParticipant& P : Rules->Participants())
		{
			if (P.alive && !P.bot)
			{
				Rules->Scores().OnSurvival(P.id, DeltaSeconds);
			}
		}
	}

	// Controls follow the match phase.
	for (const TPair<int32, TWeakObjectPtr<AController>>& P : Participants)
	{
		const AController* C = P.Value.Get();
		if (ABCVehiclePawn* V = C ? Cast<ABCVehiclePawn>(C->GetPawn()) : nullptr)
		{
			V->SetControlsEnabled(Rules->CanDrive() && !V->IsWrecked());
		}
	}

	ProcessScoreEvents();
	SyncState();

	// Wrecks linger as obstacles, then disappear.
	for (int32 i = PendingDestroy.Num() - 1; i >= 0; --i)
	{
		if (Now >= PendingDestroy[i].Value)
		{
			if (ABCVehiclePawn* V = PendingDestroy[i].Key.Get())
			{
				if (!V->GetController())
				{
					V->Destroy();
				}
			}
			PendingDestroy.RemoveAt(i);
		}
	}

	if (SlowMoUntil > 0.0 && FPlatformTime::Seconds() > SlowMoUntil)
	{
		UGameplayStatics::SetGlobalTimeDilation(this, 1.0f);
		SlowMoUntil = 0.0;
	}

	if (!bFinished && Rules->Phase() == crash::MatchPhase::Finished)
	{
		bFinished = true;
		FinishedAt = Now;
		OnMatchFinished();
	}
}

void ABCMatchGameMode::HandleRespawns()
{
	const float Now = GetWorld()->GetTimeSeconds();
	for (const int32 Id : Rules->TakeRespawns(Now))
	{
		if (!AllowRespawn(Id))
		{
			continue;
		}
		const TWeakObjectPtr<AController>* C = Participants.Find(Id);
		if (C && C->IsValid())
		{
			if (SpawnVehicleFor(C->Get()))
			{
				Rules->OnRespawned(Id);
			}
		}
	}
	// Respawn countdown for the HUD.
	for (const crash::MatchParticipant& P : Rules->Participants())
	{
		if (ABCPlayerState* PS = StateOf(P.id))
		{
			PS->RespawnIn = (!P.alive && P.respawnAt > 0.0f) ? FMath::Max(0.0f, P.respawnAt - Now) : 0.0f;
			PS->bEliminated = P.eliminated;
		}
	}
}

void ABCMatchGameMode::UpdateHill(float DeltaSeconds)
{
	if (!Mode || !Mode->hill || !Arena || !Arena->HasHill())
	{
		return;
	}
	const FVector Hill = Arena->HillCenter();
	const float Radius = Arena->HillRadius();
	std::vector<int> Inside;
	for (const TPair<int32, TWeakObjectPtr<AController>>& P : Participants)
	{
		const AController* C = P.Value.Get();
		const ABCVehiclePawn* V = C ? Cast<ABCVehiclePawn>(C->GetPawn()) : nullptr;
		if (V && !V->IsWrecked() && FVector::Dist2D(V->GetActorLocation(), Hill) < Radius &&
			FMath::Abs(V->GetActorLocation().Z - Hill.Z) < 600.0f)
		{
			Inside.push_back(P.Key);
		}
	}
	Rules->OnHillOccupants(Inside, DeltaSeconds);
}

void ABCMatchGameMode::CheckOutOfBounds()
{
	if (!Arena)
	{
		return;
	}
	const float FloorZ = Arena->FloorZ();
	const float KillZ = Arena->KillZ();
	for (const TPair<int32, TWeakObjectPtr<AController>>& P : Participants)
	{
		const AController* C = P.Value.Get();
		ABCVehiclePawn* V = C ? Cast<ABCVehiclePawn>(C->GetPawn()) : nullptr;
		if (!V || V->IsWrecked() || WreckHandled.Contains(P.Key))
		{
			continue;
		}
		const FVector Loc = V->GetActorLocation();
		const bool bFell = Loc.Z < KillZ || (Loc.Z < FloorZ - 400.0f && (!Arena->IsInsideFloor(Loc) || Arena->IsOverPit(Loc)));
		if (bFell)
		{
			NotifyVehicleWrecked(V, true);
		}
	}
}

void ABCMatchGameMode::ProcessScoreEvents()
{
	const UBCDataSubsystem* Data = UBCDataSubsystem::Get(this);
	for (const crash::ScoreEvent& E : Rules->Scores().TakeEvents())
	{
		const TWeakObjectPtr<AController>* C = Participants.Find(E.playerId);
		ABCPlayerController* PC = C ? Cast<ABCPlayerController>(C->Get()) : nullptr;
		if (PC)
		{
			FBCScorePopup Popup;
			Popup.Type = static_cast<uint8>(E.type);
			Popup.Kind = static_cast<uint8>(E.kind);
			Popup.Points = E.points;
			Popup.Multiplier = E.multiplier;
			Popup.Other = NameOf(E.otherId);
			PC->ClientScorePopup(Popup);
		}
		if (ABCPlayerState* PS = StateOf(E.playerId); PS && E.type == crash::ScoreEventType::Impact)
		{
			if (PS->ImpactCounts.Num() < crash::kImpactKindCount)
			{
				PS->ImpactCounts.SetNumZeroed(crash::kImpactKindCount);
			}
			++PS->ImpactCounts[static_cast<int32>(E.kind)];
		}
	}
	(void)Data;
	for (const crash::ParticipantStats& S : Rules->Scores().All())
	{
		if (ABCPlayerState* PS = StateOf(S.id))
		{
			PS->MatchScore = S.Score();
			PS->Takedowns = S.takedowns;
			PS->Assists = S.assists;
			PS->Wrecked = S.wrecked;
			PS->DamageDealt = FMath::RoundToInt(S.damageDealt);
			PS->SetScore(static_cast<float>(S.Score()));
		}
	}
}

void ABCMatchGameMode::SyncState()
{
	ABCGameState* GS = GetGameState<ABCGameState>();
	if (!GS || !Rules)
	{
		return;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	GS->Phase = bWaitingForPlayers ? EBCMatchPhase::Waiting : BCMatchDetail::ToPhase(Rules->Phase());
	GS->TimeRemaining = Rules->TimeRemaining(Now);
	GS->CountdownRemaining = Rules->CountdownRemaining(Now);
	GS->Round = Rules->Round();
	const int32 Teams = Mode ? Mode->teams : 0;
	GS->TeamScores.SetNum(Teams);
	GS->RoundWins.SetNum(Teams);
	for (int32 T = 0; T < Teams; ++T)
	{
		GS->TeamScores[T] = Rules->Scores().TeamScore(T);
		GS->RoundWins[T] = Rules->RoundWins(T);
	}
	GS->HillOwnerTeam = Rules->HillOwnerTeam();
	GS->bHillContested = Rules->HillContested();
	GS->BountyParticipant = Rules->BountyTarget();
}

// ---------------------------------------------------------------------------------------------
// Impacts

void ABCMatchGameMode::HandleVehicleHit(ABCVehiclePawn* Vehicle, AActor* Other, UPrimitiveComponent* OtherComp, const FHitResult& Hit,
										const FVector& NormalImpulse)
{
	if (!Rules || bFinished || !Vehicle || Vehicle->IsWrecked() || Rules->Phase() == crash::MatchPhase::Countdown)
	{
		return;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	const uint64 Key = BCMatchDetail::PairKey(Vehicle, Other);
	if (const float* Until = PairCooldowns.Find(Key); Until && *Until > Now)
	{
		return;
	}
	const FVector Center = Vehicle->GetActorLocation();
	const FVector Point = Hit.ImpactPoint;

	// ---- Vehicle vs vehicle.
	if (ABCVehiclePawn* OtherVehicle = Cast<ABCVehiclePawn>(Other))
	{
		if (OtherVehicle->IsWrecked() && Vehicle->IsWrecked())
		{
			return;
		}
		FVector N = Hit.ImpactNormal.GetSafeNormal();
		if (FVector::DotProduct(N, OtherVehicle->GetActorLocation() - Center) < 0.0)
		{
			N = -N;
		}
		crash::ImpactInput In;
		In.a = BCMatchDetail::MakeBody(Vehicle);
		In.b = BCMatchDetail::MakeBody(OtherVehicle);
		In.contactPoint = BC::ToCrash(Point);
		In.normal = BC::DirToCrash(N);
		const crash::ImpactResult R = crash::ResolveImpact(In);
		if (R.attackerId >= 0)
		{
			Rules->Scores().OnTouched(R.attackerId, R.victimId, Now);
		}
		if (!R.valid)
		{
			return;
		}
		PairCooldowns.Add(Key, Now + 0.3f);
		// Friendly fire: half damage, no points (handled by the score keeper).
		const bool bSameTeam = Mode && Mode->teams >= 2 && Vehicle->GetTeam() == OtherVehicle->GetTeam();
		auto Apply = [&](ABCVehiclePawn* Target, const crash::ImpactSide& Side, const FVector& PushDir) {
			FBCImpactApply A;
			A.Damage = Side.damage;
			if (bSameTeam)
			{
				A.Damage *= 0.5f;
			}
			A.ImpulseUE = BC::ImpulseToUE(Side.extraImpulse);
			A.WorldPoint = Point;
			A.DentDirWorld = PushDir;
			A.Severity = R.severity;
			A.Kind = static_cast<uint8>(R.kind);
			A.bVehicle = true;
			return Target->ServerApplyImpactResult(A);
		};
		const crash::DamageEvent EA = Apply(Vehicle, R.a, -N);
		const crash::DamageEvent EB = Apply(OtherVehicle, R.b, N);
		if (In.a.shockBumper)
		{
			Vehicle->GetVehicleMovement()->Ability().ConsumeShock();
		}
		if (In.b.shockBumper)
		{
			OtherVehicle->GetVehicleMovement()->Ability().ConsumeShock();
		}
		Rules->Scores().OnImpact(R, Now);

		// Big hits involving a human get a crash cam (and slow motion offline).
		if (R.severity > 0.55f)
		{
			for (ABCVehiclePawn* V : {Vehicle, OtherVehicle})
			{
				if (ABCPlayerController* PC = Cast<ABCPlayerController>(V->GetController()))
				{
					PC->ClientCrashCam(V == Vehicle ? OtherVehicle : Vehicle, 0.8f);
				}
			}
			if (IsOfflineMatch())
			{
				SlowMotion(0.45f, 0.35f);
			}
		}
		if (EB.becameWrecked)
		{
			NotifyVehicleWrecked(OtherVehicle, false);
		}
		if (EA.becameWrecked)
		{
			NotifyVehicleWrecked(Vehicle, false);
		}
		return;
	}

	// ---- Vehicle vs props / world.
	FVector N = -Hit.ImpactNormal.GetSafeNormal();
	if (FVector::DotProduct(N, Point - Center) < 0.0)
	{
		N = -N;
	}
	const float Closing = FMath::Max(0.0f, static_cast<float>(FVector::DotProduct(Vehicle->GetPreImpactVelocity(), N))) / 100.0f;
	float Scale = 1.0f;
	bool bSoft = OtherComp && OtherComp->ComponentHasTag(TEXT("Soft"));
	if (Other && Other->ActorHasTag(TEXT("Soft")))
	{
		bSoft = true;
	}
	if (ABCBreakableProp* Prop = Cast<ABCBreakableProp>(Other))
	{
		Prop->TakeHit(Vehicle, Closing, Point);
		Scale = Prop->CarDamageScale();
	}
	else if (Other && Other->ActorHasTag(TEXT("Hazard")))
	{
		if (const ABCHazardActor* Hazard = Cast<ABCHazardActor>(Other))
		{
			Scale = Hazard->DamageMultiplier();
		}
		// Hazards knock cars away regardless of speed.
		Vehicle->ServerAddImpulse(-N * Vehicle->GetStats().massKg * 600.0f + FVector(0, 0, Vehicle->GetStats().massKg * 300.0f), Point);
	}
	// Landing on the floor: the suspension takes normal landings; only brutal ones hurt.
	const bool bLanding = N.Z < -0.7f;
	if (bLanding && Closing < 9.0f)
	{
		return;
	}
	crash::ImpactInput In;
	In.a = BCMatchDetail::MakeBody(Vehicle);
	In.b.isVehicle = false;
	In.b.softWorld = bSoft;
	In.contactPoint = BC::ToCrash(Point);
	In.normal = BC::DirToCrash(N);
	crash::ImpactResult R = crash::ResolveImpact(In);
	if (!R.valid)
	{
		return;
	}
	PairCooldowns.Add(Key, Now + 0.3f);
	R.a.damage *= Scale * (bLanding ? 0.35f : 1.0f);
	FBCImpactApply A;
	A.Damage = R.a.damage;
	A.WorldPoint = Point;
	A.DentDirWorld = -N;
	A.Severity = R.severity * Scale;
	A.bVehicle = false;
	const crash::DamageEvent E = Vehicle->ServerApplyImpactResult(A);
	Rules->Scores().OnDamageTaken(Vehicle->GetParticipantId(), R.a.damage.Total());
	if (E.becameWrecked)
	{
		NotifyVehicleWrecked(Vehicle, false);
	}
	(void)NormalImpulse;
}

void ABCMatchGameMode::Explosion(const FVector& Center, float RadiusCm, float PushMs, float Damage)
{
	for (const TPair<int32, TWeakObjectPtr<AController>>& P : Participants)
	{
		const AController* C = P.Value.Get();
		ABCVehiclePawn* V = C ? Cast<ABCVehiclePawn>(C->GetPawn()) : nullptr;
		if (!V || V->IsWrecked())
		{
			continue;
		}
		const FVector Delta = V->GetActorLocation() - Center;
		const float Dist = Delta.Size();
		if (Dist > RadiusCm)
		{
			continue;
		}
		const float Falloff = 1.0f - Dist / RadiusCm;
		const FVector Dir = (Delta.GetSafeNormal() + FVector(0, 0, 0.6f)).GetSafeNormal();
		FBCImpactApply A;
		// Spread over the zones facing the blast.
		const FVector Local = V->GetActorTransform().InverseTransformVectorNoScale(-Dir);
		const int32 Zone = FMath::Abs(Local.X) > FMath::Abs(Local.Y) ? (Local.X > 0 ? 0 : 1) : (Local.Y > 0 ? 3 : 2);
		A.Damage.zone[static_cast<size_t>(Zone)] = Damage * Falloff;
		A.Damage.zone[static_cast<size_t>(crash::Zone::Bottom)] = Damage * Falloff * 0.5f;
		A.ImpulseUE = Dir * V->GetStats().massKg * PushMs * Falloff * 100.0f;
		A.WorldPoint = V->GetActorLocation() - Dir * 100.0f;
		A.DentDirWorld = Dir;
		A.Severity = Falloff;
		A.bVehicle = false;
		if (V->ServerApplyImpactResult(A).becameWrecked)
		{
			NotifyVehicleWrecked(V, false);
		}
	}
}

void ABCMatchGameMode::NotifyAirtime(ABCVehiclePawn* Vehicle, float Seconds)
{
	if (Rules && Vehicle && Rules->CanDrive())
	{
		Rules->Scores().OnAirtime(Vehicle->GetParticipantId(), Seconds);
	}
}

void ABCMatchGameMode::NotifyPropDestroyed(ABCBreakableProp* Prop, ABCVehiclePawn* By)
{
	if (Rules && By)
	{
		Rules->Scores().OnPropDestroyed(By->GetParticipantId(), GetWorld()->GetTimeSeconds());
	}
}

void ABCMatchGameMode::NotifyVehicleWrecked(ABCVehiclePawn* Vehicle, bool bRingOut)
{
	if (!Rules || !Vehicle)
	{
		return;
	}
	const int32 Id = Vehicle->GetParticipantId();
	if (WreckHandled.Contains(Id))
	{
		return;
	}
	WreckHandled.Add(Id);
	Vehicle->ServerWreck(bRingOut);
	const crash::TakedownInfo Info = Rules->OnWrecked(Id, GetWorld()->GetTimeSeconds(), bRingOut);
	PendingDestroy.Add(TPair<TWeakObjectPtr<ABCVehiclePawn>, float>(Vehicle, GetWorld()->GetTimeSeconds() + 6.0f));

	if (ABCGameState* GS = GetGameState<ABCGameState>())
	{
		FBCFeedEntry Entry;
		Entry.Attacker = Info.killer >= 0 ? NameOf(Info.killer) : FString();
		Entry.Victim = NameOf(Id);
		Entry.Kind = static_cast<uint8>(Info.finishingKind);
		Entry.bTakedown = Info.killer >= 0;
		Entry.bRingOut = bRingOut;
		const ABCPlayerState* KillerPS = StateOf(Info.killer);
		const ABCPlayerState* VictimPS = StateOf(Id);
		Entry.AttackerTeam = KillerPS ? KillerPS->Team : -1;
		Entry.VictimTeam = VictimPS ? VictimPS->Team : -1;
		GS->MulticastFeed(Entry);
	}
	if (Info.killer >= 0)
	{
		if (ABCVehiclePawn* Killer = VehicleOf(Info.killer))
		{
			if (ABCPlayerController* PC = Cast<ABCPlayerController>(Killer->GetController()))
			{
				PC->ClientCrashCam(Vehicle, 1.2f);
			}
		}
		if (IsOfflineMatch())
		{
			SlowMotion(0.7f, 0.3f);
		}
	}
	OnWreckedHook(Vehicle, Info);
}

void ABCMatchGameMode::SlowMotion(float RealSeconds, float Dilation)
{
	const UBCSettingsSubsystem* Settings = UBCSettingsSubsystem::Get(this);
	if (Settings && !Settings->Gameplay().slowMotion)
	{
		return;
	}
	UGameplayStatics::SetGlobalTimeDilation(this, Dilation);
	SlowMoUntil = FPlatformTime::Seconds() + RealSeconds;
}

// ---------------------------------------------------------------------------------------------
// End of match

bool ABCMatchGameMode::IsOfflineMatch() const
{
	return GetNetMode() == NM_Standalone;
}

void ABCMatchGameMode::RequestRestart(APlayerController* PC)
{
	if (GetNetMode() == NM_Standalone)
	{
		GetWorld()->ServerTravel(TEXT("?Restart"), true);
	}
}

ABCVehiclePawn* ABCMatchGameMode::VehicleOf(int32 ParticipantId) const
{
	const TWeakObjectPtr<AController>* C = Participants.Find(ParticipantId);
	return C && C->IsValid() ? Cast<ABCVehiclePawn>(C->Get()->GetPawn()) : nullptr;
}

ABCPlayerState* ABCMatchGameMode::StateOf(int32 ParticipantId) const
{
	const TWeakObjectPtr<AController>* C = Participants.Find(ParticipantId);
	return C && C->IsValid() ? C->Get()->GetPlayerState<ABCPlayerState>() : nullptr;
}

FString ABCMatchGameMode::NameOf(int32 ParticipantId) const
{
	if (const ABCPlayerState* PS = StateOf(ParticipantId))
	{
		return PS->GetPlayerName();
	}
	return FString();
}

void ABCMatchGameMode::OnMatchFinished()
{
	FinishAndReport();
}

void ABCMatchGameMode::FinishAndReport()
{
	ABCGameState* GS = GetGameState<ABCGameState>();
	const crash::MatchOutcome& Outcome = Rules->Outcome();
	if (GS)
	{
		GS->WinnerTeam = Outcome.winnerTeam;
		GS->Results.Reset();
		for (const int32 Id : Outcome.placements)
		{
			const crash::ParticipantStats* S = Rules->Scores().Get(Id);
			const ABCPlayerState* PS = StateOf(Id);
			FBCResultRow Row;
			Row.Name = NameOf(Id);
			Row.Team = PS ? PS->Team : -1;
			Row.Score = S ? S->Score() : 0;
			Row.Takedowns = S ? S->takedowns : 0;
			Row.Assists = S ? S->assists : 0;
			Row.Wrecked = S ? S->wrecked : 0;
			Row.Damage = S ? FMath::RoundToInt(S->damageDealt) : 0;
			Row.bBot = PS && PS->bBot;
			GS->Results.Add(Row);
		}
		SyncState();
	}

	UBCServerSubsystem* Server = UBCServerSubsystem::Get(this);
	if (Server && Server->IsManaged())
	{
		crash::JsonValue Result = crash::JsonValue::MakeObject();
		Result.Set("durationSec", crash::JsonValue(GetWorld()->GetTimeSeconds()));
		Result.Set("winnerTeam", crash::JsonValue(Outcome.winnerTeam));
		crash::JsonValue Players = crash::JsonValue::MakeArray();
		int32 Placement = 1;
		for (const int32 Id : Outcome.placements)
		{
			const ABCPlayerState* PS = StateOf(Id);
			const crash::ParticipantStats* S = Rules->Scores().Get(Id);
			const int32 Place = Placement++;
			if (!PS || PS->bBot || PS->BackendPlayerId.IsEmpty() || !S)
			{
				continue;
			}
			crash::JsonValue P = crash::JsonValue::MakeObject();
			P.Set("playerId", crash::JsonValue(BC::ToStd(PS->BackendPlayerId)));
			P.Set("team", crash::JsonValue(PS->Team));
			P.Set("score", crash::JsonValue(S->Score()));
			P.Set("takedowns", crash::JsonValue(S->takedowns));
			P.Set("assists", crash::JsonValue(S->assists));
			P.Set("wrecked", crash::JsonValue(S->wrecked));
			P.Set("damageDealt", crash::JsonValue(FMath::RoundToInt(S->damageDealt)));
			P.Set("placement", crash::JsonValue(Place));
			const crash::MatchParticipant* MP = Rules->Find(Id);
			P.Set("disconnected", crash::JsonValue(MP && !MP->connected));
			crash::JsonValue Impacts = crash::JsonValue::MakeObject();
			for (int32 k = 1; k < crash::kImpactKindCount; ++k)
			{
				if (S->impacts[static_cast<size_t>(k)] > 0)
				{
					Impacts.Set(crash::ToString(static_cast<crash::ImpactKind>(k)), crash::JsonValue(S->impacts[static_cast<size_t>(k)]));
				}
			}
			P.Set("impacts", std::move(Impacts));
			Players.Push(std::move(P));
		}
		Result.Set("players", std::move(Players));
		TWeakObjectPtr<ABCMatchGameMode> WeakThis(this);
		Server->ReportResult(Result, [WeakThis](bool bOk, const crash::JsonValue& Response) {
			if (!WeakThis.IsValid())
			{
				return;
			}
			TMap<FString, crash::JsonValue> Rewards;
			for (const crash::JsonValue& R : Response["rewards"].Items())
			{
				Rewards.Add(BC::ToFString(R["playerId"].AsString()), R);
			}
			WeakThis->SendPersonalResults(Rewards);
		});
		Server->ShutdownAfterMatch(25.0f);
		return;
	}
	SendPersonalResults({});
}

void ABCMatchGameMode::SendPersonalResults(const TMap<FString, crash::JsonValue>& BackendRewards)
{
	const crash::MatchOutcome& Outcome = Rules->Outcome();
	for (const TPair<int32, TWeakObjectPtr<AController>>& P : Participants)
	{
		ABCPlayerController* PC = Cast<ABCPlayerController>(P.Value.Get());
		const ABCPlayerState* PS = PC ? PC->GetPlayerState<ABCPlayerState>() : nullptr;
		if (!PC || !PS)
		{
			continue;
		}
		FBCPersonalResult R;
		R.bWon = Outcome.winnerTeam >= 0 && PS->Team == Outcome.winnerTeam;
		R.Placement = Outcome.placements.empty() ? 0 : 1 + static_cast<int32>(std::find(Outcome.placements.begin(), Outcome.placements.end(), P.Key) - Outcome.placements.begin());
		if (const crash::JsonValue* Reward = BackendRewards.Find(PS->BackendPlayerId))
		{
			R.bOnline = true;
			R.Credits = (*Reward)["credits"].AsInt64(0);
			R.Xp = (*Reward)["xp"].AsInt64(0);
			R.LevelsGained = (*Reward)["levelAfter"].AsInt(0) - (*Reward)["levelBefore"].AsInt(0);
			R.RatingBefore = (*Reward)["ratingBefore"].AsFloat(0.0f);
			R.RatingAfter = (*Reward)["ratingAfter"].AsFloat(0.0f);
		}
		else if (const UBCDataSubsystem* Data = UBCDataSubsystem::Get(this))
		{
			// Offline / LAN: the client grants these to its local profile when the results arrive.
			const crash::MatchReward LocalReward = crash::ComputeMatchReward(Data->Db().Progression(), PS->MatchScore, R.bWon, false);
			R.Credits = LocalReward.credits;
			R.Xp = LocalReward.xp;
		}
		R.Stars = TArray<bool>();
		PC->ClientShowResults(R);
	}
}
