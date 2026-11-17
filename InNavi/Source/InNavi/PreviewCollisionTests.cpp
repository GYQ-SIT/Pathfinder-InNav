#if WITH_DEV_AUTOMATION_TESTS

#include "InteriorRoom.h"
#include "ProceduralMeshComponent.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

namespace PreviewCollisionTests
{
	bool EqualSection(const FProcMeshSection* A, const FProcMeshSection* B)
	{
		if (!A || !B || A->ProcIndexBuffer != B->ProcIndexBuffer || A->ProcVertexBuffer.Num() != B->ProcVertexBuffer.Num()) return false;
		for (int32 Index = 0; Index < A->ProcVertexBuffer.Num(); ++Index)
		{
			const auto& X = A->ProcVertexBuffer[Index];
			const auto& Y = B->ProcVertexBuffer[Index];
			if (!X.Position.Equals(Y.Position) || !X.Normal.Equals(Y.Normal) || !X.UV0.Equals(Y.UV0) || !X.Tangent.TangentX.Equals(Y.Tangent.TangentX)) return false;
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPreviewCollisionGeometryTest, "InNavi.Building.SeeThrough.VisualOnlyCollision", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPreviewCollisionGeometryTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	auto* Editable = World->SpawnActor<AInteriorRoom>();
	auto* Preview = World->SpawnActor<AInteriorRoom>();
	const TArray<FVector2D> Outline = { {0,0}, {500,0}, {500,400}, {0,400} };
	Editable->BuildWalls(Outline, 1.f, FVector2D::ZeroVector, 0.f, 280.f, 20.f, {}, true);
	Preview->BuildWalls(Outline, 1.f, FVector2D::ZeroVector, 0.f, 280.f, 20.f, {}, true, false);
	auto* EditableMesh = Editable->FindComponentByClass<UProceduralMeshComponent>();
	auto* PreviewMesh = Preview->FindComponentByClass<UProceduralMeshComponent>();
	for (int32 Section = 0; Section < 2; ++Section)
	{
		TestTrue(TEXT("visual geometry is unchanged"), PreviewCollisionTests::EqualSection(EditableMesh->GetProcMeshSection(Section), PreviewMesh->GetProcMeshSection(Section)));
		TestFalse(TEXT("preview sections request no collision cooking"), PreviewMesh->GetProcMeshSection(Section)->bEnableCollision);
	}
	TestTrue(TEXT("editable rooms retain collision geometry"), EditableMesh->ContainsPhysicsTriMeshData(true));
	TestFalse(TEXT("preview rooms have no collision triangles"), PreviewMesh->ContainsPhysicsTriMeshData(true));
	Preview->SetFloorBaseZ(400.f);
	TestFalse(TEXT("subsequent rebuilds stay visual-only"), PreviewMesh->ContainsPhysicsTriMeshData(true));
	return true;
}

#endif
