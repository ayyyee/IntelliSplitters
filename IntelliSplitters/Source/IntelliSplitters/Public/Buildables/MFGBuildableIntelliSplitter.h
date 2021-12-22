#pragma once

#include <array>
#include <tuple>

#include "FGPlayerController.h"
#include "FGFactoryConnectionComponent.h"
#include "Buildables/FGBuildableAttachmentSplitter.h"
#include "Buildables/FGBuildableConveyorBase.h"

#include "IntelliSplittersModule.h"
#include "IntelliSplittersRCO.h"
#include "IntelliSplittersLog.h"
#include "Util/BitField.h"

#include "MFGBuildableIntelliSplitter.generated.h"


UENUM(BlueprintType, Meta = (BitFlags))
enum class EOutputState : uint8
{
	Automatic UMETA(DisplayName = "Automatic"),

	Connected UMETA(DisplayName = "Connected"),

	IntelliSplitter UMETA(DisplayName = "IntelliSplitter")
};

template <>
struct IsEnumBitfield<EOutputState> : std::true_type {};


enum class EIntelliSplitterPersistentFlags : uint32
{
	/**
	 * First 8 bits reserved for version
	 */

	ManualInputRate = 8,

	NeedsConnectionsFixup = 9,

	NeedsDistributionSetup = 10
};

template <>
struct IsEnumBitfield<EIntelliSplitterPersistentFlags> : std::true_type {};


enum class EIntelliSplitterTransientFlags : uint32
{
	/**
	 * First 8 bits reserved for version
	 */

	/** Replication is currently turned on */
	IsReplicationEnabled = 8,

	/** Splitter was loaded from savegame and needs to be processed accordingly in BeginPlay() */
	NeedsLoadedSplitterProcessing = 9,

	/** Splitter was deemed incompatible after load process, remove in Invoke_BeginPlay() hook */
	DismantleAfterLoading = 10
};

template <>
struct IsEnumBitfield<EIntelliSplitterTransientFlags> : std::true_type {};


USTRUCT(BlueprintType)
struct INTELLISPLITTERS_API FMFGBuildableIntelliSplitterReplicatedProperties
{

	GENERATED_BODY()

	static constexpr int32 NUM_OUTPUTS = 3;

	UPROPERTY(Transient)
	uint32 TransientState;

	UPROPERTY(SaveGame, Meta = (NoAutoJson))
	int32 OutputStates[NUM_OUTPUTS];

	UPROPERTY(SaveGame, Meta = (NoAutoJson))
	uint32 PersistentState;

	UPROPERTY(SaveGame, Meta = (NoAutoJson))
	int32 TargetInputRate;

	UPROPERTY(SaveGame, Meta = (NoAutoJson))
	int32 OutputRates[NUM_OUTPUTS];

	UPROPERTY(Transient, BlueprintReadOnly)
	int32 LeftInCycle;

	UPROPERTY(Transient, BlueprintReadOnly)
	int32 CycleLength;

	UPROPERTY(Transient, BlueprintReadOnly)
	int32 CachedInventoryItemCount;

	UPROPERTY(Transient, BlueprintReadOnly)
	float ItemRate;

	FMFGBuildableIntelliSplitterReplicatedProperties();

};


class AFMGBuildableIntelliSplitter;
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FAMFGBuildableIntelliSplitterOnStateChanged,
	AMFGBuildableIntelliSplitter*, IntelliSplitter);


typedef EIntelliSplitterPersistentFlags EPersistent;
typedef EIntelliSplitterTransientFlags ETransient;


/**
 * The buildable IntelliSplitter
 */
UCLASS()
class INTELLISPLITTERS_API AMFGBuildableIntelliSplitter : public AFGBuildableAttachmentSplitter
{

	GENERATED_BODY()

	friend class FIntelliSplittersModule;
	friend class AMFGIntelliSplitterHologram;
	friend class UIntelliSplittersRCO;
	friend class AMFGReplicationDetailAcotr_BuildableIntelliSplitter;

public:

	/**
	 * Default constructor.
	 */
	AMFGBuildableIntelliSplitter();

	virtual void GetLifetimeReplicatedProps(TArray< FLifetimeProperty >& OutLifetimeProps) const override;

	virtual void BeginPlay() override;

	virtual void PostLoadGame_Implementation(int32 saveVersion, int32 gameVersion) override;

	virtual UClass* GetReplicationDetailActorClass() const override;

protected:

	virtual void Factory_Tick(float dt) override;

	virtual bool Factory_GrabOutput_Implementation(
		UFGFactoryConnectionComponent* connection, FInventoryItem& out_item,
		float& out_OffsetBeyond, TSubclassOf< UFGItemDescriptor > type) override;
	
