#include "BackendDriver.h"

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "Asm.h"
#include "FrameLayout.h"
#include "GraphColoringRegisterAllocator.h"
#include "IRAdapter.h"
#include "InstructionSelector.h"
#include "MachineLocalCSE.h"
#include "MachineAsmLowering.h"
#include "Module.h"
#include "Type.h"
#include "GlobalVariable.h"

namespace {

bool isIdentifierChar(char ch)
{
	return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_';
}

std::string asmSymbolFromIRName(const std::string & irName, const std::string & fallback)
{
	const std::string & symbol = irName.empty() ? fallback : irName;
	return !symbol.empty() && symbol.front() == '@' ? symbol.substr(1) : symbol;
}

bool isNumericToken(const std::string & token)
{
	if (token.empty()) {
		return false;
	}
	char * end = nullptr;
	(void) std::strtod(token.c_str(), &end);
	return end != token.c_str() && end != nullptr && *end == '\0';
}

uint32_t floatToBits(float value)
{
	uint32_t bits = 0;
	std::memcpy(&bits, &value, sizeof(bits));
	return bits;
}

uint32_t parseFloatWord(const std::string & token)
{
	if (token.rfind("0x", 0) == 0 || token.rfind("0X", 0) == 0) {
		char * end = nullptr;
		const auto raw = static_cast<uint64_t>(std::strtoull(token.c_str(), &end, 16));
		if (end != nullptr && *end == '\0' && token.find_first_of("pP.") == std::string::npos && token.size() > 10) {
			double asDouble = 0.0;
			std::memcpy(&asDouble, &raw, sizeof(asDouble));
			return floatToBits(static_cast<float>(asDouble));
		}
	}
	return floatToBits(std::strtof(token.c_str(), nullptr));
}

uint32_t parseIntWord(const std::string & token)
{
	return static_cast<uint32_t>(std::strtol(token.c_str(), nullptr, 0));
}

bool matchWord(const std::string & text, std::size_t pos, const char * word)
{
	const std::size_t len = std::strlen(word);
	if (pos + len > text.size() || text.compare(pos, len, word) != 0) {
		return false;
	}

	if (pos > 0 && isIdentifierChar(text[pos - 1])) {
		return false;
	}
	if (pos + len < text.size() && isIdentifierChar(text[pos + len])) {
		return false;
	}
	return true;
}

char previousNonSpace(const std::string & text, std::size_t pos)
{
	while (pos > 0) {
		--pos;
		if (!std::isspace(static_cast<unsigned char>(text[pos]))) {
			return text[pos];
		}
	}
	return '\0';
}

std::string readInitializerValueToken(const std::string & text, std::size_t pos)
{
	while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
		++pos;
	}

	std::size_t end = pos;
	while (end < text.size() && !std::isspace(static_cast<unsigned char>(text[end])) && text[end] != ',' &&
		   text[end] != '[' && text[end] != ']') {
		++end;
	}

	return text.substr(pos, end - pos);
}

std::vector<uint32_t> scalarInitializerWords(Type * type, const std::string & text, std::size_t wordCount)
{
	std::vector<uint32_t> words(wordCount, 0);
	std::size_t out = 0;

	for (std::size_t pos = 0; pos < text.size() && out < words.size(); ++pos) {
		const bool intWord = matchWord(text, pos, "i32");
		const bool floatWord = matchWord(text, pos, "float");
		if (!intWord && !floatWord) {
			continue;
		}

		if (previousNonSpace(text, pos) == 'x') {
			continue;
		}

		const std::size_t typeLen = intWord ? std::strlen("i32") : std::strlen("float");
		std::string token = readInitializerValueToken(text, pos + typeLen);
		if (!isNumericToken(token)) {
			continue;
		}

		if (floatWord || (type != nullptr && type->isFloatType())) {
			words[out++] = parseFloatWord(token);
		} else {
			words[out++] = parseIntWord(token);
		}
	}

	return words;
}

