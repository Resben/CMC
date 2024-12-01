#include "AdvCharacterMovementComponent.h"
#include "GameFramework/Character.h"

#pragma region Character Movement Component

UAdvCharacterMovementComponent::UAdvCharacterMovementComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Walk_MaxWalkSpeed = 500.0f;
	Sprint_MaxWalkSpeed = 1000.0f;
	Safe_bWantsToSprint = false;

	NavAgentProps.bCanCrouch = true;
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
}
#pragma endregion Character Movement Component

#pragma region Save Move

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
}

void UAdvCharacterMovementComponent::FSavedMove_Adv::PrepMoveFor(ACharacter* C)
{
	Super::PrepMoveFor(C);

	UAdvCharacterMovementComponent* CharacterMovement = Cast<UAdvCharacterMovementComponent>(C->GetCharacterMovement());

	CharacterMovement->Safe_bWantsToSprint = Saved_bWantsToSprint;
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

#pragma region Input

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


#pragma endregion Input