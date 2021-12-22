#include "IntelliSplittersGameInstanceModule.h"

#include "Registry/RemoteCallObjectRegistry.h"
#include "IntelliSplittersLog.h"
#include "IntelliSplittersRCO.h"


void UIntelliSplittersGameInstanceModule::DispatchLifecycleEvent(ELifecyclePhase Phase)
{
	Super::DispatchLifecycleEvent(Phase);

	switch(Phase)
	{

	case ELifecyclePhase::CONSTRUCTION:
	{
		UE_LOG(LogIntelliSplitters, Display,
			TEXT("Registering AutoSplittersRCO object with RemoteCallObjectRegistry."));

		auto RCORegistry = GetGameInstance()->GetSubsystem<URemoteCallObjectRegistry>();
		RCORegistry->RegisterRemoteCallObject(UIntelliSplittersRCO::StaticClass());

		break;
	}

	default:

		break;

	}
}





