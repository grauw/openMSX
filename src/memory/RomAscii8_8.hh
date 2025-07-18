#ifndef ROMASCII8_8_HH
#define ROMASCII8_8_HH

#include "RomBlocks.hh"

#include <array>
#include <cstdint>

namespace openmsx {

class RomAscii8_8 final : public Rom8kBBlocks
{
public:
	enum class SubType : uint8_t { ASCII8_8, KOEI_8, KOEI_32, WIZARDRY, ASCII8_2, ASCII8_32 };
	RomAscii8_8(const DeviceConfig& config,
	            Rom&& rom, SubType subType);

	void reset(EmuTime time) override;
	[[nodiscard]] byte readMem(uint16_t address, EmuTime time) override;
	void writeMem(uint16_t address, byte value, EmuTime time) override;
	[[nodiscard]] const byte* getReadCacheLine(uint16_t address) const override;
	[[nodiscard]] byte* getWriteCacheLine(uint16_t address) override;

	template<typename Archive>
	void serialize(Archive& ar, unsigned version);

private:
	const byte sramEnableBit;
	const byte sramPages;
	byte sramEnabled;
	std::array<byte, NUM_BANKS> sramBlock;
};

} // namespace openmsx

#endif
