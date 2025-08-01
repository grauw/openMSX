#include "SensorKid.hh"

#include "CommandController.hh"
#include "MSXCliComm.hh"
#include "MSXException.hh"
#include "StringSetting.hh"
#include "serialize.hh"

#include "narrow.hh"

namespace openmsx {

SensorKid::SensorKid(const DeviceConfig& config)
	: MSXDevice(config)
	, portStatusCallback(getCommandController(),
		getName() + "_port_status_callback",
		"Tcl proc to call when an Sensor Kid port status is changed",
		"", Setting::Save::NO)
	, acquireCallback(getCommandController(),
		getName() + "_acquire_callback",
		"Tcl proc called to acquire analog data. "
		"Input: port number (0-3). "
		"Output: the value for that port (0-255).",
		"", Setting::Save::NO)
{
	reset(getCurrentTime());
}

void SensorKid::reset(EmuTime /*time*/)
{
	prev = 255; // previously written value to port 0
	mb4052_ana = 0; // the analog value that's currently being read
	mb4052_count = 0; // keep track of which bit we're reading
}

void SensorKid::writeIO(uint16_t port, byte value, EmuTime /*time*/)
{
	if ((port & 1) != 0) return;

	byte diff = prev ^ value;
	prev = value;

	if (diff & 0xC0) {
		// upper two bits changed
		putPort(value, diff);
	}

	if (diff & 0x10) {
		// CS bit changed
		if (value & 0x10) {
			// 0 -> 1 : start of A/D convert
			mb4052_ana = getAnalog(value & 0x0C);
			mb4052_count = 12;
		} else {
			// 1 -> 0 : end of A/D convert
			mb4052_count = 0;
		}
	}
	if ((diff & 0x01) && (value & 0x01)) {
		// CLK changed 0 -> 1
		if (mb4052_count > 0) --mb4052_count;
	}
}

byte SensorKid::readIO(uint16_t port, EmuTime /* time */)
{
	if ((port & 1) == 0) {
		// port 0
		// mb4052_count can take on values [0, 12]
		//  for 12,11,1,0 we return 1
		//  for 10        we return 0
		//  for 9..2      we return one of the analog bits
		byte result = [&]  {
			if (mb4052_count == 10) {
				return byte(0);
			} else if ((mb4052_count < 10) && (mb4052_count > 1)) {
				return byte((mb4052_ana >> (mb4052_count - 2)) & 1);
			} else {
				return byte(1);
			}
		}();
		return 0xFE | result; // other bits read as '1'.
	} else {
		// port 1
		// returns two of the previously written bits (shuffled in different positions)
		return 0xFC | ((prev >> 5) & 0x02) | ((prev >> 7) & 0x01);
	}
}

byte SensorKid::getAnalog(byte chi) const
{
	// bits 2 and 3 in the 'port-0' byte select between 4 possible channels
	// for some reason bits 2 and 3 are swapped and then shifted down
	byte port = ((chi >> 1) & 2) | ((chi >> 3) & 1);

	// Execute Tcl callback
	//   input: the port number (0-3)
	//   output: the analog value for that port (0-255)
	// On the real cartridge
	//  port 0 is connected to a light sensor         HIKARI
	//  port 1 is connector to a temperature sensor   ONDO
	//  port 2 is connected to a microphone           OTO
	//  port 3 is not connected, always return 255
	byte result = 255;
	try {
		auto obj = acquireCallback.execute(port);
		if (obj != TclObject()) {
			auto tmp = obj.getInt(getCommandController().getInterpreter());
			if ((tmp < 0) || (tmp > 255)) {
				throw MSXException("outside range 0..255");
			}
			result = narrow_cast<byte>(tmp);
		}
	} catch (MSXException& e) {
		getCliComm().printWarning(
			"Wrong result for callback function \"",
			acquireCallback.getSetting().getFullName(),
			"\": ", e.getMessage());
	}
	return result;
}

void SensorKid::putPort(byte data, byte diff) const
{
	// When the upper 2 bits (bit 6 and 7) change we send a message.
	// I assume the cartridge also has two digital output pins?
	if (diff & 0x80) {
		//std::cout << "Status Port 0: " << int((data & 0x80) == 0) << '\n';
		portStatusCallback.execute(0, (data & 0x80) == 0);
	}
	if (diff & 0x40) {
		//std::cout << "Status Port 1:  " << int((data & 0x40) == 0) << '\n';
		portStatusCallback.execute(1, (data & 0x40) == 0);
	}
}

template<typename Archive>
void SensorKid::serialize(Archive& ar, unsigned /*version*/)
{
	ar.template serializeBase<MSXDevice>(*this);
	ar.serialize("prev",         prev,
	             "mb4052_ana",   mb4052_ana,
	             "mb4052_count", mb4052_count);
}
INSTANTIATE_SERIALIZE_METHODS(SensorKid);
REGISTER_MSXDEVICE(SensorKid, "SensorKid");

} // namespace openmsx
