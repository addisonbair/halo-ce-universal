/*
CACHE.C

The map file StageKit writes: a Halo Custom Edition cache (version 609, which
the port converts itself: port/linux/game/cache_file_formats.c names each
offset used here), or for the shell's maps (maps\\ui.map, maps\\carousel.map,
which the game loads as its own: shell.c) an Xbox cache (version 5,
cache_files.c), its body a zlib stream as the game inflates it. Its tags are
built where the game reads that cache's tags, so the game's structures'
pointers are written as they are; the structure BSP goes at the top of that
room, as every map puts it.
*/

/* ---------- headers */

#include "stagekit.h"
#include <sys/mman.h>
#include "cache/physical_memory_map.h"
#include "tag_schema.h" /* port/linux/game/tag_schema.h */
#include "cache_file_formats.h" /* port/linux/game/cache_file_formats.h */

/* (the game's flags ask for strict ANSI, which hides these: Linux's values) */
#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS 0x20
#endif
#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

/* ---------- constants */

enum
{
	/* (an Xbox cache's, which physical_memory_map.c keeps to itself) */
	XBOX_TAG_CACHE_ADDRESS = 0x803A6000,
	XBOX_TAG_INDEX_BYTES = 0x24,
	/* (zlib's stored blocks: at most this many bytes each) */
	STORED_BLOCK_BYTES = 0xFFFF,
	HEADER_BYTES = 0x800,
	TAG_INDEX_BYTES = 0x28,
	MAXIMUM_TAGS = 128,
	/* a tag handle's salt: any, the same in every reference */
	HANDLE_SALT = 0x0E00,
	/* the room kept at the top of the tag cache for the structure BSP */
	BSP_BYTES = 0x10000,
	/* the room for bitmaps' pixels, in the file between the BSP and the tags */
	PIXEL_BYTES = 0x4000,
};

/* ---------- structures */

/* a cache's header (cache_files.c keeps it to itself) */
struct cache_header
{
	unsigned long signature;
	long version;
	unsigned long file_length;
	unsigned long unused0;
	unsigned long tag_data_offset;
	unsigned long tag_data_size;
	unsigned long unused1[2];
	char name[0x20];
	char build[0x20];
	short scenario_type;
	short pad;
	unsigned long checksum;
	unsigned char unused2[0x794];
	unsigned long footer_signature;
};
typedef char cache_header_size_check[sizeof(struct cache_header) == HEADER_BYTES ? 1 : -1];

struct tag_index
{
	void *instances;
	long scenario_handle;
	unsigned long checksum;
	long tag_count;
	long vertex_count;
	unsigned long vertex_data_offset;
	long index_count;
	unsigned long index_data_offset;
	unsigned long model_data_size;
	unsigned long signature;
};

/* (an Xbox cache's: its buffers, none here, then the signature) */
struct xbox_tag_index
{
	void *instances;
	long scenario_handle;
	unsigned long checksum;
	long tag_count;
	unsigned long buffers[4];
	unsigned long signature;
};
typedef char xbox_tag_index_size_check[sizeof(struct xbox_tag_index) == XBOX_TAG_INDEX_BYTES ? 1 : -1];

struct tag_instance
{
	unsigned long group;
	unsigned long parent_groups[2];
	long handle;
	char *name;
	void *address;
	long in_resource_map;
	long pad;
};

/* ---------- globals */

static boolean xbox_format;
static unsigned char *tag_cache;
static unsigned long tag_cache_bytes;
static unsigned long tags_used;
static unsigned long bsp_used;
static unsigned char pixels[PIXEL_BYTES];
static unsigned long pixels_used;
static struct tag_index *tag_index;
static struct tag_instance *instances;

/* ---------- private code */

