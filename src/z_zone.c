
/* Z_zone.c */

#include "doomdef.h"

/*
==============================================================================

						ZONE MEMORY ALLOCATION

There is never any space between memblocks, and there will never be two
contiguous free memblocks.

The rover can be left pointing at a non-empty block

It is of no value to free a cachable block, because it will get overwritten
automatically if needed

==============================================================================
*/

#define DEBUG_ 0

#define BLOCKALIGN(size,align) (((size) + ((align)-1)) & ~((align)-1))

extern u32 NextFrameIdx;

memzone_t *mainzone;

#define MEM_HEAP_SIZE (0x540000) 

/*
========================
=
= Z_Init
=
========================
*/

void Z_Init(void)
{
	byte *mem = (byte *)memalign(32,MEM_HEAP_SIZE);

	if (!mem)
		I_Error("failed to allocate %08x zone heap");

	/* mars doesn't have a refzone */
	mainzone = Z_InitZone(mem, MEM_HEAP_SIZE);
}

/*
========================
=
= Z_InitZone
=
========================
*/

memzone_t *Z_InitZone(byte *base, int size)
{
	memzone_t *zone;

	zone = (memzone_t *)base;
	zone->size = size;
	zone->rover = &zone->blocklist;
	zone->rover2 = &zone->blocklist;
	zone->rover3 = &zone->blocklist;
	zone->blocklist.size =
		size - (int)((byte *)&zone->blocklist - (byte *)zone);
	zone->blocklist.user = NULL;
	zone->blocklist.tag = 0;
	zone->blocklist.id = ZONEID;
	zone->blocklist.next = NULL;
	zone->blocklist.prev = NULL;
	zone->blocklist.lockframe = -1;

	return zone;
}

/*
========================
=
= Z_SetAllocBase
= Exclusive Doom64
=
========================
*/

void Z_SetAllocBase(memzone_t *mainzone)
{
	mainzone->rover2 = mainzone->rover;
}

/*
========================
=
= Z_Malloc2
=
= You can pass a NULL user if the tag is < PU_PURGELEVEL
========================
*/

#define MINFRAGMENT 32

void *Z_Malloc2(memzone_t *mainzone, int size, int tag, void *user)
{
	int extra;
	memblock_t *start, *rover, *newblock, *base;

#if DEBUG_
	Z_CheckZone(mainzone); /* DEBUG */
#endif

	if (backres[10] != 0xc3) {
		I_Error("failed allocation on %i", size);
	}

	/* */
	/* scan through the block list looking for the first free block */
	/* of sufficient size, throwing out any purgable blocks along the way */
	/* */

	size += sizeof(memblock_t); /* account for size of block header */
	size = BLOCKALIGN(size, 16); /* phrase align everything */

	start = base = mainzone->rover;

	while (base->user || base->size < size) {
		if (base->user)
			rover = base;
		else
			rover = base->next;

		if (!rover)
			goto backtostart;

		if (rover->user) {
			if (!(rover->tag & PU_PURGELEVEL)) {
				if ((!(rover->tag & PU_CACHE)) || ((u32)rover->lockframe >= (NextFrameIdx - 1))) {
					/* hit an in use block, so move base past it */
					base = rover->next;
					if (!base) {
backtostart:
						base = mainzone->rover2;
					}

					if (base == start) { /* scaned all the way around the list */
						Z_DumpHeap(mainzone);
						I_Error("failed allocation on %i", size);
					}
					continue;
				}
			}

			/* */
			/* free the rover block (adding the size to base) */
			/* */
			Z_Free((byte *)rover + sizeof(memblock_t)); /* mark as free */
		}

		if (base != rover) { /* merge with base */
			base->size += rover->size;
			base->next = rover->next;
			if (rover->next)
				rover->next->prev = base;
			else
				mainzone->rover3 = base;
		}
	}

	/* */
	/* found a block big enough */
	/* */
	extra = base->size - size;
	if (extra > MINFRAGMENT) {
		/* there will be a free fragment after the allocated block */
		newblock = (memblock_t *)((byte *)base + size);
		newblock->prev = base;
		newblock->next = base->next;
		if (newblock->next)
			newblock->next->prev = newblock;
		else
			mainzone->rover3 = newblock;

		base->next = newblock;
		base->size = size;

		newblock->size = extra;
		newblock->user = NULL; /* free block */
		newblock->tag = 0;
		newblock->id = ZONEID;
	}

	if (user) {
		base->user = user; /* mark as an in use block */
		*(void **)user = (void *)((byte *)base + sizeof(memblock_t));
	} else {
		if (tag >= PU_PURGELEVEL)
			I_Error("an owner is required for purgable blocks");
		base->user = (void *)1; /* mark as in use, but unowned	 */
	}

	base->tag = tag;
	base->id = ZONEID;
	base->lockframe = NextFrameIdx;

	mainzone->rover = base->next; /* next allocation will start looking here */
	if (!mainzone->rover) {
		mainzone->rover3 = base;
		mainzone->rover = mainzone->rover2;
	}

#if DEBUG_
	Z_CheckZone(mainzone); /* DEBUG */
#endif

	return (void *)((byte *)base + sizeof(memblock_t));
}

