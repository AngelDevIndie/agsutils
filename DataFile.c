#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "DataFile.h"
#include "Script.h"

void ADF_close(ADF* a) {
	AF_close(a->f);
}

ASI *ADF_get_global_script(ADF* a) {
	return &a->globalscript;
}

ASI *ADF_get_dialog_script(ADF* a) {
	return &a->dialogscript;
}

ASI *ADF_get_script(ADF* a, size_t index) {
	if(index >= a->scriptcount) return 0;
	return &a->scripts[index];
}

size_t ADF_get_scriptcount(ADF* a) {
	return a->scriptcount;
}

typedef enum interaction_type {
	it_char = 0,
	it_inventory,
	it_max
} interaction_type;

static int ADF_read_interaction(ADF *a, interaction_type t) {
	/* deserialize_interaction_scripts */
	static const size_t iter_start[it_max] = {
		[it_char] = 0,
		[it_inventory] = 1,
	};
	size_t *countmap[it_max] = { 
		[it_char] = &a->game.charactercount,
		[it_inventory] = &a->game.inventorycount,
	}, l = *countmap[t], i = iter_start[t];
	char buf[200];
	for(; i < l; i++) {
		size_t j = 0, evcnt = AF_read_uint(a->f);
		for(; j < evcnt; j++) {
			if(!AF_read_string(a->f, buf, 200)) return 0; /* function names of interaction scripts */
		}
	}
	return 1;
}

static int deserialize_command_list(ADF *a) {
	size_t l = AF_read_uint(a->f);
	AF_read_uint(a->f); /*timesrun*/
	size_t i;
	char childs[1024];
	assert(l < sizeof(childs));
	for(i = 0; i < l; i++) {
		AF_read_uint(a->f); // unused
		AF_read_uint(a->f); // type
		size_t j = 0;
		for(; j < 5; j++) {
			char buf[4];
			if(1 != AF_read(a->f, buf, 1)) // valType
				return 0;
			if(3 != AF_read(a->f, buf, 3)) // padding
				return 0;
			AF_read_uint(a->f); // val
			AF_read_uint(a->f); // extra
		}
		childs[i] = !!AF_read_uint(a->f); // children
		AF_read_uint(a->f); // parent
	}
	for(i = 0; i < l; i++) {
		if(childs[i]) deserialize_command_list(a);
	}
	return 1;
}

static int ADF_read_interaction2x(ADF *a, interaction_type t) {
	/* deserialize_new_interaction */
	size_t *countmap[it_max] = { 
		[it_char] = &a->game.charactercount,
		[it_inventory] = &a->game.inventorycount,
	}, l = *countmap[t], i = 0;
	for(; i < l; i++) {
		int response[32];
		if(AF_read_uint(a->f) != 1) continue;
		size_t evcnt = AF_read_uint(a->f);
		assert(evcnt <= 30);
		AF_read_junk(a->f, evcnt * sizeof(int)); /*event types */
		size_t j = 0;
		for(; j < evcnt; j++)
			response[j] = AF_read_int(a->f);
		for(j = 0; j < evcnt; j++)
			if(response[j]) {
				if(!deserialize_command_list(a))
					return 0;
			}
	}
	return 1;
}

