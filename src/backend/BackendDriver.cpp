#include "backend/include/BackendDriver.h"

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "backend/include/Asm.h"
#include "backend/include/FrameLayout.h"
#include "backend/include/GraphColoringRegisterAllocator.h"
#include "backend/include/IRAdapter.h"
#include "backend/include/InstructionSelector.h"
#include "backend/include/MachineAsmLowering.h"
#include "ir/include/Module.h"
#include "ir/include/Type.h"
#include "ir/Values/GlobalVariable.h"

namespace {

std::vector<std::string> tokenizeInitializer(const std::string & text)
{
	std::vector<std::string> tokens;
	std::string token;
	for (char ch: text) {
		if (std::isspace(static_cast<unsigned char>(ch)) || ch == '[' || ch == ']' || ch == ',') {
			if (!token.empty()) {
				tokens.push_back(token);
				token.clear();
			}
			continue;
		}
		token.push_back(ch);
	}
	if (!token.empty()) {
		tokens.push_back(token);
	}
	return tokens;
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

	const auto tokens = tokenizeInitializer(text);
	std::size_t out = 0;
	for (std::size_t index = 0; index + 1 < tokens.size() && out < words.size(); ++index) {
		if (tokens[index] == "i32" && isNumericToken(tokens[index + 1])) {
			words[out++] = parseIntWord(tokens[++index]);
			continue;
		}
		if (tokens[index] == "float" && isNumericToken(tokens[index + 1])) {
			words[out++] = parseFloatWord(tokens[++index]);
			continue;
		}
	}
	return words;
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

			if (globalVar->getLinkage() != GlobalValue::InternalLinkage) {
				fprintf(fp, ".globl %s\n", global.name().c_str());
			}
			fprintf(fp, ".balign %d\n", globalVar->getAlignment());
			fprintf(fp, "%s:\n", global.name().c_str());
			fprintf(fp, ".zero %d\n", globalVar->getType()->getSize());
			continue;
		}

		if (!emittedData || currentSection != DataSection::Data) {
			fprintf(fp, ".data\n");
			emittedData = true;
			currentSection = DataSection::Data;
		}

		if (globalVar->getLinkage() != GlobalValue::InternalLinkage) {
			fprintf(fp, ".globl %s\n", global.name().c_str());
		}
		fprintf(fp, ".balign %d\n", globalVar->getAlignment());
		fprintf(fp, "%s:\n", global.name().c_str());
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
