#include "stdafx.h"
#include "PPUOpcodes.h"
#include "PPUModule.h"
#include "PPUAnalyser.h"

#include <unordered_set>

#include "yaml-cpp/yaml.h"

const ppu_decoder<ppu_itype> s_ppu_itype;
const ppu_decoder<ppu_iname> s_ppu_iname;

template<>
void fmt_class_string<bitset_t<ppu_attr>::raw_type>::format(std::string& out, u64 arg)
{
	out += "[UNIMPLEMENTED]";
}

void ppu_validate(const std::string& fname, const std::vector<ppu_function>& funcs, u32 reloc)
{
	// Load custom PRX configuration if available
	if (fs::file yml{fname + ".yml"})
	{
		const auto cfg = YAML::Load(yml.to_string());

		u32 index = 0;

		// Validate detected functions using information provided
		for (const auto func : cfg["functions"])
		{
			const u32 addr = func["addr"].as<u32>(-1);
			const u32 size = func["size"].as<u32>(0);

			if (addr != -1 && index < funcs.size())
			{
				u32 found = funcs[index].addr - reloc;

				while (addr > found && index + 1 < funcs.size())
				{
					LOG_ERROR(LOADER, "%s.yml : validation failed at 0x%x (0x%x, 0x%x)", fname, found, addr, size);
					index++;
					found = funcs[index].addr - reloc;
				}

				if (addr < found)
				{
					LOG_ERROR(LOADER, "%s.yml : function not found (0x%x, 0x%x)", fname, addr, size);
					continue;
				}

				if (size && size < funcs[index].size)
				{
					LOG_ERROR(LOADER, "%s.yml : function size mismatch at 0x%x(size=0x%x) (0x%x, 0x%x)", fname, found, funcs[index].size, addr, size);
				}

				if (size > funcs[index].size)
				{
					LOG_ERROR(LOADER, "%s.yml : function size mismatch at 0x%x(size=0x%x) (0x%x, 0x%x)", fname, found, funcs[index].size, addr, size);
				}

				index++;
			}
			else
			{
				LOG_ERROR(LOADER, "%s.yml : function not found at the end (0x%x, 0x%x)", fname, addr, size);
				break;
			}
		}

		if (!index)
		{
			return; // ???
		}

		while (index < funcs.size())
		{
			if (funcs[index].size)
			{
				LOG_ERROR(LOADER, "%s.yml : function not covered at 0x%x (size=0x%x)", fname, funcs[index].addr, funcs[index].size);
			}

			index++;
		}

		LOG_SUCCESS(LOADER, "%s.yml : validation completed", fname);
	}
}

static u32 ppu_test(const vm::cptr<u32> ptr, vm::cptr<void> fend, std::initializer_list<ppu_pattern> pat)
{
	vm::cptr<u32> cur = ptr;

	for (auto& p : pat)
	{
		if (cur >= fend)
		{
			return 0;
		}

		if (*cur == ppu_instructions::NOP())
		{
			cur++;

			if (cur >= fend)
			{
				return 0;
			}
		}

		if ((*cur & p.mask) != p.opcode)
		{
			return 0;
		}

		cur++;
	}

	return cur.addr() - ptr.addr();
}

static u32 ppu_test(vm::cptr<u32> ptr, vm::cptr<void> fend, std::initializer_list<std::initializer_list<ppu_pattern>> pats)
{
	for (auto pat : pats)
	{
		if (const u32 len = ppu_test(ptr, fend, pat))
		{
			return len;
		}
	}

	return 0;
}

namespace ppu_patterns
{
	using namespace ppu_instructions;

