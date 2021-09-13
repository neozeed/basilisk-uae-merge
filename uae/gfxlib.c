 /*
  * UAE - The Un*x Amiga Emulator
  *
  * graphics.library emulation
  *
  * Copyright 1996 Bernd Schmidt
  * 
  * Ideas for this:
  * Rewrite layers completely. When there are lots of windows on the screen
  * it can take 3 minutes to update everything after resizing or moving one
  * (at least with Kick 1.3). Hide the internal structure of the layers as far
  * as possible, keep most of the data in emulator space so we save copying/
  * conversion time. Programs really shouldn't do anything directly with the
  * Layer or ClipRect structures.
  * This means that a lot of graphics.library functions will have to be
  * rewritten as well. Of course there's the problem that we can't Wait() in 
  * the emulator currently, so we can't lock layers reliably. Big problem.
  * Once that's done, add support for non-planar bitmaps. Conveniently, the
  * struct Bitmap has an unused pad field which we could abuse as some sort of
  * type field. Need to add chunky<->planar conversion routines to get it
  * going, plus variants of all the drawing functions for speed reasons.
  * 
  * When it becomes necessary to convert a structure from Amiga memory, make
  * a function with a name ending in ..FA, which takes a pointer to the
  * native structure and a CPTR and returns the native pointer.
  */

#include "sysconfig.h"
#include "sysdeps.h"

#include <assert.h>

#include "config.h"
#include "options.h"
#include "memory.h"
#include "custom.h"
#include "readcpu.h"
#include "newcpu.h"
#include "xwin.h"
#include "autoconf.h"
#include "osemu.h"

static ULONG gfxlibname, layerslibname;

struct Rectangle {
    int MinX, MinY, MaxX, MaxY;
};

static int GFX_PointInRectangle(CPTR rect, int x, int y)
{
    WORD minx = get_word(rect);
    WORD miny = get_word(rect+2);
    WORD maxx = get_word(rect+4);
    WORD maxy = get_word(rect+6);
    
    if (x < minx || x > maxx || y < miny || y > maxy)
	return 0;
    return 1;
}

static int GFX_RectContainsRect(struct Rectangle *r1, struct Rectangle *r2)
{
    return (r2->MinX >= r1->MinX && r2->MaxX <= r1->MaxX
	    && r2->MinY >= r1->MinY && r2->MaxY <= r1->MaxY);
}

static struct Rectangle *GFX_RectFA(struct Rectangle *rp, CPTR rect)
{
    rp->MinX = (WORD)get_word(rect);
    rp->MinY = (WORD)get_word(rect+2);
    rp->MaxX = (WORD)get_word(rect+4);
    rp->MaxY = (WORD)get_word(rect+6);
    return rp;
}

static int GFX_Bitmap_WritePixel(CPTR bitmap, int x, int y, CPTR rp)
{
    int i, offs;
    unsigned int bpr = get_word (bitmap);
    unsigned int rows = get_word (bitmap + 2);
    UWORD mask;

    UBYTE planemask = get_byte(rp + 24);
    UBYTE fgpen = get_byte(rp + 25);
    UBYTE bgpen = get_byte(rp + 26);
    UBYTE drmd = get_byte(rp + 28);
    UBYTE pen = drmd & 4 ? bgpen : fgpen;

    if (x < 0 || y < 0 || x >= 8*bpr || y >= rows)
	return -1;
    
    offs = y*bpr + (x & ~15)/8;

    for (i = 0; i < get_byte (bitmap + 5); i++) {
	CPTR planeptr;
	UWORD data;

	if ((planemask & (1 << i)) == 0)
	    continue;

	planeptr = get_long(bitmap + 8 + i*4);
	data = get_word(planeptr + offs);
	
	mask = 0x8000 >> (x & 15);
	
	if (drmd & 2) {
	    if ((pen & (1 << i)) != 0)
		data ^=mask;
	} else {
	    data &= ~mask;
	    if ((pen & (1 << i)) != 0)
		data |= mask;
 	}
	put_word(planeptr + offs, data);
    }
    return 0;
}

