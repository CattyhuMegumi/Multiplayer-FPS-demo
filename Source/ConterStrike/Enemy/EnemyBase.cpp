// Copyright ConterStrike. All Rights Reserved.

#include "EnemyBase.h"
#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Particles/ParticleSystem.h"
#include "Sound/SoundCue.h"
#include "TimerManager.h"

AEnemyBase::AEnemyBase()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.05f; // 20fps tick is enough for shooting

	CurrentAmmoInMag = MaxAmmoInMag;
	CurrentReserveAmmo = MaxReserveAmmo;
}

void AEnemyBase::BeginPlay()
{
	Super::BeginPlay();

	// Cache weapon mesh by tag
	TArray<UActorComponent*> Components;
	GetComponents(USkeletalMeshComponent::StaticClass(), Components);
	for (UActorComponent* Comp : Components)
	{
		if (Comp && Comp->ComponentHasTag(WeaponComponentTag))
		{
			WeaponMesh = Cast<USkeletalMeshComponent>(Comp);
			break;
		}
	}

	// If no weapon mesh found by tag, try finding any child skeletal mesh
	if (!WeaponMesh)
	{
		TArray<USkeletalMeshComponent*> MeshComponents;
		GetComponents<USkeletalMeshComponent>(MeshComponents);
		for (USkeletalMeshComponent* MeshComp : MeshComponents)
		{
			if (MeshComp && MeshComp != GetMesh())
			{
				WeaponMesh = MeshComp;
				break;
			}
		}
	}

	// Initialize ammo
	CurrentAmmoInMag = MaxAmmoInMag;
	CurrentReserveAmmo = MaxReserveAmmo;

	UE_LOG(LogTemp, Log, TEXT("[AEnemyBase] %s initialized. WeaponMesh: %s, Ammo: %d/%d"),
		*GetName(),
		WeaponMesh ? *WeaponMesh->GetName() : TEXT("NULL"),
		CurrentAmmoInMag, CurrentReserveAmmo);
}

void AEnemyBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Clear all active timers
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(FireRateTimer);
		World->GetTimerManager().ClearTimer(ReloadTimer);
		World->GetTimerManager().ClearTimer(BurstCooldownTimer);
	}

	Super::EndPlay(EndPlayReason);
}

void AEnemyBase::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Recover spread over time when not shooting
	if (!bIsShooting && CurrentSpread > 0.0f)
	{
		CurrentSpread = FMath::Max(0.0f, CurrentSpread - SpreadRecoverySpeed * DeltaTime);
	}

	// Track target acquisition for reaction delay
	if (AttackTarget != PreviousAttackTarget)
	{
		TargetAcquiredTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
		PreviousAttackTarget = AttackTarget;
	}

	bShotFiredThisTick = false;
}

// ============================================================================
// Shooting System
// ============================================================================

bool AEnemyBase::CanShoot() const
{
	// Basic validity checks
	if (!IsValid(this) || !GetWorld())
	{
		return false;
	}

	// Already shooting this frame (prevent double-fire)
	if (bShotFiredThisTick)
	{
		return false;
	}

	// Currently reloading
	if (bIsReloading)
	{
		return false;
	}

	// Animation still playing
	if (bIsPlayingShootMontage)
	{
		return false;
	}

	// Fire rate cooldown
	if (FireRateTimer.IsValid())
	{
		return false;
	}

	// Burst cooldown
	if (BurstCooldownTimer.IsValid())
	{
		return false;
	}

	// No ammo
	if (CurrentAmmoInMag <= 0)
	{
		return false;
	}

	// No valid target
	if (!HasValidTarget())
	{
		return false;
	}

	// Target not in range
	if (!IsTargetInRange())
	{
		return false;
	}

	// Target acquisition delay (reaction time)
	if (GetWorld()->GetTimeSeconds() - TargetAcquiredTime < TargetAcquisitionDelay)
	{
		return false;
	}

	return true;
}