/*
========================
=
= Z_Alloc2
=
= You can pass a NULL user if the tag is < PU_PURGELEVEL
= Exclusive Psx Doom
========================
*/

void *Z_Alloc2(memzone_t *mainzone, int size, int tag, void *user)
{
	int extra;
	memblock_t *rover, *base, *block, *newblock;

#if DEBUG_
	Z_CheckZone(mainzone); /* DEBUG */
#endif

	/* */
	/* scan through the block list looking for the first free block */
	/* of sufficient size, throwing out any purgable blocks along the way */
	/* */

	size += sizeof(memblock_t); /* account for size of block header */
	size = BLOCKALIGN(size, 16); /* phrase align everything */

	base = mainzone->rover3;

	while (base->user || base->size < size) {
		if (base->user) {
			rover = base;
		} else {
			/* hit an in use block, so move base past it */
			rover = base->prev;
			if (!rover) {
				Z_DumpHeap(mainzone);
				I_Error("failed allocation on %i", size);
			}
		}

		if (rover->user) {
			if (!(rover->tag & PU_PURGELEVEL)) {
				if ((!(rover->tag & PU_CACHE)) || ((u32)rover->lockframe >= (NextFrameIdx - 1))) {
					/* hit an in use block, so move base past it */
					base = rover->prev;
					if (!base) {
						Z_DumpHeap(mainzone);
						I_Error("failed allocation on %i", size);
					}
					continue;
				}
			}

			/* */
			/* free the rover block (adding the size to base) */
			/* */
			Z_Free((byte *)rover + sizeof(memblock_t)); /* mark as free */
		}

		if (base != rover) {
			/* merge with base */
			rover->size += base->size;
			rover->next = base->next;

			if (base->next)
				base->next->prev = rover;
			else
				mainzone->rover3 = rover;

			base = rover;
		}
	}

	/* */
	/* found a block big enough */
	/* */
	extra = (base->size - size);

	newblock = base;
	block = base;

	if (extra > MINFRAGMENT) {
		block = (memblock_t *)((byte *)base + extra);
		block->prev = newblock;
		block->next = (void *)newblock->next;

		if (newblock->next)
			newblock->next->prev = block;

		newblock->next = block;
		block->size = size;
		newblock->size = extra;
		newblock->user = 0;
		newblock->tag = 0;
		newblock->id = ZONEID;
	}

	if (block->next == 0)
		mainzone->rover3 = block;

	if (user) {
		block->user = user; /* mark as an in use block */
		*(void **)user = (void *)((byte *)block + sizeof(memblock_t));
	} else {
		if (tag >= PU_PURGELEVEL)
			I_Error("an owner is required for purgable blocks");
		block->user = (void *)1; /* mark as in use, but unowned	 */
	}

	block->id = ZONEID;
	block->tag = tag;
	block->lockframe = NextFrameIdx;

#if DEBUG_
	Z_CheckZone(mainzone); /* DEBUG */
#endif

	return (void *)((byte *)block + sizeof(memblock_t));
}

/*
========================
=
= Z_Free2
=
========================
*/

void Z_Free2(memzone_t *mainzone, void *ptr)
{
	(void)mainzone;
	memblock_t *block;

	block = (memblock_t *)((byte *)ptr - sizeof(memblock_t));
	if (block->id != ZONEID)
		I_Error("freed a pointer without ZONEID");

	if (block->user > (void **)0x100) /* smaller values are not pointers */
		*block->user = 0; /* clear the user's mark */
	block->user = NULL; /* mark as free */
	block->tag = 0;
}

/*
========================
=
= Z_FreeTags
=
========================
*/

void Z_FreeTags(memzone_t *mainzone, int tag)
{
	memblock_t *block, *next;

	for (block = &mainzone->blocklist; block; block = next) {
		/* get link before freeing */
		next = block->next;

		/* free block */
		if (block->user) {
			if (block->tag & tag) {
				Z_Free((byte *)block + sizeof(memblock_t));
			}
		}
	}

	for (block = &mainzone->blocklist; block; block = next) {
		/* get link before freeing */
		next = block->next;

		if (!block->user && next && !next->user) {
			block->size += next->size;
			block->next = next->next;
			if (next->next)
				next->next->prev = block;
			next = block;
		}
	}

	mainzone->rover = &mainzone->blocklist;
	mainzone->rover2 = &mainzone->blocklist;
	mainzone->rover3 = &mainzone->blocklist;

	block = mainzone->blocklist.next;
	while (block) {
		mainzone->rover3 = block;
		block = block->next;
	}
}