int GFX_WritePixel(CPTR rp, int x, int y)
{
    CPTR layer = get_long(rp);
    CPTR bitmap = get_long(rp + 4);
    CPTR cliprect;
    int x2, y2;

    if (bitmap == 0) {
	fprintf(stderr, "bogus RastPort in WritePixel\n");
	return -1;
    }

    /* Easy case first */
    if (layer == 0) {
	return GFX_Bitmap_WritePixel(bitmap, x, y, rp);
    }

    /*
     * Now, in theory we ought to obtain the semaphore.
     * Since we don't, the programs will happily write into the raster
     * even though we are currently moving the window around.
     * Not good.
     */
    
    x2 = x + (WORD)get_word(layer + 16);
    y2 = y + (WORD)get_word(layer + 18);
    
    if (!GFX_PointInRectangle (layer + 16, x2, y2))
	return -1;
    /* Find the right ClipRect */
    cliprect = get_long(layer + 8);
    while (cliprect != 0 && !GFX_PointInRectangle (cliprect + 16, x2, y2))
	cliprect = get_long(cliprect);
    if (cliprect == 0) {
	/* Don't complain: The "Dots" demo does this all the time. I
	 * suppose if we can't find a ClipRect, we aren't supposed to draw
	 * the dot.
	 */
	/*fprintf(stderr, "Weirdness in WritePixel\n");*/
	return -1;
    }
    if (get_long(cliprect + 8) == 0)
	return GFX_Bitmap_WritePixel(bitmap, x2, y2, rp);

    /* Now come the cases where I don't really know what to do... */
    if (get_long(cliprect + 12) == 0)
	return 0;
    
    return GFX_Bitmap_WritePixel (get_long(cliprect + 12), x2 - (WORD)get_word(cliprect + 16), 
				  y2 - (WORD)get_word(cliprect + 18), rp);
}


static ULONG gfxl_WritePixel(void) { return GFX_WritePixel(m68k_areg(regs, 1), (WORD)m68k_dreg(regs, 0), (WORD)m68k_dreg(regs, 1)); }

static ULONG gfxl_BltClear(void)
{
    CPTR mem=m68k_areg(regs, 1);
    UBYTE *mptr = chipmem_bank.xlateaddr(m68k_areg(regs, 1));
    ULONG count=m68k_dreg(regs, 0);
    ULONG flags=m68k_dreg(regs, 1);
    unsigned int i;
    ULONG pattern;

    if ((flags & 2) == 2){
	/* count is given in Rows / Bytes per row */
	count=(count & 0xFFFF) * (count >> 16);
    }

    if ((mem & 1) != 0 || (count & 1) != 0)
	fprintf(stderr, "gfx: BltClear called with odd parameters\n");
    
    /* Bit 2 set means use pattern (V36+ only, but we might as well emulate
     * it always) */
    if ((flags & 4) == 0)
	pattern = 0;
    else
	pattern= ((flags >> 16) & 0xFFFF) | (flags & 0xFFFF0000ul);

    if ((pattern & 0xFF) == ((pattern >> 8) & 0xFF)) {
	memset(mptr, pattern, count);
	return 0;
    }

    for(i = 0; i < count; i += 4) 
	chipmem_bank.lput(mem+i, pattern);
    
    if ((count & 3) != 0)
	chipmem_bank.wput(mem + i - 4, pattern);

    return 0;
}  

static ULONG gfxl_BltBitmap(void)
{
    CPTR srcbitmap = m68k_areg(regs, 0), dstbitmap = m68k_areg(regs, 1);
    int srcx = (WORD)m68k_dreg(regs, 0), srcy = (WORD)m68k_dreg(regs, 1);
    int dstx = (WORD)m68k_dreg(regs, 2), dsty = (WORD)m68k_dreg(regs, 3);
    int sizex = (WORD)m68k_dreg(regs, 4), sizey = (WORD)m68k_dreg(regs, 5);
    UBYTE minterm = (UBYTE)m68k_dreg(regs, 6), mask = m68k_dreg(regs, 7);
    return 0; /* sam: a return was missing here ! */   
}

static CPTR amiga_malloc(int len)
{
    m68k_dreg(regs, 0) = len;
    m68k_dreg(regs, 1) = 1; /* MEMF_PUBLIC */
    return CallLib(get_long(4), -198); /* AllocMem */
}

static void amiga_free(CPTR addr, int len)
{
    m68k_areg(regs, 1) = addr;
    m68k_dreg(regs, 0) = len;
    CallLib(get_long(4), -210); /* FreeMem */
}

/*
 * Region handling code
 * 
 * General ideas stolen from xc/verylongpath/miregion.c
 *
 * The Clear code is untested. And and Or seem to work, Xor is only used
 * by the 1.3 Prefs program and seems to work, too.
 */

struct RegionRectangle {
    struct RegionRectangle *Next,*Prev;
    struct Rectangle bounds;
};

struct Region {
    struct Rectangle bounds;
    struct RegionRectangle *RegionRectangle;
};

struct RectList {
    int count;
    int space;
    struct Rectangle bounds;
    struct Rectangle *rects;
};

struct BandList {
    int count;
    int space;
    int *miny, *maxy;
};

static void init_bandlist(struct BandList *bl)
{
    bl->count = 0;
    bl->space = 20;
    bl->miny = (int *)malloc(20*sizeof(int));
    bl->maxy = (int *)malloc(20*sizeof(int));
}

