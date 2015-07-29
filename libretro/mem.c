/* vim: ts=3 sts=3 sw=3 et list :
 * Copyright 2015 Higor Eurípedes
 * This file is part of blastem-libretro.
 * This file is licensed under whatever license BlastEm is licensed under.
 *
 * Mapping logic is courtesy of ToadKing.
 */
 
#include <sys/mman.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>

#include "mem.h"
#ifdef X86_64
int hint_location;
#  if defined(__APPLE__)
#    define PAGE_MASK   (4096-1)
#  elif defined(_WIN32)
#    include <windows.h>
static SYSTEM_INFO sys_info;
#    define PAGE_MASK   (sys_info.dwPageSize - 1)
#  else
#    define PAGE_MASK   (getpagesize() - 1)
#  endif
#  define round_page(x) ((((uintptr_t)(x)) + PAGE_MASK) & ~(PAGE_MASK))
#endif

#ifdef _WIN32
#  ifdef X86_64
static void *search_for_free_mem(size_t size)
{
   static uintptr_t last_addr = 0;

   if (!last_addr)
      last_addr = (uintptr_t) &hint_location - sys_info.dwPageSize;

   last_addr -= size;

   MEMORY_BASIC_INFORMATION info;
   while (VirtualQuery((void *)last_addr, &info, sizeof(info)) == sizeof(info))
   {
      // went too far, unusable for executable memory
      if (last_addr + 0x80000000 < (uintptr_t)&hint_location)
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
#  endif
void *alloc_code(size_t *size)
{
   void *ret;
#  ifdef X86_64
   if ((uintptr_t) &hint_location > 0xFFFFFFFFULL)
   {
      if (!last_addr)
         GetSystemInfo(&sys_info);

      size_t _size = round_page(*size);
      ret = search_for_free_mem(_size);
      if (ret)
         ret = VirtualAlloc(ret, _size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
   }
   else
#endif
      ret = VirtualAlloc(NULL, *size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);

   if (ret == NULL)
      fprintf(stderr, "VirtualAlloc failed: %d\n", GetLastError());

   return ret;
}
#else
#  ifndef MAP_ANONYMOUS
#    define MAP_ANONYMOUS MAP_ANON
#  endif
#  ifndef MAP_32BIT
#    define MAP_32BIT 0
#  endif
void *alloc_code(size_t *size)
{
   static char *map_hint = NULL;
   void *ret;
#  ifdef X86_64
   if (!map_hint)
   {
      if (hint_location > 0xFFFFFFFFULL)
         map_hint = (char*)round_page(&hint_location) - 0x20000000;
      else
         map_hint = (char*)0x20000000;
   } else if ((uintptr_t) map_hint > 0xFFFFFFFFULL)
      map_hint -= round_page(*size);
#  endif

   ret = mmap(map_hint, *size, PROT_EXEC | PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | (MAP_32BIT * (map_hint==0)), -1, 0);

   if (ret == MAP_FAILED)
   {
      perror("Failed to map executable memory");
      return NULL;
   }

#  ifdef X86_64
   if ((uintptr_t) map_hint <= 0xFFFFFFFF)
      map_hint += round_page(*size);
#  endif
   return ret; 
}
#endif // _WIN32
