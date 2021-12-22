#include "Buildables/MFGBuildableIntelliSplitter.h"

#include <numeric>
#include <algorithm>

#include "IntelliSplittersLog.h"
#include "IntelliSplittersModule.h"
#include "FGFactoryConnectionComponent.h"
#include "Buildables/FGBuildableConveyorBase.h"
#include "Subsystem/IntelliSplittersSubsystem.h"


#if INTELLISPLITTERS_DEBUG

#define DEBUG_THIS_SPLITTER Debug
#define DEBUG_SPLITTER(splitter) ((splitter).Debug)

#else

#define DEBUG_THIS_SPLITTER false
#define DEBUG_SPLITTER(splitter) false

#endif


template<std::size_t N, typename T>
constexpr auto MakeArray(T Value)->std::array<T, N>
{
	std::array<T, N> R{};

	for (auto& V : R)
	{
		V = Value;
	}

	return R;
}


template<std::size_t N, typename T>
constexpr auto MakeTArray(const T& Value)->TArray<T, TFixedAllocator<N>>
{
	TArray<T, TFixedAllocator<N>> Result;
	Result.Init(Value, N);
	return Result;
}


FMFGBuildableIntelliSplitterReplicatedProperties
	::FMFGBuildableIntelliSplitterReplicatedProperties()
	: TransientState(0)
	, PersistentState(0) // Do the setup in BeginPlay(), otherwise we cannot detect version changes during loading
	, TargetInputRate(0)
	, LeftInCycle(0)
	, CycleLength(0)
	, CachedInventoryItemCount(0)
	, ItemRate(0.0f)
{
	std::fill_n(OutputStates, NUM_OUTPUTS, ToBitfieldFlag(EOutputState::Automatic));
	std::fill_n(OutputRates, NUM_OUTPUTS, ToBitfieldFlag(EOutputState::Automatic));
}


AMFGBuildableIntelliSplitter::AMFGBuildableIntelliSplitter()
	: Debug(false)
	, ItemsPerCycle(MakeArray<NUM_OUTPUTS>(0))
	, BlockedFor(MakeArray<NUM_OUTPUTS>(0.0f))
	, AssignedItems(MakeArray<NUM_OUTPUTS>(0))
	, GrabbedItems(MakeArray<NUM_OUTPUTS>(0))
	, PriorityStepSize(MakeArray<NUM_OUTPUTS>(0.0f))
	, bBalancingRequired(true)
	, bNeedsInitialDistributionSetup(true)
	, CycleTime(0.0f)
	, ReallyGrabbed(0)
{
	std::fill_n(LeftInCycleForOutputs, NUM_OUTPUTS, 0);
}


void AMFGBuildableIntelliSplitter::GetLifetimeReplicatedProps(
	TArray< FLifetimeProperty >& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMFGBuildableIntelliSplitter, Replicated);
}


void AMFGBuildableIntelliSplitter::BeginPlay()
{
	// We need to fix the connection wiring before calling into our parent class
	if (HasAuthority())
	{
		if (IsSplitterFlagSet(ETransient::NeedsLoadedSplitterProcessing))
		{
			// Special case for really old and broken splitters created with 0.2.0 and older
			if (IsSplitterFlagSet(EPersistent::NeedsConnectionsFixup))
			{
				FixupConnections();
				Super::BeginPlay();
				return;
			}

			const auto IntelliSplittersSubsystem = AIntelliSplittersSubsystem::Get(this);

			switch (IntelliSplittersSubsystem->GetSerializationVersion())
			{

			case EIntelliSplittersSerializationVersion::Legacy:

				// This should apparently not be reachable

			case EIntelliSplittersSerializationVersion::FixedPrecisionArithmetic:
				
#if UE_BUILD_SHIPPING

				UE_LOG(LogIntelliSplitters, Display,
					TEXT("%s: Upgrading to IntelliSplitter."), *GetName());

				std::copy(OutputStates_DEPRECATED.begin(),
					OutputStates_DEPRECATED.end(), Replicated.OutputStates);
				OutputStates_DEPRECATED.Empty();

				Replicated.PersistentState = PersistentState_DEPRECATED;
				Replicated.TargetInputRate = TargetInputRate_DEPRECATED;

				std::copy(IntegralOutputRates_DEPRECATED.begin(),
					IntegralOutputRates_DEPRECATED.end(), Replicated.OutputRates);
				IntegralOutputRates_DEPRECATED.Empty();

				std::copy(RemainingItems_DEPRECATED.begin(),
					RemainingItems_DEPRECATED.end(), LeftInCycleForOutputs);
				RemainingItems_DEPRECATED.Empty();

#endif

			case EIntelliSplittersSerializationVersion::Initial:

				break;

			default:
				
				UE_LOG(LogIntelliSplitters, Fatal,
					TEXT("IntelliSplitter %s was saved with an unsupported "\
						"serialization version, will be removed."), *GetName());
				FIntelliSplittersModule::Get()->ScheduleDismantle(this);

			}

			Replicated.LeftInCycle = std::accumulate(LeftInCycleForOutputs, LeftInCycleForOutputs + NUM_OUTPUTS, 0);
			Replicated.CycleLength = std::accumulate(ItemsPerCycle.begin(), ItemsPerCycle.end(), 0);

			// Delay item rate calculation to first full cycle when loading the game
			CycleTime = -100000.0;

			SetupDistribution(true);
			bNeedsInitialDistributionSetup = false;
			ClearSplitterFlag(ETransient::NeedsLoadedSplitterProcessing);
		}

		Super::BeginPlay();
		SetSplitterVersion(VERSION);
		bBalancingRequired = true;
	}
	else
	{
		Super::BeginPlay();
	}
}


void AMFGBuildableIntelliSplitter::PostLoadGame_Implementation(
	int32 saveVersion, int32 gameVersion)
{
	Super::PostLoadGame_Implementation(saveVersion, gameVersion);

	if (!HasAuthority())
	{
		UE_LOG(LogIntelliSplitters, Warning,
			TEXT("[PostLoadGame_Implementation] Called without authority."));
		return;
	}

	TInlineComponentArray<UFGFactoryConnectionComponent*, 6> Connections;
	GetComponents(Connections);

	if (Connections.Num() > 4)
	{
		UE_LOG(LogIntelliSplitters, Warning,
			TEXT("%s: Ancient splitter created with 0.2.0 or older."), *GetName());
		SetSplitterFlag(EPersistent::NeedsConnectionsFixup);
	}

	SetSplitterFlag(ETransient::NeedsLoadedSplitterProcessing);

	FIntelliSplittersModule::Get()->OnSplitterLoadedFromSaveGame(this);
}


UClass* AMFGBuildableIntelliSplitter::GetReplicationDetailActorClass() const
{
	return Super::GetReplicationDetailActorClass();
}


