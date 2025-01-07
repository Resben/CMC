#include "AdvCharacterMovementComponent.h"

#include "MaterialHLSLTree.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "Net/UnrealNetwork.h"
#include "DrawDebugHelpers.h"

#include "Engine/OverlapResult.h"

// Helper Macros
#if 1
float MacroDuration = 2.f;
#define SLOG(x) GEngine->AddOnScreenDebugMessage(-1, MacroDuration ? MacroDuration : -1.f, FColor::Yellow, x);
#define POINT(x, c) DrawDebugPoint(GetWorld(), x, 10, c, !MacroDuration, MacroDuration);
#define LINE(x1, x2, c) DrawDebugLine(GetWorld(), x1, x2, c, !MacroDuration, MacroDuration);
#define CAPSULE(x, c) DrawDebugCapsule(GetWorld(), x, CapHH(), CapR(), FQuat::Identity, c, !MacroDuration, MacroDuration);
#else
#define SLOG(x)
#define POINT(x, c)
#define LINE(x1, x2, c)
#define CAPSULE(x, c)
#endif

#pragma region Character Movement Component

UAdvCharacterMovementComponent::UAdvCharacterMovementComponent()
{
	NavAgentProps.bCanCrouch = true;
}

void UAdvCharacterMovementComponent::InitializeComponent()
{
	Super::InitializeComponent();

	AdvancedCharacterOwner = Cast<AAdvancedCharacter>(GetOwner());
}

FNetworkPredictionData_Client* UAdvCharacterMovementComponent::GetPredictionData_Client() const
{
	check(PawnOwner != nullptr)

	if (ClientPredictionData == nullptr)
	{
		UAdvCharacterMovementComponent* MutableThis = const_cast<UAdvCharacterMovementComponent*>(this);

		MutableThis->ClientPredictionData = new FNetworkPredictionData_Client_Adv(*this);
		MutableThis->ClientPredictionData->MaxSmoothNetUpdateDist = 92.0f;
		MutableThis->ClientPredictionData->NoSmoothNetUpdateDist = 140.0f;
	}

	return ClientPredictionData;
}

void UAdvCharacterMovementComponent::UpdateFromCompressedFlags(uint8 Flags)
{
	Super::UpdateFromCompressedFlags(Flags);

	Safe_bWantsToSprint = (Flags & FSavedMove_Adv::FLAG_Sprint) != 0;
	Safe_bWantsToDash = (Flags & FSavedMove_Adv::FLAG_Dash) != 0;
}

// After all the movement has been updated
void UAdvCharacterMovementComponent::OnMovementUpdated(float DeltaSeconds, const FVector& OldLocation, const FVector& OldVelocity)
{
	Super::OnMovementUpdated(DeltaSeconds, OldLocation, OldVelocity);

	// Switch back to walking after our root animation has finished
	// NOTE an example of NOT using TIME
	if (IsMovementMode(MOVE_Flying) && !HasRootMotionSources()) SetMovementMode(MOVE_Walking);
	
	// Set after movement so valid for the next frame
	Safe_bPrevWantsToCrouch = bWantsToCrouch;
}

void UAdvCharacterMovementComponent::UpdateCharacterStateBeforeMovement(float DeltaSeconds)
{
	// -- SLIDE -- //
	// We need to do this before the crouch update gets to happen that's why its in this function
	// !bWantsToCrouch will be false on the second press while we compare this to the previous safe crouch
	// So a double crouch will result in a slide
	if (MovementMode == MOVE_Walking && UnSafe_bWantsToSlide)
	{
		if (CanEnterSlide())
		{
			bWantsToCrouch = false;
			SetMovementMode(MOVE_Custom, CMOVE_Slide);
		}
	}
	else if (IsCustomMovementMode(CMOVE_Slide) && ShouldExitSlide())
	{
		SetMovementMode(MOVE_Walking);
	}
	else if (IsFalling())
	{
		if (TryClimb()) SLOG("Climbing now")
	}
	else if ((IsClimbing() || IsHanging()) && bWantsToCrouch)
	{
		SetMovementMode(MOVE_Falling);
		bWantsToCrouch = false;
	}

	// -- DASH -- //
	// On the server but not the servers controlled character
	bool bAuthProxy = CharacterOwner->HasAuthority() && !CharacterOwner->IsLocallyControlled();
	if (Safe_bWantsToDash && CanDash())
	{
		if (!bAuthProxy || GetWorld()->GetTimeSeconds() - DashStartTime > Dash_AuthCooldownDuration)
		{
			PerformDash();
			Safe_bWantsToDash = false;
			Proxy_bDashStart = !Proxy_bDashStart;
		}
		else
		{
			// The dash auth cooldown was too great was this player cheating???
			UE_LOG(LogTemp, Warning, TEXT("Client tried to cheat"))
		}
	}

	if (AdvancedCharacterOwner->bPressedAdvancedJump)
	{
		if (TryMantle())
		{
			AdvancedCharacterOwner->StopJumping();
		}
		else if (TryHang())
		{
			AdvancedCharacterOwner->StopJumping();
		}
		else
		{
			AdvancedCharacterOwner->bPressedAdvancedJump = false;
			CharacterOwner->bPressedJump = true;
			CharacterOwner->CheckJumpInput(DeltaSeconds);
			bOrientRotationToMovement = true;
		}
	}

	// Transition
	if (Safe_bTransitionFinished)
	{
		SLOG("Transition finished")
		UE_LOG(LogTemp, Warning, TEXT("FINISHED RM"))
		if (TransitionName == "Mantle")
		{
			if (IsValid(TransitionQueuedMontage))
			{
				SetMovementMode(MOVE_Flying);

				// @todo is this the best way?
				if (UCapsuleComponent* CapsuleComp = CharacterOwner->GetCapsuleComponent())
				{
					CapsuleComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				}
				
				CharacterOwner->PlayAnimMontage(TransitionQueuedMontage, TransitionQueuedMontageSpeed);
				if (UAnimInstance* AnimInstance = CharacterOwner->GetMesh()->GetAnimInstance())
				{
					AnimInstance->OnMontageEnded.AddDynamic(this, &UAdvCharacterMovementComponent::OnMontageEnded);
				}
				
				TransitionQueuedMontageSpeed = 0.0f;
				TransitionQueuedMontage = nullptr;
			}
			else
			{
				SetMovementMode(MOVE_Walking);
			}	
		}
		else if (TransitionName == "Hang")
		{
			SetMovementMode(MOVE_Custom, CMOVE_Hang);
			Velocity = FVector::ZeroVector;
		}

		TransitionName = "";
		Safe_bTransitionFinished = false;
	}

	// -- WALL RUN -- //
	if (IsFalling())
	{
		TryWallRun();
	}

	// Look here for more info on how the engine handles crouching
	Super::UpdateCharacterStateBeforeMovement(DeltaSeconds);
}

void UAdvCharacterMovementComponent::UpdateCharacterStateAfterMovement(float DeltaSeconds)
{
	Super::UpdateCharacterStateAfterMovement(DeltaSeconds);

	// After mantle finished go back to Walking
	if (!HasAnimRootMotion() && Safe_bHadAnimRootMotion && IsMovementMode(MOVE_Flying))
	{
		UE_LOG(LogTemp, Warning, TEXT("Ending Anim Root Motion"))
		SetMovementMode(MOVE_Walking);
	}
	// You can't check for ERootMontionSourceStatusFlags::Finished in UpdateCharacterStateBeforeMovement since RootMotion is cleaned up before that function is called
	// So we save that in the Safe variable Safe_bTransitionFinished
	if (GetRootMotionSourceByID(TransitionRMS_ID) && GetRootMotionSourceByID(TransitionRMS_ID)->Status.HasFlag(ERootMotionSourceStatusFlags::Finished))
	{
		RemoveRootMotionSourceByID(TransitionRMS_ID);
		Safe_bTransitionFinished = true;
	}

	Safe_bHadAnimRootMotion = HasAnimRootMotion();
}

void UAdvCharacterMovementComponent::PhysCustom(float deltaTime, int32 Iterations)
{
	Super::PhysCustom(deltaTime, Iterations);

	switch (CustomMovementMode)
	{
	case CMOVE_Slide:
		PhysSlide(deltaTime, Iterations);
		break;
	case CMOVE_WallRun:
		PhysWallRun(deltaTime, Iterations);
		break;
	case CMOVE_Hang:
		break;
	case CMOVE_Climb:
		PhysClimb(deltaTime, Iterations);
		break;
	default:
		UE_LOG(LogTemp, Fatal, TEXT("Invalid Movement Mode"))
	}
}

