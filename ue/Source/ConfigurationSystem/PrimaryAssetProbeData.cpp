#include "PrimaryAssetProbeData.h"

FPrimaryAssetId UPrimaryAssetProbeData::GetPrimaryAssetId() const
{
	// 固定类型名使 ini 扫描规则和探针输出在不同机器上保持一致。
	return FPrimaryAssetId(TEXT("PrimaryAssetProbe"), GetFName());
}