void AMFGBuildableIntelliSplitter::Factory_Tick(float dt)
{
	if (!HasAuthority())
	{
		return;
	}

	// Keep outputs from pulling while we're in here
	NextInventorySlot = MakeArray<NUM_OUTPUTS>(MAX_INVENTORY_SIZE);

	// Skip direct splitter base class, it doesn't do anything useful for us
	AFGBuildableConveyorAttachment::Factory_Tick(dt);

	if (DEBUG_THIS_SPLITTER)
	{
		UE_LOG(LogIntelliSplitters, Display,
			TEXT("[Factory_Tick] Transient = %d / Persistent = %d / "\
				"CycleLength = %d / LeftInCycle = %d / "\
				"OutputStates = (%d, %d, %d) / Remaining = (%d, %d, %d)."),
			Replicated.TransientState, Replicated.PersistentState,
			Replicated.CycleLength, Replicated.LeftInCycle,
			Replicated.OutputStates[0], Replicated.OutputStates[1],
			Replicated.OutputStates[2], LeftInCycleForOutputs[0],
			LeftInCycleForOutputs[1], LeftInCycleForOutputs[2]);

		UE_LOG(LogIntelliSplitters, Display,
			TEXT("[Factory_Tick] TargetInput = %d / "\
				"OutputRates = (%d, %d, %d) / ItemsPerCycle = (%d, %d, %d)."),
			Replicated.TargetInputRate, Replicated.OutputRates[0],
			Replicated.OutputRates[1], Replicated.OutputRates[2],
			ItemsPerCycle[0], ItemsPerCycle[1], ItemsPerCycle[2]);
	}

	if (bNeedsInitialDistributionSetup)
	{
		SetupInitialDistributionState();
	}

	if (bBalancingRequired)
	{
		auto [Valid, _] = ServerBalanceNetwork(this, true);

		if (!Valid)
		{
			return;
		}
	}

	for (int i = 0; i < NUM_OUTPUTS; ++i)
	{
		Replicated.LeftInCycle -= GrabbedItems[i];
		GrabbedItems[i] = 0;
		AssignedItems[i] = 0;
	}

	InventorySlotEnd = MakeArray<NUM_OUTPUTS>(0);
	AssignedOutputs = MakeArray<MAX_INVENTORY_SIZE>(-1);

	if (Replicated.TargetInputRate == 0 && mInputs[0]->IsConnected())
	{
		auto [_, Rate, Ready] = FindIntelliSplitterAndMaxBeltRate(mInputs[0], false);
		Replicated.TargetInputRate = Rate;
	}

	int32 Connections = 0;
	bool NeedsBalancing = false;

	for (int32 i = 0; i < NUM_OUTPUTS; ++i)
	{
		const bool Connected = IsSet(Replicated.OutputStates[i], EOutputState::Connected);
		Connections += Connected;

		if (Connected != mOutputs[i]->IsConnected())
		{
			if (DEBUG_THIS_SPLITTER)
			{
				UE_LOG(LogIntelliSplitters, Display,
					TEXT("Connection change in output %d."), i);
			}

			NeedsBalancing = true;
		}
	}

	if (NeedsBalancing)
	{
		bBalancingRequired = true;
		return;
	}

	if (IsSplitterFlagSet(EPersistent::NeedsDistributionSetup))
	{
		SetupDistribution();
	}

	Replicated.CachedInventoryItemCount = 0;
	auto PopulatedInventorySlots = MakeArray<MAX_INVENTORY_SIZE>(-1);

	for (int32 i = 0; i < mInventorySizeX; ++i)
	{
		if (mBufferInventory->IsSomethingOnIndex(i))
		{
			PopulatedInventorySlots[Replicated.CachedInventoryItemCount++] = i;
		}
	}

	if (Connections == 0 || Replicated.CachedInventoryItemCount == 0)
	{
		CycleTime += dt;
		return;
	}

	if (Replicated.LeftInCycle < -40)
	{
		UE_LOG(LogIntelliSplitters, Warning,
			TEXT("LeftInCycle too negative (%d), resetting."), Replicated.LeftInCycle);
		PrepareCycle(false, true);
	}
	else if (Replicated.LeftInCycle <= 0)
	{
		PrepareCycle(true);
	}

	CycleTime += dt;

	auto AssignableItems = MakeArray<NUM_OUTPUTS>(0);

	auto LocalNextInventorySlot = MakeArray<NUM_OUTPUTS>(MAX_INVENTORY_SIZE);

	for (int32 ActiveSlot = 0; ActiveSlot
		< Replicated.CachedInventoryItemCount; ++ActiveSlot)
	{
		if (DEBUG_THIS_SPLITTER)
		{
			UE_LOG(LogIntelliSplitters, Display, TEXT("Slot = %d."), ActiveSlot);
		}

		int32 Next = -1;
		float Priority = -INFINITY;

		for (int32 i = 0; i < NUM_OUTPUTS; ++i)
		{
			/**
			 * Adding the grabbed items in the next line de-skews the algorithm
			 * if the output has been penalized for an earlier inventory slot
			 */
			
			AssignableItems[i] = LeftInCycleForOutputs[i] - AssignedItems[i] + GrabbedItems[i];
			const auto ItemPriority = AssignableItems[i] * PriorityStepSize[i];

			if (AssignableItems[i] > 0 && ItemPriority > Priority)
			{
				Next = i;
				Priority = ItemPriority;
			}
		}

		if (Next < 0)
		{
			break;
		}

		std::array<bool, NUM_OUTPUTS> Penalized = { false, false, false };

		while (IsOutputBlocked(Next) && Next >= 0)
		{
			if (DEBUG_THIS_SPLITTER)
			{
				UE_LOG(LogIntelliSplitters, Display,
					TEXT("Output %d is blocked, reassigning item and penalizing output."), Next);
			}

			Penalized[Next] = true;

			--LeftInCycleForOutputs[Next];
			++AssignableItems[Next];

			// This is a lie but will cause the correct update of LeftInCycle during next tick
			++GrabbedItems[Next];

			--AssignableItems[Next];
			Priority = -INFINITY;

			Next = -1;

			for (int32 i = 0; i < NUM_OUTPUTS; ++i)
			{
				if (Penalized[i] || AssignableItems[i] <= 0)
				{
					continue;
				}

				const auto ItemPriority = AssignableItems[i] * PriorityStepSize[i];

				if (ItemPriority > Priority)
				{
					Next = i;
					Priority = ItemPriority;
				}
			}
		}

		if (Next >= 0)
		{
			const auto Slot = PopulatedInventorySlots[ActiveSlot];

			if (DEBUG_THIS_SPLITTER)
			{
				UE_LOG(LogIntelliSplitters, Display,
					TEXT("Slot %d -> actual slot %d."), ActiveSlot, Slot);
			}

			AssignedOutputs[Slot] = Next;

			if (LocalNextInventorySlot[Next] == MAX_INVENTORY_SIZE)
			{
				LocalNextInventorySlot[Next] = Slot;
			}

			InventorySlotEnd[Next] = Slot + 1;
			++AssignedItems[Next];
		}
		else if (DEBUG_THIS_SPLITTER)
		{
			UE_LOG(LogIntelliSplitters, Warning,
				TEXT("All eligible outputs blocked, cannot assign item."));
		}
	}

	for (int32 i = 0; i < NUM_OUTPUTS; ++i)
	{
		/**
		 * Checking for GrabbedItems seems weird, but that catches
		 * stuck outputs that have been penalized
		 */

		if ((AssignedItems[i] > 0 || GrabbedItems[i] > 0))
		{
			BlockedFor[i] += dt;
		}
	}

	if (DEBUG_THIS_SPLITTER)
	{
		UE_LOG(LogIntelliSplitters, Display,
			TEXT("[Factory_Tick] Assigned imtes (jammed): "\
				"0 = %d (%f) / 1 = %d (%f) / 2 = %d (%f)."),
			AssignedItems[0], BlockedFor[0],
			AssignedItems[1], BlockedFor[1],
			AssignedItems[2], BlockedFor[2]);
	}

	// Make new items available to outputs
	NextInventorySlot = LocalNextInventorySlot;
}


