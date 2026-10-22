// Copyright BigCar Team. All Rights Reserved.
//
// The car. A box rigid body (Chaos) carries the mass, collision and physics replication; the visible body is a
// procedural deformable mesh; equipment (ram bars, wedges, plows, spikes…) is spawned as real welded collision
// geometry so a wedge physically scoops cars. Vehicle dynamics come from CrashCore via UBCVehicleMovement.
#pragma once

#include "Core/BigCarTypes.h"
#include "CoreMinimal.h"
#include "CrashCore/Damage.h"
#include "CrashCore/Stats.h"
#include "GameFramework/Pawn.h"
#include "Vehicle/BCBodyMesh.h"

#include "BCVehiclePawn.generated.h"

class UBoxComponent;
class UProceduralMeshComponent;
class UStaticMeshComponent;
class UStaticMesh;
class USpringArmComponent;
class UCameraComponent;
class UPointLightComponent;
class USpotLightComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UBCVehicleMovement;
class UBCVehicleDamage;
class UBCEngineSynthComponent;
class UBCSkidSynthComponent;
class UBCImpactSynthComponent;

// What the game mode applies to one side of a resolved impact.
struct FBCImpactApply
{
	crash::ZoneDamage Damage;
	FVector ImpulseUE = FVector::ZeroVector; // kg*cm/s
	FVector WorldPoint = FVector::ZeroVector;
	FVector DentDirWorld = FVector::ZeroVector; // direction the body is pushed
	float Severity = 0.0f;
	uint8 Kind = 0;
	bool bVehicle = true;
};

UCLASS()
class BIGCAR_API ABCVehiclePawn : public APawn
{
	GENERATED_BODY()

public:
	ABCVehiclePawn();

	// Call before FinishSpawning (deferred spawn). Display-only cars (garage showroom) have no physics.
	void InitVehicle(const FBCVehicleBuild& InBuild, int32 InTeam, bool bInDisplayOnly = false);
	// Rebuilds visuals for a new build (garage preview).
	void SetDisplayBuild(const FBCVehicleBuild& InBuild);

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PossessedBy(AController* NewController) override;

	// ---- Controls (local player input or AI).
	void SetDriverInput(const crash::VehicleControls& Controls, bool bHorn, bool bLookBack);
	void RequestAbility();
	void RequestReset();

	UFUNCTION(Server, Unreliable)
	void ServerSetControls(FBCControls NewControls);

	UFUNCTION(Server, Reliable)
	void ServerUseAbility();

	UFUNCTION(Server, Reliable)
	void ServerRequestReset();

	// ---- State.
	const FBCVehicleBuild& GetBuild() const { return Build; }
	const crash::EffectiveStats& GetStats() const { return Stats; }
	const crash::VehicleDef* GetDef() const { return Stats.def; }
	UBCVehicleMovement* GetVehicleMovement() const { return VehicleMovement; }
	UBCVehicleDamage* GetDamage() const { return Damage; }
	UBoxComponent* GetBody() const { return Body; }
	int32 GetTeam() const { return Team; }
	void SetTeam(int32 InTeam) { Team = InTeam; }
	int32 GetParticipantId() const { return ParticipantId; }
	void SetParticipantId(int32 Id) { ParticipantId = Id; }
	bool IsWrecked() const;
	bool IsDisplayOnly() const { return bDisplayOnly; }
	FVector GetPreImpactVelocity() const { return PreImpactVelocity; }
	FVector GetHalfExtentsCm() const;
	float GetLastImpactTime() const { return LastImpactTime; }
	bool AreControlsEnabled() const { return bControlsEnabled; }
	void SetControlsEnabled(bool bEnabled) { bControlsEnabled = bEnabled; }
	FString GetDriverName() const;
	bool IsBossCar() const { return bBoss; }
	void SetBossCar(bool bInBoss) { bBoss = bInBoss; }

	// ---- Server-side gameplay (called by the game mode).
	crash::DamageEvent ServerApplyImpactResult(const FBCImpactApply& Apply);
	void ServerWreck(bool bRingOut);
	void ServerResetTo(const FTransform& Transform, bool bRepair);
	void ServerAddImpulse(const FVector& ImpulseUE, const FVector& WorldPoint);
	void ServerRepair(float Fraction);

