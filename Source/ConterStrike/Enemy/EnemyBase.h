// Copyright ConterStrike. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "EnemyBase.generated.h"

class UAnimMontage;
class USoundCue;
class UParticleSystem;
class UDataTable;

// Delegate declarations
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnShootEnd);

/**
 * AEnemyBase - Base enemy character with complete shooting system.
 *
 * Features:
 * - Ammo management (magazine + reserve ammo)
 * - Fire rate control via ShootingSpeed
 * - Bullet spread / accuracy system
 * - Line trace shooting with damage application
 * - Muzzle flash VFX and shooting SFX
 * - Target validation (LOS, distance, angle)
 * - Auto-reload when magazine empty
 * - Proper behavior tree integration via ShootEnd delegate
 * - Headshot detection (bone-based)
 */
UCLASS(Blueprintable, BlueprintType, meta = (DisplayName = "Enemy Base"))
class CONTERSTRIKE_API AEnemyBase : public ACharacter
{
	GENERATED_BODY()

public:
	AEnemyBase();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaTime) override;

#pragma region Shooting System

public:
	// ---- Shooting ----

	/** Main shoot function - called by Behavior Tree task. */
	UFUNCTION(BlueprintCallable, Category = "Enemy|Shooting")
	void Shoot();

	/** Stop shooting and clean up. */
	UFUNCTION(BlueprintCallable, Category = "Enemy|Shooting")
	void StopShooting();

	/** Force reload. */
	UFUNCTION(BlueprintCallable, Category = "Enemy|Shooting")
	void Reload();

	/** Returns true if the enemy can shoot right now. */
	UFUNCTION(BlueprintPure, Category = "Enemy|Shooting")
	bool CanShoot() const;

	/** Returns true if the enemy has a valid attack target. */
	UFUNCTION(BlueprintPure, Category = "Enemy|Shooting")
	bool HasValidTarget() const;

	/** Get remaining ammo in magazine. */
	UFUNCTION(BlueprintPure, Category = "Enemy|Shooting")
	int32 GetCurrentAmmo() const { return CurrentAmmoInMag; }

	/** Get remaining reserve ammo. */
	UFUNCTION(BlueprintPure, Category = "Enemy|Shooting")
	int32 GetCurrentReserveAmmo() const { return CurrentReserveAmmo; }

	/** Get max ammo capacity per magazine. */
	UFUNCTION(BlueprintPure, Category = "Enemy|Shooting")
	int32 GetMaxAmmo() const { return MaxAmmoInMag; }

	/** Whether the enemy is currently reloading. */
	UFUNCTION(BlueprintPure, Category = "Enemy|Shooting")
	bool IsReloading() const { return bIsReloading; }

	// ---- Events ----

	/** Called when a shot is fired (for effects, animation, etc.). */
	UPROPERTY(BlueprintAssignable, Category = "Enemy|Events")
	FOnShootEnd OnShootEnd;

protected:
	/** Internal shoot execution - performs the actual line trace and damage. */
	void ExecuteShot();

	/** Perform the line trace for shooting. */
	bool PerformShootTrace(FVector& OutTraceStart, FVector& OutTraceEnd, FHitResult& OutHit);

	/** Apply damage to the hit actor. */
	void ApplyDamageToHit(const FHitResult& Hit);

	/** Play shooting effects (muzzle flash, sound). */
	void PlayShootEffects();

	/** Play shooting animation montage. */
	void PlayShootAnimation();

	/** Called when shoot montage finishes or is interrupted. */
	UFUNCTION()
	void OnShootMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	/** Validate if we can see the target (line of sight check). */
	bool HasLineOfSightToTarget() const;

	/** Check if target is within shooting range. */
	bool IsTargetInRange() const;

	/** Calculate bullet spread offset based on accuracy. */
	FVector ApplyBulletSpread(const FVector& AimDirection) const;

	/** Check if a bone name corresponds to the head. */
	bool IsHeadBone(const FName& BoneName) const;

#pragma endregion

#pragma region Weapon Configuration

