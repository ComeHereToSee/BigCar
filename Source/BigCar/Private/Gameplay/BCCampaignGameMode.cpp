// Copyright BigCar Team. All Rights Reserved.

#include "Gameplay/BCCampaignGameMode.h"

#include "Arena/BCArenaBuilder.h"
#include "Arena/BCArenaProps.h"
#include "Core/BCDataSubsystem.h"
#include "Core/BCProfileSubsystem.h"
#include "Engine/World.h"
#include "Gameplay/BCAIController.h"
#include "Gameplay/BCGameState.h"
#include "Gameplay/BCPlayerController.h"
#include "Gameplay/BCPlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "Vehicle/BCVehicleDamage.h"
#include "Vehicle/BCVehiclePawn.h"

void ABCCampaignGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);
	LevelId = UGameplayStatics::ParseOption(Options, TEXT("Level"));
	HubChapter = UGameplayStatics::ParseOption(Options, TEXT("Hub"));
	BotCount = 0;
}

void ABCCampaignGameMode::ConfigureMatch()
{
	const UBCDataSubsystem* Data = UBCDataSubsystem::Get(this);
	if (!Data)
	{
		return;
	}
	const crash::GameDatabase& Db = Data->Db();
	if (!HubChapter.IsEmpty())
	{
		const crash::ChapterDef* Chapter = Db.FindChapter(BC::ToStd(HubChapter));
		ModeId = TEXT("explore");
		ArenaId = Chapter ? BC::ToFString(Chapter->hub) : FString();
		TimeLimitOverride = 0.0f;
	}
	else
	{
		LevelDef = Db.FindLevel(BC::ToStd(LevelId));
		if (!LevelDef && !Db.Campaign().chapters.empty())
		{
			LevelDef = &Db.Campaign().chapters.front().levels.front();
		}
		if (LevelDef)
		{
			ModeId = BC::ToFString(LevelDef->mode);
			ArenaId = BC::ToFString(LevelDef->arena);
			TimeLimitOverride = LevelDef->timeLimit;
		}
	}
	Super::ConfigureMatch();
	if (ABCGameState* GS = GetGameState<ABCGameState>())
	{
		GS->LevelId = LevelDef ? BC::ToFString(LevelDef->id) : FString();
		GS->ObjectiveText = LevelDef ? BC::ToFString(LevelDef->description) : FString(TEXT("ui.hub.objective"));
	}
	// Intro dialogue holds the countdown (only for levels with lines).
	bWaitingForStory = LevelDef && !LevelDef->intro.empty();
}

FBCVehicleBuild ABCCampaignGameMode::BuildForPlayer(APlayerController* PC) const
{
	const UBCDataSubsystem* Data = UBCDataSubsystem::Get(this);
	const UBCProfileSubsystem* Profile = UBCProfileSubsystem::Get(this);
	if (!Data || !Profile)
	{
		return FBCVehicleBuild();
	}
	const crash::GameDatabase& Db = Data->Db();
	if (LevelDef && !LevelDef->loaner.empty())
	{
		return FBCVehicleBuild::FromCrash(crash::MakeDefaultBuild(Db, LevelDef->loaner));
	}
	const crash::Profile& Local = Profile->Local();
	const crash::VehicleBuild* Selected = Local.SelectedBuild();
	auto TierOk = [&Db, this](const crash::VehicleBuild& B) {
		const crash::VehicleDef* V = Db.FindVehicle(B.vehicleId);
		return V && (!LevelDef || LevelDef->maxTier <= 0 || V->tier <= LevelDef->maxTier);
	};
	if (Selected && TierOk(*Selected))
	{
		return FBCVehicleBuild::FromCrash(*Selected);
	}
	// Best owned car that satisfies the tier cap.
	const crash::VehicleBuild* Best = nullptr;
	int32 BestCr = -1;
	for (const crash::VehicleBuild& B : Local.vehicles)
	{
		if (TierOk(B))
		{
			const int32 Cr = crash::ComputeCombatRating(Db, B);
			if (Cr > BestCr)
			{
				BestCr = Cr;
				Best = &B;
			}
		}
	}
	if (Best)
	{
		return FBCVehicleBuild::FromCrash(*Best);
	}
	const std::vector<const crash::VehicleDef*> Starters = Db.StarterVehicles();
	return Starters.empty() ? FBCVehicleBuild() : FBCVehicleBuild::FromCrash(crash::MakeDefaultBuild(Db, Starters.front()->id));
}

