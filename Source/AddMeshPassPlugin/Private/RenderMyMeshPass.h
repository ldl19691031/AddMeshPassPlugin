#pragma once
#include "Stats/Stats2.h"
#include "Subsystems/EngineSubsystem.h"
#include "SceneViewExtension.h"
#include "Components/StaticMeshComponent.h"
#include "RenderMyMeshPass.generated.h"

class FMyMeshPassViewExtension;
class URenderInMyMeshPassStaticMeshComponent;
class FPrimitiveSceneProxy;
class UTextureRenderTarget2D;

DECLARE_LOG_CATEGORY_EXTERN(LogRenderMyMeshPass, Log, All);

UCLASS()
class UMyMeshPassManager : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	void RegisterComponent(URenderInMyMeshPassStaticMeshComponent* Component);
	void UnregisterComponent(URenderInMyMeshPassStaticMeshComponent* Component);

	FORCEINLINE const TArray<URenderInMyMeshPassStaticMeshComponent*>& GetRegisteredComponents() const { return RegisteredComponents; }
	FORCEINLINE UTextureRenderTarget2D* GetMyMeshPassOutputRT() const { return MyMeshPassOutputRT; }
private:
	TSharedPtr<FMyMeshPassViewExtension, ESPMode::ThreadSafe> MyMeshPassViewExtension;

	UPROPERTY()
	TArray<URenderInMyMeshPassStaticMeshComponent*> RegisteredComponents;

	UPROPERTY()
	UTextureRenderTarget2D* MyMeshPassOutputRT;

	static TSoftObjectPtr<UTextureRenderTarget2D> MyMeshPassOutput;
};

class FMyMeshPassViewExtension : public FWorldSceneViewExtension
{
public:
	FMyMeshPassViewExtension(const FAutoRegister& AutoRegister, UWorld* InWorld);
	// FSceneViewExtensionBase implementation : 
	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override;
	virtual void PostRenderBasePassDeferred_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView, const FRenderTargetBindingSlots& RenderTargets, TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTextures) override;
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override {}
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual bool IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const override;
	// End FSceneViewExtensionBase implementation

	struct FAdditionalData {
		FVector ExampleColorProperty;
	};

private:
	TArray< const FPrimitiveSceneProxy* > MyMeshPassPrimitives;



	TMap<const FPrimitiveSceneProxy*, FAdditionalData> MyMeshPassPrimitivesData;

	FTextureRenderTargetResource* MyMeshPassOutputRTResource;
};

UCLASS(Blueprintable)
class URenderInMyMeshPassStaticMeshComponent : public UStaticMeshComponent
{
	GENERATED_BODY()
public:
	//override OnRegister and OnUnregister
	virtual void OnRegister() override;
	virtual void OnUnregister() override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MyMeshPass")
	FVector ExampleColorProperty;
};