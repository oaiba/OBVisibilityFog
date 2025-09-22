// Fill out your copyright notice in the Description page of Project Settings.


#include "OBVisibilityFogComponent.h"

#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GameFramework/Character.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialParameterCollection.h"

UOBVisibilityFogComponent::UOBVisibilityFogComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UOBVisibilityFogComponent::InitializeFogComponents(USceneCaptureComponent2D* CaptureComponent,
                                                        UPostProcessComponent* PostProcessComponent)
{
	DepthCaptureComponent = CaptureComponent;
	FogPostProcessComponent = PostProcessComponent;

	if (!DepthCaptureComponent || !FogPostProcessComponent)
	{
		UE_LOG(LogTemp, Warning, TEXT("InitializeFogComponents: One or more components are null. Disabling tick."));
		SetComponentTickEnabled(false);
	}
}

void UOBVisibilityFogComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!IsValid(DepthCaptureComponent) || !IsValid(DepthRenderTarget) || !IsValid(VisionMPC))
	{
		UE_LOG(LogTemp, Warning,
		       TEXT(
			       "UOBVisibilityFogComponent on Actor '%s' is missing required dependencies (DepthCapture, RenderTarget, or MPC). Disabling tick."
		       ), *GetOwner()->GetName());
		SetComponentTickEnabled(false);
		return;
	}

	DepthCaptureComponent->TextureTarget = DepthRenderTarget;
	DepthCaptureComponent->CaptureSource = SCS_SceneDepth;
	DepthCaptureComponent->ProjectionType = ECameraProjectionMode::Perspective;
	DepthCaptureComponent->bCaptureEveryFrame = false;
	DepthCaptureComponent->bCaptureOnMovement = false;

	if (const APawn* OwnerPawn = Cast<APawn>(GetOwner()); OwnerPawn && !OwnerPawn->IsLocallyControlled())
	{
		SetComponentTickEnabled(false);
		DepthCaptureComponent->Deactivate();
	}
}

