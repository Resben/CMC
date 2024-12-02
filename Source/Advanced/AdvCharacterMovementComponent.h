#pragma once
#include "CoreMinimal.h"
#include "AdvancedCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "AdvCharacterMovementComponent.generated.h"

/// 1. You can alter movement safe variables in non-movement safe functions on the client
/// 2. You can never utilise non-movement safe variables in a movement safe function
/// 3. You can't call non-movement safe functions that alter movement safe variables on the server

UENUM(BlueprintType)
enum ECustomMovementMode
{
	CMOVE_None		UMETA(Hidden),
	CMOVE_Slide		UMETA(DisplayName = "Slide"),
	CMOVE_Prone		UMETA(DisplayName = "Prone"),
	CMOVE_Max		UMETA(Hidden),
};

UCLASS()
class ADVANCED_API UAdvCharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

	/// Our version for saving a move allowing us to save custom data
	/// Supports server authoritative behaviour
	class FSavedMove_Adv : public FSavedMove_Character
	{
		typedef FSavedMove_Character Super;

		// FLAGS
		uint8 Saved_bWantsToSprint : 1;

		// Non-Flags
		uint8 Saved_bPrevWantsToCrouch : 1;
		
	public:
		FSavedMove_Adv();
		
		virtual bool CanCombineWith(const FSavedMovePtr& newMove, ACharacter* InCharacter, float MaxDelta) const override;
		virtual void Clear() override;
		virtual uint8 GetCompressedFlags() const override;
		virtual void SetMoveFor(ACharacter* C, float InDeltaTime, FVector const& NewAccel, FNetworkPredictionData_Client_Character& ClientData) override;
		virtual void PrepMoveFor(ACharacter* C) override;
	};

	/// Refines Network Prediction to allow for our custom FSavedMove_Adv
	class FNetworkPredictionData_Client_Adv : public FNetworkPredictionData_Client_Character
	{
	public:
		FNetworkPredictionData_Client_Adv(const UCharacterMovementComponent& ClientMovement);

		typedef FNetworkPredictionData_Client_Character Super;

		virtual FSavedMovePtr AllocateNewMove() override;
	};

	UPROPERTY(EditDefaultsOnly) float Sprint_MaxWalkSpeed = 1000.0f;
	UPROPERTY(EditDefaultsOnly) float Walk_MaxWalkSpeed = 500.0f;

	UPROPERTY(EditDefaultsOnly) float Slide_MinSpeed = 350.0f;
	UPROPERTY(EditDefaultsOnly) float Slide_EnterImpulse = 500.0f;
	UPROPERTY(EditDefaultsOnly) float Slide_GravityForce = 5000.0f;
	UPROPERTY(EditDefaultsOnly) float Slide_Friction = 1.3f;

	UPROPERTY(EditDefaultsOnly) float Prone_EnterHoldDuration = 0.2f;
	UPROPERTY(EditDefaultsOnly) float Prone_SlideEnterImpulse = 300.0f;
	UPROPERTY(EditDefaultsOnly) float Prone_MaxSpeed = 300.0f;
	UPROPERTY(EditDefaultsOnly) float Prone_BreakingDeceleration = 2500.0f;
	
	UPROPERTY(Transient) AAdvancedCharacter* AdvancedCharacterOwner;
	
	bool Safe_bWantsToSprint;
	bool Safe_bPrevWantsToCrouch;


public:
	virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;
	virtual bool IsMovingOnGround() const override;
	virtual bool CanCrouchInCurrentState() const override; 
	UAdvCharacterMovementComponent();

private:
	// Slide
	void EnterSlide();
	void ExitSlide();
	void PhysSlide(float deltaTime, int32 Iterations);
	bool GetSlideSurface(FHitResult& Hit) const;

	// Prone
	void EnterProne(EMovementMode PrevMode, ECustomMovementMode PrevMovementMode);
	void ExitProne();
	bool CanProne() const;
	void PhysProne(float deltaTime, int32 Iterations);
	
protected:
	virtual void InitializeComponent() override;
	virtual void UpdateFromCompressedFlags(uint8 Flags) override;
	virtual void OnMovementUpdated(float DeltaSeconds, const FVector& OldLocation, const FVector& OldVelocity) override;
	virtual void UpdateCharacterStateBeforeMovement(float DeltaSeconds) override;
	virtual void PhysCustom(float deltaTime, int32 Iterations) override;
	
	/// Custom blueprint export functions
public:
	UFUNCTION(BlueprintCallable) void SprintPressed();
	UFUNCTION(BlueprintCallable) void SprintReleased();
	UFUNCTION(BlueprintCallable) void CrouchPressed();
	UFUNCTION(BlueprintCallable) bool IsCustomMovementMode(ECustomMovementMode InCustomMovementMode) const;
};
