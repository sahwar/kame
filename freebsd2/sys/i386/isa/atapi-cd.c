/*-
 * Copyright (c) 1998 S�ren Schmidt
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *	$Id: atapi-cd.c,v 1.4.2.2 1998/11/09 16:09:05 luigi Exp $
 */

#include "wdc.h"
#include "acd.h"
#include "opt_atapi.h"
#include "opt_devfs.h"

#if NACD > 0 && NWDC > 0 && defined(ATAPI)

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/malloc.h>
#include <sys/buf.h>
#include <sys/disklabel.h>
#include <sys/cdio.h>
#include <sys/wormio.h>
#include <sys/fcntl.h>
#include <sys/conf.h>
#include <sys/stat.h>
#ifdef DEVFS
#include <sys/devfsext.h>
#endif
#include <i386/isa/atapi.h>
#include <i386/isa/atapi-cd.h>

static d_open_t		acdopen;
static d_close_t	acdclose;
static d_write_t	acdwrite;
static d_read_t		acdread;
static d_ioctl_t	acdioctl;
static d_strategy_t	acdstrategy;

#define CDEV_MAJOR 69
#define BDEV_MAJOR 19
extern struct cdevsw acd_cdevsw ;
static struct bdevsw acd_bdevsw =
	{ acdopen,	acdclose,	acdstrategy,	acdioctl,
	  nodump,	nopsize,	0, "acd", &acd_cdevsw, -1 };
static struct cdevsw acd_cdevsw = {
    acdopen,	acdclose,	acdread,	acdwrite,	
    acdioctl,	nostop,		nullreset,	nodevtotty,
    seltrue,	nommap,		acdstrategy,	"acd",
    &acd_bdevsw, -1 };

#define NUNIT	16		/* Max # of devices */

#define F_BOPEN         0x0001  /* The block device is opened */
#define F_MEDIA_CHANGED 0x0002  /* The media have changed since open */
#define F_DEBUG         0x0004  /* Print debug info */
#define F_LOCKED        0x0008  /* This unit is locked (or should be) */
#define F_TRACK_PREP    0x0010  /* Track should be prep'ed */
#define F_TRACK_PREPED  0x0020  /* Track has been prep'ed */
#define F_DISK_PREPED   0x0040  /* Disk has been prep'ed */
#define F_WRITTEN   	0x0080  /* The medium has been written to */

static struct acd *acdtab[NUNIT];
static int acdnlun = 0;     	/* Number of configured drives */

#ifndef ATAPI_STATIC
static
#endif
int acdattach(struct atapi *, int, struct atapi_params *, int);
static struct acd *acd_init_lun(struct atapi *, int, struct atapi_params *,int);
static void acd_start(struct acd *);
static void acd_done(struct acd *, struct buf *, int, struct atapires);
static int acd_read_toc(struct acd *);
static int acd_request_wait(struct acd *, u_char, u_char, u_char, u_char, u_char, u_char, u_char, u_char, u_char, u_char, char *, int);
static void acd_describe(struct acd *);
static int acd_open(dev_t, int, int);
static int acd_setchan(struct acd *, u_char, u_char, u_char, u_char);
static int acd_eject(struct acd *, int);
static void acd_select_slot(struct acd *);
static int acd_rezero_unit(struct acd *);
static int acd_open_disk(struct acd *, int);
static int acd_open_track(struct acd *, struct wormio_prepare_track *);
static int acd_close_track(struct acd *);
static int acd_close_disk(struct acd *);
static int acd_read_track_info(struct acd *cdp, int lba, struct acd_track_info *info);
static int acd_blank_disk(struct acd *);
static void atapi_dump(int ctrlr, int lun, char *label, void *data, int len);
static void atapi_error(struct atapi *ata, int unit, struct atapires result);

struct acd *
acd_init_lun(struct atapi *ata, int unit, struct atapi_params *ap, int lun)
{
    struct acd *ptr;

    if (!(ptr = malloc(sizeof(struct acd), M_TEMP, M_NOWAIT)))
        return NULL;
    bzero(ptr, sizeof(struct acd));
    bufq_init(&ptr->buf_queue);
    ptr->ata = ata;
    ptr->unit = unit;
    ptr->lun = lun;
    ptr->param = ap;
    ptr->flags = F_MEDIA_CHANGED;
    ptr->flags &= ~(F_WRITTEN|F_TRACK_PREP|F_TRACK_PREPED);
    ptr->block_size = 2048;
    ptr->starting_lba = 0 ;
    ptr->refcnt = 0;
    ptr->slot = -1;
    ptr->changer_info = NULL;
#ifdef DEVFS
    ptr->ra_devfs_token =
        devfs_add_devswf(&acd_cdevsw, dkmakeminor(lun, 0, 0),
        DV_CHR, UID_ROOT, GID_OPERATOR, 0640,
        "racd%da", lun);
    ptr->rc_devfs_token =
        devfs_add_devswf(&acd_cdevsw, dkmakeminor(lun, 0, RAW_PART),
        DV_CHR, UID_ROOT, GID_OPERATOR, 0640,
        "racd%dc", lun);
    ptr->a_devfs_token =
        devfs_add_devswf(&acd_cdevsw, dkmakeminor(lun, 0, 0),
        DV_BLK, UID_ROOT, GID_OPERATOR, 0640,
        "acd%da", lun);
    ptr->c_devfs_token =
        devfs_add_devswf(&acd_cdevsw, dkmakeminor(lun, 0, RAW_PART),
        DV_BLK, UID_ROOT, GID_OPERATOR, 0640,
        "acd%dc", lun);
#endif
    return ptr;
}

#ifndef ATAPI_STATIC
static
#endif
int
acdattach(struct atapi *ata, int unit, struct atapi_params *ap, int debug)
{
    struct acd *cdp;
    struct atapires result;
    struct changer *chp;
    int i, count;

    if (acdnlun >= NUNIT) {
        printf("acd: too many units\n");
        return 0;
    }
    if (!atapi_request_immediate) {
        printf("acd: configuration error, ATAPI code not present!\n");
        return 0;
    }
    if ((cdp = acd_init_lun(ata, unit, ap, acdnlun)) == NULL) {
        printf("acd: out of memory\n");
        return 0;
    }
    acdtab[acdnlun] = cdp;

    if (debug) {
        cdp->flags |= F_DEBUG;
        atapi_dump(cdp->ata->ctrlr, cdp->lun, "info", ap, sizeof(*ap));
    }

    /* Get drive capabilities, some drives needs this repeated */
    for (count = 0 ; count < 5 ; count++) {
        result = atapi_request_immediate(ata, unit,
        				 ATAPI_MODE_SENSE,
        				 0, ATAPI_CDROM_CAP_PAGE,
        				 0, 0, 0, 0, 
					 sizeof(cdp->cap)>>8, sizeof(cdp->cap),
        				 0, 0, 0, 0, 0, 0, 0, 
					 (char *)&cdp->cap, sizeof(cdp->cap));
        if (result.code == 0 || result.code == RES_UNDERRUN)
            break;
    }

    /* Some drives have shorter capabilities page. */
    if (result.code == RES_UNDERRUN)
        result.code = 0;

    if (result.code == 0) {
    	cdp->cap.max_speed = ntohs(cdp->cap.max_speed);
    	cdp->cap.max_vol_levels = ntohs(cdp->cap.max_vol_levels);
    	cdp->cap.buf_size = ntohs(cdp->cap.buf_size);
    	cdp->cap.cur_speed = ntohs(cdp->cap.cur_speed);
        acd_describe(cdp);
        if (cdp->flags & F_DEBUG)
            atapi_dump(cdp->ata->ctrlr, cdp->lun, "cap", &cdp->cap,
		       sizeof(cdp->cap));
    }
    /* If this is a changer device, allocate the neeeded lun's */
    if (cdp->cap.mech == MST_MECH_CHANGER) {
        chp = malloc(sizeof(struct changer), M_TEMP, M_NOWAIT);
        if (chp == NULL) {
            printf("acd: out of memory\n");
            return 0;
        }
        bzero(chp, sizeof(struct changer));
        result = atapi_request_immediate(ata, unit, ATAPI_MECH_STATUS,
            				 0, 0, 0, 0, 0, 0, 0,
            				 sizeof(struct changer)>>8,
					 sizeof(struct changer),
            				 0, 0, 0, 0, 0, 0,
            				 (char *)chp, sizeof(struct changer));
        if (cdp->flags & F_DEBUG) {
            printf("result.code=%d curr=%02x slots=%d len=%d\n",
                result.code, chp->current_slot, chp->slots,
                htons(chp->table_length));
        }
        if (result.code == RES_UNDERRUN)
            result.code = 0;

        if (result.code == 0) {
            chp->table_length = htons(chp->table_length);
            for (i = 0; i < chp->slots && acdnlun < NUNIT; i++) {
                if (i > 0) {
                    cdp = acd_init_lun(ata, unit, ap, acdnlun);
                    if (cdp == NULL) {
                        printf("acd: out of memory\n");
                        return 0;
                    }
                }
                cdp->slot = i;
                cdp->changer_info = chp;
                printf("acd%d: changer slot %d %s\n", acdnlun, i,
		       (chp->slot[i].present ? "disk present" : "no disk"));
                acdtab[acdnlun++] = cdp;
            }
            if (acdnlun >= NUNIT) {
                printf("acd: too many units\n");
                return 0;
            }
        }
    } else
        acdnlun++;
    return 1;
}

