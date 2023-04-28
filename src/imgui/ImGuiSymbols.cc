#include "ImGuiSymbols.hh"

#include "ImGuiCpp.hh"
#include "ImGuiManager.hh"
#include "ImGuiUtils.hh"

#include "CliComm.hh"
#include "File.hh"
#include "MSXException.hh"
#include "Reactor.hh"
#include "StringOp.hh"
#include "ranges.hh"
#include "stl.hh"
#include "strCat.hh"
#include "unreachable.hh"

#include <imgui.h>

#include <cassert>
#include <optional>

namespace openmsx {

ImGuiSymbols::ImGuiSymbols(ImGuiManager& manager_)
	: manager(manager_)
{
	if (0) { // test code, TODO remove this
		symbols = load("/home/wouter/symbols.asm"); // HACK

		symbols.emplace_back("chmod", "bios.asm", 0x005f);
		symbols.emplace_back("start", "project.asm", 0x4000);
		symbols.emplace_back("stop", "project.asm", 0x7fff);
		symbols.emplace_back("label1", "test.asm", 0x0001);
		symbols.emplace_back("label2", "test.asm", 0x0002);
		symbols.emplace_back("label3", "test.asm", 0x0003);
		symbols.emplace_back("label4", "test.asm", 0x0004);
		symbols.emplace_back("label5", "test.asm", 0x0005);
		symbols.emplace_back("label6", "test.asm", 0x0006);
		symbols.emplace_back("label7", "test.asm", 0x0007);
		symbols.emplace_back("label8", "test.asm", 0x0008);
		symbols.emplace_back("label9", "test.asm", 0x0009);
		symbols.emplace_back("label10", "test.asm", 0x000a);
		symbols.emplace_back("label11", "test.asm", 0x000b);
		symbols.emplace_back("label12", "test.asm", 0x000c);
		symbols.emplace_back("label13", "test.asm", 0x000d);
		symbols.emplace_back("label14", "test.asm", 0x000e);
		symbols.emplace_back("label15", "test.asm", 0x000f);
		symbols.emplace_back("label16", "test.asm", 0x0010);
		symbols.emplace_back("label17", "test.asm", 0x0011);
		symbols.emplace_back("label18", "test.asm", 0x0012);
		symbols.emplace_back("label19", "test.asm", 0x0013);
		symbols.emplace_back("label20", "test.asm", 0x0014);
	}
}

void ImGuiSymbols::save(ImGuiTextBuffer& buf)
{
	buf.appendf("show=%d\n", show);
}

void ImGuiSymbols::loadLine(std::string_view name, zstring_view value)
{
	if (name == "show") {
		show = StringOp::stringToBool(value);
	}
}

static void checkSort(std::vector<Symbol>& symbols)
{
	auto* sortSpecs = ImGui::TableGetSortSpecs();
	if (!sortSpecs->SpecsDirty) return;

	sortSpecs->SpecsDirty = false;
	assert(sortSpecs->SpecsCount == 1);
	assert(sortSpecs->Specs);
	assert(sortSpecs->Specs->SortOrder == 0);

	auto sortUpDown = [&](auto proj) {
		if (sortSpecs->Specs->SortDirection == ImGuiSortDirection_Descending) {
			ranges::stable_sort(symbols, std::greater<>{}, proj);
		} else {
			ranges::stable_sort(symbols, std::less<>{}, proj);
		}
	};
	switch (sortSpecs->Specs->ColumnIndex) {
	case 0: // name
		sortUpDown(&Symbol::name);
		break;
	case 1: // value
		sortUpDown(&Symbol::value);
		break;
	case 2: // file
		sortUpDown(&Symbol::file);
		break;
	default:
		UNREACHABLE;
	}
}
template<bool FILTER_FILE>
static void drawTable(std::vector<Symbol>& symbols, const std::string& file = {})
{
	assert(FILTER_FILE == !file.empty());

	int flags = ImGuiTableFlags_RowBg |
	            ImGuiTableFlags_BordersV |
	            ImGuiTableFlags_BordersOuter |
	            ImGuiTableFlags_ContextMenuInBody |
	            ImGuiTableFlags_Resizable |
	            ImGuiTableFlags_Reorderable |
	            ImGuiTableFlags_Sortable |
	            ImGuiTableFlags_SizingStretchProp |
	            (FILTER_FILE ? ImGuiTableFlags_ScrollY : 0);
	im::Table(file.c_str(), FILTER_FILE ? 2 : 3, flags, {0, 100}, [&]{
		ImGui::TableSetupScrollFreeze(0, 1); // Make top row always visible
		ImGui::TableSetupColumn("name");
		ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthFixed);
		if (!FILTER_FILE) {
			ImGui::TableSetupColumn("file");
		}
		ImGui::TableHeadersRow();
		checkSort(symbols);

		for (const auto& sym : symbols) {
			if (FILTER_FILE && (sym.file != file)) continue;

			if (ImGui::TableNextColumn()) { // name
				ImGui::TextUnformatted(sym.name);
			}
			if (ImGui::TableNextColumn()) { // value
				ImGui::StrCat(hex_string<4>(sym.value));
			}
			if (!FILTER_FILE && ImGui::TableNextColumn()) { // file
				ImGui::TextUnformatted(sym.file);
			}
		}
	});
}