bool AEnemyBase::HasValidTarget() const
{
	if (!IsValid(AttackTarget))
	{
		return false;
	}

	// Target must be alive (if it's a character/pawn)
	if (const APawn* TargetPawn = Cast<APawn>(AttackTarget))
	{
		if (!TargetPawn->IsPawnControlled() || !IsValid(TargetPawn->GetController()))
		{
			return false;
		}
	}

	return true;
}

bool AEnemyBase::IsTargetInRange() const
{
	if (!IsValid(AttackTarget))
	{
		return false;
	}

	const float Distance = FVector::Dist(GetActorLocation(), AttackTarget->GetActorLocation());
	return Distance <= MaxShootRange;
}

bool AEnemyBase::HasLineOfSightToTarget() const
{
	if (!IsValid(AttackTarget) || !GetWorld())
	{
		return false;
	}

	FVector TraceStart = GetActorLocation();
	FVector TraceEnd = AttackTarget->GetActorLocation();

	// Use weapon muzzle if available
	if (WeaponMesh)
	{
		TraceStart = WeaponMesh->GetSocketLocation(MuzzleSocketName);
	}

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);
	QueryParams.bTraceComplex = false;

	FHitResult Hit;
	bool bHit = GetWorld()->LineTraceSingleByChannel(
		Hit,
		TraceStart,
		TraceEnd,
		ECC_Visibility,
		QueryParams
	);

	// If we didn't hit anything, or we hit the target, we have LOS
	return !bHit || (Hit.GetActor() == AttackTarget);
}

void AEnemyBase::Shoot()
{
	if (!CanShoot())
	{
		// If out of ammo, try to reload
		if (CurrentAmmoInMag <= 0 && CurrentReserveAmmo > 0 && !bIsReloading)
		{
			Reload();
		}
		else if (CurrentAmmoInMag <= 0 && CurrentReserveAmmo <= 0)
		{
			// Completely out of ammo - broadcast end so BT can move on
			UE_LOG(LogTemp, Warning, TEXT("[AEnemyBase] %s: Out of ammo!"), *GetName());
			OnShootEnd.Broadcast();
		}
		return;
	}

	bIsShooting = true;
	bShotFiredThisTick = true;

	// Execute the actual shot
	ExecuteShot();

	// Consume ammo
	CurrentAmmoInMag = FMath::Max(0, CurrentAmmoInMag - 1);

	// Accumulate spread
	CurrentSpread = FMath::Min(MaxSpreadAngle, CurrentSpread + SpreadPerShot);

	// Record shot time
	LastShotTime = GetWorld()->GetTimeSeconds();

	// Set fire rate timer
	if (UWorld* World = GetWorld())
	{
		const float FireInterval = ShootingSpeed > 0.0f ? (1.0f / ShootingSpeed) : 0.1f;
		World->GetTimerManager().SetTimer(
			FireRateTimer,
			FTimerDelegate::CreateLambda([this]()
			{
				bIsShooting = false;
				// Auto-shoot next round if still in combat
				// The behavior tree will call Shoot() again on next tick
			}),
			FireInterval,
			false
		);

		// Set burst cooldown
		if (BurstCooldown > 0.0f)
		{
			World->GetTimerManager().SetTimer(
				BurstCooldownTimer,
				BurstCooldown,
				false
			);
		}
	}

	// Auto-reload if magazine is empty and we have reserve ammo
	if (CurrentAmmoInMag <= 0 && CurrentReserveAmmo > 0)
	{
		Reload();
	}
}

