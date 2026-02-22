/*
 * SETUPMOD - Windows 95/98 Setup Modifier for Hercules Display Driver
 *
 * Prepares a Windows 95/98 installation on the hard drive with the Herc9x
 * display driver pre-integrated, so the user can select "Hercules Graphics
 * Card (ISA)" during a Custom setup instead of VGA.
 *
 * Expects to run from a mod pack folder (e.g. C:\W95HERC) containing:
 *   SETUPMOD.EXE, HERCULES.DRV, HERCMINI.DRV, HERCMINI.VXD, HERC9X.INF
 *
 * Includes a minimal CAB creator that builds uncompressed cabinet files,
 * since MAKECAB.EXE is Win32-only and cannot run in real-mode DOS.
 *
 * Build: Open Watcom C - wmake setupmod.exe
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dos.h>
#include <direct.h>
#include <conio.h>
#include <io.h>
#include <sys/stat.h>
#include <process.h>

/* ---------- constants ---------- */

#define MAX_PATH_LEN    128
#define MAX_LINE_LEN    512
#define MAX_INF_LINES   3000    /* msdisp95.inf is ~2016 lines */
#define MAX_CAB_FILES   256     /* max files per cab for our cab creator */
#define CAB_BLOCK_SIZE  8192    /* CFDATA block size for uncompressed cab */
#define MIN_DISK_MB     100     /* minimum usable HDD */
#define LOW_DISK_MB_95  150     /* threshold for minimal Win95 copy */

/* files we must find in our own directory */
static const char *required_files[] = {
	"HERCULES.DRV",
	"HERCMINI.DRV",
	"HERCMINI.VXD",
	"HERC9X.INF",
	NULL
};

/* Win95 bloatware to skip in minimal copy */
static const char *skip_bloat[] = {
	"CS3KIT.EXE",
	"SETUP25I.EXE",
	"SETUP32.EXE",
	NULL
};

/* ---------- globals ---------- */

static char self_dir[MAX_PATH_LEN];     /* directory we're running from */
static char cd_path[MAX_PATH_LEN];      /* e.g. "D:\WIN95" */
static char extract_exe[MAX_PATH_LEN];  /* path to EXTRACT.EXE on CD */
static char dest_dir[MAX_PATH_LEN];     /* e.g. "C:\WIN95" */
static int  win_ver;                    /* 95 or 98 */
static int  minimal_copy;              /* 1 = skip big cabs for Win95 */

/* INF patcher line buffer */
static char *inf_lines[MAX_INF_LINES];
static int   inf_count;

/* CAB creator file catalog */
static char cab_files[MAX_CAB_FILES][13];  /* 8.3 filenames */

/* ---------- utility functions ---------- */

/*
 * Get free disk space on a drive (in MB).
 * drive_num: 1=A, 2=B, 3=C, ...
 * Returns -1 on error (drive not ready / not formatted).
 */
static long get_free_mb(int drive_num)
{
	union REGS r;
	unsigned long bytes_per_cluster, free_clusters;

	r.h.ah = 0x36;  /* Get Disk Free Space */
	r.h.dl = (unsigned char)drive_num;
	int86(0x21, &r, &r);

	if (r.w.ax == 0xFFFF)
		return -1;  /* invalid drive */

	bytes_per_cluster = (unsigned long)r.w.ax * (unsigned long)r.w.cx;
	free_clusters = (unsigned long)r.w.bx;

	return (long)((bytes_per_cluster / 1024UL) * free_clusters / 1024UL);
}

/*
 * Check if a drive letter is a CD-ROM via MSCDEX (INT 2Fh AX=150Bh).
 * Returns 1 if CD-ROM, 0 if not.
 */
static int is_cdrom(int drive_num)
{
	union REGS r;

	memset(&r, 0, sizeof(r));
	r.w.ax = 0x150B;
	r.w.bx = 0;     /* clear BX - MSCDEX sets it to ADADh if installed */
	r.w.cx = drive_num - 1;  /* 0-based for MSCDEX */
	int86(0x2F, &r, &r);

	/* AX != 0 means this drive is a CD-ROM; BX == ADADh confirms
	   MSCDEX handled the call (not some other INT 2Fh handler) */
	return (r.w.bx == 0xADAD && r.w.ax != 0) ? 1 : 0;
}

/*
 * Check if a file exists.
 */
static int file_exists(const char *path)
{
	return (access(path, 0) == 0);
}

/*
 * Case-insensitive string comparison.
 */
static int streqi(const char *a, const char *b)
{
	return (stricmp(a, b) == 0);
}

/*
 * Case-insensitive substring search.
 */
static const char *stristr(const char *hay, const char *needle)
{
	size_t nlen = strlen(needle);
	size_t hlen = strlen(hay);
	size_t i;

	if (nlen > hlen) return NULL;
	for (i = 0; i <= hlen - nlen; i++) {
		if (strnicmp(hay + i, needle, nlen) == 0)
			return hay + i;
	}
	return NULL;
}

/*
 * Strip trailing whitespace/newline in place.
 */
static void chomp(char *s)
{
	int len = (int)strlen(s);
	while (len > 0 && (s[len-1] == '\n' || s[len-1] == '\r' ||
	       s[len-1] == ' ' || s[len-1] == '\t'))
		s[--len] = '\0';
}

/*
 * Ask user Y/N. Returns 1 for Y, 0 for N.
 */
static int ask_yn(const char *prompt)
{
	int ch;
	printf("%s [Y/N] ", prompt);
	fflush(stdout);
	for (;;) {
		ch = getch();
		if (ch == 'y' || ch == 'Y') { printf("Y\n"); return 1; }
		if (ch == 'n' || ch == 'N') { printf("N\n"); return 0; }
	}
}

/*
 * Run an external program directly via spawnl (no COMMAND.COM needed).
 * exe = full path to executable
 * args = argument string (what would follow the exe name on command line)
 * Returns exit code, or -1 on failure.
 */
static int run_exe(const char *exe, const char *args)
{
	int rc;
	/* spawnlp with P_WAIT: run exe, wait for completion */
	rc = spawnl(P_WAIT, exe, exe, args, NULL);
	if (rc != 0)
		fprintf(stderr, "  Command failed (rc=%d): %s %s\n", rc, exe, args);
	return rc;
}

