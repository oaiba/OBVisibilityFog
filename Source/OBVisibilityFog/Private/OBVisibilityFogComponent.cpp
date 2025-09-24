// Fill out your copyright notice in the Description page of Project Settings.

#include "OBVisibilityFogComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Rendering/Texture2DResource.h"
#include "Engine/World.h"

UOBVisibilityFogComponent::UOBVisibilityFogComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UOBVisibilityFogComponent::InitializeFogComponents(UPostProcessComponent* InPostProcessComponent)
{
	FogPostProcessComponent = InPostProcessComponent;

	if (!FogPostProcessComponent)
	{
		UE_LOG(LogTemp, Warning, TEXT("InitializeFogComponents: One or more components are null. Disabling tick."));
		SetComponentTickEnabled(false);
	}
}

void UOBVisibilityFogComponent::BeginPlay()
{
	Super::BeginPlay();

	// --- BƯỚC 1: KIỂM TRA DEPENDENCIES CƠ BẢN ---
	if (!IsValid(FogPostProcessComponent) || !IsValid(FogPostProcessMaterial))
	{
		UE_LOG(LogTemp, Warning,
			   TEXT("UOBVisibilityFogComponent trên '%s' thiếu FogPostProcessComponent hoặc FogPostProcessMaterial."),
			   *GetOwner()->GetName());
		return;
	}

	// --- BƯỚC 2: TẠO CÁC TÀI NGUYÊN SỞ HỮU BỞI COMPONENT NÀY ---
	// Tạo Data Textures
	const int32 SourceDataTextureWidth = MaxTeamSize * DataPointsPerPlayer;
	SourceDataTexture = UTexture2D::CreateTransient(SourceDataTextureWidth, 1, PF_A32B32G32R32F);
	if (SourceDataTexture) SourceDataTexture->UpdateResource();

	const int32 MatrixDataTextureWidth = MaxTeamSize * MatrixDataPointsPerPlayer;
	MatrixDataTexture = UTexture2D::CreateTransient(MatrixDataTextureWidth, 1, PF_A32B32G32R32F);
	if (MatrixDataTexture) MatrixDataTexture->UpdateResource();

	// Tạo Pool Render Target
	RenderTargetPool.Reserve(MaxTeamSize);
	for (int32 i = 0; i < MaxTeamSize; ++i)
	{
		UTextureRenderTarget2D* NewRT = NewObject<UTextureRenderTarget2D>(this);
		NewRT->InitCustomFormat(RenderTargetResolution, RenderTargetResolution, PF_R32_FLOAT, true);
		NewRT->UpdateResource();
		RenderTargetPool.Add(NewRT);
	}

	// --- BƯỚC 3: CẤU HÌNH POST PROCESS ---
	PostProcessMID = UMaterialInstanceDynamic::Create(FogPostProcessMaterial, this);
	if (!PostProcessMID)
	{
		UE_LOG(LogTemp, Error, TEXT("UOBVisibilityFogComponent: Không thể tạo MID."));
		return;
	}

	PostProcessMID->SetTextureParameterValue(FName("SourceDataTex"), SourceDataTexture);
	PostProcessMID->SetTextureParameterValue(FName("MatrixDataTex"), MatrixDataTexture);

	for (int32 i = 0; i < MaxTeamSize; ++i)
	{
		FName ParamName = *FString::Printf(TEXT("DepthMap%d"), i);
		PostProcessMID->SetTextureParameterValue(ParamName, RenderTargetPool[i]);
	}

	FogPostProcessComponent->Settings.AddBlendable(PostProcessMID, 1.0f);
}

