// Fill out your copyright notice in the Description page of Project Settings.


#include "VRCharacterBase.h"
#include "Engine/World.h"
//#include "Classes/Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "IHeadMountedDisplay.h"
#include "XRMotionControllerBase.h"
#include "Components/SceneComponent.h"
#include <exception>

// Sets default values
AVRCharacterBase::AVRCharacterBase()
{
 	// Set this character to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	VRRoot = CreateDefaultSubobject<USceneComponent>(TEXT("VRRoot")); //Creating VrOrigin Scene Component
	VRRoot->SetupAttachment(GetRootComponent()); //Attaching VrOrigin to root component (this case a capsule component)

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera")); //Creating the camera
	Camera->SetupAttachment(VRRoot); //attaches camera to Vr Origin

	//FootStep = CreateDefaultSubobject<UFootSteps>(TEXT("Foot Steps Component")); //Attaches the footstep to the VrCharacter

	
	LeftController = CreateDefaultSubobject<UMotionControllerComponent>(TEXT("Left Controller"));
	LeftController->SetupAttachment(VRRoot);
	LeftController->SetTrackingMotionSource(FXRMotionControllerBase::LeftHandSourceId);
	LeftController->SetRelativeLocation(FVector(0.f, 0.f, 110.f));

	LeftGrabber = CreateDefaultSubobject<UGrabber>(TEXT("Left Grabber"));
	LeftGrabber->SetupAttachment(LeftController);

	LeftHand = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Left Hand"));
	LeftHand->SetupAttachment(LeftController);

	RightController = CreateDefaultSubobject<UMotionControllerComponent>(TEXT("Right Controller"));
	RightController->SetupAttachment(VRRoot);
	RightController->SetTrackingMotionSource(FXRMotionControllerBase::RightHandSourceId);
	RightController->SetRelativeLocation(FVector(0.f, 0.f, 110.f));

	RightGrabber = CreateDefaultSubobject<UGrabber>(TEXT("Right Grabber"));
	RightGrabber->SetupAttachment(RightController);

	RightHand = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Right Hand"));
	RightHand->SetupAttachment(RightController);

	//Handles UI interaction
	RWidgetInteraction = CreateDefaultSubobject<UWidgetInteractionComponent>(TEXT("Right Widget Interaction Component"));
	RWidgetInteraction->SetupAttachment(RightHand);
}

// Called when the game starts or when spawned
void AVRCharacterBase::BeginPlay()
{
	Super::BeginPlay();
	//Player VR Setup stuff
	RWidgetInteraction->AttachToComponent(RightHand,
		FAttachmentTransformRules(EAttachmentRule::SnapToTarget, true), FingerTip);
	// Set tracking origin (Oculus & Vive)
	UHeadMountedDisplayFunctionLibrary::SetTrackingOrigin(EHMDTrackingOrigin::Floor);
	//Target Point set up stuff
	UGameplayStatics::GetAllActorsOfClass(this->GetWorld(), ATargetPoint::StaticClass(), TargetPointsFound);
	if (TargetPointsFound.Num() >= 1) //need to do a null check to make sure we have an array, else we might crash
	{
		CurrentLocation = StartLocation;
		MaxLocations = TargetPointsFound.Num() - 1;
		MoveToPoint(TargetPointsFound[StartLocation]);
	}
}

// Called every frame
void AVRCharacterBase::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	//Calculations to deal with collison. This basically prevents us from walking through walls
	NewCameraOffset = Camera->GetComponentLocation() - GetActorLocation();
	NewCameraOffset.Z = 0;
	AddActorWorldOffset(NewCameraOffset);
	VRRoot->AddWorldOffset(-NewCameraOffset);
	EnableInventory = IsPalmUp(LeftHand);
}

// Called to bind functionality to input
void AVRCharacterBase::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	PlayerInputComponent->BindAction(TEXT("NextPoint"), IE_Pressed, this, &AVRCharacterBase::ToNextPoint);
	PlayerInputComponent->BindAction(TEXT("PreviousPoint"), IE_Pressed, this, &AVRCharacterBase::ToPreviousPoint);

	//	PlayerInputComponent->BindAxis(TEXT("NextForward"), this, &ASimplifiedVRChar::MoveForward); For whatever reason, this doesn't work
	PlayerInputComponent->BindAxis(TEXT("MoveRight"), this, &AVRCharacterBase::MoveRight);

}