void ABCCampaignGameMode::SpawnBots()
{
	if (!LevelDef)
	{
		return;
	}
	const UBCDataSubsystem* Data = UBCDataSubsystem::Get(this);
	auto Spawn = [this, Data](const crash::CampaignCarDef& Car) {
		FString Name;
		if (!Car.name.empty())
		{
			Name = Data->Str(BC::ToFString(Car.name));
		}
		else if (const crash::VehicleDef* V = Data->Db().FindVehicle(Car.vehicle))
		{
			Name = Data->VehicleName(*V).ToString();
		}
		AddBot(FBCVehicleBuild::FromCrash(crash::MakeCampaignBuild(Car)), Car.team, BC::ToFString(Car.ai), Name, Car.boss);
	};
	for (const crash::CampaignCarDef& Ally : LevelDef->allies)
	{
		Spawn(Ally);
	}
	for (const crash::CampaignCarDef& Opponent : LevelDef->opponents)
	{
		Spawn(Opponent);
	}
}

void ABCCampaignGameMode::StartMatch()
{
	Super::StartMatch();
	SpawnCollectibles();
	StartTime = GetWorld()->GetTimeSeconds();
	NextWaveAt = StartTime + 25.0f;
	if (bWaitingForStory)
	{
		TArray<FString> Lines;
		for (const std::string& Key : LevelDef->intro)
		{
			Lines.Add(BC::ToFString(Key));
		}
		for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		{
			if (ABCPlayerController* PC = Cast<ABCPlayerController>(It->Get()))
			{
				PC->ClientStory(Lines, true);
			}
		}
	}
}

void ABCCampaignGameMode::PlayerReady(APlayerController* PC)
{
	if (bWaitingForStory && Rules)
	{
		bWaitingForStory = false;
		Rules->Start(GetWorld()->GetTimeSeconds(), 3.0f);
		StartTime = GetWorld()->GetTimeSeconds() + 3.0f;
		NextWaveAt = StartTime + 25.0f;
		if (UBCProfileSubsystem* Profile = UBCProfileSubsystem::Get(this))
		{
			for (const std::string& Key : LevelDef->intro)
			{
				Profile->MarkStorySeen(BC::ToFString(Key));
			}
		}
	}
}

void ABCCampaignGameMode::SpawnCollectibles()
{
	if (!Arena || !Arena->Def())
	{
		return;
	}
	const crash::ArenaDef* Def = Arena->Def();
	const UBCProfileSubsystem* Profile = UBCProfileSubsystem::Get(this);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (IsHub())
	{
		const crash::HubProgress* Progress = nullptr;
		if (Profile)
		{
			const auto It = Profile->Local().campaign.hubs.find(BC::ToStd(HubChapter));
			Progress = It != Profile->Local().campaign.hubs.end() ? &It->second : nullptr;
		}
		for (int32 i = 0; i < static_cast<int32>(Def->collectibles.size()); ++i)
		{
			if (Progress && std::find(Progress->collectibles.begin(), Progress->collectibles.end(), i) != Progress->collectibles.end())
			{
				continue;
			}
			if (ABCCollectibleActor* C = GetWorld()->SpawnActor<ABCCollectibleActor>(Arena->Collectible(i), FRotator::ZeroRotator, Params))
			{
				C->Index = i;
				C->bHub = true;
			}
		}
		for (const crash::SecretDef& Secret : Def->secrets)
		{
			if (Progress && std::find(Progress->secrets.begin(), Progress->secrets.end(), Secret.id) != Progress->secrets.end())
			{
				continue;
			}
			if (ABCSecretZone* Z = GetWorld()->SpawnActor<ABCSecretZone>(BC::ToUE(Secret.pos) + FVector(0, 0, Arena->FloorZ()), FRotator::ZeroRotator, Params))
			{
				Z->Setup(BC::ToFString(Secret.id), Secret.radius * 100.0f);
			}
		}
		if (ABCGameState* GS = GetGameState<ABCGameState>())
		{
			GS->CollectiblesTotal = static_cast<int32>(Def->collectibles.size() + Def->secrets.size());
			GS->CollectiblesFound = Progress ? static_cast<int32>(Progress->collectibles.size() + Progress->secrets.size()) : 0;
		}
		return;
	}
	if (!LevelDef)
	{
		return;
	}
	const crash::LevelProgress* Progress = nullptr;
	if (Profile)
	{
		const auto It = Profile->Local().campaign.levels.find(LevelDef->id);
		Progress = It != Profile->Local().campaign.levels.end() ? &It->second : nullptr;
	}
	int32 AlreadyFound = 0;
	for (int32 i = 0; i < LevelDef->collectibles; ++i)
	{
		const bool bFound = Progress && i < static_cast<int32>(Progress->collectibles.size()) && Progress->collectibles[static_cast<size_t>(i)];
		if (bFound)
		{
			++AlreadyFound;
			continue;
		}
		if (ABCCollectibleActor* C = GetWorld()->SpawnActor<ABCCollectibleActor>(Arena->Collectible(i), FRotator::ZeroRotator, Params))
		{
			C->Index = i;
		}
	}
	if (ABCGameState* GS = GetGameState<ABCGameState>())
	{
		GS->CollectiblesTotal = LevelDef->collectibles;
		GS->CollectiblesFound = AlreadyFound;
	}
}

