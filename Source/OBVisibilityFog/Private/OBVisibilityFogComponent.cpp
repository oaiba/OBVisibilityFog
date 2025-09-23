// Fill out your copyright notice in the Description page of Project Settings.


#include "OBVisibilityFogComponent.h"

#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialParameterCollection.h"
#include "Rendering/Texture2DResource.h"

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

void UOBVisibilityFogComponent::BeginPlay()
{
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
	DepthCaptureComponent->bRenderInMainRenderer = true;

	if (const APawn* OwnerPawn = Cast<APawn>(GetOwner()); OwnerPawn && !OwnerPawn->IsLocallyControlled())
	{
		SetComponentTickEnabled(false);
		DepthCaptureComponent->Deactivate();
	}

	const int32 TextureWidth = MaxTeamSize * DataPointsPerPlayer;
	SourceDataTexture = UTexture2D::CreateTransient(TextureWidth, 1, PF_A32B32G32R32F);
	if (SourceDataTexture)
	{
		SourceDataTexture->UpdateResource();
	}

	if (FogPostProcessComponent)
	{
		if (!IsValid(FogPostProcessMaterial))
		{
			UE_LOG(LogTemp, Error,
			       TEXT(
				       "UOBVisibilityFogComponent: FogPostProcessMaterial is NOT ASSIGNED in the Blueprint Editor! Cannot create MID."
			       ));
			return;
		}

		// --- CẤU HÌNH POST PROCESS ---
		if (FogPostProcessComponent)
		{
			// --- TỐI ƯU HÓA: Tắt các hiệu ứng không cần thiết ---
			// Lấy một tham chiếu đến Settings để code gọn hơn
			FPostProcessSettings& Settings = FogPostProcessComponent->Settings;

			// Tắt Bloom
			Settings.bOverride_BloomIntensity = true;
			Settings.BloomIntensity = 0.0f;

			// Tắt Auto Exposure (Eye Adaptation) - Giữ phơi sáng cố định
			Settings.bOverride_AutoExposureMinBrightness = true;
			Settings.bOverride_AutoExposureMaxBrightness = true;
			Settings.AutoExposureMinBrightness = 1.0f;
			Settings.AutoExposureMaxBrightness = 1.0f;

			// Tắt Motion Blur
			Settings.bOverride_MotionBlurAmount = true;
			Settings.MotionBlurAmount = 0.0f;

			// Tắt Screen Space Ambient Occlusion (SSAO)
			Settings.bOverride_AmbientOcclusionIntensity = true;
			Settings.AmbientOcclusionIntensity = 0.0f;

			// Tắt Vignette
			Settings.bOverride_VignetteIntensity = true;
			Settings.VignetteIntensity = 0.0f;

			PostProcessMID = UMaterialInstanceDynamic::Create(FogPostProcessMaterial, this);

			if (PostProcessMID)
			{
				PostProcessMID->SetTextureParameterValue(FName("DepthMap"), DepthRenderTarget);

				FogPostProcessComponent->Settings.AddBlendable(PostProcessMID, 1.0f);
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("[%s:%hs] Failed to create MID for PostProcessComponent."),
				       *GetNameSafe(this),
				       __FUNCTION__);
			}
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[%s:%hs] Failed to create PostProcessComponent."), *GetNameSafe(this),
			       __FUNCTION__);
		}

		Super::BeginPlay();
	}
}

