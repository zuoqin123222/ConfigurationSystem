#include "StageCornerMask.h"

#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SLeafWidget.h"

namespace
{
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
			const int32 StripeCount = FMath::CeilToInt(Radius);
			const bool bRight = Corner == EStageCorner::TopRight
				|| Corner == EStageCorner::BottomRight;
			const bool bBottom = Corner == EStageCorner::BottomLeft
				|| Corner == EStageCorner::BottomRight;
			const FSlateBrush* WhiteBrush =
				FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));

			for (int32 Stripe = 0; Stripe < StripeCount; ++Stripe)
			{
				const float SampleY = FMath::Min(
					Radius,
					static_cast<float>(Stripe) + 0.5f);
				const float Delta = SampleY - Radius;
				const float Boundary = Radius - FMath::Sqrt(
					FMath::Max(0.0f, Radius * Radius - Delta * Delta));
				const float MaskWidth = FMath::Min(
					Radius,
					FMath::CeilToFloat(Boundary) + 0.75f);
				if (MaskWidth <= 0.0f)
				{
					continue;
				}
				const float X = bRight ? Radius - MaskWidth : 0.0f;
				const float Y = bBottom
					? Radius - static_cast<float>(Stripe) - 1.05f
					: static_cast<float>(Stripe);
				FSlateDrawElement::MakeBox(
					OutDrawElements,
					LayerId,
					AllottedGeometry.ToPaintGeometry(
						FVector2f(MaskWidth, 1.1f),
						FSlateLayoutTransform(FVector2f(X, Y))),
					WhiteBrush,
					ESlateDrawEffect::None,
					FLinearColor::White);
			}
			return LayerId;
		}

	private:
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