/*
========================
=
= Z_Touch
= Exclusive Doom64
=
========================
*/

void Z_Touch(void *ptr)
{
	memblock_t *block;
	block = (memblock_t *)((byte *)ptr - sizeof(memblock_t));
	if (block->id != ZONEID)
		I_Error("touched a pointer without ZONEID");

	block->lockframe = NextFrameIdx;
}

/*
========================
=
= Z_CheckZone
=
========================
*/

void Z_CheckZone(memzone_t *mainzone)
{
	memblock_t *checkblock;
	int size;

	for (checkblock = &mainzone->blocklist; checkblock;
	     checkblock = checkblock->next) {
		if (checkblock->id != ZONEID)
			I_Error("block missing ZONEID");

		if (!checkblock->next) {
			size = (byte *)checkblock + checkblock->size -
			       (byte *)mainzone;
			if (size != mainzone->size)
				I_Error("zone size changed from %d to %d",
					mainzone->size, size);
			break;
		}

		if ((byte *)checkblock + checkblock->size !=
		    (byte *)checkblock->next)
			I_Error("block size does not touch the next block");
		if (checkblock->next->prev != checkblock)
			I_Error("next block doesn't have proper back link");
	}

#if DEBUG_
	Z_DumpHeap(mainzone);
#endif
}

/*
========================
=
= Z_ChangeTag
=
========================
*/

void Z_ChangeTag(void *ptr, int tag)
{
	memblock_t *block;

	block = (memblock_t *)((byte *)ptr - sizeof(memblock_t));
	if (block->id != ZONEID)
		I_Error("freed a pointer without ZONEID");
	if (tag >= PU_PURGELEVEL && (int)block->user < 0x100)
		I_Error("an owner is required for purgable blocks");
	block->tag = tag;
	block->lockframe = NextFrameIdx;
}

/*
========================
=
= Z_FreeMemory
=
========================
*/

int Z_FreeMemory(memzone_t *mainzone)
{
	memblock_t *block;
	int free;

	free = 0;
	for (block = &mainzone->blocklist; block; block = block->next) {
		if (!block->user)
			free += block->size;
	}

	return free;
}

/*
========================
=
= Z_DumpHeap
=
========================
*/

void Z_DumpHeap(memzone_t *mainzone) // 8002D1C8
{
//#if !DEBUG_
	//(void)mainzone;
//#elif DEBUG_
#if 1
	memblock_t *block;

	printf("zone size: %i  location: %p\n", mainzone->size, mainzone);

	for (block = &mainzone->blocklist; block; block = block->next) {
		printf("block:%p    size:%7i    user:%p    tag:%3i    frame:%i\n",
		       block, block->size, block->user, block->tag,
		       block->lockframe);

		if (!block->next)
			continue;

		if ((byte *)block + block->size != (byte *)block->next)
			printf("ERROR: block size does not touch the next block\n");
		if (block->next->prev != block)
			printf("ERROR: next block doesn't have proper back link\n");
	}
#endif
}

#if 1
/*
========================
=
= Z_Defragment
= Merges adjacent free blocks in the memory zone
=
========================
*/

void Z_Defragment(memzone_t *zone)
{
	memblock_t *block, *next;

	if (!zone)
		I_Error("Null zone heap");

	// Start with the first block
	block = &zone->blocklist;

	while (block) {
		next = block->next;

		// If the current block and the next block are both free
		if (!block->user && next && !next->user) {
			// Merge the blocks
			block->size += next->size;
			block->next = next->next;

			if (next->next) {
				next->next->prev = block;
			} else {
				// Update rover3 if we've merged with the last block
				zone->rover3 = block;
			}

			// Do not advance to the next block to handle chained merges
			continue;
		}

		// Move to the next block
		block = next;
	}

	// Reset rovers to avoid any dangling pointers
	zone->rover = &zone->blocklist;
	zone->rover2 = &zone->blocklist;
	zone->rover3 = &zone->blocklist;

	// Update rover3 to the last block in the list
	block = &zone->blocklist;
	while (block->next) {
		block = block->next;
		zone->rover3 = block;
	}
}
#endif


#if 0
// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// Copyright(C) 1993-1997 Id Software, Inc.
// Copyright(C) 2005 Simon Howard
// Copyright(C) 2007-2012 Samuel Villarreal
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
// 02111-1307, USA.
//
//-----------------------------------------------------------------------------
//
// DESCRIPTION:
//      Zone Memory Allocation. Neat.
//
//-----------------------------------------------------------------------------

#include <stdlib.h>

#include "doomdef.h"

#if !N64_ZONE

#define ZONEID    0x1d4a11

typedef struct memblock_s memblock_t;