void AEnemyBase::ExecuteShot()
{
	// Play effects
	PlayShootEffects();

	// Play animation
	PlayShootAnimation();

	// Perform line trace
	FVector TraceStart, TraceEnd;
	FHitResult Hit;
	if (PerformShootTrace(TraceStart, TraceEnd, Hit))
	{
		// Hit something - apply damage if it's a valid target
		ApplyDamageToHit(Hit);

		// Spawn impact effects
		if (BulletImpactVFX || BulletImpactCharacterVFX)
		{
			UParticleSystem* ImpactFX = Hit.GetActor() && Hit.GetActor()->IsA<ACharacter>()
				? BulletImpactCharacterVFX
				: BulletImpactVFX;

			if (ImpactFX)
			{
				UGameplayStatics::SpawnEmitterAtLocation(
					GetWorld(),
					ImpactFX,
					Hit.ImpactPoint,
					Hit.ImpactNormal.Rotation(),
					false
				);
			}
		}

		// Bullet impact sound
		if (BulletImpactSFX)
		{
			UGameplayStatics::PlaySoundAtLocation(
				GetWorld(),
				BulletImpactSFX,
				Hit.ImpactPoint
			);
		}

		// Debug visualization (editor only)
#if WITH_EDITOR
		if (UKismetSystemLibrary::K2_EnabledOnCurrentWorld(this, true))
		{
			UKismetSystemLibrary::DrawDebugLine(
				GetWorld(),
				TraceStart,
				Hit.ImpactPoint,
				FLinearColor::Green,
				0.5f,
				2.0f
			);
			UKismetSystemLibrary::DrawDebugSphere(
				GetWorld(),
				Hit.ImpactPoint,
				10.0f,
				8,
				FLinearColor::Red,
				0.5f
			);
		}
#endif
	}
	else
	{
		// Miss - draw debug
#if WITH_EDITOR
		if (UKismetSystemLibrary::K2_EnabledOnCurrentWorld(this, true))
		{
			UKismetSystemLibrary::DrawDebugLine(
				GetWorld(),
				TraceStart,
				TraceEnd,
				FLinearColor::Yellow,
				0.5f,
				1.0f
			);
		}
#endif
	}
}

bool AEnemyBase::PerformShootTrace(FVector& OutTraceStart, FVector& OutTraceEnd, FHitResult& OutHit)
{
	if (!GetWorld())
	{
		return false;
	}

	// Get trace start from weapon muzzle socket
	if (WeaponMesh)
	{
		OutTraceStart = WeaponMesh->GetSocketLocation(MuzzleSocketName);
	}
	else
	{
		// Fallback: use actor location + eye height
		OutTraceStart = GetActorLocation();
		OutTraceStart.Z += BaseEyeHeight;
	}

	// --- NEW: Calculate aim direction with spread ---
	FVector AimDirection;

	if (IsValid(AttackTarget))
	{
		// Aim at target's center or head bone
		FVector TargetLoc = AttackTarget->GetActorLocation();
		if (const ACharacter* TargetChar = Cast<ACharacter>(AttackTarget))
		{
			if (USkeletalMeshComponent* TargetMesh = TargetChar->GetMesh())
			{
				// Try to aim at head bone for more realistic targeting
				FVector HeadLoc = TargetMesh->GetSocketLocation(TargetBoneName);
				if (!HeadLoc.IsNearlyZero())
				{
					TargetLoc = HeadLoc;
				}
				else
				{
					// Fallback: add approximate head height
					TargetLoc.Z += TargetChar->BaseEyeHeight;
				}
			}
		}
		AimDirection = (TargetLoc - OutTraceStart).GetSafeNormal();
	}
	else
	{
		AimDirection = GetActorForwardVector();
	}

	// Apply bullet spread
	AimDirection = ApplyBulletSpread(AimDirection);

	// Calculate trace end
	OutTraceEnd = OutTraceStart + AimDirection * MaxShootRange;

	// Configure trace params
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);
	QueryParams.bTraceComplex = true;
	QueryParams.bReturnPhysicalMaterial = true;

	// Ignore other enemies? (friendly fire check)
	// QueryParams.AddIgnoredActors(ActorsToIgnore);

	// Perform the line trace
	bool bHit = GetWorld()->LineTraceSingleByChannel(
		OutHit,
		OutTraceStart,
		OutTraceEnd,
		ECC_GameTraceChannel1, // Use custom trace channel or Visibility
		QueryParams
	);

	return bHit;
}