bool AMFGBuildableIntelliSplitter::Factory_GrabOutput_Implementation(
	UFGFactoryConnectionComponent* connection, FInventoryItem& out_item,
	float& out_OffsetBeyond, TSubclassOf< UFGItemDescriptor > type)
{
	if (!HasAuthority())
	{
		UE_LOG(LogIntelliSplitters, Warning,
			TEXT("[Factory_GrabOutput] Called without authority."));
		return false;
	}

	int32 Output = -1;

	for (int32 i = 0; i < NUM_OUTPUTS; ++i)
	{
		if (connection == mOutputs[i])
		{
			Output = i;
			break;
		}
	}

	if (Output < 0)
	{
		UE_LOG(LogIntelliSplitters, Error,
			TEXT("[Factory_GrabOutput] Could not find connection!."));
		return false;
	}

	BlockedFor[Output] = 0.0;

	if (AssignedItems[Output] <= GrabbedItems[Output])
	{
		if (!IsSet(Replicated.OutputStates[Output], EOutputState::Connected))
		{
			bBalancingRequired = true;
		}

		return false;
	}

	for (int32 Slot = NextInventorySlot[Output]; Slot < InventorySlotEnd[Output]; ++Slot)
	{
		if (AssignedOutputs[Slot] == Output)
		{
			if (Slot > 8)
			{
				UE_LOG(LogIntelliSplitters, Warning,
					TEXT("[Factory_GrabOutput] Hit invalid slot %d for output %d."), Slot, Output);
			}

			FInventoryStack Stack;
			mBufferInventory->GetStackFromIndex(Slot, Stack);
			mBufferInventory->RemoveAllFromIndex(Slot);

			out_item = Stack.Item;
			out_OffsetBeyond = GrabbedItems[Output] * AFGBuildableConveyorBase::ITEM_SPACING;

			++GrabbedItems[Output];
			--LeftInCycleForOutputs[Output];
			++ReallyGrabbed;
			NextInventorySlot[Output] = Slot + 1;

			if (DEBUG_THIS_SPLITTER)
			{
				UE_LOG(LogIntelliSplitters, Display,
					TEXT("[Factory_GrabOutput] Sent item out of output %d."), Output);
			}

			return true;
		}
	}

	UE_LOG(LogIntelliSplitters, Warning,
		TEXT("[Factory_GrabOutput] Output %d: No valid output found, "\
			"this should not happen!."), Output);

	if (!IsSet(Replicated.OutputStates[Output], EOutputState::Connected))
	{
		bBalancingRequired = true;
	}

	return false;
}


void AMFGBuildableIntelliSplitter::FillDistributionTable(float dt)
{
	// We are doing our own distribution management, as we need to track
	// whether assigned items were actually picked up by the outputs
}


UIntelliSplittersRCO* AMFGBuildableIntelliSplitter::RCO() const
{
	return UIntelliSplittersRCO::Get(GetWorld());
}


void AMFGBuildableIntelliSplitter::ServerEnableReplication(float Duration)
{
	if (!HasAuthority())
	{
		UE_LOG(LogIntelliSplitters, Warning,
			TEXT("[ServerEnableReplication] Not a server, skipping."));
		return;
	}

	UE_LOG(LogIntelliSplitters, Display,
		TEXT("Enabling full data replication for IntelliSplitter %p."), this);

	SetSplitterFlag(ETransient::IsReplicationEnabled);
	SetNetDormancy(DORM_Awake);
	ForceNetUpdate();

	GetWorldTimerManager().SetTimer(ReplicationTimer, this,
		&AMFGBuildableIntelliSplitter::ServerReplicationEnabledTimeout,
		Duration, false);
}


bool AMFGBuildableIntelliSplitter::ServerSetTargetRateAutomatic(bool bAutomatic)
{
	if (bAutomatic == !IsSplitterFlagSet(EPersistent::ManualInputRate))
	{
		return true;
	}

	SetSplitterFlag(EPersistent::ManualInputRate, !bAutomatic);

	auto [Valid, _] = ServerBalanceNetwork(this);

	if (!Valid)
	{
		SetSplitterFlag(EPersistent::ManualInputRate, bAutomatic);
		return false;
	}

	OnStateChangedEvent.Broadcast(this);

	return true;
}


bool AMFGBuildableIntelliSplitter::ServerSetTargetInputRate(float Rate)
{
	if (Rate < 0)
	{
		return false;
	}

	if (IsTargetRateAutomatic())
	{
		return false;
	}

	int32 IntRate = static_cast<int32>(Rate * FRACTIONAL_RATE_MULTIPLIER);

	bool Changed = Replicated.TargetInputRate != IntRate;
	Replicated.TargetInputRate = IntRate;

	if (Changed)
	{
		ServerBalanceNetwork(this);
	}

	OnStateChangedEvent.Broadcast(this);

	return true;
}


bool AMFGBuildableIntelliSplitter::ServerSetOutputRate(int32 Output, float Rate)
{
	if (Output < 0 || Output > NUM_OUTPUTS - 1)
	{
		UE_LOG(LogIntelliSplitters, Error, TEXT("Invalid output index: %d."), Output);
		return false;
	}

	auto IntRate = static_cast<int32>(Rate * FRACTIONAL_RATE_MULTIPLIER);

	if (IntRate < 0 || IntRate > 780 * FRACTIONAL_RATE_MULTIPLIER)
	{
		UE_LOG(LogIntelliSplitters, Error,
			TEXT("Invalid output rate: %f (must be between 0 and 780)."), Output);
		return false;
	}

	if (IsSet(Replicated.OutputStates[Output], EOutputState::Automatic))
	{
		UE_LOG(LogIntelliSplitters, Error,
			TEXT("Output %d is automatic, ignoring rate value."), Output);
		return false;
	}

	if (Replicated.OutputRates[Output] == Rate)
	{
		return true;
	}

	int32 OldRate = Replicated.OutputRates[Output];
	Replicated.OutputRates[Output] = IntRate;

	auto [DownstreamIntelliSplitter, _, Ready]
		= FindIntelliSplitterAndMaxBeltRate(mOutputs[Output], true);

	bool OldManualInputRate = false;
	int32 OldTargetInputRate = 0;

	if (DownstreamIntelliSplitter)
	{
		OldManualInputRate = DownstreamIntelliSplitter->IsSplitterFlagSet(EPersistent::ManualInputRate);
		OldTargetInputRate = DownstreamIntelliSplitter->Replicated.TargetInputRate;

		DownstreamIntelliSplitter->SetSplitterFlag(EPersistent::ManualInputRate);
		DownstreamIntelliSplitter->Replicated.TargetInputRate = IntRate;
	}

	auto [Valid, _2] = ServerBalanceNetwork(this);

	if (!Valid)
	{
		Replicated.OutputRates[Output] = OldRate;

		if (DownstreamIntelliSplitter)
		{
			DownstreamIntelliSplitter->SetSplitterFlag(EPersistent::ManualInputRate, OldManualInputRate);
			DownstreamIntelliSplitter->Replicated.TargetInputRate = OldTargetInputRate;
		}
	}

	OnStateChangedEvent.Broadcast(this);

	return Valid;
}


bool AMFGBuildableIntelliSplitter::ServerSetOutputAutomatic(int32 Output, bool bAutomatic)
{
	if (Output < 0 || Output > NUM_OUTPUTS - 1)
	{
		return false;
	}

	if (bAutomatic == IsSet(Replicated.OutputStates[Output], EOutputState::Automatic))
	{
		return true;
	}

	auto [DownstreamIntelliSplitter, _, Ready]
		= FindIntelliSplitterAndMaxBeltRate(mOutputs[Output], true);

	if (DownstreamIntelliSplitter)
	{
		DownstreamIntelliSplitter->SetSplitterFlag(
			EPersistent::ManualInputRate, !bAutomatic);
	}
	else
	{
		Replicated.OutputStates[Output] = SetFlag(
			Replicated.OutputStates[Output], EOutputState::Automatic, bAutomatic);
	}

	auto [Valid, _2] = ServerBalanceNetwork(this);

	if (!Valid)
	{
		Replicated.OutputStates[Output] = SetFlag(
			Replicated.OutputStates[Output], EOutputState::Automatic, !bAutomatic);

		if (DownstreamIntelliSplitter)
		{
			DownstreamIntelliSplitter->SetSplitterFlag(
				EPersistent::ManualInputRate, bAutomatic);
		}

		UE_LOG(LogIntelliSplitters, Warning,
			TEXT("Failed to set output %d to %s."), Output,
			bAutomatic ? TEXT("automatic") : TEXT("manual"));
	}
	else
	{
		UE_LOG(LogIntelliSplitters, Display,
			TEXT("Set output %d to %s."), Output,
			bAutomatic ? TEXT("automatic") : TEXT("manual"));
	}

	OnStateChangedEvent.Broadcast(this);

	return Valid;
}


