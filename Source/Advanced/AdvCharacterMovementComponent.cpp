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
			MaxWalkSpeed = Sprint_MaxWalkSpeed;
		}
		else
		{
			MaxWalkSpeed = Walk_MaxWalkSpeed;
		}
	}

	// Set after movement so valid for the next frame
	Safe_bPrevWantsToCrouch = bWantsToCrouch;
}

void UAdvCharacterMovementComponent::UpdateCharacterStateBeforeMovement(float DeltaSeconds)
{
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
	default:
		UE_LOG(LogTemp, Fatal, TEXT("Invalid Movement Mode"))
	}
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
}

void UAdvCharacterMovementComponent::FSavedMove_Adv::PrepMoveFor(ACharacter* C)
{
	Super::PrepMoveFor(C);

	UAdvCharacterMovementComponent* CharacterMovement = Cast<UAdvCharacterMovementComponent>(C->GetCharacterMovement());

	CharacterMovement->Safe_bWantsToSprint = Saved_bWantsToSprint;
	CharacterMovement->Safe_bPrevWantsToCrouch = Saved_bPrevWantsToCrouch;
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
}

bool UAdvCharacterMovementComponent::IsCustomMovementMode(ECustomMovementMode InCustomMovementMode) const
{
	return MovementMode == MOVE_Custom && CustomMovementMode == InCustomMovementMode;
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
		UPrimitiveComponent * const oldBase = GetMovementBase();
		const FVector PreviousBaseLocation = (oldBase != nullptr) ? oldBase->GetComponentLocation() : FVector::ZeroVector;
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

		
	}
}

#pragma endregion Prone