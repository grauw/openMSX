// $Id$

#include <cassert>
#include <sstream>
#include "CommandController.hh"
#include "Debuggable.hh"
#include "Debugger.hh"

using std::ostringstream;

namespace openmsx {

Debugger::Debugger()
	: debugCmd(*this),
	  commandController(CommandController::instance())
{
	commandController.registerCommand(&debugCmd, "debug");
}

Debugger::~Debugger()
{
	assert(debuggables.empty());
	commandController.unregisterCommand(&debugCmd, "debug");
}

Debugger& Debugger::instance()
{
	static Debugger oneInstance;
	return oneInstance;
}

void Debugger::registerDebuggable(const string& name, Debuggable& debuggable)
{
	assert(debuggables.find(name) == debuggables.end());
	debuggables[name] = &debuggable;
}

void Debugger::unregisterDebuggable(const string& name, Debuggable& debuggable)
{
	map<string, Debuggable*>::iterator it = debuggables.find(name);
	assert(it != debuggables.end() && (it->second == &debuggable));
	debuggables.erase(it);
}

Debuggable* Debugger::getDebuggable(const string& name)
{
	map<string, Debuggable*>::iterator it = debuggables.find(name);
	if (it == debuggables.end()) {
		throw CommandException("No such debuggable.");
	}
	return it->second;
}

void Debugger::getDebuggables(set<string>& result) const
{
	for (map<string, Debuggable*>::const_iterator it = debuggables.begin();
	     it != debuggables.end(); ++it) {
		result.insert(it->first);
	}
}


// class DebugCmd

Debugger::DebugCmd::DebugCmd(Debugger& parent_)
	: parent(parent_)
{
}

string Debugger::DebugCmd::execute(const vector<string>& tokens)
	throw(CommandException)
{
	if (tokens.size() < 2) {
		throw CommandException("Missing argument");
	}
	if (tokens[1] == "read") {
		return read(tokens);
	} else if (tokens[1] == "write") {
		return write(tokens);
	} else if (tokens[1] == "size") {
		return size(tokens);
	} else if (tokens[1] == "desc") {
		return desc(tokens);
	} else if (tokens[1] == "list") {
		return list();
	}
	throw CommandException("Syntax error.");
}

string Debugger::DebugCmd::list()
{
	string result;
	for (map<string, Debuggable*>::const_iterator it =
	       parent.debuggables.begin();
	     it != parent.debuggables.end(); ++it) {
		result += it->first + '\n';
	}
	return result;
}

string Debugger::DebugCmd::desc(const vector<string>& tokens)
{
	if (tokens.size() != 3) {
		throw CommandException("Syntax error.");
	}
	Debuggable* device = parent.getDebuggable(tokens[2]);
	return device->getDescription();
}

string Debugger::DebugCmd::size(const vector<string>& tokens)
{
	if (tokens.size() != 3) {
		throw CommandException("Syntax error.");
	}
	Debuggable* device = parent.getDebuggable(tokens[2]);

	ostringstream os;
	os << device->getSize();
	return os.str();
}

string Debugger::DebugCmd::read(const vector<string>& tokens)
{
	if (tokens.size() != 4) {
		throw CommandException("Syntax error.");
	}
	Debuggable* device = parent.getDebuggable(tokens[2]);
	
	char* endPtr;
	unsigned addr = strtol(tokens[3].c_str(), &endPtr, 0);
	if ((*endPtr != '\0') || (addr >= device->getSize())) {
		throw CommandException("Invalid address");
	}
	
	ostringstream os;
	os << (int)device->read(addr);
	return os.str();
}

string Debugger::DebugCmd::write(const vector<string>& tokens)
{
	if (tokens.size() != 5) {
		throw CommandException("Syntax error.");
	}
	Debuggable* device = parent.getDebuggable(tokens[2]);
	
	char* endPtr;
	unsigned addr = strtol(tokens[3].c_str(), &endPtr, 0);
	if ((*endPtr != '\0') || (addr >= device->getSize())) {
		throw CommandException("Invalid address");
	}
	unsigned value = strtol(tokens[4].c_str(), &endPtr, 0);
	if ((*endPtr != '\0') || (value >= 256)) {
		throw CommandException("Invalid value");
	}
	
	device->write(addr, value);
	return string();
}

string Debugger::DebugCmd::help(const vector<string>& tokens) const
	throw()
{
	static const string helpText =
		"debug list                       returns a list of all debuggables\n"
		"debug desc <name>                returns a description of this debuggable\n"
		"debug size <name>                returns the size of this debuggable\n"
		"debug read <name> <addr>         read a byte from a debuggable\n"
		"debug write <name> <addr> <val>  write a byte to a debuggable\n"; 
	return helpText;
}

void Debugger::DebugCmd::tabCompletion(vector<string>& tokens) const
	throw()
{
	switch (tokens.size()) {
		case 2: {
			set<string> cmds;
			cmds.insert("list");
			cmds.insert("desc");
			cmds.insert("size");
			cmds.insert("read");
			cmds.insert("write");
			CommandController::completeString(tokens, cmds);
			break;
		}
		case 3: {
			set<string> debuggables;
			parent.getDebuggables(debuggables);
			CommandController::completeString(tokens, debuggables);
			break;
		}
	}
}

} // namespace openmsx
