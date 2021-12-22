#pragma once

#include "CoreMinimal.h"
#include "Hologram/FGAttachmentSplitterHologram.h"
#include "IntelliSplittersModule.h"

#include "MFGIntelliSplitterHologram.generated.h"


/**
 * TODO(aye): Description(s)
 */
UCLASS()
class INTELLISPLITTERS_API AMFGIntelliSplitterHologram
	: public AFGAttachmentSplitterHologram
{

	GENERATED_BODY()

	friend class FIntelliSplittersModule;

protected:

	virtual void ConfigureComponents(AFGBuildable* inBuildable) const override;

private:

	/** Array holding pre upgrade conveyor connections. */
	TArray<UFGFactoryConnectionComponent*> PreUpgradeConnections;

};