	// ---- Cosmetic events on every machine.
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastImpactFX(FVector_NetQuantize WorldPoint, FVector_NetQuantizeNormal Normal, float Severity, uint8 Kind,
						   FVector_NetQuantize LocalDent, FVector_NetQuantizeNormal LocalDentDir, float DentStrength, bool bVehicle);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastWrecked(bool bRingOut);

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastAbility(uint8 Ability);

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastPartLost(uint8 Zone, int8 Wheel);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastRepaired();

	void AddCameraShake(float Strength);
	void SetCrashCam(float Duration, AActor* Focus);

protected:
	UFUNCTION()
	void OnBodyHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse,
				   const FHitResult& Hit);

	UFUNCTION()
	void OnRep_Build();

	void BuildVehicle();
	void BuildEquipment();
	void BuildLights();
	void UpdateWheels(float DeltaSeconds);
	void UpdateCamera(float DeltaSeconds);
	void UpdateAudio();
	void UpdateDamageVisuals();
	void HandleLanded(float Seconds);
	void ApplyPaint();
	void SpawnDebris(const FVector& WorldPoint, const FVector& Direction, const FLinearColor& Color, int32 Count, float Size);
	UMaterialInstanceDynamic* MakeMaterial(const FLinearColor& Color);
	UStaticMeshComponent* AddPart(UStaticMesh* Mesh, const FTransform& Relative, const FLinearColor& Color, bool bCollision,
								  USceneComponent* Parent = nullptr);

	UPROPERTY(VisibleAnywhere, Category = "Vehicle")
	TObjectPtr<UBoxComponent> Body;

	UPROPERTY(VisibleAnywhere, Category = "Vehicle")
	TObjectPtr<UProceduralMeshComponent> BodyMesh;

	UPROPERTY(VisibleAnywhere, Category = "Vehicle")
	TObjectPtr<UBCVehicleMovement> VehicleMovement;

	UPROPERTY(VisibleAnywhere, Category = "Vehicle")
	TObjectPtr<UBCVehicleDamage> Damage;

	UPROPERTY(VisibleAnywhere, Category = "Camera")
	TObjectPtr<USpringArmComponent> CameraArm;

	UPROPERTY(VisibleAnywhere, Category = "Camera")
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY(VisibleAnywhere, Category = "Audio")
	TObjectPtr<UBCEngineSynthComponent> EngineAudio;

	UPROPERTY(VisibleAnywhere, Category = "Audio")
	TObjectPtr<UBCSkidSynthComponent> SkidAudio;

	UPROPERTY(VisibleAnywhere, Category = "Audio")
	TObjectPtr<UBCImpactSynthComponent> ImpactAudio;

	UPROPERTY()
	TObjectPtr<UPointLightComponent> FireLight;

	UPROPERTY()
	TArray<TObjectPtr<USpotLightComponent>> Headlights;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> WheelMeshes;

	UPROPERTY()
	TArray<TObjectPtr<UPrimitiveComponent>> Parts;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> PaintMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> GlassMaterial;

	UPROPERTY()
	TObjectPtr<UStaticMesh> CubeMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> CylinderMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> ConeMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> BaseMaterial;

	UPROPERTY(ReplicatedUsing = OnRep_Build)
	FBCVehicleBuild Build;

	UPROPERTY(Replicated)
	int32 Team = -1;

	UPROPERTY(Replicated)
	int32 ParticipantId = -1;

	UPROPERTY(Replicated)
	FBCControls RepControls;

	UPROPERTY(Replicated)
	bool bControlsEnabled = true;

	UPROPERTY(Replicated)
	bool bBoss = false;

private:
	crash::EffectiveStats Stats;
	FBCBodyMesh BodyShape;
	bool bBuilt = false;
	bool bDisplayOnly = false;
	bool bWreckedVisual = false;
	float BodyCenterHeight = 0.8f;
	FVector PreImpactVelocity = FVector::ZeroVector;
	float LastImpactTime = -100.0f;
	FBCControls LocalControls;
	bool bLocalHorn = false;
	bool bLocalLookBack = false;
	float ControlSendTimer = 0.0f;
	FBCControls LastSentControls;
	float ShakeAmount = 0.0f;
	float CrashCamTime = 0.0f;
	TWeakObjectPtr<AActor> CrashCamFocus;
	float FireFlicker = 0.0f;
	float UpsideDownTime = 0.0f;
	TArray<bool> WheelLost;
	TArray<uint8> ZoneBrokenShown;
	float DentCooldown = 0.0f;
	FLinearColor CurrentPaintColor = FLinearColor::White;
};

