/*
 * SETUPMOD - Windows 95/98 Setup Modifier for Hercules Display Driver
 *
 * Prepares a Windows 95/98 installation on the hard drive with the Herc9x
 * display driver pre-integrated, so the user can select "Hercules Graphics
 * Card (ISA)" during a Custom setup instead of VGA.
 *
 * Expects to run from a mod pack folder (e.g. C:\W95HERC) containing:
 *   SETUPMOD.EXE, MAKECAB.EXE, HERCULES.DRV, HERCMINI.DRV, HERCMINI.VXD,
 *   HERC9X.INF
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
#define MAX_CAB_FILES   256     /* max files in one precopy cab */
#define MIN_DISK_MB     100     /* minimum usable HDD */
#define LOW_DISK_MB_95  150     /* threshold for minimal Win95 copy */

/* files we must find in our own directory */
static const char *required_files[] = {
	"HERCULES.DRV",
	"HERCMINI.DRV",
	"HERCMINI.VXD",
	"MAKECAB.EXE",
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

/* PRECOPY file catalogs */
static char pc1_files[MAX_CAB_FILES][13];  /* 8.3 filenames from PRECOPY1 */
static int  pc1_count;
static char pc2_files[MAX_CAB_FILES][13];  /* 8.3 filenames from PRECOPY2 */
static int  pc2_count;

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
 * Run a command, return exit code. Prints command if it fails.
 */
static int run_cmd(const char *cmd)
{
	int rc = system(cmd);
	if (rc != 0)
		fprintf(stderr, "  Command failed (rc=%d): %s\n", rc, cmd);
	return rc;
}

/*
 * Copy a file using binary read/write. Returns 0 on success.
 */
static int copy_file(const char *src, const char *dst)
{
	FILE *fin, *fout;
	char buf[4096];
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
	printf("  3. Modify setup CABs to integrate Hercules driver\n");
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

			if (copy_file(src, dst) != 0) {
				fprintf(stderr, "  Warning: failed to copy %s\n",
				        ff.name);
			}
			count++;

			/* progress every 10 files */
			if (count % 10 == 0) {
				printf("  %d files copied...\r", count);
				fflush(stdout);
			}
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
	char lines[64][MAX_LINE_LEN];
	int nlines = 0;
	int replaced = 0;
	int i;

	fp = fopen(filepath, "r");
	if (!fp) return -1;

	while (nlines < 64 && fgets(lines[nlines], MAX_LINE_LEN, fp))
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

/*
 * Create a DDF for MAKECAB and run it. Files are listed by path.
 * ddf_path: where to write the DDF
 * cab_name: output CAB filename
 * out_dir: where to put the CAB
 * file_dir: directory containing the source files
 * files: array of filenames
 * nfiles: count
 * compression: "MSZIP" or "LZX"
 */
static int make_single_cab(const char *ddf_path, const char *cab_name,
                           const char *out_dir, const char *file_dir,
                           char files[][13], int nfiles,
                           const char *compression)
{
	FILE *fp;
	char cmd[MAX_PATH_LEN * 2];
	char mcab[MAX_PATH_LEN];
	int i;

	fp = fopen(ddf_path, "w");
	if (!fp) return -1;

	fprintf(fp, ".OPTION EXPLICIT\n");
	fprintf(fp, ".Set Cabinet=on\n");
	fprintf(fp, ".Set Compress=on\n");
	fprintf(fp, ".Set CabinetName1=%s\n", cab_name);
	fprintf(fp, ".Set DiskDirectoryTemplate=%s\n", out_dir);
	fprintf(fp, ".Set MaxDiskSize=0\n");
	fprintf(fp, ".Set CompressionType=%s\n", compression);
	if (stricmp(compression, "LZX") == 0)
		fprintf(fp, ".Set CompressionMemory=18\n");

	for (i = 0; i < nfiles; i++)
		fprintf(fp, "\"%s\\%s\" \"%s\"\n", file_dir, files[i], files[i]);

	fclose(fp);

	make_path(mcab, self_dir, "MAKECAB.EXE");
	sprintf(cmd, "%s /F %s", mcab, ddf_path);
	return run_cmd(cmd);
}

static int modify_mini_cab(void)
{
	char mini_cab[MAX_PATH_LEN];
	char mini_dir[MAX_PATH_LEN];
	char cmd[MAX_PATH_LEN * 2];
	char tmp[MAX_PATH_LEN];
	char files[64][13];
	int nfiles;
	const char *compression;

	printf("\nModifying MINI.CAB...\n");

	/* paths */
	make_path(mini_cab, cd_path, "MINI.CAB");
	make_path(mini_dir, self_dir, "MINI");

	/* create temp directory */
	mkdir(mini_dir);

	/* extract MINI.CAB */
	sprintf(cmd, "%s /Y /E /L %s %s", extract_exe, mini_dir, mini_cab);
	if (run_cmd(cmd) != 0) {
		fprintf(stderr, "Error: failed to extract MINI.CAB\n");
		return 0;
	}

	/* copy HERCULES.DRV into MINI */
	make_path(tmp, self_dir, "HERCULES.DRV");
	{
		char dst_tmp[MAX_PATH_LEN];
		make_path(dst_tmp, mini_dir, "HERCULES.DRV");
		if (copy_file(tmp, dst_tmp) != 0) {
			fprintf(stderr, "Error: failed to copy HERCULES.DRV\n");
			return 0;
		}
	}

	/* delete VGA.DRV from MINI */
	make_path(tmp, mini_dir, "VGA.DRV");
	remove(tmp);

	/* patch SYSTEM.INI */
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

	/* rebuild MINI.CAB */
	compression = (win_ver == 98) ? "LZX" : "MSZIP";

	/* list files in MINI dir */
	nfiles = list_dir_files(mini_dir, files, 64);

	make_path(tmp, self_dir, "MINI.DDF");
	if (make_single_cab(tmp, "MINI.CAB", self_dir, mini_dir,
	                    files, nfiles, compression) != 0) {
		fprintf(stderr, "Error: MAKECAB failed for MINI.CAB\n");
		return 0;
	}

	printf("  Rebuilt MINI.CAB (%d files, %s compression)\n",
	       nfiles, compression);

	/* clean up DDF and setup.inf/rpt that MAKECAB creates */
	remove(tmp);
	make_path(tmp, self_dir, "setup.inf");
	remove(tmp);
	make_path(tmp, self_dir, "setup.rpt");
	remove(tmp);

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

static int modify_precopy_cabs(void)
{
	char pc1_dir[MAX_PATH_LEN], pc2_dir[MAX_PATH_LEN];
	char cab_path[MAX_PATH_LEN];
	char cmd[MAX_PATH_LEN * 2];
	char tmp[MAX_PATH_LEN];
	char ddf_path[MAX_PATH_LEN];
	FILE *fp;
	int i;
	const char *compression;
	char mcab[MAX_PATH_LEN];
	char inf_path[MAX_PATH_LEN];

	printf("\nModifying PRECOPY1.CAB and PRECOPY2.CAB...\n");

	/* create temp dirs in our working directory */
	make_path(pc1_dir, self_dir, "PC1");
	make_path(pc2_dir, self_dir, "PC2");
	mkdir(pc1_dir);
	mkdir(pc2_dir);

	/* extract PRECOPY1.CAB to PC1\ */
	make_path(cab_path, cd_path, "PRECOPY1.CAB");
	if (!file_exists(cab_path)) {
		fprintf(stderr, "Error: %s not found\n", cab_path);
		return 0;
	}
	sprintf(cmd, "%s /Y /E /L %s %s", extract_exe, pc1_dir, cab_path);
	printf("  Extracting PRECOPY1.CAB...\n");
	if (run_cmd(cmd) != 0) {
		fprintf(stderr, "Error: failed to extract PRECOPY1.CAB\n");
		return 0;
	}

	/* extract PRECOPY2.CAB to PC2\ */
	make_path(cab_path, cd_path, "PRECOPY2.CAB");
	if (!file_exists(cab_path)) {
		fprintf(stderr, "Error: %s not found\n", cab_path);
		return 0;
	}
	sprintf(cmd, "%s /Y /E /L %s %s", extract_exe, pc2_dir, cab_path);
	printf("  Extracting PRECOPY2.CAB...\n");
	if (run_cmd(cmd) != 0) {
		fprintf(stderr, "Error: failed to extract PRECOPY2.CAB\n");
		return 0;
	}

	/* catalog files from each */
	pc1_count = list_dir_files(pc1_dir, pc1_files, MAX_CAB_FILES);
	pc2_count = list_dir_files(pc2_dir, pc2_files, MAX_CAB_FILES);
	printf("  PRECOPY1: %d files, PRECOPY2: %d files\n",
	       pc1_count, pc2_count);

	/* patch MSDISP.INF (should be in PC2) */
	make_path(inf_path, pc2_dir, "MSDISP.INF");
	if (!file_exists(inf_path)) {
		/* might be in PC1 on some versions */
		make_path(inf_path, pc1_dir, "MSDISP.INF");
		if (!file_exists(inf_path)) {
			fprintf(stderr, "Error: MSDISP.INF not found in PRECOPY cabs\n");
			return 0;
		}
	}

	if (!patch_msdisp_inf(inf_path))
		return 0;

	/* copy HERC9X.INF into the same dir as MSDISP.INF */
	{
		char herc_inf_src[MAX_PATH_LEN], herc_inf_dst[MAX_PATH_LEN];
		/* figure out which dir msdisp.inf is in */
		char *inf_dir;
		if (stristr(inf_path, "PC2"))
			inf_dir = pc2_dir;
		else
			inf_dir = pc1_dir;

		make_path(herc_inf_src, self_dir, "HERC9X.INF");
		make_path(herc_inf_dst, inf_dir, "HERC9X.INF");
		if (copy_file(herc_inf_src, herc_inf_dst) != 0) {
			fprintf(stderr, "Error: failed to copy HERC9X.INF\n");
			return 0;
		}

		/* add herc9x.inf to the file list for that cab */
		if (inf_dir == pc2_dir && pc2_count < MAX_CAB_FILES) {
			strcpy(pc2_files[pc2_count], "HERC9X.INF");
			pc2_count++;
		} else if (inf_dir == pc1_dir && pc1_count < MAX_CAB_FILES) {
			strcpy(pc1_files[pc1_count], "HERC9X.INF");
			pc1_count++;
		}
	}

	/* refresh catalogs in case extraction changed things */
	/* (we already have them, the only addition is HERC9X.INF above) */

	/* generate DDF that produces precopy1.cab and precopy2.cab */
	compression = (win_ver == 98) ? "LZX" : "MSZIP";
	make_path(ddf_path, self_dir, "PRECOPY.DDF");
	make_path(mcab, self_dir, "MAKECAB.EXE");

	fp = fopen(ddf_path, "w");
	if (!fp) {
		fprintf(stderr, "Error: cannot create PRECOPY.DDF\n");
		return 0;
	}

	fprintf(fp, ".OPTION EXPLICIT\n");
	fprintf(fp, ".Set Cabinet=on\n");
	fprintf(fp, ".Set Compress=on\n");
	fprintf(fp, ".Set CabinetNameTemplate=precopy*.cab\n");
	fprintf(fp, ".Set DiskDirectoryTemplate=%s\n", self_dir);
	fprintf(fp, ".Set MaxDiskSize=4608000\n");
	fprintf(fp, ".Set CompressionType=%s\n", compression);
	if (stricmp(compression, "LZX") == 0)
		fprintf(fp, ".Set CompressionMemory=18\n");
	fprintf(fp, "\n");

	/* PRECOPY1 files from PC1 dir */
	for (i = 0; i < pc1_count; i++)
		fprintf(fp, "\"%s\\%s\" \"%s\"\n", pc1_dir, pc1_files[i],
		        pc1_files[i]);

	/* force new cabinet */
	fprintf(fp, "\n.New Cabinet\n\n");

	/* PRECOPY2 files from PC2 dir */
	for (i = 0; i < pc2_count; i++)
		fprintf(fp, "\"%s\\%s\" \"%s\"\n", pc2_dir, pc2_files[i],
		        pc2_files[i]);

	fclose(fp);

	/* run MAKECAB */
	printf("  Rebuilding PRECOPY1.CAB and PRECOPY2.CAB...\n");
	sprintf(cmd, "%s /F %s", mcab, ddf_path);
	if (run_cmd(cmd) != 0) {
		fprintf(stderr, "Error: MAKECAB failed for PRECOPY cabs\n");
		return 0;
	}

	printf("  Rebuilt PRECOPY1.CAB (%d files) and PRECOPY2.CAB (%d files)\n",
	       pc1_count, pc2_count);

	/* clean up DDF and MAKECAB artifacts */
	remove(ddf_path);
	make_path(tmp, self_dir, "setup.inf");
	remove(tmp);
	make_path(tmp, self_dir, "setup.rpt");
	remove(tmp);

	return 1;
}

/* ---------- Phase 8: Deploy modified files ---------- */

static int deploy_files(void)
{
	char src[MAX_PATH_LEN], dst[MAX_PATH_LEN];
	int ok = 1;

	printf("\nDeploying modified files to %s...\n", dest_dir);

	/* copy rebuilt CABs */
	make_path(src, self_dir, "MINI.CAB");
	make_path(dst, dest_dir, "MINI.CAB");
	if (copy_file(src, dst) != 0) {
		fprintf(stderr, "  Error: failed to copy MINI.CAB\n");
		ok = 0;
	}

	make_path(src, self_dir, "PRECOPY1.CAB");
	make_path(dst, dest_dir, "PRECOPY1.CAB");
	if (copy_file(src, dst) != 0) {
		fprintf(stderr, "  Error: failed to copy PRECOPY1.CAB\n");
		ok = 0;
	}

	make_path(src, self_dir, "PRECOPY2.CAB");
	make_path(dst, dest_dir, "PRECOPY2.CAB");
	if (copy_file(src, dst) != 0) {
		fprintf(stderr, "  Error: failed to copy PRECOPY2.CAB\n");
		ok = 0;
	}

	/* copy driver files (Setup needs them loose for CopyFiles) */
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
		printf("  All files deployed.\n");

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

	make_path(tmp, self_dir, "PC1");
	rmdir_files(tmp);

	make_path(tmp, self_dir, "PC2");
	rmdir_files(tmp);

	/* remove rebuilt CABs from our directory (already copied to dest) */
	make_path(tmp, self_dir, "MINI.CAB");
	remove(tmp);
	make_path(tmp, self_dir, "PRECOPY1.CAB");
	remove(tmp);
	make_path(tmp, self_dir, "PRECOPY2.CAB");
	remove(tmp);

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

	if (!validate_hdd())
		return 1;

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
