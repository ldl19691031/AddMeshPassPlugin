#include "RenderMyMeshPass.h"
#include "EngineModule.h"
#include "Components/PrimitiveComponent.h"
#include "Materials/Material.h"
#include "MeshPassProcessor.inl"
#include "RenderGraphUtils.h"
#include "RHIGPUReadback.h"
#include "RenderingThread.h"
#include "SceneInterface.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "SceneTexturesConfig.h"
#include "SimpleMeshDrawCommandPass.h"
#include "SceneRendererInterface.h"
#include "StaticMeshSceneProxy.h"
#include "Engine/TextureRenderTarget2D.h"

DEFINE_LOG_CATEGORY(LogRenderMyMeshPass);

TSoftObjectPtr<class UTextureRenderTarget2D> UMyMeshPassManager::MyMeshPassOutput(FSoftObjectPath("/Script/Engine.TextureRenderTarget2D'/AddMeshPassPlugin/RT_Output.RT_Output'"));

//Begin Shaders

class FMyMeshPassMaterialShader : public FMeshMaterialShader {
public:
	FMyMeshPassMaterialShader(const FMeshMaterialShaderType::CompiledShaderInitializerType& Initializer)
		: FMeshMaterialShader(Initializer) 
	{
		PassUniformBuffer.Bind(Initializer.ParameterMap, FSceneTextureUniformParameters::FTypeInfo::GetStructMetadata()->GetShaderVariableName());
	}

	FMyMeshPassMaterialShader() {}

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

class FMyMeshPassMaterialShaderVS : public FMyMeshPassMaterialShader
{
	DECLARE_SHADER_TYPE(FMyMeshPassMaterialShaderVS, MeshMaterial);

public:
	FMyMeshPassMaterialShaderVS()
	{}

	FMyMeshPassMaterialShaderVS(const FMeshMaterialShaderType::CompiledShaderInitializerType& Initializer)
		: FMyMeshPassMaterialShader(Initializer)
	{
	}
};
IMPLEMENT_MATERIAL_SHADER_TYPE(, FMyMeshPassMaterialShaderVS, TEXT("/Plugin/Runtime/AddMeshPassPlugin/MyMeshPassMaterialShader.usf"), TEXT("VSMain"), SF_Vertex);


struct FMyMeshPassShaderElementData : public FMeshMaterialShaderElementData
{
	FMyMeshPassShaderElementData() {}

	FVector3f ExampleColorProperty;
};


class FMyMeshPassMaterialShaderPS : public FMyMeshPassMaterialShader
{
	DECLARE_SHADER_TYPE(FMyMeshPassMaterialShaderPS, MeshMaterial);

public:
	FMyMeshPassMaterialShaderPS()
	{}

	FMyMeshPassMaterialShaderPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FMyMeshPassMaterialShader(Initializer)
	{
		ExampleColorProperty.Bind(Initializer.ParameterMap, TEXT("ExampleColorProperty"));
	}

	void GetShaderBindings(
		const FScene* Scene,
		ERHIFeatureLevel::Type FeatureLevel,
		const FPrimitiveSceneProxy* PrimitiveSceneProxy,
		const FMaterialRenderProxy& MaterialRenderProxy,
		const FMaterial& Material,
		const FMyMeshPassShaderElementData& ShaderElementData,
		FMeshDrawSingleShaderBindings& ShaderBindings) const
	{
		FMeshMaterialShader::GetShaderBindings(Scene, FeatureLevel, PrimitiveSceneProxy, MaterialRenderProxy, Material, ShaderElementData, ShaderBindings);

		ShaderBindings.Add(ExampleColorProperty, ShaderElementData.ExampleColorProperty);
	}