static __inline__ void add_band(struct BandList *bl, int miny, int maxy, int pos)
{
    if (bl->count == bl->space) {
	bl->space += 20;
	bl->miny = (int *)realloc(bl->miny, bl->space*sizeof(int));	
	bl->maxy = (int *)realloc(bl->maxy, bl->space*sizeof(int));	
    }
    memmove(bl->miny + pos + 1, bl->miny + pos, (bl->count - pos) * sizeof(int));
    memmove(bl->maxy + pos + 1, bl->maxy + pos, (bl->count - pos) * sizeof(int));
    bl->count++;
    bl->miny[pos] = miny;
    bl->maxy[pos] = maxy;
}

static void init_rectlist(struct RectList *rl)
{
    rl->count = 0;
    rl->space = 100;
    rl->bounds.MinX = rl->bounds.MinY = rl->bounds.MaxX = rl->bounds.MaxY = 0;
    rl->rects = (struct Rectangle *)malloc(100*sizeof(struct Rectangle));
}

static __inline__ void add_rect(struct RectList *rl, struct Rectangle r)
{
    if (rl->count == 0)
	rl->bounds = r;
    else {
	if (r.MinX < rl->bounds.MinX)
	    rl->bounds.MinX = r.MinX;
	if (r.MinY < rl->bounds.MinY)
	    rl->bounds.MinY = r.MinY;
	if (r.MaxX > rl->bounds.MaxX)
	    rl->bounds.MaxX = r.MaxX;
	if (r.MaxY > rl->bounds.MaxY)
	    rl->bounds.MaxY = r.MaxY;
    }
    if (rl->count == rl->space) {
	rl->space += 100;
	rl->rects = (struct Rectangle *)realloc(rl->rects, rl->space*sizeof(struct Rectangle));	
    }
    rl->rects[rl->count++] = r;
}

static __inline__ void rem_rect(struct RectList *rl, int num)
{
    rl->count--;
    if (num == rl->count)
	return;
    rl->rects[num] = rl->rects[rl->count];
}

static void free_rectlist(struct RectList *rl)
{
    free(rl->rects);
}

static void free_bandlist(struct BandList *bl)
{
    free(bl->miny);
    free(bl->maxy);
}

static int regionrect_cmpfn(const void *a, const void *b)
{
    struct Rectangle *ra = (struct Rectangle *)a;
    struct Rectangle *rb = (struct Rectangle *)b;
    
    if (ra->MinY < rb->MinY)
	return -1;
    if (ra->MinY > rb->MinY)
	return 1;
    if (ra->MinX < rb->MinX)
	return -1;
    if (ra->MinX > rb->MinX)
	return 1;
    if (ra->MaxX < rb->MaxX)
	return -1;
    return 1;
}

static __inline__ int min(int x, int y)
{
    return x < y ? x : y;
}

static __inline__ int max(int x, int y)
{
    return x > y ? x : y;
}

static void region_addbands(struct RectList *rl, struct BandList *bl)
{
    int i,j;

    for (i = 0; i < rl->count; i++) {
	struct Rectangle tmpr = rl->rects[i];

	for (j = 0; j < bl->count; j++) {
	    /* Is the current band before the rectangle? */
	    if (bl->maxy[j] < tmpr.MinY)
		continue;
	    /* Band already present? */
	    if (bl->miny[j] == tmpr.MinY && bl->maxy[j] == tmpr.MaxY)
		break;
	    /* Completely new band? Add it */
	    if (bl->miny[j] > tmpr.MaxY) {
		add_band(bl, tmpr.MinY, tmpr.MaxY, j);
		break;
	    }
	    /* Now we know that the bands are overlapping.
	     * See whether they match in one point */
	    if (bl->miny[j] == tmpr.MinY) {
		int t;
		if (bl->maxy[j] < tmpr.MaxY) {
		    /* Rectangle exceeds band */
		    tmpr.MinY = bl->maxy[j]+1;
		    continue;
		}
		/* Rectangle splits band */
		t = bl->maxy[j];
		bl->maxy[j] = tmpr.MaxY;
		tmpr.MinY = bl->maxy[j] + 1;
		tmpr.MaxY = t;
		continue;
	    } else if (bl->maxy[j] == tmpr.MaxY) {
		int t;
		if (bl->miny[j] > tmpr.MinY) {
		    /* Rectangle exceeds band */
		    t = bl->miny[j];
		    bl->miny[j] = tmpr.MinY;
		    bl->maxy[j] = t-1;
		    tmpr.MinY = t;
		    continue;
		}
		/* Rectangle splits band */
		bl->maxy[j] = tmpr.MinY - 1;
		continue;
	    }
	    /* Bands overlap and match in no points. Get a new band and align */
	    if (bl->miny[j] > tmpr.MinY) {
		/* Rectangle begins before band, so make a new band before
		 * and adjust rectangle */
		add_band(bl, tmpr.MinY, bl->miny[j] - 1, j);
		tmpr.MinY = bl->miny[j+1];
	    } else {
		/* Rectangle begins in band */
		add_band(bl, bl->miny[j], tmpr.MinY - 1, j);
		bl->miny[j+1] = tmpr.MinY;
	    }
	    continue;
	}
	if (j == bl->count)
	    add_band(bl, tmpr.MinY, tmpr.MaxY, j);
    }
}