void UAdvCharacterMovementComponent::OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode)
{
	Super::OnMovementModeChanged(PreviousMovementMode, PreviousCustomMode);

	if (PreviousMovementMode == MOVE_Custom && PreviousCustomMode == CMOVE_Slide) ExitSlide();
	
	if (IsCustomMovementMode(CMOVE_Slide)) EnterSlide(PreviousMovementMode, (ECustomMovementMode)PreviousMovementMode);

	if (IsCustomMovementMode(CMOVE_Hang)) SLOG("Switched to hang")

	if (MovementMode == MOVE_Walking) Safe_bCanClimbAgain = true;
	
	if (IsFalling())
	{
		bOrientRotationToMovement = true;
	}
	
	// Simulated proxies will get OnMovementModeChanged triggered because custom movement mode is a replicated variable
	// So instead do your own calculations to avoid using unnecessary bandwidth
	if (IsWallRunning() && GetOwnerRole() == ROLE_SimulatedProxy)
	{
		FVector Start = UpdatedComponent->GetComponentLocation();
		FVector End = Start + UpdatedComponent->GetRightVector() * CapR() * 2;
		auto Params = AdvancedCharacterOwner->GetIgnoreCharacterParams();
		FHitResult WallHit;
		Safe_bWallRunIsRight = GetWorld()->LineTraceSingleByProfile(WallHit, Start, End, "BlockAll", Params);
	}

	OnStateChangedDelegate.Broadcast();
}

bool UAdvCharacterMovementComponent::IsMovingOnGround() const
{
	return Super::IsMovingOnGround() || IsCustomMovementMode(CMOVE_Slide);
}

bool UAdvCharacterMovementComponent::CanCrouchInCurrentState() const
{
	// Prevents the player from crouching in the air
	// Otherwise we need to rewrite the crouch mechanic from scratch
	return Super::CanCrouchInCurrentState() && IsMovingOnGround();
}

float UAdvCharacterMovementComponent::GetMaxSpeed() const
{
	if (IsMovementMode(MOVE_Walking) && Safe_bWantsToSprint && !IsCrouching()) return Sprint_MaxSpeed;

	if (MovementMode != MOVE_Custom) return Super::GetMaxSpeed();
	
	switch (CustomMovementMode)
	{
	case CMOVE_Slide:
		return Slide_MaxSpeed;
	case CMOVE_WallRun:
		return WallRun_MaxSpeed;
	case CMOVE_Hang:
		return 0.0f;
	case CMOVE_Climb:
		return Climb_MaxSpeed;
	default:
		UE_LOG(LogTemp, Fatal, TEXT("Invalid Movement Mode"))
		return -1.0f;
	}
}

float UAdvCharacterMovementComponent::GetMaxBrakingDeceleration() const
{
	if (MovementMode != MOVE_Custom) return Super::GetMaxBrakingDeceleration();

	switch (CustomMovementMode)
	{
	case CMOVE_Slide:
		return Slide_MaxBrakingDeceleration;
	case CMOVE_WallRun:
		return 0.0f;
	case CMOVE_Hang:
		return 0.0f;
	case CMOVE_Climb:
		return Climb_BrakingDeceleration;
	default:
		UE_LOG(LogTemp, Fatal, TEXT("Invalid Movement Mode"))
		return -1.0f;
	}
}

bool UAdvCharacterMovementComponent::CanAttemptJump() const
{
	return Super::CanAttemptJump() || IsWallRunning() || IsClimbing() || IsHanging() || IsSliding();
}

bool UAdvCharacterMovementComponent::DoJump(bool bReplayingMoves)
{
	// DoJump calls SetMovementMode(MOVE_Falling) so store it here
	bool bWasWallRunning = IsWallRunning();
	bool bWasOnWall = IsHanging() || IsClimbing();
	
	if (Super::DoJump(bReplayingMoves))
	{
		if (bWasWallRunning)
		{
			// Determine which side of the wall we are jumping off
			// Jump off wall based off normal
			FVector Start = UpdatedComponent->GetComponentLocation();
			FVector CastDelta = UpdatedComponent->GetRightVector() * CapR() * 2;
			FVector End = Safe_bWallRunIsRight ? Start + CastDelta : Start - CastDelta;
			auto Params = AdvancedCharacterOwner->GetIgnoreCharacterParams();
			FHitResult WallHit;
			GetWorld()->LineTraceSingleByProfile(WallHit, Start, End, "BlockAll", Params);
			Velocity += WallHit.Normal * WallRun_JumpOffForce;
		}
		else if (bWasOnWall)
		{
			// Only play the montage if there was no de-sync
			if (!bReplayingMoves)
			{
				CharacterOwner->PlayAnimMontage(Hang_WallJumpMontage);
			}
			Velocity += FVector::UpVector * Hang_WallJumpForce * 0.5f;
			Velocity += Acceleration.GetSafeNormal2D() * Hang_WallJumpForce * 0.5f;
		}
		
		return true;
	}
	return false;
}

#pragma endregion Character Movement Component

#pragma region Save Move

UAdvCharacterMovementComponent::FSavedMove_Adv::FSavedMove_Adv()
{
	Saved_bWantsToSprint = 0;
	Saved_bPrevWantsToCrouch = 0;
}

bool UAdvCharacterMovementComponent::FSavedMove_Adv::CanCombineWith(const FSavedMovePtr& newMove, ACharacter* InCharacter, float MaxDelta) const
{
	FSavedMove_Adv* NewAdvMove = static_cast<FSavedMove_Adv*>(newMove.Get());

	if (Saved_bWantsToSprint != NewAdvMove->Saved_bWantsToSprint)
	{
		return false;
	}

	if (Saved_bWantsToDash != NewAdvMove->Saved_bWantsToDash)
	{
		return false;
	}

	if (Saved_bWallRunIsRight != NewAdvMove->Saved_bWallRunIsRight)
	{
		return false;
	}

	if (Saved_bCanClimbAgain != NewAdvMove->Saved_bCanClimbAgain)
	{
		return false;
	}
	
	return FSavedMove_Character::CanCombineWith(newMove, InCharacter, MaxDelta);
}

void UAdvCharacterMovementComponent::FSavedMove_Adv::Clear()
{
	FSavedMove_Character::Clear();

	Saved_bWantsToSprint = 0;
	Saved_bWantsToDash = 0;
	Saved_bPressedAdvanceJump = 0;
	
	Saved_bHadAnimRootMotion = 0;
	Saved_bTransitionFinished = 0;
	
	Saved_bWantsToProne = 0;
	Saved_bPrevWantsToCrouch = 0;

	Saved_bWallRunIsRight = 0;

	Saved_bCanClimbAgain = 0;
}

uint8 UAdvCharacterMovementComponent::FSavedMove_Adv::GetCompressedFlags() const
{
	uint8 Result = Super::GetCompressedFlags();

	if (Saved_bWantsToSprint) Result |= FLAG_Sprint;
	if (Saved_bWantsToDash) Result |= FLAG_Dash;
	if (Saved_bPressedAdvanceJump) Result |= FLAG_JumpPressed;

	return Result;
}

void UAdvCharacterMovementComponent::FSavedMove_Adv::SetMoveFor(ACharacter* C, float InDeltaTime, FVector const& NewAccel, FNetworkPredictionData_Client_Character& ClientData)
{
	FSavedMove_Character::SetMoveFor(C, InDeltaTime, NewAccel, ClientData);

	UAdvCharacterMovementComponent* CharacterMovement = Cast<UAdvCharacterMovementComponent>(C->GetCharacterMovement());

	Saved_bWantsToSprint = CharacterMovement->Safe_bWantsToSprint;
	Saved_bPrevWantsToCrouch = CharacterMovement->Safe_bPrevWantsToCrouch;
	Saved_bWantsToProne = CharacterMovement->Safe_bWantsToProne;
	Saved_bWantsToDash = CharacterMovement->Safe_bWantsToDash;
	Saved_bWallRunIsRight = CharacterMovement->Safe_bWallRunIsRight;

	Saved_bPressedAdvanceJump = CharacterMovement->AdvancedCharacterOwner->bPressedAdvancedJump;
	Saved_bHadAnimRootMotion = CharacterMovement->Safe_bHadAnimRootMotion;
	Saved_bTransitionFinished = CharacterMovement->Safe_bTransitionFinished;

	Saved_bCanClimbAgain = CharacterMovement->Safe_bCanClimbAgain;
}

void UAdvCharacterMovementComponent::FSavedMove_Adv::PrepMoveFor(ACharacter* C)
{
	Super::PrepMoveFor(C);

	UAdvCharacterMovementComponent* CharacterMovement = Cast<UAdvCharacterMovementComponent>(C->GetCharacterMovement());

	CharacterMovement->Safe_bWantsToSprint = Saved_bWantsToSprint;
	CharacterMovement->Safe_bPrevWantsToCrouch = Saved_bPrevWantsToCrouch;
	CharacterMovement->Safe_bWantsToProne = Saved_bWantsToProne;
	CharacterMovement->Safe_bWantsToDash = Saved_bWantsToDash;
	CharacterMovement->Safe_bWallRunIsRight = Saved_bWallRunIsRight;

	CharacterMovement->AdvancedCharacterOwner->bPressedAdvancedJump = Saved_bPressedAdvanceJump;
	CharacterMovement->Safe_bHadAnimRootMotion = Saved_bHadAnimRootMotion;
	CharacterMovement->Safe_bTransitionFinished = Saved_bTransitionFinished;

	CharacterMovement->Safe_bCanClimbAgain = Saved_bCanClimbAgain;
}

