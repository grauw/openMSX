// $Id$

#include "ProbeBreakPoint.hh"
#include "Probe.hh"
#include "Debugger.hh"
#include "TclObject.hh"

namespace openmsx {

unsigned ProbeBreakPoint::lastId = 0;

ProbeBreakPoint::ProbeBreakPoint(
		GlobalCliComm& cliComm,
		std::auto_ptr<TclObject> command,
		std::auto_ptr<TclObject> condition,
		Debugger& debugger_,
		ProbeBase& probe_,
		unsigned newId /*= -1*/)
	: BreakPointBase(cliComm, command, condition)
	, debugger(debugger_)
	, probe(probe_)
	, id((newId == unsigned(-1)) ? ++lastId : newId)
{
	probe.attach(*this);
}

ProbeBreakPoint::~ProbeBreakPoint()
{
	probe.detach(*this);
}

unsigned ProbeBreakPoint::getId() const
{
	return id;
}

const ProbeBase& ProbeBreakPoint::getProbe() const
{
	return probe;
}

void ProbeBreakPoint::update(const ProbeBase& /*subject*/)
{
	checkAndExecute();
}

void ProbeBreakPoint::subjectDeleted(const ProbeBase& /*subject*/)
{
	debugger.removeProbeBreakPoint(*this);
}

} // namespace openmsx
