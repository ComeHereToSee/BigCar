// Copyright BigCar Team. All Rights Reserved.

#include "Gameplay/BCAIController.h"

#include "Arena/BCArenaBuilder.h"
#include "Core/BCDataSubsystem.h"
#include "Core/BigCarTypes.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Gameplay/BCGameState.h"
#include "Gameplay/BCMatchGameMode.h"
#include "Vehicle/BCVehicleDamage.h"
#include "Vehicle/BCVehicleMovement.h"
#include "Vehicle/BCVehiclePawn.h"

ABCAIController::ABCAIController()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	bWantsPlayerState = true;
}

void ABCAIController::SetProfile(const FString& ProfileId)
{
	const UBCDataSubsystem* Data = UBCDataSubsystem::Get(this);
	const crash::AIProfileDef* Profile = Data ? Data->Db().FindAIProfile(BC::ToStd(ProfileId)) : nullptr;
	crash::AIProfileDef Fallback;
	Fallback.id = "regular";
	Brain = MakeUnique<crash::AIBrain>(Profile ? *Profile : Fallback, static_cast<uint64>(GetUniqueID()) * 2654435761ull);
}

void ABCAIController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	ABCVehiclePawn* Me = Cast<ABCVehiclePawn>(GetPawn());
	if (!Me || !Brain || !Me->GetVehicleMovement()->IsSetUp())
	{
		return;
	}
	const ABCMatchGameMode* GM = GetWorld()->GetAuthGameMode<ABCMatchGameMode>();
	const ABCArenaBuilder* Arena = GM ? GM->GetArena() : nullptr;
	const ABCGameState* GS = GetWorld()->GetGameState<ABCGameState>();

	crash::AIPerception P;
	P.time = GetWorld()->GetTimeSeconds();
	const FTransform Xf = Me->GetActorTransform();
	P.self.id = Me->GetParticipantId();
	P.self.team = Me->GetTeam();
	P.self.position = BC::ToCrash(Xf.GetLocation());
	P.self.velocity = BC::ToCrash(Me->GetVelocity());
	P.self.forward = BC::DirToCrash(Xf.GetUnitAxis(EAxis::X));
	P.self.right = BC::DirToCrash(Xf.GetUnitAxis(EAxis::Y));
	P.self.health = Me->GetDamage()->Integrity();
	P.self.mass = Me->GetStats().massKg;
	P.boostFraction = Me->GetVehicleMovement()->BoostFraction();
	P.abilityReady = Me->GetVehicleMovement()->Ability().Ready();
	P.ability = Me->GetVehicleMovement()->Ability().Type();
	P.upsideDown = Me->GetVehicleMovement()->IsUpsideDown();
	P.forcedTarget = ForcedTarget;
	for (TActorIterator<ABCVehiclePawn> It(GetWorld()); It; ++It)
	{
		const ABCVehiclePawn* Other = *It;
		if (Other == Me || Other->IsDisplayOnly())
		{
			continue;
		}
		crash::AICarInfo Info;
		const FTransform OX = Other->GetActorTransform();
		Info.id = Other->GetParticipantId();
		Info.team = Other->GetTeam();
		Info.position = BC::ToCrash(OX.GetLocation());
		Info.velocity = BC::ToCrash(Other->GetVelocity());
		Info.forward = BC::DirToCrash(OX.GetUnitAxis(EAxis::X));
		Info.right = BC::DirToCrash(OX.GetUnitAxis(EAxis::Y));
		Info.health = Other->GetDamage()->Integrity();
		Info.mass = Other->GetStats().massKg;
		Info.wrecked = Other->IsWrecked();
		Info.isHuman = Other->IsPlayerControlled();
		Info.isBounty = GS && GS->BountyParticipant == Info.id;
		P.others.push_back(Info);
	}
	if (Arena && Arena->Def())
	{
		const crash::ArenaDef* Def = Arena->Def();
		P.arena.center = BC::ToCrash(Arena->Center());
		P.arena.halfX = Def->HalfX();
		P.arena.halfY = Def->HalfY();
		P.arena.circular = Def->shape != crash::ArenaShape::Rect;
		P.arena.ringOut = !Def->walls;
		P.arena.hasHill = GS && GS->HillRadius > 0.0f;
		if (P.arena.hasHill)
		{
			P.arena.hillCenter = BC::ToCrash(GS->HillCenter);
			P.arena.hillRadius = GS->HillRadius / 100.0f;
			P.arena.hillOwnedByMyTeam = GS->HillOwnerTeam == Me->GetTeam();
		}
	}
	// Obstacle feelers (5 Hz).
	FeelerTimer -= DeltaSeconds;
	if (FeelerTimer <= 0.0f)
	{
		FeelerTimer = 0.2f;
		const FVector Start = Xf.GetLocation() + FVector(0, 0, 40.0f);
		const float Range = FMath::Clamp(Me->GetVelocity().Size() * 1.2f, 800.0f, 3000.0f);
		FCollisionQueryParams Params(SCENE_QUERY_STAT(BCAIFeeler), false, Me);
		auto Probe = [&](float YawOffset) {
			const FVector Dir = Xf.GetRotation().RotateVector(FRotator(0.0f, YawOffset, 0.0f).Vector());
			FHitResult Hit;
			if (GetWorld()->LineTraceSingleByChannel(Hit, Start, Start + Dir * Range, ECC_WorldStatic, Params))
			{
				return Hit.Distance / Range;
			}
			return 1.0f;
		};
		ClearLeft = Probe(-30.0f);
		ClearCenter = Probe(0.0f);
		ClearRight = Probe(30.0f);
	}
	P.arena.clearLeft = ClearLeft;
	P.arena.clearCenter = ClearCenter;
	P.arena.clearRight = ClearRight;

	const crash::AIOutput Out = Brain->Think(P, DeltaSeconds);
	Me->SetDriverInput(Out.controls, false, false);
	if (Out.useAbility)
	{
		Me->RequestAbility();
	}
	if (Out.requestReset)
	{
		Me->RequestReset();
	}
}
