// Copyright BigCar Team. All Rights Reserved.

#include "Gameplay/BCGameState.h"

#include "Arena/BCArenaBuilder.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

ABCGameState::ABCGameState()
{
	PrimaryActorTick.bCanEverTick = false;
}

void ABCGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABCGameState, ModeId);
	DOREPLIFETIME(ABCGameState, ArenaId);
	DOREPLIFETIME(ABCGameState, LevelId);
	DOREPLIFETIME(ABCGameState, Phase);
	DOREPLIFETIME(ABCGameState, TimeRemaining);
	DOREPLIFETIME(ABCGameState, CountdownRemaining);
	DOREPLIFETIME(ABCGameState, Round);
	DOREPLIFETIME(ABCGameState, TeamCount);
	DOREPLIFETIME(ABCGameState, TeamScores);
	DOREPLIFETIME(ABCGameState, RoundWins);
	DOREPLIFETIME(ABCGameState, HillOwnerTeam);
	DOREPLIFETIME(ABCGameState, bHillContested);
	DOREPLIFETIME(ABCGameState, HillCenter);
	DOREPLIFETIME(ABCGameState, HillRadius);
	DOREPLIFETIME(ABCGameState, BountyParticipant);
	DOREPLIFETIME(ABCGameState, bNightArena);
	DOREPLIFETIME(ABCGameState, WinnerTeam);
	DOREPLIFETIME(ABCGameState, Results);
	DOREPLIFETIME(ABCGameState, ObjectiveText);
	DOREPLIFETIME(ABCGameState, CollectiblesFound);
	DOREPLIFETIME(ABCGameState, CollectiblesTotal);
}

void ABCGameState::MulticastFeed_Implementation(const FBCFeedEntry& Entry)
{
	RecentFeed.Add(Entry);
	if (RecentFeed.Num() > 6)
	{
		RecentFeed.RemoveAt(0);
	}
	OnFeed.Broadcast(Entry);
}

void ABCGameState::OnRep_ArenaId()
{
	if (HasAuthority() || Arena || ArenaId.IsEmpty())
	{
		return;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Arena = GetWorld()->SpawnActor<ABCArenaBuilder>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	if (Arena)
	{
		Arena->Build(ArenaId, false);
	}
}
