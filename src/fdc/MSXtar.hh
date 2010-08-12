// $Id$

// This code implements the functionality of my older msxtar program
// that could manipulate files and directories on dsk and ide-hd images
// Integrating it is seen as temporary bypassing of the need for a
// DirAsDisk2 that supports subdirs, partitions etc. since this class will
// of those functionalities although not on a dynamic base

#ifndef MSXTAR_HH
#define MSXTAR_HH

#include "MemBuffer.hh"
#include "openmsx.hh"
#include "noncopyable.hh"
#include <string>

namespace openmsx {

class SectorAccessibleDisk;

class MSXtar : private noncopyable
{
public:
	explicit MSXtar(SectorAccessibleDisk& disk);
	~MSXtar();

	void chdir(const std::string& newRootDir);
	void mkdir(const std::string& newRootDir);
	std::string dir();
	std::string addFile(const std::string& Filename);
	std::string addDir(const std::string& rootDirName);
	std::string getItemFromDir(const std::string& rootDirName,
	                           const std::string& itemName);
	void getDir(const std::string& rootDirName);

private:
	struct MSXBootSector {
		byte jumpcode[3];	// 0xE5 to bootprogram
		byte name[8];
		byte bpsector[2];	// bytes per sector (always 512)
		byte spcluster[1];	// sectors per cluster (always 2)
		byte reservedsectors[2];// amount of non-data sectors (ex bootsector)
		byte nrfats[1];		// number of fats
		byte direntries[2];	// max number of files in root directory
		byte nrsectors[2];	// number of sectors on this disk
		byte descriptor[1];	// media descriptor
		byte sectorsfat[2];	// sectors per FAT
		byte sectorstrack[2];	// sectors per track
		byte nrsides[2];	// number of sides
		byte hiddensectors[2];	// not used
		byte bootprogram[512-30];// actual bootprogram
	};

	struct MSXDirEntry {
		char filename[8];
		char ext[3];
		byte attrib;
		byte reserved[10];	// unused
		byte time[2];
		byte date[2];
		byte startcluster[2];
		byte size[4];
	};

	struct DirEntry {
		unsigned sector;
		unsigned index;
	};


	void writeLogicalSector(unsigned sector, const byte* buf);
	void readLogicalSector (unsigned sector,       byte* buf);

	unsigned clusterToSector(unsigned cluster);
	unsigned sectorToCluster(unsigned sector);
	void parseBootSector(const byte* buf);
	unsigned readFAT(unsigned clnr) const;
	void writeFAT(unsigned clnr, unsigned val);
	unsigned findFirstFreeCluster();
	unsigned findUsableIndexInSector(unsigned sector);
	unsigned getNextSector(unsigned sector);
	unsigned getStartCluster(const MSXDirEntry& entry);
	unsigned appendClusterToSubdir(unsigned sector);
	DirEntry addEntryToDir(unsigned sector);
	std::string makeSimpleMSXFileName(const std::string& fullfilename);
	unsigned addSubdir(const std::string& msxName,
	                   unsigned t, unsigned d, unsigned sector);
	void alterFileInDSK(MSXDirEntry& msxdirentry, const std::string& hostName);
	unsigned addSubdirToDSK(const std::string& hostName,
	                   const std::string& msxName, unsigned sector);
	DirEntry findEntryInDir(const std::string& name, unsigned sector,
	                        byte* sectorbuf);
	std::string addFileToDSK(const std::string& hostName, unsigned sector);
	std::string recurseDirFill(const std::string& dirName, unsigned sector);
	std::string condensName(MSXDirEntry& direntry);
	void changeTime(std::string resultFile, MSXDirEntry& direntry);
	void fileExtract(std::string resultFile, MSXDirEntry& direntry);
	void recurseDirExtract(const std::string& dirName, unsigned sector);
	std::string singleItemExtract(const std::string& dirName,
	                              const std::string& itemName, unsigned sector);
	void chroot(const std::string& newRootDir, bool createDir);


	SectorAccessibleDisk& disk;
	MemBuffer<byte> fatBuffer;

	unsigned maxCluster;
	unsigned sectorsPerCluster;
	unsigned sectorsPerFat;
	unsigned nbSectorsPerCluster;
	unsigned rootDirStart; // first sector from the root directory
	unsigned rootDirLast;  // last  sector from the root directory
	unsigned chrootSector;

	bool fatCacheDirty;
};

} // namespace openmsx

#endif