void AMFGBuildableIntelliSplitter::ServerReplicationEnabledTimeout()
{
	if (!HasAuthority())
	{
		UE_LOG(LogIntelliSplitters, Warning,
			TEXT("[ServerReplicationEnabledTimeout] Not a server, skipping."));
		return;
	}

	UE_LOG(LogIntelliSplitters, Display,
		TEXT("Disabling full data replication for IntelliSplitter %p."), this);

	SetNetDormancy(DORM_DormantAll);
	ClearSplitterFlag(ETransient::IsReplicationEnabled);

	// To get the modified replication state to the clients
	FlushNetDormancy();
}


// UFUNCTION
void AMFGBuildableIntelliSplitter::OnRep_Replicated()
{
	OnStateChangedEvent.Broadcast(this);
}


void AMFGBuildableIntelliSplitter::SetupDistribution(bool bLoadingSave)
{
	if (DEBUG_THIS_SPLITTER)
	{
		UE_LOG(LogIntelliSplitters, Display,
			TEXT("[SetupDistribution] Input = %d / Outputs = (%d, %d, %d)."),
			Replicated.TargetInputRate, Replicated.OutputRates[0],
			Replicated.OutputRates[1], Replicated.OutputRates[2]);
	}

	if (!bLoadingSave)
	{
		for (int32 i = 0; i < NUM_OUTPUTS; ++i)
		{
			Replicated.OutputStates[i] = SetFlag(Replicated.OutputStates[i],
				EOutputState::Connected, mOutputs[i]->IsConnected());
		}
	}

	if (std::none_of(Replicated.OutputStates, Replicated.OutputStates + NUM_OUTPUTS,
		[](auto State) { return IsSet(State, EOutputState::Connected); }))
	{
		std::fill_n(Replicated.OutputRates, NUM_OUTPUTS, FRACTIONAL_RATE_MULTIPLIER);
		std::fill_n(ItemsPerCycle.begin(), NUM_OUTPUTS, 0);
		return;
	}

	// Calculate item counts per cycle
	for (int32 i = 0; i < NUM_OUTPUTS; ++i)
	{
		ItemsPerCycle[i] = IsSet(Replicated.OutputStates[i],
			EOutputState::Connected) * Replicated.OutputRates[i];
	}

#if UE_BUILD_SHIPPING

	const auto GCD = std::accumulate(
		ItemsPerCycle.begin() + 1, ItemsPerCycle.end(),
		ItemsPerCycle[0], [](auto A, auto B) { return std::gcd(A, B); });

#else

	const auto GCD = std::accumulate(
		ItemsPerCycle.begin(), ItemsPerCycle.end(),
		ItemsPerCycle[0], [](auto A, auto B) { return std::gcd(A, B); });

#endif

	if (GCD == 0)
	{
		if (DEBUG_THIS_SPLITTER)
		{
			UE_LOG(LogIntelliSplitters, Display, TEXT("[SetupDistribution] Nothing connected."));
		}

		return;
	}

	for (auto& Item : ItemsPerCycle)
	{
		Item /= GCD;
	}

	Replicated.CycleLength = 0;
	bool Changed = false;

	for (int32 i = 0; i < NUM_OUTPUTS; ++i)
	{
		if (IsSet(Replicated.OutputStates[i], EOutputState::Connected))
		{
			Replicated.CycleLength += ItemsPerCycle[i];
			float StepSize = 0.0f;

			if (ItemsPerCycle[i] > 0)
			{
				StepSize = 1.0f / ItemsPerCycle[i];
			}

			if (PriorityStepSize[i] != StepSize)
			{
				PriorityStepSize[i] = StepSize;
				Changed = true;
			}
		}
		else
		{
			// Disable output
			if (PriorityStepSize[i] != 0)
			{
				PriorityStepSize[i] = 0;
				Changed = true;
			}
		}
	}

	if (Changed && !bLoadingSave)
	{
		std::fill_n(LeftInCycleForOutputs, NUM_OUTPUTS, 0);
		Replicated.LeftInCycle = 0;
		PrepareCycle(false);
	}

	ClearSplitterFlag(EPersistent::NeedsDistributionSetup);
}


void AMFGBuildableIntelliSplitter::PrepareCycle(
	bool bAllowCycleExtension, bool bReset)
{
	if (DEBUG_THIS_SPLITTER)
	{
		UE_LOG(LogIntelliSplitters, Display,
			TEXT("[PrepareCycle] Cycle (%s, %s) / cycleTime = %f / grabbed = %d"),
			bAllowCycleExtension ? TEXT("true") : TEXT("false"),
			bReset ? TEXT("true") : TEXT("false"),
			CycleTime, ReallyGrabbed);
	}

	if (!bReset && CycleTime > 0.0)
	{
		// Update statistics
		if (Replicated.ItemRate > 0.0)
		{
			Replicated.ItemRate = EXPONENTIAL_AVERAGE_WEIGHT * 60 * ReallyGrabbed
				/ CycleTime + (1.0 - EXPONENTIAL_AVERAGE_WEIGHT) * Replicated.ItemRate;
		}
		else
		{
			// Bootstrap
			Replicated.ItemRate = 60.0 * ReallyGrabbed / CycleTime;
		}

		if (bAllowCycleExtension && CycleTime < 2.0)
		{
			if (DEBUG_THIS_SPLITTER)
			{
				UE_LOG(LogIntelliSplitters, Display,
					TEXT("Cycle time too short (%f), doubling cycle length to %d."),
					CycleTime, 2 * Replicated.CycleLength);
			}

			Replicated.CycleLength *= 2;

			for (int i = 0; i < NUM_OUTPUTS; ++i)
			{
				ItemsPerCycle[i] *= 2;
			}
		}
		else if (CycleTime > 10.0)
		{
			bool CanShortenCycle = !(Replicated.CycleLength & 1);

			for (int i = 0; i < NUM_OUTPUTS; ++i)
			{
				CanShortenCycle = CanShortenCycle && !(ItemsPerCycle[i] & 1);
			}

			if (CanShortenCycle)
			{
				if (DEBUG_THIS_SPLITTER)
				{
					UE_LOG(LogIntelliSplitters, Display,
						TEXT("Cycle time too long (%f), halving cycle length to %d."),
						CycleTime, Replicated.CycleLength / 2);
				}

				Replicated.CycleLength /= 2;

				for (int i = 0; i < NUM_OUTPUTS; ++i)
				{
					ItemsPerCycle[i] /= 2;
				}
			}
		}
	}

	CycleTime = 0.0;
	ReallyGrabbed = 0;

	if (bReset)
	{
		Replicated.LeftInCycle = Replicated.CycleLength;

		for (int i = 0; i < NUM_OUTPUTS; ++i)
		{
			if (IsSet(Replicated.OutputStates[i], EOutputState::Connected)
				&& Replicated.OutputRates[i] > 0)
			{
				LeftInCycleForOutputs[i] = ItemsPerCycle[i];
			}
			else
			{
				LeftInCycleForOutputs[i] = 0;
			}
		}
	}
	else
	{
		Replicated.LeftInCycle += Replicated.CycleLength;

		for (int i = 0; i < NUM_OUTPUTS; ++i)
		{
			if (IsSet(Replicated.OutputStates[i], EOutputState::Connected)
				&& Replicated.OutputRates[i] > 0)
			{
				LeftInCycleForOutputs[i] += ItemsPerCycle[i];
			}
			else
			{
				LeftInCycleForOutputs[i] = 0;
			}
		}
	}
}


