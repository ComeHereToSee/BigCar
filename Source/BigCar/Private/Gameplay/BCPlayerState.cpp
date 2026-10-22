// Copyright BigCar Team. All Rights Reserved.

#include "Gameplay/BCPlayerState.h"

#include "Net/UnrealNetwork.h"

void ABCPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABCPlayerState, Team);
	DOREPLIFETIME(ABCPlayerState, ParticipantId);
	DOREPLIFETIME(ABCPlayerState, MatchScore);
	DOREPLIFETIME(ABCPlayerState, Takedowns);
	DOREPLIFETIME(ABCPlayerState, Assists);
	DOREPLIFETIME(ABCPlayerState, Wrecked);
	DOREPLIFETIME(ABCPlayerState, DamageDealt);
	DOREPLIFETIME(ABCPlayerState, bEliminated);
	DOREPLIFETIME(ABCPlayerState, RespawnIn);
	DOREPLIFETIME(ABCPlayerState, Build);
	DOREPLIFETIME(ABCPlayerState, Title);
	DOREPLIFETIME(ABCPlayerState, bBot);
}
