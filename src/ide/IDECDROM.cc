#include "IDECDROM.hh"

#include "CommandException.hh"
#include "DeviceConfig.hh"
#include "FileContext.hh"
#include "FileException.hh"
#include "MSXCliComm.hh"
#include "TclObject.hh"
#include "serialize.hh"

#include "endian.hh"
#include "narrow.hh"
#include "one_of.hh"
#include "xrange.hh"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <memory>

namespace openmsx {

std::shared_ptr<IDECDROM::CDInUse> IDECDROM::getDrivesInUse(MSXMotherBoard& motherBoard)
{
	return motherBoard.getSharedStuff<CDInUse>("cdInUse");
}

IDECDROM::IDECDROM(const DeviceConfig& config)
	: AbstractIDEDevice(config.getMotherBoard())
	, name("cdX")
	, devName(config.getChildData("name", "openMSX CD-ROM"))
{
	cdInUse = getDrivesInUse(getMotherBoard());

	unsigned id = 0;
	while ((*cdInUse)[id]) {
		++id;
		if (id == MAX_CD) {
			throw MSXException("Too many CDs");
		}
	}
	name[2] = char('a' + id);
	(*cdInUse)[id] = true;
	cdxCommand.emplace(
		getMotherBoard().getCommandController(),
		getMotherBoard().getStateChangeDistributor(),
		getMotherBoard().getScheduler(), *this);

	senseKey = 0;

	remMedStatNotifEnabled = false;
	mediaChanged = false;

	byteCountLimit = 0; // avoid UMR in serialize()
	transferOffset = 0;
	readSectorData = false;

	getMotherBoard().registerMediaProvider(name, *this);
	getMotherBoard().getMSXCliComm().update(CliComm::UpdateType::HARDWARE, name, "add");
}

IDECDROM::~IDECDROM()
{
	getMotherBoard().unregisterMediaProvider(*this);
	getMotherBoard().getMSXCliComm().update(CliComm::UpdateType::HARDWARE, name, "remove");

	unsigned id = name[2] - 'a';
	assert((*cdInUse)[id]);
	(*cdInUse)[id] = false;
}

void IDECDROM::getMediaInfo(TclObject& result)
{
	result.addDictKeyValue("target", file.is_open() ? file.getURL() : std::string_view{});
}

void IDECDROM::setMedia(const TclObject& info, EmuTime /*time*/)
{
	auto target = info.getOptionalDictValue(TclObject("target"));
	if (!target) return;

	if (auto trgt = target->getString(); trgt.empty()) {
		eject();
	} else {
		insert(std::string(trgt));
	}
}

bool IDECDROM::isPacketDevice()
{
	return true;
}

std::string_view IDECDROM::getDeviceName()
{
	return devName;
}

void IDECDROM::fillIdentifyBlock(AlignedBuffer& buf)
{
	// 1... ....: removable media
	// .10. ....: fast handling of packet command (immediate, in fact)
	// .... .1..: incomplete response:
	//            fields that depend on medium are undefined
	// .... ..00: support for 12-byte packets
	buf[0 * 2 + 0] = 0xC4;
	// 10.. ....: ATAPI
	// ...0 0101: CD-ROM device
	buf[0 * 2 + 1] = 0x85;

	// ...1 ....: Removable Media Status Notification feature set supported
	buf[ 83 * 2 + 0] = 0x10;
	// ...1 ....: Removable Media Status Notification feature set enabled
	buf[ 86 * 2 + 0] = remMedStatNotifEnabled * 0x10;
	// .... ..01: Removable Media Status Notification feature set supported (again??)
	buf[127 * 2 + 0] = 0x01;
}

unsigned IDECDROM::readBlockStart(AlignedBuffer& buf, unsigned count)
{
	assert(readSectorData);
	if (file.is_open()) {
		//fprintf(stderr, "read sector data at %08X\n", transferOffset);
		file.seek(transferOffset);
		file.read(std::span{buf.data(), count});
		transferOffset += count;
		return count;
	} else {
		//fprintf(stderr, "read sector failed: no medium\n");
		// TODO: Check whether more error flags should be set.
		abortReadTransfer(0);
		return 0;
	}
}

void IDECDROM::readEnd()
{
	setInterruptReason(I_O | C_D);
}

void IDECDROM::writeBlockComplete(AlignedBuffer& buf, unsigned count)
{
	// Currently, packet writes are the only kind of write transfer.
	assert(count == 12);
	(void)count; // avoid warning
	executePacketCommand(buf);
}

void IDECDROM::executeCommand(uint8_t cmd)
{
	switch (cmd) {
	case 0xA0: // Packet Command (ATAPI)
		// Determine packet size for data packets.
		byteCountLimit = getByteCount();
		//fprintf(stderr, "ATAPI Command, byte count limit %04X\n",
		//	byteCountLimit);

		// Prepare to receive the command.
		startWriteTransfer(12);
		setInterruptReason(C_D);
		break;

	case 0xDA: // ATA Get Media Status
		if (remMedStatNotifEnabled) {
			setError(0);
		} else {
			// na WP MC na MCR ABRT NM obs
			uint8_t err = 0;
			if (file.is_open()) {
				err |= 0x40; // WP (write protected)
			} else {
				err |= 0x02; // NM (no media inserted)
			}
			// MCR (media change request) is not yet supported
			if (mediaChanged) {
				err |= 0x20; // MC (media changed)
				mediaChanged = false;
			}
			//fprintf(stderr, "Get Media status: %02X\n", err);
			setError(err);
		}
		break;

	case 0xEF: // Set Features
		switch (getFeatureReg()) {
		case 0x31: // Disable Media Status Notification.
			remMedStatNotifEnabled = false;
			break;
		case 0x95: // Enable  Media Status Notification
			setLBAMid(0x00); // version
			// .... .0..: capable of physically ejecting media
			// .... ..0.: capable of locking the media
			// .... ...X: previous enabled state
			setLBAHigh(remMedStatNotifEnabled);
			remMedStatNotifEnabled = true;
			break;
		default: // other subcommands handled by base class
			AbstractIDEDevice::executeCommand(cmd);
		}
		break;

	default: // all others
		AbstractIDEDevice::executeCommand(cmd);
	}
}

void IDECDROM::startPacketReadTransfer(unsigned count)
{
	// TODO: Recompute for each packet.
	// TODO: Take even/odd stuff into account.
	// Note: The spec says maximum byte count is 0xFFFE, but I prefer
	//       powers of two, so I'll use 0x8000 instead (the device is
	//       free to set limitations of its own).
	unsigned packetSize = 512; /*std::min(
		byteCountLimit, // limit from user
		std::min(sizeof(buffer), 0x8000u) // device and spec limit
		);*/
	unsigned size = std::min(packetSize, count);
	setByteCount(size);
	setInterruptReason(I_O);
}

void IDECDROM::executePacketCommand(AlignedBuffer& packet)
{
	// It seems that unlike ATA which uses words at the basic data unit,
	// ATAPI uses bytes.
	//fprintf(stderr, "ATAPI Packet:");
	//for (auto i : xrange(12)) {
	//	fprintf(stderr, " %02X", packet[i]);
	//}
	//fprintf(stderr, "\n");

	readSectorData = false;

	switch (packet[0]) {
	case 0x03: { // REQUEST SENSE Command
		// TODO: Find out what the purpose of the allocation length is.
		//       In practice, it seems to be 18, which is the amount we want
		//       to return, but what if it would be different?
		//int allocationLength = packet[4];
		//fprintf(stderr, "  request sense: %d bytes\n", allocationLength);

		const int byteCount = 18;
		startPacketReadTransfer(byteCount);
		auto& buf = startShortReadTransfer(byteCount);
		for (auto i : xrange(byteCount)) {
			buf[i] = 0x00;
		}
		buf[ 0] = 0xF0;
		buf[ 2] = narrow_cast<uint8_t>((senseKey >> 16) & 0xFF); // sense key
		buf[12] = narrow_cast<uint8_t>((senseKey >>  8) & 0xFF); // ASC
		buf[13] = narrow_cast<uint8_t>((senseKey >>  0) & 0xFF); // ASQ
		buf[ 7] = byteCount - 7;
		senseKey = 0;
		break;
	}
	case 0x43: { // READ TOC/PMA/ATIP Command
		//bool msf = packet[1] & 2;
		int format = packet[2] & 0x0F;
		//int trackOrSession = packet[6];
		//int allocLen = (packet[7] << 8) | packet[8];
		//int control = packet[9];
		switch (format) {
		case 0: { // TOC
			//fprintf(stderr, "  read TOC: %s addressing, "
			//	"start track %d, allocation length 0x%04X\n",
			//	msf ? "MSF" : "logical block",
			//	trackOrSession, allocLen);
			setError(ABORT);
			break;
		}
		case 1: // Session Info
		case 2: // Full TOC
		case 3: // PMA
		case 4: // ATIP
		default:
			fprintf(stderr, "  read TOC: format %d not implemented\n", format);
			setError(ABORT);
		}
		break;
	}
	case 0xA8: { // READ Command
		uint32_t sectorNumber = Endian::read_UA_B32(&packet[2]);
		uint32_t sectorCount  = Endian::read_UA_B32(&packet[6]);
		//fprintf(stderr, "  read(12): sector %d, count %d\n",
		//	sectorNumber, sectorCount);
		// There are three block sizes:
		// - byteCountLimit: set by the host
		//     maximum block size for transfers
		// - byteCount: determined by the device
		//     actual block size for transfers
		// - transferCount wrap: emulation thingy
		//     transparent to host
		//fprintf(stderr, "byte count limit: %04X\n", byteCountLimit);
		//unsigned byteCount = sectorCount * 2048;
		//unsigned byteCount = sizeof(buffer);
		//unsigned byteCount = packetSize;
		/*
		if (byteCount > byteCountLimit) {
			byteCount = byteCountLimit;
		}
		if (byteCount > 0xFFFE) {
			byteCount = 0xFFFE;
		}
		*/
		//fprintf(stderr, "byte count: %04X\n", byteCount);
		readSectorData = true;
		transferOffset = sectorNumber * 2048;
		unsigned count = sectorCount * 2048;
		startPacketReadTransfer(count);
		startLongReadTransfer(count);
		break;
	}
	default:
		fprintf(stderr, "  unknown packet command 0x%02X\n", packet[0]);
		setError(ABORT);
	}
}

void IDECDROM::eject()
{
	file.close();
	mediaChanged = true;
	senseKey = 0x06 << 16; // unit attention (medium changed)
	getMotherBoard().getMSXCliComm().update(CliComm::UpdateType::MEDIA, name, {});
}

void IDECDROM::insert(const std::string& filename)
{
	file = File(filename);
	mediaChanged = true;
	senseKey = 0x06 << 16; // unit attention (medium changed)
	getMotherBoard().getMSXCliComm().update(CliComm::UpdateType::MEDIA, name, filename);
}


// class CDXCommand

CDXCommand::CDXCommand(CommandController& commandController_,
                       StateChangeDistributor& stateChangeDistributor_,
                       Scheduler& scheduler_, IDECDROM& cd_)
	: RecordedCommand(commandController_, stateChangeDistributor_,
	                  scheduler_, cd_.name)
	, cd(cd_)
{
}

void CDXCommand::execute(std::span<const TclObject> tokens, TclObject& result,
                         EmuTime /*time*/)
{
	if (tokens.size() == 1) {
		const auto& file = cd.file;
		result.addListElement(tmpStrCat(cd.name, ':'),
		                      file.is_open() ? file.getURL() : std::string{});
		if (!file.is_open()) result.addListElement("empty");
	} else if ((tokens.size() == 2) && (tokens[1] == one_of("eject", "-eject"))) {
		cd.eject();
		// TODO check for locked tray
		if (tokens[1] == "-eject") {
			result = "Warning: use of '-eject' is deprecated, "
			         "instead use the 'eject' subcommand";
		}
	} else if ((tokens.size() == 2) ||
	           ((tokens.size() == 3) && (tokens[1] == "insert"))) {
		int fileToken = 1;
		if (tokens[1] == "insert") {
			if (tokens.size() > 2) {
				fileToken = 2;
			} else {
				throw CommandException(
					"Missing argument to insert subcommand");
			}
		}
		try {
			std::string filename = userFileContext().resolve(
				tokens[fileToken].getString());
			cd.insert(filename);
		} catch (FileException& e) {
			throw CommandException("Can't change cd image: ",
			                       e.getMessage());
		}
	} else {
		throw CommandException("Too many or wrong arguments.");
	}
}

std::string CDXCommand::help(std::span<const TclObject> /*tokens*/) const
{
	return strCat(
		cd.name, "                   : display the cd image for this CD-ROM drive\n",
		cd.name, " eject             : eject the cd image from this CD-ROM drive\n",
		cd.name, " insert <filename> : change the cd image for this CD-ROM drive\n",
		cd.name, " <filename>        : change the cd image for this CD-ROM drive\n");
}

void CDXCommand::tabCompletion(std::vector<std::string>& tokens) const
{
	using namespace std::literals;
	static constexpr std::array extra = {"eject"sv, "insert"sv};
	completeFileName(tokens, userFileContext(), extra);
}


template<typename Archive>
void IDECDROM::serialize(Archive& ar, unsigned /*version*/)
{
	ar.template serializeBase<AbstractIDEDevice>(*this);

	std::string filename = file.is_open() ? file.getURL() : std::string{};
	ar.serialize("filename", filename);
	if constexpr (Archive::IS_LOADER) {
		// re-insert CD-ROM before restoring 'mediaChanged', 'senseKey'
		if (filename.empty()) {
			eject();
		} else {
			insert(filename);
		}
	}

	ar.serialize("byteCountLimit",         byteCountLimit,
	             "transferOffset",         transferOffset,
	             "senseKey",               senseKey,
	             "readSectorData",         readSectorData,
	             "remMedStatNotifEnabled", remMedStatNotifEnabled,
	             "mediaChanged",           mediaChanged);
}
INSTANTIATE_SERIALIZE_METHODS(IDECDROM);
REGISTER_POLYMORPHIC_INITIALIZER(IDEDevice, IDECDROM, "IDECDROM");

} // namespace openmsx