bool AMFGBuildableIntelliSplitter::IsOutputBlocked(int32 Output) const
{
	return BlockedFor[Output] > BLOCK_DETECTION_THRESHOLD;
}


// UFUNCTION
int32 AMFGBuildableIntelliSplitter::GetFractionalRateDigits()
{
	return FRACTIONAL_RATE_DIGITS;
}


// UFUNCTION
bool AMFGBuildableIntelliSplitter::IsReplicationEnabled() const
{
	return IsSplitterFlagSet(ETransient::IsReplicationEnabled);
}


// UFUNCTION
void AMFGBuildableIntelliSplitter::EnableReplication(float Duration)
{
	if (HasAuthority())
	{
		ServerEnableReplication(Duration);
	}
	else
	{
		UE_LOG(LogIntelliSplitters, Display, TEXT("[EnableReplication] Forwarding to RCO."));
		RCO()->EnableReplication(this, Duration);
	}
}


// UFUNCTION
bool AMFGBuildableIntelliSplitter::IsTargetRateAutomatic() const
{
	return !IsSplitterFlagSet(EPersistent::ManualInputRate);
}


// UFUNCTION
void AMFGBuildableIntelliSplitter::SetTargetRateAutomatic(bool bAutomatic)
{
	if (HasAuthority())
	{
		ServerSetTargetRateAutomatic(bAutomatic);
	}
	else
	{
		UE_LOG(LogIntelliSplitters, Display, TEXT("[SetTargetRateAutomatic] Forwarding to RCO."));
		RCO()->SetTargetRateAutomatic(this, bAutomatic);
	}
}


// UFUNCTION
float AMFGBuildableIntelliSplitter::GetTargetInputRate() const
{
	return Replicated.TargetInputRate * INV_FRACTIONAL_RATE_MULTIPLIER;
}


// UFUNCTION
void AMFGBuildableIntelliSplitter::SetTargetInputRate(float Rate)
{
	if (HasAuthority())
	{
		ServerSetTargetInputRate(Rate);
	}
	else
	{
		UE_LOG(LogIntelliSplitters, Display, TEXT("[SetTargetInputRate] Forwarding to RCO."));
		RCO()->SetTargetInputRate(this, Rate);
	}
}


// UFUNCTION
float AMFGBuildableIntelliSplitter::GetOutputRate(int32 Output) const
{
	if (Output < 0 || Output > NUM_OUTPUTS - 1)
	{
		return NAN;
	}

	return static_cast<float>(Replicated.OutputRates[Output]) * INV_FRACTIONAL_RATE_MULTIPLIER;
}


// UFUNCTION
void AMFGBuildableIntelliSplitter::SetOutputRate(int32 Output, float Rate)
{
	if (HasAuthority())
	{
		ServerSetOutputRate(Output, Rate);
	}
	else
	{
		UE_LOG(LogIntelliSplitters, Display, TEXT("[SetOutputRate] Forwarding to RCO."));
		RCO()->SetOutputRate(this, Output, Rate);
	}
}


// UFUNCTION
void AMFGBuildableIntelliSplitter::SetOutputAutomatic(int32 Output, bool bAutomatic)
{
	if (HasAuthority())
	{
		ServerSetOutputAutomatic(Output, bAutomatic);
	}
	else
	{
		UE_LOG(LogIntelliSplitters, Display, TEXT("[SetOutputAutomatic] Forwarding to RCO."));
		RCO()->SetOutputAutomatic(this, Output, bAutomatic);
	}
}


// UFUNCTION
bool AMFGBuildableIntelliSplitter::IsOutputAutomatic(int32 Output) const
{
	if (Output < 0 || Output > NUM_OUTPUTS)
	{
		return false;
	}

	return IsSet(Replicated.OutputStates[Output], EOutputState::Automatic);
}


// UFUNCTION
bool AMFGBuildableIntelliSplitter::IsOutputIntelliSplitter(int32 Output) const
{
	if (Output < 0 || Output > NUM_OUTPUTS)
	{
		return false;
	}

	return IsSet(Replicated.OutputStates[Output], EOutputState::IntelliSplitter);
}


// UFUNCTION
bool AMFGBuildableIntelliSplitter::IsOutputConnected(int32 Output) const
{
	if (Output < 0 || Output > NUM_OUTPUTS)
	{
		return false;
	}

	return IsSet(Replicated.OutputStates[Output], EOutputState::Connected);
}


// UFUNCTION
void AMFGBuildableIntelliSplitter::BalanceNetwork(bool bRootOnly)
{
	if (HasAuthority())
	{
		ServerBalanceNetwork(this, bRootOnly);
	}
	else
	{
		UE_LOG(LogIntelliSplitters, Display, TEXT("[BalanceNetwork] Forwarding to RCO."));
		RCO()->BalanceNetwork(this, bRootOnly);
	}
}


uint32 AMFGBuildableIntelliSplitter::GetSplitterVersion() const
{
	return Replicated.PersistentState & 0xFFu;
}


// UFUNCTION
int32 AMFGBuildableIntelliSplitter::GetInventorySize() const
{
	return Replicated.CachedInventoryItemCount;
}


// UFUNCTION
float AMFGBuildableIntelliSplitter::GetItemRate() const
{
	return Replicated.ItemRate;
}


// UFUNCTION
bool AMFGBuildableIntelliSplitter::IsDebugSupported()
{

#if INTELLISPLITTERS_DEBUG
	return true;
#else
	return false;
#endif

}


// UFUNCTION
bool AMFGBuildableIntelliSplitter::HasCurrentData()
{
	return HasAuthority() || IsReplicationEnabled();
}


// UFUNCTION
int32 AMFGBuildableIntelliSplitter::GetError() const
{
	return Replicated.TransientState & 0xFFu;
}


void AMFGBuildableIntelliSplitter::SetError(uint8 Error)
{
	Replicated.TransientState = (Replicated.TransientState & ~0xFFu) | static_cast<uint32>(Error);
}


void AMFGBuildableIntelliSplitter::ClearError()
{
	Replicated.TransientState &= ~0xFFu;
}