struct memblock_s {
    int id; // = ZONEID
    int tag;
    int size;
	int lockframe;
    void **user;
    memblock_t *prev;
    memblock_t *next;
};

// Linked list of allocated blocks for each tag type

static memblock_t *allocated_blocks[PU_MAX+1];

#define MEM_HEAP_SIZE (0x4C0000)
static size_t total_allocated;

//
// Z_InsertBlock
// Add a block into the linked list for its type.
//

static void Z_InsertBlock(memblock_t *block) {
	total_allocated += sizeof(memblock_t) + block->size;

    block->prev = NULL;
    block->next = allocated_blocks[block->tag];
    allocated_blocks[block->tag] = block;

    if(block->next != NULL) {
        block->next->prev = block;
    }
}

//
// Z_RemoveBlock
// Remove a block from its linked list.
//

static void Z_RemoveBlock(memblock_t *block) {
    // Unlink from list

	total_allocated -= block->size;

    if(block->prev == NULL) {
        allocated_blocks[block->tag] = block->next;    // Start of list
    }
    else {
        block->prev->next = block->next;
    }

    if(block->next != NULL) {
        block->next->prev = block->prev;
    }
}

//
// Z_Init
//

void Z_Init(void) {
	total_allocated = 0;
    memset(allocated_blocks, 0, sizeof(allocated_blocks));
}


//
// Z_Free
//

void __Z_Free(void* ptr, const char *file, int line) {
    memblock_t* block;

    block = (memblock_t *)((byte *)ptr - sizeof(memblock_t));

    if(block->id != ZONEID) {
        I_Error("freed a pointer\n without ZONEID\n (%s:%d)\n%08x", file, line, block->id);
    }

    // clear the user's mark
    if(block->user != NULL) {
        *block->user = NULL;
    }

    Z_RemoveBlock(block);

    // Free back to system
    free(block);
}

//
// Z_ClearCache
//
// Empty data from the cache list to allocate enough data of the size
// required.
//
// Returns true if any blocks were freed.
//
#if 0
static int Z_ClearAllCache(void) {
    memblock_t *block;
    memblock_t *next_block;
    int remaining;

    block = allocated_blocks[PU_CACHE];

    if(block == NULL) {
        // Cache is already empty.
        return false;
    }

    //
    // Search to the end of the PU_CACHE list.  The blocks at the end
    // of the list are the ones that have been free for longer and
    // are more likely to be unneeded now.
    //
    while(block->next != NULL) {
        block = block->next;
    }

    //
    // Search backwards through the list freeing blocks until we have
    // freed the amount of memory required.
    //
    remaining = MEM_HEAP_SIZE;

    while(remaining > 0) {
        if(block == NULL) {
            // No blocks left to free; we've done our best.
            break;
        }

        next_block = block->prev;

        Z_RemoveBlock(block);

        remaining -= block->size;

        if(block->user) {
            *block->user = NULL;
        }

        free(block);

        block = next_block;
    }

    return true;
}
#endif

static int Z_ClearCache(int size) {
    memblock_t *block;
    memblock_t *next_block;
    int remaining;

    block = allocated_blocks[PU_CACHE];

    if(block == NULL) {
        // Cache is already empty.
        return false;
    }

    //
    // Search to the end of the PU_CACHE list.  The blocks at the end
    // of the list are the ones that have been free for longer and
    // are more likely to be unneeded now.
    //
    while(block->next != NULL) {
        block = block->next;
    }

    //
    // Search backwards through the list freeing blocks until we have
    // freed the amount of memory required.
    //
    remaining = size;

    while(remaining > 0) {
        if(block == NULL) {
            // No blocks left to free; we've done our best.
            break;
        }

        next_block = block->prev;

        Z_RemoveBlock(block);

        remaining -= block->size;

        if(block->user) {
            *block->user = NULL;
        }

        free(block);

        block = next_block;
    }

    return true;
}

//
// Z_Malloc
// You can pass a NULL user if the tag is < PU_PURGELEVEL.
//

void *__Z_Malloc(int size, int tag, void *user, const char *file, int line) {
    memblock_t *newblock;
    unsigned char *data;
    void *result;

    if(tag < 0 || tag > PU_MAX) {
        I_Error("tag out of range: %i\n (%s:%d)", tag, file, line);
    }

    if(user == NULL && tag >= PU_PURGELEVEL) {
        I_Error("an owner\nis required for\npurgable blocks\n (%s:%d)", file, line);
    }

    // Malloc a block of the required size

    newblock = NULL;

	if ((total_allocated + sizeof(memblock_t) + size) >= MEM_HEAP_SIZE) {
		Z_ClearCache(sizeof(memblock_t) + size);
	}

    if(!(newblock = (memblock_t*)malloc(sizeof(memblock_t) + size))) {
        if(Z_ClearCache(sizeof(memblock_t) + size)) {
            newblock = (memblock_t*)malloc(sizeof(memblock_t) + size);
        }
    }

    if(!newblock) {
        I_Error("failed on\nallocation of %u bytes\n (%s:%d)", size, file, line);
    }

    // Hook into the linked list for this tag type

    newblock->tag = tag;
    newblock->id = ZONEID;
	newblock->lockframe = NextFrameIdx;
    newblock->user = (void**) user;
    newblock->size = size;

    Z_InsertBlock(newblock);

    data = (unsigned char*)newblock;
    result = data + sizeof(memblock_t);

    if(user != NULL) {
        *newblock->user = result;
    }

    return result;
}


