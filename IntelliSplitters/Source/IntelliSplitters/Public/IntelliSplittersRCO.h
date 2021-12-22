#pragma once

#include "CoreMinimal.h"

#include "FGPlayerController.h"
#include "FGRemoteCallObject.h"

#include "IntelliSplittersRCO.generated.h"


class AMFGBuildableIntelliSplitter;


/**
 * TODO(aye): Description(s)
 */
UCLASS(NotBlueprintable)
class INTELLISPLITTERS_API UIntelliSplittersRCO : public UFGRemoteCallObject
{

	GENERATED_BODY()

public:

	virtual void GetLifetimeReplicatedProps(
		TArray<FLifetimeProperty>& OutLifetimeProps) const override;

public:

	static UIntelliSplittersRCO* Get(UWorld* World);

    UFUNCTION(Server, Unreliable)
    void EnableReplication(
        AMFGBuildableIntelliSplitter* Splitter, float Duration) const;

    UFUNCTION(Server, Reliable)
    void SetTargetRateAutomatic(
        AMFGBuildableIntelliSplitter* Splitter, bool bAutomatic) const;

    UFUNCTION(Server, Reliable)
    void SetTargetInputRate(
        AMFGBuildableIntelliSplitter* Splitter, float Rate) const;

    UFUNCTION(Server, Reliable)
    void SetOutputRate(
        AMFGBuildableIntelliSplitter* Splitter, int32 Output, float Rate) const;

    UFUNCTION(Server, Reliable)
    void SetOutputAutomatic(
        AMFGBuildableIntelliSplitter* Splitter, int32 Output, bool bAutomatic) const;

    UFUNCTION(Server, Reliable)
    void BalanceNetwork(
        AMFGBuildableIntelliSplitter* Splitter, bool bRootOnly) const;

private:

    UPROPERTY(Replicated)
    int32 Dummy;

};