/*
 * Copy a file using binary read/write. Returns 0 on success.
 */
static int copy_file(const char *src, const char *dst)
{
	FILE *fin, *fout;
	static char buf[4096];  /* static: too large for small-model stack */
	size_t n;

	fin = fopen(src, "rb");
	if (!fin) return -1;

	fout = fopen(dst, "wb");
	if (!fout) { fclose(fin); return -1; }

	while ((n = fread(buf, 1, sizeof(buf), fin)) > 0) {
		if (fwrite(buf, 1, n, fout) != n) {
			fclose(fin); fclose(fout);
			return -1;
		}
	}

	fclose(fin);
	fclose(fout);
	return 0;
}

/*
 * Build a path: dest = dir + "\\" + file
 */
static void make_path(char *dest, const char *dir, const char *file)
{
	sprintf(dest, "%s\\%s", dir, file);
}

/*
 * Check if a line starts a new INF section (begins with '[').
 */
static int is_section_header(const char *line)
{
	const char *p = line;
	while (*p == ' ' || *p == '\t') p++;
	return (*p == '[');
}

/*
 * Check if a line's section header matches a name (case-insensitive).
 * e.g. line="[Manufacturer]" matches name="Manufacturer"
 */
static int section_matches(const char *line, const char *name)
{
	char sect[128];
	const char *p = line;
	int i = 0;

	while (*p == ' ' || *p == '\t') p++;
	if (*p != '[') return 0;
	p++;
	while (*p && *p != ']' && i < 126)
		sect[i++] = *p++;
	sect[i] = '\0';

	return (stricmp(sect, name) == 0);
}

/* ---------- Phase 1: Self-validation ---------- */

static int validate_self(void)
{
	char cwd[MAX_PATH_LEN];
	int drv;
	int i;
	char path[MAX_PATH_LEN];

	/* get current directory */
	if (getcwd(cwd, sizeof(cwd)) == NULL) {
		fprintf(stderr, "Error: cannot determine current directory.\n");
		return 0;
	}
	strncpy(self_dir, cwd, sizeof(self_dir) - 1);
	self_dir[sizeof(self_dir) - 1] = '\0';
	strupr(self_dir);

	/* check drive letter */
	drv = self_dir[0];
	if (drv < 'A' || drv > 'Z') {
		fprintf(stderr, "Error: cannot determine drive letter.\n");
		return 0;
	}

	/* reject floppy drives (A: or B:) */
	if (drv == 'A' || drv == 'B') {
		fprintf(stderr,
			"Error: cannot run from a floppy disk (%c:).\n"
			"Copy the mod pack to a hard drive folder first\n"
			"(e.g. C:\\W95HERC).\n", drv);
		return 0;
	}

	/* reject drive root (path is "X:\") */
	if (strlen(self_dir) <= 3) {
		fprintf(stderr,
			"Error: do not run from the drive root (%s).\n"
			"Create a folder like %c:\\W95HERC and run from there.\n",
			self_dir, drv);
		return 0;
	}

	/* reject CD-ROM */
	if (is_cdrom(drv - 'A' + 1)) {
		fprintf(stderr,
			"Error: cannot run from a CD-ROM drive (%c:).\n"
			"Copy the mod pack to a hard drive folder first.\n", drv);
		return 0;
	}

	/* reject C:\WIN95, C:\WIN98 */
	if (stristr(self_dir, "\\WIN95") || stristr(self_dir, "\\WIN98")) {
		fprintf(stderr,
			"Error: do not run from a Windows setup directory.\n"
			"Use a separate folder like %c:\\W95HERC.\n", drv);
		return 0;
	}

	/* check required files */
	for (i = 0; required_files[i]; i++) {
		make_path(path, self_dir, required_files[i]);
		if (!file_exists(path)) {
			fprintf(stderr, "Error: required file not found: %s\n",
			        required_files[i]);
			return 0;
		}
	}

	return 1;
}

/* ---------- Phase 2: Find Windows CD ---------- */

static int find_windows_cd(void)
{
	int drv;
	char probe[MAX_PATH_LEN];
	int found95 = 0, found98 = 0;
	char found_path95[MAX_PATH_LEN], found_path98[MAX_PATH_LEN];

	printf("Searching for Windows installation CD...\n");

	for (drv = 'C'; drv <= 'Z'; drv++) {
		/* poke CD-ROM drives to force MSCDEX to read the disc's
		   directory - otherwise file_exists() may fail on a disc
		   that hasn't been accessed yet */
		if (is_cdrom(drv - 'A' + 1)) {
			struct find_t ff;
			sprintf(probe, "%c:\\*.*", drv);
			_dos_findfirst(probe, _A_NORMAL | _A_RDONLY, &ff);
		}

		/* check both CD-ROMs and hard drives - user may have copied
		   Windows files to a partition */
		sprintf(probe, "%c:\\WIN95\\MINI.CAB", drv);
		if (file_exists(probe) && !found95) {
			sprintf(found_path95, "%c:\\WIN95", drv);
			found95 = 1;
		}

		sprintf(probe, "%c:\\WIN98\\MINI.CAB", drv);
		if (file_exists(probe) && !found98) {
			sprintf(found_path98, "%c:\\WIN98", drv);
			found98 = 1;
		}
	}

	if (!found95 && !found98) {
		fprintf(stderr,
			"Error: Windows 95/98 installation CD not found.\n"
			"Insert the CD and try again. Looking for:\n"
			"  X:\\WIN95\\MINI.CAB  or  X:\\WIN98\\MINI.CAB\n");
		return 0;
	}

	/* prefer 95 if both found */
	if (found95) {
		strcpy(cd_path, found_path95);
		win_ver = 95;
		if (found98)
			printf("  Found both Win95 and Win98; using Win95.\n");
	} else {
		strcpy(cd_path, found_path98);
		win_ver = 98;
	}

	printf("  Found Windows %d files: %s\n", win_ver, cd_path);

	/* locate EXTRACT.EXE */
	make_path(extract_exe, cd_path, "EXTRACT.EXE");
	if (!file_exists(extract_exe)) {
		/* try parent of cd_path (e.g. D:\EXTRACT.EXE) */
		sprintf(extract_exe, "%c:\\EXTRACT.EXE", cd_path[0]);
		if (!file_exists(extract_exe)) {
			fprintf(stderr,
				"Error: EXTRACT.EXE not found on the CD.\n"
				"Looked in: %s and %c:\\\n", cd_path, cd_path[0]);
			return 0;
		}
	}

	printf("  Found EXTRACT.EXE: %s\n", extract_exe);

	/* set destination */
	sprintf(dest_dir, "C:\\WIN%d", win_ver);

	return 1;
}