	const std::initializer_list<ppu_pattern> abort1
	{
		{ STDU(r1, r1, -0xc0) },
		{ MFLR(r0) },
		{ STD(r26, r1, 0x90) },
		{ STD(r27, r1, 0x98) },
		{ STD(r28, r1, 0xa0) },
		{ STD(r29, r1, 0xa8) },
		{ STD(r30, r1, 0xb0) },
		{ STD(r31, r1, 0xb8) },
		{ STD(r0, r1, 0xd0) },
		{ LI(r3, 4) },
		{ LI(r4, 0) },
		{ LI(r11, 0x3dc) },
		{ SC(0) },
		{ MR(r29, r1) },
		{ CLRLDI(r29, r29, 32) },
		{ LWZ(r4, r2, 0), 0xffff },
		{ ADDI(r31, r1, 0x70) },
		{ LI(r3, 1) },
		{ LI(r5, 0x19) },
		{ MR(r6, r31) },
		{ LWZ(r28, r29, 4) },
		{ LI(r11, 0x193) },
		{ SC(0) },
		{ ADDI(r26, r1, 0x78) },
		{ LD(r3, r28, 0x10) },
		{ MR(r4, r26) },
		{ B(0, false, true), 0x3fffffc }, // .hex2str
		{ LI(r5, 0x10) },
		{ CLRLDI(r4, r3, 32) },
		{ MR(r6, r31) },
		{ LI(r3, 1) },
		{ LI(r11, 0x193) },
		{ SC(0) },
		{ LWZ(r27, r2, 0), 0xffff },
		{ LI(r3, 1) },
		{ LI(r5, 1) },
		{ MR(r4, r27) },
		{ MR(r6, r31) },
		{ LI(r11, 0x193) },
		{ SC(0) },
		{ LD(r28, r28, 0) },
		{ CMPDI(cr7, r28, 0) },
		{ BEQ(cr7, +0x6c) },
		{ LWZ(r30, r2, 0), 0xffff },
		{ LI(r3, 1) },
		{ MR(r4, r30) },
		{ LI(r5, 0x19) },
		{ MR(r6, r31) },
		{ LI(r11, 0x193) },
		{ SC(0) },
		{ CLRLDI(r29, r28, 32) },
		{ CLRLDI(r4, r26, 32) },
		{ LD(r3, r29, 0x10) },
		{ 0, 0xffffffff }, // .hex2str
		{ LI(r5, 0x10) },
		{ CLRLDI(r4, r3, 32) },
		{ MR(r6, r31) },
		{ LI(r3, 1) },
		{ LI(r11, 0x193) },
		{ SC(0) },
		{ LI(r3, 1) },
		{ MR(r4, r27) },
		{ LI(r5, 1) },
		{ MR(r6, r31) },
		{ LI(r11, 0x193) },
		{ SC(0) },
		{ LD(r28, r29, 0) },
		{ CMPDI(cr7, r28, 0) },
		{ BNE(cr7, -0x60) },
		{ LWZ(r4, r2, 0), 0xffff },
		{ MR(r6, r31) },
		{ LI(r3, 1) },
		{ LI(r5, 0x27) },
		{ LI(r11, 0x193) },
		{ SC(0) },
		{ LI(r3, 1) },
		{ B(0, false, true), 0x3fffffc }, // .sys_process_exit
		{ LD(r2, r1, 0x28) },
		{ LI(r3, 1) },
		{ B(0, false, true), 0x3fffffc }, // .exit
	};

