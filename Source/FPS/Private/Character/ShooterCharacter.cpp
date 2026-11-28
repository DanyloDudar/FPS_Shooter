// Copyright Dudar Danylo


#include "Character/ShooterCharacter.h"

#include "Camera/CameraComponent.h"
#include "Combat/CombatComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "EnhancedInputComponent.h"
#include "Data/WeaponData.h"
#include "Kismet/KismetMathLibrary.h"
#include "Weapon/Weapon.h"

AShooterCharacter::AShooterCharacter()
{
	PrimaryActorTick.bCanEverTick = true;
	
	GetCharacterMovement()->MovementState.bCanCrouch = true;
	
	SpringArm = CreateDefaultSubobject<USpringArmComponent>("SpringArm");
	SpringArm->SetupAttachment(GetRootComponent());
	SpringArm->TargetArmLength = 0.f;
	SpringArm->bEnableCameraLag = true;
	SpringArm->CameraLagSpeed = 15.f;
	SpringArm->bUsePawnControlRotation = true;
	
	FirstPersonCamera = CreateDefaultSubobject<UCameraComponent>("FirstPersonCamera");
	FirstPersonCamera->SetupAttachment(SpringArm);
	FirstPersonCamera->bUsePawnControlRotation = false;
	
	Mesh1P = CreateDefaultSubobject<USkeletalMeshComponent>("Mesh1P");
	Mesh1P->SetupAttachment(FirstPersonCamera);
	Mesh1P->bOnlyOwnerSee = true;
	Mesh1P->bOwnerNoSee = false;
	Mesh1P->CastShadow = false;
	Mesh1P->bReceivesDecals = false;
	Mesh1P->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	Mesh1P->PrimaryComponentTick.TickGroup = TG_PrePhysics;
	
	GetMesh()->bOnlyOwnerSee = false;
	GetMesh()->bOwnerNoSee = true;
	GetMesh()->bReceivesDecals = false;
	
	CombatComponent = CreateDefaultSubobject<UCombatComponent>("CombatComponent");
	CombatComponent->SetIsReplicated(true);
	
	DefaultFieldOfView = 90.f;
	TurningStatus = ETurningInPlace::NotTurning;
}

void AShooterCharacter::BeginPlay()
{
	Super::BeginPlay();
	
	FirstPersonCamera->SetFieldOfView(DefaultFieldOfView);
}

void AShooterCharacter::BeginDestroy()
{
	Super::BeginDestroy();
	
	if (IsValid(CombatComponent))
	{
		CombatComponent->DestroyInventory();
	}
	StartingAimRotation = FRotator(0.f, GetBaseAimRotation().Yaw, 0.f);
}

FRotator AShooterCharacter::GetFixedAimRotation() const
{
	FRotator AimRotation = GetBaseAimRotation();
	if (AimRotation.Pitch > 90.f && !IsLocallyControlled())
	{
		// map pitch from [270, 360) to [-90, 0] 
		const FVector2D InRange(270.f, 360.f);
		const FVector2D OutRange(-90.f, 0.f);
		AimRotation.Pitch = FMath::GetMappedRangeValueClamped(InRange, OutRange, AimRotation.Pitch);
	}
	
	return AimRotation;
	
}

bool AShooterCharacter::HasCurrentWeapon() const
{
	return IsValid(CombatComponent) && CombatComponent->CurrentWeapon != nullptr;
}


void AShooterCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	
	CalculateTurnInPlaceParameters(DeltaTime);
	CalculateFABRIK_SocketTransform();
}

void AShooterCharacter::CalculateTurnInPlaceParameters(float DeltaTime)
{
	FVector Velocity = GetVelocity();
	Velocity.Z = 0.f;
	float Speed = Velocity.Size();
	bool bIsInAir = GetCharacterMovement()->IsFalling();
	
	if (Speed == 0.f && !bIsInAir) //Standing still && not jumping
	{
		FRotator CurrentAimRotation(0.f, GetBaseAimRotation().Yaw, 0.f);
		// StartingAimRotation initially set in BeginPlay
		FRotator DeltaAimRotation = UKismetMathLibrary::NormalizedDeltaRotator(CurrentAimRotation, StartingAimRotation);
		AO_Yaw = DeltaAimRotation.Yaw;
		
		if (TurningStatus == ETurningInPlace::NotTurning)
		{
			InterpAO_Yaw = AO_Yaw;
		}
		
		TurnInPlace(DeltaTime); // interpolates the InterpAO_Yaw value to zero.
	}
		
	if (Speed > 0.f || bIsInAir)
	{
		StartingAimRotation = FRotator(0.f, GetBaseAimRotation().Yaw, 0.f);
		AO_Yaw = 0.f;
		
		FRotator AimRotation = GetBaseAimRotation();
		FRotator MovementRotation = UKismetMathLibrary::MakeRotFromX(GetVelocity());
		FRotator DeltaOffset = UKismetMathLibrary::NormalizedDeltaRotator(AimRotation, MovementRotation);
		MovementOffsetYaw = DeltaOffset.Yaw;
		TurningStatus = ETurningInPlace::NotTurning;
	}
	AO_Yaw *=-1.f;
	
}