void UOBVisibilityFogComponent::Initialize(USceneCaptureComponent2D* InLocalCaptureComponent,
										   const TArray<USceneCaptureComponent2D*>& InTeammateCaptureComponents)
{
	if (!IsValid(InLocalCaptureComponent))
	{
		UE_LOG(LogTemp, Error, TEXT("UOBVisibilityFogComponent::Initialize - InLocalCaptureComponent không hợp lệ!"));
		return;
	}

	CaptureComponentPool.Empty();
	CaptureComponentPool.Add(InLocalCaptureComponent);
	CaptureComponentPool.Append(InTeammateCaptureComponents);

	// Giới hạn số lượng component trong pool để tránh lỗi
	if (CaptureComponentPool.Num() > MaxTeamSize)
	{
		CaptureComponentPool.SetNum(MaxTeamSize);
		UE_LOG(LogTemp, Warning, TEXT("Số lượng Scene Capture (%d) vượt quá MaxTeamSize (%d). Đã cắt bớt."),
			   CaptureComponentPool.Num(), MaxTeamSize);
	}

	// Cấu hình từng Scene Capture Component được truyền vào
	for (int32 i = 0; i < CaptureComponentPool.Num(); ++i)
	{
		if (USceneCaptureComponent2D* Capture = CaptureComponentPool[i])
		{
			Capture->CaptureSource = SCS_SceneDepth;
			Capture->ProjectionType = ECameraProjectionMode::Perspective;
			Capture->TextureTarget = RenderTargetPool[i]; // Gán RT tương ứng
			Capture->bCaptureEveryFrame = false;
			Capture->bCaptureOnMovement = false;
		}
	}

	bIsReadyToUpdate = true;
	SetComponentTickEnabled(true);
	UE_LOG(LogTemp, Log, TEXT("UOBVisibilityFogComponent Initialized với %d Scene Capture Components."),
		   CaptureComponentPool.Num());
}