static int ADF_read_gamebase(ADF *a) {
	/* acroom.h: 2881. void ReadFile() */
	size_t l;
	unsigned x;
	char game_name[52];
	l = 50 /* game name */ + 2 /*padding*/;
	if(!AF_read(a->f, game_name, l)) return 0;
	/* 100 options */
	if(!AF_read_junk(a->f, 100*4)) return 0;
	l = 256; /* 256 "paluses", unsigned char*/
	if(!AF_read_junk(a->f, l)) return 0;
	l = 256 * 4; /*sizeof color, read into defpal*/
	if(!AF_read_junk(a->f, l)) return 0;
	a->game.viewcount = AF_read_uint(a->f);
	a->game.charactercount = AF_read_uint(a->f);
	AF_read_uint(a->f); /* character of player*/
	AF_read_uint(a->f); /* totalscore*/
	a->game.inventorycount = AF_read_ushort(a->f);
	AF_read_ushort(a->f); /* padding */
	a->game.dialogcount = AF_read_uint(a->f);
	AF_read_uint(a->f); /* numdlgmessage*/
	a->game.fontcount = AF_read_uint(a->f);
	a->game.color_depth = AF_read_uint(a->f); /* color depth - offset 0x710 into game28.dta */
	AF_read_uint(a->f); /* target_win */
	AF_read_uint(a->f);/* dialog_bullet */
	AF_read_ushort(a->f);/* hotdot */
	AF_read_ushort(a->f);/* hotdotouter */
	AF_read_uint(a->f);/* uniqueid */
	AF_read_uint(a->f);/* numgui */
	a->game.cursorcount = AF_read_uint(a->f);
	x = AF_read_uint(a->f);/* default_resolution */
	if(a->version >= 43 /* 3.3.0 */ && x == 8 /* custom resolution */) {
		/* 2 ints with the custom width and height */
		if(!AF_read_junk(a->f, 8)) return 0;
	}
	AF_read_uint(a->f);/* default_lipsync_frame */
	AF_read_uint(a->f);/* invhotdotsprite */
	l = 4 * 17; /* reserved */
	if(!AF_read_junk(a->f, l)) return 0;
	l = 500 * 4 /* 500 global message numbers */;
	if(!AF_read_junk(a->f, l)) return 0;
	a->game.hasdict = !!AF_read_int(a->f);/* dict */
	AF_read_uint(a->f);/* globalscript */
	AF_read_uint(a->f);/* chars */
	AF_read_uint(a->f);/* compiled_script */
	return 1;
}

static int ADF_read_dictionary(ADF *a) {
	/* acroom.h:1547 */
	size_t i,l = AF_read_uint(a->f);
	for(i = 0; i < l; i++) {
		/* length of encrypted string */
		size_t e = AF_read_uint(a->f);
		if(!AF_read_junk(a->f, e)) return 0;
		AF_read_short(a->f); /* wordnum */
	}
	return 1;
}


static int ADF_read_view(ADF *a) {
	(void) a;
	return 1;
}

static int ADF_read_view2x(ADF *a) {
	(void) a;
	return 1;
}

int ADF_find_datafile(const char *dir, char *fnbuf, size_t flen)
{
	size_t l = strlen(dir);
	if(l >= flen - 20) return 0;
	memcpy(fnbuf, dir, l);
	char* p = fnbuf + l;
	*p = '/'; p++;
	memcpy(p, "game28.dta", sizeof("game28.dta"));
	AF f;
	if(!AF_open(&f, fnbuf)) {
		memcpy(p, "ac2game.dta", sizeof("ac2game.dta"));
		if(!AF_open(&f, fnbuf)) return 0;
	}
	AF_close(&f);
	return 1;
}

int ADF_read_cursors(ADF* a) {
	a->cursornames = malloc(a->game.cursorcount * sizeof(char*));
	if(!a->cursornames) return 0;

	unsigned i;
	for(i=0; i<a->game.cursorcount; ++i) {
		char buf[24];
		if(24 != AF_read(a->f, buf, 24)) return 0;
		assert(buf[19] == 0);
		a->cursornames[i] = strdup(buf+10);
	}
	return 1;
}

