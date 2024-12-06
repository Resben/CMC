#include "AdvCharacterMovementComponent.h"

#include "MaterialHLSLTree.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "Net/UnrealNetwork.h"

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
	
	// Set after movement so valid for the next frame
	Safe_bPrevWantsToCrouch = bWantsToCrouch;
}

void UAdvCharacterMovementComponent::UpdateCharacterStateBeforeMovement(float DeltaSeconds)
{
	// -- SLIDE -- //
	// We need to do this before the crouch update gets to happen that's why its in this function
	// !bWantsToCrouch will be false on the second press while we compare this to the previous safe crouch
	// So a double crouch will result in a slide
	if (MovementMode == MOVE_Walking && !bWantsToCrouch && Safe_bPrevWantsToCrouch)
	{
		if (CanSlide())
		{
			SetMovementMode(MOVE_Custom, CMOVE_Slide);
		}
	}
	else if (IsCustomMovementMode(CMOVE_Slide) && !bWantsToCrouch)
	{
		SetMovementMode(MOVE_Walking);
	}

	// -- PRONE -- //

	if (Safe_bWantsToProne)
	{
		if (CanProne())
		{
			SetMovementMode(MOVE_Custom, CMOVE_Prone);
			if (!CharacterOwner->HasAuthority()) Server_EnterProne(); // Not the server call EnterProne
		}
		Safe_bWantsToProne = false;
	}
	
	if (IsCustomMovementMode(CMOVE_Prone) && !bWantsToCrouch)
	{
		SetMovementMode(MOVE_Walking);
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

	// Look here for more info on how the engine handles crouching
	Super::UpdateCharacterStateBeforeMovement(DeltaSeconds);
}

void UAdvCharacterMovementComponent::PhysCustom(float deltaTime, int32 Iterations)
{
	Super::PhysCustom(deltaTime, Iterations);

	switch (CustomMovementMode)
	{
	case CMOVE_Slide:
		PhysSlide(deltaTime, Iterations);
		break;
	case CMOVE_Prone:
		PhysProne(deltaTime, Iterations);
		break;
	default:
		UE_LOG(LogTemp, Fatal, TEXT("Invalid Movement Mode"))
	}
}

void UAdvCharacterMovementComponent::OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode)
{
	Super::OnMovementModeChanged(PreviousMovementMode, PreviousCustomMode);

	if (PreviousMovementMode == MOVE_Custom && PreviousCustomMode == CMOVE_Prone) ExitProne();
	if (PreviousMovementMode == MOVE_Custom && PreviousCustomMode == CMOVE_Slide) ExitSlide();
	
	if (IsCustomMovementMode(CMOVE_Prone)) EnterProne(PreviousMovementMode, (ECustomMovementMode)PreviousMovementMode);
	if (IsCustomMovementMode(CMOVE_Slide)) EnterSlide(PreviousMovementMode, (ECustomMovementMode)PreviousMovementMode);

}

bool UAdvCharacterMovementComponent::IsMovingOnGround() const
{
	return Super::IsMovingOnGround() || IsCustomMovementMode(CMOVE_Slide) || IsCustomMovementMode(CMOVE_Prone);
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
	case CMOVE_Prone:
		return Prone_MaxSpeed;
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
		return Slide_MaxBreakingDeceleration;
	case CMOVE_Prone:
		return Prone_MaxBreakingDeceleration;
	default:
		UE_LOG(LogTemp, Fatal, TEXT("Invalid Movement Mode"))
		return -1.0f;
	}
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
	
	return FSavedMove_Character::CanCombineWith(newMove, InCharacter, MaxDelta);
}

void UAdvCharacterMovementComponent::FSavedMove_Adv::Clear()
{
	FSavedMove_Character::Clear();

	Saved_bWantsToSprint = 0;
	Saved_bWantsToDash = 0;

	Saved_bWantsToProne = 0;
	Saved_bPrevWantsToCrouch = 0;
}

uint8 UAdvCharacterMovementComponent::FSavedMove_Adv::GetCompressedFlags() const
{
	uint8 Result = Super::GetCompressedFlags();

	if (Saved_bWantsToSprint) Result |= FLAG_Sprint;
	if (Saved_bWantsToDash) Result |= FLAG_Dash;

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
}