void UOBVisibilityFogComponent::UpdateData()
{
	if (!bIsReadyToUpdate || !HasBegunPlay() || CaptureComponentPool.Num() == 0) return;

	const int32 NumSources = CaptureComponentPool.Num();

	// --- BƯỚC 1: CẬP NHẬT TẤT CẢ DEPTH MAP VÀ MA TRẬN ---
	TArray<FLinearColor>* SourceTextureDataCopy = new TArray<FLinearColor>();
	SourceTextureDataCopy->SetNumZeroed(MaxTeamSize * DataPointsPerPlayer);

	TArray<FLinearColor>* MatrixTextureDataCopy = new TArray<FLinearColor>();
	MatrixTextureDataCopy->SetNumZeroed(MaxTeamSize * MatrixDataPointsPerPlayer);

	TArray<FTeammateVisionData> AllDebugData; // Chỉ dùng cho việc vẽ debug
	if (bIsShowDebug) AllDebugData.Reserve(NumSources);

	for (int32 i = 0; i < NumSources; ++i)
	{
		USceneCaptureComponent2D* Capture = CaptureComponentPool[i];
		if (!IsValid(Capture)) continue;

		// 1.1. Lấy dữ liệu trực tiếp từ component
		FTeammateVisionData CurrentSourceData;
		CurrentSourceData.EyeLocation = Capture->GetComponentLocation();
		CurrentSourceData.ForwardVector = Capture->GetForwardVector();

		// 1.2. Tìm vị trí mặt đất bằng Line Trace
		FHitResult HitResult;
		FVector StartTrace = CurrentSourceData.EyeLocation;
		FVector EndTrace = StartTrace - FVector(0, 0, VisionDistance * 2.0f);
		if (GetWorld()->LineTraceSingleByChannel(HitResult, StartTrace, EndTrace, GroundTraceChannel))
		{
			CurrentSourceData.GroundLocation = HitResult.Location;
		}
		else
		{
			CurrentSourceData.GroundLocation = CurrentSourceData.EyeLocation - FVector(0, 0, 88.0f);
		}

		if (bIsShowDebug) AllDebugData.Add(CurrentSourceData);

		// 1.3. Cập nhật và thực hiện Scene Capture
		Capture->FOVAngle = VisionAngleDegrees;
		Capture->CaptureScene();

		// 1.4. Tính toán ma trận View-Projection
		const FMatrix ViewMatrix = FLookFromMatrix(CurrentSourceData.EyeLocation, CurrentSourceData.ForwardVector,
												   FVector::UpVector);
		const float AspectRatio = 1.0f;
		const float HorizontalFOVRadians = FMath::DegreesToRadians(VisionAngleDegrees);
		const float VerticalFOVRadians = 2.0f * FMath::Atan(FMath::Tan(HorizontalFOVRadians * 0.5f) / AspectRatio);
		const FMatrix ProjectionMatrix = FReversedZPerspectiveMatrix(VerticalFOVRadians * 0.5f, AspectRatio, 1.0f,
																	 VisionDistance);
		const FMatrix ViewProjectionMatrix = ViewMatrix * ProjectionMatrix;

		// 1.5. Đóng gói dữ liệu vào các mảng TextureDataCopy
		(*SourceTextureDataCopy)[i * DataPointsPerPlayer + 0] = FLinearColor(
			CurrentSourceData.EyeLocation.X, CurrentSourceData.EyeLocation.Y, CurrentSourceData.EyeLocation.Z,
			CurrentSourceData.GroundLocation.Z);
		(*SourceTextureDataCopy)[i * DataPointsPerPlayer + 1] = FLinearColor(
			CurrentSourceData.ForwardVector.X, CurrentSourceData.ForwardVector.Y, CurrentSourceData.ForwardVector.Z,
			0.0f);
		for (int row = 0; row < 4; ++row)
		{
			(*MatrixTextureDataCopy)[i * MatrixDataPointsPerPlayer + row] = FLinearColor(
				ViewProjectionMatrix.M[row][0], ViewProjectionMatrix.M[row][1], ViewProjectionMatrix.M[row][2],
				ViewProjectionMatrix.M[row][3]);
		}
	}

	// --- BƯỚC 2: GỬI DỮ LIỆU LÊN GPU (THREAD-SAFE) ---
	// (Không thay đổi so với phiên bản trước)
	if (SourceDataTexture && SourceDataTexture->GetResource())
	{
		ENQUEUE_RENDER_COMMAND(UpdateSourceDataTexture)(
			[Res = (FTexture2DResource*)SourceDataTexture->GetResource(), Data = SourceTextureDataCopy, Width =
				MaxTeamSize * DataPointsPerPlayer](FRHICommandListImmediate& RHICmdList)
			{
				const FUpdateTextureRegion2D Region(0, 0, 0, 0, Width, 1);
				RHICmdList.UpdateTexture2D(Res->GetTexture2DRHI(), 0, Region, Width * sizeof(FLinearColor),
										   reinterpret_cast<const uint8*>(Data->GetData()));
				delete Data;
			}
		);
	}
	else { delete SourceTextureDataCopy; }

	if (MatrixDataTexture && MatrixDataTexture->GetResource())
	{
		ENQUEUE_RENDER_COMMAND(UpdateMatrixDataTexture)(
			[Res = (FTexture2DResource*)MatrixDataTexture->GetResource(), Data = MatrixTextureDataCopy, Width =
				MaxTeamSize * MatrixDataPointsPerPlayer](FRHICommandListImmediate& RHICmdList)
			{
				const FUpdateTextureRegion2D Region(0, 0, 0, 0, Width, 1);
				RHICmdList.UpdateTexture2D(Res->GetTexture2DRHI(), 0, Region, Width * sizeof(FLinearColor),
										   reinterpret_cast<const uint8*>(Data->GetData()));
				delete Data;
			}
		);
	}
	else { delete MatrixTextureDataCopy; }


	// --- BƯỚC 3: CẬP NHẬT CÁC THAM SỐ CHUNG ---
	PostProcessMID->SetScalarParameterValue(FName("NumSources"), NumSources);
	const float VisionConeCos = FMath::Cos(FMath::DegreesToRadians(VisionAngleDegrees * 0.5f));
	PostProcessMID->SetScalarParameterValue(FName("VisionConeCosine"), VisionConeCos);
	PostProcessMID->SetScalarParameterValue(FName("VisionMaxDistance"), VisionDistance);
	PostProcessMID->SetScalarParameterValue(FName("ProximityRadius"), ProximityRadius);
	PostProcessMID->SetScalarParameterValue(FName("ProximityMaxHeight"), ProximityMaxHeight);

	// --- BƯỚC 4: VẼ DEBUG ---
#if ENABLE_DRAW_DEBUG
	if (bIsShowDebug)
	{
		for (int i = 0; i < AllDebugData.Num(); ++i)
		{
			const FTeammateVisionData& Source = AllDebugData[i];
			FColor DebugColor = (i == 0) ? FColor::Green : FColor::Cyan;
			float DebugThickness = (i == 0) ? 1.0f : 0.5f;

			DrawDebugCone(GetWorld(), Source.EyeLocation, Source.ForwardVector, VisionDistance,
						  FMath::DegreesToRadians(VisionAngleDegrees * 0.5f),
						  FMath::DegreesToRadians(VisionAngleDegrees * 0.5f), 32, DebugColor, false, 0.0f, 0,
						  DebugThickness);
			DrawDebugCylinder(GetWorld(), Source.GroundLocation - FVector(0, 0, ProximityMaxHeight),
							  Source.GroundLocation + FVector(0, 0, ProximityMaxHeight), ProximityRadius, 32,
							  DebugColor, false, 0.0f, 0, DebugThickness);
		}
	}
#endif
}
