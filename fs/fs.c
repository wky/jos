#include <inc/string.h>

#include "fs.h"

// --------------------------------------------------------------
// Super block
// --------------------------------------------------------------

// Validate the file system super-block.
void
check_super(void)
{
	if (super->s_magic != FS_MAGIC)
		panic("bad file system magic number");

	if (super->s_nblocks > DISKSIZE/BLKSIZE)
		panic("file system is too large");

	cprintf("superblock is good\n");
}


// --------------------------------------------------------------
// File system structures
// --------------------------------------------------------------

// Initialize the file system
void
fs_init(void)
{
	static_assert(sizeof(struct File) == 256);

	// Find a JOS disk.  Use the second IDE disk (number 1) if available.
	if (ide_probe_disk1())
		ide_set_disk(1);
	else
		ide_set_disk(0);

	bc_init();

	// Set "super" to point to the super block.
	super = diskaddr(1);
	check_super();
}

int 
alloc_blk()
{
	uint32_t *bitmap = diskaddr(2);
	int i, j, first = 0;
	for (i = 0; i < super->s_nblocks / 32; i++)
		for (j = 0; j < 32 && j + i*32 < super->s_nblocks; j++)
			if ((bitmap[i] & (1<<j)) != 0){
				bitmap[i] &= ~(1<<j);
				first = i *32 + j;
				break;
			}
	if (first == 0) return -E_NO_DISK;
	block_write_back((i * 32 + j)/8/BLKSIZE + 2);
	return first;
}

void
free_blk(uint32_t blk)
{
	if (blk > super->s_nblocks || blk < (super->s_nblocks / 8 + BLKSIZE - 1)/BLKSIZE + 2)
		return;
	((uint32_t *)diskaddr(2)) [blk/32] |= 1<<(blk%32);
	block_write_back(blk/8/BLKSIZE + 2);
}

void
block_write_back(uint32_t blk)
{
	char *addr = diskaddr(blk);
	int t;
	if ((uvpd[PDX(addr)] & PTE_P) && (uvpt[PGNUM(addr)] & PTE_P)){
		if ((uvpt[PGNUM(addr)] & PTE_D)) ide_write(blk * BLKSECTS, addr, BLKSECTS);
		if (blk != 1) sys_page_unmap(0, addr); // never unmap superblock
	}
}
// Find the disk block number slot for the 'filebno'th block in file 'f'.
// Set '*ppdiskbno' to point to that slot.
// The slot will be one of the f->f_direct[] entries,
// or an entry in the indirect block.
// When 'alloc' is set, this function will allocate an indirect block
// if necessary.
//
// Returns:
//	0 on success (but note that *ppdiskbno might equal 0).
//	-E_NOT_FOUND if the function needed to allocate an indirect block, but
//		alloc was 0.
//	-E_NO_DISK if there's no space on the disk for an indirect block.
//	-E_INVAL if filebno is out of range (it's >= NDIRECT + NINDIRECT).
//
// Analogy: This is like pgdir_walk for files.
// Hint: Don't forget to clear any block you allocate.
static int
file_block_walk(struct File *f, uint32_t filebno, uint32_t **ppdiskbno, bool alloc)
{
	int r;
	uint32_t *ptr;
	char *blk;

	if (filebno < NDIRECT)
		ptr = &f->f_direct[filebno];
	else if (filebno < NDIRECT + NINDIRECT) {
		if (f->f_indirect == 0) {
			if (alloc){
				if ((r = alloc_blk()) < 0) return r;
				f->f_indirect = r;
			} else return -E_NOT_FOUND;
		}
		ptr = (uint32_t*)diskaddr(f->f_indirect) + filebno - NDIRECT;
	} else return -E_INVAL;
	*ppdiskbno = ptr;
	return 0;
}

