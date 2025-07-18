#include "RomGeneric16kB.hh"
#include "serialize.hh"

namespace openmsx {

RomGeneric16kB::RomGeneric16kB(const DeviceConfig& config, Rom&& rom_)
	: Rom16kBBlocks(config, std::move(rom_))
{
	reset(EmuTime::dummy());
}

void RomGeneric16kB::reset(EmuTime /*time*/)
{
	setUnmapped(0);
	setRom(1, 0);
	setRom(2, 1);
	setUnmapped(3);
}

void RomGeneric16kB::writeMem(uint16_t address, byte value, EmuTime /*time*/)
{
	setRom(address >> 14, value);
}

byte* RomGeneric16kB::getWriteCacheLine(uint16_t address)
{
	if ((0x4000 <= address) && (address < 0xC000)) {
		return nullptr;
	} else {
		return unmappedWrite.data();
	}
}

REGISTER_MSXDEVICE(RomGeneric16kB, "RomGeneric16kB");

} // namespace openmsx