void ABCCampaignGameMode::OnCollectiblePicked(ABCCollectibleActor* Collectible, ABCVehiclePawn* By)
{
	if (!Collectible)
	{
		return;
	}
	if (Collectible->bHub)
	{
		if (UBCProfileSubsystem* Profile = UBCProfileSubsystem::Get(this))
		{
			Profile->MarkHubCollectible(HubChapter, Collectible->Index);
		}
	}
	else
	{
		CollectedThisRun.AddUnique(Collectible->Index);
	}
	if (Rules && By)
	{
		Rules->Scores().OnCollectible(By->GetParticipantId(), GetWorld()->GetTimeSeconds());
	}
	if (ABCGameState* GS = GetGameState<ABCGameState>())
	{
		++GS->CollectiblesFound;
		if (ABCPlayerController* PC = By ? Cast<ABCPlayerController>(By->GetController()) : nullptr)
		{
			PC->ClientToast(TEXT("ui.campaign.collectible_found"), FString::Printf(TEXT("%d/%d"), GS->CollectiblesFound, GS->CollectiblesTotal));
		}
	}
}

void ABCCampaignGameMode::OnSecretFound(ABCSecretZone* Secret, ABCVehiclePawn* By)
{
	if (!Secret)
	{
		return;
	}
	if (UBCProfileSubsystem* Profile = UBCProfileSubsystem::Get(this))
	{
		Profile->MarkSecret(HubChapter, Secret->SecretId);
	}
	if (ABCGameState* GS = GetGameState<ABCGameState>())
	{
		++GS->CollectiblesFound;
		if (ABCPlayerController* PC = By ? Cast<ABCPlayerController>(By->GetController()) : nullptr)
		{
			PC->ClientToast(TEXT("ui.campaign.secret_found"), FString::Printf(TEXT("%d/%d"), GS->CollectiblesFound, GS->CollectiblesTotal));
		}
	}
}

void ABCCampaignGameMode::OnWreckedHook(ABCVehiclePawn* Victim, const crash::TakedownInfo& Info)
{
	if (!Victim)
	{
		return;
	}
	if (Victim->IsBossCar())
	{
		bBossDefeated = true;
	}
	// The player getting wrecked in a no-respawn level ends the attempt right away.
	if (Victim->IsPlayerControlled() && Mode && !Mode->respawn && Rules && Rules->Phase() == crash::MatchPhase::Playing)
	{
		bPlayerOut = true;
	}
}

void ABCCampaignGameMode::SpawnWave()
{
	if (!LevelDef || LevelDef->opponents.empty())
	{
		return;
	}
	int32 Alive = 0;
	for (const crash::MatchParticipant& P : Rules->Participants())
	{
		Alive += (P.bot && P.alive && P.team != 0) ? 1 : 0;
	}
	if (Alive >= 6)
	{
		return;
	}
	const crash::CampaignCarDef& Car = LevelDef->opponents[static_cast<size_t>(WaveIndex % static_cast<int32>(LevelDef->opponents.size()))];
	crash::VehicleBuild Build = crash::MakeCampaignBuild(Car);
	for (int& Level : Build.upgrades)
	{
		Level = FMath::Min(Level + WaveIndex / 3, crash::kMaxUpgradeLevel); // waves get tougher
	}
	AddBot(FBCVehicleBuild::FromCrash(Build), 1, BC::ToFString(Car.ai), FString::Printf(TEXT("#%d"), WaveIndex + 1), false);
	++WaveIndex;
}

bool ABCCampaignGameMode::PrimaryObjectiveMet() const
{
	if (!LevelDef || !Rules)
	{
		return false;
	}
	const crash::LevelRunResult Run = BuildRun();
	return crash::EvaluateObjective(LevelDef->objective, Run);
}