FVector AEnemyBase::ApplyBulletSpread(const FVector& AimDirection) const
{
	if (CurrentSpread <= 0.0f)
	{
		return AimDirection;
	}

	// Generate random spread within a cone
	float HalfAngle = FMath::DegreesToRadians(CurrentSpread * 0.5f);

	// Random angles for the spread cone
	float RandomYaw = FMath::FRandRange(-HalfAngle, HalfAngle);
	float RandomPitch = FMath::FRandRange(-HalfAngle, HalfAngle);

	// Get basis vectors perpendicular to aim direction
	FVector RightVector = FVector::CrossProduct(AimDirection, FVector::UpVector).GetSafeNormal();
	if (RightVector.IsNearlyZero())
	{
		RightVector = FVector::CrossProduct(AimDirection, FVector::ForwardVector).GetSafeNormal();
	}
	FVector UpVector = FVector::CrossProduct(RightVector, AimDirection).GetSafeNormal();

	// Apply rotation
	FRotator SpreadRotator = FRotator(
		FMath::RadiansToDegrees(RandomPitch),
		FMath::RadiansToDegrees(RandomYaw),
		0.0f
	);

	FVector SpreadDirection = SpreadRotator.RotateVector(AimDirection);
	return SpreadDirection.GetSafeNormal();
}

void AEnemyBase::ApplyDamageToHit(const FHitResult& Hit)
{
	AActor* HitActor = Hit.GetActor();
	if (!IsValid(HitActor))
	{
		return;
	}

	// Calculate damage
	float FinalDamage = BodyDamage;

	// Check for headshot
	if (IsHeadBone(Hit.BoneName))
	{
		FinalDamage *= HeadshotMultiplier;
		UE_LOG(LogTemp, Log, TEXT("[AEnemyBase] %s: HEADSHOT! %.0f damage to %s"),
			*GetName(), FinalDamage, *HitActor->GetName());
	}

	// Apply damage
	FPointDamageEvent DamageEvent(FinalDamage, Hit, -AimDirection, nullptr);
	HitActor->TakeDamage(FinalDamage, DamageEvent, GetController(), this);

	UE_LOG(LogTemp, Verbose, TEXT("[AEnemyBase] %s: Hit %s for %.0f damage (bone: %s)"),
		*GetName(), *HitActor->GetName(), FinalDamage, *Hit.BoneName.ToString());
}

bool AEnemyBase::IsHeadBone(const FName& BoneName) const
{
	FString BoneStr = BoneName.ToString().ToLower();
	return BoneStr.Contains(TEXT("head")) ||
		   BoneStr.Contains(TEXT("neck")) ||
		   BoneStr.Contains(TEXT("cranium")) ||
		   BoneStr.Contains(TEXT("skull"));
}

void AEnemyBase::PlayShootEffects()
{
	// Muzzle flash
	if (MuzzleFlashEffect && WeaponMesh)
	{
		UGameplayStatics::SpawnEmitterAttached(
			MuzzleFlashEffect,
			WeaponMesh,
			MuzzleSocketName,
			FVector::ZeroVector,
			FRotator::ZeroRotator,
			EAttachLocation::SnapToTarget,
			false
		);
	}

	// Shooting sound
	if (ShootingSound)
	{
		if (WeaponMesh)
		{
			UGameplayStatics::SpawnSoundAttached(
				ShootingSound,
				WeaponMesh,
				MuzzleSocketName
			);
		}
		else
		{
			UGameplayStatics::PlaySoundAtLocation(GetWorld(), ShootingSound, GetActorLocation());
		}
	}
	else
	{
		// No shooting sound configured - fire a subtle log warning
		UE_LOG(LogTemp, Verbose, TEXT("[AEnemyBase] %s: No ShootingSound configured"), *GetName());
	}

	// Out of ammo sound - after firing last round
	if (CurrentAmmoInMag <= 1 && OutOfAmmoSound)
	{
		UGameplayStatics::PlaySoundAtLocation(GetWorld(), OutOfAmmoSound, GetActorLocation());
	}
}