static void region_splitrects_band(struct RectList *rl, struct BandList *bl)
{
    int i,j;
    for (i = 0; i < rl->count; i++) {
	for (j = 0; j < bl->count; j++) {
	    if (bl->miny[j] == rl->rects[i].MinY && bl->maxy[j] == rl->rects[i].MaxY)
		break;
	    if (rl->rects[i].MinY > bl->maxy[j])
		continue;
	    if (bl->miny[j] == rl->rects[i].MinY) {
		struct Rectangle tmpr;
		tmpr.MinX = rl->rects[i].MinX;
		tmpr.MaxX = rl->rects[i].MaxX;
		tmpr.MinY = bl->maxy[j] + 1;
		tmpr.MaxY = rl->rects[i].MaxY;
		add_rect(rl, tmpr); /* will be processed later */
		rl->rects[i].MaxY = bl->maxy[j];
		break;
	    }
	    fprintf(stderr, "Foo..\n");
	}
    }
    qsort(rl->rects, rl->count, sizeof (struct Rectangle), regionrect_cmpfn);
}

static void region_coalesce_rects(struct RectList *rl, int do_2nd_pass)
{
    int i,j;

    /* First pass: Coalesce horizontally */
    for (i = j = 0; i < rl->count;) {
	int offs = 1;
	while (i + offs < rl->count) {
	    if (rl->rects[i].MinY != rl->rects[i+offs].MinY
		|| rl->rects[i].MaxY != rl->rects[i+offs].MaxY
		|| rl->rects[i].MaxX+1 < rl->rects[i+offs].MinX)
		break;
	    rl->rects[i].MaxX = rl->rects[i+offs].MaxX;
	    offs++;
	}
	rl->rects[j++] = rl->rects[i];
	i += offs;
    }
    rl->count = j;
    
    if (!do_2nd_pass)
	return;
    
    /* Second pass: Coalesce bands */
    for (i = 0; i < rl->count;) {
	int match = 0;
	for (j = i + 1; j < rl->count; j++)
	    if (rl->rects[i].MinY != rl->rects[j].MinY)
		break;
	if (j < rl->count && rl->rects[i].MaxY + 1 == rl->rects[j].MinY) {
	    int k;
	    match = 1;
	    for (k = 0; i+k < j; k++) {
		if (j+k >= rl->count
		    || rl->rects[j+k].MinY != rl->rects[j].MinY)
		{
		    match = 0; break;
		}
		if (rl->rects[i+k].MinX != rl->rects[j+k].MinX
		    || rl->rects[i+k].MaxX != rl->rects[j+k].MaxX)
		{
		    match = 0;
		    break;
		}
	    }
	    if (j+k < rl->count && rl->rects[j+k].MinY == rl->rects[j].MinY)
		match = 0;
	    if (match) {
		for (k = 0; i+k < j; k++)
		    rl->rects[i+k].MaxY = rl->rects[j].MaxY;
		memmove(rl->rects + j, rl->rects + j + k, (rl->count - j - k)*sizeof(struct Rectangle));
		rl->count -= k;
	    }
	}
	if (!match)
	    i = j;
    }
}

static int copy_rects (CPTR region, struct RectList *rl)
{
    CPTR regionrect;
    int numrects = 0;
    struct Rectangle b;
    regionrect = get_long(region+8);
    b.MinX = get_word(region);
    b.MinY = get_word(region+2);
    b.MaxX = get_word(region+4);
    b.MaxY = get_word(region+6);
    
    while (regionrect != 0) {
	struct Rectangle tmpr;
	
	tmpr.MinX = (WORD)get_word(regionrect+8)  + b.MinX;
	tmpr.MinY = (WORD)get_word(regionrect+10) + b.MinY;
	tmpr.MaxX = (WORD)get_word(regionrect+12) + b.MinX;
	tmpr.MaxY = (WORD)get_word(regionrect+14) + b.MinY;
	add_rect(rl, tmpr);
	regionrect = get_long(regionrect);
	numrects++;
    }
    return numrects;
}

typedef void (*regionop)(struct RectList *,struct RectList *,struct RectList *);

static void region_do_ClearRegionRegion(struct RectList *rl1,struct RectList *rl2,
					struct RectList *rl3)
{
    int i,j;