void ABCCampaignGameMode::TickMode(float DeltaSeconds)
{
	if (IsHub() || !LevelDef || !Rules || Rules->Phase() != crash::MatchPhase::Playing)
	{
		return;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	if (Mode && Mode->win == crash::WinCondition::Survive && Now >= NextWaveAt)
	{
		SpawnWave();
		NextWaveAt = Now + FMath::Max(10.0f, 25.0f - WaveIndex * 1.5f);
	}
	if (bPlayerOut)
	{
		Rules->ForceFinish(Now, -1);
		return;
	}
	// Objective-driven levels (destruction, boss, explicit objectives) end as soon as the goal is met.
	const bool bObjectiveMode = Mode && Mode->win == crash::WinCondition::Objective;
	const bool bBossLevel = LevelDef->objective.type == crash::ObjectiveType::Boss;
	if ((bObjectiveMode || bBossLevel) && LevelDef->objective.type != crash::ObjectiveType::Win && PrimaryObjectiveMet())
	{
		Rules->ForceFinish(Now, 0);
	}
}

crash::LevelRunResult ABCCampaignGameMode::BuildRun() const
{
	crash::LevelRunResult Run;
	const float Now = GetWorld()->GetTimeSeconds();
	Run.time = FMath::Max(0.0f, Now - StartTime);
	const crash::MatchOutcome& Outcome = Rules->Outcome();
	Run.won = Outcome.decided ? Outcome.winnerTeam == 0 : false;
	Run.bossDefeated = bBossDefeated;
	for (const int32 Index : CollectedThisRun)
	{
		Run.collectiblesFound.push_back(Index);
	}
	for (const TPair<int32, TWeakObjectPtr<AController>>& P : Participants)
	{
		const APlayerController* PC = Cast<APlayerController>(P.Value.Get());
		if (!PC)
		{
			continue;
		}
		if (const crash::ParticipantStats* S = Rules->Scores().Get(P.Key))
		{
			Run.takedowns = S->takedowns;
			Run.score = S->Score();
			Run.hits = S->impacts;
			Run.propsDestroyed = S->propsDestroyed;
			Run.hillTime = S->hillTime;
			Run.survived = S->survivalTime > 0.0f ? S->survivalTime : Run.time;
			Run.playerWrecked = S->wrecked > 0;
		}
		if (const ABCVehiclePawn* V = Cast<ABCVehiclePawn>(PC->GetPawn()))
		{
			Run.damageTakenFraction = V->GetDamage()->LostFraction();
		}
	}
	return Run;
}

void ABCCampaignGameMode::OnMatchFinished()
{
	if (IsHub() || !LevelDef)
	{
		Super::OnMatchFinished();
		return;
	}
	ABCGameState* GS = GetGameState<ABCGameState>();
	if (GS)
	{
		GS->WinnerTeam = Rules->Outcome().winnerTeam;
	}
	crash::LevelRunResult Run = BuildRun();
	// Objective levels: success is the primary objective, not a team win.
	Run.won = Run.won || crash::EvaluateObjective(LevelDef->objective, Run);
	UBCProfileSubsystem* Profile = UBCProfileSubsystem::Get(this);
	if (!Profile)
	{
		return;
	}
	const crash::LevelCompletion C = Profile->ApplyCampaignResult(*LevelDef, Run);
	FBCPersonalResult R;
	R.bCampaign = true;
	R.bWon = C.success;
	R.LevelId = BC::ToFString(LevelDef->id);
	R.Credits = C.credits;
	R.Xp = C.xp;
	R.LevelsGained = C.levelUp.levelsGained;
	const crash::LevelProgress& Progress = Profile->Local().campaign.levels.at(LevelDef->id);
	for (int32 i = 0; i < 3; ++i)
	{
		R.Stars.Add(Progress.stars[static_cast<size_t>(i)]);
		R.NewStars.Add(C.newStars[static_cast<size_t>(i)]);
	}
	for (const crash::ItemRef& Item : C.items)
	{
		R.Items.Add(BC::ToFString(std::string(crash::ToString(Item.type))) + TEXT(":") + BC::ToFString(Item.id));
	}
	TArray<FString> Outro;
	if (C.success)
	{
		for (const std::string& Key : LevelDef->outro)
		{
			Outro.Add(BC::ToFString(Key));
		}
	}
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (ABCPlayerController* PC = Cast<ABCPlayerController>(It->Get()))
		{
			PC->ClientShowResults(R);
			if (Outro.Num() > 0)
			{
				PC->ClientStory(Outro, false);
			}
		}
	}
}