/* ---------- Phase 3: Validate hard drive ---------- */

static int validate_hdd(void)
{
	long free_mb;
	FILE *fp;

	printf("Checking hard drive C:...\n");

	/* check if C: is a CD-ROM */
	if (is_cdrom(3)) {
		fprintf(stderr,
			"Error: C: is a CD-ROM drive, not a hard disk.\n"
			"You must partition and format a hard drive first.\n"
			"Use FDISK to partition, then FORMAT C: to format.\n");
		return 0;
	}

	free_mb = get_free_mb(3);  /* C: = drive 3 */

	if (free_mb < 0) {
		fprintf(stderr,
			"Error: cannot read drive C: - it may not be formatted.\n"
			"Use FDISK to partition, then FORMAT C: to format.\n");
		return 0;
	}

	if (free_mb < MIN_DISK_MB) {
		fprintf(stderr,
			"Error: C: has only %ld MB free space.\n"
			"At least %d MB is required. This may be a RAM drive\n"
			"or boot disk, not a real hard drive.\n"
			"Use FDISK to partition a hard drive, then FORMAT C:.\n",
			free_mb, MIN_DISK_MB);
		return 0;
	}

	/* check writability */
	fp = fopen("C:\\HERC9X.TMP", "w");
	if (!fp) {
		fprintf(stderr, "Error: cannot write to C: drive.\n");
		return 0;
	}
	fclose(fp);
	remove("C:\\HERC9X.TMP");

	printf("  C: has %ld MB free.\n", free_mb);

	/* check if we need minimal copy for Win95 */
	if (win_ver == 95 && free_mb < LOW_DISK_MB_95) {
		printf("\n  Disk space is tight. A minimal copy will be used\n"
		       "  (~3.8 MB). Setup will ask for the CD path for some\n"
		       "  files during installation.\n");
		minimal_copy = 1;
	} else {
		minimal_copy = 0;
	}

	return 1;
}

/* ---------- Phase 3b: Load SMARTDRV for disk caching ---------- */

/*
 * Try to load SMARTDRV.EXE from the CD for faster disk I/O.
 * This is optional - silently skip if not found or if it fails.
 */
static void load_smartdrv(void)
{
	char cacheapp[MAX_PATH_LEN];

	make_path(cacheapp, cd_path, "XMSMMGR.EXE");

	printf("Loading XMSMMGR memory manager...\n");
	if (spawnl(P_WAIT, cacheapp, cacheapp, NULL) == 0)
		printf("  XMSMMGR loaded.\n");

	make_path(cacheapp, cd_path, "SMARTDRV.EXE");
	if (!file_exists(cacheapp)) {
		/* try parent dir (e.g. D:\SMARTDRV.EXE) */
		sprintf(cacheapp, "%c:\\SMARTDRV.EXE", cd_path[0]);
		if (!file_exists(cacheapp))
			return;  /* not found, skip silently */
	}

	printf("Loading SMARTDRV disk cache...\n");
	if (spawnl(P_WAIT, cacheapp, cacheapp, NULL) == 0)
		printf("  SMARTDRV loaded.\n");
}

/* ---------- Phase 4: User confirmation ---------- */

static int confirm_operation(void)
{
	printf("\n");
	printf("This tool will:\n");
	printf("  1. Create %s on the hard drive\n", dest_dir);
	printf("  2. Copy Windows %d setup files", win_ver);
	if (minimal_copy)
		printf(" (minimal, ~3.8 MB)");
	else if (win_ver == 95)
		printf(" (~60 MB)");
	else
		printf(" (~120 MB)");
	printf("\n");
	printf("  3. Rebuild MINI.CAB and PRECOPY CABs with Hercules driver\n");
	printf("  4. Place driver files for Setup to find\n");
	printf("\n");

	return ask_yn("Continue?");
}

/* ---------- Phase 5: Copy setup files ---------- */

/*
 * Check if filename matches a pattern to skip.
 * For minimal Win95: skip WIN95_*.CAB and bloatware.
 */
static int should_skip(const char *filename)
{
	int i;

	if (!minimal_copy)
		return 0;

	/* skip big CABs: WIN95_xx.CAB */
	if (strnicmp(filename, "WIN95_", 6) == 0)
		return 1;

	/* skip bloatware */
	for (i = 0; skip_bloat[i]; i++) {
		if (streqi(filename, skip_bloat[i]))
			return 1;
	}

	return 0;
}

static int copy_setup_files(void)
{
	struct find_t ff;
	char pattern[MAX_PATH_LEN];
	char src[MAX_PATH_LEN], dst[MAX_PATH_LEN];
	int count = 0, skip = 0;
	int rc;

	printf("\nCreating %s...\n", dest_dir);
	mkdir(dest_dir);

	printf("Copying setup files from %s...\n", cd_path);

	/* find all files in cd_path (not subdirectories) */
	sprintf(pattern, "%s\\*.*", cd_path);
	rc = _dos_findfirst(pattern, _A_NORMAL | _A_RDONLY | _A_ARCH, &ff);

	while (rc == 0) {
		if (should_skip(ff.name)) {
			skip++;
		} else {
			make_path(src, cd_path, ff.name);
			make_path(dst, dest_dir, ff.name);

			printf("  %-12s\r", ff.name);
			fflush(stdout);

			if (copy_file(src, dst) != 0) {
				fprintf(stderr, "  Warning: failed to copy %s\n",
				        ff.name);
			}
			count++;
		}
		rc = _dos_findnext(&ff);
	}

	printf("  %d files copied", count);
	if (skip > 0)
		printf(" (%d skipped for minimal install)", skip);
	printf(".\n");

	return 1;
}

