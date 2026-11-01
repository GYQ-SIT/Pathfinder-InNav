#include "NavigationInstructions.h"

namespace NavigationInstructionPrivate
{
	FString DistanceText(double Metres)
	{
		return Metres < 0.5 ? FString(TEXT("less than 1 m"))
			: FString::Printf(TEXT("%d m"), FMath::Max(1, FMath::RoundToInt(Metres)));
	}

	bool IsWalkingEdge(const FNavigationRoutePoint& A, const FNavigationRoutePoint& B)
	{
		return A.Floor == B.Floor && (A.Floor == 0 || A.BuildingId.IsEmpty() || B.BuildingId.IsEmpty() || A.BuildingId == B.BuildingId);
	}

	bool IsVertical(ENavigationInstructionKind Kind)
	{
		return Kind == ENavigationInstructionKind::TakeLift || Kind == ENavigationInstructionKind::TakeStairs
			|| Kind == ENavigationInstructionKind::TakeEscalator || Kind == ENavigationInstructionKind::ChangeFloor;
	}
}

bool NavigationInstructions::IsLeftTurn(const FVector2D& Incoming, const FVector2D& Outgoing)
{
	return FVector2D::CrossProduct(Incoming, Outgoing) < 0;
}

TArray<FNavigationInstruction> NavigationInstructions::Build(const TArray<FNavigationRoutePoint>& Points,
	TFunctionRef<FVector(const FNavigationRoutePoint&)> WorldPosition,
	TFunctionRef<FString(const FString&)> BuildingName, int32 FirstPoint, int32 LastPoint)
{
	using namespace NavigationInstructionPrivate;
	TArray<FNavigationInstruction> Result;
	if (LastPoint == INDEX_NONE) LastPoint = Points.Num() - 1;
	if (!Points.IsValidIndex(FirstPoint) || !Points.IsValidIndex(LastPoint) || FirstPoint >= LastPoint) return Result;

	int32 Walking = INDEX_NONE;
	FVector2D Incoming = FVector2D::ZeroVector;
	auto Add = [&](ENavigationInstructionKind Kind, const FString& Text, int32 At, int32 End, double Metres)
	{
		Result.Add({ Kind, Text, At, End, Metres });
	};
	for (int32 I = FirstPoint; I < LastPoint; ++I)
	{
		const auto& A = Points[I]; const auto& B = Points[I + 1];
		const FVector From = WorldPosition(A), To = WorldPosition(B);
		const double Metres = FVector::Distance(From, To) / 100.0;
		if (!IsWalkingEdge(A, B))
		{
			Walking = INDEX_NONE; Incoming = FVector2D::ZeroVector;
			if (A.Floor > 0 && B.Floor > 0 && !A.BuildingId.IsEmpty() && !B.BuildingId.IsEmpty() && A.BuildingId != B.BuildingId)
			{
				Add(ENavigationInstructionKind::ExitBuilding, FString::Printf(TEXT("Use the link door from %s to %s on Floor %d."), *BuildingName(A.BuildingId), *BuildingName(B.BuildingId), A.Floor), I, I + 1, Metres);
				Add(ENavigationInstructionKind::EnterBuilding, FString::Printf(TEXT("Enter the building %s on Floor %d."), *BuildingName(B.BuildingId), B.Floor), I + 1, I + 1, 0);
			}
			else if (A.Floor == 0)
				Add(ENavigationInstructionKind::EnterBuilding, TEXT("Enter the building ") + BuildingName(B.BuildingId) + TEXT(" on Floor 1."), I + 1, I + 1, Metres);
			else if (B.Floor == 0)
				Add(ENavigationInstructionKind::ExitBuilding, TEXT("Leave ") + BuildingName(A.BuildingId) + TEXT(" and continue outside."), I, I + 1, Metres);
			else
			{
				const auto Type = A.ConnectorType != EInteriorNavNodeType::PathNode ? A.ConnectorType : B.ConnectorType;
				const auto Kind = Type == EInteriorNavNodeType::Elevator ? ENavigationInstructionKind::TakeLift
					: Type == EInteriorNavNodeType::Stairs ? ENavigationInstructionKind::TakeStairs
					: Type == EInteriorNavNodeType::Escalator ? ENavigationInstructionKind::TakeEscalator : ENavigationInstructionKind::ChangeFloor;
				const TCHAR* Word = Kind == ENavigationInstructionKind::TakeLift ? TEXT("lift") : Kind == ENavigationInstructionKind::TakeStairs ? TEXT("stairs")
					: Kind == ENavigationInstructionKind::TakeEscalator ? TEXT("escalator") : TEXT("floor connection");
				// One ride through several route vertices is one instruction. Preserve its full physical distance.
				const bool bSameRide = !Result.IsEmpty() && IsVertical(Result.Last().Kind) && Result.Last().Kind == Kind
					&& Result.Last().LastPointIndex == I && Points[Result.Last().PointIndex].BuildingId == A.BuildingId
					&& Points[Result.Last().PointIndex].ConnectorId == A.ConnectorId
					&& (A.Floor - Points[Result.Last().PointIndex].Floor) * (B.Floor - A.Floor) > 0;
				const int32 Start = bSameRide ? Result.Last().PointIndex : I;
				const FString Text = FString::Printf(TEXT("Take the %s in %s from Floor %d to Floor %d."), Word, *BuildingName(A.BuildingId), Points[Start].Floor, B.Floor);
				if (bSameRide) { Result.Last().Text = Text; Result.Last().LastPointIndex = I + 1; Result.Last().Metres += Metres; }
				else Add(Kind, Text, I, I + 1, Metres);
			}
			continue;
		}

		const FVector2D Direction = FVector2D(To - From).GetSafeNormal();
		if (Direction.IsNearlyZero())
		{
			if (Walking != INDEX_NONE) { Result[Walking].Metres += Metres; Result[Walking].LastPointIndex = I + 1; }
			else if (Metres > KINDA_SMALL_NUMBER) { Walking = Result.Num(); Add(ENavigationInstructionKind::Straight, FString(), I, I + 1, Metres); }
			continue;
		}
		auto Kind = ENavigationInstructionKind::Straight;
		if (!Incoming.IsNearlyZero() && FVector2D::DotProduct(Incoming, Direction) < 0.90)
			Kind = FVector2D::DotProduct(Incoming, Direction) < -0.95 ? ENavigationInstructionKind::TurnAround
				: IsLeftTurn(Incoming, Direction) ? ENavigationInstructionKind::TurnLeft : ENavigationInstructionKind::TurnRight;
		else if (A.bIsDoorway) Kind = ENavigationInstructionKind::PassDoor;
		if (Walking == INDEX_NONE || Kind != ENavigationInstructionKind::Straight)
		{
			Walking = Result.Num(); Add(Kind, FString(), I, I + 1, Metres);
		}
		else { Result[Walking].Metres += Metres; Result[Walking].LastPointIndex = I + 1; }
		Incoming = Direction;
	}
	for (auto& Step : Result)
	{
		if (IsVertical(Step.Kind) || Step.Kind == ENavigationInstructionKind::EnterBuilding || Step.Kind == ENavigationInstructionKind::ExitBuilding) continue;
		const TCHAR* Action = Step.Kind == ENavigationInstructionKind::TurnLeft ? TEXT("Turn left and continue")
			: Step.Kind == ENavigationInstructionKind::TurnRight ? TEXT("Turn right and continue")
			: Step.Kind == ENavigationInstructionKind::TurnAround ? TEXT("Turn around and continue")
			: Step.Kind == ENavigationInstructionKind::PassDoor ? TEXT("Go through the door and continue")
			: Points[Step.PointIndex].Floor == 0 ? TEXT("Follow the outdoor path") : TEXT("Continue");
		Step.Text = FString::Printf(TEXT("%s for %s."), Action, *DistanceText(Step.Metres));
	}
	return Result;
}
