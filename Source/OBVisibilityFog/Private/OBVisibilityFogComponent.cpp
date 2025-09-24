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
	// Mặc định tắt Tick, sẽ được bật lại trong BeginPlay nếu mọi thứ hợp lệ
	SetComponentTickEnabled(false);
}

void UOBVisibilityFogComponent::InitializeFogComponents(USceneCaptureComponent2D* CaptureComponent,
														UPostProcessComponent* PostProcessComponent,
														UMaterial* InFogPostProcessMaterial)
{
	DepthCaptureComponent = CaptureComponent;
	FogPostProcessComponent = PostProcessComponent;
	FogPostProcessMaterial = InFogPostProcessMaterial;
}

void UOBVisibilityFogComponent::BeginPlay()
{
	Super::BeginPlay();

	// --- BƯỚC 1: KIỂM TRA CÁC ĐỐI TƯỢNG PHỤ THUỘC ---
	if (!IsValid(DepthCaptureComponent) || !IsValid(DepthRenderTarget) || !IsValid(VisionMPC) || !
		IsValid(FogPostProcessComponent) || !IsValid(FogPostProcessMaterial))
	{
		UE_LOG(LogTemp, Warning,
			   TEXT(
				   "UOBVisibilityFogComponent trên Actor '%s' thiếu các dependency cần thiết. Component sẽ bị vô hiệu hóa."
			   ), *GetOwner()->GetName());
		return; // Không tiếp tục nếu thiếu
	}

	// --- BƯỚC 2: CẤU HÌNH SCENECAPTURE COMPONENT ---
	DepthCaptureComponent->TextureTarget = DepthRenderTarget;
	DepthCaptureComponent->CaptureSource = SCS_SceneDepth;
	DepthCaptureComponent->ProjectionType = ECameraProjectionMode::Perspective;
	DepthCaptureComponent->bCaptureEveryFrame = false; // Tắt tự động capture để tiết kiệm hiệu năng
	DepthCaptureComponent->bCaptureOnMovement = false;
	DepthCaptureComponent->bRenderInMainRenderer = true; // ???

	// --- BƯỚC 3: TẠO DATA TEXTURE ĐỂ GỬI VÀO SHADER ---
	const int32 TextureWidth = MaxTeamSize * DataPointsPerPlayer;
	SourceDataTexture = UTexture2D::CreateTransient(TextureWidth, 1, PF_A32B32G32R32F);
	if (SourceDataTexture)
	{
		SourceDataTexture->UpdateResource();
	}

	// --- BƯỚC 4: CẤU HÌNH POST PROCESS ---
	// Tạo Material Instance Dynamic (MID) để có thể thay đổi tham số lúc runtime
	PostProcessMID = UMaterialInstanceDynamic::Create(FogPostProcessMaterial, this);
	if (!PostProcessMID)
	{
		UE_LOG(LogTemp, Error, TEXT("UOBVisibilityFogComponent: Không thể tạo MID từ FogPostProcessMaterial."));
		return;
	}

	// Gán các texture và tham số cố định vào MID
	PostProcessMID->SetTextureParameterValue(FName("DepthMap"), DepthRenderTarget);
	PostProcessMID->SetTextureParameterValue(FName("TeamDataTex"), SourceDataTexture);

	// Thêm MID vào PostProcessComponent để nó được áp dụng
	FogPostProcessComponent->Settings.AddBlendable(PostProcessMID, 1.0f);

	// Tối ưu hóa các hiệu ứng không cần thiết khác trong PostProcess
	FPostProcessSettings& Settings = FogPostProcessComponent->Settings;
	Settings.bOverride_BloomIntensity = true;
	Settings.BloomIntensity = 0.0f;
	Settings.bOverride_AutoExposureMinBrightness = true;
	Settings.bOverride_AutoExposureMaxBrightness = true;
	Settings.AutoExposureMinBrightness = 1.0f;
	Settings.AutoExposureMaxBrightness = 1.0f;
	Settings.bOverride_MotionBlurAmount = true;
	Settings.MotionBlurAmount = 0.0f;
	Settings.bOverride_AmbientOcclusionIntensity = true;
	Settings.AmbientOcclusionIntensity = 0.0f;
	Settings.bOverride_VignetteIntensity = true;
	Settings.VignetteIntensity = 0.0f;

	// Mọi thứ đã sẵn sàng, bật Tick Component
	bIsReadyToUpdate = true;
	SetComponentTickEnabled(true);
}

void UOBVisibilityFogComponent::TickComponent(float DeltaTime, ELevelTick TickType,
											  FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	// Logic chính được gọi từ bên ngoài thông qua hàm UpdateData
}