#pragma endregion Save Move

#pragma region Network Prediction Data

UAdvCharacterMovementComponent::FNetworkPredictionData_Client_Adv::FNetworkPredictionData_Client_Adv(const UCharacterMovementComponent& ClientMovement)
	: Super(ClientMovement)
{
}

FSavedMovePtr UAdvCharacterMovementComponent::FNetworkPredictionData_Client_Adv::AllocateNewMove()
{
	return FSavedMovePtr(new FSavedMove_Adv());
}

#pragma endregion Network Prediction Data

#pragma region Blueprints

void UAdvCharacterMovementComponent::SprintPressed()
{
	Safe_bWantsToSprint = true;
}

void UAdvCharacterMovementComponent::SprintReleased()
{
	Safe_bWantsToSprint = false;
}

void UAdvCharacterMovementComponent::CrouchPressed()
{
	bWantsToCrouch = true;
	UnSafe_bWantsToSlide = true;
}

void UAdvCharacterMovementComponent::CrouchReleased()
{
	bWantsToCrouch = false;
	UnSafe_bWantsToSlide = false;
}

void UAdvCharacterMovementComponent::DashPressed()
{
	float CurrentTime = GetWorld()->GetTimeSeconds();
	if (CurrentTime - DashStartTime >= Dash_CooldownDuration)
	{
		// Enough time has passed to dash again
		Safe_bWantsToDash = true;
	}
	else
	{
		// If you are holding the dash then dash when remaining time is over by calling OnDashCooldownFinished
		// Cleared if you release before the cooldown finishes
		GetWorld()->GetTimerManager().SetTimer(TimerHandle_DashCooldown, this, &UAdvCharacterMovementComponent::OnDashCooldownFinished, Dash_CooldownDuration - (CurrentTime - DashStartTime));
	}
}

void UAdvCharacterMovementComponent::DashReleased()
{
	// If we release the key before the cooldown timer then clear timer
	// We still have DashStartTime to track the next press
	// Timer is used if we are holding the press and want to dash ASAP
	GetWorld()->GetTimerManager().ClearTimer(TimerHandle_DashCooldown);
	Safe_bWantsToDash = false;
}

bool UAdvCharacterMovementComponent::IsCustomMovementMode(ECustomMovementMode InCustomMovementMode) const
{
	return MovementMode == MOVE_Custom && CustomMovementMode == InCustomMovementMode;
}

bool UAdvCharacterMovementComponent::IsMovementMode(EMovementMode InMovementMode) const
{
	return InMovementMode == MovementMode;
}

#pragma endregion Blueprints

#pragma region Helpers

bool UAdvCharacterMovementComponent::IsServer() const
{
	return CharacterOwner->HasAuthority();
}

float UAdvCharacterMovementComponent::CapR() const
{
	return CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleRadius();
}

float UAdvCharacterMovementComponent::CapHH() const
{
	return CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
}

void UAdvCharacterMovementComponent::OnMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	if (UCapsuleComponent* CapsuleComp = CharacterOwner->GetCapsuleComponent())
	{
		CapsuleComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	}
	
	if (UAnimInstance* AnimInstance = CharacterOwner->GetMesh()->GetAnimInstance())
	{
		AnimInstance->OnMontageEnded.RemoveDynamic(this, &UAdvCharacterMovementComponent::OnMontageEnded);
	}
}

#pragma endregion Helpers

#pragma region Slide

void UAdvCharacterMovementComponent::EnterSlide(EMovementMode PrevMode, ECustomMovementMode PrevCustomMode)
{
	HandleCustomCrouch();
	bOrientRotationToMovement = false;
	//Velocity += Velocity.GetSafeNormal2D() * Slide_EnterImpulse; // Check last move and maybe we can add a velocity boost or a boost based on current velocity

	FindFloor(UpdatedComponent->GetComponentLocation(), CurrentFloor, true, nullptr);
}

// ExitSlide is a SAFE movement mode so we can call SafeMoveUpdatedComponent
void UAdvCharacterMovementComponent::ExitSlide()
{
	HandleCustomUnCrouch();
	bOrientRotationToMovement = true;
	SLOG("ExitSlide")
}

void UAdvCharacterMovementComponent::ExitSlideMode()
{
	SLOG("ExitSlideMode")
	HandleCustomUnCrouch();
	SetMovementMode(MOVE_Walking);
}

/// @todo make this network safe then replace native crouch mechanics
void UAdvCharacterMovementComponent::HandleCustomCrouch()
{
	if (!HasValidData())
	{
		return;
	}

	// Change collision size to crouching dimensions
	const float ComponentScale = CharacterOwner->GetCapsuleComponent()->GetShapeScale();
	const float OldUnscaledHalfHeight = CharacterOwner->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
	const float OldUnscaledRadius = CharacterOwner->GetCapsuleComponent()->GetUnscaledCapsuleRadius();
	// Height is not allowed to be smaller than radius.
	const float ClampedCrouchedHalfHeight = FMath::Max3(0.f, OldUnscaledRadius, CrouchedHalfHeight);
	CharacterOwner->GetCapsuleComponent()->SetCapsuleSize(OldUnscaledRadius, ClampedCrouchedHalfHeight);
	float HalfHeightAdjust = (OldUnscaledHalfHeight - ClampedCrouchedHalfHeight);
	float ScaledHalfHeightAdjust = HalfHeightAdjust * ComponentScale;
	
	// Crouching to a larger height? (this is rare)
	if (ClampedCrouchedHalfHeight > OldUnscaledHalfHeight)
	{
		FCollisionQueryParams CapsuleParams(SCENE_QUERY_STAT(CrouchTrace), false, CharacterOwner);
		FCollisionResponseParams ResponseParam;
		InitCollisionParams(CapsuleParams, ResponseParam);
		const bool bEncroached = GetWorld()->OverlapBlockingTestByChannel(UpdatedComponent->GetComponentLocation() + ScaledHalfHeightAdjust * GetGravityDirection(), GetWorldToGravityTransform(),
			UpdatedComponent->GetCollisionObjectType(), GetPawnCapsuleCollisionShape(SHRINK_None), CapsuleParams, ResponseParam);

		// If encroached, cancel
		if( bEncroached )
		{
			CharacterOwner->GetCapsuleComponent()->SetCapsuleSize(OldUnscaledRadius, OldUnscaledHalfHeight);
			return;
		}
	}
	
	// Intentionally not using MoveUpdatedComponent, where a horizontal plane constraint would prevent the base of the capsule from staying at the same spot.
	UpdatedComponent->MoveComponent(ScaledHalfHeightAdjust * GetGravityDirection(), UpdatedComponent->GetComponentQuat(), true, nullptr, EMoveComponentFlags::MOVECOMP_NoFlags, ETeleportType::TeleportPhysics);
	
	bForceNextFloorCheck = true;

	// OnStartCrouch takes the change from the Default size, not the current one (though they are usually the same).
	ACharacter* DefaultCharacter = CharacterOwner->GetClass()->GetDefaultObject<ACharacter>();
	HalfHeightAdjust = (DefaultCharacter->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() - ClampedCrouchedHalfHeight);
	ScaledHalfHeightAdjust = HalfHeightAdjust * ComponentScale;

	CharacterOwner->OnStartCrouch( HalfHeightAdjust, ScaledHalfHeightAdjust );
}