/* ---------- Phase 6: Modify MINI.CAB ---------- */

/*
 * Patch a text file: replace a line containing 'find' with 'replace'.
 * Simple line-by-line processing. Returns number of replacements made.
 */
static int patch_ini_file(const char *filepath, const char *find,
                          const char *replace)
{
	FILE *fp;
	static char lines[48][256]; /* static: 12KB - INI files in MINI.CAB are small */
	int nlines = 0;
	int replaced = 0;
	int i;

	fp = fopen(filepath, "r");
	if (!fp) return -1;

	while (nlines < 48 && fgets(lines[nlines], 256, fp))
		nlines++;
	fclose(fp);

	for (i = 0; i < nlines; i++) {
		if (stristr(lines[i], find)) {
			/* replace entire line */
			sprintf(lines[i], "%s\r\n", replace);
			replaced++;
		}
	}

	if (replaced > 0) {
		fp = fopen(filepath, "w");
		if (!fp) return -1;
		for (i = 0; i < nlines; i++)
			fputs(lines[i], fp);
		fclose(fp);
	}

	return replaced;
}

/*
 * List files in a directory into an array. Returns count.
 */
static int list_dir_files(const char *dir, char files[][13], int max)
{
	struct find_t ff;
	char pattern[MAX_PATH_LEN];
	int count = 0;
	int rc;

	sprintf(pattern, "%s\\*.*", dir);
	rc = _dos_findfirst(pattern, _A_NORMAL | _A_RDONLY | _A_ARCH, &ff);
	while (rc == 0 && count < max) {
		strncpy(files[count], ff.name, 12);
		files[count][12] = '\0';
		count++;
		rc = _dos_findnext(&ff);
	}

	return count;
}

/* ---------- Minimal CAB creator (uncompressed) ---------- */

/*
 * CAB checksum: XOR-based, processes 4 bytes at a time (little-endian).
 * Matches the CSUMCompute algorithm from the Microsoft Cabinet spec.
 */
static unsigned long cab_checksum(const unsigned char *data,
                                  unsigned int len, unsigned long seed)
{
	unsigned long csum = seed;
	unsigned int nlong = len / 4;
	unsigned int rem = len % 4;
	unsigned int i;

	for (i = 0; i < nlong; i++) {
		unsigned long val;
		val  = (unsigned long)data[0];
		val |= (unsigned long)data[1] << 8;
		val |= (unsigned long)data[2] << 16;
		val |= (unsigned long)data[3] << 24;
		csum ^= val;
		data += 4;
	}

	/* handle remaining 1-3 bytes */
	if (rem >= 1) {
		unsigned long val = 0;
		val |= (unsigned long)data[0];
		if (rem >= 2) val |= (unsigned long)data[1] << 8;
		if (rem >= 3) val |= (unsigned long)data[2] << 16;
		csum ^= val;
	}

	return csum;
}

/*
 * Write a 16-bit little-endian value to a file.
 */
static void cab_write_u2(FILE *fp, unsigned int val)
{
	unsigned char b[2];
	b[0] = (unsigned char)(val & 0xFF);
	b[1] = (unsigned char)((val >> 8) & 0xFF);
	fwrite(b, 1, 2, fp);
}

/*
 * Write a 32-bit little-endian value to a file.
 */
static void cab_write_u4(FILE *fp, unsigned long val)
{
	unsigned char b[4];
	b[0] = (unsigned char)(val & 0xFF);
	b[1] = (unsigned char)((val >> 8) & 0xFF);
	b[2] = (unsigned char)((val >> 16) & 0xFF);
	b[3] = (unsigned char)((val >> 24) & 0xFF);
	fwrite(b, 1, 4, fp);
}

/*
 * Create an uncompressed CAB file from a list of files in a directory.
 *
 * cab_path:  output CAB file path
 * src_dir:   directory containing the source files
 * filenames: array of 8.3 filenames to include
 * nfiles:    number of files
 *
 * Returns 0 on success, -1 on failure.
 *
 * CAB layout (uncompressed, single folder, no spanning):
 *   CFHEADER (36 bytes)
 *   CFFOLDER (8 bytes, 1 folder)
 *   CFFILE[0..nfiles-1] (16 + strlen(name) + 1 each)
 *   CFDATA blocks (8 byte header + up to 32KB raw data each)
 */