//
// Z_FreeTags
//

void __Z_FreeTags(int tag, const char *file, int line) {
    int i;

	int tag_list[5] = {PU_STATIC, PU_LEVEL, PU_LEVSPEC, PU_PURGELEVEL, PU_CACHE};

    for(i = 0; i < 5; i++) {
		if (!(tag_list[i] & tag)) continue;

        memblock_t *block;
        memblock_t *next;

        // Free all in this chain

        for(block = allocated_blocks[tag_list[i]]; block != NULL;) {
            next = block->next;

            if(block->id != ZONEID) {
                I_Error("Changed a tag\n without ZONEID\n (%s:%d)\n%08x", file, line, block->id);
            }

            // Free this block

            if(block->user != NULL) {
                *block->user = NULL;
            }

            free(block);

            // Jump to the next in the chain

            block = next;
        }

        // This chain is empty now
        allocated_blocks[tag_list[i]] = NULL;
    }
}


//
// Z_Touch
//

void __Z_Touch(void *ptr, const char *file, int line) {
    memblock_t *block;

    block = (memblock_t*)((byte*)ptr - sizeof(memblock_t));

    if(block->id != ZONEID) {
        I_Error("touched a\npointer without ZONEID\n (%s:%d)\n%08x", file, line, block->id);
    }
}

//
// Z_CheckHeap
//

void __Z_CheckHeap(const char *file, int line) {
    memblock_t *block;
    memblock_t *prev;
    int i;

    //
    // Check all chains
    //
    for(i = 0; i < 5; ++i) {
		int tag_list[5] = {PU_STATIC, PU_LEVEL, PU_LEVSPEC, PU_PURGELEVEL, PU_CACHE};		
        prev = NULL;

        for(block = allocated_blocks[tag_list[i]]; block != NULL; block = block->next) {
            if(block->id != ZONEID) {
                I_Error("Block without\n a ZONEID!\n (%s:%d)\n%08x", file, line, block->id);
            }

            if(block->prev != prev) {
                I_Error("Doubly-linked list corrupted!\n(%s:%d)", file, line);
            }

            prev = block;
        }
    }
}

//
// Z_CheckTag
//

int __Z_CheckTag(void *ptr, const char *file, int line) {
    memblock_t*    block;

    block = (memblock_t*)((byte *)ptr - sizeof(memblock_t));

    __Z_CheckHeap(file, line);

    if(block->id != ZONEID) {
        I_Error("block doesn't have\n ZONEID (%s:%d)\n%08x", file, line, block->id);
    }

    return block->tag;
}

//
// Z_ChangeTag
//

void __Z_ChangeTag(void *ptr, int tag, const char *file, int line) {
    memblock_t*    block;

    block = (memblock_t*)((byte *)ptr - sizeof(memblock_t));

    if(block->id != ZONEID)
        I_Error("block without\n a ZONEID!\n (%s:%d)\n%08x",
                file, line, block->id);

    if(tag >= PU_PURGELEVEL && block->user == NULL) {
        I_Error("an owner is required for purgable blocks (%s:%d)", file, line);
    }

    //
    // Remove the block from its current list, and rehook it into
    // its new list.
    //
    Z_RemoveBlock(block);
    block->tag = tag;
    Z_InsertBlock(block);
}

//
// Z_TagUsage
//

int Z_TagUsage(int tag) {
    int bytes = 0;
    memblock_t *block;

    if(tag < 0 || tag > PU_MAX) {
        I_Error("tag out of range: %i", tag);
    }

    for(block = allocated_blocks[tag]; block != NULL; block = block->next) {
        bytes += block->size;
    }

    return bytes;
}

//
// Z_FreeMemory
//

int __Z_FreeMemory(void) {
    int bytes = 0;
    int i;
    memblock_t *block;

    for(i = 0; i < 5; i++) {
		int tag_list[5] = {PU_STATIC, PU_LEVEL, PU_LEVSPEC, PU_PURGELEVEL, PU_CACHE};		

        for(block = allocated_blocks[tag_list[i]]; block != NULL; block = block->next) {
            bytes += block->size;
        }
    }

    return bytes;
}






#else
/* Z_zone.c */