int ADF_open(ADF* a, const char *filename) {
	char fnbuf[512];
	size_t l, i;

	memset(a, 0, sizeof(*a));
	a->f = &a->f_b;

	if(!AF_open(a->f, filename)) return 0;

	if(30 != AF_read(a->f, fnbuf, 30)) {
		err_close:
		AF_close(a->f);
		return 0;
	}
	if(memcmp("Adventure Creator Game File v2", fnbuf, 30)) goto err_close;
	a->version = AF_read_int(a->f);
	/* here comes some version string with minor, major - we dont need it */
	/* FIXME? newer ags does this only with version >= 12 */
	/* 4 bytes containing the length of the string, followed by the string */
	l = AF_read_uint(a->f);
	if(l > 20) goto err_close;
	if(l != (size_t) AF_read(a->f, fnbuf, l)) goto err_close;

	/* main_game_file.cpp:OpenMainGameFileBase */
	if(a->version >= 48 /* kGameVersion_341 */) {
		/* latest ags now has placed an int here: number of required capabilities */
		l = AF_read_uint(a->f);
		/* followed by l int/string pairs */
		for(i=0; i<l; i++) {
			size_t len = AF_read_uint(a->f);
			if(!AF_read_junk(a->f, len)) goto err_close;
		}
	}
	if(!ADF_read_gamebase(a)) goto err_close;
	if(a->version > 32) {
		l = 40 /* guid */ + 20 /*savegame extension*/ + 50 /*savegame dir*/;
		if(l != (size_t) AF_read(a->f, fnbuf, l)) goto err_close;
	}
	if(a->version < 50 /* 3.5.0 */) {
		l = a->game.fontcount * 2; /* fontflags and fontoutline arrays [each 1b/font] */
		if(!AF_read_junk(a->f, l)) goto err_close;
		if(a->version >= 48) {
			/* version == 3.4.1 have YOffset and versions >= 3.4.1.2 < 3.5.0 have YOffeset and LineSpacing ints */
			l = a->game.fontcount * 4;
			if(a->version == 49) l*=2;
			if(!AF_read_junk(a->f, l)) goto err_close;
		}
	} else {
		/* 3.5.0 has 5 ints per font, see gamesetupstruct.cpp */
		l = a->game.fontcount * 4 * 5;
		if(!AF_read_junk(a->f, l)) goto err_close;
	}
	// number of sprites
	l = a->numsprites = (a->version < 24) ? 6000 : AF_read_uint(a->f);
	a->spriteflagsstart = AF_get_pos(a->f);
	// array of spriteflags (1 char each, max: MAX_SPRITES==30000)
	if(!AF_read_junk(a->f, l)) goto err_close;
	l = 68 * a->game.inventorycount; /* sizeof(InventoryItemInfo) */
	if(!AF_read_junk(a->f, l)) goto err_close;

	if(!ADF_read_cursors(a)) goto err_close;

	if(a->version > 32) {
		if(!ADF_read_interaction(a, it_char)) goto err_close;
		if(!ADF_read_interaction(a, it_inventory)) goto err_close;
	} else {
		if(!ADF_read_interaction2x(a, it_char)) goto err_close;
		if(!ADF_read_interaction2x(a, it_inventory)) goto err_close;
		a->globalvarcount = AF_read_uint(a->f);
		l = 28 /* sizeof(InteractionVariable)*/ * a->globalvarcount;
		if(!AF_read_junk(a->f, l)) goto err_close;
	}
	if(a->game.hasdict)
		if(!ADF_read_dictionary(a)) goto err_close;
	a->scriptstart = AF_get_pos(a->f);
	if(!ASI_read_script(a->f, &a->globalscript)) goto err_close;
	if(a->version > 37)
		if(!ASI_read_script(a->f, &a->dialogscript)) goto err_close;
	if(a->version >= 31) {
		a->scriptcount = AF_read_uint(a->f);
		for(l = 0; l < a->scriptcount; l++)
			if(!ASI_read_script(a->f, &a->scripts[l])) goto err_close;
	}
	a->scriptend = AF_get_pos(a->f);
	/* at this point we have everything we need */
	return 1;
	
	if(a->version > 31) {
		for(l = 0; l < a->game.viewcount; l++)
			if(!ADF_read_view(a)) goto err_close;
	} else {
		for(l = 0; l < a->game.viewcount; l++)
			if(!ADF_read_view2x(a)) goto err_close;
	}
	if(a->version <= 19) {
		/* skip junk */
		l = AF_read_uint(a->f) * 0x204;
		if(!AF_read_junk(a->f, l)) goto err_close;
	}
	/* ... we are around line 11977 in ac.cpp at this point*/
	return 1;
}