void UOBVisibilityFogComponent::TickComponent(float DeltaTime, ELevelTick TickType,
                                              FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

void UOBVisibilityFogComponent::UpdateData(const TArray<FTeammateVisionData>& InTeammateData)
{
	if (!HasBegunPlay()) return;

	if (!bIsReadyToTick)
	{
		if (SourceDataTexture && SourceDataTexture->GetResource())
		{
			bIsReadyToTick = true;
		}

		return;
	}

	// --- BƯỚC 1: KIỂM TRA CÁC ĐIỀU KIỆN CẦN THIẾT ---
	if (!VisionMPC || !DepthCaptureComponent || !DepthRenderTarget || !PostProcessMID || !SourceDataTexture)
	{
		return;
	}

	const AActor* OwnerActor = GetOwner();
	const FVector EyeLocation = DepthCaptureComponent->GetComponentLocation();
	const FVector ActorGroundLocation = EyeLocation - FVector(0, 0, 150);
	const FVector ForwardVector = OwnerActor->GetActorForwardVector();

	// Giả sử dữ liệu đồng đội được nạp vào biến thành viên CurrentTeammateData từ trước
	// const TArray<FTeammateVisionData>& InTeammateData = this->GetTeamData();

	// --- BƯỚC 2: TẬP HỢP DỮ LIỆU CỦA TẤT CẢ NGUỒN (BẢN THÂN + ĐỒNG ĐỘI) ---
	TArray<FTeammateVisionData> AllSourcesData;
	AllSourcesData.Reserve(InTeammateData.Num() + 1); // Tối ưu hóa bộ nhớ

	// Thêm dữ liệu của chính mình vào danh sách trước tiên
	FTeammateVisionData MyData;
	MyData.EyeLocation = EyeLocation;
	MyData.GroundLocation = ActorGroundLocation; // Lấy vị trí chân một cách an toàn
	MyData.ForwardVector = ForwardVector;
	AllSourcesData.Add(MyData);

	// Thêm dữ liệu của tất cả đồng đội (nếu có)
	AllSourcesData.Append(InTeammateData);

	const int32 NumSources = FMath::Min(AllSourcesData.Num(), MaxTeamSize);

	// --- BƯỚC 3: CẬP NHẬT DATA TEXTURE (AN TOÀN VỚI RENDER THREAD) ---
	const int32 TextureWidth = MaxTeamSize * DataPointsPerPlayer;
	TArray<FLinearColor>* TextureDataCopy = new TArray<FLinearColor>(); // Tạo bản sao để gửi đi
	TextureDataCopy->SetNumZeroed(TextureWidth);

	for (int32 i = 0; i < NumSources; ++i)
	{
		const FTeammateVisionData& Source = AllSourcesData[i];
		(*TextureDataCopy)[i * DataPointsPerPlayer + 0] = FLinearColor(Source.EyeLocation.X, Source.EyeLocation.Y,
		                                                               Source.EyeLocation.Z, Source.GroundLocation.Z);
		(*TextureDataCopy)[i * DataPointsPerPlayer + 1] = FLinearColor(Source.ForwardVector.X, Source.ForwardVector.Y,
		                                                               Source.ForwardVector.Z, 0);
	}

	if (FTexture2DResource* TextureResource = static_cast<FTexture2DResource*>(SourceDataTexture->GetResource()))
	{
		ENQUEUE_RENDER_COMMAND(UpdateTeamDataTexture)(
			[TextureResource, TextureDataCopy, TextureWidth](FRHICommandListImmediate& RHICmdList)
			{
				const int32 DataSize = TextureWidth * sizeof(FLinearColor);
				const FUpdateTextureRegion2D Region(0, 0, 0, 0, TextureWidth, 1); // Tạo trên stack, không dùng new
				RHICmdList.UpdateTexture2D(TextureResource->GetTexture2DRHI(), 0, Region, DataSize,
				                           reinterpret_cast<const uint8*>(TextureDataCopy->GetData()));
				delete TextureDataCopy;
			}
		);
	}
	else
	{
		delete TextureDataCopy; // Xóa nếu không thể gửi lệnh
	}

	// --- BƯỚC 4: CẬP NHẬT CÁC MATERIAL PARAMETERS ---
	// Cập nhật MID của Post Process
	PostProcessMID->SetTextureParameterValue(FName("TeamDataTex"), SourceDataTexture);
	PostProcessMID->SetScalarParameterValue(FName("NumSources"), NumSources);

	// Cập nhật Occlusion cho người chơi local (dùng MPC)
	DepthCaptureComponent->SetWorldLocationAndRotation(MyData.EyeLocation, MyData.ForwardVector.Rotation());
	DepthCaptureComponent->FOVAngle = VisionAngleDegrees;
	DepthCaptureComponent->CaptureScene();

	const FMatrix ViewMatrix = FLookFromMatrix(MyData.EyeLocation, MyData.ForwardVector, FVector::UpVector);
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

	// --- BƯỚC 5: VẼ DEBUG ---
#if ENABLE_DRAW_DEBUG
	if (bIsShowDebug)
	{
		// Vẽ debug cho chính mình
		DrawDebugCone(GetWorld(), MyData.EyeLocation, MyData.ForwardVector, VisionDistance,
		              FMath::DegreesToRadians(VisionAngleDegrees / 2.0f),
		              FMath::DegreesToRadians(VisionAngleDegrees / 2.0f), 32, FColor::Green, false, 0.0f, 0, 1.0f);
		DrawDebugCircle(GetWorld(), MyData.GroundLocation, ProximityRadius, 32, FColor::Blue, false, 0.0f, 0, 1.0f,
		                FVector(0, 0, 1));

		// Vẽ debug cho đồng đội
		for (const FTeammateVisionData& Teammate : InTeammateData)
		{
			DrawDebugCone(GetWorld(), Teammate.EyeLocation, Teammate.ForwardVector, VisionDistance,
			              FMath::DegreesToRadians(VisionAngleDegrees / 2.0f),
			              FMath::DegreesToRadians(VisionAngleDegrees / 2.0f), 32, FColor::Cyan, false, 0.0f, 0, 0.5f);
			DrawDebugCircle(GetWorld(), Teammate.GroundLocation, ProximityRadius, 32, FColor::Cyan, false, 0.0f, 0,
			                0.5f, FVector(0, 0, 1));
		}
	}
#endif
}
