// Fill out your copyright notice in the Description page of Project Settings.


#include "AdvPlayerCameraManager.h"
#include "AdvancedCharacter.h"
#include "Components/CapsuleComponent.h"

AAdvPlayerCameraManager::AAdvPlayerCameraManager()
{
	
}

void AAdvPlayerCameraManager::UpdateViewTarget(FTViewTarget& OutVT, float DeltaTime)
{
	Super::UpdateViewTarget(OutVT, DeltaTime);

	if (AAdvancedCharacter * AdvCharacter = Cast<AAdvancedCharacter>(GetOwningPlayerController()->GetPawn()))
	{
		UAdvCharacterMovementComponent* AMC = AdvCharacter->GetAdvancedCharacterMovementComponent();
		FVector TargetCrouchOffset = FVector(
			// x and y are irrelevant only height needed
			0, 0,
			// Must use a workaround to get the default object height
			AMC->GetCrouchedHalfHeight() - AdvCharacter->GetClass()->GetDefaultObject<ACharacter>()->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()
			);
		
		// Actual offset to update
		FVector Offset = FMath::Lerp(FVector::ZeroVector, TargetCrouchOffset, FMath::Clamp(CrouchBlendTime, 0.0f, 1.0f));
		
		if (AMC->IsCrouching())
		{
			// We update crouch blend time from 0 to CrouchBlendDuration allowing the Lerp to work with DeltaTime
			CrouchBlendTime = FMath::Clamp(CrouchBlendTime + DeltaTime, 0.0f, CrouchBlendDuration);
			// This negates the original effect of camera jumping to the TargetCrouchOffset (The default functionality)
			Offset -= TargetCrouchOffset;
		}
		else
		{
			// Opposite: CrouchBlendDuration to 0
			CrouchBlendTime = FMath::Clamp(CrouchBlendTime - DeltaTime, 0.0f, CrouchBlendDuration);
		}

		// Ensure the player is on the group if we want to change the offset (Camera crouching in air won't work)
		if (AMC->IsMovingOnGround())
		{
			OutVT.POV.Location += Offset;
		}
	}
}
