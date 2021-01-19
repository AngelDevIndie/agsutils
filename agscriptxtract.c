#define _GNU_SOURCE
#include "DataFile.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include "version.h"
#define ADS ":::AGStract " VERSION " by rofl0r:::"

static int usage(char *argv0) {
	dprintf(2, ADS "\nusage:\n%s [-oblf] dir [outdir]\n"
		   "extract all scripts from game files in dir\n"
		   "pass a directory with extracted game files.\n"
		   "options:\n"
		   "-o : dump offset comments in disassembly\n"
		   "-b : dump hexadecimal bytecode comments in disassembly\n"
		   "-f : dump informative original fixups section\n"
		   "-l : remove linenumber debug assembly directives [produces smaller files]\n"
		   , argv0);
	return 1;
}

static void disas(const char*inp, char *o, int flags) {
	//ARF_find_code_start
	AF f_b, *f = &f_b;
	ASI sc;
	if(AF_open(f, o)) {
		char s[256];
		size_t l = strlen(o);
		memcpy(s, o, l + 1);
		s[l-1] = 's';
		ASI *i = ASI_read_script(f, &sc) ? &sc : 0;
		dprintf(1, "disassembling [%s] %s -> %s", inp, o, s);
		if(!i || !ASI_disassemble(f, i, s, flags)) dprintf(1, " FAIL");
		dprintf(1, "\n");
		AF_close(f);
	}
}

static char *filename(const char *dir, const char *fn, char *buf, size_t bsize) {
	snprintf(buf, bsize, "%s/%s", dir, fn);
	return buf;
}

#include "RoomFile.h"
#include <dirent.h>
static int dumprooms(const char* dir, const char* out, int flags) {
	DIR* d = opendir(dir);
	if(!d) return 1;
	int errors = 0;
	struct dirent* di = 0;

	while((di = readdir(d))) {
		size_t l = strlen(di->d_name);
		if(l > 4 + 4 && !memcmp(di->d_name, "room", 4) && !memcmp(di->d_name + l - 4, ".crm", 4)) {
			char fnbuf[512];
			snprintf(fnbuf, sizeof(fnbuf), "%s/%s", dir, di->d_name);
			AF f; ssize_t off; ASI s;
			if(!AF_open(&f, fnbuf)) goto extract_error;
			struct RoomFile rinfo = {0};
			if(!RoomFile_read(&f, &rinfo)) goto extract_error;
			if((off = ARF_find_code_start(&f, 0)) == -1) goto extract_error;
			assert(off == rinfo.blockpos[BLOCKTYPE_COMPSCRIPT3]);
			AF_set_pos(&f, off);
			if(!ASI_read_script(&f, &s)) {
				dprintf(2, "trouble finding script in %s\n", di->d_name);
				continue;
			}
			char buf[256];
			assert(l < sizeof(buf));
			memcpy(buf, di->d_name, l - 4);
			buf[l-4] = '.';
			buf[l-3] = 'o';
			buf[l-2] = 0;
			char outbuf[256];
			AF_dump_chunk(&f, s.start, s.len, filename(out, buf, outbuf, sizeof outbuf));
			disas(di->d_name, outbuf, flags);
			size_t sourcelen;
			char *source = RoomFile_extract_source(&f, &rinfo, &sourcelen);
			if(source) {
				buf[l-3] = 'a';
				buf[l-2] = 's';
				buf[l-1] = 'c';
				buf[l] = 0;
				FILE *f = fopen(filename(out, buf, outbuf, sizeof outbuf), "w");
				if(f) {
					dprintf(1, "extracting room source %s -> %s\n", di->d_name, outbuf);
					fwrite(source, 1, sourcelen, f);
					fclose(f);
				}
				free(source);
			}
			continue;
			extract_error:
			dprintf(2, "warning: extraction of file %s failed\n", di->d_name);
			++errors;
		}
	}
	closedir(d);
	return errors;
}

void dump_script(AF* f, ASI* s, char* fn, int flags) {
	if(!s->len) return;
	AF_dump_chunk(f, s->start, s->len, fn);
	disas("game28.dta", fn, flags);
}

