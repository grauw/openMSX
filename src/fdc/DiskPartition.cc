// $Id$

#include "DiskPartition.hh"
#include "DiskImageUtils.hh"
#include "Filename.hh"
#include "MSXException.hh"
#include "StringOp.hh"
#include "endian.hh"
#include <cassert>

namespace openmsx {

using namespace DiskImageUtils;

static DiskName getDiskName(SectorAccessibleDisk* disk, unsigned partition)
{
	if (auto dsk = dynamic_cast<Disk*>(disk)) {
		return DiskName(dsk->getName().getFilename(),
		                StringOp::Builder() << ':' << partition);
	} else {
		return DiskName(Filename("dummy"));
	}
}

DiskPartition::DiskPartition(SectorAccessibleDisk& disk, unsigned partition,
                             const std::shared_ptr<SectorAccessibleDisk>& owned_)
	: SectorBasedDisk(getDiskName(&disk, partition))
	, parent(disk)
	, owned(owned_)
{
	assert(!owned.get() || (owned.get() == &disk));

	if (partition == 0) {
		start = 0;
		setNbSectors(disk.getNbSectors());
	} else {
		checkValidPartition(disk, partition); // throws
		PartitionTable pt;
		disk.readSector(0, reinterpret_cast<byte*>(&pt));
		Partition& p = pt.part[31 - partition];
		start = p.start;
		setNbSectors(p.size);
	}
}

DiskPartition::DiskPartition(SectorAccessibleDisk& parent_,
                             size_t start_, size_t length)
	: SectorBasedDisk(getDiskName(nullptr, 0))
	, parent(parent_)
	, start(start_)
{
	setNbSectors(length);
}

void DiskPartition::readSectorImpl(size_t sector, byte* buf)
{
	parent.readSector(start + sector, buf);
}

void DiskPartition::writeSectorImpl(size_t sector, const byte* buf)
{
	parent.writeSector(start + sector, buf);
}

bool DiskPartition::isWriteProtectedImpl() const
{
	return parent.isWriteProtected();
}

} // namespace openmsx
