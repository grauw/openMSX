// $Id$

#include "PrinterPortSimpl.hh"
#include "DACSound8U.hh"


namespace openmsx {

PrinterPortSimpl::PrinterPortSimpl()
{
}

PrinterPortSimpl::~PrinterPortSimpl()
{
}

bool PrinterPortSimpl::getStatus(const EmuTime &time)
{
	return true;	// TODO check
}

void PrinterPortSimpl::setStrobe(bool strobe, const EmuTime &time)
{
	// ignore strobe	TODO check
}

void PrinterPortSimpl::writeData(byte data, const EmuTime &time)
{
	dac->writeDAC(data,time);
}

void PrinterPortSimpl::plugHelper(Connector *connector, const EmuTime &time)
{
	short volume = 12000;	// TODO read from config
	dac.reset(new DACSound8U("simpl", getDescription(), volume, time));
}

void PrinterPortSimpl::unplugHelper(const EmuTime &time)
{
	dac.reset();
}

const string& PrinterPortSimpl::getName() const
{
	static const string name("simpl");
	return name;
}

const string& PrinterPortSimpl::getDescription() const
{
	static const string desc("Play samples via your printer port.");
	return desc;
}

} // namespace openmsx