void AEnemyBase::PlayShootAnimation()
{
	if (!ShootMontage)
	{
		// No montage - fire ShootEnd immediately
		OnShootEnd.Broadcast();
		return;
	}

	bIsPlayingShootMontage = true;

	// Play the montage
	PlayAnimMontage(ShootMontage, ShootAnimPlayRate);

	// Bind to montage end
	FOnMontageEnded MontageEndedDelegate;
	MontageEndedDelegate.BindUObject(this, &AEnemyBase::OnShootMontageEnded);
	GetMesh()->GetAnimInstance()->Montage_SetEndDelegate(MontageEndedDelegate, ShootMontage);

	// Also schedule a fallback timer in case the delegate doesn't fire
	if (UWorld* World = GetWorld())
	{
		float MontageLength = ShootMontage->GetPlayLength() / ShootAnimPlayRate;
		World->GetTimerManager().SetTimer(
			FireRateTimer,
			FTimerDelegate::CreateLambda([this]()
			{
				if (bIsPlayingShootMontage)
				{
					OnShootMontageEnded(nullptr, false);
				}
			}),
			MontageLength + 0.1f,
			false
		);
	}
}

void AEnemyBase::OnShootMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	bIsPlayingShootMontage = false;

	// Broadcast end event so Behavior Tree task can finish
	OnShootEnd.Broadcast();

	UE_LOG(LogTemp, Verbose, TEXT("[AEnemyBase] %s: Shoot montage ended (interrupted: %d)"),
		*GetName(), bInterrupted);
}

void AEnemyBase::StopShooting()
{
	bIsShooting = false;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(FireRateTimer);
		World->GetTimerManager().ClearTimer(BurstCooldownTimer);
	}

	// Stop current montage
	if (bIsPlayingShootMontage && ShootMontage)
	{
		StopAnimMontage(ShootMontage);
		bIsPlayingShootMontage = false;
	}

	// Broadcast end so BT doesn't hang
	OnShootEnd.Broadcast();
}

// ============================================================================
// Reload System
// ============================================================================

void AEnemyBase::Reload()
{
	if (bIsReloading)
	{
		return;
	}

	// Already full
	if (CurrentAmmoInMag >= MaxAmmoInMag)
	{
		return;
	}

	// No reserve ammo
	if (CurrentReserveAmmo <= 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[AEnemyBase] %s: No reserve ammo to reload!"), *GetName());
		OnShootEnd.Broadcast();
		return;
	}

	bIsReloading = true;

	// Stop shooting while reloading
	StopShooting();

	UE_LOG(LogTemp, Log, TEXT("[AEnemyBase] %s: Reloading..."), *GetName());

	// Play reload animation
	if (ReloadMontage)
	{
		PlayAnimMontage(ReloadMontage);
	}

	// Calculate reload time
	float ReloadTime = ReloadMontage ? ReloadMontage->GetPlayLength() : 2.0f;

	// Complete reload after animation time
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			ReloadTimer,
			FTimerDelegate::CreateLambda([this]()
			{
				const int32 AmmoNeeded = MaxAmmoInMag - CurrentAmmoInMag;
				const int32 AmmoToLoad = FMath::Min(AmmoNeeded, CurrentReserveAmmo);

				CurrentAmmoInMag += AmmoToLoad;
				CurrentReserveAmmo -= AmmoToLoad;
				bIsReloading = false;

				UE_LOG(LogTemp, Log, TEXT("[AEnemyBase] %s: Reload complete. Ammo: %d/%d"),
					*GetName(), CurrentAmmoInMag, CurrentReserveAmmo);

				// Resume shooting after reload
				OnShootEnd.Broadcast();
			}),
			ReloadTime,
			false
		);
	}
}