    for (i = j = 0; i < rl2->count && j < rl1->count;) {
	struct Rectangle tmpr;

	while ((rl1->rects[j].MinY < rl2->rects[i].MinY
		|| (rl1->rects[j].MinY == rl2->rects[i].MinY
		    && rl1->rects[j].MaxX < rl2->rects[i].MinX))
	       && j < rl1->count)
	    j++;
	if (j >= rl1->count)
	    break;
	while ((rl1->rects[j].MinY > rl2->rects[i].MinY
		|| (rl1->rects[j].MinY == rl2->rects[i].MinY
		    && rl1->rects[j].MinX > rl2->rects[i].MaxX))
	       && i < rl2->count)
	{
	    add_rect(rl3, rl2->rects[i]);
	    i++;
	}
	if (i >= rl2->count)
	    break;
	
	tmpr = rl2->rects[i];
	
	while (i < rl2->count && j < rl1->count
	       && rl1->rects[j].MinY == tmpr.MinY
	       && rl2->rects[i].MinY == tmpr.MinY
	       && rl1->rects[j].MinX <= rl2->rects[i].MaxX
	       && rl1->rects[j].MaxX >= rl2->rects[i].MinX)
	{
	    int oldmin = tmpr.MinX;
	    int oldmax = tmpr.MaxX;
	    if (tmpr.MinX < rl1->rects[j].MinX) {
		tmpr.MaxX = rl1->rects[j].MinX - 1;
		add_rect(rl3, tmpr);
	    }
	    if (oldmax <= rl1->rects[j].MaxX) {
		i++;
		if (i < rl2->count && rl2->rects[i].MinY == tmpr.MinY)
		    tmpr = rl2->rects[i];
	    } else {
		tmpr.MinX = rl1->rects[j].MaxX + 1;
		tmpr.MaxX = oldmax;
		j++;
	    }
	}
    }
    for(; i < rl2->count; i++)
	add_rect(rl3, rl2->rects[i]);
}

static void region_do_AndRegionRegion(struct RectList *rl1,struct RectList *rl2,
				      struct RectList *rl3)
{
    int i,j;

    for (i = j = 0; i < rl2->count && j < rl1->count;) {
	while ((rl1->rects[j].MinY < rl2->rects[i].MinY
		|| (rl1->rects[j].MinY == rl2->rects[i].MinY
		    && rl1->rects[j].MaxX < rl2->rects[i].MinX))
	       && j < rl1->count)
	    j++;
	if (j >= rl1->count)
	    break;
	while ((rl1->rects[j].MinY > rl2->rects[i].MinY
		|| (rl1->rects[j].MinY == rl2->rects[i].MinY
		    && rl1->rects[j].MinX > rl2->rects[i].MaxX))
	       && i < rl2->count)
	    i++;
	if (i >= rl2->count)
	    break;
	if (rl1->rects[j].MinY == rl2->rects[i].MinY
	    && rl1->rects[j].MinX <= rl2->rects[i].MaxX
	    && rl1->rects[j].MaxX >= rl2->rects[i].MinX)
	{
	    /* We have an intersection! */
	    struct Rectangle tmpr;
	    tmpr = rl2->rects[i];
	    if (tmpr.MinX < rl1->rects[j].MinX)
		tmpr.MinX = rl1->rects[j].MinX;
	    if (tmpr.MaxX > rl1->rects[j].MaxX)
		tmpr.MaxX = rl1->rects[j].MaxX;
	    add_rect(rl3, tmpr);
	    if (rl1->rects[j].MaxX == rl2->rects[i].MaxX)
		i++, j++;
	    else if (rl1->rects[j].MaxX > rl2->rects[i].MaxX)
		i++;
	    else
		j++;
	}
    }
}

static void region_do_OrRegionRegion(struct RectList *rl1,struct RectList *rl2,
				     struct RectList *rl3)
{
    int i,j;

    for (i = j = 0; i < rl2->count && j < rl1->count;) {
	while ((rl1->rects[j].MinY < rl2->rects[i].MinY
		|| (rl1->rects[j].MinY == rl2->rects[i].MinY
		    && rl1->rects[j].MaxX < rl2->rects[i].MinX))
	       && j < rl1->count)
	{
	    add_rect(rl3, rl1->rects[j]);
	    j++;
	}
	if (j >= rl1->count)
	    break;
	while ((rl1->rects[j].MinY > rl2->rects[i].MinY
		|| (rl1->rects[j].MinY == rl2->rects[i].MinY
		    && rl1->rects[j].MinX > rl2->rects[i].MaxX))
	       && i < rl2->count)
	{
	    add_rect(rl3, rl2->rects[i]);
	    i++;
	}
	if (i >= rl2->count)
	    break;
	if (rl1->rects[j].MinY == rl2->rects[i].MinY
	    && rl1->rects[j].MinX <= rl2->rects[i].MaxX
	    && rl1->rects[j].MaxX >= rl2->rects[i].MinX)
	{
	    /* We have an intersection! */
	    struct Rectangle tmpr;
	    tmpr = rl2->rects[i];
	    if (tmpr.MinX > rl1->rects[j].MinX)
		tmpr.MinX = rl1->rects[j].MinX;
	    if (tmpr.MaxX < rl1->rects[j].MaxX)
		tmpr.MaxX = rl1->rects[j].MaxX;
	    i++; j++;
	    for (;;) {
		int cont = 0;
		if (j < rl1->count && rl1->rects[j].MinY == tmpr.MinY
		    && tmpr.MaxX+1 >= rl1->rects[j].MinX) {
		    if (tmpr.MaxX < rl1->rects[j].MaxX)
			tmpr.MaxX = rl1->rects[j].MaxX;
		    j++; cont = 1;
		}
		if (i < rl2->count && rl2->rects[i].MinY == tmpr.MinY
		    && tmpr.MaxX+1 >= rl2->rects[i].MinX) {
		    if (tmpr.MaxX < rl2->rects[i].MaxX)
			tmpr.MaxX = rl2->rects[i].MaxX;
		    i++; cont = 1;
		}
		if (!cont)
		    break;
	    }
	    add_rect(rl3, tmpr);
	}
    }
    for(; i < rl2->count; i++)
	add_rect(rl3, rl2->rects[i]);
    for(; j < rl1->count; j++)
	add_rect(rl3, rl1->rects[j]);
}

