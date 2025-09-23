// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/PostProcessComponent.h"
#include "OBVisibilityFogComponent.generated.h"

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

	/**
	 * Function to set up required components from Blueprint or C++.
	 * @param CaptureComponent SceneCapture2D component to be used.
	 * @param PostProcessComponent PostProcess component to be used.
	 * @param InFogPostProcessMaterial
	 */
	UFUNCTION(BlueprintCallable, Category = "Visibility Fog")
	void InitializeFogComponents(USceneCaptureComponent2D* CaptureComponent,
								 UPostProcessComponent* PostProcessComponent, UMaterial* InFogPostProcessMaterial);

	// UFUNCTION(BlueprintCallable, Category = "Visibility Fog")
	// void UpdateTeammateData(const TArray<FTeammateVisionData>& InTeammateData);

	UFUNCTION(BlueprintCallable, Category = "Visibility Fog")
	void UpdateData(const TArray<FTeammateVisionData>& InTeammateData);

protected:
	virtual void BeginPlay() override;

public:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
							   FActorComponentTickFunction* ThisTickFunction) override;

	// --- CONFIGURATION PARAMETERS (Transferred from Character) ---

	/** The component that captures the vision cone's depth map. ASSIGN IN BLUEPRINT EDITOR. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visibility Fog|Dependencies")
	TObjectPtr<USceneCaptureComponent2D> DepthCaptureComponent;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visibility Fog|Dependencies")
	TObjectPtr<UPostProcessComponent> FogPostProcessComponent;

	/** The Render Target asset to store the depth map. ASSIGN IN BLUEPRINT EDITOR. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visibility Fog|Dependencies")
	TObjectPtr<UTextureRenderTarget2D> DepthRenderTarget;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visibility Fog|Dependencies")
	TObjectPtr<UMaterial> FogPostProcessMaterial;

	/** The Material Parameter Collection to send data to shaders. ASSIGN IN BLUEPRINT EDITOR. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visibility Fog|Dependencies")
	TObjectPtr<UMaterialParameterCollection> VisionMPC;

	/** Maximum distance for vision cone. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visibility Fog|Config")
	float VisionDistance = 2000.0f;
	
	/** Angle of vision cone in degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visibility Fog|Config")
	float VisionAngleDegrees = 90.0f;

	/** Detection radius around the character. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visibility Fog|Config")
	float ProximityRadius = 100.0f; 

	/** Maximum height that the proximity detection area can affect. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visibility Fog|Config")
	float ProximityMaxHeight = 200.0f;
	
	/** Enable/disable debug visualization. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visibility Fog|Debug")
	bool bIsShowDebug = false;

	// Enable/disable show the debug message on the screen
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visibility Fog|Debug")
	bool bIsShowDebugMessage = false;

private:
	// --- CÁC THUỘC TÍNH MỚI ---
    
	// // Mảng lưu trữ dữ liệu của team, được cập nhật từ bên ngoài
	// TArray<FTeammateVisionData> CurrentTeammateData;
    
	// Texture chứa dữ liệu (vị trí, hướng nhìn...) để gửi vào shader
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> SourceDataTexture;

	// Material Instance của Post Process để set các tham số động
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PostProcessMID;

	const int32 MaxTeamSize = 8; // Số lượng người chơi tối đa hệ thống hỗ trợ
	const int32 DataPointsPerPlayer = 2; // 1 cho vị trí, 1 cho hướng nhìn

	bool bIsReadyToTick = false;
};
