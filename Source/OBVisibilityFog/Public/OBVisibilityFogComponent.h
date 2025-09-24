// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/PostProcessComponent.h"
#include "OBVisibilityFogComponent.generated.h"

// Struct FTeammateVisionData vẫn hữu ích để tổ chức dữ liệu nội bộ
USTRUCT(BlueprintType)
struct FTeammateVisionData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Vision Data")
	FVector EyeLocation;

	UPROPERTY(BlueprintReadWrite, Category = "Vision Data")
	FVector ForwardVector;

	UPROPERTY(BlueprintReadWrite, Category = "Vision Data")
	FVector GroundLocation;
};

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class OBVISIBILITYFOG_API UOBVisibilityFogComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UOBVisibilityFogComponent();
	
	UFUNCTION(BlueprintCallable, Category = "Visibility Fog")
	void InitializeFogComponents(UPostProcessComponent* InPostProcessComponent);

	/**
	 * Khởi tạo component với các Scene Capture cần thiết.
	 * Phải được gọi sau BeginPlay từ một đối tượng quản lý.
	 * @param InLocalCaptureComponent Scene Capture của người chơi đang điều khiển.
	 * @param InTeammateCaptureComponents Mảng các Scene Capture của đồng đội.
	 */
	UFUNCTION(BlueprintCallable, Category = "Visibility Fog")
	void Initialize(USceneCaptureComponent2D* InLocalCaptureComponent,
					const TArray<USceneCaptureComponent2D*>& InTeammateCaptureComponents);

	/**
	 * Cập nhật toàn bộ logic sương mù.
	 * Hàm này sẽ tự động lấy thông tin từ các Scene Capture đã được Initialize.
	 */
	UFUNCTION(BlueprintCallable, Category = "Visibility Fog")
	void UpdateData();

protected:
	virtual void BeginPlay() override;

public:
	// --- CÁC THAM SỐ CẤU HÌNH ---
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visibility Fog|Dependencies")
	TObjectPtr<UPostProcessComponent> FogPostProcessComponent;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visibility Fog|Dependencies")
	TObjectPtr<UMaterial> FogPostProcessMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visibility Fog|Config")
	int32 RenderTargetResolution = 512;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visibility Fog|Config")
	float VisionDistance = 2000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visibility Fog|Config")
	float VisionAngleDegrees = 90.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visibility Fog|Config")
	float ProximityRadius = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visibility Fog|Config")
	float ProximityMaxHeight = 200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visibility Fog|Config")
	TEnumAsByte<ECollisionChannel> GroundTraceChannel = ECC_Visibility;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visibility Fog|Debug")
	bool bIsShowDebug = false;

private:
	// --- CÁC THUỘC TÍNH QUẢN LÝ BÊN TRONG ---
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> SourceDataTexture;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> MatrixDataTexture;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PostProcessMID;

	// Pool này giờ sẽ lưu trữ các con trỏ được truyền vào từ bên ngoài
	UPROPERTY(Transient)
	TArray<TObjectPtr<USceneCaptureComponent2D>> CaptureComponentPool;

	// Pool này vẫn được tạo và sở hữu bởi component này
	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextureRenderTarget2D>> RenderTargetPool;

	const int32 MaxTeamSize = 8;
	const int32 DataPointsPerPlayer = 2;
	const int32 MatrixDataPointsPerPlayer = 4;

	bool bIsReadyToUpdate = false;
};
