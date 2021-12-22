#pragma once

#include <type_traits>


template<typename T>
struct IsEnumBitfield : std::false_type{};


template<typename T>
static constexpr bool IsEnumBitfieldV = IsEnumBitfield<T>::value;


template<typename Enum>
constexpr std::enable_if_t<IsEnumBitfieldV<Enum>, std::underlying_type_t<Enum>> ToBitfieldFlag(Enum Flag)
{
	return static_cast<std::underlying_type_t<Enum>>(1) << static_cast<std::underlying_type_t<Enum>>(Flag);
}


template<typename Enum>
constexpr std::enable_if_t<IsEnumBitfieldV<Enum>, bool> IsSet(std::underlying_type_t<Enum> BitField, Enum Flag)
{
	return BitField & ToBitfieldFlag(Flag);
}


template<typename Enum>
constexpr std::enable_if_t<IsEnumBitfieldV<Enum>, std::underlying_type_t<Enum>> SetFlag(
	std::underlying_type_t<Enum> BitField, Enum Flag, bool Enabled)
{
	return (BitField & ~ToBitfieldFlag(Flag)) | (Enabled * ToBitfieldFlag(Flag));
}


template<typename Enum>
constexpr std::enable_if_t<IsEnumBitfieldV<Enum>, std::underlying_type_t<Enum>> SetFlag(
	std::underlying_type_t<Enum> BitField, Enum Flag)
{
	return BitField | ToBitfieldFlag(Flag);
}


template<typename Enum>
constexpr std::enable_if_t<IsEnumBitfieldV<Enum>, std::underlying_type_t<Enum>> ClearFlag(
	std::underlying_type_t<Enum> BitField, Enum Flag)
{
	return BitField & ~ToBitfieldFlag(Flag);
}


template<typename Enum>
constexpr std::enable_if_t<IsEnumBitfieldV<Enum>, std::underlying_type_t<Enum>> ToggleFlag(
	std::underlying_type_t<Enum> BitField, Enum Flag)
{
	return BitField ^ ToBitfieldFlag(Flag);
}


constexpr static int32 PowConstexpr(int32 Base, int32 Exponent)
{
	int32 Result = 1;

	while (Exponent-- > 0)
	{
		Result *= Base;
	}

	return Result;
}




