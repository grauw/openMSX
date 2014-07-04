#include "BreakPoint.hh"
#include "TclObject.hh"

namespace openmsx {

unsigned BreakPoint::lastId = 0;

BreakPoint::BreakPoint(GlobalCliComm& cliComm, Interpreter& interp,
                       word address_, TclObject command, TclObject condition)
	: BreakPointBase(cliComm, interp, command, condition)
	, id(++lastId)
	, address(address_)
{
}

word BreakPoint::getAddress() const
{
	return address;
}

unsigned BreakPoint::getId() const
{
	return id;
}

} // namespace openmsx