public:
	// ---- Weapon Stats (configurable per enemy instance or via DataTable) ----

	/** Maximum ammo in a single magazine. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Weapon Stats", meta = (ClampMin = "1", ClampMax = "100"))
	int32 MaxAmmoInMag = 30;

	/** Maximum reserve ammo carried. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Weapon Stats", meta = (ClampMin = "0", ClampMax = "500"))
	int32 MaxReserveAmmo = 120;

	/** Shots per second (Rounds Per Minute / 60). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Weapon Stats", meta = (ClampMin = "0.1", ClampMax = "30"))
	float ShootingSpeed = 10.0f;

	/** Base damage per shot (body hit). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Weapon Stats", meta = (ClampMin = "0"))
	float BodyDamage = 25.0f;

	/** Damage multiplier for headshots. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Weapon Stats", meta = (ClampMin = "1.0", ClampMax = "10.0"))
	float HeadshotMultiplier = 4.0f;

	/** Maximum shooting distance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Weapon Stats", meta = (ClampMin = "100", ClampMax = "50000"))
	float MaxShootRange = 10000.0f;

	/** Minimum delay between bursts (prevents full-auto behavior if undesired). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Weapon Stats", meta = (ClampMin = "0", ClampMax = "5"))
	float BurstCooldown = 0.1f;

	// ---- Accuracy ----

	/** Base bullet spread angle in degrees (0 = perfect accuracy). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Accuracy", meta = (ClampMin = "0", ClampMax = "15"))
	float BaseSpreadAngle = 1.5f;

	/** Additional spread per shot (recoil accumulation). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Accuracy", meta = (ClampMin = "0", ClampMax = "5"))
	float SpreadPerShot = 0.3f;

	/** Maximum accumulated spread angle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Accuracy", meta = (ClampMin = "0", ClampMax = "20"))
	float MaxSpreadAngle = 5.0f;

	/** How fast spread recovers per second when not shooting. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Accuracy", meta = (ClampMin = "0", ClampMax = "20"))
	float SpreadRecoverySpeed = 4.0f;

	/** Reaction time before first shot when acquiring a new target (seconds). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Accuracy", meta = (ClampMin = "0", ClampMax = "5"))
	float TargetAcquisitionDelay = 0.3f;

	// ---- Effects ----

	/** Muzzle flash particle effect. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Effects")
	TObjectPtr<UParticleSystem> MuzzleFlashEffect;

	/** Shooting sound cue. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Effects")
	TObjectPtr<USoundCue> ShootingSound;

	/** Out of ammo click sound. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Effects")
	TObjectPtr<USoundCue> OutOfAmmoSound;

	/** Bullet impact VFX (on environment). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Effects")
	TObjectPtr<UParticleSystem> BulletImpactVFX;

	/** Bullet impact VFX (on characters). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Effects")
	TObjectPtr<UParticleSystem> BulletImpactCharacterVFX;

	/** Bullet impact SFX. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Effects")
	TObjectPtr<USoundCue> BulletImpactSFX;

	// ---- Animation ----

	/** Shoot animation montage. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Animation")
	TObjectPtr<UAnimMontage> ShootMontage;

	/** Reload animation montage. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Animation")
	TObjectPtr<UAnimMontage> ReloadMontage;

	/** Play rate for shoot animation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Animation", meta = (ClampMin = "0.1", ClampMax = "5.0"))
	float ShootAnimPlayRate = 1.0f;

	// ---- Weapon Socket ----

	/** Socket name on the weapon mesh for muzzle flash / trace start. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Weapon Setup")
	FName MuzzleSocketName = FName("S_Muzzle");

	/** Tag on the skeletal mesh component that represents the weapon. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Weapon Setup")
	FName WeaponComponentTag = FName("Weapon");

	/** Targeting bone name for aim direction (head). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Weapon Setup")
	FName TargetBoneName = FName("head");

#pragma endregion

#pragma region Behavior Tree

public:
	/** Attack target - typically set from Behavior Tree Blackboard. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|AI")
	TObjectPtr<AActor> AttackTarget;

	/** Whether this enemy is currently actively shooting. */
	UPROPERTY(BlueprintReadOnly, Category = "Enemy|AI")
	bool bIsShooting = false;

#pragma endregion

protected:
	// ---- Internal State ----

	/** Current ammo in the magazine. */
	int32 CurrentAmmoInMag = 30;

	/** Current reserve ammo. */
	int32 CurrentReserveAmmo = 120;

	/** Current accumulated spread. */
	float CurrentSpread = 0.0f;

	/** Whether currently reloading. */
	bool bIsReloading = false;

	/** Whether the shoot montage is currently playing. */
	bool bIsPlayingShootMontage = false;

	/** Timer handle for fire rate. */
	FTimerHandle FireRateTimer;

	/** Timer handle for reload completion. */
	FTimerHandle ReloadTimer;

	/** Timer handle for burst cooldown. */
	FTimerHandle BurstCooldownTimer;

	/** Cached weapon skeletal mesh component. */
	UPROPERTY()
	TObjectPtr<USkeletalMeshComponent> WeaponMesh;

	/** Track whether we have a valid shot this tick to prevent double-firing. */
	bool bShotFiredThisTick = false;

	/** Timestamp of last shot for spread recovery. */
	float LastShotTime = 0.0f;

	/** Timestamp when target was first acquired. */
	float TargetAcquiredTime = 0.0f;

	/** Previous attack target for acquisition delay tracking. */
	UPROPERTY()
	TObjectPtr<AActor> PreviousAttackTarget;
};