std::vector<uint32_t> initializerWords(Type * type, const std::string & text)
{
	const std::size_t wordCount = type != nullptr && type->getSize() > 0 ? static_cast<std::size_t>(type->getSize() / 4) : 1;
	std::vector<uint32_t> words(wordCount, 0);
	if (text.empty() || text == "zeroinitializer") {
		return words;
	}

	if (type != nullptr && type->isFloatType()) {
		words[0] = parseFloatWord(text);
		return words;
	}

	if (type != nullptr && type->isInt32Type()) {
		words[0] = parseIntWord(text);
		return words;
	}

	return scalarInitializerWords(type, text, wordCount);
}

void emitWords(FILE * fp, const std::vector<uint32_t> & words)
{
	for (uint32_t word: words) {
		fprintf(fp, ".word 0x%08x\n", word);
	}
}

} // namespace

bool BackendDriver::run(Module * module, const std::string & outputFile) const
{
	if (module == nullptr) {
		return false;
	}

	FILE * fp = fopen(outputFile.c_str(), "w");
	if (fp == nullptr) {
		return false;
	}

	IRModuleView moduleView = IRAdapter::adapt(module);

	bool emittedData = false;
	bool emittedBss = false;
	enum class DataSection { None, Data, Bss };
	DataSection currentSection = DataSection::None;
	for (const auto & global: moduleView.globals()) {
		if (!global.valid()) {
			continue;
		}

		auto * globalVar = dynamic_cast<GlobalVariable *>(global.raw());
		if (globalVar == nullptr) {
			continue;
		}

		if (globalVar->isInBSSSection()) {
			if (!emittedBss || currentSection != DataSection::Bss) {
				fprintf(fp, ".bss\n");
				emittedBss = true;
				currentSection = DataSection::Bss;
			}

			const std::string symbolName = asmSymbolFromIRName(global.irName(), global.name());
			if (globalVar->getLinkage() != GlobalValue::InternalLinkage) {
				fprintf(fp, ".globl %s\n", symbolName.c_str());
			}
			fprintf(fp, ".balign %d\n", globalVar->getAlignment());
			fprintf(fp, "%s:\n", symbolName.c_str());
			fprintf(fp, ".zero %d\n", globalVar->getType()->getSize());
			continue;
		}

		if (!emittedData || currentSection != DataSection::Data) {
			fprintf(fp, ".data\n");
			emittedData = true;
			currentSection = DataSection::Data;
		}

		const std::string symbolName = asmSymbolFromIRName(global.irName(), global.name());
		if (globalVar->getLinkage() != GlobalValue::InternalLinkage) {
			fprintf(fp, ".globl %s\n", symbolName.c_str());
		}
		fprintf(fp, ".balign %d\n", globalVar->getAlignment());
		fprintf(fp, "%s:\n", symbolName.c_str());
		if (globalVar->hasInitializerText()) {
			emitWords(fp, initializerWords(globalVar->getType(), globalVar->getInitializerText()));
		} else {
			fprintf(fp, ".word %d\n", globalVar->hasInitializerValue() ? globalVar->getInitializerInt() : 0);
		}
	}

	for (const auto & function: moduleView.functions()) {
		if (!function.valid() || function.isBuiltin()) {
			continue;
		}

		FunctionFrameLayout layout = FrameLayoutBuilder::build(function);
		InstructionSelector selector(function, layout);
		MachineFunction machineFunction = selector.run();
		MachineLocalCSE localCSE;
		localCSE.run(machineFunction);
		GraphColoringRegisterAllocator allocator;
		if (!allocator.run(machineFunction, layout)) {
			fclose(fp);
			return false;
		}
		MachineAsmLowering lowering(machineFunction, layout);
		AsmFunction asmFunction = lowering.run();
		auto * func = dynamic_cast<Function *>(function.raw());
		if (func && func->getLinkage() == GlobalValue::InternalLinkage) {
			asmFunction.setInternalLinkage(true);
		}
		AsmPrinter::printFunction(fp, asmFunction);
	}

	fclose(fp);
	return true;
}
