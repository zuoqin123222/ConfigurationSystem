#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "StageCornerMask.generated.h"

UENUM()
enum class EStageCorner : uint8
{
	TopLeft,
	TopRight,
	BottomLeft,
	BottomRight
};

/** 用原生 Slate 条带绘制舞台外侧的反向圆角白色遮罩。 */
UCLASS(NotBlueprintable, Transient)
class CONFIGURATIONSYSTEM_API UStageCornerMask final : public UWidget
{
	GENERATED_BODY()

public:
	void SetCorner(EStageCorner InCorner);
	void SetRadius(float InRadius);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	EStageCorner Corner = EStageCorner::TopLeft;
	float Radius = 24.0f;
};
