// $Id$

#ifndef AMDFLASH_HH
#define AMDFLASH_HH

#include "MemBuffer.hh"
#include "openmsx.hh"
#include "noncopyable.hh"
#include <memory>

namespace openmsx {

class MSXMotherBoard;
class Rom;
class SRAM;
class XMLElement;

class AmdFlash : private noncopyable
{
public:
	AmdFlash(MSXMotherBoard& motherBoard, const Rom& rom,
	         unsigned logSectorSize, unsigned totalSectors,
	         unsigned writeProtectedFlags, const XMLElement& config);
	AmdFlash(MSXMotherBoard& motherBoard, const Rom& rom,
	         unsigned logSectorSize, unsigned totalSectors,
	         unsigned writeProtectedFlags);
	~AmdFlash();

	void reset();

	unsigned getSize() const;
	byte read(unsigned address);
	byte peek(unsigned address) const;
	void write(unsigned address, byte value);
	const byte* getReadCacheLine(unsigned address) const;

	template<typename Archive>
	void serialize(Archive& ar, unsigned version);

//private:
	struct AmdCmd {
		unsigned addr;
		byte value;

		template<typename Archive>
		void serialize(Archive& ar, unsigned version);
	};

	enum State { ST_IDLE, ST_IDENT };

private:
	void init(unsigned totalSectors, unsigned writeProtectedFlags,
	          const XMLElement* config);

	void setState(State newState);
	bool checkCommandEraseSector();
	bool checkCommandEraseChip();
	bool checkCommandProgram();
	bool checkCommandManifacturer();
	bool partialMatch(unsigned len, const byte* dataSeq) const;

	MSXMotherBoard& motherBoard;
	const Rom& rom;
	std::auto_ptr<SRAM> ram;
	MemBuffer<int> writeAddress;
	MemBuffer<const byte*> readAddress;
	const unsigned logSectorSize;
	const unsigned sectorMask;
	const unsigned size;

	static const unsigned MAX_CMD_SIZE = 8;
	AmdCmd cmd[MAX_CMD_SIZE];
	unsigned cmdIdx;
	State state;
};

} // namespace openmsx

#endif