/*
==============================================================================

						ZONE MEMORY ALLOCATION

There is never any space between memblocks, and there will never be two
contiguous free memblocks.

The rover can be left pointing at a non-empty block

It is of no value to free a cachable block, because it will get overwritten
automatically if needed

==============================================================================
*/

#define DEBUG_ 0

extern u32 NextFrameIdx;

memzone_t *mainzone;

#define MEM_HEAP_SIZE (0x4C0000)

/*
========================
=
= Z_Init
=
========================
*/

void Z_Init(void)
{
	byte *mem = (byte *)memalign(32,MEM_HEAP_SIZE);

	if (!mem)
		I_Error("failed to allocate %08x zone heap", MEM_HEAP_SIZE);

	/* mars doesn't have a refzone */
	mainzone = Z_InitZone(mem, MEM_HEAP_SIZE);
}

/*
========================
=
= Z_InitZone
=
========================
*/

memzone_t *Z_InitZone(byte *base, int size)
{
	memzone_t *zone;

	zone = (memzone_t *)base;
	zone->size = size;
	zone->rover = &zone->blocklist;
	zone->rover2 = &zone->blocklist;
	zone->rover3 = &zone->blocklist;
	zone->blocklist.size =
		size - (int)((byte *)&zone->blocklist - (byte *)zone);
	zone->blocklist.user = NULL;
	zone->blocklist.tag = 0;
	zone->blocklist.id = ZONEID;
	zone->blocklist.next = NULL;
	zone->blocklist.prev = NULL;
//	zone->blocklist.lockframe = -1;

	return zone;
}
/*
========================
=
= Z_SetAllocBase
= Exclusive Doom64
=
========================
*/
void Z_SetAllocBase(memzone_t *mainzone)
{
	mainzone->rover2 = mainzone->rover;
}

/*
========================
=
= Z_Malloc2
=
= You can pass a NULL user if the tag is < PU_PURGELEVEL
========================
*/

#define MINFRAGMENT 64

void *__Z_Malloc2(memzone_t *mainzone, int size, int tag, void *user, uintptr_t retaddr, const char *file, int line)
{
	int extra;
	memblock_t *start, *rover, *newblock, *base;

	if(DEBUG_)
		dbgio_printf("%s %s:%d\n", __func__, file, line);

#if DEBUG_
//	Z_CheckZone(mainzone); /* DEBUG */
#endif

	if (backres[10] != 0xc3) {
		I_Error("failed allocation on %i",
							size);
	}

	/* */
	/* scan through the block list looking for the first free block */
	/* of sufficient size, throwing out any purgable blocks along the way */
	/* */

	size += sizeof(memblock_t); /* account for size of block header */
	size = (size + 15) & ~15; /* phrase align everything */

	start = base = mainzone->rover;

	while (base->user || base->size < size) {
		if (base->user)
			rover = base;
		else
			rover = base->next;

		if (!rover)
			goto backtostart;

		if (rover->user) {
			if (!(rover->tag & PU_PURGELEVEL)) {
				if (!(rover->tag & PU_CACHE) ||
				    (u32)rover->lockframe >= (NextFrameIdx - 1)) {
					/* hit an in use block, so move base past it */
					base = rover->next;
					if (!base) {
backtostart:
						base = mainzone->rover2;
					}

					if (base ==
					    start) /* scaned all the way around the list */
					{
						Z_DumpHeap(mainzone);
						I_Error("failed allocation on %i",
							size);
					}
					continue;
				}
			}

			/* */
			/* free the rover block (adding the size to base) */
			/* */
			Z_Free((byte *)rover +
			       sizeof(memblock_t)); /* mark as free */
		}

		if (base != rover) { /* merge with base */
			base->size += rover->size;
			base->next = rover->next;
			if (rover->next)
				rover->next->prev = base;
			else
				mainzone->rover3 = base;
		}
	}

	/* */
	/* found a block big enough */
	/* */
	extra = base->size - size;
	if (extra >
	    MINFRAGMENT) { /* there will be a free fragment after the allocated block */
		newblock = (memblock_t *)((byte *)base + size);
		newblock->prev = base;
		newblock->next = base->next;
		if (newblock->next)
			newblock->next->prev = newblock;
		else
			mainzone->rover3 = newblock;

		base->next = newblock;
		base->size = size;

		newblock->size = extra;
		newblock->user = NULL; /* free block */
		newblock->tag = 0;
		newblock->id = ZONEID;
	}

	if (user) {
		base->user = user; /* mark as an in use block */
		*(void **)user = (void *)((byte *)base + sizeof(memblock_t));
	} else {
		if (tag >= PU_PURGELEVEL)
			I_Error("an owner is required for purgable blocks");
		base->user = (void *)1; /* mark as in use, but unowned	 */
	}

	base->tag = tag;
	base->id = ZONEID;
	base->lockframe = NextFrameIdx;
	base->gfxcache = (void*)retaddr;

	mainzone->rover =
		base->next; /* next allocation will start looking here */
	if (!mainzone->rover) {
		mainzone->rover3 = base;
		mainzone->rover =
			mainzone->rover2; //mainzone->rover = &mainzone->blocklist;
	}

#if DEBUG_
//	Z_CheckZone(mainzone); /* DEBUG */
#endif

	return (void *)((byte *)base + sizeof(memblock_t));
}