void UAdvCharacterMovementComponent::HandleCustomUnCrouch()
{
	if (!HasValidData())
	{
		return;
	}

	ACharacter* DefaultCharacter = CharacterOwner->GetClass()->GetDefaultObject<ACharacter>();

	const float CurrentCrouchedHalfHeight = CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();

	const float ComponentScale = CharacterOwner->GetCapsuleComponent()->GetShapeScale();
	const float OldUnscaledHalfHeight = CharacterOwner->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
	const float HalfHeightAdjust = DefaultCharacter->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() - OldUnscaledHalfHeight;
	const float ScaledHalfHeightAdjust = HalfHeightAdjust * ComponentScale;
	const FVector PawnLocation = UpdatedComponent->GetComponentLocation();

	// Grow to uncrouched size.
	check(CharacterOwner->GetCapsuleComponent());
	
	// Try to stay in place and see if the larger capsule fits. We use a slightly taller capsule to avoid penetration.
	const UWorld* MyWorld = GetWorld();
	const float SweepInflation = UE_KINDA_SMALL_NUMBER * 10.f;
	FCollisionQueryParams CapsuleParams(SCENE_QUERY_STAT(CrouchTrace), false, CharacterOwner);
	FCollisionResponseParams ResponseParam;
	InitCollisionParams(CapsuleParams, ResponseParam);

	// Compensate for the difference between current capsule size and standing size
	const FCollisionShape StandingCapsuleShape = GetPawnCapsuleCollisionShape(SHRINK_HeightCustom, -SweepInflation - ScaledHalfHeightAdjust); // Shrink by negative amount, so actually grow it.
	const ECollisionChannel CollisionChannel = UpdatedComponent->GetCollisionObjectType();
	bool bEncroached = true;
	
	// Expand while keeping base location the same.
	FVector StandingLocation = PawnLocation + (StandingCapsuleShape.GetCapsuleHalfHeight() - CurrentCrouchedHalfHeight) * -GetGravityDirection();
	bEncroached = MyWorld->OverlapBlockingTestByChannel(StandingLocation, GetWorldToGravityTransform(), CollisionChannel, StandingCapsuleShape, CapsuleParams, ResponseParam);

	if (bEncroached)
	{
		if (IsMovingOnGround())
		{
			// Something might be just barely overhead, try moving down closer to the floor to avoid it.
			const float MinFloorDist = UE_KINDA_SMALL_NUMBER * 10.f;
			if (CurrentFloor.bBlockingHit && CurrentFloor.FloorDist > MinFloorDist)
			{
				StandingLocation -= (CurrentFloor.FloorDist - MinFloorDist) * -GetGravityDirection();
				bEncroached = MyWorld->OverlapBlockingTestByChannel(StandingLocation, GetWorldToGravityTransform(), CollisionChannel, StandingCapsuleShape, CapsuleParams, ResponseParam);
			}
		}				
	}

	if (!bEncroached)
	{
		// Commit the change in location.
		UpdatedComponent->MoveComponent(StandingLocation - PawnLocation, UpdatedComponent->GetComponentQuat(),false, nullptr, EMoveComponentFlags::MOVECOMP_NoFlags,ETeleportType::TeleportPhysics);
		bForceNextFloorCheck = true;
	}

	// If still encroached then abort.
	if (bEncroached)
	{
		return;
	}
	
	// Now call SetCapsuleSize() to cause touch/untouch events and actually grow the capsule
	CharacterOwner->GetCapsuleComponent()->SetCapsuleSize(DefaultCharacter->GetCapsuleComponent()->GetUnscaledCapsuleRadius(), DefaultCharacter->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight(), true);
	CharacterOwner->OnEndCrouch( HalfHeightAdjust, ScaledHalfHeightAdjust );
}

bool UAdvCharacterMovementComponent::CanEnterSlide() const
{
	FVector Start = UpdatedComponent->GetComponentLocation();
	FVector End = Start + CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * 2.5f * FVector::DownVector;
	FName ProfileName = TEXT("BlockAll");
	bool bValidSurface = GetWorld()->LineTraceTestByProfile(Start, End, ProfileName, AdvancedCharacterOwner->GetIgnoreCharacterParams());
	bool bEnoughSpeed = Velocity.SizeSquared() > pow(Slide_MinEnterSpeed, 2);
	
	return bValidSurface && bEnoughSpeed;
}

bool UAdvCharacterMovementComponent::ShouldExitSlide() const
{
	FVector Start = UpdatedComponent->GetComponentLocation();
	FVector End = Start + CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * 2.5f * FVector::DownVector;
	FName ProfileName = TEXT("BlockAll");
	bool bValidSurface = GetWorld()->LineTraceTestByProfile(Start, End, ProfileName, AdvancedCharacterOwner->GetIgnoreCharacterParams());
	bool bEnoughSpeed = Velocity.SizeSquared() < pow(Slide_MinExitSpeed, 2);
	
	return (bValidSurface && bEnoughSpeed) || !UnSafe_bWantsToSlide;
}

void UAdvCharacterMovementComponent::PhysSlide(float deltaTime, int32 Iterations)
{
	// Source code boilerplate stuff
	if (deltaTime < MIN_TICK_TIME)
	{
		return;
	}

	if (ShouldExitSlide())
	{
		SetMovementMode(MOVE_Walking);
		StartNewPhysics(deltaTime, Iterations);
		return;
	}

	bJustTeleported = false;
	bool bCheckedFall = false;
	bool bTriedLedgeMove = false;
	float remainingTime = deltaTime;
	
	while ( (remainingTime >= MIN_TICK_TIME) && (Iterations < MaxSimulationIterations) && CharacterOwner && (CharacterOwner->Controller || bRunPhysicsWithNoController || (CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy)) )
	{
		// Setup sub-stepping
		Iterations++;
		bJustTeleported = false;
		const float timeTick = GetSimulationTimeStep(remainingTime, Iterations);
		remainingTime -= timeTick;

		// Old values
		UPrimitiveComponent * const OldBase = GetMovementBase();
		const FVector PreviousBaseLocation = (OldBase != nullptr) ? OldBase->GetComponentLocation() : FVector::ZeroVector;
		const FVector OldLocation = UpdatedComponent->GetComponentLocation();
		const FFindFloorResult OldFloor = CurrentFloor;

		// Ensure velocity is horizontal
		MaintainHorizontalGroundVelocity();
		const FVector OldVelocity = Velocity;

		// Calculate a slope force for going down slopes
		FVector SlopeForce = CurrentFloor.HitResult.Normal;
		SlopeForce.Z = 0.0f;
		Velocity += SlopeForce * Slide_GravityForce * deltaTime;

		Acceleration = Acceleration.ProjectOnTo(UpdatedComponent->GetRightVector().GetSafeNormal2D());

		float CurrentSpeed = AdvancedCharacterOwner->GetVelocity().Size();
		float NormalizedSpeed = FMath::Clamp(CurrentSpeed / GetMaxSpeed(), 0.0f, 1.0f);
		
		// bFluid -> friction is applied more instead apply your own Slide_FrictionFactor
		CalcVelocity(timeTick, GroundFriction * Slide_FrictionCurveFactor->GetFloatValue(NormalizedSpeed), false, GetMaxBrakingDeceleration());

		// Move parameters
		const FVector MoveVelocity = Velocity;
		const FVector Delta = timeTick * MoveVelocity;
		const bool bZeroDelta = Delta.IsNearlyZero();
		FStepDownResult StepDownResult;
		bool bFloorWalkable = CurrentFloor.IsWalkableFloor();

		if (bZeroDelta)
		{
			remainingTime = 0.0f;
		}
		else
		{
			// Try to move forward
			MoveAlongFloor(MoveVelocity, timeTick, &StepDownResult);

			if (IsFalling())
			{
				// Pawn decided to jump up
				const float DesiredDist = Delta.Size();
				if (DesiredDist > KINDA_SMALL_NUMBER)
				{
					const float ActualDist = (UpdatedComponent->GetComponentLocation() - OldLocation).Size2D();
					remainingTime += timeTick * (1.0f - FMath::Min(1.0f, ActualDist / DesiredDist));
				}
				StartNewPhysics(remainingTime, Iterations);
				return;
			}
			else if (IsSwimming())
			{
				StartSwimming(OldLocation, OldVelocity, timeTick, remainingTime, Iterations);
				return;
			}
		}

		// Update floor
		// StepUp might have already done it for us
		if (StepDownResult.bComputedFloor)
		{
			CurrentFloor = StepDownResult.FloorResult;
		}
		else
		{
			FindFloor(UpdatedComponent->GetComponentLocation(), CurrentFloor, bZeroDelta, nullptr);
		}

		// Check ledge edges here
		if (!CurrentFloor.IsWalkableFloor())
		{
			bool bMustJump = bZeroDelta || (OldBase == nullptr || (!OldBase->IsQueryCollisionEnabled() && MovementBaseUtility::IsDynamicBase(OldBase)));
    
			// Initiate falling while maintaining current velocity
			if (CheckFall(OldFloor, CurrentFloor.HitResult, Delta, OldLocation, remainingTime, timeTick, Iterations, bMustJump))
			{
				return;
			}
		}
		else
		{
			// Validate the floor check
			if (CurrentFloor.IsWalkableFloor())
			{
				if (ShouldCatchAir(OldFloor, CurrentFloor))
				{
					HandleWalkingOffLedge(OldFloor.HitResult.ImpactNormal, OldFloor.HitResult.Normal, OldLocation, timeTick);
					if (IsMovingOnGround())
					{
						SLOG("Start Falling Here")
						// If still walking, then fall. If not assume the user set a different mode they want to keep.
						StartFalling(Iterations, remainingTime, timeTick, Delta, OldLocation);
					}
					return;
				}

				AdjustFloorHeight();
				SetBase(CurrentFloor.HitResult.Component.Get(), CurrentFloor.HitResult.BoneName);
			}
			else if (CurrentFloor.HitResult.bStartPenetrating && remainingTime <= 0.0f)
			{
				// The floor check failed because it started in penetration
				// We do not want to try to move downward because the downward sweep failed, rather we'd like to try pop out of the floor
				FHitResult Hit(CurrentFloor.HitResult);
				Hit.TraceEnd = Hit.TraceStart + FVector(0.0f, 0.0f, MAX_FLOOR_DIST);
				const FVector RequestedAdjustment = GetPenetrationAdjustment(Hit);
				ResolvePenetration(RequestedAdjustment, Hit, UpdatedComponent->GetComponentQuat());
				bForceNextFloorCheck = true;
			}

			if (IsSwimming())
			{
				StartSwimming(OldLocation, Velocity, timeTick, remainingTime, Iterations);
				return;
			}

			// See if we need to start falling
			if (!CurrentFloor.IsWalkableFloor() && !CurrentFloor.HitResult.bStartPenetrating)
			{
				const bool bMustJump = bJustTeleported || bZeroDelta || (OldBase == nullptr || (!OldBase->IsQueryCollisionEnabled() && MovementBaseUtility::IsDynamicBase(OldBase)));
				if ((bMustJump || bCheckedFall) && CheckFall(OldFloor, CurrentFloor.HitResult, Delta, OldLocation, remainingTime, timeTick, Iterations, bMustJump))
				{
					return;
				}
				bCheckedFall = true;
			}
		}

		// Allow overlap events and such to change physics state and velocity
		if (IsMovingOnGround() && bFloorWalkable)
		{
			// Make velocity reflect actual move
			if (!bJustTeleported && !HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity() && timeTick >= MIN_TICK_TIME)
			{
				Velocity = (UpdatedComponent->GetComponentLocation() - OldLocation) / timeTick;
				MaintainHorizontalGroundVelocity();
			}
		}

		// If we didn't move at all this iteration then abort (since further iterations will be stuck
		if (UpdatedComponent->GetComponentLocation() == OldLocation)
		{
			remainingTime = 0.0f;
			break;
		}
	}
	
	FHitResult Hit;
	FQuat NewRotation = FRotationMatrix::MakeFromXZ(Velocity.GetSafeNormal2D(), FVector::UpVector).ToQuat();
	SafeMoveUpdatedComponent(FVector::ZeroVector, NewRotation, false, Hit);
}