/* every reference and tag index the generator left as it was made (zero, which
names the first tag) set to none, by the groups' own schemas (tag_schema_*.c),
so the map needs no correction (tools/map_validate.c) */
static void unset_references_to_none(
	struct tag_schema_definition const *definition,
	unsigned char *base,
	char *empty_name)
{
	struct tag_schema_field const *field;

	for (field = definition->fields; field->type != _tag_schema_terminator; field++)
	{
		short index;

		for (index = 0; index < field->count; index++)
		{
			unsigned char *at = base + field->offset + index * field->size;

			if (field->type == _tag_schema_reference)
			{
				struct tag_reference *reference = (struct tag_reference *)at;
				unsigned long const *groups = field->definition;

				if (!reference->group_tag && !reference->name && !reference->index)
				{
					reference->group_tag = groups ? groups[0] : NONE;
					reference->name = empty_name;
					reference->index = NONE;
				}
			}
			else if (field->type == _tag_schema_tag_index && TEST_FLAG(field->flags, _tag_schema_none_bit))
			{
				if (!*(long *)at)
					*(long *)at = NONE;
			}
			else if (field->type == _tag_schema_block)
			{
				struct tag_block *block = (struct tag_block *)at;
				struct tag_schema_definition const *element = field->definition;
				long element_index;

				for (element_index = 0; element_index < block->count; element_index++)
					unset_references_to_none(element, (unsigned char *)block->address + element_index * element->size,
						empty_name);
			}
			else if (field->type == _tag_schema_struct)
			{
				unset_references_to_none(field->definition, at, empty_name);
			}
		}
	}
}

/* each tag's references made none, where the schemas describe its group (the
structure BSP, written apart, is left as the generator made it) */
static void tags_finish(
	void)
{
	char *empty_name = cache_allocate(1);
	long index;

	for (index = 0; index < tag_index->tag_count; index++)
	{
		struct tag_instance const *instance = &instances[index];
		struct tag_schema_group const *const *list;

		for (list = tag_schema_group_lists; *list; list++)
		{
			struct tag_schema_group const *group;

			for (group = *list; group->group_tag; group++)
			{
				if (group->group_tag == instance->group && instance->address)
					unset_references_to_none(group->definition, instance->address, empty_name);
			}
		}
	}
}

/* writes part of the body after the header: as it is, or for an Xbox cache
as zlib's stored blocks (a stream the game's inflate reads, uncompressed),
the last part ending the stream with its checksum */
static boolean body_write(
	FILE *file,
	unsigned char const *data,
	unsigned long size,
	boolean last)
{
	static unsigned long adler_a = 1, adler_b = 0;
	static boolean started = FALSE;
	unsigned long at = 0;

	if (!xbox_format)
		return !size || fwrite(data, size, 1, file) == 1;
	if (!started)
	{
		static unsigned char const stream_header[2] = { 0x78, 0x01 };

		started = TRUE;
		if (fwrite(stream_header, 2, 1, file) != 1)
			return FALSE;
	}
	do
	{
		unsigned long length = size - at < STORED_BLOCK_BYTES ? size - at : STORED_BLOCK_BYTES;
		boolean final = last && at + length == size;
		unsigned char block_header[5];
		unsigned long index;

		block_header[0] = final ? 1 : 0;
		block_header[1] = (unsigned char)length;
		block_header[2] = (unsigned char)(length >> 8);
		block_header[3] = (unsigned char)~length;
		block_header[4] = (unsigned char)(~length >> 8);
		if (fwrite(block_header, 5, 1, file) != 1 || (length && fwrite(data + at, length, 1, file) != 1))
			return FALSE;
		for (index = 0; index < length; index++)
		{
			adler_a = (adler_a + data[at + index]) % 65521;
			adler_b = (adler_b + adler_a) % 65521;
		}
		at += length;
	}
	while (at < size);
	if (last)
	{
		unsigned long adler = adler_b << 16 | adler_a;
		unsigned char trailer[4] = { (unsigned char)(adler >> 24), (unsigned char)(adler >> 16),
			(unsigned char)(adler >> 8), (unsigned char)adler };

		return fwrite(trailer, 4, 1, file) == 1;
	}
	return TRUE;
}

/* ---------- public code */

