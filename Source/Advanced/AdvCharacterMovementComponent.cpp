#include "AdvCharacterMovementComponent.h"

#include "MaterialHLSLTree.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"

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

	Safe_bWantsToSprint = (Flags & FSavedMove_Character::FLAG_Custom_0) != 0;
}

// After all the movement has been updated
void UAdvCharacterMovementComponent::OnMovementUpdated(float DeltaSeconds, const FVector& OldLocation, const FVector& OldVelocity)
{
	Super::OnMovementUpdated(DeltaSeconds, OldLocation, OldVelocity);

	if (MovementMode == MOVE_Walking)
	{
		if (Safe_bWantsToSprint)
		{
			MaxWalkSpeed = Sprint_MaxSpeed;
		}
		else
		{
			MaxWalkSpeed = Walk_MaxSpeed;
		}
	}

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
		FHitResult PotentialSlideSurface;
		if (Velocity.SizeSquared2D() > pow(Slide_MinSpeed, 2) && GetSlideSurface(PotentialSlideSurface))
		{
			// Sets bWantsToCrouch to true again so we maintain the capsule shape before crouch is processed later
			EnterSlide();
		}
	}

	// If sliding and we crouch again to cancel
	if (IsCustomMovementMode(CMOVE_Slide) && !bWantsToCrouch)
	{
		ExitSlide();
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

	if (IsCustomMovementMode(CMOVE_Prone)) EnterProne(PreviousMovementMode, (ECustomMovementMode)PreviousMovementMode);
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
	
	return FSavedMove_Character::CanCombineWith(newMove, InCharacter, MaxDelta);
}

void UAdvCharacterMovementComponent::FSavedMove_Adv::Clear()
{
	FSavedMove_Character::Clear();

	Saved_bWantsToSprint = 0;
}

uint8 UAdvCharacterMovementComponent::FSavedMove_Adv::GetCompressedFlags() const
{
	uint8 Result = Super::GetCompressedFlags();

	if (Saved_bWantsToSprint) Result |= FLAG_Custom_0;


	return Result;
}

void UAdvCharacterMovementComponent::FSavedMove_Adv::SetMoveFor(ACharacter* C, float InDeltaTime, FVector const& NewAccel, FNetworkPredictionData_Client_Character& ClientData)
{
	FSavedMove_Character::SetMoveFor(C, InDeltaTime, NewAccel, ClientData);

	UAdvCharacterMovementComponent* CharacterMovement = Cast<UAdvCharacterMovementComponent>(C->GetCharacterMovement());

	Saved_bWantsToSprint = CharacterMovement->Safe_bWantsToSprint;
	Saved_bPrevWantsToCrouch = CharacterMovement->Safe_bPrevWantsToCrouch;
	Saved_bWantsToProne = CharacterMovement->Safe_bWantsToProne;
}

