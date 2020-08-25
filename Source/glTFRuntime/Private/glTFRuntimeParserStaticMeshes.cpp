// Copyright 2020, Roberto De Ioris.

#include "glTFRuntimeParser.h"
#include "StaticMeshDescription.h"
#include "PhysXCookHelper.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Interfaces/ITargetPlatform.h"

UStaticMesh* FglTFRuntimeParser::LoadStaticMesh_Internal(TSharedRef<FJsonObject> JsonMeshObject, const FglTFRuntimeStaticMeshConfig& StaticMeshConfig)
{
	const TArray<TSharedPtr<FJsonValue>>* JsonPrimitives;
	if (!JsonMeshObject->TryGetArrayField("primitives", JsonPrimitives))
	{
		return nullptr;
	}

	TArray<FglTFRuntimePrimitive> Primitives;
	if (!LoadPrimitives(JsonPrimitives, Primitives, StaticMeshConfig.MaterialsConfig))
		return nullptr;

	UStaticMesh* StaticMesh = NewObject<UStaticMesh>(StaticMeshConfig.Outer ? StaticMeshConfig.Outer : GetTransientPackage(), NAME_None, RF_Public);
	StaticMesh->bAllowCPUAccess = StaticMeshConfig.bAllowCPUAccess;

	UStaticMeshDescription* MeshDescription = UStaticMesh::CreateStaticMeshDescription();

	TArray<FStaticMaterial> StaticMaterials;

	TArray<uint32> CPUVertexInstancesIDs;

	int32 NumUVs = 1;
	for (FglTFRuntimePrimitive& Primitive : Primitives)
	{
		if (Primitive.UVs.Num() > NumUVs)
		{
			NumUVs = Primitive.UVs.Num();
		}
	}

	for (FglTFRuntimePrimitive& Primitive : Primitives)
	{
		FPolygonGroupID PolygonGroupID = MeshDescription->CreatePolygonGroup();

		TPolygonGroupAttributesRef<FName> PolygonGroupMaterialSlotNames = MeshDescription->GetPolygonGroupMaterialSlotNames();
		PolygonGroupMaterialSlotNames[PolygonGroupID] = Primitive.Material->GetFName();
		FStaticMaterial StaticMaterial(Primitive.Material, Primitive.Material->GetFName());
		StaticMaterial.UVChannelData.bInitialized = true;
		StaticMaterials.Add(StaticMaterial);

		TVertexAttributesRef<FVector> PositionsAttributesRef = MeshDescription->GetVertexPositions();
		TVertexInstanceAttributesRef<FVector> NormalsInstanceAttributesRef = MeshDescription->GetVertexInstanceNormals();
		TVertexInstanceAttributesRef<FVector> TangentsInstanceAttributesRef = MeshDescription->GetVertexInstanceTangents();
		TVertexInstanceAttributesRef<FVector2D> UVsInstanceAttributesRef = MeshDescription->GetVertexInstanceUVs();
		TVertexInstanceAttributesRef<FVector4> ColorsInstanceAttributesRef = MeshDescription->GetVertexInstanceColors();

		UVsInstanceAttributesRef.SetNumIndices(NumUVs);

		TArray<FVertexInstanceID> VertexInstancesIDs;
		TArray<FVertexID> VerticesIDs;
		TArray<FVertexID> TriangleVerticesIDs;


		for (FVector& Position : Primitive.Positions)
		{
			FVertexID VertexID = MeshDescription->CreateVertex();
			PositionsAttributesRef[VertexID] = Position;
			VerticesIDs.Add(VertexID);
		}

		for (uint32 VertexIndex : Primitive.Indices)
		{
			if (VertexIndex >= (uint32)VerticesIDs.Num())
				return nullptr;

			FVertexInstanceID NewVertexInstanceID = MeshDescription->CreateVertexInstance(VerticesIDs[VertexIndex]);

			if (StaticMesh->bAllowCPUAccess)
			{
				CPUVertexInstancesIDs.Add(NewVertexInstanceID.GetValue());
			}

			if (Primitive.Normals.Num() > 0)
			{
				if (VertexIndex >= (uint32)Primitive.Normals.Num())
				{
					NormalsInstanceAttributesRef[NewVertexInstanceID] = FVector::ZeroVector;
				}
				else
				{
					NormalsInstanceAttributesRef[NewVertexInstanceID] = Primitive.Normals[VertexIndex];
				}
			}

			if (Primitive.Tangents.Num() > 0)
			{
				if (VertexIndex >= (uint32)Primitive.Tangents.Num())
				{
					TangentsInstanceAttributesRef[NewVertexInstanceID] = FVector::ZeroVector;
				}
				else
				{
					TangentsInstanceAttributesRef[NewVertexInstanceID] = Primitive.Tangents[VertexIndex];
				}
			}

			if (Primitive.Colors.Num() > 0)
			{
				if (VertexIndex >= (uint32)Primitive.Colors.Num())
				{
					ColorsInstanceAttributesRef[NewVertexInstanceID] = FVector4(0, 0, 0, 0);
				}
				else
				{
					ColorsInstanceAttributesRef[NewVertexInstanceID] = Primitive.Colors[VertexIndex];
				}
			}

			for (int32 UVIndex = 0; UVIndex < Primitive.UVs.Num(); UVIndex++)
			{
				if (VertexIndex >= (uint32)Primitive.UVs[UVIndex].Num())
				{
					UVsInstanceAttributesRef.Set(NewVertexInstanceID, UVIndex, FVector2D::ZeroVector);
				}
				else
				{
					UVsInstanceAttributesRef.Set(NewVertexInstanceID, UVIndex, Primitive.UVs[UVIndex][VertexIndex]);
				}
			}

			VertexInstancesIDs.Add(NewVertexInstanceID);
			TriangleVerticesIDs.Add(VerticesIDs[VertexIndex]);

			if (VertexInstancesIDs.Num() == 3)
			{
				// degenerate ?
				if (TriangleVerticesIDs[0] == TriangleVerticesIDs[1] ||
					TriangleVerticesIDs[1] == TriangleVerticesIDs[2] ||
					TriangleVerticesIDs[0] == TriangleVerticesIDs[2])
				{
					VertexInstancesIDs.Empty();
					TriangleVerticesIDs.Empty();
					continue;
				}

				TArray<FEdgeID> Edges;
				// fix winding ?
				if (StaticMeshConfig.bReverseWinding)
				{
					VertexInstancesIDs.Swap(1, 2);
				}
				FTriangleID TriangleID = MeshDescription->CreateTriangle(PolygonGroupID, VertexInstancesIDs, Edges);
				if (TriangleID == FTriangleID::Invalid)
				{
					return nullptr;
				}
				VertexInstancesIDs.Empty();
				TriangleVerticesIDs.Empty();
			}
		}

	}

	StaticMesh->StaticMaterials = StaticMaterials;

	TArray<UStaticMeshDescription*> MeshDescriptions = { MeshDescription };
	//StaticMesh->BuildFromStaticMeshDescriptions(MeshDescriptions, false);


	/*//StaticMesh->LightMapCoordinateIndex = NumUVs;

	ENQUEUE_RENDER_COMMAND(InitColorVertexBufferCommand)(
		[StaticMesh, bHasVertexColors](FRHICommandListImmediate& RHICmdList)
	{
		for (FStaticMeshLODResources& Resources : StaticMesh->RenderData->LODResources)
		{
			Resources.bHasColorVertexData = bHasVertexColors;
			if (!Resources.VertexBuffers.ColorVertexBuffer.IsInitialized())
			{
				Resources.VertexBuffers.ColorVertexBuffer.InitResource();
				//Resources.VertexBuffers.ColorVertexBuffer.Init(Resources.VertexBuffers.StaticMeshVertexBuffer.GetNumVertices(), true);
			}

			//if (Resources.bHasColorVertexData)
			{

			}
			UE_LOG(LogTemp, Error, TEXT("ColorVertexBuffer: %d %d %d"), Resources.VertexBuffers.ColorVertexBuffer.IsInitialized(), StaticMesh->RenderData->LODVertexFactories.Num(), Resources.bHasColorVertexData);
		}
	});

	FlushRenderingCommands();

	StaticMesh->RenderData->LODVertexFactories[0].ReleaseResources();
	for (FStaticMeshLODResources& Resources : StaticMesh->RenderData->LODResources)
	{
		StaticMesh->RenderData->LODVertexFactories[0].InitVertexFactory(Resources, StaticMesh->RenderData->LODVertexFactories[0].VertexFactory, 0, StaticMesh, false);
	}*/

	/*StaticMesh->RenderData->LODVertexFactories[0].InitVertexFactory(Resources, StaticMesh->RenderData->LODVertexFactories[0].VertexFactory, 0, StaticMesh, false);


	StaticMesh->RenderData->ReleaseResources();
	StaticMesh->RenderData->InitResources(ERHIFeatureLevel::Num, StaticMesh);*/


	StaticMesh->RenderData = MakeUnique<FStaticMeshRenderData>();
	StaticMesh->RenderData->AllocateLODResources(MeshDescriptions.Num());
	UE_LOG(LogTemp, Error, TEXT("RenderData Initialized: %d"), StaticMesh->RenderData->IsInitialized());
	for (FStaticMeshLODResources& Resources : StaticMesh->RenderData->LODResources)
	{
		Resources.bHasColorVertexData = true;
		UE_LOG(LogTemp, Error, TEXT("[0] StaticMeshVertexBuffer: %d"), Resources.VertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords());

		FStaticMeshConstAttributes MeshDescriptionAttributes(MeshDescription->GetMeshDescription());

		// Fill vertex buffers

		int32 NumVertexInstances = MeshDescription->GetMeshDescription().VertexInstances().GetArraySize();
		int32 NumTriangles = MeshDescription->GetMeshDescription().Triangles().Num();

		TArray<FStaticMeshBuildVertex> StaticMeshBuildVertices;
		StaticMeshBuildVertices.SetNum(NumVertexInstances);

		TVertexAttributesConstRef<FVector> VertexPositions = MeshDescriptionAttributes.GetVertexPositions();
		TVertexInstanceAttributesConstRef<FVector> VertexInstanceNormals = MeshDescriptionAttributes.GetVertexInstanceNormals();
		TVertexInstanceAttributesConstRef<FVector> VertexInstanceTangents = MeshDescriptionAttributes.GetVertexInstanceTangents();
		TVertexInstanceAttributesConstRef<float> VertexInstanceBinormalSigns = MeshDescriptionAttributes.GetVertexInstanceBinormalSigns();
		TVertexInstanceAttributesConstRef<FVector4> VertexInstanceColors = MeshDescriptionAttributes.GetVertexInstanceColors();
		TVertexInstanceAttributesConstRef<FVector2D> VertexInstanceUVs = MeshDescriptionAttributes.GetVertexInstanceUVs();

		for (FVertexInstanceID VertexInstanceID : MeshDescription->GetMeshDescription().VertexInstances().GetElementIDs())
		{
			FStaticMeshBuildVertex& StaticMeshVertex = StaticMeshBuildVertices[VertexInstanceID.GetValue()];

			StaticMeshVertex.Position = VertexPositions[MeshDescription->GetMeshDescription().GetVertexInstanceVertex(VertexInstanceID)];
			StaticMeshVertex.TangentX = VertexInstanceTangents[VertexInstanceID];
			StaticMeshVertex.TangentY = FVector::CrossProduct(VertexInstanceNormals[VertexInstanceID], VertexInstanceTangents[VertexInstanceID]).GetSafeNormal() * VertexInstanceBinormalSigns[VertexInstanceID];
			StaticMeshVertex.TangentZ = VertexInstanceNormals[VertexInstanceID];

			for (int32 UVIndex = 0; UVIndex < VertexInstanceUVs.GetNumIndices(); ++UVIndex)
			{
				StaticMeshVertex.UVs[UVIndex] = VertexInstanceUVs.Get(VertexInstanceID, UVIndex);
			}
		}

		bool bHasVertexColors = false;
		if (VertexInstanceColors.IsValid())
		{
			for (FVertexInstanceID VertexInstanceID : MeshDescription->GetMeshDescription().VertexInstances().GetElementIDs())
			{
				FStaticMeshBuildVertex& StaticMeshVertex = StaticMeshBuildVertices[VertexInstanceID.GetValue()];

				FLinearColor Color(VertexInstanceColors[VertexInstanceID]);
				if (Color != FLinearColor::White)
				{
					bHasVertexColors = true;
				}

				StaticMeshVertex.Color = Color.ToFColor(true);
			}
		}

		Resources.VertexBuffers.PositionVertexBuffer.Init(StaticMeshBuildVertices);
		Resources.VertexBuffers.StaticMeshVertexBuffer.Init(StaticMeshBuildVertices, VertexInstanceUVs.GetNumIndices());

		FColorVertexBuffer& ColorVertexBuffer = Resources.VertexBuffers.ColorVertexBuffer;

		ColorVertexBuffer.Init(StaticMeshBuildVertices);

		// Fill index buffer and sections array

		int32 NumPolygonGroups = MeshDescription->GetMeshDescription().PolygonGroups().Num();

		TPolygonGroupAttributesConstRef<FName> MaterialSlotNames = MeshDescriptionAttributes.GetPolygonGroupMaterialSlotNames();

		TArray<uint32> IndexBuffer;
		IndexBuffer.SetNumZeroed(NumTriangles * 3);

		FStaticMeshLODResources::FStaticMeshSectionArray& Sections = Resources.Sections;

		int32 SectionIndex = 0;
		int32 IndexBufferIndex = 0;
		EIndexBufferStride::Type IndexBufferStride = EIndexBufferStride::Force16Bit;

		for (FPolygonGroupID PolygonGroupID : MeshDescription->GetMeshDescription().PolygonGroups().GetElementIDs())
		{
			// Skip empty polygon groups - we do not want to build empty sections
			if (MeshDescription->GetMeshDescription().GetNumPolygonGroupPolygons(PolygonGroupID) == 0)
			{
				continue;
			}

			FStaticMeshSection& Section = Sections.AddDefaulted_GetRef();
			Section.FirstIndex = IndexBufferIndex;

			int32 TriangleCount = 0;
			uint32 MinVertexIndex = TNumericLimits<uint32>::Max();
			uint32 MaxVertexIndex = TNumericLimits<uint32>::Min();

			for (FPolygonID PolygonID : MeshDescription->GetMeshDescription().GetPolygonGroupPolygons(PolygonGroupID))
			{
				for (FTriangleID TriangleID : MeshDescription->GetMeshDescription().GetPolygonTriangleIDs(PolygonID))
				{
					for (FVertexInstanceID TriangleVertexInstanceIDs : MeshDescription->GetMeshDescription().GetTriangleVertexInstances(TriangleID))
					{
						uint32 VertexIndex = static_cast<uint32>(TriangleVertexInstanceIDs.GetValue());
						MinVertexIndex = FMath::Min(MinVertexIndex, VertexIndex);
						MaxVertexIndex = FMath::Max(MaxVertexIndex, VertexIndex);
						IndexBuffer[IndexBufferIndex] = VertexIndex;
						IndexBufferIndex++;
					}

					TriangleCount++;
				}
			}

			Section.NumTriangles = TriangleCount;
			Section.MinVertexIndex = MinVertexIndex;
			Section.MaxVertexIndex = MaxVertexIndex;

			const int32 MaterialIndex = StaticMaterials.IndexOfByPredicate(
				[&MaterialSlotName = MaterialSlotNames[PolygonGroupID]](const FStaticMaterial& StaticMaterial) { return StaticMaterial.MaterialSlotName == MaterialSlotName; }
			);

			Section.MaterialIndex = MaterialIndex;
			Section.bEnableCollision = true;
			Section.bCastShadow = true;

			if (MaxVertexIndex > TNumericLimits<uint16>::Max())
			{
				IndexBufferStride = EIndexBufferStride::Force32Bit;
			}

			SectionIndex++;
		}
		check(IndexBufferIndex == NumTriangles * 3);

		Resources.IndexBuffer.SetIndices(IndexBuffer, IndexBufferStride);

		// Fill depth only index buffer

		TArray<uint32> DepthOnlyIndexBuffer(IndexBuffer);
		for (uint32& Index : DepthOnlyIndexBuffer)
		{
			// Compress all vertex instances into the same instance for each vertex
			Index = MeshDescription->GetMeshDescription().GetVertexVertexInstances(MeshDescription->GetMeshDescription().GetVertexInstanceVertex(FVertexInstanceID(Index)))[0].GetValue();
		}

		Resources.bHasDepthOnlyIndices = true;
		Resources.DepthOnlyIndexBuffer.SetIndices(DepthOnlyIndexBuffer, IndexBufferStride);
		Resources.DepthOnlyNumTriangles = NumTriangles;

		// Fill reversed index buffer
		TArray<uint32> ReversedIndexBuffer(IndexBuffer);
		for (int32 ReversedIndexBufferIndex = 0; ReversedIndexBufferIndex < IndexBuffer.Num(); ReversedIndexBufferIndex += 3)
		{
			Swap(ReversedIndexBuffer[ReversedIndexBufferIndex + 0], ReversedIndexBuffer[ReversedIndexBufferIndex + 2]);
		}

		Resources.AdditionalIndexBuffers = new FAdditionalStaticMeshIndexBuffers();
		Resources.bHasReversedIndices = true;
		Resources.AdditionalIndexBuffers->ReversedIndexBuffer.SetIndices(ReversedIndexBuffer, IndexBufferStride);

		// Fill reversed depth index buffer
		TArray<uint32> ReversedDepthOnlyIndexBuffer(DepthOnlyIndexBuffer);
		for (int32 ReversedIndexBufferIndex = 0; ReversedIndexBufferIndex < IndexBuffer.Num(); ReversedIndexBufferIndex += 3)
		{
			Swap(ReversedDepthOnlyIndexBuffer[ReversedIndexBufferIndex + 0], ReversedDepthOnlyIndexBuffer[ReversedIndexBufferIndex + 2]);
		}

		Resources.bHasReversedDepthOnlyIndices = true;
		Resources.AdditionalIndexBuffers->ReversedDepthOnlyIndexBuffer.SetIndices(ReversedIndexBuffer, IndexBufferStride);

		Resources.bHasAdjacencyInfo = false;
	}
	StaticMesh->InitResources();
	for (FStaticMeshLODResources& Resources : StaticMesh->RenderData->LODResources)
	{
		//Resources.bHasColorVertexData = true;
		UE_LOG(LogTemp, Error, TEXT("[1] StaticMeshVertexBuffer: %d"), Resources.VertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords());
	}

	if (!StaticMesh->BodySetup)
	{
		StaticMesh->CreateBodySetup();
	}

	StaticMesh->BodySetup->bMeshCollideAll = false;
	StaticMesh->BodySetup->CollisionTraceFlag = StaticMeshConfig.CollisionComplexity;

	StaticMesh->BodySetup->InvalidatePhysicsData();

#if 0 && !WITH_EDITOR
	if (StaticMesh->bAllowCPUAccess)
	{
		FStaticMeshLODResources& LOD = StaticMesh->RenderData->LODResources[0];
		ENQUEUE_RENDER_COMMAND(FixIndexBufferOnCPUCommand)(
			[&LOD, &CPUVertexInstancesIDs](FRHICommandListImmediate& RHICmdList)
		{
			LOD.IndexBuffer.ReleaseResource();
			LOD.IndexBuffer = FRawStaticIndexBuffer(true);
			LOD.IndexBuffer.SetIndices(CPUVertexInstancesIDs, EIndexBufferStride::AutoDetect);
			LOD.IndexBuffer.InitResource();
		});

		FlushRenderingCommands();
	}
#endif

	if (StaticMeshConfig.bBuildSimpleCollision)
	{
		FKBoxElem BoxElem;
		BoxElem.Center = StaticMesh->RenderData->Bounds.Origin;
		BoxElem.X = StaticMesh->RenderData->Bounds.BoxExtent.X * 2.0f;
		BoxElem.Y = StaticMesh->RenderData->Bounds.BoxExtent.Y * 2.0f;
		BoxElem.Z = StaticMesh->RenderData->Bounds.BoxExtent.Z * 2.0f;
		StaticMesh->BodySetup->AggGeom.BoxElems.Add(BoxElem);
	}

	for (const FBox& Box : StaticMeshConfig.BoxCollisions)
	{
		FKBoxElem BoxElem;
		BoxElem.Center = Box.GetCenter();
		FVector BoxSize = Box.GetSize();
		BoxElem.X = BoxSize.X;
		BoxElem.Y = BoxSize.Y;
		BoxElem.Z = BoxSize.Z;
		StaticMesh->BodySetup->AggGeom.BoxElems.Add(BoxElem);
	}

	for (const FVector4 Sphere : StaticMeshConfig.SphereCollisions)
	{
		FKSphereElem SphereElem;
		SphereElem.Center = Sphere;
		SphereElem.Radius = Sphere.W;
		StaticMesh->BodySetup->AggGeom.SphereElems.Add(SphereElem);
	}

	StaticMesh->BodySetup->CreatePhysicsMeshes();

	if (OnStaticMeshCreated.IsBound())
	{
		OnStaticMeshCreated.Broadcast(StaticMesh);
	}

	return StaticMesh;
}