void AVRCharacterBase::MoveForward(float Value)
{
	AddMovementInput(Camera->GetForwardVector(), Value);
}

void AVRCharacterBase::MoveRight(float Value)
{
	AddMovementInput(Camera->GetRightVector(), Value);

}

void AVRCharacterBase::GripLeft()
{
	HeldObject = Grip(LeftGrabber, LeftHand);
	ItemInHand = HeldObject;
	LTriggerPressed = true;
}

void AVRCharacterBase::ReleaseLeft()
{
	HeldObject = Release(LeftGrabber, LeftHand);
	ItemInHand = HeldObject;
	LTriggerPressed = false;
}

void AVRCharacterBase::GripRight()
{
	HeldObject = Grip(RightGrabber, RightHand);
	ItemInHand = HeldObject;
	RTriggerPressed = true;
}

void AVRCharacterBase::ReleaseRight()
{
	HeldObject = Release(RightGrabber, RightHand);
	ItemInHand = HeldObject;
	RTriggerPressed = false;
}

AActor* AVRCharacterBase::Grip(UGrabber* Grabber, USkeletalMeshComponent* Hand)
{
	auto Held = Grabber->Grab(20.0f, true, true, EGrabTypeEnum::PRECISION_GRAB, "");
	if (Held)
	{
		FAttachmentTransformRules TransformRules = FAttachmentTransformRules(
			EAttachmentRule::SnapToTarget,
			true);
		Held->AttachToComponent(Hand, TransformRules, SocketName);
		return Held;
	}
	return nullptr;
}

AActor* AVRCharacterBase::Release(UGrabber* Grabber, USkeletalMeshComponent* Hand)
{
	auto Held = Grabber->Release();
	if (Held)
	{
		FDetachmentTransformRules TransformRules = FDetachmentTransformRules(
			EDetachmentRule::KeepWorld,
			true);
		Held->DetachFromActor(TransformRules);
		return Held;
	}
	return nullptr;
}

bool AVRCharacterBase::IsPalmUp(USkeletalMeshComponent* Hand, bool Debug)
{
	if ((Hand->GetSocketRotation("").Roll <= -120.0f && Hand->GetSocketRotation("").Roll >= -180.0f) ||
		(Hand->GetSocketRotation("").Roll <= 180.0f && Hand->GetSocketRotation("").Roll >= 155.0f))
	{
		if (Debug)
		{
			UE_LOG(LogTemp, Error, TEXT("Can Inventory, %f"), Hand->GetSocketRotation("").Roll);
		}
		return true;
	}
	if (Debug)
	{
		UE_LOG(LogTemp, Warning, TEXT("Can't Inventory, %f"), Hand->GetSocketRotation("").Roll);
	}
	return false;

}


void AVRCharacterBase::ToNextPoint()
{
	//Move forward in the array of target points
	CurrentLocation += 1;
	//if at [end of the array] go to [0]
	if (CurrentLocation > MaxLocations)
	{
		CurrentLocation = 0;
	}
	//Teleport to currently selected target point
	if (TargetPointsFound[CurrentLocation] != nullptr)
	{
		MoveToPoint(TargetPointsFound[CurrentLocation]);
		UE_LOG(LogTemp, Warning, TEXT("Moving to next point"));
	}
	

}

void AVRCharacterBase::ToPreviousPoint()
{
	//Move back in the array of target points
	CurrentLocation -= 1;
	//if at [0] go to [end of the array]
	if (CurrentLocation < 0)
	{
		CurrentLocation = MaxLocations;
	}
	//Teleport to currently selected target point
	if (TargetPointsFound[CurrentLocation] != nullptr)
	{
		MoveToPoint(TargetPointsFound[CurrentLocation]);
		UE_LOG(LogTemp, Warning, TEXT("Moving to previous point"));
	}

}

void AVRCharacterBase::MoveToPoint(AActor* PointOfInterst)
{
	SetActorLocation(PointOfInterst->GetActorLocation());
}


// Check if the HMD is worn
bool AVRCharacterBase::IsHeadsetOn()
{
	if (UHeadMountedDisplayFunctionLibrary::GetHMDWornState() == EHMDWornState::Worn)
	{
		return true;
	}

	return false;
}

