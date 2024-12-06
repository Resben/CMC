#pragma once
#include "CoreMinimal.h"
#include "AdvancedCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "AdvCharacterMovementComponent.generated.h"

/// 1. You can alter movement safe variables in non-movement safe functions on the client
/// 2. You can never utilise non-movement safe variables in a movement safe function
/// 3. You can't call non-movement safe functions that alter movement safe variables on the server

// @todo look into delegates
// An event that you can create and broadcast (A signal?)
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FDashStartDelegate);

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
	public:
		enum CompressedFlags
		{
			FLAG_Sprint		= 0x10,
			FLAG_Dash		= 0x20,
			FLAG_Custom_1	= 0x40,
			FLAG_Custom_2	= 0x80,
		};
		
		typedef FSavedMove_Character Super;

		// FLAGS
		uint8 Saved_bWantsToSprint : 1;
		uint8 Saved_bWantsToDash : 1;

		// Non-Flags
		uint8 Saved_bPrevWantsToCrouch : 1;
		uint8 Saved_bWantsToProne : 1;
		
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

	UPROPERTY(EditDefaultsOnly) float Sprint_MaxSpeed = 750.0f;
	
	UPROPERTY(EditDefaultsOnly) float Slide_MaxSpeed = 400.0f;
	UPROPERTY(EditDefaultsOnly) float Slide_MinSpeed = 400.0f;
	UPROPERTY(EditDefaultsOnly) float Slide_EnterImpulse = 400.0f;
	UPROPERTY(EditDefaultsOnly) float Slide_GravityForce = 4000.0f;
	UPROPERTY(EditDefaultsOnly) float Slide_FrictionFactor = 0.06f;
	UPROPERTY(EditDefaultsOnly) float Slide_MaxBreakingDeceleration = 1000.0f;

	UPROPERTY(EditDefaultsOnly) float Prone_EnterHoldDuration = 0.2f;
	UPROPERTY(EditDefaultsOnly) float Prone_SlideEnterImpulse = 300.0f;
	UPROPERTY(EditDefaultsOnly) float Prone_MaxSpeed = 300.0f;
	UPROPERTY(EditDefaultsOnly) float Prone_MaxBreakingDeceleration = 2500.0f;

	UPROPERTY(EditDefaultsOnly) float Dash_Impulse = 1000.0f;
	UPROPERTY(EditDefaultsOnly) float Dash_CooldownDuration = 1.0f;
	// Prevents cheating by making sure cooldown is not half for example it's dash time allowing for a margin of
	// error of 0.1f since DeltaTime is actually not perfectly synced on the client and server
	UPROPERTY(EditDefaultsOnly) float Dash_AuthCooldownDuration = 0.9f;
	
	// Transient
	UPROPERTY(Transient) AAdvancedCharacter* AdvancedCharacterOwner;
	
	bool Safe_bWantsToSprint;
	bool Safe_bPrevWantsToCrouch;
	bool Safe_bWantsToProne;
	bool Safe_bWantsToDash;
	
	float DashStartTime;
	
	FTimerHandle TimerHandle_EnterProne;
	FTimerHandle TimerHandle_DashCooldown;

	// Replication
	UPROPERTY(ReplicatedUsing=OnRep_DashStart) bool Proxy_bDashStart;

public:
	virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;
	virtual bool IsMovingOnGround() const override;
	virtual bool CanCrouchInCurrentState() const override;
	virtual float GetMaxSpeed() const override;
	virtual float GetMaxBrakingDeceleration() const override;
	UAdvCharacterMovementComponent();

private:
	// Slide
	void EnterSlide(EMovementMode PrevMode, ECustomMovementMode PrevCustomMode);
	void ExitSlide();
	void PhysSlide(float deltaTime, int32 Iterations);
	bool CanSlide() const;

	// Prone
	void TryEnterProne() { Safe_bWantsToProne = true; }
	UFUNCTION(Server, Reliable) void Server_EnterProne();
	
	void EnterProne(EMovementMode PrevMode, ECustomMovementMode PrevMovementMode);
	void ExitProne();
	bool CanProne() const;
	void PhysProne(float deltaTime, int32 Iterations);

	// Dash
	void OnDashCooldownFinished();
	bool CanDash() const;
	void PerformDash();
	
protected:
	virtual void InitializeComponent() override;
	virtual void UpdateFromCompressedFlags(uint8 Flags) override;
	virtual void OnMovementUpdated(float DeltaSeconds, const FVector& OldLocation, const FVector& OldVelocity) override;
	virtual void UpdateCharacterStateBeforeMovement(float DeltaSeconds) override;
	virtual void PhysCustom(float deltaTime, int32 Iterations) override;
	virtual void OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode) override;
	
	/// Custom blueprint export functions
public:
	UPROPERTY(BlueprintAssignable) FDashStartDelegate DashStartDelegate;
	
	UFUNCTION(BlueprintCallable) void SprintPressed();
	UFUNCTION(BlueprintCallable) void SprintReleased();
	UFUNCTION(BlueprintCallable) void CrouchPressed();
	UFUNCTION(BlueprintCallable) void CrouchReleased();
	UFUNCTION(BlueprintCallable) void DashPressed();
	UFUNCTION(BlueprintCallable) void DashReleased();

	
	UFUNCTION(BlueprintPure) bool IsCustomMovementMode(ECustomMovementMode InCustomMovementMode) const;
	UFUNCTION(BlueprintPure) bool IsMovementMode(EMovementMode InMovementMode) const;

	// Can move replication to the base character to save bandwidth
public:
	virtual void GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const override;
private:
	UFUNCTION() void OnRep_DashStart();
};