void cache_open(
	boolean xbox_cache)
{
	unsigned long address = xbox_cache ? XBOX_TAG_CACHE_ADDRESS : CUSTOM_EDITION_TAG_CACHE_ADDRESS;

	xbox_format = xbox_cache;
	tag_cache_bytes = xbox_format ? TAG_CACHE_SIZE : CUSTOM_EDITION_TAG_CACHE_BYTES;
	tag_cache = mmap((void *)address, tag_cache_bytes, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
	if (tag_cache != (unsigned char *)address)
	{
		fprintf(stderr, "stagekit: the tag cache's address is taken\n");
		exit(1);
	}
	tag_index = cache_allocate(xbox_format ? XBOX_TAG_INDEX_BYTES : TAG_INDEX_BYTES);
	instances = cache_allocate(MAXIMUM_TAGS * sizeof(struct tag_instance));
	tag_index->instances = instances;
	if (xbox_format)
		((struct xbox_tag_index *)tag_index)->signature = FOURCC('t', 'a', 'g', 's');
	else
		tag_index->signature = FOURCC('t', 'a', 'g', 's');
}

boolean cache_xbox(
	void)
{
	return xbox_format;
}

void *cache_allocate(
	unsigned long size)
{
	void *result = tag_cache + tags_used;

	tags_used = (tags_used + size + 3) & ~3UL;
	if (tags_used > tag_cache_bytes - BSP_BYTES)
	{
		fprintf(stderr, "stagekit: the tags do not fit\n");
		exit(1);
	}
	return result;
}

void *cache_block(
	struct tag_block *block,
	long count,
	unsigned long element_size)
{
	block->count = count;
	block->address = count ? cache_allocate(count * element_size) : NULL;
	return block->address;
}

long cache_pixels(
	void const *data,
	unsigned long size)
{
	long result = HEADER_BYTES + BSP_BYTES + pixels_used;

	if (pixels_used + size > PIXEL_BYTES)
	{
		fprintf(stderr, "stagekit: the pixels do not fit\n");
		exit(1);
	}
	memcpy(pixels + pixels_used, data, size);
	pixels_used = (pixels_used + size + 3) & ~3UL;
	return result;
}

void *cache_block_add(
	struct tag_block *block,
	unsigned long element_size)
{
	unsigned char const *old = block->address;
	unsigned char *grown = cache_allocate((block->count + 1) * element_size);

	/* (the tags are allocated upwards: a copy, its old elements first) */
	memcpy(grown, old, block->count * element_size);
	block->address = grown;
	block->count++;
	return grown + (block->count - 1) * element_size;
}

void *cache_bsp_allocate(
	unsigned long size)
{
	void *result = tag_cache + tag_cache_bytes - BSP_BYTES + bsp_used;

	bsp_used = (bsp_used + size + 3) & ~3UL;
	if (bsp_used > BSP_BYTES)
	{
		fprintf(stderr, "stagekit: the structure BSP does not fit\n");
		exit(1);
	}
	return result;
}

void *cache_bsp_block(
	struct tag_block *block,
	long count,
	unsigned long element_size)
{
	block->count = count;
	block->address = count ? cache_bsp_allocate(count * element_size) : NULL;
	return block->address;
}

long cache_tag_new(
	unsigned long group,
	unsigned long parent,
	unsigned long grandparent,
	char const *name,
	unsigned long size,
	void **data)
{
	long index = tag_index->tag_count++;
	struct tag_instance *instance = &instances[index];
	char *name_copy = cache_allocate(strlen(name) + 1);

	if (index >= (long)MAXIMUM_TAGS)
	{
		fprintf(stderr, "stagekit: more than %d tags\n", MAXIMUM_TAGS);
		exit(1);
	}
	strcpy(name_copy, name);
	instance->group = group;
	instance->parent_groups[0] = parent;
	instance->parent_groups[1] = grandparent;
	instance->handle = ((HANDLE_SALT + index) << 16) | index;
	instance->name = name_copy;
	instance->address = *data = cache_allocate(size);
	return instance->handle;
}

/* (a structure BSP's instance has no data of its own: the scenario's
reference places it) */
void cache_tag_no_data(
	long handle)
{
	instances[handle & 0xFFFF].address = NULL;
}

void cache_reference(
	struct tag_reference *reference,
	long handle)
{
	struct tag_instance *instance = &instances[handle & 0xFFFF];

	reference->group_tag = instance->group;
	reference->name = instance->name;
	reference->name_length = strlen(instance->name);
	reference->index = handle;
}

void cache_no_reference(
	struct tag_reference *reference,
	unsigned long group)
{
	reference->group_tag = group;
	reference->name = cache_allocate(1);
	reference->name_length = 0;
	reference->index = NONE;
}

void cache_set_scenario(
	long handle)
{
	tag_index->scenario_handle = handle;
}

void cache_write(
	char const *path,
	char const *name,
	short scenario_type,
	long *tag_count)
{
	static char const *const resource_maps[] = { "bitmaps.map", "sounds.map", "loc.map" };
	struct cache_header header = { 0 };
	unsigned long tag_data_offset = HEADER_BYTES + BSP_BYTES + pixels_used;
	char resource_path[1024];
	long folder_length = 0;
	long index;
	FILE *file;

	tags_finish();
	if (!xbox_format)
	{
		/* no models' vertices or indices: none, before the tags */
		tag_index->vertex_data_offset = tag_data_offset;
		tag_index->index_data_offset = 0;
		tag_index->model_data_size = 0;
	}
	header.signature = FOURCC('h', 'e', 'a', 'd');
	header.version = xbox_format ? 5 : 609;
	header.file_length = tag_data_offset + tags_used;
	header.tag_data_offset = tag_data_offset;
	header.tag_data_size = tags_used;
	strcpy(header.name, name);
	/* (an Xbox cache's build is one the game lists, cache_file_builds, else
	it plays no network game: the NTSC release's; a Custom Edition cache's,
	which the port does not check, StageKit's own) */
	strcpy(header.build, xbox_format ? "01.10.12.2276" : "stagekit");
	header.scenario_type = scenario_type;
	/* (one of its contents: a network game's machines compare it, none when 0,
	custom_edition_cache.c; and the game keeps an Xbox cache it has inflated
	and takes it again while it is the same) */
	{
		unsigned long at;

		for (at = 0; at < tags_used; at++)
			header.checksum = header.checksum * 31 + tag_cache[at];
		for (at = 0; at < pixels_used; at++)
			header.checksum = header.checksum * 31 + pixels[at];
		for (at = 0; at < bsp_used; at++)
			header.checksum = header.checksum * 31 + tag_cache[tag_cache_bytes - BSP_BYTES + at];
	}
	header.footer_signature = FOURCC('f', 'o', 'o', 't');
	file = fopen(path, "wb");
	if (!file || fwrite(&header, sizeof(header), 1, file) != 1 ||
		!body_write(file, tag_cache + tag_cache_bytes - BSP_BYTES, BSP_BYTES, FALSE) ||
		!body_write(file, pixels, pixels_used, FALSE) ||
		!body_write(file, tag_cache, tags_used, TRUE) ||
		fclose(file))
	{
		fprintf(stderr, "stagekit: could not write %s\n", path);
		exit(1);
	}
	if (xbox_format)
	{
		*tag_count = tag_index->tag_count;
		return;
	}

	/* Custom Edition's resource maps, which a network game asks for beside a
	map: empty ones (a type, where names and items would be, no items) */
	for (index = 0; path[index]; index++)
	{
		if (path[index] == '/')
			folder_length = index + 1;
	}
	for (index = 0; index < (long)NUMBEROF(resource_maps); index++)
	{
		long resource_header[4] = { index + 1, 16, 16, 0 };
		long at;

		if (folder_length + strlen(resource_maps[index]) + 1 > sizeof(resource_path))
		{
			fprintf(stderr, "stagekit: the folder of %s is too long\n", path);
			exit(1);
		}
		for (at = 0; at < folder_length; at++)
			resource_path[at] = path[at];
		strcpy(resource_path + folder_length, resource_maps[index]);
		/* (one that is there, perhaps a real one, is left as it is) */
		file = fopen(resource_path, "rb");
		if (file)
		{
			fclose(file);
			continue;
		}
		file = fopen(resource_path, "wb");
		if (!file || fwrite(resource_header, sizeof(resource_header), 1, file) != 1 || fclose(file))
		{
			fprintf(stderr, "stagekit: could not write %s\n", resource_path);
			exit(1);
		}
	}
	*tag_count = tag_index->tag_count;
}

/* where the structure BSP is in the file, how big, and where in memory */
void cache_bsp_place(
	struct scenario_structure_bsp_reference *reference)
{
	reference->file_offset = HEADER_BYTES;
	reference->file_size = BSP_BYTES;
	reference->base_address = tag_cache + tag_cache_bytes - BSP_BYTES;
}