void UAdvCharacterMovementComponent::FSavedMove_Adv::PrepMoveFor(ACharacter* C)
{
	Super::PrepMoveFor(C);

	UAdvCharacterMovementComponent* CharacterMovement = Cast<UAdvCharacterMovementComponent>(C->GetCharacterMovement());

	CharacterMovement->Safe_bWantsToSprint = Saved_bWantsToSprint;
	CharacterMovement->Safe_bPrevWantsToCrouch = Saved_bPrevWantsToCrouch;
	CharacterMovement->Safe_bWantsToProne = Saved_bWantsToProne;
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

void UAdvCharacterMovementComponent::EnterSlide()
{
	bWantsToCrouch = true; // Use the capsule logic of a crouched state
	Velocity += Velocity.GetSafeNormal2D() * Slide_EnterImpulse;
	SetMovementMode(MOVE_Custom, CMOVE_Slide);
}

// ExitSlide is a SAFE movement mode so we can call SafeMoveUpdatedComponent
void UAdvCharacterMovementComponent::ExitSlide()
{
	bWantsToCrouch = false;

	// Correct the rotation
	FQuat NewRotation = FRotationMatrix::MakeFromXZ(UpdatedComponent->GetForwardVector().GetSafeNormal2D(), FVector::UpVector).ToQuat();
	FHitResult Hit;
	SafeMoveUpdatedComponent(FVector::ZeroVector, NewRotation, true, Hit);
	SetMovementMode(MOVE_Walking);
}

void UAdvCharacterMovementComponent::PhysSlide(float deltaTime, int32 Iterations)
{
	// Source code boilerplate stuff
	if (deltaTime < MIN_TICK_TIME)
	{
		return;
	}

	// Root motion is when an animation controls and characters velocity and/or position
	// Might not be needed for a slide
	RestorePreAdditiveRootMotionVelocity();

	// Checking to make sure we have a valid slide
	// Get out of the slide if there is no surface or less than min speed
	FHitResult SurfaceHit;
	if (!GetSlideSurface(SurfaceHit) || Velocity.SizeSquared() < pow(Slide_MinSpeed, 2))
	{
		ExitSlide();
		StartNewPhysics(deltaTime, Iterations); // Start a new iteration in the new physics function
		return;
	}

	// Surface gravity
	Velocity += Slide_GravityForce * FVector::DownVector * deltaTime; // v += a * dt

	// Strafe -> Steer the slide left or right
	// Acceleration is the input vector (from your keys)
	if (FMath::Abs(FVector::DotProduct(Acceleration.GetSafeNormal(), UpdatedComponent->GetRightVector())) > 0.5f)
	{
		Acceleration = Acceleration.ProjectOnTo(UpdatedComponent->GetRightVector());
	}
	else
	{
		Acceleration = FVector::ZeroVector;
	}

	// Calc Velocity
	// Boilerplate animation handling
	if (!HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity())
	{
		CalcVelocity(deltaTime, Slide_Friction, true, GetMaxBrakingDeceleration());
	}
	ApplyRootMotionToVelocity(deltaTime);

	// Now we can perform the move
	Iterations++;
	bJustTeleported = false; // Boilerplate

	
	FVector OldLocation = UpdatedComponent->GetComponentLocation();
	FQuat OldRotation = UpdatedComponent->GetComponentRotation().Quaternion();
	FHitResult Hit(1.0f);
	FVector Adjusted = Velocity * deltaTime; // x = v * dt
	// Conform rotation to the surface it is on
	// NewRotation XAxis: Velocity vector projected onto the surface plane
	// NewRotation ZAxis: Surface plane normal
	FVector VelPlaneDir = FVector::VectorPlaneProject(Velocity, SurfaceHit.Normal).GetSafeNormal();
	FQuat NewRotation = FRotationMatrix::MakeFromXZ(VelPlaneDir, SurfaceHit.Normal).ToQuat();
	// Actually move the character
	// Adjusted: Delta position
	// NewRotation: Absolute rotation
	// bSweep: true so we don't teleport the capsule but move it allowing for collisions to occur between current and target position (preventing clipping)
	// Hit: The hit result in the event we hit something in the sweep
	SafeMoveUpdatedComponent(Adjusted, NewRotation, true, Hit);

	// Check did we hit something?
	if (Hit.Time < 1.0f)
	{
		// Handle that impact
		HandleImpact(Hit, deltaTime, Adjusted);
		// If we do hit something we don't want to suddenly stop but then slide along this new surface based on the hit
		SlideAlongSurface(Adjusted, (1.0f - Hit.Time), Hit.Normal, Hit, true);
	}

	// Check if the slide conditions are still met
	FHitResult NewSurfaceHit;
	if (!GetSlideSurface(NewSurfaceHit) || Velocity.SizeSquared() < pow(Slide_MinSpeed, 2))
	{
		ExitSlide();
	}

	// Update outgoing Velocity & Acceleration
	if (!bJustTeleported && !HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity())
	{
		Velocity = (UpdatedComponent->GetComponentLocation() - OldLocation) / deltaTime; // v = dx / dt
	}
}

bool UAdvCharacterMovementComponent::GetSlideSurface(FHitResult& Hit) const
{
	FVector Start = UpdatedComponent->GetComponentLocation();
	FVector End = Start + CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * 2.0f * FVector::DownVector;
	FName ProfileName = TEXT("BlockAll");
	return GetWorld()->LineTraceSingleByProfile(Hit, Start, End, ProfileName, AdvancedCharacterOwner->GetIgnoreCharacterParams());
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