static int cab_create(const char *cab_path, const char *src_dir,
                      char filenames[][13], int nfiles)
{
	FILE *out;
	FILE *fin;
	char path[MAX_PATH_LEN];
	static unsigned long file_sizes[MAX_CAB_FILES];
	unsigned long total_data = 0;
	unsigned long coffFiles;     /* offset to first CFFILE */
	unsigned long coffData;      /* offset to first CFDATA */
	unsigned long cbCabinet;     /* total cabinet size */
	int cCFData;                 /* number of data blocks */
	unsigned long folder_offset; /* running offset within folder */
	int i;
	static unsigned char block[CAB_BLOCK_SIZE]; /* static: 32KB */
	unsigned char hdr[8];        /* CFDATA header for checksum */
	unsigned long csum;
	size_t n;
	unsigned long data_written;

	/* first pass: get all file sizes */
	for (i = 0; i < nfiles; i++) {
		struct stat st;
		make_path(path, src_dir, filenames[i]);
		if (stat(path, &st) != 0) {
			fprintf(stderr, "  CAB: cannot stat %s\n", filenames[i]);
			return -1;
		}
		file_sizes[i] = (unsigned long)st.st_size;
		total_data += file_sizes[i];
	}

	/* calculate data block count */
	cCFData = (int)((total_data + CAB_BLOCK_SIZE - 1) / CAB_BLOCK_SIZE);
	if (cCFData == 0) cCFData = 1; /* at least one empty block */

	/* calculate offsets */
	coffFiles = 36 + 8;  /* CFHEADER(36) + 1 CFFOLDER(8) */

	coffData = coffFiles;
	for (i = 0; i < nfiles; i++)
		coffData += 16 + (unsigned long)strlen(filenames[i]) + 1;

	cbCabinet = coffData + (unsigned long)cCFData * 8 + total_data;

	/* open output */
	out = fopen(cab_path, "wb");
	if (!out) {
		fprintf(stderr, "  CAB: cannot create %s\n", cab_path);
		return -1;
	}

	/* --- CFHEADER (36 bytes) --- */
	fwrite("MSCF", 1, 4, out);    /* signature */
	cab_write_u4(out, 0);          /* reserved1 */
	cab_write_u4(out, cbCabinet);  /* cbCabinet */
	cab_write_u4(out, 0);          /* reserved2 */
	cab_write_u4(out, coffFiles);  /* coffFiles */
	cab_write_u4(out, 0);          /* reserved3 */
	fputc(3, out);                 /* versionMinor */
	fputc(1, out);                 /* versionMajor */
	cab_write_u2(out, 1);          /* cFolders */
	cab_write_u2(out, (unsigned int)nfiles); /* cFiles */
	cab_write_u2(out, 0);          /* flags */
	cab_write_u2(out, 0);          /* setID */
	cab_write_u2(out, 0);          /* iCabinet */

	/* --- CFFOLDER (8 bytes) --- */
	cab_write_u4(out, coffData);   /* coffCabStart */
	cab_write_u2(out, (unsigned int)cCFData); /* cCFData */
	cab_write_u2(out, 0);          /* typeCompress = NONE */

	/* --- CFFILE entries --- */
	folder_offset = 0;
	for (i = 0; i < nfiles; i++) {
		cab_write_u4(out, file_sizes[i]);     /* cbFile */
		cab_write_u4(out, folder_offset);     /* uoffFolderStart */
		cab_write_u2(out, 0);                 /* iFolder = 0 */
		cab_write_u2(out, 0);                 /* date (unused) */
		cab_write_u2(out, 0);                 /* time (unused) */
		cab_write_u2(out, 0x20);              /* attribs = _A_ARCH */
		fwrite(filenames[i], 1,
		       strlen(filenames[i]) + 1, out); /* szName + NUL */
		folder_offset += file_sizes[i];
	}

	/* --- CFDATA blocks --- */
	/* We need to stream all files concatenated, split into 32KB blocks.
	 * Open each file in sequence and fill blocks. */
	data_written = 0;

	/* We'll build blocks by reading files sequentially */
	{
		int fi = 0;              /* current file index */
		unsigned long fpos = 0;  /* position within current file */
		int file_open = 0;

		fin = NULL;

		while (data_written < total_data) {
			unsigned int block_len = 0;

			/* fill one block */
			while (block_len < CAB_BLOCK_SIZE && data_written + block_len < total_data) {
				/* open next file if needed */
				if (!file_open) {
					if (fi >= nfiles) break;
					make_path(path, src_dir, filenames[fi]);
					fin = fopen(path, "rb");
					if (!fin) {
						fprintf(stderr, "  CAB: cannot read %s\n",
						        filenames[fi]);
						fclose(out);
						return -1;
					}
					fpos = 0;
					file_open = 1;
				}

				/* read from current file */
				{
					unsigned int want = CAB_BLOCK_SIZE - block_len;
					unsigned long avail = file_sizes[fi] - fpos;
					if (avail > (unsigned long)want)
						avail = (unsigned long)want;

					n = fread(block + block_len, 1, (unsigned int)avail, fin);
					if (n == 0) {
						/* read error - treat file as exhausted */
						fprintf(stderr, "  CAB: read error on %s\n",
						        filenames[fi]);
						fclose(fin);
						fin = NULL;
						file_open = 0;
						fi++;
						break;
					}
					block_len += (unsigned int)n;
					fpos += (unsigned long)n;
				}

				/* check if file is exhausted */
				if (fpos >= file_sizes[fi]) {
					fclose(fin);
					fin = NULL;
					file_open = 0;
					fi++;
				}
			}

			/* compute checksum: first checksum the header bytes,
			 * then chain with the data checksum */
			hdr[0] = (unsigned char)(block_len & 0xFF);
			hdr[1] = (unsigned char)((block_len >> 8) & 0xFF);
			hdr[2] = (unsigned char)(block_len & 0xFF);
			hdr[3] = (unsigned char)((block_len >> 8) & 0xFF);
			csum = cab_checksum(hdr, 4, 0);
			csum = cab_checksum(block, block_len, csum);

			/* write CFDATA header */
			cab_write_u4(out, csum);                         /* csum */
			cab_write_u2(out, (unsigned int)block_len);      /* cbData */
			cab_write_u2(out, (unsigned int)block_len);      /* cbUncomp */

			/* write raw data */
			fwrite(block, 1, block_len, out);

			data_written += block_len;
		}

		if (fin) fclose(fin);
	}

	fclose(out);
	return 0;
}

/* ---------- Phase 6: Modify MINI.CAB ---------- */

