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
														UPostProcessComponent* PostProcessComponent,
														UMaterial* InFogPostProcessMaterial)
{
	DepthCaptureComponent = CaptureComponent;
	FogPostProcessComponent = PostProcessComponent;
	FogPostProcessMaterial = InFogPostProcessMaterial;

	if (!DepthCaptureComponent || !FogPostProcessComponent)
	{
		UE_LOG(LogTemp, Warning, TEXT("InitializeFogComponents: One or more components are null. Disabling tick."));
		SetComponentTickEnabled(false);
	}
}

void UOBVisibilityFogComponent::UpdateTeammateData(const TArray<FTeammateVisionData>& InTeammateData)
{
	this->CurrentTeammateData = InTeammateData;
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

	// --- Tạo Data Texture và MID cho Post Process ---
	const int32 TextureWidth = MaxTeamSize * DataPointsPerPlayer;
	SourceDataTexture = UTexture2D::CreateTransient(TextureWidth, 1, PF_A32B32G32R32F);
	if (SourceDataTexture)
	{
		SourceDataTexture->UpdateResource();
	}

	if (FogPostProcessComponent)
	{
		PostProcessMID = UKismetMaterialLibrary::CreateDynamicMaterialInstance(
			GetOwner()->GetWorld(), FogPostProcessMaterial, TEXT("PostProcessMID"), EMIDCreationFlags::Transient);
		PostProcessMID->SetTextureParameterValue(FName("DepthMap"), DepthRenderTarget);
		FogPostProcessComponent->Settings.WeightedBlendables.Array[0].Weight = 1.0f;
		FogPostProcessComponent->Settings.WeightedBlendables.Array[0].Object = PostProcessMID;
	}
}

void UOBVisibilityFogComponent::TickComponent(float DeltaTime, ELevelTick TickType,
											  FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

void UOBVisibilityFogComponent::UpdateData()
{
	if (!IsValid(VisionMPC) || !IsValid(DepthCaptureComponent) || !IsValid(DepthRenderTarget))
	{
		return;
	}

	const AActor* OwnerActor = GetOwner();

	const FVector EyeLocation = DepthCaptureComponent->GetComponentLocation();
	const FVector ActorGroundLocation = EyeLocation - FVector(0, 0, 150);
	const FVector ForwardVector = OwnerActor->GetActorForwardVector();
	const FRotator EyeRotation = ForwardVector.Rotation();


	if (!SourceDataTexture || !PostProcessMID) return;

	// --- TẬP HỢP DỮ LIỆU TỪ TẤT CẢ NGUỒN ---
	TArray<FTeammateVisionData> AllSourcesData = CurrentTeammateData;

	// Thêm dữ liệu của chính mình vào danh sách
	FTeammateVisionData MyData;
	MyData.EyeLocation = EyeLocation; // Dùng socket chest
	MyData.GroundLocation = ActorGroundLocation;
	MyData.ForwardVector = ForwardVector;
	AllSourcesData.Insert(MyData, 0); // Thêm vào đầu mảng

	const int32 NumSources = FMath::Min(AllSourcesData.Num(), MaxTeamSize);

	// --- CẬP NHẬT DATA TEXTURE ---
	const int32 TextureWidth = MaxTeamSize * DataPointsPerPlayer;
	TArray<FLinearColor> TextureData;
	TextureData.SetNumZeroed(TextureWidth);

	for (int32 i = 0; i < NumSources; ++i)
	{
		// Ghi dữ liệu vào mảng
		const FTeammateVisionData& Source = AllSourcesData[i];
		// Pixel 0, 2, 4,...: EyeLocation + Ground Z
		TextureData[i * DataPointsPerPlayer + 0] = FLinearColor(Source.EyeLocation.X, Source.EyeLocation.Y,
																Source.EyeLocation.Z, Source.GroundLocation.Z);
		// Pixel 1, 3, 5,...: ForwardVector
		TextureData[i * DataPointsPerPlayer + 1] = FLinearColor(Source.ForwardVector.X, Source.ForwardVector.Y,
																Source.ForwardVector.Z, 0);
	}

	const FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, TextureWidth, 1);
	SourceDataTexture->UpdateTextureRegions(0, 1, Region, TextureWidth * sizeof(FLinearColor), sizeof(FLinearColor),
											reinterpret_cast<uint8*>(TextureData.GetData()));

	// --- CẬP NHẬT MATERIAL ---
	// Cập nhật MID của Post Process
	PostProcessMID->SetTextureParameterValue(FName("TeamDataTex"), SourceDataTexture);
	PostProcessMID->SetScalarParameterValue(FName("NumSources"), NumSources);

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