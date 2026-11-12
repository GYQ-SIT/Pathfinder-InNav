#include "DesktopMemoryCaptureSubsystem.h"

#include "DesktopRenderingSubsystem.h"
#include "BuildingGenerator.h"
#include "NavPlayerController.h"
#include "PolyBuilding.h"
#include "RoadSegmentActor.h"
#include "ContextBuildingActor.h"
#include "InteriorRoom.h"
#include "ProceduralMeshComponent.h"
#include "EditScreenWidget.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMemory.h"
#include "HAL/PlatformProcess.h"
#include "HAL/FileManager.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UnrealClient.h"

void UDesktopMemoryCaptureSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
#if PLATFORM_WINDOWS
	bEnabled = FApp::IsUnattended() && FParse::Value(FCommandLine::Get(), TEXT("DesktopMemoryOutput="), Output);
#endif
	if (!bEnabled) return;
	if (auto* FrameLimit = IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS"))) FrameLimit->SetWithCurrentPriority(30.0f);
	FParse::Value(FCommandLine::Get(), TEXT("DesktopMemoryProject="), Project);
	FParse::Value(FCommandLine::Get(), TEXT("DesktopMemoryBuilding="), BuildingId);
	FParse::Value(FCommandLine::Get(), TEXT("DesktopHistoryStorage="), HistoryMode);
	FParse::Value(FCommandLine::Get(), TEXT("DesktopHistorySnapshots="), HistoryCount);
	bScreenshots = FParse::Param(FCommandLine::Get(), TEXT("DesktopMemoryScreenshots"));
	Data = MakeShared<FJsonObject>();
	NextAction = FPlatformTime::Seconds() + 3.0;
}