	const std::initializer_list<ppu_pattern> abort2
	{
		{ STDU(r1, r1, -0xc0) },
		{ MFLR(r0) },
		{ STD(r27, r1, 0x98) },
		{ STD(r28, r1, 0xa0) },
		{ STD(r29, r1, 0xa8) },
		{ STD(r30, r1, 0xb0) },
		{ STD(r31, r1, 0xb8) },
		{ STD(r0, r1, 0xd0) },
		{ MR(r9, r1) },
		{ CLRLDI(r9, r9, 32) },
		{ LWZ(r4, r2, 0), 0xffff },
		{ ADDI(r31, r1, 0x70) },
		{ LI(r3, 1) },
		{ LI(r5, 0x19) },
		{ MR(r6, r31) },
		{ LWZ(r29, r9, 4) },
		{ LI(r11, 0x193) },
		{ SC(0) },
		{ ADDI(r27, r1, 0x78) },
		{ LD(r3, r29, 0x10) },
		{ MR(r4, r27) },
		{ B(0, false, true), 0x3fffffc }, // .hex2str
		{ LI(r5, 0x10) },
		{ CLRLDI(r4, r3, 32) },
		{ MR(r6, r31) },
		{ LI(r3, 1) },
		{ LI(r11, 0x193) },
		{ SC(0) },
		{ LWZ(r28, r2, 0), 0xffff },
		{ LI(r3, 1) },
		{ LI(r5, 1) },
		{ MR(r4, r28) },
		{ MR(r6, r31) },
		{ LI(r11, 0x193) },
		{ SC(0) },
		{ LD(r29, r29, 0) },
		{ CMPDI(cr7, r29, 0) },
		{ BEQ(cr7, +0x6c) },
		{ LWZ(r30, r2, 0), 0xffff },
		{ LI(r3, 1) },
		{ MR(r4, r30) },
		{ LI(r5, 0x19) },
		{ MR(r6, r31) },
		{ LI(r11, 0x193) },
		{ SC(0) },
		{ CLRLDI(r29, r29, 32) },
		{ CLRLDI(r4, r27, 32) },
		{ LD(r3, r29, 0x10) },
		{ 0, 0xffffffff }, // .hex2str
		{ LI(r5, 0x10) },
		{ CLRLDI(r4, r3, 32) },
		{ MR(r6, r31) },
		{ LI(r3, 1) },
		{ LI(r11, 0x193) },
		{ SC(0) },
		{ LI(r3, 1) },
		{ MR(r4, r28) },
		{ LI(r5, 1) },
		{ MR(r6, r31) },
		{ LI(r11, 0x193) },
		{ SC(0) },
		{ LD(r29, r29, 0) },
		{ CMPDI(cr7, r29, 0) },
		{ BNE(cr7, -0x60) },
		{ LWZ(r4, r2, 0), 0xffff },
		{ MR(r6, r31) },
		{ LI(r3, 1) },
		{ LI(r5, 0x27) },
		{ LI(r11, 0x193) },
		{ SC(0) },
		{ LI(r3, 1) },
		{ B(0, false, true), 0x3fffffc }, // .sys_process_exit
		{ LD(r2, r1, 0x28) },
		{ LI(r3, 1) },
		{ B(0, false, true), 0x3fffffc }, // .exit
	};

	const std::initializer_list<std::initializer_list<ppu_pattern>> abort
	{
		abort1,
		abort2,
	};
}