void ImGuiSymbols::paint(MSXMotherBoard* /*motherBoard*/)
{
	if (!show) return;

	im::Window("Symbol Manager", &show, [&]{
		if (ImGui::Button("Load symbol file...")) {
			manager.openFile.selectFile(
				"Select symbol file", "ASM (*.asm){.asm}", // TODO extensions
				[this](const std::string& filename) {
					reload(filename);
				});
		}

		auto files = getFiles(); // make copy because cache may get dropped
		im::TreeNode("Symbols per file", ImGuiTreeNodeFlags_DefaultOpen, [&]{
			for (const auto& file : files) {
				auto title = strCat("File: ", file);
				im::TreeNode(title.c_str(), [&]{
					if (ImGui::Button("Reload")) {
						reload(file);
					}
					ImGui::SameLine();
					if (ImGui::Button("Remove")) {
						remove(file);
					}
					drawTable<true>(symbols, file);
				});
			}
		});
		im::TreeNode("All symbols", [&]{
			if (ImGui::Button("Reload all")) {
				for (const auto& file : files) {
					reload(file);
				}
			}
			ImGui::SameLine();
			if (ImGui::Button("Remove all")) {
				removeAll();
			}
			drawTable<false>(symbols);
		});
	});
}

void ImGuiSymbols::dropCaches()
{
	filesCache.clear();
	lookupValueCache.clear();
}

const std::vector<std::string>& ImGuiSymbols::getFiles()
{
	if (filesCache.empty()) {
		// This is O(N*M), with N #symbols, M #files (so possibly O(N^2))
		// That's fine: in practice there won't be too many different files.
		for (const auto& sym : symbols) {
			if (!contains(filesCache, sym.file)) {
				filesCache.push_back(sym.file);
			}
		}
		ranges::sort(filesCache);
	}
	return filesCache;
}

std::string_view ImGuiSymbols::lookupValue(uint16_t value)
{
	if (lookupValueCache.empty()) {
		for (const auto& sym : symbols) {
			// does not replace existing values, meaning you can
			// only query one symbol name for the same value
			lookupValueCache.try_emplace(sym.value, sym.name);
		}
	}
	if (auto* sym = lookup(lookupValueCache, value)) {
		return *sym;
	}
	return {};
}

void ImGuiSymbols::reload(const std::string& file)
{
	auto& cliComm = manager.getReactor().getCliComm();
	try {
		auto newSymbols = load(file);
		if (newSymbols.empty()) {
			cliComm.printWarning(
				"Symbol file \"", file,
				"\" doesn't contain any symbols");
			return; // don't remove previous version
		}
		remove(file);
		append(symbols, std::move(newSymbols));
	} catch (MSXException& e) {
		manager.getReactor().getCliComm().printWarning(
			"Couldn't load symbol file \"", file, "\": ", e.getMessage());
	}
	dropCaches();
}

void ImGuiSymbols::removeAll()
{
	symbols.clear();
	dropCaches();
}

void ImGuiSymbols::remove(const std::string& file)
{
	std::erase_if(symbols, [&](auto& sym) { return sym.file == file; });
	dropCaches();
}

static std::optional<uint16_t> parseValue(std::string_view str)
{
	if (str.ends_with('h') || str.ends_with('H')) { // hex
		str.remove_suffix(1);
		return StringOp::stringToBase<16, uint16_t>(str);
	}
	if (str.starts_with('$') || str.starts_with('#')) { // hex
		str.remove_prefix(1);
		return StringOp::stringToBase<16, uint16_t>(str);
	}
	if (str.starts_with('%')) { // bin
		str.remove_prefix(1);
		return StringOp::stringToBase<2, uint16_t>(str);
	}
	// this recognizes the prefixes "0x" or "0X" (for hexadecimal)
	//                          and "0b" or "0B" (for binary)
	// no prefix in interpreted as decimal
	// "0" as a prefix for octal is intentionally NOT supported
	return StringOp::stringTo<uint16_t>(str);
}

std::vector<Symbol> ImGuiSymbols::load(const std::string& filename)
{
	// TODO support and auto-detect many more file formats.
	File file(filename);
	auto buf = file.mmap();
	std::string_view strBuf(reinterpret_cast<const char*>(buf.data()), buf.size());

	std::vector<Symbol> result;
	static constexpr std::string_view whitespace = " \t\r";
	for (std::string_view fullLine : StringOp::split_view(strBuf, '\n')) {
		StringOp::trimLeft(fullLine, whitespace);
		auto [line, _] = StringOp::splitOnFirst(fullLine, ';');

		auto [pos, size] = std::tuple(line.find("%equ"), 4);
		if (pos == std::string_view::npos) std::tie(pos, size) = std::tuple(line.find("equ"), 3);
		if (pos == std::string_view::npos) continue;

		auto label = line.substr(0, pos);
		StringOp::trimRight(label, whitespace);
		if (label.ends_with(':')) label.remove_suffix(1);

		auto value = line.substr(pos + size);
		StringOp::trim(value, whitespace);
		if (auto num = parseValue(value)) {
			result.emplace_back(std::string(label), filename, *num);
		}
	}
	return result;
}

} // namespace openmsx