void UOBVisibilityFogComponent::UpdateData(const TArray<FTeammateVisionData>& InTeammateData)
{
	// Chỉ thực thi nếu component đã được khởi tạo thành công và đang trong game
	if (!bIsReadyToUpdate || !HasBegunPlay())
	{
		return;
	}

	// --- BƯỚC 1: KIỂM TRA CÁC ĐỐI TƯỢNG CẦN THIẾT TRƯỚC KHI UPDATE ---
	// Dù đã kiểm tra ở BeginPlay, kiểm tra lại để đảm bảo an toàn
	if (!VisionMPC || !DepthCaptureComponent || !DepthRenderTarget || !PostProcessMID || !SourceDataTexture)
	{
		return;
	}

	// --- BƯỚC 2: TẬP HỢP DỮ LIỆU TẦM NHÌN (BẢN THÂN + ĐỒNG ĐỘI) ---
	const AActor* OwnerActor = GetOwner();
	if (!IsValid(OwnerActor)) return;

	TArray<FTeammateVisionData> AllSourcesData;
	AllSourcesData.Reserve(InTeammateData.Num() + 1);

	// Thêm dữ liệu của chính người chơi local vào đầu danh sách
	FTeammateVisionData MyData;
	MyData.EyeLocation = DepthCaptureComponent->GetComponentLocation();
	MyData.ForwardVector = OwnerActor->GetActorForwardVector();
	// Giả định vị trí mặt đất, có thể cải thiện bằng line trace nếu cần
	MyData.GroundLocation = OwnerActor->GetActorLocation();
	AllSourcesData.Add(MyData);

	// Thêm dữ liệu của các đồng đội
	AllSourcesData.Append(InTeammateData);

	const int32 NumSources = FMath::Min(AllSourcesData.Num(), MaxTeamSize);

	// --- BƯỚC 3: CẬP NHẬT DATA TEXTURE VỚI DỮ LIỆU MỚI (THREAD-SAFE) ---
	const int32 TextureWidth = MaxTeamSize * DataPointsPerPlayer;
	// Tạo một bản sao dữ liệu để gửi sang Render Thread
	TArray<FLinearColor>* TextureDataCopy = new TArray<FLinearColor>();
	TextureDataCopy->SetNumZeroed(TextureWidth);

	for (int32 i = 0; i < NumSources; ++i)
	{
		const FTeammateVisionData& Source = AllSourcesData[i];
		// Dữ liệu 1: Vị trí mắt (XYZ), Vị trí mặt đất (W là Z)
		(*TextureDataCopy)[i * DataPointsPerPlayer + 0] = FLinearColor(Source.EyeLocation.X, Source.EyeLocation.Y,
																	   Source.EyeLocation.Z, Source.GroundLocation.Z);
		// Dữ liệu 2: Hướng nhìn (XYZ)
		(*TextureDataCopy)[i * DataPointsPerPlayer + 1] = FLinearColor(Source.ForwardVector.X, Source.ForwardVector.Y,
																	   Source.ForwardVector.Z, 0.0f);
	}

	if (FTexture2DResource* TextureResource = static_cast<FTexture2DResource*>(SourceDataTexture->GetResource()))
	{
		ENQUEUE_RENDER_COMMAND(UpdateTeamDataTexture)(
			[TextureResource, TextureDataCopy, TextureWidth](FRHICommandListImmediate& RHICmdList)
			{
				const int32 DataSize = TextureWidth * sizeof(FLinearColor);
				const FUpdateTextureRegion2D Region(0, 0, 0, 0, TextureWidth, 1);
				RHICmdList.UpdateTexture2D(TextureResource->GetTexture2DRHI(), 0, Region, DataSize,
										   reinterpret_cast<const uint8*>(TextureDataCopy->GetData()));
				// Quan trọng: Giải phóng bộ nhớ sau khi đã sử dụng xong trên Render Thread
				delete TextureDataCopy;
			}
		);
	}
	else
	{
		// Nếu không thể gửi lệnh, vẫn phải giải phóng bộ nhớ để tránh memory leak
		delete TextureDataCopy;
	}

	// --- BƯỚC 4: CẬP NHẬT CÁC THAM SỐ MATERIAL CHO LOCAL PLAYER ---

	// 4.1. Cập nhật các tham số trên MID của Post Process
	PostProcessMID->SetScalarParameterValue(FName("NumSources"), NumSources);

	// 4.2. Ghi lại Depth Map từ góc nhìn người chơi local
	DepthCaptureComponent->SetWorldLocationAndRotation(MyData.EyeLocation, MyData.ForwardVector.Rotation());
	DepthCaptureComponent->FOVAngle = VisionAngleDegrees;
	DepthCaptureComponent->CaptureScene();

	// 4.3. Tính toán ma trận View-Projection để gửi vào shader
	// SỬA LỖI: Sử dụng cách tính ma trận chính xác hơn
	// const FMatrix ViewMatrix = FViewMatrix(MyData.EyeLocation, MyData.EyeLocation + MyData.ForwardVector, FVector::UpVector);
	const FMatrix ViewMatrix = FLookFromMatrix(MyData.EyeLocation, MyData.ForwardVector, FVector::UpVector);
	const float AspectRatio = static_cast<float>(DepthRenderTarget->SizeX) / static_cast<float>(DepthRenderTarget->
		SizeY);
	const float HorizontalFOVRadians = FMath::DegreesToRadians(VisionAngleDegrees);
	// Chuyển đổi FOV ngang sang dọc
	const float VerticalFOVRadians = 2.0f * FMath::Atan(FMath::Tan(HorizontalFOVRadians * 0.5f) / AspectRatio);
	// SỬA LỖI: Dùng VisionDistance cho far plane thay vì GNearClippingPlane
	const FMatrix ProjectionMatrix = FReversedZPerspectiveMatrix(VerticalFOVRadians * 0.5f, AspectRatio, 1.0f,
																 VisionDistance);
	const FMatrix ViewProjectionMatrix = ViewMatrix * ProjectionMatrix;

	// 4.4. Gửi ma trận và các tham số khác vào Material Parameter Collection (MPC)
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

	const float VisionConeCos = FMath::Cos(FMath::DegreesToRadians(VisionAngleDegrees * 0.5f));
	UKismetMaterialLibrary::SetScalarParameterValue(GetWorld(), VisionMPC, FName("VisionConeCosine"), VisionConeCos);
	UKismetMaterialLibrary::SetScalarParameterValue(GetWorld(), VisionMPC, FName("VisionMaxDistance"), VisionDistance);
	UKismetMaterialLibrary::SetScalarParameterValue(GetWorld(), VisionMPC, FName("ProximityRadius"), ProximityRadius);
	UKismetMaterialLibrary::SetScalarParameterValue(GetWorld(), VisionMPC, FName("ProximityMaxHeight"),
													ProximityMaxHeight);
	// Gửi thêm các dữ liệu của local player vào MPC để shader không cần đọc lại từ texture
	UKismetMaterialLibrary::SetVectorParameterValue(GetWorld(), VisionMPC, FName("PlayerPosition"),
													FLinearColor(MyData.EyeLocation));
	UKismetMaterialLibrary::SetVectorParameterValue(GetWorld(), VisionMPC, FName("PlayerForwardVector"),
													FLinearColor(MyData.ForwardVector));
	UKismetMaterialLibrary::SetVectorParameterValue(GetWorld(), VisionMPC, FName("PlayerGroundPosition"),
													FLinearColor(MyData.GroundLocation));

	// --- BƯỚC 5: VẼ DEBUG (NẾU ĐƯỢC BẬT) ---
#if ENABLE_DRAW_DEBUG
	if (bIsShowDebug)
	{
		// Vẽ debug cho chính mình
		DrawDebugCone(GetWorld(), MyData.EyeLocation, MyData.ForwardVector, VisionDistance,
					  FMath::DegreesToRadians(VisionAngleDegrees * 0.5f),
					  FMath::DegreesToRadians(VisionAngleDegrees * 0.5f), 32, FColor::Green, false, 0.0f, 0, 1.0f);
		DrawDebugCylinder(GetWorld(), MyData.GroundLocation - FVector(0, 0, ProximityMaxHeight),
						  MyData.GroundLocation + FVector(0, 0, ProximityMaxHeight), ProximityRadius, 32, FColor::Blue,
						  false, 0.0f, 0, 1.0f);

		// Vẽ debug cho đồng đội
		for (int i = 1; i < NumSources; ++i)
		{
			const FTeammateVisionData& Teammate = AllSourcesData[i];
			DrawDebugCone(GetWorld(), Teammate.EyeLocation, Teammate.ForwardVector, VisionDistance,
						  FMath::DegreesToRadians(VisionAngleDegrees * 0.5f),
						  FMath::DegreesToRadians(VisionAngleDegrees * 0.5f), 32, FColor::Cyan, false, 0.0f, 0, 0.5f);
			DrawDebugCylinder(GetWorld(), Teammate.GroundLocation - FVector(0, 0, ProximityMaxHeight),
							  Teammate.GroundLocation + FVector(0, 0, ProximityMaxHeight), ProximityRadius, 32,
							  FColor::Cyan, false, 0.0f, 0, 0.5f);
		}
	}
#endif
}
