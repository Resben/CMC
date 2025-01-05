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
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnStateChanged);

UENUM(BlueprintType)
enum ECustomMovementMode
{
	CMOVE_None		UMETA(Hidden),
	CMOVE_Slide		UMETA(DisplayName = "Slide"),
	CMOVE_WallRun	UMETA(DisplayName = "Wall Run"),
	CMOVE_Hang	UMETA(DisplayName = "Hang"),
	CMOVE_Climb 	UMETA(DisplayName = "Climb"),
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
		uint8 Saved_bPressedAdvanceJump : 1;

		// Non-Flags
		uint8 Saved_bPrevWantsToCrouch : 1;
		uint8 Saved_bWantsToProne : 1;
		uint8 Saved_bHadAnimRootMotion : 1;
		uint8 Saved_bTransitionFinished : 1;
		uint8 Saved_bWallRunIsRight : 1;
		uint8 Saved_bCanClimbAgain : 1;
		
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

	UPROPERTY(EditDefaultsOnly) bool Setting_GravityEnabledDash = true;
	UPROPERTY(EditDefaultsOnly) float Sprint_MaxSpeed = 750.0f;
	
	UPROPERTY(EditDefaultsOnly) float Slide_MaxSpeed = 750.0f;
	UPROPERTY(EditDefaultsOnly) float Slide_MinEnterSpeed = 600.0f;
	UPROPERTY(EditDefaultsOnly) float Slide_MinExitSpeed = 200.0f;
	UPROPERTY(EditDefaultsOnly) float Slide_EnterImpulse = 400.0f;
	UPROPERTY(EditDefaultsOnly) float Slide_GravityForce = 4000.0f;
	UPROPERTY(EditDefaultsOnly) UCurveFloat* Slide_FrictionCurveFactor;
	UPROPERTY(EditDefaultsOnly) float Slide_MaxBrakingDeceleration = 1000.0f;
	
	UPROPERTY(EditDefaultsOnly) float Dash_Impulse = 1000.0f;
	UPROPERTY(EditDefaultsOnly) float Dash_CooldownDuration = 1.0f;
	// Prevents cheating by making sure cooldown is not half for example it's dash time allowing for a margin of
	// error of 0.1f since DeltaTime is actually not perfectly synced on the client and server
	UPROPERTY(EditDefaultsOnly) float Dash_AuthCooldownDuration = 0.9f;
	UPROPERTY(EditDefaultsOnly) UAnimMontage* Dash_Montage;

	UPROPERTY(EditDefaultsOnly) float Mantle_MaxDistance = 200;
	UPROPERTY(EditDefaultsOnly) float Mantle_MinDepth = 30;
	UPROPERTY(EditDefaultsOnly) float Mantle_MinWallSteepnessAngle = 75;
	UPROPERTY(EditDefaultsOnly) float Mantle_MaxSurfaceAngle = 40;
	UPROPERTY(EditDefaultsOnly) float Mantle_MaxAlignmentAngle = 45;
	UPROPERTY(EditDefaultsOnly) float Mantle_MinTransitionTime = 0.1;
	UPROPERTY(EditDefaultsOnly) float Mantle_MaxTransitionTime = 0.25;

	UPROPERTY(EditDefaultsOnly) float Mantle_MinShortVaultHeight = 60.f;
	UPROPERTY(EditDefaultsOnly) float Mantle_MinTallVaultHeight = 110.f;
	UPROPERTY(EditDefaultsOnly) float Mantle_MaxVaultHeight = 150.f;

	UPROPERTY(EditDefaultsOnly) float Mantle_MinShortClimbHeight = 60.f;
	UPROPERTY(EditDefaultsOnly) float Mantle_MinTallClimbHeight = 180.f;
	UPROPERTY(EditDefaultsOnly) float Mantle_MaxClimbHeight = 230.f;
	
	UPROPERTY(EditDefaultsOnly) UAnimMontage* Mantle_TallClimbMontage;
	UPROPERTY(EditDefaultsOnly) UAnimMontage* Mantle_TransitionTallClimbMontage;
	UPROPERTY(EditDefaultsOnly) UAnimMontage* Mantle_ProxyTallClimbMontage;
	
	UPROPERTY(EditDefaultsOnly) UAnimMontage* Mantle_ShortClimbMontage;
	UPROPERTY(EditDefaultsOnly) UAnimMontage* Mantle_TransitionShortClimbMontage;
	UPROPERTY(EditDefaultsOnly) UAnimMontage* Mantle_ProxyShortClimbMontage;
	