bool FglTFRuntimeParser::LoadStaticMeshes(TArray<UStaticMesh*>& StaticMeshes, const FglTFRuntimeStaticMeshConfig& StaticMeshConfig)
{
	const TArray<TSharedPtr<FJsonValue>>* JsonMeshes;
	// no meshes ?
	if (!Root->TryGetArrayField("meshes", JsonMeshes))
	{
		return false;
	}

	for (int32 Index = 0; Index < JsonMeshes->Num(); Index++)
	{
		UStaticMesh* StaticMesh = LoadStaticMesh(Index, StaticMeshConfig);
		if (!StaticMesh)
		{
			return false;
		}
		StaticMeshes.Add(StaticMesh);
	}

	return true;
}

UStaticMesh* FglTFRuntimeParser::LoadStaticMesh(const int32 MeshIndex, const FglTFRuntimeStaticMeshConfig& StaticMeshConfig)
{

	TSharedPtr<FJsonObject> JsonMeshObject = GetJsonObjectFromRootIndex("meshes", MeshIndex);
	if (!JsonMeshObject)
	{
		return nullptr;
	}

	if (CanReadFromCache(StaticMeshConfig.CacheMode) && StaticMeshesCache.Contains(MeshIndex))
	{
		return StaticMeshesCache[MeshIndex];
	}

	UStaticMesh* StaticMesh = LoadStaticMesh_Internal(JsonMeshObject.ToSharedRef(), StaticMeshConfig);
	if (!StaticMesh)
	{
		return nullptr;
	}

	if (CanWriteToCache(StaticMeshConfig.CacheMode))
	{
		StaticMeshesCache.Add(MeshIndex, StaticMesh);
	}

	return StaticMesh;
}

UStaticMesh* FglTFRuntimeParser::LoadStaticMeshByName(const FString Name, const FglTFRuntimeStaticMeshConfig& StaticMeshConfig)
{
	const TArray<TSharedPtr<FJsonValue>>* JsonMeshes;
	if (!Root->TryGetArrayField("meshes", JsonMeshes))
	{
		return nullptr;
	}

	for (int32 MeshIndex = 0; MeshIndex < JsonMeshes->Num(); MeshIndex++)
	{
		TSharedPtr<FJsonObject> JsonMeshObject = (*JsonMeshes)[MeshIndex]->AsObject();
		if (!JsonMeshObject)
		{
			return nullptr;
		}
		FString MeshName;
		if (JsonMeshObject->TryGetStringField("name", MeshName))
		{
			if (MeshName == Name)
			{
				return LoadStaticMesh(MeshIndex, StaticMeshConfig);
			}
		}
	}

	return nullptr;
}