void AShooterCharacter::TurnInPlace(float DeltaTime)
{
	if (AO_Yaw > 90.f)
	{
		TurningStatus = ETurningInPlace::Right;
	}
	else if (AO_Yaw < -90.f)
	{
		TurningStatus = ETurningInPlace::Left;
	}
	if (TurningStatus != ETurningInPlace::NotTurning) // we are turning
	{
		InterpAO_Yaw = FMath::FInterpTo(InterpAO_Yaw, 0.f, DeltaTime, 4.0f);
		AO_Yaw = InterpAO_Yaw;
		 if (FMath::Abs(AO_Yaw) < 5.f)
		 {
		 	TurningStatus = ETurningInPlace::NotTurning;
		 	StartingAimRotation = FRotator(0.f, GetBaseAimRotation().Yaw, 0.f);
		 }
	}
		
}


void AShooterCharacter::CalculateFABRIK_SocketTransform()
{
	if (IsValid(CombatComponent) && IsValid(CombatComponent->CurrentWeapon) && IsValid(CombatComponent->CurrentWeapon->GetMesh3P()))
	{
		FABRIK_SocketTransform = CombatComponent->CurrentWeapon->GetMesh3P()->GetSocketTransform("FABRIK_Socket", RTS_World);
		FVector OutLocation;
		FRotator OutRotation;
		GetMesh()->TransformToBoneSpace(
			"hand_r", 
			FABRIK_SocketTransform.GetLocation(), 
			FABRIK_SocketTransform.GetRotation().Rotator(), 
			OutLocation, 
			OutRotation);
		FABRIK_SocketTransform.SetLocation(OutLocation);
		FABRIK_SocketTransform.SetRotation(OutRotation.Quaternion());
	}
}


void AShooterCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	
	UEnhancedInputComponent* ShooterInputComponent = CastChecked<UEnhancedInputComponent>(PlayerInputComponent);
	
	ShooterInputComponent->BindAction(CycleWeaponAction, ETriggerEvent::Started, this, &ThisClass::Input_CycleWeapon);
	ShooterInputComponent->BindAction(FireWeaponAction, ETriggerEvent::Started, this, &ThisClass::Input_FireWeapon_Pressed);
	ShooterInputComponent->BindAction(FireWeaponAction, ETriggerEvent::Completed, this, &ThisClass::Input_FireWeapon_Released);
	ShooterInputComponent->BindAction(AimWeaponAction, ETriggerEvent::Started, this, &ThisClass::Input_AimWeapon_Pressed);
	ShooterInputComponent->BindAction(AimWeaponAction, ETriggerEvent::Completed, this, &ThisClass::Input_AimWeapon_Released);
	ShooterInputComponent->BindAction(ReloadWeaponAction, ETriggerEvent::Started, this, &ThisClass::Input_ReloadWeapon);
	
}

void AShooterCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	if (IsValid(CombatComponent))
	{
		CombatComponent->SpawnInventory();
	}
}

FName AShooterCharacter::GetWeaponAttachPoint_Implementation(const FGameplayTag& WeaponType) const
{
	checkf(CombatComponent->WeaponData, TEXT("No Weapon Data Assets - Please fill out BP_ShooterCharacter"));
	return CombatComponent->WeaponData->GripPoints.FindChecked(WeaponType);
}

USkeletalMeshComponent* AShooterCharacter::GetMesh1P_Implementation() const
{
	return Mesh1P;
}

USkeletalMeshComponent* AShooterCharacter::GetMesh3P_Implementation() const
{
	// Default mesh
	return GetMesh();
}

void AShooterCharacter::Input_CycleWeapon()
{
	CombatComponent->Initiate_CycleWeapon();
}

void AShooterCharacter::Input_ReloadWeapon()
{
	CombatComponent->Initiate_ReloadWeapon();
}

void AShooterCharacter::Input_FireWeapon_Pressed()
{
	CombatComponent->Initiate_FireWeapon_Pressed();
}

void AShooterCharacter::Input_FireWeapon_Released()
{
	CombatComponent->Initiate_FireWeapon_Released();
}

void AShooterCharacter::Input_AimWeapon_Pressed()
{
	CombatComponent->Initiate_Aim_Pressed();
	OnAim(true);
}

void AShooterCharacter::Input_AimWeapon_Released()
{
	CombatComponent->Initiate_Aim_Released();
	OnAim(false);
}