static int modify_mini_cab(void)
{
	char mini_cab[MAX_PATH_LEN];
	char mini_dir[MAX_PATH_LEN];
	char cmd[MAX_PATH_LEN * 2];
	char tmp[MAX_PATH_LEN];
	int nfiles;

	printf("\nModifying MINI.CAB...\n");

	make_path(mini_cab, cd_path, "MINI.CAB");
	make_path(mini_dir, self_dir, "MINI");

	mkdir(mini_dir);

	/* extract MINI.CAB to temp dir */
	sprintf(cmd, "/Y /E /L %s %s", mini_dir, mini_cab);
	if (run_exe(extract_exe, cmd) != 0) {
		fprintf(stderr, "Error: failed to extract MINI.CAB\n");
		return 0;
	}

	/* copy HERCULES.DRV into MINI dir */
	make_path(tmp, self_dir, "HERCULES.DRV");
	{
		char dst_tmp[MAX_PATH_LEN];
		make_path(dst_tmp, mini_dir, "HERCULES.DRV");
		if (copy_file(tmp, dst_tmp) != 0) {
			fprintf(stderr, "Error: failed to copy HERCULES.DRV\n");
			return 0;
		}
	}

	/* delete VGA.DRV (replaced by HERCULES.DRV) */
	make_path(tmp, mini_dir, "VGA.DRV");
	remove(tmp);

	/* patch SYSTEM.INI: vga.drv -> hercules.drv */
	make_path(tmp, mini_dir, "SYSTEM.INI");
	if (patch_ini_file(tmp, "display.drv=vga.drv",
	                   "display.drv=hercules.drv") < 1) {
		fprintf(stderr, "Warning: could not patch SYSTEM.INI\n"
		        "  (display.drv=vga.drv not found)\n");
	} else {
		printf("  Patched SYSTEM.INI: display.drv=hercules.drv\n");
	}

	/* Win98: patch WIN.INI to disable TrueType */
	if (win_ver == 98) {
		make_path(tmp, mini_dir, "WIN.INI");
		if (file_exists(tmp)) {
			if (patch_ini_file(tmp, "TTEnable=1", "TTEnable=0") >= 1)
				printf("  Patched WIN.INI: TTEnable=0\n");
		}
	}

	/* rebuild MINI.CAB (uncompressed) */
	nfiles = list_dir_files(mini_dir, cab_files, MAX_CAB_FILES);
	make_path(tmp, dest_dir, "MINI.CAB");
	if (cab_create(tmp, mini_dir, cab_files, nfiles) != 0) {
		fprintf(stderr, "Error: failed to create MINI.CAB\n");
		return 0;
	}

	printf("  Rebuilt MINI.CAB (%d files, uncompressed)\n", nfiles);
	return 1;
}

/* ---------- Phase 7: INF patcher ---------- */

/*
 * Load an INF file into the inf_lines[] array.
 */
static int inf_load(const char *path)
{
	FILE *fp;
	char buf[MAX_LINE_LEN];

	inf_count = 0;

	fp = fopen(path, "r");
	if (!fp) return -1;

	while (fgets(buf, MAX_LINE_LEN, fp) && inf_count < MAX_INF_LINES) {
		chomp(buf);
		inf_lines[inf_count] = strdup(buf);
		if (!inf_lines[inf_count]) {
			fclose(fp);
			fprintf(stderr, "Error: out of memory loading INF\n");
			return -1;
		}
		inf_count++;
	}

	fclose(fp);
	return inf_count;
}

/*
 * Save the inf_lines[] array back to a file.
 */
static int inf_save(const char *path)
{
	FILE *fp;
	int i;

	fp = fopen(path, "w");
	if (!fp) return -1;

	for (i = 0; i < inf_count; i++)
		fprintf(fp, "%s\r\n", inf_lines[i]);

	fclose(fp);
	return 0;
}

/*
 * Free all inf_lines memory.
 */
static void inf_free(void)
{
	int i;
	for (i = 0; i < inf_count; i++) {
		free(inf_lines[i]);
		inf_lines[i] = NULL;
	}
	inf_count = 0;
}

/*
 * Insert a line at position 'pos', shifting everything down.
 * Returns new count, or -1 if full.
 */
static int inf_insert(int pos, const char *text)
{
	int i;
	if (inf_count >= MAX_INF_LINES) return -1;
	if (pos > inf_count) pos = inf_count;

	for (i = inf_count; i > pos; i--)
		inf_lines[i] = inf_lines[i-1];

	inf_lines[pos] = strdup(text);
	inf_count++;
	return inf_count;
}

/*
 * Insert multiple lines at position 'pos'. Lines are separated by \n
 * in the input string. Returns number of lines inserted.
 */
static int inf_insert_block(int pos, const char *block)
{
	char buf[MAX_LINE_LEN];
	int inserted = 0;
	const char *p = block;

	while (*p) {
		int i = 0;
		while (*p && *p != '\n' && i < MAX_LINE_LEN - 1)
			buf[i++] = *p++;
		buf[i] = '\0';
		if (*p == '\n') p++;

		if (inf_insert(pos + inserted, buf) < 0) break;
		inserted++;
	}

	return inserted;
}

/*
 * Find the line index of a section header. Returns -1 if not found.
 */
static int inf_find_section(const char *name)
{
	int i;
	for (i = 0; i < inf_count; i++) {
		if (section_matches(inf_lines[i], name))
			return i;
	}
	return -1;
}

/*
 * Find the last line of a section (before next section header or EOF).
 */
static int inf_section_end(int section_start)
{
	int i;
	for (i = section_start + 1; i < inf_count; i++) {
		if (is_section_header(inf_lines[i]))
			return i - 1;
	}
	return inf_count - 1;
}

/*
 * Find a line containing a substring (case-insensitive), starting at 'from'.
 * Returns line index or -1.
 */
static int inf_find_line(int from, const char *substr)
{
	int i;
	for (i = from; i < inf_count; i++) {
		if (stristr(inf_lines[i], substr))
			return i;
	}
	return -1;
}

/* The Herc9x install sections to inject into msdisp.inf */
static const char herc9x_install_block[] =
	"\n"
	"; -------------- Herc9x Project (Hercules MDA/HGC)\n"
	"[Herc9x]\n"
	"CopyFiles=Herc9x.Copy\n"
	"DelReg=Prev.DelReg\n"
	"AddReg=Herc9x.AddReg\n"
	"UpdateInis=Herc9x.UpdateInis\n"
	"LogConfig=Herc9x.LogConfig\n"
	"\n"
	"[Herc9x.Copy]\n"
	"hercmini.drv\n"
	"hercmini.vxd\n"
	"\n"
	"[Herc9x.AddReg]\n"
	"HKR,,Ver,,4.0\n"
	"HKR,,DevLoader,,*configmg\n"
	"HKR,,DriverDesc,,%Herc9x.DeviceDesc%\n"
	"HKR,DEFAULT,drv,,hercmini.drv\n"
	"HKR,DEFAULT,Mode,,\"1,720,348\"\n"
	"HKR,DEFAULT,RefreshRate,,-1\n"
	"HKR,DEFAULT,DDC,,0\n"
	"HKR,INFO,ChipType,,\"Hercules\"\n"
	"HKR,INFO,DACType,,\"Monochrome\"\n"
	"HKR,INFO,MemType,,\"Standard\"\n"
	"HKR,INFO,VideoMemory,1,00,00,01,00\n"
	"HKR,\"MODES\\1\\720,348\"\n"
	"HKR,\"MODES\\1\\720,522\"\n"
	"\n"
	"[Herc9x.UpdateInis]\n"
	"system.ini,boot,\"display.drv=*\",\"display.drv=pnpdrvr.drv\"\n"
	"system.ini,386Enh,,\"device=hercmini.vxd\"\n"
	"\n"
	"[Herc9x.LogConfig]\n"
	"ConfigPriority=HARDWIRED\n"
	"IOConfig=3B0-3BF\n"
	"MemConfig=B0000-BFFFF\n";