void AMFGBuildableIntelliSplitter::FixupConnections()
{
	auto Module = FModuleManager::GetModulePtr<FIntelliSplittersModule>("IntelliSplitters");

	TInlineComponentArray<UFGFactoryConnectionComponent*, 6> Connections;
	GetComponents(Connections);

	UE_LOG(LogIntelliSplitters, Error,
		TEXT("%s: Clearing out connection components for "\
			"splitters of previous version to avoid crashing."), *GetName());

#if INTELLISPLITTERS_DEBUG

	TInlineComponentArray<UFGFactoryConnectionComponent*, 6> Partners;
	int32 PartnerCount = 0;

	int32 ii = 0;

	for (auto& C : Connections)
	{
		UFGFactoryConnectionComponent* Partner
			= C->IsConnected() ? C->GetConnection() : nullptr;

		Partners.Add(Partner);
		PartnerCount += Partner != nullptr;

		auto Pos = this->GetTransform()
			.InverseTransformPosition(C->GetComponentLocation());

		auto Rot = this->GetTransform()
			.InverseTransformRotation(C->GetComponentRotation().Quaternion());

		UE_LOG(LogIntelliSplitters, Display,
			TEXT("Splitter %p: Component %d (%s) - %p Connected %s / "\
				"Direction = %s / Global = %s / Pos = %s / Rot = %s."),
			this, ii, *C->GetName(), C,
			C->IsConnected() ? TEXT("true") : TEXT("false"),
			C->GetDirection() == EFactoryConnectionDirection::FCD_INPUT
			? TEXT("Input") : TEXT("Output"),
			*C->GetComponentLocation().ToString(),
			*Pos.ToString(), *Rot.ToString());

		if (Partner)
		{
			Pos = this->GetTransform()
				.InverseTransformPosition(Partner->GetComponentLocation());

			Rot = this->GetTransform()
				.InverseTransformRotation(Partner->GetComponentRotation().Quaternion());

			UE_LOG(LogIntelliSplitters, Display,
				TEXT("Splitter %p: Component %d (%s) - %p Connected %s / "\
					"Direction = %s / Global = %s / Pos = %s / Rot = %s."),
				this, ii, *Partner->GetName(), Partner,
				Partner->IsConnected() ? TEXT("true") : TEXT("false"),
				Partner->GetDirection() == EFactoryConnectionDirection::FCD_INPUT
				? TEXT("Input") : TEXT("Output"),
				*Partner->GetComponentLocation().ToString(),
				*Pos.ToString(), *Rot.ToString());

			auto Belt = Cast<AFGBuildableConveyorBase>(Partner->GetOuterBuildable());

			if (!Belt)
			{
				UE_LOG(LogIntelliSplitters, Error,
					TEXT("Splitter %p: Component %d partner is not a belt, but a %s."),
					this, ii, *Partner->GetOuterBuildable()->StaticClass()->GetName());
			}
			else
			{
				UE_LOG(LogIntelliSplitters, Display,
					TEXT("Splitter %p: Belt Connection0 = %p / Connection1 = %p."),
					this, ii, Belt->GetConnection0(), Belt->GetConnection1());
			}
		}

		++ii;
	}

#endif

	auto& [This, OldBluePrintConnections, ConveyorConnections]
		= Module->PreComponentFixSplitters.Add_GetRef({this, {}, {}});

	for (auto Connection : Connections)
	{
		if (Connection->GetName() == TEXT("Output0")
			|| Connection->GetName() == TEXT("Input0"))
		{
			//log
			UE_LOG(LogIntelliSplitters, Display,
				TEXT("Detaching component %s and scheduling for destruction."),
				*Connection->GetName());

			RemoveOwnedComponent(Connection);
			OldBluePrintConnections.Emplace(Connection);
		}

		if (Connection->IsConnected())
		{
			//log
			UE_LOG(LogIntelliSplitters, Display,
				TEXT("Recording existing connection."));

			ConveyorConnections.Emplace(Connection->GetConnection());
		}
	}

	ClearSplitterFlag(EPersistent::NeedsConnectionsFixup);
}


void AMFGBuildableIntelliSplitter::SetupInitialDistributionState()
{
	auto [InputSplitter, MaxInputRate, Ready]
		= FindIntelliSplitterAndMaxBeltRate(mInputs[0], false);

	Replicated.TargetInputRate = MaxInputRate;

	for (int32 i = 0; i < NUM_OUTPUTS; ++i)
	{
		auto [OutputSplitter, MaxRate, Ready2]
			= FindIntelliSplitterAndMaxBeltRate(mOutputs[i], true);

		if (MaxRate > 0)
		{
			Replicated.OutputRates[i] = FRACTIONAL_RATE_MULTIPLIER;
			Replicated.OutputStates[i] = SetFlag(
				Replicated.OutputStates[i], EOutputState::Connected);
		}
		else
		{
			Replicated.OutputRates[i] = 0;
			Replicated.OutputStates[i] = ClearFlag(
				Replicated.OutputStates[i], EOutputState::Connected);
		}

		Replicated.OutputStates[i] = SetFlag(Replicated.OutputStates[i],
			EOutputState::IntelliSplitter, OutputSplitter != nullptr);
	}

	bNeedsInitialDistributionSetup = false;
	bBalancingRequired = true;
}