// Set *blk to the address in memory where the filebno'th
// block of file 'f' would be mapped.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_NO_DISK if a block needed to be allocated but the disk is full.
//	-E_INVAL if filebno is out of range.
//
int
file_get_block(struct File *f, uint32_t filebno, char **blk)
{
	int r;
	uint32_t *ptr;

	if ((r = file_block_walk(f, filebno, &ptr, 1)) < 0)
		return r;
	if (*ptr == 0) {
		if ((r = alloc_blk()) < 0) return r;
		*ptr = r;
	}
	*blk = diskaddr(*ptr);
	return 0;
}

// Try to find a file named "name" in dir.  If so, set *file to it.
//
// Returns 0 and sets *file on success, < 0 on error.  Errors are:
//	-E_NOT_FOUND if the file is not found
static int
dir_lookup(struct File *dir, const char *name, struct File **file)
{
	int r;
	uint32_t i, j, nblock;
	char *blk;
	struct File *f;

	// Search dir for name.
	// We maintain the invariant that the size of a directory-file
	// is always a multiple of the file system's block size.
	assert((dir->f_size % BLKSIZE) == 0);
	nblock = dir->f_size / BLKSIZE;
	for (i = 0; i < nblock; i++) {
		if ((r = file_get_block(dir, i, &blk)) < 0)
			return r;
		f = (struct File*) blk;
		for (j = 0; j < BLKFILES; j++)
			if (strcmp(f[j].f_name, name) == 0) {
				*file = &f[j];
				return 0;
			}
	}
	return -E_NOT_FOUND;
}


// Skip over slashes.
static const char*
skip_slash(const char *p)
{
	while (*p == '/')
		p++;
	return p;
}

// Evaluate a path name, starting at the root.
// On success, set *pf to the file we found
// and set *pdir to the directory the file is in.
// If we cannot find the file but find the directory
// it should be in, set *pdir and copy the final path
// element into lastelem.
static int
walk_path(const char *path, struct File **pdir, struct File **pf, char *lastelem)
{
	const char *p;
	char name[MAXNAMELEN];
	struct File *dir, *f;
	int r;

	// if (*path != '/')
	//	return -E_BAD_PATH;
	path = skip_slash(path);
	f = &super->s_root;
	dir = 0;
	name[0] = 0;

	if (pdir)
		*pdir = 0;
	*pf = 0;
	while (*path != '\0') {
		dir = f;
		p = path;
		while (*path != '/' && *path != '\0')
			path++;
		if (path - p >= MAXNAMELEN)
			return -E_BAD_PATH;
		memmove(name, p, path - p);
		name[path - p] = '\0';
		path = skip_slash(path);

		if (dir->f_type != FTYPE_DIR)
			return -E_NOT_FOUND;

		if ((r = dir_lookup(dir, name, &f)) < 0) {
			if (r == -E_NOT_FOUND && *path == '\0') {
				if (pdir)
					*pdir = dir;
				if (lastelem)
					strcpy(lastelem, name);
				*pf = 0;
			}
			return r;
		}
	}

	if (pdir)
		*pdir = dir;
	*pf = f;
	return 0;
}

// --------------------------------------------------------------
// File operations
// --------------------------------------------------------------


int
file_create(const char *path, struct File **f)
{
	struct File *dir;
	char name[MAXNAMELEN];
	int r = walk_path(path, &dir, f, name);
	if (r != -E_NOT_FOUND){
		return -E_FILE_EXISTS;
	}
	if (name[0] == '/') return -E_NOT_FOUND;
	
	struct File *entries;
	assert(dir->f_size && (dir->f_size % BLKSIZE) == 0);
	uint32_t nblock = (dir->f_size / BLKSIZE);
	if ((r = file_get_block(dir, nblock-1, (char **)(&entries))) < 0) return r;
	int i = 0;
	while (i < BLKFILES && entries[i].f_size > 0) i++; // find an empty entry in this directory
	if (i == BLKFILES){ // alloc a new block if can't find any
		i = 0;
		dir->f_size += BLKSIZE;
		if ((r = file_get_block(dir, nblock, (char **)f)) < 0) return r;
	}else *f = entries+i;

	i = 0;
	while (i < MAXNAMELEN && name[i]) i++;

	if (name[i-1] == '/'){ // create dir
		cprintf("create dir:%s\n", name);
		name[i-1] = '\0';
		strcpy((*f)->f_name, name);
		(*f)->f_size = BLKSIZE;
		char *__x;
		if ((r = file_get_block(*f, 0, &__x)) < 0) return r;
	}else{// create file
		cprintf("create file:%s\n", name);
		strcpy((*f)->f_name, name);
		(*f)->f_size = 0;
	}
	return 0;
}