void 
acd_describe(struct acd *cdp)
{
    int comma;
    char *mechanism;

    printf("acd%d: drive speed ", cdp->lun);
    if (cdp->cap.cur_speed != cdp->cap.max_speed)
        printf("%d - ", cdp->cap.cur_speed * 1000 / 1024);
    printf("%dKB/sec", cdp->cap.max_speed * 1000 / 1024);
    if (cdp->cap.buf_size)
        printf(", %dKB cache\n", cdp->cap.buf_size);

    printf("acd%d: supported read  types:", cdp->lun);
    comma = 0;
    if (cdp->cap.read_cdr) {
        printf(" CD-R"); comma = 1;
    }
    if (cdp->cap.read_cdrw) {
        printf("%s CD-RW", comma ? "," : ""); comma = 1;
    }
    if (cdp->cap.cd_da) {
        printf("%s CD-DA", comma ? "," : ""); comma = 1;
    }
    if (cdp->cap.method2)
        printf("%s packet track", comma ? "," : "");
    if (cdp->cap.write_cdr || cdp->cap.write_cdrw) {
    	printf("\nacd%d: supported write types:", cdp->lun);
        comma = 0;
    	if (cdp->cap.write_cdr) {
            printf(" CD-R" ); comma = 1;
	}
    	if (cdp->cap.write_cdrw) {
            printf("%s CD-RW", comma ? "," : ""); comma = 1;
	}
    	if (cdp->cap.test_write) {
            printf("%s test write", comma ? "," : ""); comma = 1;
	}
    }
    if (cdp->cap.audio_play) {
    	printf("\nacd%d: Audio: ", cdp->lun);
    	if (cdp->cap.audio_play)
            printf("play");
    	if (cdp->cap.max_vol_levels)
            printf(", %d volume levels", cdp->cap.max_vol_levels);
    }
    printf("\nacd%d: Mechanism: ", cdp->lun);
    switch (cdp->cap.mech) {
    case MST_MECH_CADDY:
        mechanism = "caddy"; break;
    case MST_MECH_TRAY:
        mechanism = "tray"; break;
    case MST_MECH_POPUP:
        mechanism = "popup"; break;
    case MST_MECH_CHANGER:
        mechanism = "changer"; break;
    case MST_MECH_CARTRIDGE:
        mechanism = "cartridge"; break;
    default:
        mechanism = 0; break;
    }
    if (mechanism)
        printf("%s%s", cdp->cap.eject ? "ejectable " : "", mechanism);
    else if (cdp->cap.eject)
        printf("ejectable");

    if (cdp->cap.mech != MST_MECH_CHANGER) {
        printf("\nacd%d: Medium: ", cdp->lun);
        switch (cdp->cap.medium_type & MST_TYPE_MASK_HIGH) {
        case MST_CDROM:
            printf("CD-ROM "); break;
        case MST_CDR:
            printf("CD-R "); break;
        case MST_CDRW:
            printf("CD-RW "); break;
        case MST_DOOR_OPEN:
            printf("door open"); break;
        case MST_NO_DISC:
            printf("no/blank disc inside"); break;
        case MST_FMT_ERROR:
            printf("medium format error"); break;
	}
        if ((cdp->cap.medium_type & MST_TYPE_MASK_HIGH) < MST_TYPE_MASK_HIGH) {
            switch (cdp->cap.medium_type & MST_TYPE_MASK_LOW) {
            case MST_DATA_120:
                printf("120mm data disc loaded"); break;
            case MST_AUDIO_120:
                printf("120mm audio disc loaded"); break;
            case MST_COMB_120:
                printf("120mm data/audio disc loaded"); break;
            case MST_PHOTO_120:
                printf("120mm photo disc loaded"); break;
            case MST_DATA_80:
                printf("80mm data disc loaded"); break;
            case MST_AUDIO_80:
                printf("80mm audio disc loaded"); break;
            case MST_COMB_80:
                printf("80mm data/audio disc loaded"); break;
            case MST_PHOTO_80:
                printf("80mm photo disc loaded"); break;
            case MST_FMT_NONE:
                switch (cdp->cap.medium_type & MST_TYPE_MASK_HIGH) {
	        case MST_CDROM:
		    printf("unknown medium"); break;
                case MST_CDR:
                case MST_CDRW:
                    printf("blank medium"); break;
	        }
                break;
            default:
                printf("unknown type=0x%x", cdp->cap.medium_type); break;
            }
	}
    }
    if (cdp->cap.lock)
        printf(cdp->cap.locked ? ", locked" : ", unlocked");
    if (cdp->cap.prevent)
        printf(", lock protected");
    printf("\n");
}

static int
acdopen(dev_t dev, int flags, int fmt, struct proc *p)
{
    int lun = dkunit(dev);
    int track = dkslice(dev); /* XXX this is a hack... */
    struct acd *cdp;

    if (lun >= acdnlun || !atapi_request_immediate)
        return ENXIO;
    cdp = acdtab[lun];

    if (!(cdp->flags & F_BOPEN) && !cdp->refcnt) {
        /* Prevent user eject */
        acd_request_wait(cdp, ATAPI_PREVENT_ALLOW,
            0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0);
        cdp->flags |= F_LOCKED;
    }
    if (fmt == S_IFBLK)
        cdp->flags |= F_BOPEN;
    else
        ++cdp->refcnt;

    if ((flags & O_NONBLOCK) == 0) {
        if ((flags & FWRITE) != 0) {
            /* read/write */
            if (acd_rezero_unit(cdp)) {
                printf("acd%d: rezero failed\n", lun);
                return EIO;
            }
        } else {
            /* read only */
            if (acd_read_toc(cdp) < 0) {
                printf("acd%d: read_toc failed\n", lun);
                /* return EIO; */
            }
        }
    }
    cdp->starting_lba = ntohl(cdp->toc.tab[track].addr.lba) ;
    if (track != 0) {
	printf("Warning, opening track %d at %d\n",
		 track, cdp->starting_lba);
    }
    return 0;
}

