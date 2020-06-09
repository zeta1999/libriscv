#include "rv32i_instr.hpp"
#include "machine.hpp"
#include "rv32i_instr.cpp"
#ifdef RISCV_EXT_ATOMICS
#include "rv32a_instr.cpp"
#endif
#include "rv32c_instr.cpp"
#include "rv32f_instr.cpp"

namespace riscv
{
	constexpr CPU<4>::instruction_t instructions[128] =
	{
		[0b0000011] = DECODED_INSTR(LOAD),
		[0b0100011] = DECODED_INSTR(STORE),
		[0b1100011] = DECODED_INSTR(BRANCH),
		[0b1100111] = DECODED_INSTR(JALR),
		[0b1101111] = DECODED_INSTR(JAL),
		[0b0010011] = DECODED_INSTR(OP_IMM),
		[0b0110011] = DECODED_INSTR(OP),
		[0b1110011] = DECODED_INSTR(SYSTEM),
		[0b0110111] = DECODED_INSTR(LUI),
		[0b0010111] = DECODED_INSTR(AUIPC),
		[0b0011011] = DECODED_INSTR(OP_IMM32),
		[0b0111011] = DECODED_INSTR(OP32),
		[0b0001111] = DECODED_INSTR(FENCE),
		// RV32F & RV32D - Floating-point instructions
		[0b0000111] = DECODED_INSTR(FLW_FLD),
		[0b0100111] = DECODED_INSTR(FSW_FSD),
		[0b1000011] = DECODED_INSTR(FMADD),
		[0b1000111] = DECODED_INSTR(FMSUB),
		[0b1001011] = DECODED_INSTR(FNMSUB),
		[0b1001111] = DECODED_INSTR(FNMADD),
	};

	template<>
	const CPU<4>::instruction_t& CPU<4>::decode(const format_t instruction) const
	{
#ifndef RISCV_EXT_COMPRESSED
		auto instr = instructions[instruction.opcode()];
		if (instr.handler) return instr;
#endif
#define DECODER(x) return(x)
#include "rv32_instr.inc"
#undef DECODER
	}

	template<>
	void CPU<4>::execute(const format_t instruction)
	{
#define DECODER(x) x.handler(*this, instruction); return;
#include "rv32_instr.inc"
#undef DECODER
	}

	std::string RV32I::to_string(CPU<4>& cpu, format_t format, const instruction_t& instr)
	{
		char buffer[256];
		char ibuffer[128];
		int  ibuflen = instr.printer(ibuffer, sizeof(ibuffer), cpu, format);
		int  len = 0;
		if (format.length() == 4) {
			len = snprintf(buffer, sizeof(buffer),
					"[%08X] %08X %.*s",
					cpu.pc(), format.whole, ibuflen, ibuffer);
		}
		else if (format.length() == 2) {
			len = snprintf(buffer, sizeof(buffer),
					"[%08X]     %04hX %.*s",
					cpu.pc(), (uint16_t) format.whole, ibuflen, ibuffer);
		}
		else {
			throw MachineException(UNIMPLEMENTED_INSTRUCTION_LENGTH,
									"Unimplemented instruction format length");
		}
		return std::string(buffer, len);
	}
}