void UDesktopMemoryCaptureSubsystem::Tick(float DeltaTime)
{
	const double Now = FPlatformTime::Seconds();
	if (bStarted && LastFrameTime > 0) FrameMilliseconds.Add((Now - LastFrameTime) * 1000.0);
	LastFrameTime = Now;
	if (Now < NextAction) return;
	if (!bStarted)
	{
		if (!GetWorld() || !GetWorld()->GetFirstPlayerController()) return;
		Controller = Cast<ANavPlayerController>(GetWorld()->GetFirstPlayerController());
		for (TActorIterator<ABuildingGenerator> It(GetWorld()); It; ++It) { Generator = *It; break; }
		UGameViewportClient* Viewport = GetGameInstance()->GetGameViewportClient();
		if (!Controller.IsValid() || !Generator.IsValid() || !Viewport || !Viewport->Viewport
			|| FParse::Param(FCommandLine::Get(), TEXT("nullrhi"))) { Finish(TEXT("Rendered desktop world/viewport unavailable.")); return; }
		if (!HistoryMode.IsEmpty() && (HistoryMode != TEXT("Plain") && HistoryMode != TEXT("Compressed"))) { Finish(TEXT("Invalid history storage mode.")); return; }
		if (HistoryCount < 1 || HistoryCount > 2000) { Finish(TEXT("History snapshot count out of range.")); return; }
		Data->SetStringField(TEXT("started_at_utc"), FDateTime::UtcNow().ToIso8601());
		Data->SetStringField(TEXT("engine_version"), FEngineVersion::Current().ToString());
#if UE_BUILD_SHIPPING
		Data->SetStringField(TEXT("configuration"), TEXT("Shipping"));
#else
		Data->SetStringField(TEXT("configuration"), TEXT("Development"));
#endif
		Data->SetBoolField(TEXT("editor_executable"), WITH_EDITOR != 0);
		Data->SetStringField(TEXT("cpu"), FPlatformMisc::GetCPUBrand().TrimStartAndEnd());
		Data->SetNumberField(TEXT("visible_ram_bytes"), FPlatformMemory::GetConstants().TotalPhysical);
		Data->SetNumberField(TEXT("pid"), FPlatformProcess::GetCurrentProcessId());
		const FIntPoint Size = Viewport->Viewport->GetSizeXY();
		Data->SetNumberField(TEXT("viewport_width"), Size.X);
		Data->SetNumberField(TEXT("viewport_height"), Size.Y);
		auto* Rendering = GetGameInstance()->GetSubsystem<UDesktopRenderingSubsystem>();
		Data->SetStringField(TEXT("render_preset"), Rendering && Rendering->IsLightweight() ? TEXT("Lightweight") : TEXT("Standard"));
		auto Variables = MakeShared<FJsonObject>();
		for (const TCHAR* Name : { TEXT("r.DynamicGlobalIlluminationMethod"), TEXT("r.ReflectionMethod"), TEXT("r.Shadow.Virtual.Enable"), TEXT("r.RayTracing.Enable"), TEXT("r.CustomDepth") })
		{
			if (auto* Variable = IConsoleManager::Get().FindConsoleVariable(Name)) Variables->SetNumberField(Name, Variable->GetInt());
		}
		Data->SetObjectField(TEXT("render_variables"), Variables);
		Data->SetStringField(TEXT("project_file"), Project);
		Data->SetStringField(TEXT("building_id"), BuildingId);
		Data->SetStringField(TEXT("history_storage"), HistoryMode);
		Data->SetNumberField(TEXT("settle_seconds"), 5);
		Data->SetNumberField(TEXT("samples_per_phase"), 10);
		Data->SetNumberField(TEXT("requested_interval_seconds"), 0.5);
		Data->SetNumberField(TEXT("frame_rate_cap"), 30);
		Data->SetStringField(TEXT("frame_time_source"), TEXT("monotonic_tick_interval"));
		Data->SetBoolField(TEXT("scenery_batched"), false);
		Data->SetBoolField(TEXT("legacy_preview_collision"), FParse::Param(FCommandLine::Get(), TEXT("LegacyPreviewCollision")));
		if (!Project.IsEmpty())
		{
			TArray<UUserWidget*> Widgets;
			UWidgetBlueprintLibrary::GetAllWidgetsOfClass(Controller.Get(), Widgets, UUserWidget::StaticClass(), true);
			for (auto* Widget : Widgets) Widget->RemoveFromParent();
			auto* Screen = CreateWidget<UEditScreenWidget>(Controller.Get(), UEditScreenWidget::StaticClass());
			if (!Screen) { Finish(TEXT("Cannot create the exterior screen.")); return; }
			Screen->AddToViewport();
			Controller->SetCampusSeeThroughEnabled(false);
			Controller->StartNewProject();
			CollectGarbage(RF_NoFlags, true);
		}
		bStarted = true;
		NextAction = FPlatformTime::Seconds() + 5.0;
		return;
	}
	if (!Controller.IsValid() || !Generator.IsValid()) { Finish(TEXT("The captured world was closed.")); return; }
	if (bAdvance)
	{
		if (!Samples.IsEmpty() && !FrameMilliseconds.IsEmpty())
		{
			auto Frames = MakeShared<FJsonObject>();
			Frames->SetStringField(TEXT("phase"), Samples.Last()->AsObject()->GetStringField(TEXT("phase")));
			FrameMilliseconds.Sort();
			double Total = 0;
			for (double Milliseconds : FrameMilliseconds) Total += Milliseconds;
			Frames->SetNumberField(TEXT("count"), FrameMilliseconds.Num());
			Frames->SetNumberField(TEXT("mean_ms"), Total / FrameMilliseconds.Num());
			Frames->SetNumberField(TEXT("p95_ms"), FrameMilliseconds[FMath::Clamp(FMath::CeilToInt(FrameMilliseconds.Num() * 0.95) - 1, 0, FrameMilliseconds.Num() - 1)]);
			Frames->SetNumberField(TEXT("max_ms"), FrameMilliseconds.Last());
			FrameSummaries.Add(MakeShared<FJsonValueObject>(Frames));
			FrameMilliseconds.Reset();
		}
		bAdvance = false;
		if (Project.IsEmpty() || ++Phase == (HistoryMode.IsEmpty() ? 4 : 2)) { Finish(); return; }
		if (Phase == 1)
		{
			const double LoadStart = FPlatformTime::Seconds();
			if (Controller->LoadBuildingDataFromPathOrDatabase(Project) == 0 || !Generator->FindBuildingById(BuildingId)) { Finish(TEXT("Cannot load benchmark project/building.")); return; }
			Data->SetNumberField(TEXT("project_load_ms"), (FPlatformTime::Seconds() - LoadStart) * 1000.0);
			int32 Roads = 0, ContextBuildings = 0;
			for (TActorIterator<ARoadSegmentActor> It(GetWorld()); It; ++It) ++Roads;
			for (TActorIterator<AContextBuildingActor> It(GetWorld()); It; ++It) ++ContextBuildings;
			Data->SetNumberField(TEXT("road_features"), Roads);
			Data->SetNumberField(TEXT("context_features"), ContextBuildings);
			Data->SetNumberField(TEXT("scenery_batch_actors"), 0);
			Data->SetNumberField(TEXT("editable_buildings"), Generator->GetAllBuildings().Num());
			if (FParse::Param(FCommandLine::Get(), TEXT("DesktopMemorySelectBuilding"))) Controller->SelectBuildingById(BuildingId);
			if (!HistoryMode.IsEmpty() && !PrepareHistory()) { Finish(TEXT("Cannot prepare the history workload.")); return; }
		}
		else if (Phase == 2)
		{
			Controller->SetBuildingRoomFloor(BuildingId, 1);
			Controller->SetBuildingMadeTransparent(BuildingId, true);
		}
		else Controller->SetBuildingRoomFloor(BuildingId, 0);
		NextAction = FPlatformTime::Seconds() + 5.0;
		return;
	}
	const TCHAR* PhaseName = Project.IsEmpty() ? TEXT("startup_screen") : !HistoryMode.IsEmpty()
		? (Phase == 0 ? TEXT("empty_baseline") : TEXT("history_populated"))
		: Phase == 0 ? TEXT("empty_baseline") : Phase == 1 ? TEXT("campus_opaque") : Phase == 2 ? TEXT("selected_floor_1") : TEXT("selected_building_all_floors");
	auto Sample = MakeShared<FJsonObject>();
	const FPlatformMemoryStats Stats = FPlatformMemory::GetStats();
	Sample->SetStringField(TEXT("phase"), PhaseName);
	Sample->SetStringField(TEXT("measured_at_utc"), FDateTime::UtcNow().ToIso8601());
	Sample->SetNumberField(TEXT("resident_bytes"), Stats.UsedPhysical);
	Sample->SetNumberField(TEXT("private_committed_bytes"), Stats.UsedVirtual);
	int32 Cached = 0, Shown = 0;
	for (auto* Building : Generator->GetAllBuildings())
	{
		Cached += Controller->CountCachedCampusRoomsForTests(Building->BuildingId);
		Shown += Controller->CountShownCampusRoomsForTests(Building->BuildingId);
	}
	Sample->SetNumberField(TEXT("cached_preview_rooms"), Cached);
	Sample->SetNumberField(TEXT("shown_preview_rooms"), Shown);
	int32 CollisionSections = 0;
	for (TActorIterator<AInteriorRoom> It(GetWorld()); It; ++It)
	{
		if (auto* Mesh = It->FindComponentByClass<UProceduralMeshComponent>())
		{
			for (int32 Index = 0; Index < Mesh->GetNumSections(); ++Index)
			{
				if (Mesh->GetProcMeshSection(Index)->bEnableCollision) ++CollisionSections;
			}
		}
	}
	Sample->SetNumberField(TEXT("preview_collision_sections"), CollisionSections);
	Sample->SetBoolField(TEXT("world_rendering_disabled"), GetGameInstance()->GetGameViewportClient()->bDisableWorldRendering);
	Samples.Add(MakeShared<FJsonValueObject>(Sample));
	NextAction = Now + 0.5;
	if (++PhaseSamples == 10)
	{
		PhaseSamples = 0;
		bAdvance = true;
		if (bScreenshots) FScreenshotRequest::RequestScreenshot(FPaths::ChangeExtension(Output, TEXT("")) + PhaseName + TEXT(".png"), true, false);
		NextAction = Now + (bScreenshots ? 1.0 : 0.5);
	}
}