int 
acdclose(dev_t dev, int flags, int fmt, struct proc *p)
{
    struct acd *cdp = acdtab[dkunit(dev)];

    if (fmt == S_IFBLK)
    	cdp->flags &= ~F_BOPEN;
    else
    	cdp->refcnt = 0; /* only one goes through, right ? */

    /* Are we the last open ?? */
    if (!(cdp->flags & F_BOPEN) && !cdp->refcnt) {
	/* Yup, do we need to close any written tracks */
        if ((flags & FWRITE) != 0) {
            if ((cdp->flags & F_TRACK_PREPED) != 0) {
                acd_close_track(cdp);
                cdp->flags &= ~(F_TRACK_PREPED | F_TRACK_PREP);
            }
        }
	/* Allow the user eject */
        acd_request_wait(cdp, ATAPI_PREVENT_ALLOW,
        		 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    }
    cdp->flags &= ~F_LOCKED;
    return 0;
}

static int
acdread(dev_t dev, struct uio *uio, int ioflag)
{
    struct acd *cdp = acdtab[dkunit(dev)];

    return physio(acdstrategy, NULL, dev, 1, minphys, uio);
}

static int
acdwrite(dev_t dev, struct uio *uio, int ioflag)
{
    struct acd *cdp = acdtab[dkunit(dev)];

    return physio(acdstrategy, NULL, dev, 0, minphys, uio);
}

static void ar_done(struct acd *, struct buf *, int, struct atapires);

static void
ar_done(struct acd *t, struct buf *bp, int resid, struct atapires result)
{              
    struct atapireq *ar = bp->b_driver2;
    if (result.code) {
	atapi_error(t->ata, t->unit, result);
	bp->b_error = EIO ;
	bp->b_flags |= B_ERROR;
    } else {
	bp->b_resid = resid;
	ar->datalen -= resid;
	ar->result = result;
	if ((bp->b_flags & B_READ) == B_WRITE)
	    t->flags |= F_WRITTEN;
    }
    biodone(bp);
}              

static void
acdr_strategy(struct buf *bp)
{
    int lun = dkunit(bp->b_dev);
    struct acd *cdp = acdtab[lun];
    struct atapireq *ar =  bp->b_driver2;
    int count ;
    if (cdp == NULL || ar == NULL) {
	bp->b_error = EINVAL;
	bp->b_flags |= B_ERROR;
	biodone(bp);
	return;
    }
    if (ar->cmd[0] == ATAPI_WRITE_BIG)
	count = -bp->b_bcount ;
    else
	count = bp->b_bcount ;

    atapi_request_callback(cdp->ata, cdp->unit,
	    ar->cmd[0],  ar->cmd[1],  ar->cmd[2],  ar->cmd[3],
	    ar->cmd[4],  ar->cmd[5],  ar->cmd[6],  ar->cmd[7],
	    ar->cmd[8],  ar->cmd[9],  ar->cmd[10], ar->cmd[11],
	    ar->cmd[12], ar->cmd[13], ar->cmd[14], ar->cmd[15],
	    bp->b_un.b_addr, count,
	    (atapi_callback_t *)ar_done, cdp, bp);
}

void 
acdstrategy(struct buf *bp)
{
    int lun = dkunit(bp->b_dev);
    struct acd *cdp = acdtab[lun];
    int x;

#ifdef NOTYET
    /* allow write only on CD-R/RW media */   /* all for now SOS */
    if (!(bp->b_flags & B_READ) && !(writeable_media)) {
        bp->b_error = EROFS;
        bp->b_flags |= B_ERROR;
        biodone(bp);
        return;
    }
#endif

    if (bp->b_bcount == 0) {
        bp->b_resid = 0;
        biodone(bp);
        return;
    }
    
    bp->b_pblkno = bp->b_blkno /* + cdp->starting_lba */ ;
    bp->b_resid = bp->b_bcount;

    x = splbio();
    bufqdisksort(&cdp->buf_queue, bp);
    acd_start(cdp);
    splx(x);
}

static void 
acd_start(struct acd *cdp)
{
    struct buf *bp = bufq_first(&cdp->buf_queue);
    u_long lba, blocks;
    int cmd;
    int count;

    if (!bp)
        return;

    bufq_remove(&cdp->buf_queue, bp);

    /* Should reject all queued entries if media have changed. */
    if (cdp->flags & F_MEDIA_CHANGED) {
        bp->b_error = EIO;
        bp->b_flags |= B_ERROR;
        biodone(bp);
        return;
    }
    acd_select_slot(cdp);

    if ((bp->b_flags & B_READ) == B_WRITE) {
        if ((cdp->flags & F_TRACK_PREPED) == 0) {
            if ((cdp->flags & F_TRACK_PREP) == 0) {
                printf("acd%d: sequence error\n", cdp->lun);
                bp->b_error = EIO;
                bp->b_flags |= B_ERROR;
                biodone(bp);
                return;
            } else {
                if (acd_open_track(cdp, &cdp->preptrack) != 0) {
                    biodone(bp);
                    return;
                }
                cdp->flags |= F_TRACK_PREPED;
            }
        }
    }

    if (bp->b_flags & B_READ)
#ifdef NOTYET
    	lba = bp->b_offset / cdp->block_size;
#else
    	lba = bp->b_blkno / (cdp->block_size / DEV_BSIZE) +
		cdp->starting_lba ;
#endif
    else 
#if 1
	lba = cdp->next_writeable_lba + (bp->b_blkno / cdp->block_size);
#else
	lba = cdp->next_writeable_lba + (bp->b_offset / cdp->block_size);
#endif
    blocks = (bp->b_bcount + (cdp->block_size - 1)) / cdp->block_size;

    if ((bp->b_flags & B_READ) == B_WRITE) {
        cmd = ATAPI_WRITE_BIG;
        count = -bp->b_bcount;
    } else {
        cmd = ATAPI_READ_BIG;
        count = bp->b_bcount;
    }

    atapi_request_callback(cdp->ata, cdp->unit, cmd, 0,
        		   lba>>24, lba>>16, lba>>8, lba, 0, 
			   blocks>>8, blocks, 0, 0, 0, 0, 0, 0, 0, 
			   (u_char *)bp->b_data, count, 
			   (atapi_callback_t *)acd_done, cdp, bp);
}

static void 
acd_done(struct acd *cdp, struct buf *bp, int resid, struct atapires result)
{
    if (result.code) {
        atapi_error(cdp->ata, cdp->unit, result);
        bp->b_error = EIO;
        bp->b_flags |= B_ERROR;
    } else {
        bp->b_resid = resid;
        if ((bp->b_flags & B_READ) == B_WRITE)
            cdp->flags |= F_WRITTEN;
    }
    biodone(bp);
    acd_start(cdp);
}

static int 
acd_request_wait(struct acd *cdp, u_char cmd, u_char a1, u_char a2,
    u_char a3, u_char a4, u_char a5, u_char a6, u_char a7, u_char a8,
    u_char a9, char *addr, int count)
{
    struct atapires result;

    result = atapi_request_wait(cdp->ata, cdp->unit, cmd, a1, a2, a3, a4, a5,
			        a6, a7, a8, a9, 0, 0, 0, 0, 0, 0, addr, count);
    if (result.code) {
        atapi_error(cdp->ata, cdp->unit, result);
        return EIO;
    }
    return 0;
}

static __inline void 
lba2msf(int lba, u_char *m, u_char *s, u_char *f)
{
    lba += 150;
    lba &= 0xffffff;
    *m = lba / (60 * 75);
    lba %= (60 * 75);
    *s = lba / 75;
    *f = lba % 75;
}

static __inline int 
msf2lba(u_char m, u_char s, u_char f)
{
    return (m * 60 + s) * 75 + f - 150;
}

int 
acdioctl(dev_t dev, int cmd, caddr_t addr, int flag, struct proc *p)
{
    int lun = dkunit(dev);
    struct acd *cdp = acdtab[lun];
    int error = 0;

    if (cdp->flags & F_MEDIA_CHANGED)
        switch (cmd) {
        case CDIOCRESET:
            break;
        default:
            acd_read_toc(cdp);
            acd_request_wait(cdp, ATAPI_PREVENT_ALLOW,
            		     0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0);
            cdp->flags |= F_LOCKED;
            break;
        }
    switch (cmd) {
/*
    case CDIOCRESUME:
        bzero(cdb);
        cdb->cmd = ATAPI_PAUSE;
        cdb->b8 = 0x01;
        return atapi_cmd_wait(cdp->ata, cdp->unit, cdb, 0, 0, timout, 0);
*/
    case CDIOCRESUME:
        return acd_request_wait(cdp, ATAPI_PAUSE, 
				0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0);

    case CDIOCPAUSE:
        return acd_request_wait(cdp, ATAPI_PAUSE, 
				0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);

    case CDIOCSTART:
        return acd_request_wait(cdp, ATAPI_START_STOP,
				1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0);

    case CDIOCSTOP:
        return acd_request_wait(cdp, ATAPI_START_STOP,
				1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);

    case CDIOCALLOW:
        acd_select_slot(cdp);
        cdp->flags &= ~F_LOCKED;
        return acd_request_wait(cdp, ATAPI_PREVENT_ALLOW,
            			0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);

    case CDIOCPREVENT:
        acd_select_slot(cdp);
        cdp->flags |= F_LOCKED;
        return acd_request_wait(cdp, ATAPI_PREVENT_ALLOW,
            			0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0);

    case CDIOCRESET:
        if (p->p_cred->pc_ucred->cr_uid)
            return EPERM;
        return acd_request_wait(cdp, ATAPI_TEST_UNIT_READY,
            			0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);

    case CDIOCEJECT:
        if ((cdp->flags & F_BOPEN) && cdp->refcnt)
            return EBUSY;
        return acd_eject(cdp, 0);

    case CDIOCCLOSE:
        if ((cdp->flags & F_BOPEN) && cdp->refcnt)
            return 0;
        return acd_eject(cdp, 1);

    case CDIOREADTOCHEADER:
        if (!cdp->toc.hdr.ending_track)
            return EIO;
        bcopy(&cdp->toc.hdr, addr, sizeof(cdp->toc.hdr));
        break;

    case CDIOREADTOCENTRYS:
	{
            struct ioc_read_toc_entry *te = (struct ioc_read_toc_entry *)addr;
            struct toc *toc = &cdp->toc;
            struct toc buf;
            u_long len;
            u_char starting_track = te->starting_track;

            if (!cdp->toc.hdr.ending_track)
                return EIO;

            if (te->data_len < sizeof(toc->tab[0]) || 
		(te->data_len % sizeof(toc->tab[0])) != 0 || 
		te->address_format != CD_MSF_FORMAT &&
		te->address_format != CD_LBA_FORMAT)
                return EINVAL;

            if (!starting_track)
                starting_track = toc->hdr.starting_track;
            else if (starting_track == 170) 
                starting_track = toc->hdr.ending_track + 1;
            else if (starting_track < toc->hdr.starting_track ||
                     starting_track > toc->hdr.ending_track + 1)
                return EINVAL;

            len = ((toc->hdr.ending_track + 1 - starting_track) + 1) *
		  sizeof(toc->tab[0]);
            if (te->data_len < len)
                len = te->data_len;
            if (len > sizeof(toc->tab))
                return EINVAL;

            if (te->address_format == CD_MSF_FORMAT) {
                struct cd_toc_entry *entry;

                buf = cdp->toc;
                toc = &buf;
                entry = toc->tab + (toc->hdr.ending_track + 1 -
                    	toc->hdr.starting_track) + 1;
                while (--entry >= toc->tab)
                    lba2msf(ntohl(entry->addr.lba), &entry->addr.msf.minute,
                            &entry->addr.msf.second, &entry->addr.msf.frame);
            }
            return copyout(toc->tab + starting_track - toc->hdr.starting_track,
			   te->data, len);
        }

    case CDIOREADTOCENTRY:
	{
            struct ioc_read_toc_single_entry *te =
            	(struct ioc_read_toc_single_entry *)addr;
            struct toc *toc = &cdp->toc;
            struct toc buf;
            u_char track = te->track;

            if (!cdp->toc.hdr.ending_track)
                return EIO;

            if (te->address_format != CD_MSF_FORMAT && 
		te->address_format != CD_LBA_FORMAT)
                return EINVAL;

            if (!track)
                track = toc->hdr.starting_track;
            else if (track == 170)
                track = toc->hdr.ending_track + 1;
            else if (track < toc->hdr.starting_track ||
                     track > toc->hdr.ending_track + 1)
                return EINVAL;

            if (te->address_format == CD_MSF_FORMAT) {
                struct cd_toc_entry *entry;

                buf = cdp->toc;
                toc = &buf;
                entry = toc->tab + (track - toc->hdr.starting_track);
                lba2msf(ntohl(entry->addr.lba), &entry->addr.msf.minute,
                        &entry->addr.msf.second, &entry->addr.msf.frame);
            }
            bcopy(toc->tab + track - toc->hdr.starting_track,
                  &te->entry, sizeof(struct cd_toc_entry));
        }
	break;

    case CDIOCREADSUBCHANNEL:
	{
            struct ioc_read_subchannel *args =
            	(struct ioc_read_subchannel *)addr;
            struct cd_sub_channel_info data;
            u_long len = args->data_len;
            int abslba, rellba;

            if (len > sizeof(data) ||
                len < sizeof(struct cd_sub_channel_header))
                return EINVAL;

            if (acd_request_wait(cdp, ATAPI_READ_SUBCHANNEL,
				 0, 0x40, 1, 0, 0, 0, 
				 sizeof(cdp->subchan)>>8, sizeof(cdp->subchan),
                		 0,
				 (char *)&cdp->subchan, 
				 sizeof(cdp->subchan)) != 0)
                return EIO;
            if (cdp->flags & F_DEBUG)
                atapi_dump(cdp->ata->ctrlr, cdp->lun, "subchan", &cdp->subchan, 
			   sizeof(cdp->subchan));

            abslba = cdp->subchan.abslba;
            rellba = cdp->subchan.rellba;
            if (args->address_format == CD_MSF_FORMAT) {
                lba2msf(ntohl(abslba),
                    &data.what.position.absaddr.msf.minute,
                    &data.what.position.absaddr.msf.second,
                    &data.what.position.absaddr.msf.frame);
                lba2msf(ntohl(rellba),
                    &data.what.position.reladdr.msf.minute,
                    &data.what.position.reladdr.msf.second,
                    &data.what.position.reladdr.msf.frame);
            } else {
                data.what.position.absaddr.lba = abslba;
                data.what.position.reladdr.lba = rellba;
            }
            data.header.audio_status = cdp->subchan.audio_status;
            data.what.position.control = cdp->subchan.control & 0xf;
            data.what.position.addr_type = cdp->subchan.control >> 4;
            data.what.position.track_number = cdp->subchan.track;
            data.what.position.index_number = cdp->subchan.indx;
            return copyout(&data, args->data, len);
        }

    case CDIOCPLAYMSF:
	{
            struct ioc_play_msf *args = (struct ioc_play_msf *)addr;

            return acd_request_wait(cdp, ATAPI_PLAY_MSF, 0, 0,
                		    args->start_m, args->start_s, args->start_f,
                		    args->end_m, args->end_s, args->end_f,
				    0, 0, 0);
        }

    case CDIOCPLAYBLOCKS:
	{
            struct ioc_play_blocks *args = (struct ioc_play_blocks *)addr;

            return acd_request_wait(cdp, ATAPI_PLAY_BIG, 0,
                		    args->blk>>24 & 0xff, args->blk>>16 & 0xff,
                		    args->blk>>8 & 0xff, args->blk & 0xff,
                		    args->len>>24 & 0xff, args->len>>16 & 0xff,
                		    args->len>>8 & 0xff, args->len & 0xff,
				    0, 0);
        }

    case CDIOCPLAYTRACKS:
	{
            struct ioc_play_track *args = (struct ioc_play_track *)addr;
            u_long start, len;
            int t1, t2;

            if (!cdp->toc.hdr.ending_track)
                return EIO;

            if (args->end_track < cdp->toc.hdr.ending_track + 1)
                ++args->end_track;
            if (args->end_track > cdp->toc.hdr.ending_track + 1)
                args->end_track = cdp->toc.hdr.ending_track + 1;
            t1 = args->start_track - cdp->toc.hdr.starting_track;
            t2 = args->end_track - cdp->toc.hdr.starting_track;
            if (t1 < 0 || t2 < 0)
                return EINVAL;
            start = ntohl(cdp->toc.tab[t1].addr.lba);
            len = ntohl(cdp->toc.tab[t2].addr.lba) - start;

            return acd_request_wait(cdp, ATAPI_PLAY_BIG, 0,
                		    start>>24 & 0xff, start>>16 & 0xff,
                		    start>>8 & 0xff, start & 0xff,
                		    len>>24 & 0xff, len>>16 & 0xff,
                		    len>>8 & 0xff, len & 0xff, 0, 0);
        }

    case CDIOCREADAUDIO:
	{
	    struct ioc_read_audio* args = (struct ioc_read_audio*) addr;
	    int lba, frames, result = 0;
	    u_char *buffer, *ubuf = args->buffer;

	    if (!cdp->toc.hdr.ending_track)
		return EIO;
		
	    if ((frames = args->nframes) < 0)
		return EINVAL;

	    if (args->address_format == CD_LBA_FORMAT)
		lba = args->address.lba;
	    else if (args->address_format == CD_MSF_FORMAT)
	        lba = msf2lba(args->address.msf.minute,
			     args->address.msf.second,
			     args->address.msf.frame);
	    else
		return EINVAL;
#ifndef CD_BUFFER_BLOCKS
#define CD_BUFFER_BLOCKS 8
#endif
            if (!(buffer = malloc(CD_BUFFER_BLOCKS * 2352, M_TEMP, M_NOWAIT)))
                return ENOMEM;

            while (frames > 0) {
                u_char blocks;
                int size;

                blocks = (frames>CD_BUFFER_BLOCKS) ? CD_BUFFER_BLOCKS : frames;
                size = blocks * 2352;

                result = acd_request_wait(cdp, ATAPI_READ_CD, 4,
                                          lba>>24, (lba>>16)&0xff,
                                          (lba>>8)&0xff, lba&0xff, 0, 0,
                                          blocks, 0xf0, buffer, size);
                if (result != 0)
                    break;

                result = copyout(buffer, ubuf, size);
                if (result != 0)
                    break;
                    
                ubuf += size;
                frames -= blocks;
                lba += blocks;
            }

            free(buffer, M_TEMP);
            return result;
        }

#if 1
    case CDRIOCATAPIREQ: {
	    struct atapireq *ar, *oar = (struct atapireq *)addr;
	    struct buf *bp;
                
	    if (oar->datalen < 0)
		return (EINVAL);

	    MALLOC(ar, struct atapireq *, sizeof *ar, M_TEMP, M_WAITOK);
	    MALLOC(bp, struct buf *, sizeof *bp, M_TEMP, M_WAITOK);
  
	    bcopy(oar, ar, sizeof *ar);
	    bzero(bp, sizeof *bp);
 
	    bp->b_proc = p;
	    bp->b_dev = dev;
	    bp->b_driver1 = cdp;
	    bp->b_driver2 = ar;
   
	    if (ar->datalen) {
		struct uio auio;
		struct iovec aiov;
        
		aiov.iov_base = ar->databuf;
		aiov.iov_len = ar->datalen;
		auio.uio_iov = &aiov;
		auio.uio_iovcnt = 1;

		auio.uio_offset = 0;
		auio.uio_resid = ar->datalen;
 
		if (ar->cmd[0] == ATAPI_WRITE_BIG ) {
		    if ((cdp->flags & F_TRACK_PREPED) == 0) {
			if ((cdp->flags & F_TRACK_PREP) == 0) {
			    printf("acd%d: sequence error\n", cdp->lun);
			    error = EIO;
			    goto done;
			} else {
			    printf("opening track...\n");
			    error= acd_open_track(cdp, &cdp->preptrack);
			    if (error != 0) {
				printf("open_track failed\n");
				goto done;
			    }
			    cdp->flags |= F_TRACK_PREPED;
			}
		    }
		    auio.uio_rw = UIO_WRITE;
		    bp->b_bcount = - ar->datalen;
		} else {
		    auio.uio_rw = UIO_READ;
		    bp->b_bcount = ar->datalen;
		}
		auio.uio_segflg = UIO_USERSPACE;
		auio.uio_procp = p;
 
		error = physio(acdr_strategy, bp, dev, 
		    auio.uio_rw == UIO_READ ? 1 : 0, minphys, &auio);
	    } else {
		bp->b_flags = B_READ | B_BUSY;
		acdr_strategy(bp);
		error = bp->b_error;
	    }
 
done:
	    bcopy(ar, oar, sizeof *ar);
	    FREE(ar, M_TEMP);
	    FREE(bp, M_TEMP);
	    break;
        }
#endif


    case CDIOCGETVOL:
	{
            struct ioc_vol *arg = (struct ioc_vol *)addr;

            error = acd_request_wait(cdp, ATAPI_MODE_SENSE, 0, CDROM_AUDIO_PAGE,
                		     0, 0, 0, 0, 
				     sizeof(cdp->au)>>8, sizeof(cdp->au), 0,
                		     (char *)&cdp->au, sizeof(cdp->au));
            if (error)
                return error;
            if (cdp->flags & F_DEBUG)
                atapi_dump(cdp->ata->ctrlr, cdp->lun, "au", &cdp->au,
			   sizeof(cdp->au));
            if (cdp->au.page_code != CDROM_AUDIO_PAGE)
                return EIO;
            arg->vol[0] = cdp->au.port[0].volume;
            arg->vol[1] = cdp->au.port[1].volume;
            arg->vol[2] = cdp->au.port[2].volume;
            arg->vol[3] = cdp->au.port[3].volume;
        }
        break;

    case CDIOCSETVOL:
	{
            struct ioc_vol *arg = (struct ioc_vol *)addr;

            error = acd_request_wait(cdp, ATAPI_MODE_SENSE, 0, CDROM_AUDIO_PAGE,
                		     0, 0, 0, 0, 
				     sizeof(cdp->au)>>8, sizeof(cdp->au), 0,
                		     (char *)&cdp->au, sizeof(cdp->au));
            if (error)
                return error;
            if (cdp->flags & F_DEBUG)
                atapi_dump(cdp->ata->ctrlr, cdp->lun, "au", &cdp->au, 
			   sizeof(cdp->au));
            if (cdp->au.page_code != CDROM_AUDIO_PAGE)
                return EIO;

            error = acd_request_wait(cdp, ATAPI_MODE_SENSE, 0, 
				     CDROM_AUDIO_PAGE_MASK, 0, 0, 0, 0, 
				     sizeof(cdp->aumask)>>8,sizeof(cdp->aumask),
				     0,
				     (char *)&cdp->aumask, sizeof(cdp->aumask));
            if (error)
                return error;
            if (cdp->flags & F_DEBUG)
                atapi_dump(cdp->ata->ctrlr, cdp->lun, "mask", &cdp->aumask, 
			   sizeof(cdp->aumask));

            cdp->au.data_length = 0;
            cdp->au.port[0].channels = CHANNEL_0;
            cdp->au.port[1].channels = CHANNEL_1;
            cdp->au.port[0].volume = arg->vol[0] & cdp->aumask.port[0].volume;
            cdp->au.port[1].volume = arg->vol[1] & cdp->aumask.port[1].volume;
            cdp->au.port[2].volume = arg->vol[2] & cdp->aumask.port[2].volume;
            cdp->au.port[3].volume = arg->vol[3] & cdp->aumask.port[3].volume;
            return acd_request_wait(cdp, ATAPI_MODE_SELECT, 0x10,
                		    0, 0, 0, 0, 0, 
				    sizeof(cdp->au)>>8, sizeof(cdp->au),
                		    0, (char *)&cdp->au, -sizeof(cdp->au));
        }

    case CDIOCSETPATCH:
	{
            struct ioc_patch *arg = (struct ioc_patch *)addr;

            return acd_setchan(cdp, arg->patch[0], arg->patch[1],
                	       arg->patch[2], arg->patch[3]);
        }

    case CDIOCSETMONO:
        return acd_setchan(cdp, CHANNEL_0|CHANNEL_1, CHANNEL_0|CHANNEL_1, 0, 0);

    case CDIOCSETSTEREO:
        return acd_setchan(cdp, CHANNEL_0, CHANNEL_1, 0, 0);

    case CDIOCSETMUTE:
        return acd_setchan(cdp, 0, 0, 0, 0);

    case CDIOCSETLEFT:
        return acd_setchan(cdp, CHANNEL_0, CHANNEL_0, 0, 0);

    case CDIOCSETRIGHT:
        return acd_setchan(cdp, CHANNEL_1, CHANNEL_1, 0, 0);

    case CDRIOCNEXTWRITEABLEADDR:
	{
	    struct acd_track_info *track_info;

            track_info = malloc(sizeof(*track_info), M_TEMP, M_NOWAIT);
            if (track_info == NULL)
		return ENOMEM ;

	    if ((error = acd_read_track_info(cdp, 0xff, track_info))) {
		printf("acd_rd_trk_info returns error %d\n", error);
		*(int*)addr = cdp->next_writeable_lba = 0 ; /* XXX */
		break;
	    }
	    if (track_info->nwa_valid == 0) {
		printf("acd_rd_trk_info returns invalid info (maybe blank)\n");
		*(int*)addr = cdp->next_writeable_lba = 0 ; /* XXX */
		free(track_info, M_TEMP);
		return EINVAL;
	    }
	    *(int *)addr = cdp->next_writeable_lba =
		    track_info->next_writeable_addr;
	    free(track_info, M_TEMP);
	}
	break;
 
    case WORMIOCPREPDISK:
        {
            struct wormio_prepare_disk *w = (struct wormio_prepare_disk *)addr;

            if (w->dummy != 0 && w->dummy != 1)
                error = EINVAL;
            else {
                error = acd_open_disk(cdp, w->dummy);
                if (error == 0) {
                    cdp->flags |= F_DISK_PREPED;
                    cdp->dummy = w->dummy;
                    cdp->speed = w->speed;
                }
            }
        }
        break;

    case WORMIOCPREPTRACK:
        {
            struct wormio_prepare_track *w =(struct wormio_prepare_track *)addr;

            if (w->audio != 0 && w->audio != 1)
                error = EINVAL;
            else if (w->audio == 0 && w->preemp)
                error = EINVAL;
            else if ((cdp->flags & F_DISK_PREPED) == 0) {
                error = EINVAL;
                printf("acd%d: sequence error (PREP_TRACK)\n", cdp->lun);
            } else {
		/*
		 * if something has changed, synchronize track.
		 */
		if (cdp->flags & F_TRACK_PREPED &&
			(cdp->preptrack.audio != w->audio ||
			 cdp->preptrack.preemp != w->preemp) ) {
		    error = acd_close_track(cdp);
		    cdp->flags &= ~(F_TRACK_PREPED | F_TRACK_PREP);
		}
                cdp->flags |= F_TRACK_PREP;
                cdp->preptrack = *w;
            }
        }
        break;

    case WORMIOCFINISHTRACK:
        if ((cdp->flags & F_TRACK_PREPED) != 0)
            error = acd_close_track(cdp);
        cdp->flags &= ~(F_TRACK_PREPED | F_TRACK_PREP);
        break;

    case WORMIOCFIXATION:
        {
            struct wormio_fixation *w =
            (struct wormio_fixation *)addr;

            if ((cdp->flags & F_WRITTEN) == 0)
                error = EINVAL;
            else if (w->toc_type < 0 /* WORM_TOC_TYPE_AUDIO */ ||
                w->toc_type > 4 /* WORM_TOC_TYPE_CDI */ )
                error = EINVAL;
            else if (w->onp != 0 && w->onp != 1)
                error = EINVAL;
            else {
                /* no fixation needed if dummy write */
                if (cdp->dummy == 0)
                    error = acd_close_disk(cdp);
                cdp->flags &=
                    ~(F_WRITTEN|F_DISK_PREPED|F_TRACK_PREP|F_TRACK_PREPED);
            }
        }
        break;

    case CDRIOCBLANK:
        return acd_blank_disk(cdp);

    default:
        return ENOTTY;
    }
    return error;
}

static int 
acd_read_toc(struct acd *cdp)
{
    int ntracks, len;
    struct atapires result;

    bzero(&cdp->toc, sizeof(cdp->toc));
    bzero(&cdp->info, sizeof(cdp->info));

    acd_select_slot(cdp);

    result = atapi_request_wait(cdp->ata, cdp->unit, ATAPI_TEST_UNIT_READY,
        			0, 0, 0, 0, 0, 0, 0, 0,
				0, 0, 0, 0, 0, 0, 0, 0, 0);

    if (result.code == RES_ERR &&
        (result.error & AER_SKEY) == AER_SK_UNIT_ATTENTION) {
        cdp->flags |= F_MEDIA_CHANGED;
    	cdp->flags &= ~(F_WRITTEN|F_TRACK_PREP|F_TRACK_PREPED);
        result = atapi_request_wait(cdp->ata, cdp->unit, ATAPI_TEST_UNIT_READY,
				    0, 0, 0, 0, 0, 0, 0, 0,
				    0, 0, 0, 0, 0, 0, 0, 0, 0);
    }

    if (result.code) {
        atapi_error(cdp->ata, cdp->unit, result);
        return EIO;
    }

    cdp->flags &= ~F_MEDIA_CHANGED;

    len = sizeof(struct ioc_toc_header) + sizeof(struct cd_toc_entry);
    if (acd_request_wait(cdp, ATAPI_READ_TOC, 0, 0, 0, 0, 0, 0,
        		 len>>8, len & 0xff, 0, (char *)&cdp->toc, len) != 0) {
        bzero(&cdp->toc, sizeof(cdp->toc));
        return 0;
    }
    ntracks = cdp->toc.hdr.ending_track - cdp->toc.hdr.starting_track + 1;
    if (ntracks <= 0 || ntracks > MAXTRK) {
        bzero(&cdp->toc, sizeof(cdp->toc));
        return 0;
    }

    len = sizeof(struct ioc_toc_header) + ntracks * sizeof(struct cd_toc_entry);
    if (acd_request_wait(cdp, ATAPI_READ_TOC, 0, 0, 0, 0, 0, 0,
        		 len>>8, len & 0xff, 0, (char *)&cdp->toc, len) & 0xff){
        bzero(&cdp->toc, sizeof(cdp->toc));
        return 0;
    }

    cdp->toc.hdr.len = ntohs(cdp->toc.hdr.len);

    if (acd_request_wait(cdp, ATAPI_READ_CAPACITY, 0, 0, 0, 0, 0, 0, 0, 0, 0,
			 (char *)&cdp->info, sizeof(cdp->info)) != 0)
        bzero(&cdp->info, sizeof(cdp->info));

    cdp->toc.tab[ntracks].control = cdp->toc.tab[ntracks - 1].control;
    cdp->toc.tab[ntracks].addr_type = cdp->toc.tab[ntracks - 1].addr_type;
    cdp->toc.tab[ntracks].track = 170;
    cdp->toc.tab[ntracks].addr.lba = cdp->info.volsize;

    cdp->info.volsize = ntohl(cdp->info.volsize);
    cdp->info.blksize = ntohl(cdp->info.blksize);

    if (cdp->info.volsize && cdp->toc.hdr.ending_track
        && (cdp->flags & F_DEBUG)) {
        printf("acd%d: ", cdp->lun);
        if (cdp->toc.tab[0].control & 4)
            printf("%ldMB ", cdp->info.volsize / 512);
        else
            printf("%ld:%ld audio ", cdp->info.volsize / 75 / 60,
                cdp->info.volsize / 75 % 60);
        printf("(%ld sectors (%d bytes)), %d tracks\n", 
	    cdp->info.volsize, cdp->info.blksize,
            cdp->toc.hdr.ending_track - cdp->toc.hdr.starting_track + 1);
    }
    return 0;
}

/*
 * Set up the audio channel masks.
 */
static int 
acd_setchan(struct acd *cdp, u_char c0, u_char c1, u_char c2, u_char c3)
{
    int error;

    error = acd_request_wait(cdp, ATAPI_MODE_SENSE, 0, CDROM_AUDIO_PAGE,
        		     0, 0, 0, 0, 
			     sizeof(cdp->au)>>8, sizeof(cdp->au), 0,
        		     (char *)&cdp->au, sizeof(cdp->au));
    if (error)
        return error;
    if (cdp->flags & F_DEBUG)
        atapi_dump(cdp->ata->ctrlr, cdp->lun, "au", &cdp->au, sizeof(cdp->au));
    if (cdp->au.page_code != CDROM_AUDIO_PAGE)
        return EIO;

    cdp->au.data_length = 0;
    cdp->au.port[0].channels = c0;
    cdp->au.port[1].channels = c1;
    cdp->au.port[2].channels = c2;
    cdp->au.port[3].channels = c3;
    return acd_request_wait(cdp, ATAPI_MODE_SELECT, 0x10,
        		    0, 0, 0, 0, 0, 
			    sizeof(cdp->au)>>8, sizeof(cdp->au), 0,
			    (char *)&cdp->au, -sizeof(cdp->au));
}

static int 
acd_eject(struct acd *cdp, int close)
{
    struct atapires result;

    acd_select_slot(cdp);

    result = atapi_request_wait(cdp->ata, cdp->unit, ATAPI_START_STOP, 1,
			        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);

    if (result.code == RES_ERR &&
        ((result.error & AER_SKEY) == AER_SK_NOT_READY ||
        (result.error & AER_SKEY) == AER_SK_UNIT_ATTENTION)) {
        int err;

        if (!close)
            return 0;
        err = acd_request_wait(cdp, ATAPI_START_STOP, 0, 0, 0, 3,
			       0, 0, 0, 0, 0, 0, 0);
        if (err)
            return err;

        acd_read_toc(cdp);

        acd_request_wait(cdp, ATAPI_PREVENT_ALLOW, 0, 0, 0, 1,
			 0, 0, 0, 0, 0, 0, 0);
        cdp->flags |= F_LOCKED;
        return 0;
    }
    if (result.code) {
        atapi_error(cdp->ata, cdp->unit, result);
        return EIO;
    }
    if (close)
        return 0;

    tsleep((caddr_t) &lbolt, PRIBIO, "acdej1", 0);
    tsleep((caddr_t) &lbolt, PRIBIO, "acdej2", 0);

    acd_request_wait(cdp, ATAPI_PREVENT_ALLOW, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    cdp->flags &= ~F_LOCKED;

    cdp->flags |= F_MEDIA_CHANGED;
    cdp->flags &= ~(F_WRITTEN|F_TRACK_PREP|F_TRACK_PREPED);
    return acd_request_wait(cdp, ATAPI_START_STOP, 0, 0, 0, 2,
			    0, 0, 0, 0, 0, 0, 0);
}

static void
acd_select_slot(struct acd *cdp)
{
    if (cdp->slot < 0 || cdp->changer_info->current_slot == cdp->slot)
        return;

    /* Unlock (might not be needed but its cheaper than asking) */
    acd_request_wait(cdp, ATAPI_PREVENT_ALLOW, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);

    /* Unload the current media from player */
    acd_request_wait(cdp, ATAPI_LOAD_UNLOAD, 0, 0, 0, 2,
		     0, 0, 0, cdp->changer_info->current_slot, 0, 0, 0);

    /* load the wanted slot */
    acd_request_wait(cdp, ATAPI_LOAD_UNLOAD, 0, 0, 0, 3,
		     0, 0, 0, cdp->slot, 0, 0, 0);

    cdp->changer_info->current_slot = cdp->slot;

    /* Lock the media if needed */
    if (cdp->flags & F_LOCKED) {
        acd_request_wait(cdp, ATAPI_PREVENT_ALLOW, 0, 0, 0, 1,
			 0, 0, 0, 0, 0, 0, 0);
    }
}

static int
acd_rezero_unit(struct acd *cdp)
{
    return acd_request_wait(cdp, ATAPI_REZERO_UNIT, 0, 0, 0, 0,
			    0, 0, 0, 0, 0, 0, 0);
}

static int
acd_open_disk(struct acd *cdp, int test)
{
    cdp->next_writeable_lba = 0;
    return 0;
}

static int
acd_close_disk(struct acd *cdp)
{
    return acd_request_wait(cdp, ATAPI_CLOSE_TRACK, 0x00,
        		    0x02, 0, 0, 0/*track*/, 0, 0, 0, 0, 0, 0);
}

static int
acd_open_track(struct acd *cdp, struct wormio_prepare_track *ptp)
{
    struct write_param *param; /* cannot be in stack */
    struct atapires result;
    int error;

    if (!(param = malloc(sizeof(struct write_param), M_TEMP, M_WAITOK)))
	return ENOMEM ;
    bzero(param, sizeof(*param));

	/* XXX this was wait */
    result = atapi_request_wait(cdp->ata, cdp->unit, ATAPI_MODE_SENSE,
        			0, 0x05, 0, 0, 0, 0, 
				sizeof(*param)>>8, sizeof(*param),
        			0, 0, 0, 0, 0, 0, 0,
        			(char *)param, sizeof(*param));

    if (cdp->flags & F_DEBUG)
        atapi_dump(cdp->ata->ctrlr, cdp->lun, "0x05", param, sizeof(*param));
    if (result.code)
	printf("result: code 0x%x status 0x%x err 0x%x\n",
		result.code, result.status, result.error);

    if (result.code == RES_UNDERRUN)
        result.code = 0;

    if (result.code) {
        atapi_error(cdp->ata, cdp->unit, result);
        return EIO;
    }
    bzero(param, sizeof(*param)); /* XXX */
    param->page_code = 0x05;
    param->page_length = 0x32;
    param->test_write = cdp->dummy ? 1 : 0;
    param->write_type = CDR_WTYPE_TRACK; /* 01 */

    switch (ptp->audio) {

    case 0: /* CDR_DATA */
	cdp->block_size = 2048;
    	param->track_mode = CDR_TMODE_DATA;
    	param->data_block_type = CDR_DB_ROM_MODE1;
    	param->session_format = CDR_SESS_CDROM;
	break;

    default: /*  CDR_AUDIO */
	cdp->block_size = 2352;
	if (ptp->preemp)
    	    param->track_mode = CDR_TMODE_AUDIO;
	else
    	    param->track_mode = CDR_TMODE_AUDIO /* XXX 0 */;
    	param->data_block_type = CDR_DB_RAW;
    	param->session_format = CDR_SESS_CDROM;
	break;

#if 0
    case CDR_MODE2:
    	param->track_mode = CDR_TMODE_DATA;
    	param->data_block_type = CDR_DB_ROM_MODE2;
    	param->session_format = CDR_SESS_CDROM;
	break;

    case CDR_XA1:
    	param->track_mode = CDR_TMODE_DATA;
    	param->data_block_type = CDR_DB_XA_MODE1;
    	param->session_format = CDR_SESS_CDROM_XA;
	break;

    case CDR_XA2:
    	param->track_mode = CDR_TMODE_DATA;
    	param->data_block_type = CDR_DB_XA_MODE2_F1;
    	param->session_format = CDR_SESS_CDROM_XA;
	break;

    case CDR_CDI:
    	param->track_mode = CDR_TMODE_DATA;
    	param->data_block_type = CDR_DB_XA_MODE2_F1;
    	param->session_format = CDR_SESS_CDI;
	break;
#endif
    }

    param->multi_session = CDR_MSES_NONE;
    param->fp = 0;

    param->packet_size = htonl(0);
    param->audio_pause_length = htons(150); /* default value */

    if (cdp->flags & F_DEBUG)
        atapi_dump(cdp->ata->ctrlr, cdp->lun, "0x05", param, sizeof(*param));

	/* XXX this was wait! */
    result = atapi_request_wait(cdp->ata, cdp->unit, ATAPI_MODE_SELECT,
        			0x10, 0, 0, 0, 0, 0, 
				sizeof(*param)>>8, sizeof(*param),
        			0, 0, 0, 0, 0, 0, 0,
        			(char *)param, -sizeof(*param));

    free(param, M_TEMP);
    if (result.code == RES_UNDERRUN)
        result.code = 0;

    if (result.code) {
        atapi_error(cdp->ata, cdp->unit, result);
        return EIO;
    }
    return 0;
}

static int
acd_close_track(struct acd *cdp)
{
    return acd_request_wait(cdp, ATAPI_SYNCHRONIZE_CACHE, 0,
        		    0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
}

static int
acd_read_track_info(struct acd *cdp, int lba, struct acd_track_info *info)
{
    int error = 0;
    error = acd_request_wait(cdp, ATAPI_READ_TRACK_INFO, 0x01,
                             lba>>24, (lba>>16)&0xff,
                             (lba>>8)&0xff, lba&0xff,
		 	     0, 
			     sizeof(*info)>>8, sizeof(*info), 0,
			     (char *)info, sizeof(*info));
    if (error)
	return error;
    info->track_start_addr = ntohl(info->track_start_addr);
    info->next_writeable_addr = ntohl(info->next_writeable_addr);
    info->free_blocks = ntohl(info->free_blocks);
    info->fixed_packet_size = ntohl(info->fixed_packet_size);
    info->track_length = ntohl(info->track_length);
    return 0;
}

static int
acd_blank_disk(struct acd *cdp)
{
    int error;

    error = acd_request_wait(cdp, 0xa1, 0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    cdp->flags |= F_MEDIA_CHANGED;
    cdp->flags &= ~(F_WRITTEN|F_TRACK_PREP|F_TRACK_PREPED);
    return error;
}

static void
atapi_error(struct atapi *ata, int unit, struct atapires result)
{
    if (result.code != RES_ERR) {
        printf("atapi%d:%d: ERROR %d, status=%b, error=%b\n", 
	       ata->ctrlr, unit, result.code, result.status, 
	       ARS_BITS, result.error, AER_BITS);
        return;
    }
    switch (result.error & AER_SKEY) {
    case AER_SK_NOT_READY:
        if (ata->debug)
            printf("atapi%d:%d: not ready\n", ata->ctrlr, unit);
        break;

    case AER_SK_BLANK_CHECK:
        if (ata->debug)
            printf("atapi%d:%d: blank check\n", ata->ctrlr, unit);
        break;

    case AER_SK_MEDIUM_ERROR:
        if (ata->debug)
            printf("atapi%d:%d: medium error\n", ata->ctrlr, unit);
        break;

    case AER_SK_HARDWARE_ERROR:
        if (ata->debug)
            printf("atapi%d:%d: hardware error\n", ata->ctrlr, unit);
        break;

    case AER_SK_ILLEGAL_REQUEST:
        if (ata->debug)
            printf("atapi%d:%d: illegal request\n", ata->ctrlr, unit);
        break;

    case AER_SK_UNIT_ATTENTION:
        if (ata->debug)
            printf("atapi%d:%d: unit attention\n", ata->ctrlr, unit);
        break;

    case AER_SK_DATA_PROTECT:
        if (ata->debug)
            printf("atapi%d:%d: reading protected data\n", ata->ctrlr, unit);
        break;

    case AER_SK_ABORTED_COMMAND:
        if (ata->debug)
            printf("atapi%d:%d: command aborted\n", ata->ctrlr, unit);
        break;

    case AER_SK_MISCOMPARE:
        if (ata->debug)
            printf("atapi%d:%d: data don't match medium\n", ata->ctrlr, unit);
        break;

    default:
        if (ata->debug)
            printf("atapi%d:%d: unknown error, status=%b, error=%b\n", 
	       	   ata->ctrlr, unit, result.status, ARS_BITS, 
	           result.error, AER_BITS);
    }
}

static void 
atapi_dump(int ctrlr, int lun, char *label, void *data, int len)
{
	u_char *p = data;

	printf ("atapi%d%d: %s %x", ctrlr, lun, label, *p++);
	while (--len > 0) printf ("-%x", *p++);
	printf ("\n");
}

#ifdef ACD_MODULE
#include <sys/exec.h>
#include <sys/sysent.h>
#include <sys/lkm.h>

MOD_DEV(acd, LM_DT_BLOCK, BDEV_MAJOR, &acd_bdevsw);
MOD_DEV(racd, LM_DT_CHAR, CDEV_MAJOR, &acd_cdevsw);

int 
acd_load(struct lkm_table *lkmtp, int cmd)
{
    struct atapi *ata;
    int n, u;

    if (!atapi_start)
        return EPROTONOSUPPORT;
    n = 0;
    for (ata = atapi_tab; ata < atapi_tab + 2; ++ata)
        if (ata->port)
            for (u = 0; u < 2; ++u)
                if (ata->params[u] && !ata->attached[u] &&
                    acdattach(ata, u, ata->params[u],
                    ata->debug) >= 0) {
                    ata->attached[u] = 1;
                    ++n;
                }
    if (!n)
        return ENXIO;
    return 0;
}

int 
acd_unload(struct lkm_table *lkmtp, int cmd)
{
    struct acd **cdpp;

    for (cdpp = acdtab; cdpp < acdtab + acdnlun; ++cdpp)
        if (((*cdpp)->flags & F_BOPEN) || (*cdpp)->refcnt)
            return EBUSY;
    for (cdpp = acdtab; cdpp < acdtab + acdnlun; ++t) {
        (*cdpp)->ata->attached[(*cdpp)->unit] = 0;
        free(*cdpp, M_TEMP);
    }
    acdnlun = 0;
    bzero(acdtab, sizeof(acdtab));
    return 0;
}

int 
acd_mod(struct lkm_table *lkmtp, int cmd, int ver)
{
    int err = 0;

    if (ver != LKM_VERSION)
        return EINVAL;

    if (cmd == LKM_E_LOAD)
        err = acd_load(lkmtp, cmd);
    else if (cmd == LKM_E_UNLOAD)
        err = acd_unload(lkmtp, cmd);
    if (err)
        return err;

    lkmtp->private.lkm_dev = &MOD_PRIVATE(racd);
    err = lkmdispatch(lkmtp, cmd);
    if (err)
        return err;

    lkmtp->private.lkm_dev = &MOD_PRIVATE(acd);
    return lkmdispatch(lkmtp, cmd);
}

#endif /* ACD_MODULE */

static acd_devsw_installed = 0;

static void 
acd_drvinit(void *unused)
{
    dev_t dev;

    if (!acd_devsw_installed) {
	dev = makedev(CDEV_MAJOR, 0);
	cdevsw_add(&dev,&acd_cdevsw, NULL);
	dev = makedev(BDEV_MAJOR, 0);
	bdevsw_add(&dev,&acd_bdevsw, NULL);
        acd_devsw_installed = 1;
    }
}

SYSINIT(acddev, SI_SUB_DRIVERS, SI_ORDER_MIDDLE + CDEV_MAJOR, acd_drvinit, NULL)
#endif /* NACD && NWDC && ATAPI */
