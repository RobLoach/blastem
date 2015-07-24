/*
 Copyright 2013 Michael Pavone
 This file is part of BlastEm.
 BlastEm is free software distributed under the terms of the GNU General Public License version 3 or greater. See COPYING for full license text.
*/

#include "mem.h"
#include <windows.h>

#ifdef __amd64
int hint_location;
static SYSTEM_INFO sys_info;
#define PAGE_MASK     (sys_info.dwPageSize - 1)
#define round_page(x) ((((uintptr_t)(x)) + PAGE_MASK) & ~(PAGE_MASK))
#endif

#if defined(_WIN32) && defined(__amd64)
static uintptr_t last_addr;
static void *search_for_free_mem(size_t size)
{
	if (!last_addr)
		last_addr = (uintptr_t) &hint_location - sys_info.dwPageSize;
	last_addr -= size;

	MEMORY_BASIC_INFORMATION info;
	while (VirtualQuery((void *)last_addr, &info, sizeof(info)) == sizeof(info))
	{
		// went too far, unusable for executable memory
		if (last_addr + 0x80000000 < (uintptr_t) &hint_location)
			return NULL;

		uintptr_t end = last_addr + size;
		if (info.State != MEM_FREE)
		{
			last_addr = (uintptr_t) info.AllocationBase - size;
			continue;
		}

		if ((uintptr_t)info.BaseAddress + (uintptr_t)info.RegionSize >= end &&
				(uintptr_t)info.BaseAddress <= last_addr)
			return (void *)last_addr;

		last_addr -= size;
	}

	return NULL;
}
#endif

void * alloc_code(size_t *size)
{
	void *ptr;
#ifdef __amd64
	if ((uintptr_t) &hint_location > 0xFFFFFFFFULL)
	{
		if (!last_addr)
			GetSystemInfo(&sys_info);

		size_t _size = round_page(*size);
		ptr = SearchForFreeMem(_size);
		if (ptr)
			ptr = VirtualAlloc(ptr, _size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
	}
	else
#endif
		ptr = VirtualAlloc(NULL, *size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);

	if (ptr == NULL)
		fprintf(stderr, "VirtualAlloc failed: %d\n", GetLastError());

	return ptr;
}