bool UDesktopMemoryCaptureSubsystem::PrepareHistory()
{
	const auto Interior = Generator->GetInteriorDataForBuilding(BuildingId);
	if (!Interior) return false;
	const auto& Floors = Interior->GetArrayField(TEXT("floors"));
	if (Floors.IsEmpty()) return false;
	const auto& Rooms = Floors[0]->AsObject()->GetArrayField(TEXT("rooms"));
	if (Rooms.IsEmpty()) return false;
	uint64 Logical = 0, Stored = 0;
	for (int32 Index = 0; Index < HistoryCount; ++Index)
	{
		Rooms[0]->AsObject()->SetStringField(TEXT("name"), FString::Printf(TEXT("History edit %d"), Index));
		FString Json;
		if (!FJsonSerializer::Serialize(Interior.ToSharedRef(), TJsonWriterFactory<>::Create(&Json))) return false;
		Logical += (static_cast<uint64>(Json.Len()) + 1) * sizeof(TCHAR);
		if (HistoryMode == TEXT("Plain"))
		{
			PlainHistory.Add(MoveTemp(Json));
			Stored += PlainHistory.Last().GetAllocatedSize();
		}
		else
		{
			CompressedHistory.AddDefaulted_GetRef().Set(MoveTemp(Json));
			Stored += CompressedHistory.Last().GetStoredBytes();
		}
	}
	Data->SetNumberField(TEXT("history_snapshots"), HistoryCount);
	Data->SetNumberField(TEXT("history_logical_bytes"), Logical);
	Data->SetNumberField(TEXT("history_stored_bytes"), Stored);
	return true;
}

void UDesktopMemoryCaptureSubsystem::Finish(const FString& Error)
{
	bEnabled = false;
	Data->SetNumberField(TEXT("schema_version"), 1);
	Data->SetArrayField(TEXT("samples"), Samples);
	Data->SetArrayField(TEXT("frame_summaries"), FrameSummaries);
	Data->SetStringField(TEXT("error"), Error);
	FString Json;
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Output), true);
	const bool bSaved = FJsonSerializer::Serialize(Data.ToSharedRef(), TJsonWriterFactory<>::Create(&Json)) && FFileHelper::SaveStringToFile(Json, *Output);
	FPlatformMisc::RequestExitWithStatus(false, Error.IsEmpty() && bSaved ? 0 : 1);
}