	void GetElementShaderBindings(
		const FShaderMapPointerTable& PointerTable,
		const FScene* Scene,
		const FSceneView* ViewIfDynamicMeshCommand,
		const FVertexFactory* VertexFactory,
		const EVertexInputStreamType InputStreamType,
		const FStaticFeatureLevel FeatureLevel,
		const FPrimitiveSceneProxy* PrimitiveSceneProxy,
		const FMeshBatch& MeshBatch,
		const FMeshBatchElement& BatchElement,
		const FMyMeshPassShaderElementData& ShaderElementData,
		FMeshDrawSingleShaderBindings& ShaderBindings,
		FVertexInputStreamArray& VertexStreams) const
	{
		FMeshMaterialShader::GetElementShaderBindings(PointerTable, Scene, ViewIfDynamicMeshCommand, VertexFactory, InputStreamType, FeatureLevel, PrimitiveSceneProxy, MeshBatch, BatchElement, ShaderElementData, ShaderBindings, VertexStreams);
	}

	LAYOUT_FIELD(FShaderParameter, ExampleColorProperty);
};
IMPLEMENT_MATERIAL_SHADER_TYPE(, FMyMeshPassMaterialShaderPS, TEXT("/Plugin/Runtime/AddMeshPassPlugin/MyMeshPassMaterialShader.usf"), TEXT("PSMain"), SF_Pixel);
//End Shaders

//Begin Mesh Pass Processor
class FMyMeshPassProcessor : public FMeshPassProcessor
{
public:
	FMyMeshPassProcessor(
			const FScene* InScene, 
			const FSceneView* InViewIfDynamicMeshCommand, 
			FMeshPassDrawListContext* InDrawListContext)
		: FMeshPassProcessor(
			EMeshPass::Num,  //<-- Is this OK?
			InScene, 
			InViewIfDynamicMeshCommand->GetFeatureLevel(), InViewIfDynamicMeshCommand, InDrawListContext
		)
	{
		PassDrawRenderState.SetBlendState(TStaticBlendState<>::GetRHI());
		PassDrawRenderState.SetDepthStencilState(TStaticDepthStencilState<>::GetRHI());
	}

	virtual void AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId = -1) override;
	
	FMeshPassProcessorRenderState PassDrawRenderState;

	FMyMeshPassViewExtension::FAdditionalData MyMeshPassPrimitivesData;
};

//End Mesh Pass Processor


inline void FMyMeshPassProcessor::AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId)
{
	const FMaterialRenderProxy* MaterialRenderProxy = MeshBatch.MaterialRenderProxy;
	while (MaterialRenderProxy)
	{
		const FMaterial* Material = MaterialRenderProxy->GetMaterialNoFallback(FeatureLevel);
		if (Material)
		{
			const FVertexFactory* VertexFactory = MeshBatch.VertexFactory;

			TMeshProcessorShaders<
				FMyMeshPassMaterialShaderVS,
				FMyMeshPassMaterialShaderPS> PassShaders;

			FMaterialShaderTypes ShaderTypes;
			ShaderTypes.AddShaderType<FMyMeshPassMaterialShaderVS>();
			ShaderTypes.AddShaderType<FMyMeshPassMaterialShaderPS>();

			FMaterialShaders Shaders;
			if (!Material->TryGetShaders(ShaderTypes, VertexFactory->GetType(), Shaders))
			{
				MaterialRenderProxy = MaterialRenderProxy->GetFallback(FeatureLevel);
				continue;
			}
			Shaders.TryGetVertexShader(PassShaders.VertexShader);
			Shaders.TryGetPixelShader(PassShaders.PixelShader);

			const FMeshDrawingPolicyOverrideSettings OverrideSettings = ComputeMeshOverrideSettings(MeshBatch);
			const ERasterizerFillMode MeshFillMode = ComputeMeshFillMode(*Material, OverrideSettings);
			const ERasterizerCullMode MeshCullMode = CM_None;

			const FMeshDrawCommandSortKey SortKey = CalculateMeshStaticSortKey(PassShaders.VertexShader, PassShaders.PixelShader);

			FMyMeshPassShaderElementData ShaderElementData;
			ShaderElementData.InitializeMeshMaterialData(ViewIfDynamicMeshCommand, PrimitiveSceneProxy, MeshBatch, -1, true);
			ShaderElementData.ExampleColorProperty = FVector3f(MyMeshPassPrimitivesData.ExampleColorProperty.X, MyMeshPassPrimitivesData.ExampleColorProperty.Y,
				MyMeshPassPrimitivesData.ExampleColorProperty.Z);

			BuildMeshDrawCommands(
				MeshBatch,
				BatchElementMask,
				PrimitiveSceneProxy,
				*MaterialRenderProxy,
				*Material,
				PassDrawRenderState,
				PassShaders,
				MeshFillMode,
				MeshCullMode,
				SortKey,
				EMeshPassFeatures::Default,
				ShaderElementData
			);

			break;
		}

		MaterialRenderProxy = MaterialRenderProxy->GetFallback(FeatureLevel);
	}

}