#pragma endregion Slide

#pragma region Dash

void UAdvCharacterMovementComponent::OnDashCooldownFinished()
{
	Safe_bWantsToDash = true;
}

bool UAdvCharacterMovementComponent::CanDash() const
{
	return IsWalking() && !IsCrouching() || IsFalling();
}


void UAdvCharacterMovementComponent::PerformDash()
{
	DashStartTime = GetWorld()->GetTimeSeconds();
	
	if (Setting_GravityEnabledDash) SetMovementMode(MOVE_Falling);
	else SetMovementMode(MOVE_Flying);
	
	CharacterOwner->PlayAnimMontage(Dash_Montage);
	DashStartDelegate.Broadcast();
}

#pragma endregion Dash

#pragma region Mantle

bool UAdvCharacterMovementComponent::TryMantle()
{
	// Conditions for allowing the Mantle
	if (!(IsMovementMode(MOVE_Walking) && !IsCrouching()) && !IsMovementMode(MOVE_Falling) && !IsCustomMovementMode(CMOVE_Climb)) return false;

	// Helper variables

	// Get the bottom of the capsule
	FVector BaseLoc = UpdatedComponent->GetComponentLocation() + FVector::DownVector * CapHH();
	FVector Fwd = UpdatedComponent->GetForwardVector().GetSafeNormal2D();
	auto Params = AdvancedCharacterOwner->GetIgnoreCharacterParams();
	float MaxHeight = Mantle_MaxClimbHeight; // Assuming this has the largest value
	// Minimum steepness we are going to tolerate
	float CosMMWSA = FMath::Cos(FMath::DegreesToRadians(Mantle_MinWallSteepnessAngle));
	float CosMMSA = FMath::Cos(FMath::DegreesToRadians(Mantle_MaxSurfaceAngle));
	// Max alignment of player to wall to mantle
	float CosMMAA = FMath::Cos(FMath::DegreesToRadians(Mantle_MaxAlignmentAngle));

	SLOG("Starting Mantle Attempt")

	int numberOfLineTraces = 5;

	// ---- FRONT TRACE ---- //
	// Check the front face (Wall that is in front of you)
	FHitResult FrontHit;
	// We want a longer check distance if the character has velocity toward the forward vector (which is the direction we check the wall for)
	// And a shorter check distance if the character velocity is in the opposite direction of the forward vector
	// We clamp this check distance to CapR + 30 minimum and Mantle_MaxDistance maximum based on the Velocity | Fwd
	float CheckDistance = FMath::Clamp(Velocity | Fwd, CapR() + 30, Mantle_MaxDistance);
	// We want to mantle it if it is above the max step height so we start how checks from here up
	FVector FrontStart = BaseLoc + FVector::UpVector * (Mantle_MinShortClimbHeight - 1);
	for (int i = 0; i < numberOfLineTraces + 1; i++)
	{
		LINE(FrontStart, FrontStart + Fwd * CheckDistance, FColor::Red)
		if (GetWorld()->LineTraceSingleByProfile(FrontHit, FrontStart, FrontStart + Fwd * CheckDistance, "BlockAll", Params)) break;
		FrontStart += FVector::UpVector * (2.0f * CapHH() - (Mantle_MinShortClimbHeight - 1)) / numberOfLineTraces;
	}
	if (!FrontHit.IsValidBlockingHit()) return false;
	// Get the steepness of the Normal of the hit
	float CosWallSteepnessAngle = FrontHit.Normal | FVector::UpVector;
	// First check is for the steepness of the wall the second check is for the angle of the wall to the player i.e. how close is your fwd to being perpendicular to the wall
	// So we get the minus of the hit (so about the same direction as fwd)
	if (FMath::Abs(CosWallSteepnessAngle) > CosMMWSA || (Fwd | -FrontHit.Normal) < CosMMAA) return false;
	POINT(FrontHit.Location, FColor::Red);

	// ---- TOP TRACE ---- //
	TArray<FHitResult> HeightHits;
	FHitResult SurfaceHit;
	// The point in which we want to get to in order to mantle may not be directly up in the case in which the wall has some slant
	// Therefor we must project the world up vector onto the surface normal and there we can follow it up to the top of this plane to get
	// where we want to mantle onto (@ 51 minutes)
	FVector WallUp = FVector::VectorPlaneProject(FVector::UpVector, FrontHit.Normal).GetSafeNormal();
	float WallCos = FVector::UpVector | FrontHit.Normal;
	float WallSin = FMath::Sqrt(1 - WallCos * WallCos);
	// Hit -> Move into the wall by 3 Fwd (So we can detect very thin starts NOT SUPER EFFECTIVE instead try move in by half distance? come back to this later) @todo
	// -> Up the wall in the direction of WallUp till max height minus min height -> ?? WallSin ?? @todo
	FVector TraceStart = FrontHit.Location + (Fwd * 3) + WallUp * (MaxHeight - (Mantle_MinShortClimbHeight - 1)) / WallSin;
	LINE(TraceStart, FrontHit.Location + Fwd, FColor::Orange)

	// Get multiple collision points in case there is something above that mantle wall
	if (!GetWorld()->LineTraceMultiByProfile(HeightHits, TraceStart, FrontHit.Location + Fwd, "BlockAll", Params)) return false;

	for (const FHitResult Hit : HeightHits)
	{
		// Was not one of the hits inside an object
		if (Hit.IsValidBlockingHit())
		{
			SurfaceHit = Hit;
			break;
		}
	}

	// Limit to the angle which can be mantled on
	if (!SurfaceHit.IsValidBlockingHit() || (SurfaceHit.Normal | FVector::UpVector) < CosMMSA) return false;
	// @todo review
	float Height = (SurfaceHit.Location - BaseLoc) | FVector::UpVector;

	SLOG(FString::Printf(TEXT("Height: %f"), Height))
	POINT(SurfaceHit.Location, FColor::Blue)

	if (Height > MaxHeight) return false;

	// ---- CHECK CLEARANCE ---- //
	// Angle of the surface
	float SurfaceCos = FVector::UpVector | SurfaceHit.Normal;
	float SurfaceSin = FMath::Sqrt(1 - SurfaceCos * SurfaceCos);
	// Location to place the test capsule
	// Move the capsule by the Fwd of the Capsule radius so they are fully on the geometry
	// Up vector multiplied by the height adding the height of the surface angle (Accounts for the height caused by the angle of the surface)
	FVector ClearCapLoc = SurfaceHit.Location + Fwd * CapR() + FVector::UpVector * (CapHH() + 1 + CapR() * 2 * SurfaceSin);
	FCollisionShape CapShape = FCollisionShape::MakeCapsule(CapR(), CapHH());
	if (GetWorld()->OverlapAnyTestByProfile(ClearCapLoc, FQuat::Identity, "BlockAll", CapShape, Params))
	{
		CAPSULE(ClearCapLoc, FColor::Red)
		return false;
	}

	CAPSULE(ClearCapLoc, FColor::Green)
	SLOG("Can Mantle")

	// ---- CHECK IF SHOULD VAULT ---- //
	// Essentially walls that are less than 1 capsule thick and has enough room for a capsule on the other side
	bool shouldVault = false;
	FHitResult VaultHit;
	FVector VaultStart = FrontHit.Location + -FrontHit.Normal * CapR() * 2;
	VaultStart.Z = UpdatedComponent->GetComponentLocation().Z - CapHH() * 0.5;
	FVector VaultEnd = VaultStart + FVector::DownVector * CapHH() * 2.5;
	
	LINE(VaultStart, VaultEnd, FColor::Purple)

	if (Height < Mantle_MaxVaultHeight)
	{
		if (GetWorld()->LineTraceSingleByProfile(VaultHit, VaultStart, VaultEnd, "BlockAll", Params))
		{ 
			FVector VaultCapLoc = VaultHit.Location;
			VaultCapLoc.Z += CapHH() + 2;
			if (GetWorld()->OverlapAnyTestByProfile(VaultCapLoc, FQuat::Identity, "BlockAll", CapShape, Params))
			{
				CAPSULE(VaultCapLoc, FColor::Orange)
			}
			else
			{
				CAPSULE(VaultCapLoc, FColor::Green)
				shouldVault = true;
			}	
		}	
	}

	const std::string Type = shouldVault ? "Vault" : "Mantle";
	
	FVector ShortMantleTarget = GetMantleStartLocation(FrontHit, SurfaceHit, false, Type);
	FVector TallMantleTarget = GetMantleStartLocation(FrontHit, SurfaceHit, true, Type);

	bool bTallMantle = false;
	// Check heights for either Mantle or Vault
	if ((IsMovementMode(MOVE_Walking) && Type == "Mantle" && Height > Mantle_MinTallClimbHeight) ||
		(IsMovementMode(MOVE_Walking) && Type == "Vault" && Height > Mantle_MinTallVaultHeight))
		bTallMantle = true;
	// If we are falling and Velocity is downward
	else if (IsMovementMode(MOVE_Falling) && (Velocity | FVector::UpVector) < 0)
	{
		// Don't want to tall mantle if the object is not tall enough for the tall mantle animation which is capsule height
		if (!GetWorld()->OverlapAnyTestByProfile(TallMantleTarget, FQuat::Identity, "BlockAll", CapShape, Params))
			bTallMantle = true;
	}

	FVector TransitionTarget = bTallMantle ? TallMantleTarget : ShortMantleTarget;
	CAPSULE(TransitionTarget, FColor::Yellow)
	// Perform Transition to Mantle
	CAPSULE(UpdatedComponent->GetComponentLocation(), FColor::Red)

	// The transition montage speed is controlled by the current UpSpeed
	// If the character was moving upward then we speed up the transition
	// If the character was moving downward then we slow down the transtion
	// Makes it feel more realistic
	float UpSpeed = Velocity | FVector::UpVector;
	float TransDistance = FVector::Dist(TransitionTarget, UpdatedComponent->GetComponentLocation());
	TransitionQueuedMontageSpeed = FMath::GetMappedRangeValueClamped(FVector2D(-500, 750), FVector2D(0.9f, 1.2f), UpSpeed);
	// Delete the old RMS from the SharedPTR and create a new one
	// RootMotionSource kind of like a tween to drive the capsule from start to target position
	TransitionRMS.Reset();
	TransitionRMS = MakeShared<FRootMotionSource_MoveToForce>();
	// No accumulation
	TransitionRMS->AccumulateMode = ERootMotionAccumulateMode::Override;

	// Duration of the transition based on how far you are away from the target distance
	TransitionRMS->Duration = FMath::Clamp(TransDistance / 500.0f, Mantle_MinTransitionTime, Mantle_MaxTransitionTime);
	SLOG(FString::Printf(TEXT("Duration: %f"), TransitionRMS->Duration))
	TransitionRMS->StartLocation = UpdatedComponent->GetComponentLocation();
	TransitionRMS->TargetLocation = TransitionTarget;
	TransitionName = "Mantle";
	
	// Zero out the Velocity
	Velocity = FVector::ZeroVector;
	// Remove gravity application
	SetMovementMode(MOVE_Flying);
	TransitionRMS_ID = ApplyRootMotionSource(TransitionRMS);

	// Queue animations
	// Transition Montages are NOT root animations
	// Transition Montages are 1 second and the speed can be scaled based on the TransitionRMS Duration
	SetMantleMontages(Type, bTallMantle);
	
	return true;
}