/*
========================
=
= Z_Alloc2
=
= You can pass a NULL user if the tag is < PU_PURGELEVEL
= Exclusive Psx Doom
========================
*/

void *__Z_Alloc2(memzone_t *mainzone, int size, int tag, void *user, uintptr_t retaddr, const char *file, int line)
{
	int extra;
	memblock_t *rover, *base, *block, *newblock;
	if(DEBUG_)
		dbgio_printf("%s %s:%d\n", __func__, file, line);

#if DEBUG_
//	Z_CheckZone(mainzone); /* DEBUG */
#endif

	/* */
	/* scan through the block list looking for the first free block */
	/* of sufficient size, throwing out any purgable blocks along the way */
	/* */

	size += sizeof(memblock_t); /* account for size of block header */
	size = (size + 15) & ~15; /* phrase align everything */

	base = mainzone->rover3;

	if (!base)
		I_Error("NULL mainzone->rover3");

	while (base->user || base->size < size) {
		if (base->user)
			rover = base;
		else {
			/* hit an in use block, so move base past it */
			rover = base->prev;
			if (!rover) {
				Z_DumpHeap(mainzone);
				I_Error("failed allocation on %i",
					size);
			}
		}

		if (rover->user) {
			if (!(rover->tag & PU_PURGELEVEL)) {
				if (!(rover->tag & PU_CACHE) ||
				    (u32)rover->lockframe >= (NextFrameIdx - 1)) {
					/* hit an in use block, so move base past it */
					base = rover->prev;
					if (!base) {
						Z_DumpHeap(mainzone);
						I_Error("failed allocation on %i",
							size);
					}
					continue;
				}
			}

			/* */
			/* free the rover block (adding the size to base) */
			/* */
			Z_Free((byte *)rover +
			       sizeof(memblock_t)); /* mark as free */
		}

		if (base != rover) {
			/* merge with base */
			rover->size += base->size;
			rover->next = base->next;

			if (base->next)
				base->next->prev = rover;
			else
				mainzone->rover3 = rover;

			base = rover;
		}
	}

	/* */
	/* found a block big enough */
	/* */
	extra = (base->size - size);

	newblock = base;
	block = base;

	if (extra > MINFRAGMENT) {
		block = (memblock_t *)((byte *)base + extra);
		block->prev = newblock;
		block->next = (void *)newblock->next;

		if (newblock->next)
			newblock->next->prev = block;

		newblock->next = block;
		block->size = size;
		newblock->size = extra;
		newblock->user = 0;
		newblock->tag = 0;
		newblock->id = ZONEID;
	}

	if (block->next == 0)
		mainzone->rover3 = block;

	if (user) {
		block->user = user; /* mark as an in use block */
		*(void **)user = (void *)((byte *)block + sizeof(memblock_t));
	} else {
		if (tag >= PU_PURGELEVEL)
			I_Error("an owner is required for purgable blocks");
		block->user = (void *)1; /* mark as in use, but unowned	 */
	}

	block->id = ZONEID;
	block->tag = tag;
	block->lockframe = NextFrameIdx;
	base->gfxcache = (void*)retaddr;
#if DEBUG_
//	Z_CheckZone(mainzone); /* DEBUG */
#endif

	return (void *)((byte *)block + sizeof(memblock_t));
}

/*
========================
=
= Z_Free2
=
========================
*/

void __Z_Free2(memzone_t *mainzone, void *ptr, const char *file, int line)
{
	(void)mainzone;
	memblock_t *block;
	if(DEBUG_)
		dbgio_printf("%s %s:%d\n", __func__, file, line);

	block = (memblock_t *)((byte *)ptr - sizeof(memblock_t));
	if (block->id != ZONEID)
		I_Error("freed a pointer without ZONEID");

	if (block->user > (void **)0x100) /* smaller values are not pointers */
		*block->user = 0; /* clear the user's mark */
	block->user = NULL; /* mark as free */
	block->tag = 0;
}

/*
========================
=
= Z_FreeTags
=
========================
*/