// Manager and View Extension
void UMyMeshPassManager::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	MyMeshPassOutputRT = MyMeshPassOutput.LoadSynchronous();
	check(MyMeshPassOutputRT);

	MyMeshPassViewExtension = FSceneViewExtensions::NewExtension<FMyMeshPassViewExtension>(GetWorld());
}

void UMyMeshPassManager::Deinitialize()
{
	Super::Deinitialize();
}


void UMyMeshPassManager::RegisterComponent(URenderInMyMeshPassStaticMeshComponent* Component)
{
	RegisteredComponents.Add(Component);
}

void UMyMeshPassManager::UnregisterComponent(URenderInMyMeshPassStaticMeshComponent* Component)
{
	RegisteredComponents.Remove(Component);
}

FMyMeshPassViewExtension::FMyMeshPassViewExtension(const FAutoRegister& AutoRegister, UWorld* InWorld)
	: FWorldSceneViewExtension(AutoRegister, InWorld)
{
	UMyMeshPassManager* Manager = InWorld->GetSubsystem<UMyMeshPassManager>();
	if (Manager)
	{
		MyMeshPassOutputRTResource = Manager->GetMyMeshPassOutputRT()->GameThread_GetRenderTargetResource();
		check(MyMeshPassOutputRTResource);
	}
}


void FMyMeshPassViewExtension::SetupViewFamily(FSceneViewFamily& InViewFamily)
{
	check(IsInGameThread());
	MyMeshPassPrimitives.Empty();
	MyMeshPassPrimitivesData.Empty();
	if (this->GetWorld())
	{
		UMyMeshPassManager* Manager = this->GetWorld()->GetSubsystem<UMyMeshPassManager>();
		if (Manager)
		{
			for (URenderInMyMeshPassStaticMeshComponent* Component : Manager->GetRegisteredComponents())
			{
				if (Component && Component->IsVisible())
				{
					MyMeshPassPrimitives.Add(Component->GetSceneProxy());

					FAdditionalData Data;
					Data.ExampleColorProperty = Component->ExampleColorProperty;
					MyMeshPassPrimitivesData.Add(Component->GetSceneProxy(), Data);
				}
			}

			const FIntPoint RenderTargetSize = InViewFamily.RenderTarget->GetSizeXY();
			UTextureRenderTarget2D* MyMeshPassOutputRT = Manager->GetMyMeshPassOutputRT();
			if (MyMeshPassOutputRT->SizeX != RenderTargetSize.X 
				|| MyMeshPassOutputRT->SizeY != RenderTargetSize.Y)
			{
				MyMeshPassOutputRT->ResizeTarget(RenderTargetSize.X, RenderTargetSize.Y);
				//We must flush rendering commands here to make sure the render target is ready for rendering
				FlushRenderingCommands();
			}
		}
	}
	
}

bool FMyMeshPassViewExtension::IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const
{
	return true;
}

BEGIN_SHADER_PARAMETER_STRUCT(FRenderMyMeshPassParameters, )
	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneUniformParameters, Scene)
	SHADER_PARAMETER_STRUCT_INCLUDE(FInstanceCullingDrawParams, InstanceCullingDrawParams)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