void UAdvCharacterMovementComponent::SetMantleMontages(const std::string& Type, const bool bTallMantle)
{
	
	if (Type == "Mantle")
	{
		if (bTallMantle)
		{
			TransitionQueuedMontage = Mantle_TallClimbMontage;
			CharacterOwner->PlayAnimMontage(Mantle_TransitionTallClimbMontage, 1 / TransitionRMS->Duration);
			if (IsServer()) Proxy_bTallMantle = !Proxy_bTallMantle;
		}
		else
		{
			TransitionQueuedMontage = Mantle_ShortClimbMontage;
			CharacterOwner->PlayAnimMontage(Mantle_TransitionShortClimbMontage, 1 / TransitionRMS->Duration);
			if (IsServer()) Proxy_bShortMantle = !Proxy_bShortMantle;
		}
	}
	else if (Type == "Vault")
	{
		if (bTallMantle)
		{
			TransitionQueuedMontage = Mantle_TallVaultMontage;
			CharacterOwner->PlayAnimMontage(Mantle_TransitionTallVaultMontage, 1 / TransitionRMS->Duration);
			if (IsServer()) Proxy_bTallVault = !Proxy_bTallVault;
		}
		else
		{
			TransitionQueuedMontage = Mantle_ShortVaultMontage;
			CharacterOwner->PlayAnimMontage(Mantle_TransitionShortVaultMontage, 1 / TransitionRMS->Duration);
			if (IsServer()) Proxy_bShortVault = !Proxy_bShortVault;
		}
	}
}

FVector UAdvCharacterMovementComponent::GetMantleStartLocation(const FHitResult& FrontHit, const FHitResult& SurfaceHit, const bool bTallMantle, const std::string& Type) const
{
	// Working backwards from the top point to the point in which the capsule must be transitioned to in order to start the animation
	
	float CosWallSteepnessAngle = FrontHit.Normal | FVector::UpVector;

	float DownDistance = 0.0f;
	if (Type == "Mantle")
	{
		DownDistance = bTallMantle ? Mantle_MinTallClimbHeight : Mantle_MinShortClimbHeight;
	}
	else if (Type == "Vault")
	{
		DownDistance = bTallMantle ? Mantle_MinTallVaultHeight : Mantle_MinShortVaultHeight;
	}
	
	FVector EdgeTangent = FVector::CrossProduct(SurfaceHit.Normal, FrontHit.Normal).GetSafeNormal();
	// 1. Start at the surface hit location
	// 2. Move backwards so capsule is lined up with wall
	// 3. Instead of having the character move more towards where they are looking move the position so it is closer to the capsule using the edge tangent and at a value of 0.3 (looks better)
	// 4. Move the capsule UP so it is inline with the surface hit
	// 5. Move the capsule DOWN depending on what kind of mantle we are going for
	// 6. Move the capsule to adjust for the steepness of the wall it is mantling (not beneath/inside the wall because of its angle)
	FVector MantleStart = SurfaceHit.Location;
	MantleStart += FrontHit.Normal.GetSafeNormal2D() * (2.0f + CapR());
	MantleStart += UpdatedComponent->GetForwardVector().GetSafeNormal2D().ProjectOnTo(EdgeTangent) * CapR() * 0.3f;
	MantleStart += FVector::UpVector * CapHH();
	MantleStart += FVector::DownVector * DownDistance;
	MantleStart += FrontHit.Normal.GetSafeNormal2D() * CosWallSteepnessAngle;

	return MantleStart;
}

#pragma endregion Mantle

#pragma region Wall Run

