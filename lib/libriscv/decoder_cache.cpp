#include "memory.hpp"
#include "machine.hpp"
#include "decoder_cache.hpp"
#include <stdexcept>

#include "rv32i_instr.hpp"

namespace riscv
{

#ifdef RISCV_INSTR_CACHE
	template <int W>
	void Memory<W>::generate_decoder_cache(address_t addr, size_t len)
	{
		constexpr address_t PMASK = Page::size()-1;
		const address_t pbase = addr & ~PMASK;
		const size_t prelen  = addr - pbase;
		const size_t midlen  = len + prelen;
		const size_t plen =
			(PMASK & midlen) ? ((midlen + Page::size()) & ~PMASK) : midlen;

		const size_t n_pages = plen / Page::size();
		auto* decoder_array = new DecoderCache<Page::SIZE> [n_pages];
		this->m_exec_decoder =
			decoder_array[0].template get_base<W>() - pbase / DecoderCache<Page::SIZE>::DIVISOR;
		this->m_decoder_cache = &decoder_array[0];
		size_t dcindex = 0;
		while (len > 0)
		{
			const size_t size = std::min(Page::size(), len);
			const size_t pageno = page_number(addr);
			// find page itself
			auto it = m_pages.find(pageno);
			if (it != m_pages.end()) {
				auto& page = it->second;
				if (page.attr.exec) {
					// assign slice
					auto* cache = &decoder_array[dcindex];
					dcindex++;
#ifdef RISCV_INSTR_CACHE_PREGEN
					// fill start with illegal instructions
					const address_t start_offset = addr & (Page::size()-1);
					for (address_t off = 0; off < start_offset; off += 4)
					{
						cache->template get<W> (off / cache->DIVISOR) =
							machine().cpu.decode(rv32i_instruction{0}).handler;
					}
					// generate instruction handler pointers for machine code
					for (address_t dst = addr; dst < addr + size;)
					{
						const address_t offset = dst & (Page::size()-1);
						rv32i_instruction instruction;
						instruction.whole = *(uint32_t*) (page.data() + offset);

						cache->template get<W> (offset / cache->DIVISOR) =
							machine().cpu.decode(instruction).handler;

						dst += instruction.length();
					}
					// fill end with illegal instructions
					const address_t end_offset = (addr + size) & (Page::size()-1);
					if (end_offset != 0)
					for (address_t off = end_offset; off < Page::size(); off += 4)
					{
						cache->template get<W> (off / cache->DIVISOR) =
							machine().cpu.decode(rv32i_instruction{0}).handler;
					}
#else
					// the instructions will be decoded on-demand
					*cache = {};
#endif
				}
			}

			addr += size;
			len  -= size;
		}
	}
#endif

#ifndef __GNUG__
	template struct Memory<4>;
	template struct Memory<8>;
#endif
}