// Open "path".  On success set *pf to point at the file and return 0.
// On error return < 0.
int
file_open(const char *path, struct File **pf)
{
	return walk_path(path, 0, pf, 0);
}

// Read count bytes from f into buf, starting from seek position
// offset.  This meant to mimic the standard pread function.
// Returns the number of bytes read, < 0 on error.
ssize_t
file_read(struct File *f, void *buf, size_t count, off_t offset)
{
	int r, bn;
	off_t pos;
	char *blk;

	if (offset >= f->f_size)
		return 0;

	count = MIN(count, f->f_size - offset);

	for (pos = offset; pos < offset + count; ) {
		if ((r = file_get_block(f, pos / BLKSIZE, &blk)) < 0)
			return r;
		bn = MIN(BLKSIZE - pos % BLKSIZE, offset + count - pos);
		memmove(buf, blk + pos % BLKSIZE, bn);
		pos += bn;
		buf += bn;
	}
	return count;
}

int
file_write(struct File *f, const void *buf, size_t count, off_t offset)
{
	int r, bn;
	off_t pos;
	char *blk, *ptr = (char *)buf;
	if (offset > f->f_size) return -E_NOT_SUPP; // writing past eof
	for (pos = offset; pos < offset + count; ) {
		if ((r = file_get_block(f, pos / BLKSIZE, &blk)) < 0) return r;
		bn = MIN(BLKSIZE - pos % BLKSIZE, offset + count - pos);
		memmove(blk + pos % BLKSIZE, ptr, bn);
		pos += bn;
		ptr += bn;
	}
	if (offset + count > f->f_size)
		f->f_size = offset + count;
	return count;
}

int
file_set_size(struct File *f, off_t newsize)
{
	int blk, r;
	uint32_t *blkptr;
	for (blk = (newsize + BLKSIZE - 1)/BLKSIZE; blk < (f->f_size + BLKSIZE -1)/BLKSIZE; blk++){
		if ((r = file_block_walk(f, blk, &blkptr, 0)) < 0) return r;
		// delete this blk
		if (*blkptr != 0) free_blk(*blkptr);
	}
	for (blk = (f->f_size + BLKSIZE -1)/BLKSIZE; blk < (newsize + BLKSIZE - 1)/BLKSIZE; blk++){
		if ((r = file_block_walk(f, blk, &blkptr, 1)) < 0
			|| (r = alloc_blk()) < 0)
			return r;
		*blkptr = r;
	}
	f->f_size = newsize;
	return newsize;
}

void
file_flush(struct File *f)
{
	block_write_back(((uint32_t)f - DISKMAP)/BLKSIZE);
	int r, blk;
	uint32_t *ptr;
	for (blk = 0; blk < (f->f_size + BLKSIZE - 1)/BLKSIZE; blk ++){
		file_block_walk(f, blk, &ptr, 0);
		block_write_back(*ptr);
	}
}

int
file_remove(const char *path)
{
	// TODO remove
	return -E_NOT_SUPP;
}

void
fs_sync(void)
{
	int blk;
	for (blk = 0; blk < super->s_nblocks; blk ++)
		block_write_back(blk);
}
