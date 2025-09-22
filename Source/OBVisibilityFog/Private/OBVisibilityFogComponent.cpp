// Fill out your copyright notice in the Description page of Project Settings.


#include "OBVisibilityFogComponent.h"

#include "Components/SceneCaptureComponent2D.h"

UOBVisibilityFogComponent::UOBVisibilityFogComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UOBVisibilityFogComponent::InitializeFogComponents(USceneCaptureComponent2D* CaptureComponent,
														UPostProcessComponent* PostProcessComponent)
{
	TargetSceneCapture = CaptureComponent;
	TargetPostProcess = PostProcessComponent;

	if (!TargetSceneCapture || !TargetPostProcess)
	{
		UE_LOG(LogTemp, Warning, TEXT("InitializeFogComponents: One or more components are null. Disabling tick."));
		SetComponentTickEnabled(false);
	}
}

void UOBVisibilityFogComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!IsValid(TargetSceneCapture) || !IsValid(TargetPostProcess))
	{
		UE_LOG(LogTemp, Warning,
			   TEXT(
				   "UOBVisibilityFogComponent on Actor '%s' is missing TargetSceneCapture or TargetPostProcess. Component will not tick."
			   ), *GetOwner()->GetName());
		SetComponentTickEnabled(false);
		return;
	}

	if (const APawn* OwnerPawn = Cast<APawn>(GetOwner()); OwnerPawn && !OwnerPawn->IsLocallyControlled())
	{
		UE_LOG(LogTemp, Log, TEXT("UOBVisibilityFogComponent: Disabling for non-local pawn."));
		SetComponentTickEnabled(false);
		TargetSceneCapture->Deactivate();
		TargetPostProcess->bEnabled = false;
	}
	else
	{
		TargetSceneCapture->bCaptureEveryFrame = false;
		TargetSceneCapture->bCaptureOnMovement = false;
		TargetPostProcess->bEnabled = true;
		TargetSceneCapture->Activate();
	}
}

void UOBVisibilityFogComponent::TickComponent(float DeltaTime, ELevelTick TickType,
											  FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (TargetSceneCapture && TargetSceneCapture->IsActive())
	{
		TargetSceneCapture->CaptureScene();
	}
}