/* Strings to add */
static const char herc9x_strings[] =
	"Herc9x=\"Herc9x Project\"\n"
	"Herc9x.DeviceDesc=\"Hercules Graphics Card (ISA)\"\n";

/*
 * Patch msdisp.inf with Herc9x driver entries.
 * Performs 5 targeted insertions.
 */
static int patch_msdisp_inf(const char *inf_path)
{
	int idx, end, n;

	if (inf_load(inf_path) < 0) {
		fprintf(stderr, "Error: cannot load %s\n", inf_path);
		return 0;
	}

	printf("  Patching MSDISP.INF (%d lines)...\n", inf_count);

	/* 1. [DestinationDirs]: add Herc9x.Copy entry */
	idx = inf_find_section("DestinationDirs");
	if (idx < 0) {
		fprintf(stderr, "Error: [DestinationDirs] not found in INF\n");
		inf_free();
		return 0;
	}
	end = inf_section_end(idx);
	inf_insert(end + 1,
		"Herc9x.Copy     =11             ;LDID_SYS");
	printf("    Added Herc9x.Copy to [DestinationDirs]\n");

	/* 2. [Manufacturer]: add %Herc9x% line after %Herc% */
	idx = inf_find_section("Manufacturer");
	if (idx < 0) {
		fprintf(stderr, "Error: [Manufacturer] not found in INF\n");
		inf_free();
		return 0;
	}
	{
		int herc_line = inf_find_line(idx, "%Herc%");
		if (herc_line >= 0) {
			inf_insert(herc_line + 1,
				"%Herc9x%=Mfg.Herc9x");
			printf("    Added %%Herc9x%% to [Manufacturer]\n");
		} else {
			/* no existing Herc entry; add at end of section */
			end = inf_section_end(idx);
			inf_insert(end + 1,
				"%Herc9x%=Mfg.Herc9x");
			printf("    Added %%Herc9x%% to [Manufacturer] (at end)\n");
		}
	}

	/* 3. After [Mfg.Herc] section: add [Mfg.Herc9x] */
	idx = inf_find_section("Mfg.Herc");
	if (idx >= 0) {
		end = inf_section_end(idx);
		n = inf_insert_block(end + 1,
			"\n"
			"[Mfg.Herc9x]\n"
			"%Herc9x.DeviceDesc%=Herc9x, Display_Herc9x\n");
		printf("    Added [Mfg.Herc9x] section\n");
	} else {
		/* Mfg.Herc doesn't exist - find Manufacturer and add after
		   last Mfg.* section */
		idx = inf_find_section("Manufacturer");
		if (idx >= 0) {
			end = inf_section_end(idx);
			/* skip past all Mfg.* sections */
			while (end + 1 < inf_count) {
				if (is_section_header(inf_lines[end + 1]) &&
				    strnicmp(inf_lines[end + 1] + 1, "Mfg.", 4) == 0) {
					end = inf_section_end(end + 1);
				} else {
					break;
				}
			}
			n = inf_insert_block(end + 1,
				"\n"
				"[Mfg.Herc9x]\n"
				"%Herc9x.DeviceDesc%=Herc9x, Display_Herc9x\n");
			printf("    Added [Mfg.Herc9x] section (after Mfg blocks)\n");
		}
	}

	/* 4. Insert install sections just above [Strings] */
	idx = inf_find_section("Strings");
	if (idx < 0) {
		fprintf(stderr, "Error: [Strings] not found in INF\n");
		inf_free();
		return 0;
	}
	/* back up past blank lines and comments above [Strings] */
	{
		int insert_pos = idx;
		while (insert_pos > 0) {
			const char *prev = inf_lines[insert_pos - 1];
			/* skip blank lines and comment lines */
			if (prev[0] == '\0' || prev[0] == ';' ||
			    prev[0] == ' ' || prev[0] == '\t') {
				/* check if it's truly a comment about Strings */
				insert_pos--;
			} else {
				break;
			}
		}
		/* actually, insert right before [Strings] line for clarity */
		insert_pos = idx;
		n = inf_insert_block(insert_pos, herc9x_install_block);
		printf("    Added Herc9x install sections (%d lines)\n", n);
	}

	/* 5. Append to [Strings] */
	/* [Strings] has moved due to insertions, re-find it */
	idx = inf_find_section("Strings");
	if (idx >= 0) {
		end = inf_section_end(idx);
		inf_insert_block(end + 1, herc9x_strings);
		printf("    Added Herc9x strings\n");
	}

	/* save */
	if (inf_save(inf_path) < 0) {
		fprintf(stderr, "Error: cannot write patched INF\n");
		inf_free();
		return 0;
	}

	printf("  MSDISP.INF patched successfully (%d lines).\n", inf_count);
	inf_free();
	return 1;
}

/* ---------- Phase 7: Modify PRECOPY CABs ---------- */

/*
 * Extract PRECOPY1.CAB (with /A to follow chained PRECOPY2.CAB) into
 * one folder, patch MSDISP.INF, add HERC9X.INF, and repack as a single
 * unified PRECOPY1.CAB.  Delete PRECOPY2.CAB from dest since all files
 * are now in PRECOPY1.
 */