std::tuple<bool, int32> AMFGBuildableIntelliSplitter::ServerBalanceNetwork(
	AMFGBuildableIntelliSplitter* ForSplitter, bool bRootOnly)
{
	if (!ForSplitter)
	{
		UE_LOG(LogIntelliSplitters, Error,
			TEXT("BalanceNetwork() must be called with a valid ForSplitter argument, aborting!")
		);
		return { false,-1 };
	}

	if (ForSplitter->IsSplitterFlagSet(EPersistent::NeedsConnectionsFixup)
		|| !ForSplitter->HasActorBegunPlay())
	{
		return { false, -1 };
	}

	TSet<AMFGBuildableIntelliSplitter*> SplitterSet;

	// Start by going upstream
	auto Root = ForSplitter;
	SplitterSet.Add(Root);

	for (auto [Current, Rate, Ready]
		= FindIntelliSplitterAndMaxBeltRate(Root->mInputs[0], false);
		Current; std::tie(Current, Rate, Ready)
		= FindIntelliSplitterAndMaxBeltRate(Current->mInputs[0], false))
	{
		if (Current->IsSplitterFlagSet(EPersistent::NeedsConnectionsFixup)
			|| !Current->HasActorBegunPlay())
		{
			return { false, -1 };
		}

		if (SplitterSet.Contains(Current))
		{
			UE_LOG(LogIntelliSplitters, Warning,
				TEXT("Cycle in auto splitter network detected, canceling."));
			return { false,-1 };
		}

		SplitterSet.Add(Current);
		Root = Current;
	}

	if (bRootOnly && ForSplitter != Root)
	{
		Root->bBalancingRequired = true;
		return { false, -1 };
	}

	const auto& Config = AIntelliSplittersSubsystem::Get(ForSplitter)->GetConfig();

	// Now walk the tree to discover the whole network
	TArray<TArray<FNetworkNode>> Network;
	int32 SplitterCount = 0;

	if (!DiscoverHierarchy(Network, Root, 0, nullptr,
		INT32_MAX, Root, Config.Features.RespectOverclocking))
	{
		Root->bBalancingRequired = true;
		return { false, -1 };
	}

	UE_LOG(LogIntelliSplitters, Display,
		TEXT("[BalanceNetwork] Starting algorithm for root splitter %p (%s)."),
		Root, *Root->GetName());

	for (int32 Level = Network.Num() - 1; Level >= 0; --Level)
	{
		for (auto& Node : Network[Level])
		{
			++SplitterCount;

			auto& Splitter = *Node.Splitter;
			Splitter.bBalancingRequired = false;

			for (int32 i = 0; i < NUM_OUTPUTS; ++i)
			{
				if (Node.MaxOutputRates[i] == 0)
				{
					if (IsSet(Splitter.Replicated.OutputStates[i], EOutputState::Connected))
					{
						Splitter.Replicated.OutputStates[i]
							= ClearFlag(Splitter.Replicated.OutputStates[i], EOutputState::Connected);
						Node.bConnectionStateChanged = true;
					}

					if (IsSet(Splitter.Replicated.OutputStates[i], EOutputState::IntelliSplitter))
					{
						Splitter.Replicated.OutputStates[i]
							= ClearFlag(Splitter.Replicated.OutputStates[i], EOutputState::IntelliSplitter);
						Node.bConnectionStateChanged = true;
					}

					continue;
				}

				if (!IsSet(Splitter.Replicated.OutputStates[i], EOutputState::Connected))
				{
					Splitter.Replicated.OutputStates[i]
						= SetFlag(Splitter.Replicated.OutputStates[i], EOutputState::Connected);
					Node.bConnectionStateChanged = true;
				}

				if (Node.Outputs[i])
				{
					if (!IsSet(Splitter.Replicated.OutputStates[i], EOutputState::IntelliSplitter))
					{
						Splitter.Replicated.OutputStates[i]
							= SetFlag(Splitter.Replicated.OutputStates[i], EOutputState::IntelliSplitter);
						Node.bConnectionStateChanged = true;
					}

					auto& OutputNode = *Node.Outputs[i];
					auto& OutputSplitter = *OutputNode.Splitter;

					if (OutputSplitter.IsSplitterFlagSet(EPersistent::ManualInputRate))
					{
						Splitter.Replicated.OutputStates[i]
							= ClearFlag(Splitter.Replicated.OutputStates[i], EOutputState::Automatic);
						Node.FixedDemand += OutputSplitter.Replicated.TargetInputRate;
					}
					else
					{
						Splitter.Replicated.OutputStates[i]
							= SetFlag(Splitter.Replicated.OutputStates[i], EOutputState::Automatic);
						Node.Shares += OutputNode.Shares;
						Node.FixedDemand += OutputNode.FixedDemand;
					}
				}
				else
				{
					if (IsSet(Splitter.Replicated.OutputStates[i], EOutputState::IntelliSplitter))
					{
						Splitter.Replicated.OutputStates[i]
							= ClearFlag(Splitter.Replicated.OutputStates[i], EOutputState::IntelliSplitter);
						Node.bConnectionStateChanged = true;
					}
					if (IsSet(Splitter.Replicated.OutputStates[i], EOutputState::Automatic))
					{
						Node.Shares += Node.PotentialShares[i];
					}
					else
					{
						Node.FixedDemand += Splitter.Replicated.OutputRates[i];
					}
				}
			}
		}
	}

	// Ok, now the hard part: Distribute the available items

	Network[0][0].AllocatedInputRate = Root->Replicated.TargetInputRate;
	bool Valid = true;

	for (auto& Level : Network)
	{
		if (!Valid)
		{
			break;
		}

		for (auto& Node : Level)
		{
			auto& Splitter = *Node.Splitter;

			if (Node.MaxInputRate < Node.FixedDemand)
			{
				UE_LOG(LogIntelliSplitters, Warning,
					TEXT("Max input rate is not sufficient to satisfy fixed demand: %d < %d."),
					Node.MaxInputRate, Node.FixedDemand);

				Valid = false;
				break;
			}

			int32 AvailableForShares = Node.AllocatedInputRate - Node.FixedDemand;

			if (AvailableForShares < 0)
			{
				UE_LOG(LogIntelliSplitters, Warning,
					TEXT("Not enough available input for requested "\
						"fixed output rates: Demand = %d / Available = %d."),
					Node.FixedDemand, Node.AllocatedInputRate);

				Valid = false;
				break;
			}

			// Avoid division by zero
			auto [RatePerShare, Remainder] = Node.Shares > 0
				? std::div(static_cast<int64>(AvailableForShares)
					* FRACTIONAL_SHARE_MULTIPLIER, Node.Shares)
				: std::lldiv_t{ 0,0 };

			if (Remainder != 0)
			{
				UE_LOG(LogIntelliSplitters, Warning,
					TEXT("Could not evenly distribute rate among shares: "\
						"Available = %d / Shares = %lld / Rate = %lld / Remainder = %lld."),
					AvailableForShares, Node.Shares, RatePerShare, Remainder);
			}

			if (DEBUG_SPLITTER(Splitter))
			{
				UE_LOG(LogIntelliSplitters, Display,
					TEXT("Distribution setup: Input = %d / FixedDemand = %d / "\
						"RatePerShare = %lld / Shares = %lld / Remainder = %lld."),
					Node.AllocatedInputRate, Node.FixedDemand,
					RatePerShare, Node.Shares, Remainder);
			}

			int64 UndistributedShares = 0;
			int64 UndistributedRate = 0;

			for (int32 i = 0; i < NUM_OUTPUTS; ++i)
			{
				if (Node.Outputs[i])
				{
					if (Node.Outputs[i]->Splitter->IsSplitterFlagSet(EPersistent::ManualInputRate))
					{
						Node.AllocatedOutputRates[i]
							= Node.Outputs[i]->Splitter->Replicated.TargetInputRate;
					}
					else
					{
						int64 Rate = RatePerShare * Node.Outputs[i]->Shares;

						if (Remainder > 0)
						{
							auto [ExtraRate, NewUndistributedShares]
								= std::div(UndistributedShares + RatePerShare
									* Node.Outputs[i]->Shares, Remainder);

							UE_LOG(LogIntelliSplitters, Display,
								TEXT("Output %d: Increasing rate from %lld to %lld, "\
									"NewUndistributedShares = %lld."),
								i, Rate, Rate + ExtraRate, NewUndistributedShares);

							UndistributedShares = NewUndistributedShares;
							Rate += ExtraRate;
						}

						auto [ShareBasedRate, OutputRemainder]
							= std::div(Rate, FRACTIONAL_SHARE_MULTIPLIER);

						if (OutputRemainder != 0)
						{
							UE_LOG(LogIntelliSplitters, Warning,
								TEXT("Could not calculate fixed precision output rate for "\
									"output %d (autosplitter): RatePerShare = %lld / "\
									"Shares = %lld / Rate = %lld / Remainder = %lld."),
								i, RatePerShare, Node.Outputs[i]->Shares,
								ShareBasedRate, OutputRemainder);

							UndistributedRate += OutputRemainder;
						}

						Node.AllocatedOutputRates[i] = Node.Outputs[i]->FixedDemand + ShareBasedRate;
					}

					Node.Outputs[i]->AllocatedInputRate = Node.AllocatedOutputRates[i];
				}
				else
				{
					if (IsSet(Splitter.Replicated.OutputStates[i], EOutputState::Connected))
					{
						if (IsSet(Splitter.Replicated.OutputStates[i], EOutputState::Automatic))
						{
							int64 Rate = RatePerShare * Node.PotentialShares[i];

							if (Remainder > 0)
							{
								auto [ExtraRate, NewUndistributedShares] =
									std::div(UndistributedShares + RatePerShare
										* Node.PotentialShares[i], Remainder);

								UE_LOG(LogIntelliSplitters, Display,
									TEXT("Output %d: Increasing rate from %lld to %lld, "\
										"NewUndistributedShares = %lld."),
									i, Rate, Rate + ExtraRate, NewUndistributedShares);

								UndistributedShares = NewUndistributedShares;
								Rate += ExtraRate;
							}

							auto [ShareBasedRate, OutputRemainder]
								= std::div(Rate, FRACTIONAL_SHARE_MULTIPLIER);

							if (OutputRemainder != 0)
							{
								UE_LOG(LogIntelliSplitters, Warning,
									TEXT("Could not calculate fixed precision output rate "\
										"for output %d: RatePerShare = %lld / "\
										"PotentialShares = %lld / Rate = %lld / Remainder = %lld."),
									i, RatePerShare, Node.PotentialShares[i],
									ShareBasedRate, OutputRemainder);

								UndistributedRate += OutputRemainder;
							}

							Node.AllocatedOutputRates[i] = ShareBasedRate;
						}
						else
						{
							Node.AllocatedOutputRates[i] = Splitter.Replicated.OutputRates[i];
						}
					}
				}
			}

			if (UndistributedRate > 0)
			{
				UE_LOG(LogIntelliSplitters, Warning,
					TEXT("%lld units of unallocated distribution rate."), UndistributedRate);
			}

			UE_LOG(LogIntelliSplitters, Display,
				TEXT("Allocated output rates: %d, %d, %d."),
				Node.AllocatedOutputRates[0], Node.AllocatedOutputRates[1],
				Node.AllocatedOutputRates[2]);
		}
	}

	if (!Valid)
	{
		UE_LOG(LogIntelliSplitters, Warning,
			TEXT("Invalid network configuration, aborting network balancing."));

		return { false,SplitterCount };
	}

	// We have a consistent new network setup, now switching network to new settings
	for (auto& Level : Network)
	{
		for (auto& Node : Level)
		{
			auto& Splitter = *Node.Splitter;
			bool NeedsSetupDistribution = Node.bConnectionStateChanged;

			if (Splitter.Replicated.TargetInputRate != Node.AllocatedInputRate)
			{
				NeedsSetupDistribution = true;
				Splitter.Replicated.TargetInputRate = Node.AllocatedInputRate;
			}

			for (int32 i = 0; i < NUM_OUTPUTS; ++i)
			{
				if (IsSet(Splitter.Replicated.OutputStates[i], EOutputState::Connected)
					&& Splitter.Replicated.OutputRates[i] != Node.AllocatedOutputRates[i])
				{
					NeedsSetupDistribution = true;
					Splitter.Replicated.OutputRates[i] = Node.AllocatedOutputRates[i];
				}
			}

			if (NeedsSetupDistribution)
			{
				Splitter.SetSplitterFlag(EPersistent::NeedsDistributionSetup);
			}
		}
	}

	return { true, SplitterCount };
}