bool UAdvCharacterMovementComponent::TryWallRun()
{
	// Must be falling
	// Horizontal velocity must be faster than Min Speed
	// Prevents wall run if you have high vertical velocity (Can be changed to what you see fit)
	if (!IsFalling()) return false;
	if (Velocity.SizeSquared2D() < pow(WallRun_MinSpeed, 2)) return false;
	if (Velocity.Z < -WallRun_MaxVerticalSpeed) return false;

	// Set line hits for left and right
	FVector Start = UpdatedComponent->GetComponentLocation();
	FVector LeftEnd = Start - UpdatedComponent->GetRightVector() * CapR() * 2;
	FVector RightEnd = Start + UpdatedComponent->GetRightVector() * CapR() * 2;
	auto Params = AdvancedCharacterOwner->GetIgnoreCharacterParams();
	FHitResult FloorHit, WallHit;

	// Check height
	if (GetWorld()->LineTraceSingleByProfile(FloorHit, Start, Start + FVector::DownVector * (CapHH() + WallRun_MinHeight), "BlockAll", Params)) return false;

	// Left Cast
	GetWorld()->LineTraceSingleByProfile(WallHit, Start, LeftEnd, "BlockAll", Params);
	// Velocity must be point at the wall to some degree just not away from the wall
	if (WallHit.IsValidBlockingHit() && (Velocity | WallHit.Normal) < 0)
	{
		Safe_bWallRunIsRight = false;
	}
	else
	{
		// Right Cast
		GetWorld()->LineTraceSingleByProfile(WallHit, Start, RightEnd, "BlockAll", Params);
		if (WallHit.IsValidBlockingHit() && (Velocity | WallHit.Normal) < 0)
		{
			Safe_bWallRunIsRight = true;
		}
		else
		{
			return false;
		}
	}

	// The velocity vector projected onto the plane of the wall
	FVector ProjectedVelocity = FVector::VectorPlaneProject(Velocity, WallHit.Normal);

	// More restrictive than the first check for MinSpeed
	if (ProjectedVelocity.SizeSquared2D() < pow(WallRun_MinSpeed, 2)) return false;

	// Passed all conditions enter wall run
	Velocity = ProjectedVelocity;
	Velocity.Z = FMath::Clamp(Velocity.Z, 0.0f, WallRun_MaxVerticalSpeed);
	SetMovementMode(MOVE_Custom, CMOVE_WallRun);
	SLOG("Starting Wall Run");
	return true;
}

void UAdvCharacterMovementComponent::PhysWallRun(float deltaTime, int32 Iterations)
{
	if (deltaTime < MIN_TICK_TIME)
	{
		return;
	}
	if (!CharacterOwner || (!CharacterOwner->Controller && !bRunPhysicsWithNoController && !HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity() && (CharacterOwner->GetLocalRole() != ROLE_SimulatedProxy)))
	{
		Acceleration = FVector::ZeroVector;
		Velocity = FVector::ZeroVector;
		return;
	}
	
	bJustTeleported = false;
	float remainingTime = deltaTime;
	// Perform the move
	while ( (remainingTime >= MIN_TICK_TIME) && (Iterations < MaxSimulationIterations) && CharacterOwner && (CharacterOwner->Controller || bRunPhysicsWithNoController || (CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy)) )
	{
		Iterations++;
		bJustTeleported = false;
		const float timeTick = GetSimulationTimeStep(remainingTime, Iterations);
		remainingTime -= timeTick;
		const FVector OldLocation = UpdatedComponent->GetComponentLocation();

		FVector Start = UpdatedComponent->GetComponentLocation();
		FVector CastDelta = UpdatedComponent->GetRightVector() * CapR() * 2;
		FVector End = Safe_bWallRunIsRight ? Start + CastDelta : Start - CastDelta;
		auto Params = AdvancedCharacterOwner->GetIgnoreCharacterParams();
		float SinPullAwayAngle = FMath::Sin(FMath::DegreesToRadians(WallRun_PullAwayAngle));
		FHitResult WallHit;
		GetWorld()->LineTraceSingleByProfile(WallHit, Start, End, "BlockAll", Params);
		bool bWantsToPullAway = WallHit.IsValidBlockingHit() && !Acceleration.IsNearlyZero() && (Acceleration.GetSafeNormal() | WallHit.Normal) > SinPullAwayAngle;

		// Fall off wall
		if (!WallHit.IsValidBlockingHit() || bWantsToPullAway)
		{
			SetMovementMode(MOVE_Falling);
			StartNewPhysics(remainingTime, Iterations);
			return;
		}

		// Project acceleration onto the wall
		Acceleration = FVector::VectorPlaneProject(Acceleration, WallHit.Normal);
		Acceleration.Z = 0.0f;
		// Apply acceleration
		CalcVelocity(timeTick, 0.0f, false, GetMaxBrakingDeceleration());
		// Project Velocity onto the wall
		Velocity = FVector::VectorPlaneProject(Velocity, WallHit.Normal);
		// How much acceleration is tangent to the wall
		// If our input is along the wall we get a value closer to 1 (-1 if we are against the direction along the wall)
		float TangentAccel = Acceleration.GetSafeNormal() | Velocity.GetSafeNormal2D();
		bool bVelUp = Velocity.Z > 0.0f;
		// Apply gravity (they let go of input or go against the flow)
		// Define the pattern using a curve of how gravity effects 
		Velocity.Z += GetGravityZ() * WallRun_GravityScaleCurve->GetFloatValue(bVelUp ? 0.0f : TangentAccel) * timeTick;
		// Losing too much velocity or too much downward velocity
		if (Velocity.SizeSquared2D() < pow(WallRun_MinSpeed, 2) || Velocity.Z < -WallRun_MaxVerticalSpeed)
		{
			SetMovementMode(MOVE_Falling);
			StartNewPhysics(remainingTime, Iterations);
			return;
		}

		// Commute move paramaters
		const FVector Delta = timeTick * Velocity; // dx = v * dt
		const bool bZeroDelta = Delta.IsNearlyZero();
		if (bZeroDelta)
		{
			remainingTime = 0.0f;
		}
		else
		{
			FHitResult Hit;
			// Move us by the delta of velocity
			SafeMoveUpdatedComponent(Delta, UpdatedComponent->GetComponentQuat(), true, Hit);
			// Move them at the wall
			FVector WallAttractionDelta = -WallHit.Normal * WallRun_AttractionForce * timeTick;
			SafeMoveUpdatedComponent(WallAttractionDelta, UpdatedComponent->GetComponentQuat(), true, Hit);
		}
		if (UpdatedComponent->GetComponentLocation() == OldLocation)
		{
			remainingTime = 0.0f;
			break;
		}
		Velocity = (UpdatedComponent->GetComponentLocation() - OldLocation) / timeTick; // v = dx / dt
	}

	// Checks if we can still wallrun in the next frame
	FVector Start = UpdatedComponent->GetComponentLocation();
	FVector CastDelta = UpdatedComponent->GetRightVector() * CapR() * 2;
	FVector End = Safe_bWallRunIsRight ? Start + CastDelta : Start - CastDelta;
	auto Params = AdvancedCharacterOwner->GetIgnoreCharacterParams();
	FHitResult FloorHit, WallHit;
	GetWorld()->LineTraceSingleByProfile(WallHit, Start, End, "BlockAll", Params);
	GetWorld()->LineTraceSingleByProfile(FloorHit, Start, Start + FVector::DownVector * (CapHH() + WallRun_MinHeight * 0.5f), "BlockAll", Params);
	if (FloorHit.IsValidBlockingHit() || !WallHit.IsValidBlockingHit() || Velocity.SizeSquared2D() < pow(WallRun_MinSpeed, 2))
	{
		SetMovementMode(MOVE_Falling);
	}
}

#pragma endregion Wall Run

#pragma region Climbing