static int modify_precopy_cabs(void)
{
	char pc_dir[MAX_PATH_LEN];
	char cab_path[MAX_PATH_LEN];
	char cmd[MAX_PATH_LEN * 2];
	char inf_path[MAX_PATH_LEN];
	char tmp[MAX_PATH_LEN];
	int nfiles;

	printf("\nModifying PRECOPY CABs...\n");

	/* create temp dir */
	make_path(pc_dir, self_dir, "PRECOPY");
	mkdir(pc_dir);

	/* extract PRECOPY1.CAB with /A to follow chained cabs */
	make_path(cab_path, cd_path, "PRECOPY1.CAB");
	if (!file_exists(cab_path)) {
		fprintf(stderr, "Error: %s not found\n", cab_path);
		return 0;
	}
	sprintf(cmd, "/A /Y /E /L %s %s", pc_dir, cab_path);
	printf("  Extracting PRECOPY cabs (following chain)...\n");
	if (run_exe(extract_exe, cmd) != 0) {
		fprintf(stderr, "Error: failed to extract PRECOPY cabs\n");
		return 0;
	}

	/* patch MSDISP.INF */
	make_path(inf_path, pc_dir, "MSDISP.INF");
	if (!file_exists(inf_path)) {
		fprintf(stderr, "Error: MSDISP.INF not found in PRECOPY cabs\n");
		return 0;
	}

	if (!patch_msdisp_inf(inf_path))
		return 0;

	/* copy HERC9X.INF into PRECOPY dir */
	{
		char herc_src[MAX_PATH_LEN], herc_dst[MAX_PATH_LEN];
		make_path(herc_src, self_dir, "HERC9X.INF");
		make_path(herc_dst, pc_dir, "HERC9X.INF");
		if (copy_file(herc_src, herc_dst) != 0) {
			fprintf(stderr, "Error: failed to copy HERC9X.INF\n");
			return 0;
		}
	}

	/* rebuild as single PRECOPY1.CAB */
	nfiles = list_dir_files(pc_dir, cab_files, MAX_CAB_FILES);
	make_path(cab_path, dest_dir, "PRECOPY1.CAB");
	printf("  Rebuilding PRECOPY1.CAB (%d files)...\n", nfiles);
	if (cab_create(cab_path, pc_dir, cab_files, nfiles) != 0) {
		fprintf(stderr, "Error: failed to create PRECOPY1.CAB\n");
		return 0;
	}

	/* delete PRECOPY2.CAB from dest (all files now in PRECOPY1) */
	make_path(tmp, dest_dir, "PRECOPY2.CAB");
	remove(tmp);

	printf("  Rebuilt unified PRECOPY1.CAB (uncompressed)\n");
	return 1;
}

/* ---------- Phase 8: Deploy modified files ---------- */

static int deploy_files(void)
{
	char src[MAX_PATH_LEN], dst[MAX_PATH_LEN];
	int ok = 1;

	printf("\nDeploying driver files to %s...\n", dest_dir);

	/* CABs were written directly to dest_dir by cab_create().
	 * Just need to copy loose driver files for Setup's CopyFiles. */

	make_path(src, self_dir, "HERCMINI.DRV");
	make_path(dst, dest_dir, "HERCMINI.DRV");
	if (copy_file(src, dst) != 0) {
		fprintf(stderr, "  Error: failed to copy HERCMINI.DRV\n");
		ok = 0;
	}

	make_path(src, self_dir, "HERCMINI.VXD");
	make_path(dst, dest_dir, "HERCMINI.VXD");
	if (copy_file(src, dst) != 0) {
		fprintf(stderr, "  Error: failed to copy HERCMINI.VXD\n");
		ok = 0;
	}

	if (ok)
		printf("  Driver files deployed.\n");

	return ok;
}

/* ---------- Phase 9: Cleanup and instructions ---------- */

/*
 * Recursively delete a directory's files (non-recursive, flat dirs only).
 */
static void rmdir_files(const char *dir)
{
	struct find_t ff;
	char pattern[MAX_PATH_LEN], path[MAX_PATH_LEN];
	int rc;

	sprintf(pattern, "%s\\*.*", dir);
	rc = _dos_findfirst(pattern, _A_NORMAL | _A_RDONLY | _A_ARCH, &ff);
	while (rc == 0) {
		make_path(path, dir, ff.name);
		remove(path);
		rc = _dos_findnext(&ff);
	}
	rmdir(dir);
}

static void cleanup_and_instruct(void)
{
	char tmp[MAX_PATH_LEN];

	printf("\nCleaning up temporary files...\n");

	/* remove temp directories */
	make_path(tmp, self_dir, "MINI");
	rmdir_files(tmp);

	make_path(tmp, self_dir, "PRECOPY");
	rmdir_files(tmp);

	/* final instructions */
	printf("\n");
	printf("============================================================\n");
	printf("  Setup files are ready in %s\n", dest_dir);
	printf("============================================================\n");
	printf("\n");
	printf("To install Windows %d:\n", win_ver);
	printf("  C:\n");
	printf("  cd \\WIN%d\n", win_ver);
	printf("  setup\n");
	printf("\n");
	printf("IMPORTANT: Choose \"Custom\" setup type.\n");
	printf("After selecting software components, you will see a\n");
	printf("hardware confirmation page. Click \"Change Display\"\n");
	printf("and select:\n");
	printf("  Manufacturer:  Herc9x Project\n");
	printf("  Model:         Hercules Graphics Card (ISA)\n");
	printf("\n");
	printf("Then continue with Setup as normal.\n");
	if (minimal_copy) {
		printf("\nNote: Minimal copy was used. Setup may ask for the\n");
		printf("CD-ROM path for some files. Enter: %s\n", cd_path);
	}
	printf("\n");
}

/* ---------- main ---------- */

int main(void)
{
	printf("SETUPMOD - Herc9x Windows Setup Modifier v1.0\n");
	printf("Integrates Hercules display driver into Windows 95/98 Setup\n\n");

	if (!validate_self())
		return 1;

	if (!find_windows_cd())
		return 1;

	if (win_ver == 98) {
		fprintf(stderr,
			"Windows 98 support is not yet available.\n"
			"Check https://github.com/FalconFour/Herc9x for updates.\n");
		return 1;
	}

	if (!validate_hdd())
		return 1;

	load_smartdrv();

	if (!confirm_operation())
		return 0;

	if (!copy_setup_files())
		return 1;

	if (!modify_mini_cab())
		return 1;

	if (!modify_precopy_cabs())
		return 1;

	if (!deploy_files())
		return 1;

	cleanup_and_instruct();

	return 0;
}