void UOBVisibilityFogComponent::TickComponent(float DeltaTime, ELevelTick TickType,
                                              FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!IsValid(VisionMPC) || !IsValid(DepthCaptureComponent) || !IsValid(DepthRenderTarget))
	{
		return;
	}

	const AActor* OwnerActor = GetOwner();

	const FVector EyeLocation = DepthCaptureComponent->GetComponentLocation();
	const FVector ActorGroundLocation = EyeLocation - FVector(0, 0, 150);
	const FVector ForwardVector = OwnerActor->GetActorForwardVector();
	const FRotator EyeRotation = ForwardVector.Rotation();

	DepthCaptureComponent->SetWorldLocationAndRotation(EyeLocation, EyeRotation);
	DepthCaptureComponent->FOVAngle = VisionAngleDegrees;
	DepthCaptureComponent->CaptureScene();

	const FMatrix ViewMatrix = FLookFromMatrix(EyeLocation, ForwardVector, FVector::UpVector);
	const float AspectRatio = static_cast<float>(DepthRenderTarget->SizeX) / static_cast<float>(DepthRenderTarget->
		SizeY);
	const float HorizontalFOVRadians = FMath::DegreesToRadians(VisionAngleDegrees);
	const float VerticalFOVRadians = 2.0f * FMath::Atan(FMath::Tan(HorizontalFOVRadians / 2.0f) / AspectRatio);
	const FMatrix ProjectionMatrix = FReversedZPerspectiveMatrix(VerticalFOVRadians / 2.0f, AspectRatio,
	                                                             GNearClippingPlane, GNearClippingPlane);
	const FMatrix ViewProjectionMatrix = ViewMatrix * ProjectionMatrix;

	UKismetMaterialLibrary::SetVectorParameterValue(GetWorld(), VisionMPC, FName("MatrixRow0"),
	                                                FLinearColor(ViewProjectionMatrix.M[0][0],
	                                                             ViewProjectionMatrix.M[0][1],
	                                                             ViewProjectionMatrix.M[0][2],
	                                                             ViewProjectionMatrix.M[0][3]));
	UKismetMaterialLibrary::SetVectorParameterValue(GetWorld(), VisionMPC, FName("MatrixRow1"),
	                                                FLinearColor(ViewProjectionMatrix.M[1][0],
	                                                             ViewProjectionMatrix.M[1][1],
	                                                             ViewProjectionMatrix.M[1][2],
	                                                             ViewProjectionMatrix.M[1][3]));
	UKismetMaterialLibrary::SetVectorParameterValue(GetWorld(), VisionMPC, FName("MatrixRow2"),
	                                                FLinearColor(ViewProjectionMatrix.M[2][0],
	                                                             ViewProjectionMatrix.M[2][1],
	                                                             ViewProjectionMatrix.M[2][2],
	                                                             ViewProjectionMatrix.M[2][3]));
	UKismetMaterialLibrary::SetVectorParameterValue(GetWorld(), VisionMPC, FName("MatrixRow3"),
	                                                FLinearColor(ViewProjectionMatrix.M[3][0],
	                                                             ViewProjectionMatrix.M[3][1],
	                                                             ViewProjectionMatrix.M[3][2],
	                                                             ViewProjectionMatrix.M[3][3]));

	const float VisionConeCos = FMath::Cos(FMath::DegreesToRadians(VisionAngleDegrees / 2.0f));
	UKismetMaterialLibrary::SetScalarParameterValue(GetWorld(), VisionMPC, FName("VisionConeCosine"), VisionConeCos);
	UKismetMaterialLibrary::SetScalarParameterValue(GetWorld(), VisionMPC, FName("VisionMaxDistance"), VisionDistance);
	UKismetMaterialLibrary::SetScalarParameterValue(GetWorld(), VisionMPC, FName("ProximityRadius"), ProximityRadius);
	UKismetMaterialLibrary::SetVectorParameterValue(GetWorld(), VisionMPC, FName("PlayerPosition"),
	                                                FLinearColor(EyeLocation));
	UKismetMaterialLibrary::SetVectorParameterValue(GetWorld(), VisionMPC, FName("PlayerForwardVector"),
	                                                FLinearColor(ForwardVector));
	UKismetMaterialLibrary::SetVectorParameterValue(GetWorld(), VisionMPC, FName("PlayerGroundPosition"),
	                                                FLinearColor(ActorGroundLocation));
	UKismetMaterialLibrary::SetScalarParameterValue(GetWorld(), VisionMPC, FName("ProximityMaxHeight"),
	                                                ProximityMaxHeight);

	if (bIsShowDebugMessage && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(__LINE__, 0.0f, FColor::White,
		                                 FString::Printf(TEXT("VisionConeCos: %f"), VisionConeCos));
		GEngine->AddOnScreenDebugMessage(__LINE__, 0.0f, FColor::White,
		                                 FString::Printf(TEXT("VisionDistance: %f"), VisionDistance));
		GEngine->AddOnScreenDebugMessage(__LINE__, 0.0f, FColor::White,
		                                 FString::Printf(TEXT("ProximityRadius: %f"), ProximityRadius));
		GEngine->AddOnScreenDebugMessage(__LINE__, 0.0f, FColor::White,
		                                 FString::Printf(TEXT("EyeLocation: %s"), *EyeLocation.ToString()));
		GEngine->AddOnScreenDebugMessage(__LINE__, 0.0f, FColor::White,
		                                 FString::Printf(TEXT("ForwardVector: %s"), *ForwardVector.ToString()));
		GEngine->AddOnScreenDebugMessage(__LINE__, 0.0f, FColor::White,
		                                 FString::Printf(
			                                 TEXT("ActorGroundLocation: %s"), *ActorGroundLocation.ToString()));
		GEngine->AddOnScreenDebugMessage(__LINE__, 0.0f, FColor::White,
		                                 FString::Printf(TEXT("ProximityMaxHeight: %f"), ProximityMaxHeight));
	}

#if ENABLE_DRAW_DEBUG
	if (bIsShowDebug)
	{
		DrawDebugCone(GetWorld(), EyeLocation, ForwardVector, VisionDistance,
		              FMath::DegreesToRadians(VisionAngleDegrees / 2.0f),
		              FMath::DegreesToRadians(VisionAngleDegrees / 2.0f), 32, FColor::Green, false, 0.0f, 0, 1.0f);
		DrawDebugCircle(GetWorld(), ActorGroundLocation, ProximityRadius, 32, FColor::Blue, false, 0.0f, 0, 1.0f,
		                FVector(0, 0, 1));
		DrawDebugSphere(GetWorld(), ActorGroundLocation, ProximityRadius, 32, FColor::Purple, false, 0.0f, 0, 1.0f);
		DrawDebugLine(GetWorld(), EyeLocation, ActorGroundLocation, FColor::Yellow, false, 0.0f, 0, 1.0f);
	}
#endif
}