void FMyMeshPassViewExtension::PostRenderBasePassDeferred_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& View, const FRenderTargetBindingSlots& RenderTargets, TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTextures)
{
	RDG_GPU_MASK_SCOPE(GraphBuilder, View.GPUMask);
	RDG_EVENT_SCOPE(GraphBuilder, "RenderMyMeshPass");
	if (MyMeshPassPrimitives.Num() == 0)
	{
		return;
	}

	const FIntPoint RenderTargetSize = View.Family->RenderTarget->GetSizeXY();
	if (RenderTargetSize.X <= 0 || RenderTargetSize.Y <= 0)
	{
		return;
	}
	if (!MyMeshPassOutputRTResource)
	{
		return;
	}
	if(MyMeshPassOutputRTResource->GetSizeXY() != RenderTargetSize)
	{
		return;
	}

	//FRDGTextureRef OutputTexture = GraphBuilder.CreateTexture(
	//	FRDGTextureDesc::Create2D(RenderTargetSize, PF_R8G8B8A8, FClearValueBinding::Black, TexCreate_RenderTargetable | TexCreate_ShaderResource),
	//	TEXT("RenderMyMeshPassTempRT"));

	FRDGTextureRef DepthTexture = GraphBuilder.CreateTexture(
		FRDGTextureDesc::Create2D(RenderTargetSize, PF_DepthStencil, FClearValueBinding::DepthFar, TexCreate_DepthStencilTargetable),
		TEXT("RenderMyMeshPassTempDepth"));
	const FTextureRHIRef SourceTexture = MyMeshPassOutputRTResource->GetTexture2DRHI();
	FRDGTextureRef MyMeshPassOutputTexture = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(SourceTexture, TEXT("OutputRT")));
	auto* PassParameters = GraphBuilder.AllocParameters<FRenderMyMeshPassParameters>();
	PassParameters->View = View.ViewUniformBuffer;
	PassParameters->Scene = GetSceneUniformBufferRef(GraphBuilder, View);
	PassParameters->RenderTargets[0] = FRenderTargetBinding(MyMeshPassOutputTexture, ERenderTargetLoadAction::EClear);
	PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(DepthTexture, ERenderTargetLoadAction::EClear, ERenderTargetLoadAction::ENoAction, FExclusiveDepthStencil::DepthWrite_StencilNop);
	
	TArray< const FPrimitiveSceneProxy* > RenderedPrimitives = MyMeshPassPrimitives;
	TMap< const FPrimitiveSceneProxy*, FAdditionalData> RenderedPrimitivesData = this->MyMeshPassPrimitivesData;
	AddSimpleMeshPass(
		GraphBuilder, 
		PassParameters, 
		View.Family->Scene->GetRenderScene(), 
		View, 
		nullptr, 
		RDG_EVENT_NAME("RenderMyMeshPass"), 
		View.UnscaledViewRect,
		[View, RenderedPrimitives, RenderedPrimitivesData](FDynamicPassMeshDrawListContext* DynamicMeshPassContext)
	{
		FMyMeshPassProcessor PassMeshProcessor(nullptr, &View, DynamicMeshPassContext);
		for (const FPrimitiveSceneProxy* primitive : RenderedPrimitives)
		{
			if (primitive == nullptr)
			{
				continue;
			}
			const FStaticMeshSceneProxy* MeshProxy = static_cast<const FStaticMeshSceneProxy*>(primitive);
			int32 LODIndex = 0;
			TArray<FMeshBatch> MeshElements;
			primitive->GetMeshDescription(LODIndex, MeshElements);
			if (MeshElements.Num() == 0)
			{
				continue;
			}
			PassMeshProcessor.MyMeshPassPrimitivesData = RenderedPrimitivesData[primitive];
			PassMeshProcessor.AddMeshBatch(MeshElements[0], 1, primitive);
		}
	});

	
}

void URenderInMyMeshPassStaticMeshComponent::OnRegister()
{
	Super::OnRegister();
	//register self to the manager
	UMyMeshPassManager* Manager = GetWorld()->GetSubsystem<UMyMeshPassManager>();
	if (Manager)
	{
		Manager->RegisterComponent(this);
	}
	else
	{
		UE_LOG(LogRenderMyMeshPass, Warning, TEXT("Failed to register component to manager"));
	}
}

void URenderInMyMeshPassStaticMeshComponent::OnUnregister()
{
	Super::OnUnregister();
	//unregister self from the manager
	UMyMeshPassManager* Manager = GetWorld()->GetSubsystem<UMyMeshPassManager>();
	if (Manager)
	{
		Manager->UnregisterComponent(this);
	}
	else
	{
		UE_LOG(LogRenderMyMeshPass, Warning, TEXT("Failed to unregister component from manager"));
	}
}