std::tuple<AMFGBuildableIntelliSplitter*, int32, bool>
	AMFGBuildableIntelliSplitter::FindIntelliSplitterAndMaxBeltRate(
	UFGFactoryConnectionComponent* Connection, bool bForward)
{
	int32 Rate = INT32_MAX;

	while (Connection->IsConnected())
	{
		Connection = Connection->GetConnection();

		const auto Belt = Cast<AFGBuildableConveyorBase>(Connection->GetOuterBuildable());

		if (Belt)
		{
			Connection = bForward ? Belt->GetConnection1() : Belt->GetConnection0();
			Rate = std::min(Rate, static_cast<int32>(Belt->GetSpeed()) * (FRACTIONAL_RATE_MULTIPLIER / 2));
			continue;
		}

		return { Cast<AMFGBuildableIntelliSplitter>(Connection->GetOuterBuildable()), Rate, true };
	}

	return { nullptr, 0, true };
}


std::tuple<AFGBuildableFactory*, int32, bool>
	AMFGBuildableIntelliSplitter::FindFactoryAndMaxBeltRate(
	UFGFactoryConnectionComponent* Connection, bool bForward)
{
	int32 Rate = INT32_MAX;

	while (Connection->IsConnected())
	{
		Connection = Connection->GetConnection();

		const auto Belt = Cast<AFGBuildableConveyorBase>(Connection->GetOuterBuildable());

		if (Belt)
		{
			Connection = bForward ? Belt->GetConnection1() : Belt->GetConnection0();
			Rate = std::min(Rate, static_cast<int32>(Belt->GetSpeed()) * (FRACTIONAL_RATE_MULTIPLIER / 2));
			continue;
		}

		return { Cast<AFGBuildableFactory>(Connection->GetOuterBuildable()), Rate, true };
	}

	return { nullptr, 0, true };
}


bool AMFGBuildableIntelliSplitter::DiscoverHierarchy(
	TArray<TArray<FNetworkNode>>& Nodes, AMFGBuildableIntelliSplitter* Splitter,
	const int32 Level, FNetworkNode* InputNode, const int32 ChildInParent,
	AMFGBuildableIntelliSplitter* Root, bool bExtractPotentialShares)
{
	if (!Splitter->HasActorBegunPlay())
	{
		return false;
	}

	if (!Nodes.IsValidIndex(Level))
	{
		Nodes.Emplace();
	}

	auto& Node = Nodes[Level][Nodes[Level].Emplace(Splitter, InputNode)];

	if (InputNode)
	{
		InputNode->Outputs[ChildInParent] = &Node;
		Node.MaxInputRate = InputNode->MaxOutputRates[ChildInParent];
	}
	else
	{
		auto [_, MaxRate, Ready] = FindIntelliSplitterAndMaxBeltRate(Splitter->mInputs[0], true);
		Node.MaxInputRate = MaxRate;
	}

	for (int32 i = 0; i < NUM_OUTPUTS; ++i)
	{
		const auto [Downstream, MaxRate, Ready] = FindFactoryAndMaxBeltRate(Splitter->mOutputs[i], true);
		Node.MaxOutputRates[i] = MaxRate;

		if (Downstream)
		{
			const auto DownstreamIntelliSplitter = Cast<AMFGBuildableIntelliSplitter>(Downstream);

			if (DownstreamIntelliSplitter)
			{
				if (!DiscoverHierarchy(Nodes, DownstreamIntelliSplitter,
					Level + 1, &Node, i, Root, bExtractPotentialShares))
				{
					return false;
				}
			}
			else
			{
				if (bExtractPotentialShares)
				{
					Node.PotentialShares[i] = static_cast<int32>(
						Downstream->GetPendingPotential() * FRACTIONAL_SHARE_MULTIPLIER);
				}
				else
				{
					Node.PotentialShares[i] = FRACTIONAL_SHARE_MULTIPLIER;
				}
			}
		}
	}

	return true;
}


void AMFGBuildableIntelliSplitter::SetSplitterVersion(uint32 Version)
{
	if (Version < 1 || Version > 254)
	{
		UE_LOG(LogIntelliSplitters, Error, TEXT("Invalid IntelliSplitter version: %d."), Version);
		return;
	}

	if (Version < GetSplitterVersion())
	{
		UE_LOG(LogIntelliSplitters, Error,
			TEXT("Cannot downgrade IntelliSplitter from version %d to %d."), GetSplitterVersion(), Version);
		return;
	}

	Replicated.PersistentState = (Replicated.PersistentState & ~0xFFu) | (Version & 0xFFu);
}


// FORCEINLINE
bool AMFGBuildableIntelliSplitter::IsSplitterFlagSet(EPersistent Flag) const
{
	return IsSet(Replicated.PersistentState, Flag);
}


// FORCEINLINE
void AMFGBuildableIntelliSplitter::SetSplitterFlag(EPersistent Flag, bool bValue)
{
	Replicated.PersistentState = SetFlag(Replicated.PersistentState, Flag, bValue);
}


// FORCEINLINE
void AMFGBuildableIntelliSplitter::SetSplitterFlag(EPersistent Flag)
{
	Replicated.PersistentState = SetFlag(Replicated.PersistentState, Flag);
}


// FORCEINLINE
void AMFGBuildableIntelliSplitter::ClearSplitterFlag(EPersistent Flag)
{
	Replicated.PersistentState = ClearFlag(Replicated.PersistentState, Flag);
}


// FORCEINLINE
void AMFGBuildableIntelliSplitter::ToggleSplitterFlag(EPersistent Flag)
{
	Replicated.PersistentState = ToggleFlag(Replicated.PersistentState, Flag);
}


// FORCEINLINE
bool AMFGBuildableIntelliSplitter::IsSplitterFlagSet(ETransient Flag) const
{
	return IsSet(Replicated.TransientState, Flag);
}


// FORCEINLINE
void AMFGBuildableIntelliSplitter::SetSplitterFlag(ETransient Flag, bool bValue)
{
	Replicated.TransientState = SetFlag(Replicated.TransientState, Flag, bValue);
}


// FORCEINLINE
void AMFGBuildableIntelliSplitter::SetSplitterFlag(ETransient Flag)
{
	Replicated.TransientState = SetFlag(Replicated.TransientState, Flag);
}


// FORCEINLINE
void AMFGBuildableIntelliSplitter::ClearSplitterFlag(ETransient Flag)
{
	Replicated.TransientState = ClearFlag(Replicated.TransientState, Flag);
}


// FORCEINLINE
void AMFGBuildableIntelliSplitter::ToggleSplitterFlag(ETransient Flag)
{
	Replicated.TransientState = ToggleFlag(Replicated.TransientState, Flag);
}




