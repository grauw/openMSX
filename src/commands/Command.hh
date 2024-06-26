#ifndef COMMAND_HH
#define COMMAND_HH

#include "Completer.hh"
#include "strCat.hh"
#include "CommandException.hh"
#include <cassert>
#include <span>
#include <string_view>
#include <vector>

namespace openmsx {

class CommandController;
class GlobalCommandController;
class Interpreter;
class TclObject;
class CliComm;

class CommandCompleter : public Completer
{
public:
	CommandCompleter(const CommandCompleter&) = delete;
	CommandCompleter(CommandCompleter&&) = delete;
	CommandCompleter& operator=(const CommandCompleter&) = delete;
	CommandCompleter& operator=(CommandCompleter&&) = delete;

	[[nodiscard]] CommandController& getCommandController() const { return commandController; }
	[[nodiscard]] Interpreter& getInterpreter() const final;

protected:
	CommandCompleter(CommandController& controller, std::string_view name);
	~CommandCompleter();

	[[nodiscard]] GlobalCommandController& getGlobalCommandController() const;
	[[nodiscard]] CliComm& getCliComm() const;

private:
	CommandController& commandController;
};


class Command : public CommandCompleter
{
	struct UnknownSubCommand {};

public:
	/** Execute this command.
	  * @param tokens Tokenized command line;
	  *     tokens[0] is the command itself.
	  * @param result The result of the command must be assigned to this
	  *               parameter.
	  * @throws CommandException Thrown when there was an error while
	  *                          executing this command.
	  */
	virtual void execute(std::span<const TclObject> tokens, TclObject& result) = 0;

	/** Attempt tab completion for this command.
	  * Default implementation does nothing.
	  * @param tokens Tokenized command line;
	  *     tokens[0] is the command itself.
	  *     The last token is incomplete, this method tries to complete it.
	  */
	void tabCompletion(std::vector<std::string>& tokens) const override;

	// see comments in MSXMotherBoard::loadMachineCommand
	void setAllowedInEmptyMachine(bool value) { allowInEmptyMachine = value; }
	[[nodiscard]] bool isAllowedInEmptyMachine() const { return allowInEmptyMachine; }

	// used by Interpreter::(un)registerCommand()
	void setToken(void* token_) { assert(!token); token = token_; }
	[[nodiscard]] void* getToken() const { return token; }

	// helper to delegate to a subcommand
	template<typename... Args>
	void executeSubCommand(std::string_view subCmd, Args&&... args) {
		try {
			executeSubCommandImpl(subCmd, std::forward<Args>(args)...);
		} catch (UnknownSubCommand&) {
			unknownSubCommand(subCmd, std::forward<Args>(args)...);
		}
	}

protected:
	Command(CommandController& controller, std::string_view name);
	~Command();

private:
	template<typename Func, typename... Args>
	void executeSubCommandImpl(std::string_view subCmd, std::string_view candidate, Func func, Args&&... args) const {
		if (subCmd == candidate) {
			func();
		} else {
			executeSubCommandImpl(subCmd, std::forward<Args>(args)...);
		}
	}
	[[noreturn]] void executeSubCommandImpl(std::string_view /*subCmd*/) const {
		throw UnknownSubCommand{}; // exhausted all possible candidates
	}

	template<typename Func, typename... Args>
	void unknownSubCommand(std::string_view subCmd, std::string_view firstCandidate, Func /*func*/, Args&&... args) const {
		unknownSubCommandImpl(strCat("Unknown subcommand '", subCmd, "'. Must be one of '", firstCandidate, '\''),
		                      std::forward<Args>(args)...);
	}
	template<typename Func, typename... Args>
	void unknownSubCommandImpl(std::string message, std::string_view candidate, Func /*func*/, Args&&... args) const {
		strAppend(message, ", '", candidate, '\'');
		unknownSubCommandImpl(message, std::forward<Args>(args)...);
		throw SyntaxError();
	}
	template<typename Func>
	void unknownSubCommandImpl(std::string message, std::string_view lastCandidate, Func /*func*/) const {
		strAppend(message, " or '", lastCandidate, "'.");
		throw CommandException(message);
	}

private:
	bool allowInEmptyMachine = true;
	void* token = nullptr;
};

} // namespace openmsx

#endif