bool UAdvCharacterMovementComponent::TryHang()
{
	if (!IsMovementMode(MOVE_Falling)) return false;

	SLOG("Wants to climb")

	FHitResult WallHit;
	FVector Start = UpdatedComponent->GetComponentLocation();
	FVector End = Start + UpdatedComponent->GetForwardVector() * 300.0f; // Not needed to be parameterised -> restricted later
	auto Params = AdvancedCharacterOwner->GetIgnoreCharacterParams();
	if (!GetWorld()->LineTraceSingleByProfile(WallHit, Start, End, "BlockAll", Params)) return false;

	TArray<FOverlapResult> OverlapResults;

	// Move detection to the players head then move 2 capsules in front
	// Can parameterise the box size to give more accessibility to what can be grabbed
	FVector ColLoc = UpdatedComponent->GetComponentLocation() + FVector::UpVector * CapHH() + UpdatedComponent->GetForwardVector() * CapR() * 3;
	auto ColBox = FCollisionShape::MakeBox(FVector(100, 100, 50));
	// Makes the box lined up with the wall and the Up vector
	FQuat ColRot = FRotationMatrix::MakeFromXZ(WallHit.Normal, FVector::UpVector).ToQuat();

	if (!GetWorld()->OverlapMultiByChannel(OverlapResults, ColLoc, ColRot, ECC_WorldStatic, ColBox, Params)) return false;

	AActor* ClimbPoint = nullptr;

	float MaxHeight = -1e20;
	for (FOverlapResult Result : OverlapResults)
	{
		if (Result.GetActor()->ActorHasTag("Climb Point"))
		{
			float Height = Result.GetActor()->GetActorLocation().Z;
			if (Height > MaxHeight)
			{
				MaxHeight = Height;
				ClimbPoint = Result.GetActor();
			}
		}
	}
	if (!IsValid(ClimbPoint)) return false;

	// Where the capsule should be
	// Back away from the wall -> 1.01 for a bit of tolerance from the wall
	FVector TargetLocation = ClimbPoint->GetActorLocation() + WallHit.Normal * CapR() * 1.01f + FVector::DownVector * CapHH();
	FQuat TargetRotation = FRotationMatrix::MakeFromXZ(-WallHit.Normal, FVector::UpVector).ToQuat();

	// Test if the character can reach this goal -> Including the movement to said goal not just if they fit in the target
	FTransform CurrentTransform = UpdatedComponent->GetComponentTransform();
	FHitResult Hit, ReturnHit;
	// Sweep to true so it actually simulates this movement and checks if there was any collision
	SafeMoveUpdatedComponent(TargetLocation - UpdatedComponent->GetComponentLocation(), UpdatedComponent->GetComponentQuat(), true, Hit);
	FVector ResultLocation = UpdatedComponent->GetComponentLocation();
	// Move them back
	SafeMoveUpdatedComponent(CurrentTransform.GetLocation() - ResultLocation, TargetRotation, false, ReturnHit);
	if (!ResultLocation.Equals(TargetLocation)) return false;

	// Passed all conditions

	bOrientRotationToMovement = false;

	float UpSpeed = Velocity | FVector::UpVector;
	float TransDistance = FVector::Dist(TargetLocation, UpdatedComponent->GetComponentLocation());

	TransitionQueuedMontageSpeed = FMath::GetMappedRangeValueClamped(FVector2D(-500, 750), FVector2D(0.9f, 1.2f), UpSpeed);
	TransitionRMS.Reset();
	TransitionRMS = MakeShared<FRootMotionSource_MoveToForce>();
	TransitionRMS->AccumulateMode = ERootMotionAccumulateMode::Override;

	TransitionRMS->Duration = FMath::Clamp(TransDistance / 500.0f, Hang_MinTransitionTime, Hang_MaxTransitionTime);
	SLOG(FString::Printf(TEXT("Duration: %f"), TransitionRMS->Duration))
	TransitionRMS->StartLocation = UpdatedComponent->GetComponentLocation();
	TransitionRMS->TargetLocation = TargetLocation;

	Velocity = FVector::ZeroVector;
	SetMovementMode(MOVE_Flying);
	TransitionRMS_ID = ApplyRootMotionSource(TransitionRMS);

	TransitionQueuedMontage = nullptr;
	TransitionName = "Hang";
	CharacterOwner->PlayAnimMontage(Hang_TransitionMontage, 1 / TransitionRMS->Duration);

	return true;
}

bool UAdvCharacterMovementComponent::TryClimb()
{
	if (!IsFalling() || !Safe_bCanClimbAgain) return false;
	
	FHitResult SurfaceHit;
	FHitResult ClimbResult;
	FVector Start = UpdatedComponent->GetComponentLocation();
	FVector End = Start + UpdatedComponent->GetForwardVector() * Climb_ReachDistance;
	auto Params = AdvancedCharacterOwner->GetIgnoreCharacterParams();
	GetWorld()->LineTraceSingleByProfile(SurfaceHit, Start, End, "BlockAll", Params);

	if (!SurfaceHit.IsValidBlockingHit()) return false;

	if (TryMantle()) return false; // We would prefer to mantle instead if we have a valid climbing surface
	
	FQuat NewRotation = FRotationMatrix::MakeFromXZ(-SurfaceHit.Normal, FVector::UpVector).ToQuat();
	SafeMoveUpdatedComponent(FVector::ZeroVector, NewRotation, false, ClimbResult);

	SetMovementMode(MOVE_Custom, CMOVE_Climb);

	bOrientRotationToMovement = false;

	ClimbTimeRemaining = Climb_MaxDuration;
	Safe_bCanClimbAgain = false;
	
	return true;
}

void UAdvCharacterMovementComponent::PhysClimb(float deltaTime, int32 Iterations)
{
	if (deltaTime < MIN_TICK_TIME)
	{
		return;
	}

	ClimbTimeRemaining -= deltaTime;
	if (ClimbTimeRemaining <= 0)
	{
		CharacterOwner->Jump();
		return;
	}
	
	if (!CharacterOwner || (!CharacterOwner->Controller && !bRunPhysicsWithNoController && !HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity() && (CharacterOwner->GetLocalRole() != ROLE_SimulatedProxy)))
	{
		Acceleration = FVector::ZeroVector;
		Velocity = FVector::ZeroVector;
		return;
	}

	ClimbMantleCheckAccumulator += deltaTime;
	if (ClimbMantleCheckAccumulator >= Climb_MantleCheckInterval)
	{
		ClimbMantleCheckAccumulator = 0.0f;

		if (TryMantle())
		{
			SLOG("Successfully Mantled from climb")
			return;
		}
	}

	// Setup Acceleration
	Acceleration = Acceleration.RotateAngleAxis(90.0f, -UpdatedComponent->GetRightVector());
	Acceleration.X = 0.0f;
	Acceleration.Y = 0.0f;
	// Downward input detected
	if (Acceleration.Z < 0.0f)
	{
		SetMovementMode(MOVE_Falling);
		StartNewPhysics(deltaTime, Iterations);
		return;
	}
	
	bJustTeleported = false;
	Iterations++;
	const FVector OldLocation = UpdatedComponent->GetComponentLocation();
	FHitResult SurfaceHit, FloorHit;
	auto Params = AdvancedCharacterOwner->GetIgnoreCharacterParams();
	GetWorld()->LineTraceSingleByProfile(SurfaceHit, OldLocation, OldLocation + UpdatedComponent->GetForwardVector() * Climb_ReachDistance, "BlockAll", Params);
	GetWorld()->LineTraceSingleByProfile(FloorHit, OldLocation, OldLocation + FVector::DownVector * CapHH() * 1.2f, "BlockAll", Params);

	if (!SurfaceHit.IsValidBlockingHit() || FloorHit.IsValidBlockingHit())
	{
		SetMovementMode(MOVE_Falling);
		StartNewPhysics(deltaTime, Iterations);
		return;
	}
	
	CalcVelocity(deltaTime, 0.0f, false, GetMaxBrakingDeceleration());
	Velocity = FVector::VectorPlaneProject(Velocity, SurfaceHit.Normal);
	// Ensure no side to side movement
	Velocity.X = 0.0f;
	Velocity.Y = 0.0f;

	const bool bVelUp = Acceleration.Z > 0.0f;
	if (!bVelUp)
	{
		Velocity.Z += GetGravityZ() * Climb_GravityScaleCurve * deltaTime;
	}
	
	if (Velocity.Z < Climb_MaxDownwardVelocity)
	{
		SetMovementMode(MOVE_Falling);
		StartNewPhysics(deltaTime, Iterations);
		return;
	}
	
	const FVector Delta = deltaTime * Velocity; // dx = v * dt
	if (!Delta.IsNearlyZero())
	{
		FHitResult Hit;
		SafeMoveUpdatedComponent(Delta, UpdatedComponent->GetComponentQuat(), true, Hit);
		FVector WallAttractionDelta = -SurfaceHit.Normal * WallRun_AttractionForce * deltaTime;
		SafeMoveUpdatedComponent(WallAttractionDelta , UpdatedComponent->GetComponentQuat(), true, Hit);
	}

	Velocity = (UpdatedComponent->GetComponentLocation() - OldLocation) / deltaTime; // v = dx / dt
}

#pragma endregion Climbing

#pragma region Replication

void UAdvCharacterMovementComponent::GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(UAdvCharacterMovementComponent, Proxy_bDashStart, COND_SkipOwner)
	DOREPLIFETIME_CONDITION(UAdvCharacterMovementComponent, Proxy_bShortMantle, COND_SkipOwner)
	DOREPLIFETIME_CONDITION(UAdvCharacterMovementComponent, Proxy_bTallMantle, COND_SkipOwner)
	DOREPLIFETIME_CONDITION(UAdvCharacterMovementComponent, Proxy_bShortVault, COND_SkipOwner)
	DOREPLIFETIME_CONDITION(UAdvCharacterMovementComponent, Proxy_bTallVault, COND_SkipOwner)
}

void UAdvCharacterMovementComponent::OnRep_DashStart()
{
	CharacterOwner->PlayAnimMontage(Dash_Montage);
	DashStartDelegate.Broadcast();
}

void UAdvCharacterMovementComponent::OnRep_ShortMantle()
{
	CharacterOwner->PlayAnimMontage(Mantle_ProxyShortClimbMontage);
}

void UAdvCharacterMovementComponent::OnRep_TallMantle()
{
	CharacterOwner->PlayAnimMontage(Mantle_ProxyTallClimbMontage);
}

void UAdvCharacterMovementComponent::OnRep_ShortVault()
{
	
}

void UAdvCharacterMovementComponent::OnRep_TallVault()
{
	
}

#pragma endregion Replication
