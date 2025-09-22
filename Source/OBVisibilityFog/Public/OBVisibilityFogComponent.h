// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/PostProcessComponent.h"
#include "OBVisibilityFogComponent.generated.h"

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
	 */
	UFUNCTION(BlueprintCallable, Category = "Visibility Fog")
	void InitializeFogComponents(USceneCaptureComponent2D* CaptureComponent,
								 UPostProcessComponent* PostProcessComponent);

protected:
	virtual void BeginPlay() override;

public:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
							   FActorComponentTickFunction* ThisTickFunction) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visibility Fog|Dependencies")
	USceneCaptureComponent2D* TargetSceneCapture = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visibility Fog|Dependencies")
	UPostProcessComponent* TargetPostProcess = nullptr;

protected:
};
