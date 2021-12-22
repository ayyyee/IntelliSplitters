#pragma once

#include "CoreMinimal.h"
#include "Configuration/ConfigManager.h"
#include "Engine/Engine.h"

#include "IntelliSplittersConfig.generated.h"


struct FIntelliSplittersConfig_Upgrade;
struct FIntelliSplittersConfig_Features;
struct FIntelliSplittersConfig_Preferences;


/**
 * Config struct for Upgrade
 */
USTRUCT(BlueprintType)
struct FIntelliSplittersConfig_Upgrade
{

	GENERATED_BODY()

public:

	UPROPERTY(BlueprintReadWrite)
	bool RemoveAllConveyors;

	UPROPERTY(BlueprintReadWrite)
	bool ShowWarningMessages;

};


/**
 * Config struct for Features
 */
USTRUCT(BlueprintType)
struct FIntelliSplittersConfig_Features
{

	GENERATED_BODY()

public:

	UPROPERTY(BlueprintReadWrite)
	bool RespectOverclocking;

};


/**
 * Config struct for Preferences
 */
USTRUCT(BlueprintType)
struct FIntelliSplittersConfig_Preferences
{

	GENERATED_BODY()

public:

	UPROPERTY(BlueprintReadWrite)
	bool ShowAlphaWarning;

};


/**
 * Struct generated from Mod Configuration Asset '/IntelliSplitters/IntelliSplittersConfig'.
 */
USTRUCT(BlueprintType)
struct FIntelliSplittersConfig
{

	GENERATED_BODY()

public:

	UPROPERTY(BlueprintReadWrite)
	FIntelliSplittersConfig_Upgrade Upgrade;

	UPROPERTY(BlueprintReadWrite)
	FIntelliSplittersConfig_Features Features;

	UPROPERTY(BlueprintReadWrite)
	FIntelliSplittersConfig_Preferences Preferences;

	/**
	 * Retrieves active configuration and returns object of this struct containing it.
	 */
	static FIntelliSplittersConfig GetActiveConfig()
	{
		FIntelliSplittersConfig ConfigStruct{};

		FConfigId ConfigId{ "IntelliSplitters", "" };

		UConfigManager* ConfigManager = GEngine->GetEngineSubsystem<UConfigManager>();

		ConfigManager->FillConfigurationStruct(ConfigId,
			FDynamicStructInfo{ FIntelliSplittersConfig::StaticStruct(), &ConfigStruct });

		return ConfigStruct;
	}

};




