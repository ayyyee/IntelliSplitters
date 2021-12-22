#pragma once

// Get more debug output to console when debug flag is set in the splitter UI
#define INTELLISPLITTERS_DEBUG 1

#include <tuple>

#include "CoreMinimal.h"
#include <FGFactoryConnectionComponent.h>

#include "IntelliSplittersLog.h"

#include "Buildables/MFGBuildableIntelliSplitter.h"
#include "Modules/ModuleManager.h"


class FIntelliSplittersModule : public IModuleInterface
{

	friend class AMFGBuildableIntelliSplitter;
	friend class AIntelliSplittersSubsystem;

public:

	/**
	 * Module startup method.
	 */
	virtual void StartupModule() override;

	/**
	 * Get a pointer to the module.
	 */
	static FIntelliSplittersModule* Get();

private:

	/**
	 * Increase LoadedSplitterCount when a splitter is loaded from save-game.
	 * 
	 * @param Splitter Pointer to the splitter actor
	 */
	void OnSplitterLoadedFromSaveGame(AMFGBuildableIntelliSplitter* Splitter);

	/**
	 * Add splitter to DoomedSplitters.
	 * 
	 * @param Splitter Pointer to the splitter acotr
	 */
	void ScheduleDismantle(AMFGBuildableIntelliSplitter* Splitter);

	/**
	 * Flag indicating whether there are any loaded splitters.
	 * 
	 * @return Whether there are any loaded splitters
	 */
	bool HasLoadedSplitters() const;

	/**
	 * Replace pre-component fix splitters.
	 * 
	 * TODO(aye): Better description
	 * 
	 * @param World The world
	 * @param IntelliSplittersSubsystem The splitters subsystem
	 */
	void ReplacePreComponentFixSplitters(UWorld* World, AIntelliSplittersSubsystem* IntelliSplittersSubsystem);

public:

	/** Holds a flag indicating whether the plugin is in alpha version. */
	static const bool bIsAlphaVersion = true;

	/** Holds the mod reference. */
	static const FName ModReference;

private:

	/** Array holding pre-component fix splitters. */
	TArray<std::tuple<AMFGBuildableIntelliSplitter*,
		TInlineComponentArray<UFGFactoryConnectionComponent*, 2>,
		TInlineComponentArray<UFGFactoryConnectionComponent*, 4>>
	> PreComponentFixSplitters;

	/** Holds number of loaded splitters. */
	int32 LoadedSplitterCount;

	/** Array holding doomed splitters. */
	TArray<AMFGBuildableIntelliSplitter*> DoomedSplitters;

};




