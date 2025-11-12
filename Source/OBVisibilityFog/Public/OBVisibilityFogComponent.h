// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/PostProcessComponent.h"
#include "OBVisibilityFogComponent.generated.h"

/**
 * @struct FTeammateVisionData
 * @brief Struct để lưu trữ dữ liệu về tầm nhìn cần thiết của một người chơi.
 * Dữ liệu này sẽ được gửi vào texture để shader xử lý.
 */
USTRUCT(BlueprintType)
struct FTeammateVisionData
{
	GENERATED_BODY()

	// Vị trí mắt (nguồn của tầm nhìn)
	UPROPERTY(BlueprintReadWrite, Category = "Vision Data")
	FVector EyeLocation = FVector::ZeroVector;

	// Vector chỉ hướng nhìn về phía trước
	UPROPERTY(BlueprintReadWrite, Category = "Vision Data")
	FVector ForwardVector = FVector::ZeroVector;

	// Vị trí trên mặt đất, dùng cho vùng bán kính xung quanh
	UPROPERTY(BlueprintReadWrite, Category = "Vision Data")
	FVector GroundLocation = FVector::ZeroVector;
};

/**
 * @class UOBVisibilityFogComponent
 * @brief Quản lý logic sương mù chiến tranh (Fog of War) cho một Actor.
 * Component này chịu trách nhiệm thu thập dữ liệu tầm nhìn, cập nhật các tham số cho material
 * và render hiệu ứng che khuất cuối cùng thông qua Post Process.
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class OBVISIBILITYFOG_API UOBVisibilityFogComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UOBVisibilityFogComponent();

	/**
	 * Khởi tạo các component cần thiết từ Blueprint hoặc C++.
	 * @param CaptureComponent SceneCapture2D component để ghi lại depth map.
	 * @param PostProcessComponent PostProcess component để áp dụng hiệu ứng sương mù.
	 * @param InFogPostProcessMaterial Material dùng cho hiệu ứng post process.
	 */
	UFUNCTION(BlueprintCallable, Category = "Visibility Fog")
	void InitializeFogComponents(USceneCaptureComponent2D* CaptureComponent,
	                             UPostProcessComponent* PostProcessComponent, UMaterial* InFogPostProcessMaterial);

	/**
	 * Cập nhật dữ liệu tầm nhìn từ tất cả các nguồn (bản thân và đồng đội).
	 * Đây là hàm chính để chạy logic mỗi frame.
	 * @param InTeammateData Mảng chứa dữ liệu tầm nhìn của các đồng đội.
	 */
	UFUNCTION(BlueprintCallable, Category = "Visibility Fog")
	void UpdateData(const TArray<FTeammateVisionData>& InTeammateData);

protected:
	virtual void BeginPlay() override;

public:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
	                           FActorComponentTickFunction* ThisTickFunction) override;

	// --- CÁC THAM SỐ CẤU HÌNH (Được thiết lập trên Character) ---

	/** Component dùng để ghi lại depth map từ góc nhìn của người chơi local. GÁN TRONG BLUEPRINT. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visibility Fog|Dependencies")
	TObjectPtr<USceneCaptureComponent2D> DepthCaptureComponent;

	/** Component PostProcess để áp dụng material sương mù lên toàn màn hình. GÁN TRONG BLUEPRINT. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visibility Fog|Dependencies")
	TObjectPtr<UPostProcessComponent> FogPostProcessComponent;

	/** Render Target để lưu trữ depth map được ghi lại. GÁN TRONG BLUEPRINT. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visibility Fog|Dependencies")
	TObjectPtr<UTextureRenderTarget2D> DepthRenderTarget;

	/** Material Post Process chính chứa logic shader sương mù. GÁN TRONG BLUEPRINT. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visibility Fog|Dependencies")
	TObjectPtr<UMaterial> FogPostProcessMaterial;

	/** Material Parameter Collection để gửi dữ liệu chung (vd: ma trận) vào shader. GÁN TRONG BLUEPRINT. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visibility Fog|Dependencies")
	TObjectPtr<UMaterialParameterCollection> VisionMPC;

	/** Kênh va chạm dùng cho việc tìm các actor trong tầm nhìn. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visibility Fog|Config")
	TEnumAsByte<ECollisionChannel> VisionTraceChannel = ECC_WorldStatic;

	/** Khoảng cách tối đa của hình nón tầm nhìn. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visibility Fog|Config")
	float VisionDistance = 2000.0f;

	/** Góc của hình nón tầm nhìn (tính bằng độ). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visibility Fog|Config")
	float VisionAngleDegrees = 90.0f;

	/** Bán kính của vùng phát hiện xung quanh người chơi. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visibility Fog|Config")
	float ProximityRadius = 100.0f;

	/** Chiều cao tối đa mà vùng bán kính xung quanh có thể ảnh hưởng. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visibility Fog|Config")
	float ProximityMaxHeight = 200.0f;

	/** Bật/tắt hiển thị debug. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visibility Fog|Debug")
	bool bIsShowDebug = false;

	/** Bật/tắt hiển thị thông báo debug trên màn hình. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visibility Fog|Debug")
	bool bIsShowDebugMessage = false;

private:
	// Texture dùng để chứa dữ liệu vị trí và hướng nhìn của tất cả người chơi.
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> SourceDataTexture;

	// Material Instance Dynamic của Post Process để có thể thay đổi tham số lúc runtime.
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PostProcessMID;

	// Số lượng người chơi tối đa mà hệ thống hỗ trợ.
	const int32 MaxTeamSize = 8;
	// Số điểm dữ liệu cho mỗi người chơi (1 cho vị trí, 1 cho hướng nhìn).
	const int32 DataPointsPerPlayer = 2;

	// Cờ để đảm bảo logic chỉ chạy khi component đã sẵn sàng.
	bool bIsReadyToUpdate = false;
};