	UPROPERTY(EditDefaultsOnly) UAnimMontage* Mantle_TallVaultMontage;
	UPROPERTY(EditDefaultsOnly) UAnimMontage* Mantle_TransitionTallVaultMontage;
	UPROPERTY(EditDefaultsOnly) UAnimMontage* Mantle_ProxyTallVaultMontage;

	UPROPERTY(EditDefaultsOnly) UAnimMontage* Mantle_ShortVaultMontage;
	UPROPERTY(EditDefaultsOnly) UAnimMontage* Mantle_TransitionShortVaultMontage;
	UPROPERTY(EditDefaultsOnly) UAnimMontage* Mantle_ProxyShortVaultMontage;

	UPROPERTY(EditDefaultsOnly) float WallRun_MinSpeed = 200.f;
	UPROPERTY(EditDefaultsOnly) float WallRun_MaxSpeed = 800.f;
	UPROPERTY(EditDefaultsOnly) float WallRun_MaxVerticalSpeed = 200.f;
	UPROPERTY(EditDefaultsOnly) float WallRun_PullAwayAngle = 75;
	UPROPERTY(EditDefaultsOnly) float WallRun_AttractionForce = 200.f;
	UPROPERTY(EditDefaultsOnly) float WallRun_MinHeight = 50.f;
	UPROPERTY(EditDefaultsOnly) UCurveFloat* WallRun_GravityScaleCurve;
	UPROPERTY(EditDefaultsOnly) float WallRun_JumpOffForce = 300.f;

	UPROPERTY(EditDefaultsOnly) float Hang_MinTransitionTime = 0.1;
	UPROPERTY(EditDefaultsOnly) float Hang_MaxTransitionTime = 0.25;
	UPROPERTY(EditDefaultsOnly) UAnimMontage* Hang_TransitionMontage;
	UPROPERTY(EditDefaultsOnly) UAnimMontage* Hang_WallJumpMontage;
	UPROPERTY(EditDefaultsOnly) float Hang_WallJumpForce = 400.f;

	UPROPERTY(EditDefaultsOnly) float Climb_MaxSpeed = 300.f;
	UPROPERTY(EditDefaultsOnly) float Climb_BrakingDeceleration = 1000.f;
	UPROPERTY(EditDefaultsOnly) float Climb_ReachDistance = 50.f;
	UPROPERTY(EditDefaultsOnly) float Climb_MaxDuration = 3.f;
	UPROPERTY(EditDefaultsOnly) float Climb_MantleCheckInterval = 0.5f;
	UPROPERTY(EditDefaultsOnly) float Climb_GravityScaleCurve = 0.4f;
	UPROPERTY(EditDefaultsOnly) float Climb_MaxDownwardVelocity = -300.f;
	
	// Transient
	UPROPERTY(Transient) AAdvancedCharacter* AdvancedCharacterOwner;

	// Flags
	bool Safe_bWantsToSprint;
	bool Safe_bWantsToProne;
	bool Safe_bWantsToDash;

	// Non-Flags
	bool Safe_bPrevWantsToCrouch;
	bool Safe_bHadAnimRootMotion;
	bool Safe_bWallRunIsRight;
	bool Safe_bCanClimbAgain;
	
	bool Safe_bTransitionFinished;
	TSharedPtr<FRootMotionSource_MoveToForce> TransitionRMS;
	FString TransitionName;
	UPROPERTY(Transient) UAnimMontage* TransitionQueuedMontage;
	float TransitionQueuedMontageSpeed;
	int TransitionRMS_ID;
	
	float DashStartTime;
	FTimerHandle TimerHandle_DashCooldown;
	
	// Replication
	UPROPERTY(ReplicatedUsing=OnRep_DashStart) bool Proxy_bDashStart;

	UPROPERTY(ReplicatedUsing=OnRep_ShortMantle) bool Proxy_bShortMantle;
	UPROPERTY(ReplicatedUsing=OnRep_TallMantle) bool Proxy_bTallMantle;
	UPROPERTY(ReplicatedUsing=OnRep_ShortVault) bool Proxy_bShortVault;
	UPROPERTY(ReplicatedUsing=OnRep_TallVault) bool Proxy_bTallVault;

