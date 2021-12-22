#pragma once

#include "CoreMinimal.h"
#include "IntelliSplittersSerializationVersion.generated.h"


UENUM(BlueprintType)
enum class EIntelliSplittersSerializationVersion : uint8
{
	/** AutoSplitters Legacy */
	Legacy = 0,

	/** AutoSplitters FixedPrecisionArithmetic */
	FixedPrecisionArithmetic = 1,

	/** Initial IntelliSplitters version */
	Initial = 2,

	/** Keep at the bottom of the list */
	VersionPlusOne,

	/** Latest version */
	Latest = VersionPlusOne - 1
};




