#include "LayoutValidation.h"

#include "CompGeom/PolygonTriangulation.h"
#include "Containers/StaticArray.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/FileHelper.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace LayoutValidation
{
    const TCHAR* SeverityName(ESeverity Severity)
    {
        return Severity == ESeverity::Error ? TEXT("Error") : Severity == ESeverity::Warning ? TEXT("Warning") : TEXT("Info");
    }

    FString FIssue::Key() const { return Rule + TEXT(":") + JsonPath + TEXT(":") + BuildingId + TEXT(":") + FString::FromInt(Floor) + TEXT(":") + ObjectId; }
    int32 FReport::Count(ESeverity Severity) const
    {
        int32 Result = 0;
        for (const auto& Issue : Issues) Result += Issue.Severity == Severity ? 1 : 0;
        return Result;
    }
    void FReport::Sort()
    {
        Issues.Sort([](const FIssue& A, const FIssue& B)
        {
            if (A.Severity != B.Severity) return A.Severity < B.Severity;
            if (A.BuildingId != B.BuildingId) return A.BuildingId < B.BuildingId;
            if (A.Floor != B.Floor) return A.Floor < B.Floor;
            return A.Key() < B.Key();
        });
    }
    void FReport::FilterToBuilding(const FString& BuildingId)
    {
        ScopeBuildingId = BuildingId;
        if (!BuildingId.IsEmpty()) Issues.RemoveAll([&](const FIssue& Issue) { return !Issue.BuildingId.IsEmpty() && Issue.BuildingId != BuildingId; });
    }
    TSharedRef<FJsonObject> FReport::ToJson() const
    {
        auto Root = MakeShared<FJsonObject>();
        Root->SetNumberField(TEXT("reportVersion"), 1);
        Root->SetStringField(TEXT("source"), Source);
        Root->SetStringField(TEXT("fingerprint"), Fingerprint);
        Root->SetStringField(TEXT("scopeBuildingId"), ScopeBuildingId);
        Root->SetBoolField(TEXT("navigationChecked"), bNavigationChecked);
        Root->SetNumberField(TEXT("roomsChecked"), RoomsChecked);
        Root->SetNumberField(TEXT("roomsSkipped"), RoomsSkipped);
        TArray<TSharedPtr<FJsonValue>> Values;
        for (const auto& Issue : Issues)
        {
            auto Item = MakeShared<FJsonObject>();
            Item->SetStringField(TEXT("key"), Issue.Key());
            Item->SetStringField(TEXT("rule"), Issue.Rule);
            Item->SetStringField(TEXT("severity"), SeverityName(Issue.Severity));
            Item->SetNumberField(TEXT("objectKind"), static_cast<uint8>(Issue.Kind));
            Item->SetStringField(TEXT("buildingId"), Issue.BuildingId);
            Item->SetNumberField(TEXT("floor"), Issue.Floor);
            Item->SetStringField(TEXT("objectId"), Issue.ObjectId);
            Item->SetStringField(TEXT("ownerId"), Issue.OwnerId);
            Item->SetStringField(TEXT("jsonPath"), Issue.JsonPath);
            Item->SetStringField(TEXT("message"), Issue.Message);
            Item->SetStringField(TEXT("suggestion"), Issue.Suggestion);
            if (Issue.bHasPosition) Item->SetArrayField(TEXT("position"), { MakeShared<FJsonValueNumber>(Issue.Position.X), MakeShared<FJsonValueNumber>(Issue.Position.Y) });
            Values.Add(MakeShared<FJsonValueObject>(Item));
        }
        Root->SetArrayField(TEXT("issues"), Values);
        return Root;
    }
    FString Fingerprint(const TSharedPtr<FJsonObject>& Project)
    {
        FString Text;
        if (Project.IsValid()) FJsonSerializer::Serialize(Project.ToSharedRef(), TJsonWriterFactory<>::Create(&Text));
        const FTCHARToUTF8 Bytes(*Text);
        return FMD5::HashBytes(reinterpret_cast<const uint8*>(Bytes.Get()), Bytes.Length());
    }
    bool ExportReport(const FReport& Report, const FString& Path)
    {
        FString Text;
        return FJsonSerializer::Serialize(Report.ToJson(), TJsonWriterFactory<>::Create(&Text)) && FFileHelper::SaveStringToFile(Text, *Path);
    }

    namespace Private
    {
        constexpr double Epsilon = 0.0001;
        double Cross(FVector2D A, FVector2D B, FVector2D C) { return (B.X - A.X) * (C.Y - A.Y) - (B.Y - A.Y) * (C.X - A.X); }
        bool On(FVector2D P, FVector2D A, FVector2D B)
        {
            return FMath::Abs(Cross(A, B, P)) <= Epsilon && P.X >= FMath::Min(A.X, B.X) - Epsilon && P.X <= FMath::Max(A.X, B.X) + Epsilon
                && P.Y >= FMath::Min(A.Y, B.Y) - Epsilon && P.Y <= FMath::Max(A.Y, B.Y) + Epsilon;
        }
        bool Crosses(FVector2D A, FVector2D B, FVector2D C, FVector2D D)
        {
            return ((Cross(A, B, C) > Epsilon && Cross(A, B, D) < -Epsilon) || (Cross(A, B, C) < -Epsilon && Cross(A, B, D) > Epsilon))
                && ((Cross(C, D, A) > Epsilon && Cross(C, D, B) < -Epsilon) || (Cross(C, D, A) < -Epsilon && Cross(C, D, B) > Epsilon));
        }
        bool Touches(FVector2D A, FVector2D B, FVector2D C, FVector2D D) { return Crosses(A, B, C, D) || On(A, C, D) || On(B, C, D) || On(C, A, B) || On(D, A, B); }
        bool Inside(FVector2D P, const TArray<FVector2D>& Polygon)
        {
            bool bInside = false;
            for (int32 I = 0, J = Polygon.Num() - 1; I < Polygon.Num(); J = I++)
            {
                const auto A = Polygon[J], B = Polygon[I];
                if (On(P, A, B)) return true;
                if ((A.Y > P.Y) != (B.Y > P.Y) && P.X < (B.X - A.X) * (P.Y - A.Y) / (B.Y - A.Y) + A.X) bInside = !bInside;
            }
            return bInside;
        }

        struct FContext
        {
            FString Building, Id, Owner, Path = TEXT("$");
            int32 Floor = INDEX_NONE;
            EObjectKind Kind = EObjectKind::Project;
        };
        struct FNode
        {
            FContext Context;
            FString Type, Group;
            FVector2D Position;
            double Rotation = 0, Size = 1;
            TArray<int32> Floors;
        };
        struct FReference { FContext Context; FString Input, Target; };
        struct FRoomShape
        {
            FContext Context;
            FBox2D Bounds{ForceInit};
            TArray<TStaticArray<FVector2D, 3>> Triangles;

            FRoomShape(const FContext& InContext, const TArray<FVector2D>& Points) : Context(InContext)
            {
                for (const FVector2D& Point : Points) Bounds += Point;
                TArray<UE::Geometry::FIndex3i> Indices;
                PolygonTriangulation::TriangulateSimplePolygon(Points, Indices);
                for (const auto& Triangle : Indices)
                {
                    const FVector2D A = Points[Triangle.A], B = Points[Triangle.B], C = Points[Triangle.C];
                    const double Area = Cross(A, B, C);
                    if (FMath::Abs(Area) > Epsilon)
                    {
                        Triangles.Add(Area > 0 ? TStaticArray<FVector2D, 3>{A, B, C} : TStaticArray<FVector2D, 3>{A, C, B});
                    }
                }
            }
        };

        bool RoomAreasOverlap(const FRoomShape& A, const FRoomShape& B)
        {
            if (!A.Bounds.Intersect(B.Bounds)) return false;
            // A separating edge also rules out triangles that only share an edge or vertex.
            auto HasSeparatingEdge = [](const auto& One, const auto& Other)
            {
                for (int32 I = 0; I < 3; ++I)
                {
                    const FVector2D From = One[I], To = One[(I + 1) % 3];
                    if (FMath::Max3(Cross(From, To, Other[0]), Cross(From, To, Other[1]), Cross(From, To, Other[2])) <= Epsilon)
                        return true;
                }
                return false;
            };
            for (const auto& First : A.Triangles) for (const auto& Second : B.Triangles)
            {
                if (!HasSeparatingEdge(First, Second) && !HasSeparatingEdge(Second, First)) return true;
            }
            return false;
        }

        struct FReader
        {
            explicit FReader(TFunctionRef<bool()> InShouldCancel) : ShouldCancel(InShouldCancel) {}
            TFunctionRef<bool()> ShouldCancel;
            FReport Report;
            FContext C;

            void Issue(const TCHAR* Rule, const FString& Message, const FString& Suggestion, ESeverity Severity = ESeverity::Error)
            {
                FIssue Item;
                Item.Rule = Rule; Item.Message = Message; Item.Suggestion = Suggestion; Item.Severity = Severity;
                Item.BuildingId = C.Building; Item.Floor = C.Floor; Item.Kind = C.Kind;
                Item.ObjectId = C.Id; Item.OwnerId = C.Owner; Item.JsonPath = C.Path;
                Report.Issues.Add(MoveTemp(Item));
            }
            void Invalid(const FString& Field) { Issue(TEXT("DATA_TYPE"), TEXT("Missing or invalid field: ") + Field, TEXT("Correct the field's type or value in the project data.")); }
            TSharedPtr<FJsonObject> Object(const TSharedPtr<FJsonValue>& Value)
            {
                if (!Value.IsValid() || Value->Type != EJson::Object) { Invalid(C.Path); return nullptr; }
                return Value->AsObject();
            }
            TArray<TSharedPtr<FJsonValue>> Array(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, bool bRequired = false)
            {
                const auto Value = Obj->Values.Find(Key);
                if (!Value) { if (bRequired) Invalid(Key); return {}; }
                if (!Value->IsValid() || (*Value)->Type != EJson::Array) { Invalid(Key); return {}; }
                return (*Value)->AsArray();
            }
            FString String(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, bool bRequired = false)
            {
                const auto Value = Obj->Values.Find(Key);
                if (!Value) { if (bRequired) Invalid(Key); return {}; }
                if (!Value->IsValid() || (*Value)->Type != EJson::String) { Invalid(Key); return {}; }
                return (*Value)->AsString();
            }
            double Number(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, double Default, bool bRequired = false)
            {
                const auto Value = Obj->Values.Find(Key);
                if (!Value) { if (bRequired) Invalid(Key); return Default; }
                if (!Value->IsValid() || (*Value)->Type != EJson::Number || !FMath::IsFinite((*Value)->AsNumber())) { Invalid(Key); return Default; }
                return (*Value)->AsNumber();
            }
            int32 Integer(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, int32 Default, bool bRequired = false)
            {
                const double N = Number(Obj, Key, Default, bRequired);
                if (N < MIN_int32 || N > MAX_int32 || FMath::FloorToDouble(N) != N) { Invalid(Key); return Default; }
                return static_cast<int32>(N);
            }
            bool Boolean(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, bool Default)
            {
                const auto Value = Obj->Values.Find(Key);
                if (!Value) return Default;
                if (!Value->IsValid() || (*Value)->Type != EJson::Boolean) { Invalid(Key); return Default; }
                return (*Value)->AsBool();
            }
            FVector2D XY(const TSharedPtr<FJsonValue>& Value)
            {
                if (Value.IsValid() && Value->Type == EJson::Array)
                {
                    const auto& Pair = Value->AsArray();
                    if (Pair.Num() >= 2 && Pair[0].IsValid() && Pair[1].IsValid() && Pair[0]->Type == EJson::Number && Pair[1]->Type == EJson::Number
                        && FMath::IsFinite(Pair[0]->AsNumber()) && FMath::IsFinite(Pair[1]->AsNumber())) return FVector2D(Pair[0]->AsNumber(), Pair[1]->AsNumber());
                }
                Invalid(TEXT("coordinate pair")); return FVector2D::ZeroVector;
            }
            FString Id(const TSharedPtr<FJsonObject>& Obj, TSet<FString>& Ids)
            {
                C.Id = String(Obj, TEXT("id"), true);
                if (C.Id.TrimStartAndEnd().IsEmpty()) Issue(TEXT("ID_EMPTY"), TEXT("An object has an empty ID."), TEXT("Assign a unique non-empty ID."));
                else if (Ids.Contains(C.Id)) Issue(TEXT("ID_DUPLICATE"), TEXT("Duplicate ID: ") + C.Id, TEXT("Assign a unique ID and update references to it."));
                Ids.Add(C.Id); return C.Id;
            }
            TArray<FVector2D> Shape(const TSharedPtr<FJsonObject>& Obj, bool bClosed)
            {
                TArray<FVector2D> Points;
                for (const auto& Value : Array(Obj, TEXT("footprint"), true)) Points.Add(XY(Value));
                const int32 Min = bClosed ? 3 : 2;
                if (Points.Num() < Min) { Issue(TEXT("GEOMETRY_POINTS"), TEXT("The footprint has too few points."), bClosed ? TEXT("Provide at least three points for a closed outline.") : TEXT("Provide at least two points for a wall path.")); return Points; }
                const int32 Edges = bClosed ? Points.Num() : Points.Num() - 1;
                double Area = 0;
                for (int32 I = 0; I < Edges; ++I)
                {
                    if (ShouldCancel()) return {};
                    const auto A = Points[I], B = Points[(I + 1) % Points.Num()];
                    if (A.Equals(B, Epsilon)) Issue(TEXT("GEOMETRY_ZERO_EDGE"), TEXT("The footprint contains a zero-length edge."), TEXT("Remove repeated adjacent points."));
                    Area += A.X * B.Y - B.X * A.Y;
                    for (int32 J = I + 2; J < Edges; ++J)
                    {
                        if (bClosed && I == 0 && J == Edges - 1) continue;
                        if (Touches(A, B, Points[J], Points[(J + 1) % Points.Num()]))
                        { Issue(TEXT("GEOMETRY_SELF_INTERSECTION"), TEXT("Non-adjacent footprint edges intersect."), TEXT("Redraw the intersecting edges."), bClosed ? ESeverity::Error : ESeverity::Warning); break; }
                    }
                }
                if (bClosed && FMath::Abs(Area) <= Epsilon) Issue(TEXT("GEOMETRY_ZERO_AREA"), TEXT("The closed footprint has no area."), TEXT("Draw a usable closed outline."));
                return Points;
            }
            void Containment(const TArray<FVector2D>& Points, bool bClosed, const TArray<FVector2D>& Boundary)
            {
                bool bFits = true;
                for (const auto& P : Points) bFits &= Inside(P, Boundary);
                const int32 Edges = bClosed ? Points.Num() : FMath::Max(0, Points.Num() - 1);
                for (int32 I = 0; I < Edges; ++I)
                {
                    if (ShouldCancel()) return;
                    const auto A = Points[I], B = Points[(I + 1) % Points.Num()];
                    bFits &= Inside((A + B) * 0.5, Boundary);
                    for (int32 J = 0; J < Boundary.Num(); ++J) bFits &= !Crosses(A, B, Boundary[J], Boundary[(J + 1) % Boundary.Num()]);
                }
                if (!bFits) Issue(TEXT("GEOMETRY_OUTSIDE"), TEXT("The shape extends outside its building."), TEXT("Move or reshape it to fit inside the building outline."));
            }
            void Opening(const TSharedPtr<FJsonObject>& Obj, const TArray<FVector2D>& Points, bool bClosed, double Scale, TSet<FString>& Ids, TArray<FReference>& References)
            {
                Id(Obj, Ids);
                const FString Type = String(Obj, TEXT("type")).ToLower();
                if (!Type.IsEmpty() && Type != TEXT("door") && Type != TEXT("window") && Type != TEXT("void")) Invalid(TEXT("opening type"));
                const int32 Edge = Integer(Obj, TEXT("edgeIndex"), 0);
                const double Offset = Number(Obj, TEXT("offset"), 0), Width = Number(Obj, TEXT("width"), 0, true);
                const int32 Edges = bClosed ? Points.Num() : FMath::Max(0, Points.Num() - 1);
                if (Edge < 0 || Edge >= Edges || Width <= 0 || Offset < 0 || (Edges > 0 && Edge >= 0 && Edge < Edges && Offset + Width > FVector2D::Distance(Points[Edge], Points[(Edge + 1) % Points.Num()]) * Scale + Epsilon))
                    Issue(TEXT("OPENING_BOUNDS"), TEXT("The opening does not fit on its wall edge."), TEXT("Choose an existing edge and a positive width and offset that fit along it."));
                if (Type == TEXT("window"))
                {
                    if (Number(Obj, TEXT("height"), 120) <= 0 || Number(Obj, TEXT("sillHeight"), 0) < 0) Invalid(TEXT("window dimensions"));
                }
                if (Type.IsEmpty() || Type == TEXT("door")) References.Add({ C, String(Obj, TEXT("inputId")), String(Obj, TEXT("connectsTo")) });
            }
            void Interior(const TSharedPtr<FJsonObject>& Obj, const TArray<FVector2D>& Boundary, int32 FloorCount, double Scale)
            {
                const auto Base = C;
                TSet<int32> FloorIds;
                TSet<FString> RoomIds, NavigationIds, NodeIds, EntranceIds;
                TArray<FNode> Nodes;
                TArray<FReference> References;
                TArray<FRoomShape> Shapes;
                const auto Floors = Array(Obj, TEXT("floors"));
                if (Floors.IsEmpty()) Issue(TEXT("INTERIOR_UNAUTHORED"), TEXT("This building has no authored floors."), TEXT("Author its interior before checking room reachability."), ESeverity::Info);
                for (int32 I = 0; I < Floors.Num(); ++I)
                {
                    if (ShouldCancel()) return;
                    C = Base; C.Kind = EObjectKind::Floor; C.Path = Base.Path + FString::Printf(TEXT(".floors[%d]"), I);
                    const auto Floor = Object(Floors[I]); if (!Floor) continue;
                    C.Floor = Integer(Floor, TEXT("level"), 1);
                    if (C.Floor < 1 || C.Floor > FloorCount) Issue(TEXT("FLOOR_RANGE"), TEXT("Floor level is outside this building's floor count."), TEXT("Correct the floor level or building floor count."));
                    if (FloorIds.Contains(C.Floor)) Issue(TEXT("FLOOR_DUPLICATE"), TEXT("The same floor level is defined twice."), TEXT("Keep a single definition of each floor."));
                    FloorIds.Add(C.Floor);
                    if (Number(Floor, TEXT("wallHeight"), 280) <= 0 || Number(Floor, TEXT("wallThickness"), 20) < 0) Invalid(TEXT("wall dimensions"));
                    const auto FloorContext = C;
                    const auto Rooms = Array(Floor, TEXT("rooms"));
                    for (int32 R = 0; R < Rooms.Num(); ++R)
                    {
                        if (ShouldCancel()) return;
                        C = FloorContext; C.Kind = EObjectKind::Room; C.Path += FString::Printf(TEXT(".rooms[%d]"), R);
                        const auto Room = Object(Rooms[R]); if (!Room) continue;
                        Id(Room, RoomIds); String(Room, TEXT("name")); String(Room, TEXT("roomType")); String(Room, TEXT("access")); Boolean(Room, TEXT("requireKey"), false);
                        const FString NavigationId = String(Room, TEXT("navigationId"));
                        if (!NavigationId.IsEmpty())
                        {
                            if (NavigationIds.Contains(NavigationId))
                                Issue(TEXT("NAVIGATION_ID_DUPLICATE"), TEXT("Multiple rooms share the same mobile place ID."), TEXT("Assign a unique navigationId to each room."), ESeverity::Warning);
                            NavigationIds.Add(NavigationId);
                        }
                        const bool bClosed = Boolean(Room, TEXT("closed"), true);
                        const int32 IssuesBeforeShape = Report.Issues.Num();
                        const auto Points = Shape(Room, bClosed);
                        if (ShouldCancel()) return;
                        if (bClosed && Report.Issues.Num() == IssuesBeforeShape) Shapes.Emplace(C, Points);
                        Containment(Points, bClosed, Boundary);
                        const auto RoomContext = C;
                        TSet<FString> OpeningIds;
                        const auto Openings = Array(Room, TEXT("openings"));
                        for (int32 O = 0; O < Openings.Num(); ++O)
                        {
                            C = RoomContext; C.Kind = EObjectKind::Opening; C.Owner = RoomContext.Id; C.Path += FString::Printf(TEXT(".openings[%d]"), O);
                            if (const auto Item = Object(Openings[O])) Opening(Item, Points, bClosed, Scale, OpeningIds, References);
                        }
                        // Overlaps are advisory: the authoring tools allow intentional overlap.
                        for (int32 O = 0; O < Openings.Num(); ++O) for (int32 P = O + 1; P < Openings.Num(); ++P)
                        {
                            if (ShouldCancel()) return;
                            if (!Openings[O].IsValid() || !Openings[P].IsValid() || Openings[O]->Type != EJson::Object || Openings[P]->Type != EJson::Object) continue;
                            const auto A = Openings[O]->AsObject(), B = Openings[P]->AsObject();
                            int32 EA = 0, EB = 0; double OA = 0, OB = 0, WA = 0, WB = 0;
                            A->TryGetNumberField(TEXT("edgeIndex"), EA); B->TryGetNumberField(TEXT("edgeIndex"), EB);
                            A->TryGetNumberField(TEXT("offset"), OA); B->TryGetNumberField(TEXT("offset"), OB);
                            A->TryGetNumberField(TEXT("width"), WA); B->TryGetNumberField(TEXT("width"), WB);
                            if (EA == EB && FMath::Min(OA + WA, OB + WB) > FMath::Max(OA, OB) + Epsilon)
                            {
                                C = RoomContext; C.Kind = EObjectKind::Opening; C.Owner = RoomContext.Id;
                                C.Path += FString::Printf(TEXT(".openings[%d]"), O); A->TryGetStringField(TEXT("id"), C.Id);
                                Issue(TEXT("OPENING_OVERLAP"), TEXT("Two openings overlap on the same wall edge."), TEXT("Review their offsets and widths; keep the overlap only if intentional."), ESeverity::Warning);
                            }
                        }
                    }
                    const auto NavNodes = Array(Floor, TEXT("navNodes"));
                    for (int32 N = 0; N < NavNodes.Num(); ++N)
                    {
                        C = FloorContext; C.Kind = EObjectKind::Connector; C.Path += FString::Printf(TEXT(".navNodes[%d]"), N);
                        const auto ObjNode = Object(NavNodes[N]); if (!ObjNode) continue;
                        Id(ObjNode, NodeIds);
                        FNode Node; Node.Type = String(ObjNode, TEXT("type")).ToLower(); Node.Group = String(ObjNode, TEXT("connectorGroupId"));
                        if (!Node.Group.IsEmpty()) C.Id = Node.Group;
                        Node.Context = C;
                        if (!Node.Type.IsEmpty() && Node.Type != TEXT("pathnode") && Node.Type != TEXT("elevator") && Node.Type != TEXT("stairs") && Node.Type != TEXT("escalator")) Invalid(TEXT("connector type"));
                        const auto Position = ObjNode->Values.Find(TEXT("position")); Node.Position = XY(Position ? *Position : nullptr);
                        if (!Inside(Node.Position, Boundary)) Issue(TEXT("CONNECTOR_OUTSIDE"), TEXT("Navigation node is outside the building."), TEXT("Move it inside the building."));
                        Node.Rotation = Number(ObjNode, TEXT("rotationDegrees"), 0); Node.Size = Number(ObjNode, TEXT("sizeScale"), 1);
                        if (Node.Size <= 0) Invalid(TEXT("connector sizeScale"));
                        for (const auto& Level : Array(ObjNode, TEXT("connectsFloors")))
                        {
                            if (!Level.IsValid() || Level->Type != EJson::Number || !FMath::IsFinite(Level->AsNumber()) || Level->AsNumber() < 1 || Level->AsNumber() > FloorCount || FMath::FloorToDouble(Level->AsNumber()) != Level->AsNumber()) Invalid(TEXT("connectsFloors level"));
                            else Node.Floors.AddUnique(static_cast<int32>(Level->AsNumber()));
                        }
                        Nodes.Add(MoveTemp(Node));
                    }
                }
                for (int32 A = 0; A < Shapes.Num(); ++A) for (int32 B = A + 1; B < Shapes.Num(); ++B)
                {
                    if (ShouldCancel()) return;
                    if (Shapes[A].Context.Floor != Shapes[B].Context.Floor) continue;
                    if (RoomAreasOverlap(Shapes[A], Shapes[B]))
                    {
                        C = Shapes[A].Context;
                        Issue(TEXT("ROOM_OVERLAP"), TEXT("Room footprint overlaps ") + Shapes[B].Context.Id + TEXT("."), TEXT("Review the overlap; keep it if it is intentional."), ESeverity::Warning);
                    }
                }
                for (const auto& Reference : References)
                {
                    C = Reference.Context;
                    if (!Reference.Input.IsEmpty() && Reference.Input != C.Owner) Issue(TEXT("DOOR_OWNER"), TEXT("Door inputId differs from its owning room."), TEXT("Review the door's input room."), ESeverity::Warning);
                    if (!Reference.Target.IsEmpty() && !Reference.Target.Equals(TEXT("outside"), ESearchCase::IgnoreCase) && !Reference.Target.Equals(TEXT("corridor"), ESearchCase::IgnoreCase) && !RoomIds.Contains(Reference.Target))
                        Issue(TEXT("DOOR_REFERENCE"), TEXT("Door metadata refers to a missing room: ") + Reference.Target, TEXT("Update the door's connection metadata."), ESeverity::Warning);
                }
                for (const auto& Node : Nodes)
                {
                    if (ShouldCancel()) return;
                    C = Node.Context;
                    for (int32 Level : Node.Floors) if (!FloorIds.Contains(Level)) Issue(TEXT("CONNECTOR_FLOOR_MISSING"), TEXT("A connector refers to an unauthored floor."), TEXT("Author that floor or update the connector's served floors."));
                    if (Node.Type.IsEmpty() || Node.Type == TEXT("pathnode") || Node.Group.IsEmpty()) continue; // The planner supports legacy ungrouped connectors.
                    for (const auto& Other : Nodes)
                    {
                        if (&Node == &Other || Node.Group != Other.Group) continue;
                        if (Node.Context.Floor == Other.Context.Floor || Node.Type != Other.Type || !Node.Position.Equals(Other.Position, 0.01) || !FMath::IsNearlyEqual(Node.Size, Other.Size, Epsilon) || !FMath::IsNearlyEqual(Node.Rotation, Other.Rotation, Epsilon))
                        { Issue(TEXT("CONNECTOR_GROUP_INCONSISTENT"), TEXT("Copies of this connector disagree on type, position, size or floor."), TEXT("Edit the connector group so its floor instances agree.")); break; }
                    }
                    for (int32 Level : Node.Floors)
                    {
                        if (!Nodes.ContainsByPredicate([&](const FNode& Other) { return Other.Group == Node.Group && Other.Context.Floor == Level; }))
                            Issue(TEXT("CONNECTOR_INSTANCE_MISSING"), FString::Printf(TEXT("Connector has no instance on its served floor %d."), Level), TEXT("Repair the connector's served floors."));
                        else if (Level != Node.Context.Floor && Nodes.ContainsByPredicate([&](const FNode& Other) { return Other.Group == Node.Group && Other.Context.Floor == Level && !Other.Floors.IsEmpty() && !Other.Floors.Contains(Node.Context.Floor); }))
                            Issue(TEXT("CONNECTOR_FLOORS_DISAGREE"), TEXT("Connector floor instances do not allow travel in both directions."), TEXT("Make the served-floor lists consistent."));
                    }
                }
                const auto Entrances = Array(Obj, TEXT("entrances"));
                for (int32 E = 0; E < Entrances.Num(); ++E)
                {
                    C = Base; C.Kind = EObjectKind::Entrance; C.Path += FString::Printf(TEXT(".entrances[%d]"), E);
                    const auto Entrance = Object(Entrances[E]); if (!Entrance) continue;
                    Id(Entrance, EntranceIds); C.Floor = Integer(Entrance, TEXT("level"), 1);
                    const FString Kind = String(Entrance, TEXT("kind")).ToLower();
                    if (!Kind.IsEmpty() && Kind != TEXT("main") && Kind != TEXT("link")) Invalid(TEXT("entrance kind"));
                    if (C.Floor < 1 || C.Floor > FloorCount || (Kind != TEXT("link") && C.Floor != 1)) Issue(TEXT("ENTRANCE_FLOOR"), TEXT("Entrance is on an invalid floor; main entrances belong on level 1."), TEXT("Correct the entrance floor or kind."));
                    const int32 Edge = Integer(Entrance, TEXT("edge"), 0);
                    const double Offset = Number(Entrance, TEXT("offset"), 0), Width = Number(Entrance, TEXT("width"), 240);
                    if (!Boundary.IsValidIndex(Edge) || Offset < 0 || Width <= 0 || (Boundary.IsValidIndex(Edge) && Offset + Width > FVector2D::Distance(Boundary[Edge], Boundary[(Edge + 1) % Boundary.Num()]) * Scale + Epsilon))
                        Issue(TEXT("ENTRANCE_BOUNDS"), TEXT("Entrance does not fit along the building boundary."), TEXT("Move or resize it on an existing boundary edge."));
                    if (Kind == TEXT("link"))
                    {
                        const auto Target = String(Entrance, TEXT("connectsTo"), true);
                        if (Target.IsEmpty() || Target == C.Building) Issue(TEXT("ENTRANCE_TARGET"), TEXT("Link entrance needs a different target building."), TEXT("Choose the neighbouring building."));
                    }
                }
                C = Base;
            }
        };
    }

    FReport ValidateProject(const TSharedPtr<FJsonObject>& Project)
    {
        return ValidateProject(Project, [] { return false; });
    }

    FReport ValidateProject(const TSharedPtr<FJsonObject>& Project, TFunctionRef<bool()> ShouldCancel)
    {
        using namespace Private;
        if (ShouldCancel()) return {};
        FReader Reader(ShouldCancel);
        if (!Project) { Reader.Invalid(TEXT("project object")); return Reader.Report; }
        const double Scale = Reader.Number(Project, TEXT("scale"), 1);
        if (Scale <= KINDA_SMALL_NUMBER || Scale > TNumericLimits<float>::Max()) Reader.Invalid(TEXT("supported project scale"));
        if (Reader.Number(Project, TEXT("floorHeight"), 300) <= 0) Reader.Invalid(TEXT("floorHeight"));
        const int32 Version = Reader.Integer(Project, TEXT("version"), 0);
        if (Version < 0) Reader.Invalid(TEXT("version"));
        if (Version > 1) Reader.Issue(TEXT("FORMAT_NEWER"), TEXT("This project uses a newer format; some fields may not be checked."), TEXT("Use a validator supporting this format."), ESeverity::Warning);
        const auto Buildings = Reader.Array(Project, TEXT("buildings"), true);
        TSet<FString> Ids;
        TMap<FString, TSharedPtr<FJsonObject>> ById;
        for (int32 B = 0; B < Buildings.Num(); ++B)
        {
            if (ShouldCancel()) return {};
            Reader.C = {}; Reader.C.Kind = EObjectKind::Building; Reader.C.Path = FString::Printf(TEXT("$.buildings[%d]"), B);
            const auto Building = Reader.Object(Buildings[B]); if (!Building) continue;
            Reader.Id(Building, Ids); Reader.C.Building = Reader.C.Id;
            // Attribute identity diagnostics to their building, including duplicate IDs.
            for (auto& Issue : Reader.Report.Issues) if (Issue.JsonPath == Reader.C.Path && Issue.BuildingId.IsEmpty()) Issue.BuildingId = Reader.C.Building;
            ById.Add(Reader.C.Building, Building);
            Reader.String(Building, TEXT("name"), true);
            const int32 Count = Reader.Integer(Building, TEXT("floorCount"), 10);
            if (Count < 1 || Count > MaxBuildingFloors || Reader.Number(Building, TEXT("floorHeight"), Reader.Number(Project, TEXT("floorHeight"), 300)) <= 0) Reader.Invalid(TEXT("building floor dimensions"));
            const auto Boundary = Reader.Shape(Building, true);
            if (const auto Offset = Building->Values.Find(TEXT("offset"))) Reader.XY(*Offset);
            const auto Interior = Building->Values.Find(TEXT("interior"));
            if (Interior)
            {
                Reader.C.Path += TEXT(".interior");
                if (auto Object = Reader.Object(*Interior)) Reader.Interior(Object, Boundary, Count, Scale);
            }
            else Reader.Issue(TEXT("INTERIOR_UNAUTHORED"), TEXT("This building has no authored interior."), TEXT("Author its interior before checking room reachability."), ESeverity::Info);
        }
        // Resolve link references after indexing every building. A reciprocal saved entrance is not required.
        for (int32 B = 0; B < Buildings.Num(); ++B)
        {
            if (ShouldCancel()) return {};
            if (!Buildings[B].IsValid() || Buildings[B]->Type != EJson::Object) continue;
            const auto Building = Buildings[B]->AsObject();
            const TSharedPtr<FJsonObject>* Interior = nullptr;
            if (!Building->TryGetObjectField(TEXT("interior"), Interior)) continue;
            const TArray<TSharedPtr<FJsonValue>>* Entrances = nullptr;
            if (!(*Interior)->TryGetArrayField(TEXT("entrances"), Entrances)) continue;
            for (int32 E = 0; E < Entrances->Num(); ++E)
            {
                const auto& Value = (*Entrances)[E]; if (!Value.IsValid() || Value->Type != EJson::Object) continue;
                const auto Door = Value->AsObject(); FString Kind, Target;
                Door->TryGetStringField(TEXT("kind"), Kind); Door->TryGetStringField(TEXT("connectsTo"), Target);
                if (!Kind.Equals(TEXT("link"), ESearchCase::IgnoreCase) || ById.Contains(Target)) continue;
                Reader.C = {}; Reader.C.Kind = EObjectKind::Entrance;
                Building->TryGetStringField(TEXT("id"), Reader.C.Building); Door->TryGetStringField(TEXT("id"), Reader.C.Id); Door->TryGetNumberField(TEXT("level"), Reader.C.Floor);
                Reader.C.Path = FString::Printf(TEXT("$.buildings[%d].interior.entrances[%d]"), B, E);
                Reader.Issue(TEXT("ENTRANCE_BUILDING_MISSING"), TEXT("Link entrance refers to a missing building: ") + Target, TEXT("Choose an existing neighbouring building."));
            }
        }
        if (ShouldCancel()) return {};
        Reader.Report.Fingerprint = Fingerprint(Project);
        Reader.Report.Sort();
        return Reader.Report;
    }
    FReport ValidateFile(const FString& Path)
    {
        FString Text;
        TSharedPtr<FJsonObject> Project;
        if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Project) || !Project)
        {
            FReport Report; Report.Source = Path;
            FIssue Issue; Issue.Rule = TEXT("FILE_READ"); Issue.JsonPath = TEXT("$"); Issue.Message = TEXT("The file could not be read as a JSON project."); Issue.Suggestion = TEXT("Check the path and JSON syntax.");
            Report.Issues.Add(MoveTemp(Issue)); return Report;
        }
        FReport Report = ValidateProject(Project); Report.Source = Path; return Report;
    }
}
