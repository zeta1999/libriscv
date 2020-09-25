#include "machine.hpp"
#include "common.hpp"
#include "decoder_cache.hpp"
#include "riscvbase.hpp"
#include "rv32i_instr.hpp"
#include "rv64i_instr.hpp"
#ifdef __GNUG__
#include "rv32i.cpp"
#endif

namespace riscv
{
	template <int W>
	void CPU<W>::reset()
	{
		this->m_regs = {};
		this->reset_stack_pointer();
#ifdef RISCV_PAGE_CACHE
		// invalidate the page cache
		for (auto& cache : this->m_page_cache)
			cache.pageno = -1;
#endif
		this->m_current_page = {};
		// jumping causes some extra calculations
		this->jump(machine().memory.start_address());
	}

	template <int W>
	typename CPU<W>::format_t CPU<W>::read_upper_half(address_t offset)
	{
		format_t instruction;
		// read short instruction at address
		instruction.whole =
			m_current_page.page->template aligned_read<uint16_t> (offset);

		// read upper half, completing a 32-bit instruction
		if (UNLIKELY(instruction.is_long())) {
			// this instruction crosses a page-border
			this->change_page(m_current_page.pageno + 1);
			instruction.half[1] =
				m_current_page.page->template aligned_read<uint16_t>(0);
		}
		return instruction;
	}

	template <int W>
	typename CPU<W>::format_t CPU<W>::read_next_instruction()
	{
		format_t instruction;

		// We have to check the bounds just to be thorough, as this will
		// instantly crash if something is wrong. In addition,
		// page management is only done for jumps outside of execute segment.
		// Secondly, any jump traps will **HAVE** to return to the execute
		// segment before returning.
		if (LIKELY(this->pc() >= m_exec_begin && this->pc() < m_exec_end)) {
			instruction.whole = *(uint32_t*) &m_exec_data[this->pc()];
			return instruction;
		}

		// We don't need to manage the current page when
		// we have the whole execute-range (+ instruction cache)
		// WARNING: this combination will break jump-traps (for now)
		const address_t this_page = this->pc() >> Page::SHIFT;
		if (this_page != this->m_current_page.pageno) {
			this->change_page(this_page);
		}
		const address_t offset = this->pc() & (Page::size()-1);

		if constexpr (!compressed_enabled) {
			// special case for non-compressed mode:
			// we can always read whole instructions
			instruction.whole =
				m_current_page.page->template aligned_read<uint32_t> (offset);
		}
		else
		{
			// here we support compressed instructions
			// read only full-sized instructions until the end of the buffer
			if (LIKELY(offset <= Page::size() - 4))
			{
				// we can read the whole thing
				instruction.whole =
					m_current_page.page->template aligned_read<uint32_t> (offset);
				return instruction;
			}

			return read_upper_half(offset);
		}
		return instruction;
	}

	template<int W>
	void CPU<W>::simulate()
	{
#ifdef RISCV_DEBUG
		this->break_checks();

		const auto& handler = this->decode(instruction);
		// instruction logging
		if (UNLIKELY(machine().verbose_instructions))
		{
			const auto string = isa_t::to_string(*this, instruction, handler);
			printf("%s\n", string.c_str());
		}

		// execute instruction
		handler.handler(*this, instruction);
#else
		int i = 0;
		for (; i < 100 && !machine().stopped(); i++)
		{
			const auto instruction = this->read_next_instruction();
# ifdef RISCV_INSTR_CACHE
			// retrieve instructions directly from the constant cache
			// WARNING: the contract between read_next_instruction and this
			// is that any jump traps must return to the caller, and be re-
			// validated, otherwise this code will read garbage data!
			auto& cache_entry =
				machine().memory.get_decoder_cache()[this->pc() / DecoderCache<Page::SIZE>::DIVISOR];
#ifndef RISCV_INSTR_CACHE_PREGEN
			if (UNLIKELY(!cache_entry)) {
				cache_entry = this->decode(instruction).handler;
			}
#endif
			// execute instruction
			cache_entry(*this, instruction);
# else
			// decode & execute instruction directly
			this->execute(instruction);
# endif
			// increment PC
			if constexpr (compressed_enabled)
				registers().pc += instruction.length();
			else
				registers().pc += 4;
		}
		// increment instruction counter
		this->m_counter += i;
#endif

#ifdef RISCV_DEBUG
		if (UNLIKELY(machine().verbose_registers))
		{
			auto regs = this->registers().to_string();
			printf("\n%s\n\n", regs.c_str());
			if (UNLIKELY(machine().verbose_fp_registers)) {
				printf("%s\n", registers().flp_to_string().c_str());
			}
		}
		// increment instruction counter
		this->m_counter ++;
		// increment PC
		if constexpr (compressed_enabled)
			registers().pc += instruction.length();
		else
			registers().pc += 4;
#endif
	}

	template<int W> __attribute__((cold))
	void CPU<W>::trigger_exception(interrupt_t intr, address_t data)
	{
		// TODO: replace with callback system
		switch (intr)
		{
		case ILLEGAL_OPCODE:
			throw MachineException(ILLEGAL_OPCODE,
					"Illegal opcode executed", data);
		case ILLEGAL_OPERATION:
			throw MachineException(ILLEGAL_OPERATION,
					"Illegal operation during instruction decoding", data);
		case PROTECTION_FAULT:
			throw MachineException(PROTECTION_FAULT,
					"Protection fault", data);
		case EXECUTION_SPACE_PROTECTION_FAULT:
			throw MachineException(EXECUTION_SPACE_PROTECTION_FAULT,
					"Execution space protection fault", data);
		case MISALIGNED_INSTRUCTION:
			// NOTE: only check for this when jumping or branching
			throw MachineException(MISALIGNED_INSTRUCTION,
					"Misaligned instruction executed", data);
		case UNIMPLEMENTED_INSTRUCTION:
			throw MachineException(UNIMPLEMENTED_INSTRUCTION,
					"Unimplemented instruction executed", data);
		default:
			throw MachineException(UNKNOWN_EXCEPTION,
					"Unknown exception", intr);
		}
	}

	template <int W> __attribute__((cold))
	std::string Registers<W>::to_string() const
	{
		char buffer[600];
		int  len = 0;
		for (int i = 1; i < 32; i++) {
			len += snprintf(buffer+len, sizeof(buffer) - len,
					"[%s\t%08lX] ", RISCV::regname(i), (long) this->get(i));
			if (i % 5 == 4) {
				len += snprintf(buffer+len, sizeof(buffer)-len, "\n");
			}
		}
		return std::string(buffer, len);
	}

	template <int W> __attribute__((cold))
	std::string Registers<W>::flp_to_string() const
	{
		char buffer[800];
		int  len = 0;
		for (int i = 0; i < 32; i++) {
			auto& src = this->getfl(i);
			const char T = (src.i32[1] == -1) ? 'S' : 'D';
			double val = (src.i32[1] == -1) ? src.f32[0] : src.f64;
			len += snprintf(buffer+len, sizeof(buffer) - len,
					"[%s\t%c%+.2f] ", RISCV::flpname(i), T, val);
			if (i % 5 == 4) {
				len += snprintf(buffer+len, sizeof(buffer)-len, "\n");
			}
		}
		return std::string(buffer, len);
	}

	template struct CPU<4>;
	template struct Registers<4>;
	template struct CPU<8>;
	template struct Registers<8>;
}