void dump_header(ADF *a, char *fn) {
	unsigned i;
	dprintf(1, "regenerating script header %s\n", fn);
	FILE *f = fopen(fn, "w");
	fprintf(f, "#if SCRIPT_API < 300000 && SCRIPT_API > 262000\n");
	if(ADF_get_cursorcount(a)) fprintf(f, "enum CursorMode {\n");
	for(i=0; i<ADF_get_cursorcount(a); ++i) {
		if(i > 0) fprintf(f, ",\n");
		char buf[16] = {0}, *p = ADF_get_cursorname(a, i), *q = buf;
		while(*p) {
			if(!isspace(*p)) *(q++) = *p;
			++p;
		}
		fprintf(f, "  eMode%s = %u", buf, i);
	}
	if(i) fprintf(f, "};\n");
	fprintf(f, "import Character character[%zu];\n", ADF_get_charactercount(a));
	for(i=0; i<ADF_get_charactercount(a); ++i) {
		char buf[64], *p = buf, *n = ADF_get_characterscriptname(a, i);
		*(p++) = 'c';
		*(p++) = toupper(*(n++));
		while(*n) *(p++) = tolower(*(n++));
		*p = 0;
		fprintf(f, "import Character %s;\n", buf);
	}
	fprintf(f, "#endif\n");
	for(i=0; i<ADF_get_charactercount(a); ++i)
		fprintf(f, "#define %s %zu\n", ADF_get_characterscriptname(a, i), (size_t)i);
	for(i=0; i<ADF_get_guicount(a); ++i) {
		char buf[64], *p = ADF_get_guiname(a, i), *q = buf;
		while(*p) *(q++) = toupper(*(p++));
		*q = 0;
		fprintf(f, "#define %s FindGUIID(\"%s\")\n", buf, ADF_get_guiname(a, i));
	}

	fclose(f);
}

static void dump_old_dialogscripts(ADF *a, char *dir) {
	if(!a->old_dialogscripts) return;
	size_t i, n =a->game.dialogcount;
	for(i=0; i<n; ++i) {
		char fnbuf[512];
		snprintf(fnbuf, sizeof(fnbuf), "%s/dialogscript%03d.ads", dir, (int)i);
		dprintf(1, "extracting dialogscript source %s\n", fnbuf);
		FILE *f = fopen(fnbuf, "w");
		if(!f) {
			perror("fopen");
			continue;
		}
		fprintf(f, "%s", a->old_dialogscripts[i]);
		fclose(f);
	}
}

int main(int argc, char**argv) {
	int flags = 0, c;
	while ((c = getopt(argc, argv, "oblf")) != EOF) switch(c) {
		case 'o': flags |= DISAS_DEBUG_OFFSETS; break;
		case 'b': flags |= DISAS_DEBUG_BYTECODE; break;
		case 'l': flags |= DISAS_SKIP_LINENO; break;
		case 'f': flags |= DISAS_DEBUG_FIXUPS; break;
		default: return usage(argv[0]);
	}
	if(!argv[optind]) return usage(argv[0]);
	char *dir = argv[optind];
	char *out = argv[optind+1];
	if(!out) out = ".";
	else mkdir(out, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);

	int errors = 0;
	ADF a_b, *a = &a_b;
	char fnbuf[512];
	if(!ADF_find_datafile(dir, fnbuf, sizeof(fnbuf)))
		return 1;
	if(!ADF_open(a, fnbuf)) return 1;
	ASI* s;
	s = ADF_get_global_script(a);
	char buf[256];
	dump_script(a->f, s, filename(out, "globalscript.o", buf, sizeof buf), flags);
	s = ADF_get_dialog_script(a);
	dump_script(a->f, s, filename(out, "dialogscript.o", buf, sizeof buf), flags);
	size_t i, l = ADF_get_scriptcount(a);
	for(i = 0; i < l; i++) {
		char fnbuf[32];
		s = ADF_get_script(a, i);
		snprintf(fnbuf, sizeof(fnbuf), "gamescript%zu.o", i);
		dump_script(a->f, s, filename(out, fnbuf, buf, sizeof buf), flags);
	}

	dump_header(a, filename(out, "builtinscriptheader.ash", buf, sizeof buf));
	dump_old_dialogscripts(a, out);
	ADF_close(a);
	errors += dumprooms(dir, out, flags);
	if(errors) dprintf(2, "agscriptxtract: got %d errors\n", errors);

	return !!errors;
}