static void region_do_XorRegionRegion(struct RectList *rl1,struct RectList *rl2,
				      struct RectList *rl3)
{
    int i,j;

    for (i = j = 0; i < rl2->count && j < rl1->count;) {
	struct Rectangle tmpr1, tmpr2;

	while ((rl1->rects[j].MinY < rl2->rects[i].MinY
		|| (rl1->rects[j].MinY == rl2->rects[i].MinY
		    && rl1->rects[j].MaxX < rl2->rects[i].MinX))
	       && j < rl1->count)
	{
	    add_rect(rl3, rl1->rects[j]);
	    j++;
	}
	if (j >= rl1->count)
	    break;
	while ((rl1->rects[j].MinY > rl2->rects[i].MinY
		|| (rl1->rects[j].MinY == rl2->rects[i].MinY
		    && rl1->rects[j].MinX > rl2->rects[i].MaxX))
	       && i < rl2->count)
	{
	    add_rect(rl3, rl2->rects[i]);
	    i++;
	}
	if (i >= rl2->count)
	    break;

	tmpr2 = rl2->rects[i];
	tmpr1 = rl1->rects[j];
	
	while (i < rl2->count && j < rl1->count
	       && rl1->rects[j].MinY == tmpr1.MinY
	       && rl2->rects[i].MinY == tmpr1.MinY
	       && rl1->rects[j].MinX <= rl2->rects[i].MaxX
	       && rl1->rects[j].MaxX >= rl2->rects[i].MinX)
	{
	    int oldmin2 = tmpr2.MinX;
	    int oldmax2 = tmpr2.MaxX;
	    int oldmin1 = tmpr1.MinX;
	    int oldmax1 = tmpr1.MaxX;
	    int need_1 = 0, need_2 = 0;

	    if (tmpr2.MinX > tmpr1.MinX	&& tmpr2.MaxX < tmpr1.MaxX) 
	    {
		/*
		 *    ###########
		 *       ****
		 */
		tmpr1.MaxX = tmpr2.MinX - 1;
		add_rect(rl3, tmpr1);
		tmpr1.MaxX = oldmax1;
		tmpr1.MinX = tmpr2.MaxX + 1;
		add_rect(rl3, tmpr1);
		need_2 = 1;
	    } else if (tmpr2.MinX > tmpr1.MinX && tmpr2.MaxX > tmpr1.MaxX) {
		/*
		 *    ##########
		 *       *********
		 */
		tmpr1.MaxX = tmpr2.MinX - 1;
		add_rect(rl3, tmpr1);
		tmpr2.MinX = oldmax1 + 1;
		add_rect(rl3, tmpr2);
		need_1 = 1;
	    } else if (tmpr2.MinX < tmpr1.MinX && tmpr2.MaxX < tmpr1.MaxX) {
		/*
		 *       ##########
		 *    *********
		 */
		tmpr2.MaxX = tmpr1.MinX - 1;
		add_rect(rl3, tmpr2);
		tmpr1.MinX = oldmax2 + 1;
		add_rect(rl3, tmpr1);
		need_2 = 1;
	    } else if (tmpr2.MinX < tmpr1.MinX && tmpr2.MaxX > tmpr1.MaxX) {
		/*
		 *       ###
		 *    *********
		 */
		tmpr2.MaxX = tmpr1.MinX - 1;
		add_rect(rl3, tmpr2);
		tmpr2.MaxX = oldmax2;
		tmpr2.MinX = tmpr1.MaxX + 1;
		add_rect(rl3, tmpr2);
		need_1 = 1;
	    } else if (tmpr1.MinX == tmpr2.MinX && tmpr2.MaxX < tmpr1.MaxX) {
		/*
		 *    #############
		 *    *********
		 */
		tmpr1.MinX = tmpr2.MaxX + 1;
		need_2 = 1;
	    } else if (tmpr1.MinX == tmpr2.MinX && tmpr2.MaxX > tmpr1.MaxX) {
		/*
		 *    #########
		 *    *************
		 */
		tmpr2.MinX = tmpr1.MaxX + 1;
		need_1 = 1;
	    } else if (tmpr1.MinX < tmpr2.MinX && tmpr2.MaxX == tmpr1.MaxX) {
		/*
		 *    #############
		 *        *********
		 */
		tmpr1.MaxX = tmpr2.MinX - 1;
		add_rect(rl3, tmpr1);
		need_2 = need_1 = 1;
	    } else if (tmpr1.MinX > tmpr2.MinX && tmpr2.MaxX == tmpr1.MaxX) {
		/*
		 *        #########
		 *    *************
		 */
		tmpr2.MaxX = tmpr1.MinX - 1;
		add_rect(rl3, tmpr2);
		need_2 = need_1 = 1;
	    } else {
		assert(tmpr1.MinX == tmpr2.MinX && tmpr2.MaxX == tmpr1.MaxX);
		need_1 = need_2 = 1;
	    }
	    if (need_1) {
		j++;
		if (j < rl1->count && rl1->rects[j].MinY == tmpr1.MinY)
		    tmpr1 = rl1->rects[j];
	    }
	    if (need_2) {
		i++;
		if (i < rl2->count && rl2->rects[i].MinY == tmpr2.MinY)
		    tmpr2 = rl2->rects[i];
	    }
	}
    }
    for(; i < rl2->count; i++)
	add_rect(rl3, rl2->rects[i]);
    for(; j < rl1->count; j++)
	add_rect(rl3, rl1->rects[j]);
}

