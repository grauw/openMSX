#include "CliExtension.hh"
#include "CommandLineParser.hh"
#include "HardwareConfig.hh"
#include "MSXMotherBoard.hh"
#include "MSXException.hh"
#include <cassert>

namespace openmsx {

CliExtension::CliExtension(CommandLineParser& cmdLineParser_)
	: cmdLineParser(cmdLineParser_)
{
	cmdLineParser.registerOption("-ext", *this);
	cmdLineParser.registerOption("-exta", *this);
	cmdLineParser.registerOption("-extb", *this);
	cmdLineParser.registerOption("-extc", *this);
	cmdLineParser.registerOption("-extd", *this);
}

void CliExtension::parseOption(const std::string& option, std::span<std::string>& cmdLine)
{
	try {
		std::string extensionName = getArgument(option, cmdLine);
		MSXMotherBoard* motherboard = cmdLineParser.getMotherBoard();
		assert(motherboard);
		std::string slotName;
		if (option.size() == 5) {
			slotName = option[4];
		} else {
			slotName = "any";
		}
		motherboard->insertExtension(extensionName,
			motherboard->loadExtension(extensionName, slotName));
	} catch (MSXException& e) {
		throw FatalError(std::move(e).getMessage());
	}
}

std::string_view CliExtension::optionHelp() const
{
	return "Insert the extension specified in argument";
}

} // namespace openmsx
