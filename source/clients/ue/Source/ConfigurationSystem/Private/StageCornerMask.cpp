#include "StageCornerMask.h"

#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SLeafWidget.h"

namespace
{
	constexpr int32 ArcSegmentCount = 48;
	constexpr float FeatherWidth = 1.25f;

	class SStageCornerMask final : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SStageCornerMask)
			: _Corner(EStageCorner::TopLeft)
			, _Radius(24.0f)
		{
		}
			SLATE_ARGUMENT(EStageCorner, Corner)
			SLATE_ARGUMENT(float, Radius)
		SLATE_END_ARGS()

		void Construct(const FArguments& Arguments)
		{
			Corner = Arguments._Corner;
			Radius = FMath::Max(1.0f, Arguments._Radius);
			SetCanTick(false);
		}

		virtual FVector2D ComputeDesiredSize(float) const override
		{
			return FVector2D(Radius);
		}

		virtual int32 OnPaint(
			const FPaintArgs& Args,
			const FGeometry& AllottedGeometry,
			const FSlateRect& MyCullingRect,
			FSlateWindowElementList& OutDrawElements,
			const int32 LayerId,
			const FWidgetStyle& InWidgetStyle,
			const bool bParentEnabled) const override
		{
			(void)Args;
			(void)MyCullingRect;
			(void)InWidgetStyle;
			(void)bParentEnabled;

			const FSlateBrush* WhiteBrush =
				FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
			const FSlateResourceHandle ResourceHandle =
				FSlateApplication::Get().GetRenderer()->GetResourceHandle(*WhiteBrush);
			const FSlateRenderTransform RenderTransform =
				AllottedGeometry.GetAccumulatedRenderTransform();
			const FVector2f OuterCorner = GetOuterCorner();
			const FVector2f CircleCenter = FVector2f(Radius, Radius) - OuterCorner;
			const FVector2f FirstArcPoint = GetFirstArcPoint();
			const FVector2f LastArcPoint = GetLastArcPoint();
			const float StartAngle = FMath::Atan2(
				FirstArcPoint.Y - CircleCenter.Y,
				FirstArcPoint.X - CircleCenter.X);
			float EndAngle = FMath::Atan2(
				LastArcPoint.Y - CircleCenter.Y,
				LastArcPoint.X - CircleCenter.X);
			while (EndAngle < StartAngle)
			{
				EndAngle += 2.0f * PI;
			}
			if (EndAngle - StartAngle > PI)
			{
				EndAngle -= 2.0f * PI;
			}

			TArray<FSlateVertex> Vertices;
			TArray<SlateIndex> Indices;
			Vertices.Reserve(1 + (ArcSegmentCount + 1) * 3);
			Indices.Reserve(ArcSegmentCount * 9);
			AddVertex(
				Vertices,
				RenderTransform,
				OuterCorner,
				FColor::White);

			const float SolidRadius = Radius + FeatherWidth;
			const float TransparentRadius = FMath::Max(
				0.0f,
				Radius - FeatherWidth);
			for (int32 Segment = 0; Segment <= ArcSegmentCount; ++Segment)
			{
				const float Alpha =
					static_cast<float>(Segment) / ArcSegmentCount;
				const float Angle = FMath::Lerp(StartAngle, EndAngle, Alpha);
				const FVector2f Direction(
					FMath::Cos(Angle),
					FMath::Sin(Angle));
				const FVector2f SolidPoint = ClampToMask(
					CircleCenter + Direction * SolidRadius);
				const FVector2f TransparentPoint =
					CircleCenter + Direction * TransparentRadius;
				AddVertex(
					Vertices,
					RenderTransform,
					SolidPoint,
					FColor::White);
				AddVertex(
					Vertices,
					RenderTransform,
					TransparentPoint,
					FColor::Transparent);
			}

			for (int32 Segment = 0; Segment < ArcSegmentCount; ++Segment)
			{
				const SlateIndex SolidA =
					static_cast<SlateIndex>(1 + Segment * 2);
				const SlateIndex TransparentA = SolidA + 1;
				const SlateIndex SolidB = SolidA + 2;
				const SlateIndex TransparentB = SolidA + 3;
				Indices.Append({0, SolidA, SolidB});
				Indices.Append({
					SolidA,
					TransparentA,
					TransparentB,
					SolidA,
					TransparentB,
					SolidB});
			}

			FSlateDrawElement::MakeCustomVerts(
				OutDrawElements,
				LayerId,
				ResourceHandle,
				Vertices,
				Indices,
				nullptr,
				0,
				0,
				ESlateDrawEffect::PreMultipliedAlpha);
			return LayerId;
		}

	private:
		static void AddVertex(
			TArray<FSlateVertex>& Vertices,
			const FSlateRenderTransform& RenderTransform,
			const FVector2f Position,
			const FColor Color)
		{
			Vertices.Add(
				FSlateVertex::Make<ESlateVertexRounding::Disabled>(
					RenderTransform,
					Position,
					FVector2f::ZeroVector,
					Color));
		}

		FVector2f ClampToMask(const FVector2f Point) const
		{
			return FVector2f(
				FMath::Clamp(Point.X, 0.0f, Radius),
				FMath::Clamp(Point.Y, 0.0f, Radius));
		}

		FVector2f GetOuterCorner() const
		{
			switch (Corner)
			{
			case EStageCorner::TopRight:
				return FVector2f(Radius, 0.0f);
			case EStageCorner::BottomLeft:
				return FVector2f(0.0f, Radius);
			case EStageCorner::BottomRight:
				return FVector2f(Radius, Radius);
			default:
				return FVector2f::ZeroVector;
			}
		}

		FVector2f GetFirstArcPoint() const
		{
			switch (Corner)
			{
			case EStageCorner::TopRight:
				return FVector2f(0.0f, 0.0f);
			case EStageCorner::BottomLeft:
				return FVector2f(0.0f, 0.0f);
			case EStageCorner::BottomRight:
				return FVector2f(Radius, 0.0f);
			default:
				return FVector2f(Radius, 0.0f);
			}
		}

		FVector2f GetLastArcPoint() const
		{
			switch (Corner)
			{
			case EStageCorner::TopRight:
				return FVector2f(Radius, Radius);
			case EStageCorner::BottomLeft:
				return FVector2f(Radius, Radius);
			case EStageCorner::BottomRight:
				return FVector2f(0.0f, Radius);
			default:
				return FVector2f(0.0f, Radius);
			}
		}

		EStageCorner Corner = EStageCorner::TopLeft;
		float Radius = 24.0f;
	};
}

void UStageCornerMask::SetCorner(const EStageCorner InCorner)
{
	Corner = InCorner;
}

void UStageCornerMask::SetRadius(const float InRadius)
{
	Radius = FMath::Max(1.0f, InRadius);
}

TSharedRef<SWidget> UStageCornerMask::RebuildWidget()
{
	return SNew(SStageCornerMask)
		.Corner(Corner)
		.Radius(Radius);
}