std::vector<ppu_function> ppu_analyse(const std::vector<std::pair<u32, u32>>& segs, const std::vector<std::pair<u32, u32>>& secs, u32 lib_toc)
{
	// Assume first segment is executable
	const u32 start = segs[0].first;
	const u32 end = segs[0].first + segs[0].second;

	// Known TOCs (usually only 1)
	std::unordered_set<u32> TOCs;

	// Known functions
	std::map<u32, ppu_function> funcs;

	// Function analysis workload
	std::vector<std::reference_wrapper<ppu_function>> func_queue;

	// Register new function
	auto add_func = [&](u32 addr, u32 toc, u32 origin) -> ppu_function&
	{
		ppu_function& func = funcs[addr];

		if (func.addr)
		{
			// Update TOC (TODO: this doesn't work well, must update TOC recursively)
			if (func.toc == 0 || toc == -1)
			{
				func.toc = toc;
			}
			else if (toc && func.toc != -1 && func.toc != toc)
			{
				//LOG_WARNING(PPU, "Function 0x%x: TOC mismatch (0x%x vs 0x%x)", addr, toc, func.toc);
				func.toc = -1;
			}

			return func;
		}

		func_queue.emplace_back(func);
		func.addr = addr;
		func.toc = toc;
		LOG_TRACE(PPU, "Function 0x%x added (toc=0x%x, origin=0x%x)", addr, toc, origin);
		return func;
	};

	// Register new TOC and find basic set of functions
	auto add_toc = [&](u32 toc)
	{
		if (!toc || toc == -1 || !TOCs.emplace(toc).second)
		{
			return;
		}

		// Grope for OPD section (TODO: optimization, better constraints)
		for (const auto& seg : segs)
		{
			for (vm::cptr<u32> ptr = vm::cast(seg.first); ptr.addr() < seg.first + seg.second; ptr++)
			{
				if (ptr[0] >= start && ptr[0] < end && ptr[0] % 4 == 0 && ptr[1] == toc)
				{
					// New function
					LOG_NOTICE(PPU, "OPD*: [0x%x] 0x%x (TOC=0x%x)", ptr, ptr[0], ptr[1]);
					add_func(*ptr, toc, ptr.addr());
					ptr++;
				}
			}
		}
	};

	// Get next reliable function address
	auto get_limit = [&](u32 addr) -> u32
	{
		for (auto it = funcs.lower_bound(addr), end = funcs.end(); it != end; it++)
		{
			if (it->second.attr & ppu_attr::known_addr)
			{
				return it->first;
			}
		}

		return end;
	};

	// Find OPD section
	for (const auto& sec : secs)
	{
		vm::cptr<void> sec_end = vm::cast(sec.first + sec.second);

		// Probe
		for (vm::cptr<u32> ptr = vm::cast(sec.first); ptr < sec_end; ptr += 2)
		{
			if (ptr + 6 <= sec_end && !ptr[0] && !ptr[2] && ptr[1] == ptr[4] && ptr[3] == ptr[5])
			{
				// Special OPD format case (some homebrews)
				ptr += 4;
			}

			if (ptr + 2 > sec_end)
			{
				sec_end.set(0);
				break;
			}

			const u32 addr = ptr[0];
			const u32 _toc = ptr[1];

			// Rough Table of Contents borders
			const u32 _toc_begin = _toc - 0x8000;
			const u32 _toc_end = _toc + 0x8000;

			// TODO: improve TOC constraints
			if (_toc % 4 || _toc == 0 || _toc >= 0x40000000 || (_toc >= start && _toc < end))
			{
				sec_end.set(0);
				break;
			}

			if (addr % 4 || addr < start || addr >= end || addr == _toc)
			{
				sec_end.set(0);
				break;
			}
		}

		if (sec_end) LOG_NOTICE(PPU, "Reading OPD section at 0x%x...", sec.first);

		// Mine
		for (vm::cptr<u32> ptr = vm::cast(sec.first); ptr < sec_end; ptr += 2)
		{
			// Special case: see "Probe"
			if (!ptr[0]) ptr += 4;

			// Add function and TOC
			const u32 addr = ptr[0];
			const u32 toc = ptr[1];
			LOG_NOTICE(PPU, "OPD: [0x%x] 0x%x (TOC=0x%x)", ptr, addr, toc);

			TOCs.emplace(toc);
			auto& func = add_func(addr, toc, ptr.addr());
			func.attr += ppu_attr::known_addr;
		}
	}

	// Secondary attempt (TODO, needs better strategy)
	if (/*secs.empty() &&*/ lib_toc)
	{
		add_toc(lib_toc);
	}

	// Find .eh_frame section
	for (const auto& sec : secs)
	{
		vm::cptr<void> sec_end = vm::cast(sec.first + sec.second);

		// Probe
		for (vm::cptr<u32> ptr = vm::cast(sec.first); ptr < sec_end;)
		{
			if (ptr % 4 || ptr.addr() < sec.first || ptr >= sec_end)
			{
				sec_end.set(0);
				break;
			}

			const u32 size = ptr[0] + 4;

			if (size == 4 && ptr + 1 == sec_end)
			{
				// Null terminator
				break;
			}

			if (size % 4 || size < 0x10 || ptr + size / 4 > sec_end)
			{
				sec_end.set(0);
				break;
			}

			if (ptr[1])
			{
				const u32 cie_off = ptr.addr() - ptr[1] + 4;

				if (cie_off % 4 || cie_off < sec.first || cie_off >= sec_end.addr())
				{
					sec_end.set(0);
					break;
				}
			}

			ptr = vm::cast(ptr.addr() + size);
		}

		if (sec_end && sec.second > 4) LOG_NOTICE(PPU, "Reading .eh_frame section at 0x%x...", sec.first);

		// Mine
		for (vm::cptr<u32> ptr = vm::cast(sec.first); ptr < sec_end; ptr = vm::cast(ptr.addr() + ptr[0] + 4))
		{
			if (ptr[0] == 0)
			{
				// Null terminator
				break;
			}

			if (ptr[1] == 0)
			{
				// CIE
				LOG_NOTICE(PPU, ".eh_frame: [0x%x] CIE 0x%x", ptr, ptr[0]);
			}
			else
			{
				// Get associated CIE (currently unused)
				const vm::cptr<u32> cie = vm::cast(ptr.addr() - ptr[1] + 4);

				u32 addr = 0;
				u32 size = 0;

				// TODO: 64 bit or 32 bit values (approximation)
				if (ptr[2] == 0 && ptr[3] == 0)
				{
					size = ptr[5];
				}
				else if ((ptr[2] == -1 || ptr[2] == 0) && ptr[4] == 0 && ptr[5])
				{
					addr = ptr[3];
					size = ptr[5];
				}
				else if (ptr[2] != -1 && ptr[3])
				{
					addr = ptr[2];
					size = ptr[3];
				}
				else
				{
					LOG_ERROR(PPU, ".eh_frame: [0x%x] 0x%x, 0x%x, 0x%x, 0x%x, 0x%x", ptr, ptr[0], ptr[1], ptr[2], ptr[3], ptr[4]);
					continue;
				}

				// TODO: absolute/relative offset (approximation)
				if (addr > 0xc0000000)
				{
					addr += ptr.addr() + 8;
				}

				LOG_NOTICE(PPU, ".eh_frame: [0x%x] FDE 0x%x (cie=*0x%x, addr=0x%x, size=0x%x)", ptr, ptr[0], cie, addr, size);

				// TODO: invalid offsets, zero offsets (removed functions?)
				if (addr % 4 || size % 4 || size > (end - start) || addr < start || addr + size > end)
				{
					if (addr) LOG_ERROR(PPU, ".eh_frame: Invalid function 0x%x", addr);
					continue;
				}

				auto& func = add_func(addr, 0, ptr.addr());
				func.attr += ppu_attr::known_addr;
				func.attr += ppu_attr::known_size;
				func.size = size;
			}
		}
	}

	// Main loop (func_queue may grow)
	for (std::size_t i = 0; i < func_queue.size(); i++)
	{
		ppu_function& func = func_queue[i];

		if (func.blocks.empty())
		{
			// Special function analysis
			const vm::cptr<u32> ptr = vm::cast(func.addr);
			const vm::cptr<void> fend = vm::cast(end);

			using namespace ppu_instructions;

			if (ptr + 1 <= fend && (ptr[0] & 0xfc000001) == B({}, {}))
			{
				// Simple gate
				const u32 target = (ptr[0] & 0x2 ? 0 : ptr.addr()) + ppu_opcode_t{ptr[0]}.bt24;

				if (target == func.addr)
				{
					// Special case
					func.size = 0x4;
					func.blocks.emplace(func.addr, func.size);
					func.attr += ppu_attr::no_return;
					continue;
				}
				
				if (target >= start && target < end)
				{
					auto& new_func = add_func(target, func.toc, func.addr);

					if (new_func.blocks.empty())
					{
						func_queue.emplace_back(func);
						continue;
					}

					func.size = 0x4;
					func.blocks.emplace(func.addr, func.size);
					func.attr += new_func.attr & ppu_attr::no_return;
					func.called_from.emplace(target);
					func.gate_target = target;
					continue;
				}
			}

			if (ptr + 4 <= fend &&
				ptr[0] == STD(r2, r1, 0x28) &&
				(ptr[1] & 0xffff0000) == ADDIS(r2, r2, {}) &&
				(ptr[2] & 0xffff0000) == ADDI(r2, r2, {}) &&
				(ptr[3] & 0xfc000001) == B({}, {}))
			{
				// TOC change gate
				const u32 new_toc = func.toc && func.toc != -1 ? func.toc + (ptr[1] << 16) + s16(ptr[2]) : 0;
				const u32 target = (ptr[3] & 0x2 ? 0 : (ptr + 3).addr()) + ppu_opcode_t{ptr[3]}.bt24;

				if (target >= start && target < end)
				{
					add_toc(new_toc);

					auto& new_func = add_func(target, new_toc, func.addr);

					if (new_func.blocks.empty())
					{
						func_queue.emplace_back(func);
						continue;
					}

					func.size = 0x10;
					func.blocks.emplace(func.addr, func.size);
					func.attr += new_func.attr & ppu_attr::no_return;
					func.called_from.emplace(target);
					func.gate_target = target;
					continue;
				}
			}

			if (ptr + 8 <= fend &&
				(ptr[0] & 0xffff0000) == LI(r12, 0) &&
				(ptr[1] & 0xffff0000) == ORIS(r12, r12, 0) &&
				(ptr[2] & 0xffff0000) == LWZ(r12, r12, 0) &&
				ptr[3] == STD(r2, r1, 0x28) &&
				ptr[4] == LWZ(r0, r12, 0) &&
				ptr[5] == LWZ(r2, r12, 4) &&
				ptr[6] == MTCTR(r0) &&
				ptr[7] == BCTR())
			{
				// The most used simple import stub
				func.size = 0x20;
				func.blocks.emplace(func.addr, func.size);
				func.attr += ppu_attr::known_addr;
				func.attr += ppu_attr::known_size;
				continue;
			}

			if (const u32 len = ppu_test(ptr, fend, ppu_patterns::abort))
			{
				// Function "abort"
				LOG_NOTICE(PPU, "Function [0x%x]: 'abort'", func.addr);
				func.attr += ppu_attr::no_return;
				func.attr += ppu_attr::known_size;
				func.size = len;
			}

			if (ptr + 3 <= fend &&
				(ptr[0] & 0xffff0000) == LI(r0, 0) &&
				(ptr[1] & 0xffff0000) == ORIS(r0, r0, 0) &&
				(ptr[2] & 0xfc000003) == B({}, {}, {}))
			{
				// Import stub with r0 usage
				func.attr += ppu_attr::uses_r0;
			}

			// TODO: detect no_return, scribe more TODOs

			// Acknowledge completion
			func.blocks.emplace(vm::cast(func.addr), 0);
		}

		// Get function limit
		const u32 func_end = std::min<u32>(get_limit(func.addr + 1), func.attr & ppu_attr::known_size ? func.addr + func.size : end);

		// Block analysis workload
		std::vector<std::reference_wrapper<std::pair<const u32, u32>>> block_queue;

		// Add new block for analysis
		auto add_block = [&](u32 addr) -> bool
		{
			if (addr < func.addr || addr >= func_end)
			{
				return false;
			}

			const auto _pair = func.blocks.emplace(addr, 0);

			if (_pair.second)
			{
				block_queue.emplace_back(*_pair.first);
				return true;
			}

			return false;
		};

		for (auto& block : func.blocks)
		{
			if (!block.second && block.first < func_end)
			{
				block_queue.emplace_back(block);
			}
		}

		// TODO: lower priority?
		if (func.attr & ppu_attr::no_size)
		{
			// Get next function
			const auto _next = funcs.lower_bound(func.blocks.crbegin()->first + 1);

			// Get limit
			const u32 func_end2 = _next == funcs.end() ? func_end : std::min<u32>(_next->first, func_end);

			// Find more block entries
			for (const auto& seg : segs)
			{
				for (vm::cptr<u32> ptr = vm::cast(seg.first); ptr.addr() < seg.first + seg.second; ptr++)
				{
					const u32 value = *ptr;

					if (value % 4 == 0 && value >= func.addr && value < func_end2)
					{
						add_block(value);
					}
				}
			}
		}

		const bool was_empty = block_queue.empty();

		// Block loop (block_queue may grow, may be aborted via clearing)
		for (std::size_t j = 0; j < block_queue.size(); j++)
		{
			auto& block = block_queue[j].get();

			for (vm::cptr<u32> _ptr = vm::cast(block.first); _ptr.addr() < func_end;)
			{
				const u32 iaddr = _ptr.addr();
				const ppu_opcode_t op{*_ptr++};
				const ppu_itype::type type = s_ppu_itype.decode(op.opcode);

				if (type == ppu_itype::UNK)
				{
					// Invalid blocks will remain empty
					break;
				}
				else if (type == ppu_itype::B || type == ppu_itype::BC)
				{
					const u32 target = (op.aa ? 0 : iaddr) + (type == ppu_itype::B ? +op.bt24 : +op.bt14);

					if (target < start || target >= end)
					{
						LOG_WARNING(PPU, "[0x%x] Invalid branch at 0x%x -> 0x%x", func.addr, iaddr, target);
						continue;
					}

					const bool is_call = op.lk && target != iaddr;
					const auto pfunc = is_call ? &add_func(target, 0, func.addr) : nullptr;

					if (pfunc && pfunc->blocks.empty())
					{
						// Postpone analysis (no info)
						block_queue.clear();
						break;
					}
					
					// Add next block if necessary
					if ((is_call && !pfunc->attr.test(ppu_attr::no_return)) || (type == ppu_itype::BC && (op.bo & 0x14) != 0x14))
					{
						add_block(_ptr.addr());
					}

					if (op.lk && (target == iaddr || pfunc->attr.test(ppu_attr::no_return)))
					{
						// Nothing
					}
					else if (is_call || target < func.addr || target >= func_end)
					{
						// Add function call (including obvious tail call)
						add_func(target, 0, func.addr);
					}
					else
					{
						// Add block
						add_block(target);
					}

					block.second = _ptr.addr() - block.first;
					break;
				}
				else if (type == ppu_itype::BCLR)
				{
					if (op.lk || (op.bo & 0x14) != 0x14)
					{
						add_block(_ptr.addr());
					}

					block.second = _ptr.addr() - block.first;
					break;
				}
				else if (type == ppu_itype::BCCTR)
				{
					if (op.lk || (op.bo & 0x10) != 0x10)
					{
						add_block(_ptr.addr());
					}
					else
					{
						// Analyse jumptable (TODO)
						const u32 jt_addr = _ptr.addr();
						const u32 jt_end = func_end;

						for (; _ptr.addr() < jt_end; _ptr++)
						{
							const u32 addr = jt_addr + *_ptr;

							if (addr == jt_addr)
							{
								// TODO (cannot branch to jumptable itself)
								break;
							}

							if (addr % 4 || addr < func.addr || addr >= jt_end)
							{
								break;
							}

							add_block(addr);
						}

						if (jt_addr != jt_end && _ptr.addr() == jt_addr)
						{
							// Acknowledge jumptable detection failure
							if (!func.attr.test_and_set(ppu_attr::no_size))
							{
								LOG_WARNING(PPU, "[0x%x] Jump table not found! 0x%x-0x%x", func.addr, jt_addr, jt_end);
							}

							add_block(iaddr);
							block_queue.clear();
						}
						else
						{
							LOG_TRACE(PPU, "[0x%x] Jump table found: 0x%x-0x%x", func.addr, jt_addr, _ptr);
						}
					}

					block.second = _ptr.addr() - block.first;
					break;
				}
			}
		}

		if (block_queue.empty() && !was_empty)
		{
			// Block aborted: abort function, postpone
			func_queue.emplace_back(func);
			continue;
		}
		
		// Finalization: determine function size
		if (!func.attr.test(ppu_attr::known_size))
		{
			const auto last = func.blocks.crbegin();

			if (last != func.blocks.crend())
			{
				func.size = last->first + last->second - func.addr;
			}
		}

		// Finalization: normalize blocks
		for (auto& block : func.blocks)
		{
			const auto next = func.blocks.upper_bound(block.first);

			// Normalize block if necessary
			if (next != func.blocks.end())
			{
				block.second = next->first - block.first;
			}

			// Invalidate blocks out of the function
			const u32 fend = func.addr + func.size;
			const u32 bend = block.first + block.second;

			if (block.first >= fend)
			{
				block.second = 0;
			}
			else if (bend > fend)
			{
				block.second -= bend - fend;
			}
		}

		// Finalization: process remaining tail calls
		for (const auto& block : func.blocks)
		{
			for (vm::cptr<u32> _ptr = vm::cast(block.first); _ptr.addr() < block.first + block.second;)
			{
				const u32 iaddr = _ptr.addr();
				const ppu_opcode_t op{*_ptr++};
				const ppu_itype::type type = s_ppu_itype.decode(op.opcode);

				if (type == ppu_itype::B || type == ppu_itype::BC)
				{
					const u32 target = (op.aa ? 0 : iaddr) + (type == ppu_itype::B ? +op.bt24 : +op.bt14);

					if (target >= start && target < end)
					{
						if (target < func.addr || target >= func.addr + func.size)
						{
							func.called_from.emplace(target);
							add_func(target, func.toc, func.addr);
						}
					}
				}
				else if (type == ppu_itype::BCCTR && !op.lk)
				{
					// Jumptable (do not touch entries)
					break;
				}
			}
		}

		// Finalization: decrease known function size (TODO)
		if (func.attr & ppu_attr::known_size)
		{
			const auto last = func.blocks.crbegin();

			if (last != func.blocks.crend())
			{
				func.size = std::min<u32>(func.size, last->first + last->second - func.addr);
			}
		}
	}

	// Function shrinkage, disabled (TODO: it's potentially dangerous but improvable)
	for (auto& _pair : funcs)
	{
		auto& func = _pair.second;

		// Get next function addr
		const auto _next = funcs.lower_bound(_pair.first + 1);

		const u32 next = _next == funcs.end() ? end : _next->first;

		// Just ensure that functions don't overlap
		if (func.addr + func.size > next)
		{
			LOG_WARNING(PPU, "Function overlap: [0x%x] 0x%x -> 0x%x", func.addr, func.size, next - func.addr);
			continue; //func.size = next - func.addr;

			// Also invalidate blocks
			for (auto& block : func.blocks)
			{
				if (block.first + block.second > next)
				{
					block.second = block.first >= next ? 0 : next - block.first;
				}
			}
		}
	}
	
	// Convert map to vector (destructive)
	std::vector<ppu_function> result;

	for (auto&& func : funcs)
	{
		result.emplace_back(std::move(func.second));
	}

	LOG_NOTICE(PPU, "Function analysis: %zu functions (%zu enqueued)", result.size(), func_queue.size());

	return result;
}