static ULONG gfxl_perform_regionop(regionop op, int with_rect)
{
    int i,j,k;
    CPTR reg1;
    CPTR reg2;
    CPTR tmp, rpp;
    struct RectList rl1, rl2, rl3;
    struct BandList bl;

    int retval = 0;
    int numrects2;
    
    init_rectlist(&rl1); init_rectlist(&rl2); init_rectlist(&rl3);

    if (with_rect) {
	struct Rectangle tmpr;
	reg2 = m68k_areg(regs, 0);
	numrects2 = copy_rects(reg2, &rl2);
	tmpr.MinX = get_word(m68k_areg(regs, 1));
	tmpr.MinY = get_word(m68k_areg(regs, 1) + 2);
	tmpr.MaxX = get_word(m68k_areg(regs, 1) + 4);
	tmpr.MaxY = get_word(m68k_areg(regs, 1) + 6);
	add_rect(&rl1, tmpr);
    } else {
	reg1 = m68k_areg(regs, 0);
	reg2 = m68k_areg(regs, 1);

	copy_rects(reg1, &rl1);
	numrects2 = copy_rects(reg2, &rl2);
    }

    init_bandlist(&bl);
    region_addbands(&rl1, &bl);
    region_addbands(&rl2, &bl);
    region_splitrects_band(&rl1, &bl);
    region_splitrects_band(&rl2, &bl);
    region_coalesce_rects(&rl1, 0);
    region_coalesce_rects(&rl2, 0);

    (*op)(&rl1, &rl2, &rl3);
    region_coalesce_rects(&rl3, 1);

    rpp = reg2 + 8;
    if (rl3.count < numrects2) {
	while (numrects2-- != rl3.count) {
	    tmp = get_long(rpp);
	    put_long(rpp, get_long(tmp));
	    amiga_free(tmp, 16);
	}
	if (rl3.count > 0)
	    put_long(get_long(rpp) + 4, rpp);
    } else if (rl3.count > numrects2) {
	while(numrects2++ != rl3.count) {
	    CPTR prev = get_long(rpp);
	    tmp = amiga_malloc(16);
	    if (tmp == 0)
		goto done;
	    put_long(tmp, prev);
	    put_long(tmp + 4, rpp);
	    if (prev != 0)
		put_long(prev + 4, tmp);
	    put_long(rpp, tmp);	    
	}
    }
    
    if (rl3.count > 0) {
	rpp = reg2 + 8;
	for (i = 0; i < rl3.count; i++) {
	    CPTR rr = get_long(rpp);
	    put_word(rr+8, rl3.rects[i].MinX - rl3.bounds.MinX);
	    put_word(rr+10, rl3.rects[i].MinY - rl3.bounds.MinY);
	    put_word(rr+12, rl3.rects[i].MaxX - rl3.bounds.MinX);
	    put_word(rr+14, rl3.rects[i].MaxY - rl3.bounds.MinY);
	    rpp = rr;
	}
	if (get_long(rpp) != 0)
	    fprintf(stderr, "BUG\n");
    } 
    put_word(reg2+0, rl3.bounds.MinX);
    put_word(reg2+2, rl3.bounds.MinY);
    put_word(reg2+4, rl3.bounds.MaxX);
    put_word(reg2+6, rl3.bounds.MaxY);
    retval = 1;

    done:
    free_rectlist(&rl1); free_rectlist(&rl2); free_rectlist(&rl3);
    free_bandlist(&bl);    

    return retval;
}

