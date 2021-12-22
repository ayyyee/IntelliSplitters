#pragma once

#include "CoreMinimal.h"

#include "Module/GameInstanceModule.h"

#include "IntelliSplittersGameInstanceModule.generated.h"


/**
 * TODO(aye): Description(s)
 */
UCLASS(BlueprintType)
class INTELLISPLITTERS_API UIntelliSplittersGameInstanceModule
	: public UGameInstanceModule
{

	GENERATED_BODY()

public:

	virtual void DispatchLifecycleEvent(ELifecyclePhase Phase) override;

};