	virtual void FillDistributionTable(float dt) override;

protected:

	UIntelliSplittersRCO* RCO() const;

	void ServerEnableReplication(float Duration);

	bool ServerSetTargetRateAutomatic(bool bAutomatic);

	bool ServerSetTargetInputRate(float Rate);

	bool ServerSetOutputRate(int32 Output, float Rate);

	bool ServerSetOutputAutomatic(int32 Output, bool bAutomatic);

	void ServerReplicationEnabledTimeout();

	UFUNCTION()
	void OnRep_Replicated();

private:

	void SetupDistribution(bool bLoadingSave = false);

	void PrepareCycle(bool bAllowCycleExtension, bool bReset = false);

	bool IsOutputBlocked(int32 Output) const;

public:

    UFUNCTION(BlueprintPure)
    static int32 GetFractionalRateDigits();

    UFUNCTION(BlueprintPure)
    bool IsReplicationEnabled() const;

    UFUNCTION(BlueprintCallable)
    void EnableReplication(float Duration);

    UFUNCTION(BlueprintCallable, BlueprintPure)
    bool IsTargetRateAutomatic() const;

    UFUNCTION(BlueprintCallable)
    void SetTargetRateAutomatic(bool bAutomatic);

    UFUNCTION(BlueprintPure)
    float GetTargetInputRate() const;

    UFUNCTION(BlueprintCallable)
    void SetTargetInputRate(float Rate);

    UFUNCTION(BlueprintPure)
    float GetOutputRate(int32 Output) const;

    UFUNCTION(BlueprintCallable)
    void SetOutputRate(int32 Output, float Rate);

    UFUNCTION(BlueprintCallable)
    void SetOutputAutomatic(int32 Output, bool bAutomatic);

    UFUNCTION(BlueprintPure)
    bool IsOutputAutomatic(int32 Output) const;

    UFUNCTION(BlueprintPure)
    bool IsOutputIntelliSplitter(int32 Output) const;

    UFUNCTION(BlueprintPure)
    bool IsOutputConnected(int32 Output) const;

    UFUNCTION(BlueprintCallable)
	void BalanceNetwork(bool bRootOnly = true);

	uint32 GetSplitterVersion() const;

    UFUNCTION(BlueprintPure)
	int32 GetInventorySize() const;

    UFUNCTION(BlueprintPure)
	float GetItemRate() const;

    UFUNCTION(BluePrintPure)
	static bool IsDebugSupported();

    UFUNCTION(BluePrintCallable)
	bool HasCurrentData(); // do not mark this const, as it will turn the function pure in the blueprint

    UFUNCTION(BlueprintPure)
	int32 GetError() const;

public:

	static constexpr uint32 VERSION = 1;

	static constexpr int32 MAX_INVENTORY_SIZE = 10;

	static constexpr float EXPONENTIAL_AVERAGE_WEIGHT = 0.5f;

	static constexpr int32 NUM_OUTPUTS = 3;

	static constexpr float BLOCK_DETECTION_THRESHOLD = 0.5f;

	static constexpr int32 FRACTIONAL_RATE_DIGITS = 3;
	static constexpr int32 FRACTIONAL_RATE_MULTIPLIER = PowConstexpr(10, FRACTIONAL_RATE_DIGITS);
	static constexpr float INV_FRACTIONAL_RATE_MULTIPLIER = 1.0f / FRACTIONAL_RATE_MULTIPLIER;

	static constexpr int32 FRACTIONAL_SHARE_DIGITS = 5;
	static constexpr int64 FRACTIONAL_SHARE_MULTIPLIER = PowConstexpr(10, FRACTIONAL_SHARE_DIGITS);
	static constexpr float INV_FRACTIONAL_SHARE_MULTIPLIER = 1.0f / FRACTIONAL_SHARE_MULTIPLIER;

	static constexpr float UPGRADE_POSITION_REQUIRED_DELTA = 100.0f;

public:

	struct FNetworkNode
	{
		AMFGBuildableIntelliSplitter* Splitter;

		FNetworkNode* Input;

		int32 MaxInputRate;

		std::array<FNetworkNode*, NUM_OUTPUTS> Outputs;

		std::array<int64, NUM_OUTPUTS> PotentialShares;

		std::array<int32, NUM_OUTPUTS> MaxOutputRates;

		int32 FixedDemand;

		int64 Shares;

		int32 AllocatedInputRate;

		std::array<int32, NUM_OUTPUTS> AllocatedOutputRates;

		bool bConnectionStateChanged;