void __Z_FreeTags(memzone_t *mainzone, int tag, const char *file, int line)
{
	memblock_t *block, *next;
	if(DEBUG_)
		dbgio_printf("%s %s:%d\n", __func__, file, line);

	for (block = &mainzone->blocklist; block; block = next) {
		/* get link before freeing */
		next = block->next;

		/* free block */
		if (block->user) {
			if (block->tag & tag) {
				Z_Free((byte *)block + sizeof(memblock_t));
			}
		}
	}

	for (block = &mainzone->blocklist; block; block = next) {
		/* get link before freeing */
		next = block->next;

		if (!block->user && next && !next->user) {
			block->size += next->size;
			block->next = next->next;
			if (next->next)
				next->next->prev = block;
			next = block;
		}
	}

	mainzone->rover = &mainzone->blocklist;
	mainzone->rover2 = &mainzone->blocklist;
	mainzone->rover3 = &mainzone->blocklist;

	block = mainzone->blocklist.next;
	while (block) {
		mainzone->rover3 = block;
		block = block->next;
	}
}

/*
========================
=
= Z_Touch
= Exclusive Doom64
=
========================
*/
extern int last_touched;
void __Z_Touch(void *ptr, const char *file, int line)
{
	memblock_t *block;
	if(0)//DEBUG_)
		dbgio_printf("%s %s:%d\n", __func__, file, line);

	block = (memblock_t *)((byte *)ptr - sizeof(memblock_t));
	if (block->id != ZONEID) {
		uint32_t *block32 = (uint32_t *)block;
		dbgio_printf("INVALID ZONE ON TOUCH %d\n", last_touched);
		for (int i = 0; i < sizeof(memblock_t)/4; i++) {
			dbgio_printf("%08lx ", block32[i]);
		}
		dbgio_printf("\n");

		I_Error("touched a pointer to %d without ZONEID", last_touched);
	}

	block->lockframe = NextFrameIdx;
}

/*
========================
=
= Z_CheckZone
=
========================
*/

void __Z_CheckZone(memzone_t *mainzone, const char *file, int line)
{
	memblock_t *checkblock;
	int size;
	if(DEBUG_)
		dbgio_printf("%s %s:%d\n", __func__, file, line);

	for (checkblock = &mainzone->blocklist; checkblock;
	     checkblock = checkblock->next) {

		if (checkblock->id != ZONEID) {
			Z_DumpHeap(mainzone);
			dbgio_printf("checkblock->id %08x\n", checkblock->id);
			I_Error("block missing ZONEID");
		}
		if (!checkblock->next) {
			size = (byte *)checkblock + checkblock->size -
			       (byte *)mainzone;
			if (size != mainzone->size) {
				Z_DumpHeap(mainzone);
				I_Error("zone size changed from %d to %d\n",
					mainzone->size, size);
			}
			break;
		}

		if ((byte *)checkblock + checkblock->size !=
		    (byte *)checkblock->next) {
			Z_DumpHeap(mainzone);
			I_Error("block size does not touch the next block\n");
		}
		if (checkblock->next->prev != checkblock) {
			Z_DumpHeap(mainzone);
			I_Error("next block doesn't have proper back link\n");
		}
	}


#if DEBUG_
//	Z_DumpHeap(mainzone);
#endif
}

/*
========================
=
= Z_ChangeTag
=
========================
*/

void __Z_ChangeTag(void *ptr, int tag, const char *file, int line)
{
	memblock_t *block;
	if(DEBUG_)
		dbgio_printf("%s %s:%d\n", __func__, file, line);

	block = (memblock_t *)((byte *)ptr - sizeof(memblock_t));
	if (block->id != ZONEID)
		I_Error("freed a pointer without ZONEID");
	if (tag >= PU_PURGELEVEL && (int)block->user < 0x100)
		I_Error("an owner is required for purgable blocks");
	block->tag = tag;
	block->lockframe = NextFrameIdx;
}

/*
========================
=
= Z_FreeMemory
=
========================
*/

int Z_FreeMemory(memzone_t *mainzone)
{
	memblock_t *block;
	int free;

	free = 0;
	for (block = &mainzone->blocklist; block; block = block->next) {
		if (!block->user)
			free += block->size;
	}

	return free;
}

/*
========================
=
= Z_DumpHeap
=
========================
*/

void Z_DumpHeap(memzone_t *mainzone) // 8002D1C8
{
#if !DEBUG_
	(void)mainzone;
#elif DEBUG_
	memblock_t *block;

	printf("zone size: %i  location: %p\n", mainzone->size, mainzone);

	for (block = &mainzone->blocklist; block; block = block->next) {
		printf("block:%p    id:%8i    size:%7i    user:%p    tag:%3i    frame:%i    retaddr:%08x\n",
		       block, block->id, block->size, block->user, block->tag,
		       block->lockframe, (uintptr_t)block->gfxcache);

		if (!block->next)
			continue;

		if ((byte *)block + block->size != (byte *)block->next)
			printf("ERROR: block size does not touch the next block\n");
		if (block->next->prev != block)
			printf("ERROR: next block doesn't have proper back link\n");
	}
#endif
}

#endif
#endif