static ULONG gfxl_AndRegionRegion(void)
{
    return gfxl_perform_regionop(region_do_AndRegionRegion, 0);
}
static ULONG gfxl_XorRegionRegion(void)
{
    return gfxl_perform_regionop(region_do_XorRegionRegion, 0);
}
static ULONG gfxl_OrRegionRegion(void)
{
    return gfxl_perform_regionop(region_do_OrRegionRegion, 0);
}

static ULONG gfxl_ClearRectRegion(void)
{
    return gfxl_perform_regionop(region_do_ClearRegionRegion, 1);
}
static ULONG gfxl_OrRectRegion(void)
{
    return gfxl_perform_regionop(region_do_OrRegionRegion, 1);
}

static ULONG gfxl_AndRectRegion(void)
{
    return gfxl_perform_regionop(region_do_AndRegionRegion, 1);
}

static ULONG gfxl_XorRectRegion(void)
{
    return gfxl_perform_regionop(region_do_XorRegionRegion, 1);
}


/*
 *  Initialization
 */
static ULONG gfxlib_init(void)
{
    ULONG old_arr;
    CPTR gfxbase, layersbase;
    CPTR sysbase=m68k_areg(regs, 6); 
    int i=0;

    /* Install new routines */
    /* We have to call SetFunction here instead of writing direktly into the GfxBase,
     * because of the library checksum ! */

    m68k_dreg(regs, 0)=0;
    m68k_areg(regs, 1)=gfxlibname;
    gfxbase=CallLib(sysbase, -408);  /* OpenLibrary */
    m68k_dreg(regs, 0)=0;
    m68k_areg(regs, 1)=layerslibname;
    layersbase=CallLib(sysbase, -408);  /* OpenLibrary */

    libemu_InstallFunction(gfxl_WritePixel, gfxbase, -324, "");
    libemu_InstallFunction(gfxl_BltClear, gfxbase, -300, "");
    libemu_InstallFunction(gfxl_AndRegionRegion, gfxbase, -624, "");
    libemu_InstallFunction(gfxl_OrRegionRegion, gfxbase, -612, "");
    libemu_InstallFunction(gfxl_XorRegionRegion, gfxbase, -618, "");
    libemu_InstallFunction(gfxl_AndRectRegion, gfxbase, -504, "");
    libemu_InstallFunction(gfxl_OrRectRegion, gfxbase, -510, "");
    libemu_InstallFunction(gfxl_XorRectRegion, gfxbase, -558, "");
    libemu_InstallFunction(gfxl_ClearRectRegion, gfxbase, -522, "");

    libemu_InstallFunction(NULL, layersbase, -60, "MoveLayer");
    libemu_InstallFunction(NULL, layersbase, -96, "LockLayer");
    libemu_InstallFunction(NULL, layersbase, -102, "UnlockLayer");
    libemu_InstallFunction(NULL, layersbase, -108, "LockLayers");
    libemu_InstallFunction(NULL, layersbase, -114, "UnlockLayers");
    libemu_InstallFunction(NULL, layersbase, -120, "LockLayerInfo");
    libemu_InstallFunction(NULL, layersbase, -138, "UnlockLayerInfo");
    libemu_InstallFunction(NULL, layersbase, -144, "NewLayerInfo");
    libemu_InstallFunction(NULL, layersbase, -156, "FattenLayerInfo");
    return 0;
}

/* 
 *  Install the gfx-library-replacement 
 */
void gfxlib_install(void)
{
    ULONG begin, end, resname, resid;
    int i;
    
    if(!use_gfxlib) return;
    
    fprintf(stderr, "Warning: you enabled the graphics.library replacement with -g\n"
	    "This may be buggy right now, and will not speed things up much.\n");

    resname = ds("UAEgfxlib.resource");
    resid = ds("UAE gfxlib 0.1");

    gfxlibname = ds("graphics.library");
    layerslibname = ds("layers.library");

    begin = here();
    dw(0x4AFC);             /* RTC_MATCHWORD */
    dl(begin);              /* our start address */
    dl(0);                  /* Continue scan here */
    dw(0x0101);             /* RTF_COLDSTART; Version 1 */
    dw(0x0805);             /* NT_RESOURCE; pri 5 */
    dl(resname);            /* name */
    dl(resid);              /* ID */
    dl(here() + 4);         /* Init area: directly after this */

    calltrap(deftrap(gfxlib_init)); dw(RTS);

    end = here();
    org(begin + 6);
    dl(end);

    org(end);
}