void UAdvCharacterMovementComponent::FSavedMove_Adv::PrepMoveFor(ACharacter* C)
{
	Super::PrepMoveFor(C);

	UAdvCharacterMovementComponent* CharacterMovement = Cast<UAdvCharacterMovementComponent>(C->GetCharacterMovement());

	CharacterMovement->Safe_bWantsToSprint = Saved_bWantsToSprint;
	CharacterMovement->Safe_bPrevWantsToCrouch = Saved_bPrevWantsToCrouch;
	CharacterMovement->Safe_bWantsToProne = Saved_bWantsToProne;
	CharacterMovement->Safe_bWantsToDash = Saved_bWantsToDash;
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
	bWantsToCrouch = !bWantsToCrouch;
	GetWorld()->GetTimerManager().SetTimer(TimerHandle_EnterProne, this, &UAdvCharacterMovementComponent::TryEnterProne, Prone_EnterHoldDuration);
}

void UAdvCharacterMovementComponent::CrouchReleased()
{
	GetWorld()->GetTimerManager().ClearTimer(TimerHandle_EnterProne);
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

#pragma region Slide

void UAdvCharacterMovementComponent::EnterSlide(EMovementMode PrevMode, ECustomMovementMode PrevCustomMode)
{
	bWantsToCrouch = true; // Use the capsule logic of a crouched state
	bOrientRotationToMovement = false;
	Velocity += Velocity.GetSafeNormal2D() * Slide_EnterImpulse;

	FindFloor(UpdatedComponent->GetComponentLocation(), CurrentFloor, true, nullptr);
}

// ExitSlide is a SAFE movement mode so we can call SafeMoveUpdatedComponent
void UAdvCharacterMovementComponent::ExitSlide()
{
	bWantsToCrouch = false;
	bOrientRotationToMovement = true;
}

bool UAdvCharacterMovementComponent::CanSlide() const
{
	FVector Start = UpdatedComponent->GetComponentLocation();
	FVector End = Start + CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * 2.5f * FVector::DownVector;
	FName ProfileName = TEXT("BlockAll");
	bool bValidSurface = GetWorld()->LineTraceTestByProfile(Start, End, ProfileName, AdvancedCharacterOwner->GetIgnoreCharacterParams());
	bool bEnoughSpeed = Velocity.SizeSquared() > pow(Slide_MinSpeed, 2);
	
	return bValidSurface && bEnoughSpeed;
}

void UAdvCharacterMovementComponent::PhysSlide(float deltaTime, int32 Iterations)
{
	// Source code boilerplate stuff
	if (deltaTime < MIN_TICK_TIME)
	{
		return;
	}

	if (!CanSlide())
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

		// bFluid -> friction is applied more instead apply your own Slide_FrictionFactor
		CalcVelocity(timeTick, GroundFriction * Slide_FrictionFactor, false, GetMaxBrakingDeceleration());

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
		const bool bCheckLedges = !CanWalkOffLedges();
		if (bCheckLedges && !CurrentFloor.IsWalkableFloor())
		{
			// Calculate possible alternative movement
			const FVector NewDelta = bTriedLedgeMove ? FVector::ZeroVector : GetLedgeMove(OldLocation, Delta, OldFloor);
			if (!NewDelta.IsZero())
			{
				// Revert move and prevent repeated ledge moves if the first fails
				RevertMove(OldLocation, OldBase, PreviousBaseLocation, OldFloor,  false);

				bTriedLedgeMove = true;

				// Try new movement direction
				Velocity = NewDelta / timeTick;
				remainingTime += timeTick;
				continue;
			}
			else
			{
				// See if it is ok to jump
				// May be problem if oldbase has world collision on
				bool bMustJump = bZeroDelta || (OldBase == nullptr || (!OldBase->IsQueryCollisionEnabled() && MovementBaseUtility::IsDynamicBase(OldBase)));
				if ((bMustJump || !bCheckedFall) && CheckFall(OldFloor, CurrentFloor.HitResult, Delta, OldLocation, remainingTime, timeTick, Iterations, bMustJump))
				{
					return;
				}
				bCheckedFall = true;

				// Revert this move
				RevertMove(OldLocation, OldBase, PreviousBaseLocation, OldFloor, true);
				remainingTime = 0.0f;
				break;
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

#pragma region Prone

void UAdvCharacterMovementComponent::Server_EnterProne_Implementation()
{
	Safe_bWantsToProne = true;
}

void UAdvCharacterMovementComponent::EnterProne(EMovementMode PrevMode, ECustomMovementMode PrevCustomMode)
{
	bWantsToCrouch = true;

	// Not necessary dive boost
	if (PrevMode == MOVE_Custom && PrevCustomMode == CMOVE_Slide)
	{
		Velocity += Velocity.GetSafeNormal2D() * Prone_SlideEnterImpulse;
	}

	// Ensures the very first substep has a valid updated floor
	FindFloor(UpdatedComponent->GetComponentLocation(), CurrentFloor, true, nullptr);
}

void UAdvCharacterMovementComponent::ExitProne() {}

bool UAdvCharacterMovementComponent::CanProne() const
{
	return IsCustomMovementMode(CMOVE_Slide) || IsMovementMode(MOVE_Walking) && IsCrouching();
}

void UAdvCharacterMovementComponent::PhysProne(float deltaTime, int32 Iterations)
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
	bool bCheckedFall = false;
	bool bTriedLedgeMove = false;
	float remainingTime = deltaTime;

	while ((remainingTime >= MIN_TICK_TIME) && (Iterations < MaxSimulationIterations) && CharacterOwner && (CharacterOwner->Controller || bRunPhysicsWithNoController || (CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy)))
	{
		// Using simulated time step for more accurate results
		// Multiple calculations of movement over a single frame
		Iterations++;
		bJustTeleported = false;
		const float timeTick = GetSimulationTimeStep(remainingTime, Iterations);
		remainingTime -= timeTick;

		// Get old values -> May need to revert if we go over a ledge
		UPrimitiveComponent * const OldBase = GetMovementBase();
		const FVector PreviousBaseLocation = (OldBase != nullptr) ? OldBase->GetComponentLocation() : FVector::ZeroVector;
		const FVector OldLocation = UpdatedComponent->GetComponentLocation();
		const FFindFloorResult OldFloor = CurrentFloor;

		// Ensure velocity is horizontal
		MaintainHorizontalGroundVelocity();
		const FVector OldVelocity = Velocity;
		Acceleration.Z = 0.0f;

		// Apply Acceleration
		// Breaking deceleration friction that applies when you let go of the controls -> breaking
		CalcVelocity(timeTick, GroundFriction, false, GetMaxBrakingDeceleration());

		// Compute the move parameters
		const FVector MoveVelocity = Velocity;
		const FVector Delta = timeTick * MoveVelocity; // dx = v * dt
		const bool bZeroDelta = Delta.IsNearlyZero();
		FStepDownResult StepDownResult;

		// If the velocity is zero no need for a full simulation
		if (bZeroDelta)
		{
			remainingTime = 0.0f;
		}
		else
		{
			MoveAlongFloor(MoveVelocity, timeTick, &StepDownResult);

			// Clean up the move
			if (IsFalling())
			{
				const float DesiredDist = Delta.Size();
				if (DesiredDist > KINDA_SMALL_NUMBER)
				{
					const float ActualDist = (UpdatedComponent->GetComponentLocation() - OldLocation).Size2D();
					remainingTime += timeTick * (1.0f - FMath::Min(1.0f, ActualDist / DesiredDist));
				}
				// Switch movement modes mid-simulation
				StartNewPhysics(remainingTime, Iterations);
				return;
			}
			else if (IsSwimming())
			{
				StartSwimming(OldLocation, OldVelocity, timeTick, remainingTime, Iterations);
				return;
			}
			// Add more edge cases as you see fit
		}

		// Update CurrentFloor
		if (StepDownResult.bComputedFloor)
		{
			CurrentFloor = StepDownResult.FloorResult;
		}
		else
		{
			FindFloor(UpdatedComponent->GetComponentLocation(), CurrentFloor, bZeroDelta, nullptr);
		}

		// LEDGE CHECK
		const bool bCheckLedges = !CanWalkOffLedges();
		if (bCheckLedges && !CurrentFloor.IsWalkableFloor())
		{
			// Calculate possible alternate movement
			const FVector GravDir = FVector(0.0f, 0.0f, -1.0f);
			const FVector NewDelta = bTriedLedgeMove ? FVector::ZeroVector : GetLedgeMove(OldLocation, Delta, OldFloor); // Tutorial uses GravDir we use OldFloor
			if (!NewDelta.IsZero())
			{
				// Revert the move
				RevertMove(OldLocation, OldBase, PreviousBaseLocation, OldFloor, false);

				// Avoid repeated ledge moves if the first one fails
				bTriedLedgeMove = true;

				// Try new movement direction
				// Revert the remaining time
				// Reverse the velocity to the new move -> Move towards the ledge move
				Velocity = NewDelta / timeTick; // v = dx / dt
				remainingTime += timeTick;
				continue;
			}
			else
			{
				bool bMustJump = bZeroDelta || (OldBase == nullptr || (!OldBase->IsQueryCollisionEnabled() && MovementBaseUtility::IsDynamicBase(OldBase)));
				if ((bMustJump || !bCheckedFall) && CheckFall(OldFloor, CurrentFloor.HitResult, Delta, OldLocation, remainingTime, timeTick, Iterations, bMustJump))
				{
					return;
				}
				bCheckedFall = true;

				RevertMove(OldLocation, OldBase, PreviousBaseLocation, OldFloor, true);
				remainingTime = 0.0f;
				break;
			}
		}
		else
		{
			// Here handle if we can't check for ledges OR the current floor is walkable
			// Was walkable -> potential staircase?
			if (CurrentFloor.IsWalkableFloor())
			{
				AdjustFloorHeight(); // Adjust the capsule relative to the floor
				SetBase(CurrentFloor.HitResult.Component.Get(), CurrentFloor.HitResult.BoneName); // Kind of what the actor is standing on -> combines velocities of what they are standing on plus their own velocity (maybe)
			}
			else if (CurrentFloor.HitResult.bStartPenetrating && remainingTime <= 0.0f)
			{
				// The floor check failed because it started in penetration
				// We do not want to try to move downward because the downward sweep failed, rather we'd like to try to pop out of the floor
				FHitResult Hit(CurrentFloor.HitResult);
				Hit.TraceEnd = Hit.TraceStart + FVector(0.0f, 0.0f, MAX_FLOOR_DIST);
				const FVector RequestedAdjusted = GetPenetrationAdjustment(Hit);
				ResolvePenetration(RequestedAdjusted, Hit, UpdatedComponent->GetComponentQuat());
				bForceNextFloorCheck = true;
			}

			// Again check if we are swimming or failing
			if (IsSwimming())
			{
				StartSwimming(OldLocation, Velocity, timeTick, remainingTime, Iterations);
				return;
			}

			// See if we start falling
			if (!CurrentFloor.IsWalkableFloor() && !CurrentFloor.HitResult.bStartPenetrating)
			{
				const bool bMustJump = bJustTeleported || bZeroDelta || (OldBase == nullptr || (OldBase->IsQueryCollisionEnabled() && MovementBaseUtility::IsDynamicBase(OldBase)));
				if ((bMustJump || !bCheckedFall) && CheckFall(OldFloor, CurrentFloor.HitResult, Delta, OldLocation, remainingTime, timeTick, Iterations, bMustJump))
				{
					return;
				}
				bCheckedFall = false;
			}
		}

		// Allow overlap events and such to change physics state and velocity
		if (IsMovingOnGround())
		{
			// Make velocity reflect actual move
			if (!bJustTeleported && !HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity() && timeTick >= MIN_TICK_TIME)
			{
				Velocity = (UpdatedComponent->GetComponentLocation() - OldLocation) / timeTick; // v = dx / dt
				MaintainHorizontalGroundVelocity();
			}
		}

		// If we didn't move at all this iteration then abort (since future iterations will also be stuck)
		if (UpdatedComponent->GetComponentVelocity() == OldLocation)
		{
			remainingTime = 0.0f;
			break;
		}
	}

	if (IsMovingOnGround())
	{
		MaintainHorizontalGroundVelocity();
	}
}

#pragma endregion Prone

#pragma region Dash

void UAdvCharacterMovementComponent::OnDashCooldownFinished()
{
	Safe_bWantsToDash = true;
}

bool UAdvCharacterMovementComponent::CanDash() const
{
	return IsWalking() && !IsCrouching();
}

// Similar to launch here we can customise it
// Launch is Velocity +=
// Launch can be called in movement unsafe
// Launch calls next frame
void UAdvCharacterMovementComponent::PerformDash()
{
	DashStartTime = GetWorld()->GetTimeSeconds();

	// If your stationary use the facing direction else use the direction you are moving
	// Acceleration is the same as the input vector on the client -> no need for server rpc
	FVector DashDirection = (Acceleration.IsNearlyZero() ? UpdatedComponent->GetForwardVector() : Acceleration).GetSafeNormal2D();
	// Uses the fixed value use += for a combination of dash and movement
	Velocity = Dash_Impulse * (DashDirection + FVector::UpVector * 0.1f);

	// Face in dash direction 
	FQuat NewRotation = FRotationMatrix::MakeFromXZ(DashDirection, FVector::UpVector).ToQuat();
	FHitResult Hit;
	SafeMoveUpdatedComponent(FVector::ZeroVector, NewRotation, false, Hit);

	SetMovementMode(MOVE_Falling);

	DashStartDelegate.Broadcast();
}

#pragma endregion Dash

#pragma region Replication

void UAdvCharacterMovementComponent::GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(UAdvCharacterMovementComponent, Proxy_bDashStart, COND_SkipOwner)
}

void UAdvCharacterMovementComponent::OnRep_DashStart()
{
	DashStartDelegate.Broadcast();
}


#pragma endregion Replication