		explicit FNetworkNode(AMFGBuildableIntelliSplitter* Splitter, FNetworkNode* Input = nullptr)
			: Splitter(Splitter)
			, Input(Input)
			, MaxInputRate(0)
			, Outputs({ nullptr })
			, PotentialShares({ 0 })
			, MaxOutputRates({ 0 })
			, FixedDemand(0)
			, Shares(0)
			, AllocatedInputRate(0)
			, AllocatedOutputRates({ 0 })
			, bConnectionStateChanged(false)
		{}
	};

private:

	void SetError(uint8 Error);

	void ClearError();

	void FixupConnections();

	void SetupInitialDistributionState();

	static std::tuple<bool, int32> ServerBalanceNetwork(
		AMFGBuildableIntelliSplitter* ForSplitter, bool bRootOnly = false);

	static std::tuple<AMFGBuildableIntelliSplitter*, int32, bool>
		FindIntelliSplitterAndMaxBeltRate(
		UFGFactoryConnectionComponent* Connection, bool bForward);

	static std::tuple<AFGBuildableFactory*, int32, bool>
		FindFactoryAndMaxBeltRate(
		UFGFactoryConnectionComponent* Connection, bool bForward);

	static bool DiscoverHierarchy(
		TArray<TArray<FNetworkNode>>& Nodes, AMFGBuildableIntelliSplitter* Splitter,
		const int32 Level, FNetworkNode* InputNode, const int32 ChildInParent,
		AMFGBuildableIntelliSplitter* Root, bool bExtractPotentialShares);

	void SetSplitterVersion(uint32 Version);

	FORCEINLINE bool IsSplitterFlagSet(EPersistent Flag) const;

	FORCEINLINE void SetSplitterFlag(EPersistent Flag, bool bValue);

	FORCEINLINE void SetSplitterFlag(EPersistent Flag);

	FORCEINLINE void ClearSplitterFlag(EPersistent Flag);

	FORCEINLINE void ToggleSplitterFlag(EPersistent Flag);

	FORCEINLINE bool IsSplitterFlagSet(ETransient Flag) const;

	FORCEINLINE void SetSplitterFlag(ETransient Flag, bool bValue);

	FORCEINLINE void SetSplitterFlag(ETransient Flag);

	FORCEINLINE void ClearSplitterFlag(ETransient Flag);

	FORCEINLINE void ToggleSplitterFlag(ETransient Flag);

protected:

	UPROPERTY(SaveGame, ReplicatedUsing = OnRep_Replicated, BlueprintReadOnly, Meta = (NoAutoJson))
	FMFGBuildableIntelliSplitterReplicatedProperties Replicated;

	UPROPERTY(Transient)
	uint32 TransientState_DEPRECATED;

	UPROPERTY(SaveGame, Meta = (NoAutoJson))
	TArray<int32> OutputStates_DEPRECATED;

	UPROPERTY(SaveGame, Meta = (NoAutoJson))
	TArray<int32> RemainingItems_DEPRECATED;

	UPROPERTY(SaveGame, Meta = (NoAutoJson))
	uint32 PersistentState_DEPRECATED;

	UPROPERTY(SaveGame, Meta = (NoAutoJson))
	int32 TargetInputRate_DEPRECATED;

	UPROPERTY(SaveGame, Meta = (NoAutoJson))
	TArray<int32> IntegralOutputRates_DEPRECATED;

	UPROPERTY(Transient)
	int32 LeftInCycle_DEPRECATED;

	UPROPERTY(SaveGame, Meta = (NoAutoJson))
	int32 LeftInCycleForOutputs[NUM_OUTPUTS];

	UPROPERTY(Transient, BlueprintReadWrite)
	bool Debug;

	UPROPERTY(Transient)
	int32 CycleLength_DEPRECATED;

	UPROPERTY(Transient)
	int32 CachedInventoryItemCount_DEPRECATED;

	UPROPERTY(Transient)
	float ItemRate_DEPRECATED;

	UPROPERTY(BlueprintAssignable)
	FAMFGBuildableIntelliSplitterOnStateChanged OnStateChangedEvent;

private:

	std::array<int32, NUM_OUTPUTS> ItemsPerCycle;

	std::array<float, NUM_OUTPUTS> BlockedFor;

	std::array<int32, NUM_OUTPUTS> AssignedItems;

	std::array<int32, NUM_OUTPUTS> GrabbedItems;

	std::array<float, NUM_OUTPUTS> PriorityStepSize;

	std::array<int32, MAX_INVENTORY_SIZE> AssignedOutputs;

	std::array<int32, NUM_OUTPUTS> NextInventorySlot;

	std::array<int32, NUM_OUTPUTS> InventorySlotEnd;

	bool bBalancingRequired;

	bool bNeedsInitialDistributionSetup;

	float CycleTime;

	int32 ReallyGrabbed;

	FTimerHandle ReplicationTimer;

};