	// UnSafe because I just don't understand it
	bool UnSafe_bWantsToSlide;
	float ClimbMantleCheckAccumulator = 0.0f;
	
public:
	virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;
	virtual bool IsMovingOnGround() const override;
	virtual bool CanCrouchInCurrentState() const override;
	virtual float GetMaxSpeed() const override;
	virtual float GetMaxBrakingDeceleration() const override;
	virtual bool CanAttemptJump() const override;
	virtual bool DoJump(bool bReplayingMoves) override;
	UAdvCharacterMovementComponent();

	// Slide
private:
	void EnterSlide(EMovementMode PrevMode, ECustomMovementMode PrevCustomMode);
	void ExitSlide();
	void PhysSlide(float deltaTime, int32 Iterations);
	bool CanEnterSlide() const;
	bool ShouldExitSlide() const;

	// NOTE: THESE ARE NOT SIMULATED ON PROXIES
	void HandleCustomCrouch();
	void HandleCustomUnCrouch();

public:
	void ExitSlideMode();

	// Dash
private:
	void OnDashCooldownFinished();
	bool CanDash() const;
	void PerformDash();

	// Mantle
	bool TryMantle();
	FVector GetMantleStartLocation(const FHitResult& FrontHit, const FHitResult& SurfaceHit, const bool bTallMantle, const std::string& Type) const;
	void SetMantleMontages(const std::string& Type, const bool bTallMantle);
	
	// Wall Run
	bool TryWallRun();
	void PhysWallRun(float deltaTime, int32 Iterations);

	// Hang
	bool TryHang();

	// Climb
	bool TryClimb();
	void PhysClimb(float deltaTime, int32 Iterations);
	float ClimbTimeRemaining = 0.0f; // Maybe save this as well?
	
	// Helpers
	bool IsServer() const;
	float CapR() const;
	float CapHH() const;
	
	
protected:
	virtual void InitializeComponent() override;
	virtual void UpdateFromCompressedFlags(uint8 Flags) override;
	virtual void OnMovementUpdated(float DeltaSeconds, const FVector& OldLocation, const FVector& OldVelocity) override;
	virtual void UpdateCharacterStateBeforeMovement(float DeltaSeconds) override;
	virtual void UpdateCharacterStateAfterMovement(float DeltaSeconds) override;
	virtual void PhysCustom(float deltaTime, int32 Iterations) override;
	virtual void OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode) override;
	
	/// Custom blueprint export functions
public:
	UPROPERTY(BlueprintAssignable) FDashStartDelegate DashStartDelegate;

	UPROPERTY(BlueprintAssignable, Category="Movement") FOnStateChanged OnStateChangedDelegate; 
	
	UFUNCTION(BlueprintCallable) void SprintPressed();
	UFUNCTION(BlueprintCallable) void SprintReleased();
	UFUNCTION(BlueprintCallable) void CrouchPressed();
	UFUNCTION(BlueprintCallable) void CrouchReleased();
	UFUNCTION(BlueprintCallable) void DashPressed();
	UFUNCTION(BlueprintCallable) void DashReleased();

	UFUNCTION(BlueprintPure) bool IsCustomMovementMode(ECustomMovementMode InCustomMovementMode) const;
	UFUNCTION(BlueprintPure) bool IsMovementMode(EMovementMode InMovementMode) const;

	UFUNCTION(BlueprintPure) bool IsSliding() const { return IsCustomMovementMode(CMOVE_Slide); }
	
	UFUNCTION(BlueprintPure) bool IsWallRunning() const { return IsCustomMovementMode(CMOVE_WallRun); }
	UFUNCTION(BlueprintPure) bool WallRunningIsRight() const { return Safe_bWallRunIsRight; }

	UFUNCTION(BlueprintPure) bool IsHanging() const { return IsCustomMovementMode(CMOVE_Hang); }
	UFUNCTION(BlueprintPure) bool IsClimbing() const { return IsCustomMovementMode(CMOVE_Climb); }
	
	// Can move replication to the base character to save bandwidth
public:
	virtual void GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const override;
private:
	UFUNCTION() void OnRep_DashStart();
	UFUNCTION() void OnRep_ShortMantle();
	UFUNCTION() void OnRep_TallMantle();
	UFUNCTION() void OnRep_ShortVault();
	UFUNCTION() void OnRep_TallVault();
};
