/*$$$!!Warning: Huawei key information asset. No spread without permission.$$$*/
/*CODEMARK:HuE1v2Elv7J9C1aMkGmdiUQZoS/R2sDdIoxXqdBKL9eiPHH/FdSvQYZfBQZKkVdipTcRXX+G
kqk+/W4lTjU7wqFxjZf0LDwCjpr43YYwLpANSkN3SEmzpLd2djjy+EZczQecyEEWxqdmWeqB
7bvhSgt6bEJHeCmBL1fJByUqQMheY0+8TIRL2m7rBZMokwrVbwSAzwa/Zwd5+jictMixP/AG
0htKGU83Lo3cJX2rXg0IAyusQ79vPtqY+qlR1X9EqOFV0Ifi/LR5jOc5nNbmkA==#*/
/*$$$!!Warning: Deleting or modifying the preceding information is prohibited.$$$*/

/*
 * MPEG-1/2 decoder
 * Copyright (c) 2000,2001 Fabrice Bellard
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file libavcodec/mpeg12.c
 * MPEG-1/2 decoder
 */

//#define DEBUG

#include "avcodec.h"
#include "dsputil.h"
#include "mpegvideo.h"
#include "internal.h"
#include "mpeg12.h"
#include "mpeg12data.h"
#include "mpeg12decdata.h"
#include "bytestream.h"
#include "vdpau_internal.h"
#include "xvmc_internal.h"

/*xiongfei ���Ӵ�ͷ�ļ� xiongfei20100212*/
#include "mpegvideo_parser.h"
/*������ͷ�ļ� xiongfei20100303*/
#include "iMedia_error.h"

#define MV_VLC_BITS 9
#define MBINCR_VLC_BITS 9
#define MB_PAT_VLC_BITS 9
#define MB_PTYPE_VLC_BITS 6
#define MB_BTYPE_VLC_BITS 6

#define MPEG2_MIN_WIDTH  32
#define MPEG2_MAX_WIDTH  1920
#define MPEG2_MIN_HEIGHT 16
#define MPEG2_MAX_HEIGHT 1152

static inline int mpeg1_decode_block_inter(MpegEncContext *s,
        DCTELEM *block,
        int n);
static inline int mpeg1_fast_decode_block_inter(MpegEncContext *s, DCTELEM *block, int n);
static inline int mpeg2_decode_block_non_intra(MpegEncContext *s,
        DCTELEM *block,
        int n);
static inline int mpeg2_decode_block_intra(MpegEncContext *s,
        DCTELEM *block,
        int n);
static inline int mpeg2_fast_decode_block_non_intra(MpegEncContext *s, DCTELEM *block, int n);
static inline int mpeg2_fast_decode_block_intra(MpegEncContext *s, DCTELEM *block, int n);
static int mpeg_decode_motion(MpegEncContext *s, int fcode, int pred);
static void exchange_uv(MpegEncContext *s);

static const enum PixelFormat pixfmt_xvmc_mpg2_420[] =
    {
        PIX_FMT_XVMC_MPEG2_IDCT,
        PIX_FMT_XVMC_MPEG2_MC,
        PIX_FMT_NONE
    };

uint8_t ff_mpeg12_static_rl_table_store[2][2][2 * MAX_RUN + MAX_LEVEL + 3];


#define INIT_2D_VLC_RL(rl, static_size)\
    {\
        static RL_VLC_ELEM rl_vlc_table[static_size];\
        INIT_VLC_STATIC(&rl.vlc, TEX_VLC_BITS, rl.n + 2,\
                        &rl.table_vlc[0][1], 4, 2,\
                        &rl.table_vlc[0][0], 4, 2, static_size);\
        \
        rl.rl_vlc[0]= rl_vlc_table;\
        init_2d_vlc_rl(&rl);\
    }

int GreatComDiv(int data1, int data2)
{
    int temp;
    int rem;
    int m, n;
    int remain;

    n = data1;
    m = data2;

    if (n < m)
    {
        temp = n;
        n = m;
        m = temp;
    }

    while (0 != m)
    {
        remain = m;
        rem = n % m;
        n = m;
        m = rem;
    }

    return remain;
}

void mpeg12_get_sar(MpegEncContext *s2, uint16_t *sample_height, uint16_t *sample_width)
{
    int remain, width, height;

    switch ( s2->aspect_ratio_info)
    {
        case 0x01:
            *sample_height = 1;
            *sample_width = 1;
            break;

        case 0x02:
        {
            if ((0 != s2->iDisplayHeight) && (0 != s2->iDisplayWeight))
            {
                width = s2->iDisplayWeight;
                height = s2->iDisplayHeight;
            }
            else
            {
                width = s2->width;
                height = s2->height;
            }

            height *= 4;
            width *= 3;
            remain = GreatComDiv(height, width);
            width /= remain;
            height /= remain;
            *sample_height = width;
            *sample_width = height;
        }
        break;

        case 0x03:
        {
            int width, height;

            if ((0 != s2->iDisplayHeight) && (0 != s2->iDisplayWeight))
            {
                width = s2->iDisplayWeight;
                height = s2->iDisplayHeight;
            }
            else
            {
                width = s2->width;
                height = s2->height;
            }

            height *= 16;
            width *= 9;
            remain = GreatComDiv(height, width);
            width /= remain;
            height /= remain;
            *sample_height = width;
            *sample_width = height;
        }
        break;

        case 0x04:
        {
            int width, height;

            if ((0 != s2->iDisplayHeight) && (0 != s2->iDisplayWeight))
            {
                width = s2->iDisplayWeight;
                height = s2->iDisplayHeight;
            }
            else
            {
                width = s2->width;
                height = s2->height;
            }

            height *= 221;
            width *= 100;
            remain = GreatComDiv(height, width);
            width /= remain;
            height /= remain;
            *sample_height = width;
            *sample_width = height;
        }
        break;

        default:
            *sample_height = 1;
            *sample_width = 1;
            break;
    }
}


static void init_2d_vlc_rl(RLTable *rl)
{
    int i;

    for (i = 0; i < rl->vlc.table_size; i++)
    {
        int code = rl->vlc.table[i][0];
        int len = rl->vlc.table[i][1];
        int level, run;

        if (len == 0) // illegal code
        {
            run = 65;
            level = MAX_LEVEL;
        }
        else if (len < 0) //more bits needed
        {
            run = 0;
            level = code;
        }
        else
        {
            if (code == rl->n) //esc
            {
                run = 65;
                level = 0;
            }
            else if (code == rl->n + 1) //eob
            {
                run = 0;
                level = 127;
            }
            else
            {
                run =   rl->table_run  [code] + 1;
                level = rl->table_level[code];
            }
        }

        rl->rl_vlc[0][i].len = len;
        rl->rl_vlc[0][i].level = level;
        rl->rl_vlc[0][i].run = run;
    }
}

void ff_mpeg12_common_init(MpegEncContext *s)
{

    s->y_dc_scale_table =
        s->c_dc_scale_table = mpeg2_dc_scale_table[s->intra_dc_precision];

}

void ff_mpeg1_clean_buffers(MpegEncContext *s)
{
    s->last_dc[0] = 1 << (7 + s->intra_dc_precision);
    s->last_dc[1] = s->last_dc[0];
    s->last_dc[2] = s->last_dc[0];
    memset(s->last_mv, 0, sizeof(s->last_mv));
}


/******************************************/
/* decoding */

static VLC mv_vlc;
static VLC mbincr_vlc;
static VLC mb_ptype_vlc;
static VLC mb_btype_vlc;
static VLC mb_pat_vlc;

av_cold void ff_mpeg12_init_vlcs(void)
{
    static int done = 0;

    if (!done)
    {
        done = 1;

        INIT_VLC_STATIC(&dc_lum_vlc, DC_VLC_BITS, 12,
                        ff_mpeg12_vlc_dc_lum_bits, 1, 1,
                        ff_mpeg12_vlc_dc_lum_code, 2, 2, 512);
        INIT_VLC_STATIC(&dc_chroma_vlc,  DC_VLC_BITS, 12,
                        ff_mpeg12_vlc_dc_chroma_bits, 1, 1,
                        ff_mpeg12_vlc_dc_chroma_code, 2, 2, 514);
        INIT_VLC_STATIC(&mv_vlc, MV_VLC_BITS, 17,
                        &ff_mpeg12_mbMotionVectorTable[0][1], 2, 1,
                        &ff_mpeg12_mbMotionVectorTable[0][0], 2, 1, 518);
        INIT_VLC_STATIC(&mbincr_vlc, MBINCR_VLC_BITS, 36,
                        &ff_mpeg12_mbAddrIncrTable[0][1], 2, 1,
                        &ff_mpeg12_mbAddrIncrTable[0][0], 2, 1, 538);
        INIT_VLC_STATIC(&mb_pat_vlc, MB_PAT_VLC_BITS, 64,
                        &ff_mpeg12_mbPatTable[0][1], 2, 1,
                        &ff_mpeg12_mbPatTable[0][0], 2, 1, 512);

        INIT_VLC_STATIC(&mb_ptype_vlc, MB_PTYPE_VLC_BITS, 7,
                        &table_mb_ptype[0][1], 2, 1,
                        &table_mb_ptype[0][0], 2, 1, 64);
        INIT_VLC_STATIC(&mb_btype_vlc, MB_BTYPE_VLC_BITS, 11,
                        &table_mb_btype[0][1], 2, 1,
                        &table_mb_btype[0][0], 2, 1, 64);
        init_rl(&ff_rl_mpeg1, ff_mpeg12_static_rl_table_store[0]);
        init_rl(&ff_rl_mpeg2, ff_mpeg12_static_rl_table_store[1]);

        INIT_2D_VLC_RL(ff_rl_mpeg1, 680);
        INIT_2D_VLC_RL(ff_rl_mpeg2, 674);
    }
}

static inline int get_dmv(MpegEncContext *s)
{
    if (get_bits1(&s->gb))
    { return 1 - (get_bits1(&s->gb) << 1); }
    else
    { return 0; }
}

static inline int get_qscale(MpegEncContext *s)
{
    int qscale = get_bits(&s->gb, 5);

    if (s->q_scale_type)
    {
        return non_linear_qscale[qscale];
    }
    else
    {
        return qscale << 1;
    }
}

/* motion type (for MPEG-2) */
#define MT_FIELD 1
#define MT_FRAME 2
#define MT_16X8  2
#define MT_DMV   3

static int mpeg_decode_mb(MpegEncContext *s,
                          DCTELEM block[12][64])
{
    int i, j, k, cbp, val, mb_type, motion_type;

    /*s->chroma_formatָʾ�Ƿ�420 422 444 xiongfei20100222*/
    const int mb_block_count = 4 + (1 << s->chroma_format);

    dprintf(s->avctx, "decode_mb: x=%d y=%d\n", s->mb_x, s->mb_y);

    assert(s->mb_skipped == 0);

    if (s->mb_skip_run-- != 0)
    {
        if (s->pict_type == FF_P_TYPE)
        {
            s->mb_skipped = 1;
            s->current_picture.mb_type[ s->mb_x + s->mb_y * s->mb_stride ] = MB_TYPE_SKIP | MB_TYPE_L0 | MB_TYPE_16x16;
        }
        else
        {
            int mb_type;

            if (s->mb_x)
            { mb_type = s->current_picture.mb_type[ s->mb_x + s->mb_y * s->mb_stride - 1]; }
            else
            { mb_type = s->current_picture.mb_type[ s->mb_width + (s->mb_y - 1) * s->mb_stride - 1]; } // FIXME not sure if this is allowed in MPEG at all

            if (IS_INTRA(mb_type))
            { return -1; }

            s->current_picture.mb_type[ s->mb_x + s->mb_y * s->mb_stride ] =
                mb_type | MB_TYPE_SKIP;
            //            assert(s->current_picture.mb_type[ s->mb_x + s->mb_y*s->mb_stride - 1]&(MB_TYPE_16x16|MB_TYPE_16x8));

            if ((s->mv[0][0][0] | s->mv[0][0][1] | s->mv[1][0][0] | s->mv[1][0][1]) == 0)
            { s->mb_skipped = 1; }
        }

        return 0;
    }

    /*���½��������� xiongfei20100222*/
    switch (s->pict_type)
    {
        default:
        case FF_I_TYPE:

            /*I֡���ģʽ����ο�B-2 xiongfei20100222*/
            if (get_bits1(&s->gb) == 0)
            {
                /*���Ӵ����� xiongfei20100317*/
                if (get_bits1(&s->gb) == 0)
                {
                    av_log(s->avctx, AV_LOG_WARNING, "invalid mb type in I Frame at %d %d\n", s->mb_x, s->mb_y);
                    IMEDIA_SET_ERR_MB(s->avctx->iErrorCode, IMEDIA_ERR_MB_TYPE);
                    s->avctx->iTotalError ++;
                    return -1;
                }

                mb_type = MB_TYPE_QUANT | MB_TYPE_INTRA;
            }
            else
            {
                mb_type = MB_TYPE_INTRA;
            }

            break;

        case FF_P_TYPE:
            /*P֡���ģʽ����ο�B-3 xiongfei20100222*/
            mb_type = get_vlc2(&s->gb, mb_ptype_vlc.table, MB_PTYPE_VLC_BITS, 1);

            /*���Ӵ����� xiongfei20100317*/
            if (mb_type < 0)
            {
                av_log(s->avctx, AV_LOG_WARNING, "invalid mb type in P Frame at %d %d\n", s->mb_x, s->mb_y);
                IMEDIA_SET_ERR_MB(s->avctx->iErrorCode, IMEDIA_ERR_MB_TYPE);
                s->avctx->iTotalError ++;
                return -1;
            }

            mb_type = ptype2mb_type[ mb_type ];
            break;

        case FF_B_TYPE:
            /*B֡���ģʽ����ο�B-4 xiongfei20100222*/
            mb_type = get_vlc2(&s->gb, mb_btype_vlc.table, MB_BTYPE_VLC_BITS, 1);

            /*���Ӵ����� xiongfei20100317*/
            if (mb_type < 0)
            {
                av_log(s->avctx, AV_LOG_WARNING, "invalid mb type in B Frame at %d %d\n", s->mb_x, s->mb_y);
                IMEDIA_SET_ERR_MB(s->avctx->iErrorCode, IMEDIA_ERR_MB_TYPE);
                s->avctx->iTotalError ++;
                return -1;
            }

            mb_type = btype2mb_type[ mb_type ];
            break;
    }

    dprintf(s->avctx, "mb_type=%x\n", mb_type);
    //    motion_type = 0; /* avoid warning */

    if (IS_INTRA(mb_type))
    {
        /*intra��������� xiongfei20100222*/
        /*��������ַ��ʼ6��block��С�ռ�������� xiongfei20100224*/
        s->dsp.clear_blocks(s->block[0]);

        if (!s->chroma_y_shift)
        {
            s->dsp.clear_blocks(s->block[6]);
        }

        /* compute DCT type */
        if (s->picture_structure == PICT_FRAME && //FIXME add an interlaced_dct coded var?
            !s->frame_pred_frame_dct)
        {
            s->interlaced_dct = get_bits1(&s->gb);
        }

        /*���qp xiongfei20100222*/
        if (IS_QUANT(mb_type))
        { s->qscale = get_qscale(s); }

        if (s->concealment_motion_vectors)
        {
            /* just parse them */
            if (s->picture_structure != PICT_FRAME)
            { skip_bits1(&s->gb); } /* field select */

            s->mv[0][0][0] = s->last_mv[0][0][0] = s->last_mv[0][1][0] =
                    mpeg_decode_motion(s, s->mpeg_f_code[0][0], s->last_mv[0][0][0]);
            s->mv[0][0][1] = s->last_mv[0][0][1] = s->last_mv[0][1][1] =
                    mpeg_decode_motion(s, s->mpeg_f_code[0][1], s->last_mv[0][0][1]);

            skip_bits1(&s->gb); /* marker */
        }
        else
        { memset(s->last_mv, 0, sizeof(s->last_mv)); } /* reset mv prediction */

        s->mb_intra = 1;

        //if 1, we memcpy blocks in xvmcvideo
        //xiongfei20100210
        /*
                if(CONFIG_MPEG_XVMC_DECODER && s->avctx->xvmc_acceleration > 1){
                    ff_xvmc_pack_pblocks(s,-1);//inter are always full blocks
                    if(s->swap_uv){
                        exchange_uv(s);
                    }
                }
        */
        /*intra��vld ��ÿ��һ��block������������ݴ����s->pblocks[i]��xiongfei20100222*/
        if (s->codec_id == CODEC_ID_MPEG2VIDEO)
        {
            if (s->flags2 & CODEC_FLAG2_FAST)
            {
                for (i = 0; i < 6; i++)
                {
                    mpeg2_fast_decode_block_intra(s, *s->pblocks[i], i);
                }
            }
            else
            {
                for (i = 0; i < mb_block_count; i++)
                {
                    if (mpeg2_decode_block_intra(s, *s->pblocks[i], i) < 0)
                    { return -1; }
                }
            }
        }
        else
        {
            for (i = 0; i < 6; i++)
            {
                if (ff_mpeg1_decode_block_intra(s, *s->pblocks[i], i) < 0)
                { return -1; }
            }
        }
    }
    else
    {
        /*inter�Լ�B�������� xiongfei20100222*/
        if (mb_type & MB_TYPE_ZERO_MV)
        {
            /*0mv����� xiongfei20100222*/
            assert(mb_type & MB_TYPE_CBP);

            s->mv_dir = MV_DIR_FORWARD;

            if (s->picture_structure == PICT_FRAME)
            {
                if (!s->frame_pred_frame_dct)
                { s->interlaced_dct = get_bits1(&s->gb); }

                s->mv_type = MV_TYPE_16X16;
            }
            else
            {
                s->mv_type = MV_TYPE_FIELD;
                mb_type |= MB_TYPE_INTERLACED;
                s->field_select[0][0] = s->picture_structure - 1;
            }

            if (IS_QUANT(mb_type))
            { s->qscale = get_qscale(s); }

            /*omv������£�����mvԤ��ֵ xiongfei20100222*/
            s->last_mv[0][0][0] = 0;
            s->last_mv[0][0][1] = 0;
            s->last_mv[0][1][0] = 0;
            s->last_mv[0][1][1] = 0;
            s->mv[0][0][0] = 0;
            s->mv[0][0][1] = 0;
        }
        else
        {
            /*��mv����� xiongfei20100222*/
            assert(mb_type & MB_TYPE_L0L1);

            //FIXME decide if MBs in field pictures are MB_TYPE_INTERLACED
            /* get additional motion vector type */
            if (s->frame_pred_frame_dct)
            { motion_type = MT_FRAME; }
            else
            {
                motion_type = get_bits(&s->gb, 2);

                if (s->picture_structure == PICT_FRAME && HAS_CBP(mb_type))
                { s->interlaced_dct = get_bits1(&s->gb); }
            }

            if (IS_QUANT(mb_type))
            { s->qscale = get_qscale(s); }

            /* motion vectors */
            s->mv_dir = (mb_type >> 13) & 3;
            dprintf(s->avctx, "motion_type=%d\n", motion_type);

            /*�����˶�ʸ�����ͽ��н���mv xiongfei20100222*/
            /*��ʱ��������mv�����Ǹ������������������ݣ�û�н����κα任 xiongfei20100224*/
            switch (motion_type)
            {
                case MT_FRAME: /* or MT_16X8 */
                    if (s->picture_structure == PICT_FRAME)
                    {
                        mb_type |= MB_TYPE_16x16;
                        s->mv_type = MV_TYPE_16X16;

                        for (i = 0; i < 2; i++)
                        {
                            if (USES_LIST(mb_type, i))
                            {
                                /*xiongfei20100401 modified ���fcode*/
                                {
                                    if ((s->mpeg_f_code[i][0] < 1) || (s->mpeg_f_code[i][1] > 9) || (s->mpeg_f_code[i][0] > 9) || (s->mpeg_f_code[i][1] < 1))
                                    {
                                        av_log(s->avctx, AV_LOG_WARNING,
                                               "MPEG F_CODE  wrong\n");
                                        IMEDIA_SET_ERR_MB(s->avctx->iErrorCode, IMEDIA_ERR_MB_MV);
                                        s->avctx->iTotalError ++;
                                        return -1;
                                    }

                                }
                                /* MT_FRAME */
                                s->mv[i][0][0] = s->last_mv[i][0][0] = s->last_mv[i][1][0] =
                                        mpeg_decode_motion(s, s->mpeg_f_code[i][0], s->last_mv[i][0][0]);
                                s->mv[i][0][1] = s->last_mv[i][0][1] = s->last_mv[i][1][1] =
                                        mpeg_decode_motion(s, s->mpeg_f_code[i][1], s->last_mv[i][0][1]);

                                /* full_pel: only for MPEG-1 */
                                if (s->full_pel[i])
                                {
                                    s->mv[i][0][0] <<= 1;
                                    s->mv[i][0][1] <<= 1;
                                }
                            }
                        }
                    }
                    else
                    {
                        mb_type |= MB_TYPE_16x8 | MB_TYPE_INTERLACED;
                        s->mv_type = MV_TYPE_16X8;

                        for (i = 0; i < 2; i++)
                        {
                            if (USES_LIST(mb_type, i))
                            {
                                /* MT_16X8 */
                                for (j = 0; j < 2; j++)
                                {
                                    /*��òο��� xiongfei20100222*/
                                    s->field_select[i][j] = get_bits1(&s->gb);

                                    for (k = 0; k < 2; k++)
                                    {
                                        /*xiongfei20100401 modified ���fcode*/
                                        {
                                            if ((s->mpeg_f_code[i][k] < 1) || (s->mpeg_f_code[i][k] > 9))
                                            {
                                                av_log(s->avctx, AV_LOG_WARNING,
                                                       "MPEG F_CODE  wrong\n");
                                                IMEDIA_SET_ERR_MB(s->avctx->iErrorCode, IMEDIA_ERR_MB_MV);
                                                s->avctx->iTotalError ++;
                                                return -1;
                                            }

                                        }
                                        val = mpeg_decode_motion(s, s->mpeg_f_code[i][k],
                                                                 s->last_mv[i][j][k]);
                                        s->last_mv[i][j][k] = val;
                                        s->mv[i][j][k] = val;
                                    }
                                }
                            }
                        }
                    }

                    break;

                case MT_FIELD:
                    s->mv_type = MV_TYPE_FIELD;

                    if (s->picture_structure == PICT_FRAME)
                    {
                        mb_type |= MB_TYPE_16x8 | MB_TYPE_INTERLACED;

                        for (i = 0; i < 2; i++)
                        {
                            if (USES_LIST(mb_type, i))
                            {
                                for (j = 0; j < 2; j++)
                                {
                                    s->field_select[i][j] = get_bits1(&s->gb);
                                    /*xiongfei20100401 modified ���fcode*/
                                    {
                                        if ((s->mpeg_f_code[i][0] < 1) || (s->mpeg_f_code[i][1] > 9) || (s->mpeg_f_code[i][0] > 9) || (s->mpeg_f_code[i][1] < 1))
                                        {
                                            av_log(s->avctx, AV_LOG_WARNING,
                                                   "MPEG F_CODE  wrong\n");
                                            IMEDIA_SET_ERR_MB(s->avctx->iErrorCode, IMEDIA_ERR_MB_MV);
                                            s->avctx->iTotalError ++;
                                            return -1;
                                        }

                                    }
                                    val = mpeg_decode_motion(s, s->mpeg_f_code[i][0],
                                                             s->last_mv[i][j][0]);
                                    s->last_mv[i][j][0] = val;
                                    s->mv[i][j][0] = val;
                                    dprintf(s->avctx, "fmx=%d\n", val);
                                    val = mpeg_decode_motion(s, s->mpeg_f_code[i][1],
                                                             s->last_mv[i][j][1] >> 1);
                                    s->last_mv[i][j][1] = val << 1;
                                    s->mv[i][j][1] = val;
                                    dprintf(s->avctx, "fmy=%d\n", val);
                                }
                            }
                        }
                    }
                    else
                    {
                        mb_type |= MB_TYPE_16x16 | MB_TYPE_INTERLACED;

                        for (i = 0; i < 2; i++)
                        {
                            if (USES_LIST(mb_type, i))
                            {
                                s->field_select[i][0] = get_bits1(&s->gb);

                                for (k = 0; k < 2; k++)
                                {
                                    /*xiongfei20100401 modified ���fcode*/
                                    {
                                        if ((s->mpeg_f_code[i][k] < 1) || (s->mpeg_f_code[i][k] > 9))
                                        {
                                            av_log(s->avctx, AV_LOG_WARNING,
                                                   "MPEG F_CODE  wrong\n");
                                            IMEDIA_SET_ERR_MB(s->avctx->iErrorCode, IMEDIA_ERR_MB_MV);
                                            s->avctx->iTotalError ++;
                                            return -1;
                                        }

                                    }
                                    val = mpeg_decode_motion(s, s->mpeg_f_code[i][k],
                                                             s->last_mv[i][0][k]);
                                    s->last_mv[i][0][k] = val;
                                    s->last_mv[i][1][k] = val;
                                    s->mv[i][0][k] = val;
                                }
                            }
                        }
                    }

                    break;

                case MT_DMV:
                    s->mv_type = MV_TYPE_DMV;

                    for (i = 0; i < 2; i++)
                    {
                        if (USES_LIST(mb_type, i))
                        {
                            int dmx, dmy, mx, my, m;
                            /*xiongfei20100401 modified ���fcode*/
                            {
                                if ((s->mpeg_f_code[i][0] < 1) || (s->mpeg_f_code[i][1] > 9) || (s->mpeg_f_code[i][0] > 9) || (s->mpeg_f_code[i][1] < 1))
                                {
                                    av_log(s->avctx, AV_LOG_WARNING,
                                           "MPEG F_CODE  wrong\n");
                                    IMEDIA_SET_ERR_MB(s->avctx->iErrorCode, IMEDIA_ERR_MB_MV);
                                    s->avctx->iTotalError ++;
                                    return -1;
                                }

                            }
                            mx = mpeg_decode_motion(s, s->mpeg_f_code[i][0],
                                                    s->last_mv[i][0][0]);
                            s->last_mv[i][0][0] = mx;
                            s->last_mv[i][1][0] = mx;
                            dmx = get_dmv(s);
                            my = mpeg_decode_motion(s, s->mpeg_f_code[i][1],
                                                    s->last_mv[i][0][1] >> 1);
                            dmy = get_dmv(s);


                            s->last_mv[i][0][1] = my << 1;
                            s->last_mv[i][1][1] = my << 1;

                            s->mv[i][0][0] = mx;
                            s->mv[i][0][1] = my;
                            s->mv[i][1][0] = mx;//not used
                            s->mv[i][1][1] = my;//not used

                            if (s->picture_structure == PICT_FRAME)
                            {
                                mb_type |= MB_TYPE_16x16 | MB_TYPE_INTERLACED;

                                //m = 1 + 2 * s->top_field_first;
                                m = s->top_field_first ? 1 : 3;

                                /* top -> top pred */
                                s->mv[i][2][0] = ((mx * m + (mx > 0)) >> 1) + dmx;
                                s->mv[i][2][1] = ((my * m + (my > 0)) >> 1) + dmy - 1;
                                m = 4 - m;
                                s->mv[i][3][0] = ((mx * m + (mx > 0)) >> 1) + dmx;
                                s->mv[i][3][1] = ((my * m + (my > 0)) >> 1) + dmy + 1;
                            }
                            else
                            {
                                mb_type |= MB_TYPE_16x16;

                                s->mv[i][2][0] = ((mx + (mx > 0)) >> 1) + dmx;
                                s->mv[i][2][1] = ((my + (my > 0)) >> 1) + dmy;

                                if (s->picture_structure == PICT_TOP_FIELD)
                                { s->mv[i][2][1]--; }
                                else
                                { s->mv[i][2][1]++; }
                            }
                        }
                    }

                    break;

                default:
                    /*���Ӵ����� xiongfei20100317*/
                    av_log(s->avctx, AV_LOG_WARNING, "00 motion_type at %d %d\n", s->mb_x, s->mb_y);
                    IMEDIA_SET_ERR_MB(s->avctx->iErrorCode, IMEDIA_ERR_MB_MV);
                    s->avctx->iTotalError ++;
                    return -1;
            }
        }

        /*����inter����B��в�� xiongfei20100222*/
        s->mb_intra = 0;

        if (HAS_CBP(mb_type))
        {
            s->dsp.clear_blocks(s->block[0]);

            cbp = get_vlc2(&s->gb, mb_pat_vlc.table, MB_PAT_VLC_BITS, 1);

            if (mb_block_count > 6)
            {
                cbp <<= mb_block_count - 6;
                cbp |= get_bits(&s->gb, mb_block_count - 6);
                s->dsp.clear_blocks(s->block[6]);
            }

            /*���Ӵ����� xiongfei20100317*/
            if (cbp <= 0)
            {
                av_log(s->avctx, AV_LOG_WARNING, "invalid cbp at %d %d\n", s->mb_x, s->mb_y);
                IMEDIA_SET_ERR_MB(s->avctx->iErrorCode, IMEDIA_ERR_MB_CBP);
                s->avctx->iTotalError ++;
                return -1;
            }

            //if 1, we memcpy blocks in xvmcvideo
            //xiongfei20100210
            /*
            			if(CONFIG_MPEG_XVMC_DECODER && s->avctx->xvmc_acceleration > 1){
                            ff_xvmc_pack_pblocks(s,cbp);
                            if(s->swap_uv){
                                exchange_uv(s);
                            }
                        }
            */
            if (s->codec_id == CODEC_ID_MPEG2VIDEO)
            {
                if (s->flags2 & CODEC_FLAG2_FAST)
                {
                    for (i = 0; i < 6; i++)
                    {
                        if (cbp & 32)
                        {
                            mpeg2_fast_decode_block_non_intra(s, *s->pblocks[i], i);
                        }
                        else
                        {
                            s->block_last_index[i] = -1;
                        }

                        cbp += cbp;
                    }
                }
                else
                {
                    cbp <<= 12 - mb_block_count;

                    for (i = 0; i < mb_block_count; i++)
                    {
                        if ( cbp & (1 << 11) )
                        {
                            if (mpeg2_decode_block_non_intra(s, *s->pblocks[i], i) < 0)
                            { return -1; }
                        }
                        else
                        {
                            s->block_last_index[i] = -1;
                        }

                        cbp += cbp;
                    }
                }
            }
            else
            {
                if (s->flags2 & CODEC_FLAG2_FAST)
                {
                    for (i = 0; i < 6; i++)
                    {
                        if (cbp & 32)
                        {
                            mpeg1_fast_decode_block_inter(s, *s->pblocks[i], i);
                        }
                        else
                        {
                            s->block_last_index[i] = -1;
                        }

                        cbp += cbp;
                    }
                }
                else
                {
                    for (i = 0; i < 6; i++)
                    {
                        if (cbp & 32)
                        {
                            if (mpeg1_decode_block_inter(s, *s->pblocks[i], i) < 0)
                            { return -1; }
                        }
                        else
                        {
                            s->block_last_index[i] = -1;
                        }

                        cbp += cbp;
                    }
                }
            }
        }
        else
        {
            for (i = 0; i < 12; i++)
            { s->block_last_index[i] = -1; }
        }
    }

    s->current_picture.mb_type[ s->mb_x + s->mb_y * s->mb_stride ] = mb_type;

    return 0;
}

/* as H.263, but only 17 codes */
static int mpeg_decode_motion(MpegEncContext *s, int fcode, int pred)
{
    int code, sign, val, l, shift;

    /*xiongfei20100401�޸Ĵ˺���  ���Ӷ�low high���ж� */
    //	int low,high,range;

    code = get_vlc2(&s->gb, mv_vlc.table, MV_VLC_BITS, 2);

    if (code == 0)
    {
        return pred;
    }

    /*�������ڴ����Ӵ����� �������Ӵ�ӡ��Ϣ ��ʾ�˴����ش��� xiongfei20100317*/
    if (code < 0)
    {
        av_log(s->avctx, AV_LOG_WARNING, "ac-tex damaged at %d %d\n", s->mb_x, s->mb_y);
        IMEDIA_SET_ERR_MB(s->avctx->iErrorCode, IMEDIA_ERR_MB_MV);
        s->avctx->iTotalError ++;
        return 0xffff;
    }

    sign = get_bits1(&s->gb);
    shift = fcode - 1;

    /*xiongfei20100401�޸Ĵ˺���  ���Ӷ�low high���ж� */
    // 	low = (-16)*shift;
    // 	high = 16*shift - 1;
    // 	range = 32 * shift;

    val = code;

    if (shift)
    {
        val = (val - 1) << shift;
        val |= get_bits(&s->gb, shift);
        val++;
    }


    if (sign)
    { val = -val; }

    val += pred;

    /*xiongfei20100401�޸Ĵ˺���  ���Ӷ�low high���ж� */
    // 	{
    // 		if(val < low)
    // 		{
    // 			val += range;
    // 		}
    // 		if(val > high)
    // 		{
    // 			val -= range;
    // 		}
    // 	}

    /* modulo decoding */
    l = INT_BIT - 5 - shift;
    val = (val << l) >> l;
    return val;
}

/*inline*/ int ff_mpeg1_decode_block_intra(MpegEncContext *s,
        DCTELEM *block,
        int n)
{
    int level, dc, diff, i, j, run;
    int component;
    RLTable *rl = &ff_rl_mpeg1;
    uint8_t *const scantable = s->intra_scantable.permutated;
    const uint16_t *quant_matrix = s->intra_matrix;
    const int qscale = s->qscale;

    /* DC coefficient */
    component = (n <= 3 ? 0 : n - 4 + 1);
    diff = decode_dc(&s->gb, component);

    if (diff >= 0xffff)
    { return -1; }

    dc = s->last_dc[component];
    dc += diff;
    s->last_dc[component] = dc;
    block[0] = dc * quant_matrix[0];
    dprintf(s->avctx, "dc=%d diff=%d\n", dc, diff);
    i = 0;
    {
        OPEN_READER(re, &s->gb);

        /* now quantify & encode AC coefficients */
        for (;;)
        {
            UPDATE_CACHE(re, &s->gb);
            GET_RL_VLC(level, run, re, &s->gb, rl->rl_vlc[0], TEX_VLC_BITS, 2, 0);

            if (level == 127)
            {
                break;
            }
            else if (level != 0)
            {
                i += run;
                j = scantable[i];
                level = (level * qscale * quant_matrix[j]) >> 4;
                level = (level - 1) | 1;
                level = (level ^ SHOW_SBITS(re, &s->gb, 1)) - SHOW_SBITS(re, &s->gb, 1);
                LAST_SKIP_BITS(re, &s->gb, 1);
            }
            else
            {
                /* escape */
                run = SHOW_UBITS(re, &s->gb, 6) + 1;
                LAST_SKIP_BITS(re, &s->gb, 6);
                UPDATE_CACHE(re, &s->gb);
                level = SHOW_SBITS(re, &s->gb, 8);
                SKIP_BITS(re, &s->gb, 8);

                if (level == -128)
                {
                    level = SHOW_UBITS(re, &s->gb, 8) - 256;
                    LAST_SKIP_BITS(re, &s->gb, 8);
                }
                else if (level == 0)
                {
                    level = SHOW_UBITS(re, &s->gb, 8)      ;
                    LAST_SKIP_BITS(re, &s->gb, 8);
                }

                i += run;
                j = scantable[i];

                if (level < 0)
                {
                    level = -level;
                    level = (level * qscale * quant_matrix[j]) >> 4;
                    level = (level - 1) | 1;
                    level = -level;
                }
                else
                {
                    level = (level * qscale * quant_matrix[j]) >> 4;
                    level = (level - 1) | 1;
                }
            }

            if (i > 63)
            {
                av_log(s->avctx, AV_LOG_WARNING, "ac-tex damaged at %d %d\n", s->mb_x, s->mb_y);
                return -1;
            }

            block[j] = level;
        }

        CLOSE_READER(re, &s->gb);
    }
    s->block_last_index[n] = i;
    return 0;
}

static inline int mpeg1_decode_block_inter(MpegEncContext *s,
        DCTELEM *block,
        int n)
{
    int level, i, j, run;
    RLTable *rl = &ff_rl_mpeg1;
    uint8_t *const scantable = s->intra_scantable.permutated;
    const uint16_t *quant_matrix = s->inter_matrix;
    const int qscale = s->qscale;

    {
        OPEN_READER(re, &s->gb);
        i = -1;
        // special case for first coefficient, no need to add second VLC table
        UPDATE_CACHE(re, &s->gb);

        if (((int32_t)GET_CACHE(re, &s->gb)) < 0)
        {
            level = (3 * qscale * quant_matrix[0]) >> 5;
            level = (level - 1) | 1;

            if (GET_CACHE(re, &s->gb) & 0x40000000)
            { level = -level; }

            block[0] = level;
            i++;
            SKIP_BITS(re, &s->gb, 2);

            if (((int32_t)GET_CACHE(re, &s->gb)) <= (int32_t)0xBFFFFFFF)
            { goto end; }
        }

#if MIN_CACHE_BITS < 19
        UPDATE_CACHE(re, &s->gb);
#endif

        /* now quantify & encode AC coefficients */
        for (;;)
        {
            GET_RL_VLC(level, run, re, &s->gb, rl->rl_vlc[0], TEX_VLC_BITS, 2, 0);

            if (level != 0)
            {
                i += run;
                j = scantable[i];
                level = ((level * 2 + 1) * qscale * quant_matrix[j]) >> 5;
                level = (level - 1) | 1;
                level = (level ^ SHOW_SBITS(re, &s->gb, 1)) - SHOW_SBITS(re, &s->gb, 1);
                SKIP_BITS(re, &s->gb, 1);
            }
            else
            {
                /* escape */
                run = SHOW_UBITS(re, &s->gb, 6) + 1;
                LAST_SKIP_BITS(re, &s->gb, 6);
                UPDATE_CACHE(re, &s->gb);
                level = SHOW_SBITS(re, &s->gb, 8);
                SKIP_BITS(re, &s->gb, 8);

                if (level == -128)
                {
                    level = SHOW_UBITS(re, &s->gb, 8) - 256;
                    SKIP_BITS(re, &s->gb, 8);
                }
                else if (level == 0)
                {
                    level = SHOW_UBITS(re, &s->gb, 8)      ;
                    SKIP_BITS(re, &s->gb, 8);
                }

                i += run;
                j = scantable[i];

                if (level < 0)
                {
                    level = -level;
                    level = ((level * 2 + 1) * qscale * quant_matrix[j]) >> 5;
                    level = (level - 1) | 1;
                    level = -level;
                }
                else
                {
                    level = ((level * 2 + 1) * qscale * quant_matrix[j]) >> 5;
                    level = (level - 1) | 1;
                }
            }

            if (i > 63)
            {
                av_log(s->avctx, AV_LOG_WARNING, "ac-tex damaged at %d %d\n", s->mb_x, s->mb_y);
                return -1;
            }

            block[j] = level;
#if MIN_CACHE_BITS < 19
            UPDATE_CACHE(re, &s->gb);
#endif

            if (((int32_t)GET_CACHE(re, &s->gb)) <= (int32_t)0xBFFFFFFF)
            { break; }

#if MIN_CACHE_BITS >= 19
            UPDATE_CACHE(re, &s->gb);
#endif
        }

    end:
        LAST_SKIP_BITS(re, &s->gb, 2);
        CLOSE_READER(re, &s->gb);
    }
    s->block_last_index[n] = i;
    return 0;
}

static inline int mpeg1_fast_decode_block_inter(MpegEncContext *s, DCTELEM *block, int n)
{
    int level, i, j, run;
    RLTable *rl = &ff_rl_mpeg1;
    uint8_t *const scantable = s->intra_scantable.permutated;
    const int qscale = s->qscale;

    {
        OPEN_READER(re, &s->gb);
        i = -1;
        // special case for first coefficient, no need to add second VLC table
        UPDATE_CACHE(re, &s->gb);

        if (((int32_t)GET_CACHE(re, &s->gb)) < 0)
        {
            level = (3 * qscale) >> 1;
            level = (level - 1) | 1;

            if (GET_CACHE(re, &s->gb) & 0x40000000)
            { level = -level; }

            block[0] = level;
            i++;
            SKIP_BITS(re, &s->gb, 2);

            if (((int32_t)GET_CACHE(re, &s->gb)) <= (int32_t)0xBFFFFFFF)
            { goto end; }
        }

#if MIN_CACHE_BITS < 19
        UPDATE_CACHE(re, &s->gb);
#endif

        /* now quantify & encode AC coefficients */
        for (;;)
        {
            GET_RL_VLC(level, run, re, &s->gb, rl->rl_vlc[0], TEX_VLC_BITS, 2, 0);

            if (level != 0)
            {
                i += run;
                j = scantable[i];
                level = ((level * 2 + 1) * qscale) >> 1;
                level = (level - 1) | 1;
                level = (level ^ SHOW_SBITS(re, &s->gb, 1)) - SHOW_SBITS(re, &s->gb, 1);
                SKIP_BITS(re, &s->gb, 1);
            }
            else
            {
                /* escape */
                run = SHOW_UBITS(re, &s->gb, 6) + 1;
                LAST_SKIP_BITS(re, &s->gb, 6);
                UPDATE_CACHE(re, &s->gb);
                level = SHOW_SBITS(re, &s->gb, 8);
                SKIP_BITS(re, &s->gb, 8);

                if (level == -128)
                {
                    level = SHOW_UBITS(re, &s->gb, 8) - 256;
                    SKIP_BITS(re, &s->gb, 8);
                }
                else if (level == 0)
                {
                    level = SHOW_UBITS(re, &s->gb, 8)      ;
                    SKIP_BITS(re, &s->gb, 8);
                }

                i += run;
                j = scantable[i];

                if (level < 0)
                {
                    level = -level;
                    level = ((level * 2 + 1) * qscale) >> 1;
                    level = (level - 1) | 1;
                    level = -level;
                }
                else
                {
                    level = ((level * 2 + 1) * qscale) >> 1;
                    level = (level - 1) | 1;
                }
            }

            block[j] = level;
#if MIN_CACHE_BITS < 19
            UPDATE_CACHE(re, &s->gb);
#endif

            if (((int32_t)GET_CACHE(re, &s->gb)) <= (int32_t)0xBFFFFFFF)
            { break; }

#if MIN_CACHE_BITS >= 19
            UPDATE_CACHE(re, &s->gb);
#endif
        }

    end:
        LAST_SKIP_BITS(re, &s->gb, 2);
        CLOSE_READER(re, &s->gb);
    }
    s->block_last_index[n] = i;
    return 0;
}


static inline int mpeg2_decode_block_non_intra(MpegEncContext *s,
        DCTELEM *block,
        int n)
{
    int level, i, j, run;
    RLTable *rl = &ff_rl_mpeg1;
    uint8_t *const scantable = s->intra_scantable.permutated;
    const uint16_t *quant_matrix;
    const int qscale = s->qscale;
    int mismatch;

    mismatch = 1;

    {
        OPEN_READER(re, &s->gb);
        i = -1;

        if (n < 4)
        { quant_matrix = s->inter_matrix; }
        else
        { quant_matrix = s->chroma_inter_matrix; }

        // special case for first coefficient, no need to add second VLC table
        /*��intra��ĵ�һ��dctϵ���ֿ����룬�����Э�� xiongfei20100224 */
        UPDATE_CACHE(re, &s->gb);

        if (((int32_t)GET_CACHE(re, &s->gb)) < 0)
        {
            level = (3 * qscale * quant_matrix[0]) >> 5;

            if (GET_CACHE(re, &s->gb) & 0x40000000)
            { level = -level; }

            block[0] = level;
            mismatch ^= level;
            i++;
            SKIP_BITS(re, &s->gb, 2);

            if (((int32_t)GET_CACHE(re, &s->gb)) <= (int32_t)0xBFFFFFFF)
            { goto end; }
        }

#if MIN_CACHE_BITS < 19
        UPDATE_CACHE(re, &s->gb);
#endif

        /* now quantify & encode AC coefficients */
        for (;;)
        {
            GET_RL_VLC(level, run, re, &s->gb, rl->rl_vlc[0], TEX_VLC_BITS, 2, 0);

            if (level != 0)
            {
                i += run;
                j = scantable[i];
                level = ((level * 2 + 1) * qscale * quant_matrix[j]) >> 5;
                level = (level ^ SHOW_SBITS(re, &s->gb, 1)) - SHOW_SBITS(re, &s->gb, 1);
                SKIP_BITS(re, &s->gb, 1);
            }
            else
            {
                /* escape */
                run = SHOW_UBITS(re, &s->gb, 6) + 1;
                LAST_SKIP_BITS(re, &s->gb, 6);
                UPDATE_CACHE(re, &s->gb);
                level = SHOW_SBITS(re, &s->gb, 12);
                SKIP_BITS(re, &s->gb, 12);

                i += run;
                j = scantable[i];

                if (level < 0)
                {
                    level = ((-level * 2 + 1) * qscale * quant_matrix[j]) >> 5;
                    level = -level;
                }
                else
                {
                    level = ((level * 2 + 1) * qscale * quant_matrix[j]) >> 5;
                }
            }

            /*���Ӵ����� xiongfei20100317*/
            if (i > 63)
            {
                av_log(s->avctx, AV_LOG_WARNING, "ac-tex damaged at %d %d\n", s->mb_x, s->mb_y);
                IMEDIA_SET_ERR_RESIDUAL(s->avctx->iErrorCode, IMEDIA_ERR_RESIDUAL_AC);
                s->avctx->iTotalError ++;
                return -1;
            }

            mismatch ^= level;
            block[j] = level;
#if MIN_CACHE_BITS < 19
            UPDATE_CACHE(re, &s->gb);
#endif

            if (((int32_t)GET_CACHE(re, &s->gb)) <= (int32_t)0xBFFFFFFF)
            { break; }

#if MIN_CACHE_BITS >= 19
            UPDATE_CACHE(re, &s->gb);
#endif
        }

    end:
        LAST_SKIP_BITS(re, &s->gb, 2);
        CLOSE_READER(re, &s->gb);
    }
    block[63] ^= (mismatch & 1);

    s->block_last_index[n] = i;
    return 0;
}

static inline int mpeg2_fast_decode_block_non_intra(MpegEncContext *s,
        DCTELEM *block,
        int n)
{
    int level, i, j, run;
    RLTable *rl = &ff_rl_mpeg1;
    uint8_t *const scantable = s->intra_scantable.permutated;
    const int qscale = s->qscale;
    OPEN_READER(re, &s->gb);
    i = -1;

    // special case for first coefficient, no need to add second VLC table
    UPDATE_CACHE(re, &s->gb);

    if (((int32_t)GET_CACHE(re, &s->gb)) < 0)
    {
        level = (3 * qscale) >> 1;

        if (GET_CACHE(re, &s->gb) & 0x40000000)
        { level = -level; }

        block[0] = level;
        i++;
        SKIP_BITS(re, &s->gb, 2);

        if (((int32_t)GET_CACHE(re, &s->gb)) <= (int32_t)0xBFFFFFFF)
        { goto end; }
    }

#if MIN_CACHE_BITS < 19
    UPDATE_CACHE(re, &s->gb);
#endif

    /* now quantify & encode AC coefficients */
    for (;;)
    {
        GET_RL_VLC(level, run, re, &s->gb, rl->rl_vlc[0], TEX_VLC_BITS, 2, 0);

        if (level != 0)
        {
            i += run;
            j = scantable[i];
            level = ((level * 2 + 1) * qscale) >> 1;
            level = (level ^ SHOW_SBITS(re, &s->gb, 1)) - SHOW_SBITS(re, &s->gb, 1);
            SKIP_BITS(re, &s->gb, 1);
        }
        else
        {
            /* escape */
            run = SHOW_UBITS(re, &s->gb, 6) + 1;
            LAST_SKIP_BITS(re, &s->gb, 6);
            UPDATE_CACHE(re, &s->gb);
            level = SHOW_SBITS(re, &s->gb, 12);
            SKIP_BITS(re, &s->gb, 12);

            i += run;
            j = scantable[i];

            if (level < 0)
            {
                level = ((-level * 2 + 1) * qscale) >> 1;
                level = -level;
            }
            else
            {
                level = ((level * 2 + 1) * qscale) >> 1;
            }
        }

        block[j] = level;
#if MIN_CACHE_BITS < 19
        UPDATE_CACHE(re, &s->gb);
#endif

        if (((int32_t)GET_CACHE(re, &s->gb)) <= (int32_t)0xBFFFFFFF)
        { break; }

#if MIN_CACHE_BITS >=19
        UPDATE_CACHE(re, &s->gb);
#endif
    }

end:
    LAST_SKIP_BITS(re, &s->gb, 2);
    CLOSE_READER(re, &s->gb);
    s->block_last_index[n] = i;
    return 0;
}


static inline int mpeg2_decode_block_intra(MpegEncContext *s,
        DCTELEM *block,
        int n)
{
    int level, dc, diff, i, j, run;
    int component;
    RLTable *rl;
    uint8_t *const scantable = s->intra_scantable.permutated;
    const uint16_t *quant_matrix;
    const int qscale = s->qscale;
    int mismatch;

    /* DC coefficient */
    if (n < 4)
    {
        quant_matrix = s->intra_matrix;
        component = 0;
    }
    else
    {
        quant_matrix = s->chroma_intra_matrix;
        component = (n & 1) + 1;
    }

    diff = decode_dc(&s->gb, component);

    /*������־��ӡ�Լ������� xongfei20100317*/
    if (diff >= 0xffff)
    {
        av_log(s->avctx, AV_LOG_WARNING, "dc[%d] damaged \n", diff);
        IMEDIA_SET_ERR_RESIDUAL(s->avctx->iErrorCode, IMEDIA_ERR_RESIDUAL_DC);
        s->avctx->iTotalError ++;
        return -1;
    }

    /*�˴���ñ�block DCֵ ����Ҫ������ xiongfei20100223*/
    dc = s->last_dc[component];
    dc += diff;

    s->last_dc[component] = dc;

    /*�˴������ǰintra���DCϵ��ֵ xiongfei20100223*/
    block[0] = dc << (3 - s->intra_dc_precision);
    dprintf(s->avctx, "dc=%d\n", block[0]);
    mismatch = block[0] ^ 1;
    i = 0;

    if (s->intra_vlc_format)
    { rl = &ff_rl_mpeg2; }
    else
    { rl = &ff_rl_mpeg1; }

    /*��ʼintra���vlc���� xiongfei20100224*/
    {
        OPEN_READER(re, &s->gb);

        /* now quantify & encode AC coefficients */
        for (;;)
        {
            UPDATE_CACHE(re, &s->gb);
            GET_RL_VLC(level, run, re, &s->gb, rl->rl_vlc[0], TEX_VLC_BITS, 2, 0);

            if (level == 127)
            {
                break;
            }
            else if (level != 0)
            {
                i += run;
                j = scantable[i];
                level = (level * qscale * quant_matrix[j]) >> 4;
                level = (level ^ SHOW_SBITS(re, &s->gb, 1)) - SHOW_SBITS(re, &s->gb, 1);
                LAST_SKIP_BITS(re, &s->gb, 1);
            }
            else
            {
                /* escape */
                run = SHOW_UBITS(re, &s->gb, 6) + 1;
                LAST_SKIP_BITS(re, &s->gb, 6);
                UPDATE_CACHE(re, &s->gb);
                level = SHOW_SBITS(re, &s->gb, 12);
                SKIP_BITS(re, &s->gb, 12);
                i += run;
                j = scantable[i];

                if (level < 0)
                {
                    level = (-level * qscale * quant_matrix[j]) >> 4;
                    level = -level;
                }
                else
                {
                    level = (level * qscale * quant_matrix[j]) >> 4;
                }
            }

            /*���Ӵ����� xiongfei20100317*/
            if (i > 63)
            {
                av_log(s->avctx, AV_LOG_WARNING, "ac-tex damaged at %d %d\n", s->mb_x, s->mb_y);
                IMEDIA_SET_ERR_RESIDUAL(s->avctx->iErrorCode, IMEDIA_ERR_RESIDUAL_AC);
                s->avctx->iTotalError ++;
                return -1;
            }

            mismatch ^= level;
            block[j] = level;
        }

        CLOSE_READER(re, &s->gb);
    }
    block[63] ^= mismatch & 1;

    s->block_last_index[n] = i;
    return 0;
}

static inline int mpeg2_fast_decode_block_intra(MpegEncContext *s,
        DCTELEM *block,
        int n)
{
    int level, dc, diff, j, run;
    int component;
    RLTable *rl;
    uint8_t *scantable = s->intra_scantable.permutated;
    const uint16_t *quant_matrix;
    const int qscale = s->qscale;

    /* DC coefficient */
    if (n < 4)
    {
        quant_matrix = s->intra_matrix;
        component = 0;
    }
    else
    {
        quant_matrix = s->chroma_intra_matrix;
        component = (n & 1) + 1;
    }

    diff = decode_dc(&s->gb, component);

    if (diff >= 0xffff)
    { return -1; }

    dc = s->last_dc[component];
    dc += diff;
    s->last_dc[component] = dc;
    block[0] = dc << (3 - s->intra_dc_precision);

    if (s->intra_vlc_format)
    { rl = &ff_rl_mpeg2; }
    else
    { rl = &ff_rl_mpeg1; }

    {
        OPEN_READER(re, &s->gb);

        /* now quantify & encode AC coefficients */
        for (;;)
        {
            UPDATE_CACHE(re, &s->gb);
            GET_RL_VLC(level, run, re, &s->gb, rl->rl_vlc[0], TEX_VLC_BITS, 2, 0);

            if (level == 127)
            {
                break;
            }
            else if (level != 0)
            {
                scantable += run;
                j = *scantable;
                level = (level * qscale * quant_matrix[j]) >> 4;
                level = (level ^ SHOW_SBITS(re, &s->gb, 1)) - SHOW_SBITS(re, &s->gb, 1);
                LAST_SKIP_BITS(re, &s->gb, 1);
            }
            else
            {
                /* escape */
                run = SHOW_UBITS(re, &s->gb, 6) + 1;
                LAST_SKIP_BITS(re, &s->gb, 6);
                UPDATE_CACHE(re, &s->gb);
                level = SHOW_SBITS(re, &s->gb, 12);
                SKIP_BITS(re, &s->gb, 12);
                scantable += run;
                j = *scantable;

                if (level < 0)
                {
                    level = (-level * qscale * quant_matrix[j]) >> 4;
                    level = -level;
                }
                else
                {
                    level = (level * qscale * quant_matrix[j]) >> 4;
                }
            }

            block[j] = level;
        }

        CLOSE_READER(re, &s->gb);
    }

    s->block_last_index[n] = scantable - s->intra_scantable.permutated;
    return 0;
}
/*�� Mpeg1Context�Ķ����Ƶ�mpeg12.hͷ�ļ��� xiongfei20100221*/
static av_cold int mpeg_decode_init(AVCodecContext *avctx)
{
    Mpeg1Context *s = avctx->priv_data;
    MpegEncContext *s2 = &s->mpeg_enc_ctx;
    int i;

    /* we need some permutation to store matrices,
     * until MPV_common_init() sets the real permutation. */
    for (i = 0; i < 64; i++)
    { s2->dsp.idct_permutation[i] = i; }

    MPV_decode_defaults(s2);

    s->mpeg_enc_ctx.avctx = avctx;
    s->mpeg_enc_ctx.flags = avctx->flags;
    s->mpeg_enc_ctx.flags2 = avctx->flags2;
    ff_mpeg12_common_init(&s->mpeg_enc_ctx);
    ff_mpeg12_init_vlcs();

    s->mpeg_enc_ctx_allocated = 0;
    s->mpeg_enc_ctx.picture_number = 0;
    s->repeat_field = 0;
    s->mpeg_enc_ctx.codec_id = avctx->codec->id;

    /* xiongfei20100302*/
    /*parse_context.buffe�еĻ���ռ��ڴ˳�ʼ������������ff_combine_frame���������·����ڴ� xiongfei20100302*/
    /* �������루l00139685): ��rbsp_buffer��parse.buffer�����ڴ� */
#define MAX_FRAME_SIZE MPEG2_MAX_WIDTH*MPEG2_MAX_HEIGHT*2
    s2->parse_context.buffer = av_malloc_hw(avctx, MAX_FRAME_SIZE);

    if (NULL == s2->parse_context.buffer)
    {
        av_log(avctx, AV_LOG_ERROR, "dec_ctx[%p] decode_init() malloc buffer for s->parse_context.buffer failed!\n", avctx);
        return -1;
    }

    s2->parse_context.buffer_size = MAX_FRAME_SIZE;

    /* 2010/06/17 15:30:00 liuxw+00139685 */
    /* �ڳ�ʼ������������parse_context״̬��ʼ��(ԭ������mpv_common_init��ʵ�ֵ�) */
    s2->parse_context.state = -1;

    return 0;
}

static void quant_matrix_rebuild(uint16_t *matrix, const uint8_t *old_perm,
                                 const uint8_t *new_perm)
{
    uint16_t temp_matrix[64];
    int i;

    memcpy(temp_matrix, matrix, 64 * sizeof(uint16_t));

    for (i = 0; i < 64; i++)
    {
        matrix[new_perm[i]] = temp_matrix[old_perm[i]];
    }
}

static enum PixelFormat mpeg_get_pixelformat(AVCodecContext *avctx)
{
    Mpeg1Context *s1 = avctx->priv_data;
    MpegEncContext *s = &s1->mpeg_enc_ctx;

    if (avctx->xvmc_acceleration)
    { return avctx->get_format(avctx, pixfmt_xvmc_mpg2_420); }
    else if (avctx->codec->capabilities & CODEC_CAP_HWACCEL_VDPAU)
    {
        if (avctx->codec_id == CODEC_ID_MPEG1VIDEO)
        { return PIX_FMT_VDPAU_MPEG1; }
        else
        { return PIX_FMT_VDPAU_MPEG2; }
    }
    else
    {
        if (s->chroma_format <  2)
        { return avctx->get_format(avctx, ff_hwaccel_pixfmt_list_420); }
        else if (s->chroma_format == 2)
        { return PIX_FMT_YUV422P; }
        else
        { return PIX_FMT_YUV444P; }
    }
}

/* Call this function when we know all parameters.
 * It may be called in different places for MPEG-1 and MPEG-2. */
static int mpeg_decode_postinit(AVCodecContext *avctx)
{
    Mpeg1Context *s1 = avctx->priv_data;
    MpegEncContext *s = &s1->mpeg_enc_ctx;
    uint8_t old_permutation[64];

    /*x00141957 20100519 �޸� ���ζ�aspect_ratio_info���жϣ���Ϊ��Ӱ�����*/
    /*���ⵥ�ţ�AZ1D02088
    �޸��ˣ��ܷ� +00141957
    ʱ�䣺2010/5/19
    ���������� ��aspect_ratio_info���ж��������ⲻһ�£��Ӷ����½���δ���
    �����޸ģ� ����aspect_ratio_info�Ĵ������ϸ���*/
    if (
        //         (s1->mpeg_enc_ctx_allocated == 0)||
        //         avctx->coded_width  != s->width ||
        //         avctx->coded_height != s->height||
        //         s1->save_width != s->width ||
        //         s1->save_height != s->height ||
        //         s1->save_aspect_info != s->aspect_ratio_info||
        //         0)
        (s1->mpeg_enc_ctx_allocated == 0) ||
        avctx->coded_width  != s->width ||
        avctx->coded_height != s->height ||
        s1->save_width != s->width ||
        s1->save_height != s->height ||
        0)
    {

        if (s1->mpeg_enc_ctx_allocated)
        {
            ParseContext pc = s->parse_context;
            s->parse_context.buffer = 0;
            MPV_common_end(s);
            s->parse_context = pc;
        }

        if ( (s->width == 0 ) || (s->height == 0))
        { return -2; }

        avcodec_set_dimensions(avctx, s->width, s->height);
        avctx->bit_rate = s->bit_rate;
        s1->save_aspect_info = s->aspect_ratio_info;
        s1->save_width = s->width;
        s1->save_height = s->height;

        /* low_delay may be forced, in this case we will have B-frames
         * that behave like P-frames. */
        avctx->has_b_frames = !(s->low_delay);

        assert((avctx->sub_id == 1) == (avctx->codec_id == CODEC_ID_MPEG1VIDEO));

        if (avctx->codec_id == CODEC_ID_MPEG1VIDEO)
        {
            //MPEG-1 fps
            avctx->time_base.den = ff_frame_rate_tab[s->frame_rate_index].num;
            avctx->time_base.num = ff_frame_rate_tab[s->frame_rate_index].den;
            //MPEG-1 aspect
            //           avctx->sample_aspect_ratio= av_d2q(1.0/ff_mpeg1_aspect[s->aspect_ratio_info], 255);
            avctx->sample_aspect_ratio.num = 0;
            avctx->sample_aspect_ratio.den = 1;
            avctx->ticks_per_frame = 1;
        }
        else  //MPEG-2
        {
            //MPEG-2 fps
            /*          av_reduce(&s->avctx->time_base.den,&s->avctx->time_base.num,
                          ff_frame_rate_tab[s->frame_rate_index].num * s1->frame_rate_ext.num*2,
                          ff_frame_rate_tab[s->frame_rate_index].den * s1->frame_rate_ext.den,
                          1<<30); */
            avctx->ticks_per_frame = 2;

            //MPEG-2 aspect
            if (s->aspect_ratio_info > 1)
            {
                //we ignore the spec here as reality does not match the spec, see for example
                // res_change_ffmpeg_aspect.ts and sequence-display-aspect.mpg
                //if( (s1->pan_scan.width == 0 )||(s1->pan_scan.height == 0) || 1)
                /*x00141957 20100726*/
                if ( (s1->pan_scan.width == 0 ) || (s1->pan_scan.height == 0) )
                {
                    // s->avctx->sample_aspect_ratio=
                    /*    av_div_q(
                         ff_mpeg2_aspect[s->aspect_ratio_info],
                         _AVRational(s->width, s->height)
                         )*/;
                }
                else
                {
                    //    s->avctx->sample_aspect_ratio=
                    /*    av_div_q(
                         ff_mpeg2_aspect[s->aspect_ratio_info],
                         _AVRational(s1->pan_scan.width, s1->pan_scan.height)
                        )*/;
                }
            }
            else
            {
                s->avctx->sample_aspect_ratio =
                    ff_mpeg2_aspect[s->aspect_ratio_info];
            }
        }//MPEG-2

        /* 2010/05/08 10:05:00 songxiaogang+00133955 */
        /* ��mpeg2 idct���㷨�ĳ���libmpeg2��һ�£����������ұ���������һ���Բ��� */
        /*
        #if defined(__GNUC__)
        #if HAVE_MMX && CONFIG_GPL
                avctx->idct_algo = FF_IDCT_LIBMPEG2MMX;
        #endif
        #endif
        */
        avctx->pix_fmt = mpeg_get_pixelformat(avctx);
        avctx->hwaccel = ff_find_hwaccel(avctx->codec->id, avctx->pix_fmt);

        //until then pix_fmt may be changed right after codec init
        if ( avctx->pix_fmt == PIX_FMT_XVMC_MPEG2_IDCT ||
             avctx->hwaccel ||
             s->avctx->codec->capabilities & CODEC_CAP_HWACCEL_VDPAU )
            if ( avctx->idct_algo == FF_IDCT_AUTO )
            { avctx->idct_algo = FF_IDCT_SIMPLE; }

        /* Quantization matrices may need reordering
         * if DCT permutation is changed. */

        memcpy(old_permutation, s->dsp.idct_permutation, 64 * sizeof(uint8_t));

        if (MPV_common_init(s) < 0)
        { return -2; }

        /*��ʼ���������� xiongfei20100224*/
        quant_matrix_rebuild(s->intra_matrix,       old_permutation, s->dsp.idct_permutation);
        quant_matrix_rebuild(s->inter_matrix,       old_permutation, s->dsp.idct_permutation);
        quant_matrix_rebuild(s->chroma_intra_matrix, old_permutation, s->dsp.idct_permutation);
        quant_matrix_rebuild(s->chroma_inter_matrix, old_permutation, s->dsp.idct_permutation);

        s1->mpeg_enc_ctx_allocated = 1;
    }

    return 0;
}

static int mpeg1_decode_picture(AVCodecContext *avctx,
                                const uint8_t *buf, int buf_size)
{
    Mpeg1Context *s1 = avctx->priv_data;
    MpegEncContext *s = &s1->mpeg_enc_ctx;
    int ref, f_code, vbv_delay;

    /*MpegEncContext�ṹ����һЩ�ڴ��ʼ���ڴ� xiongfei20100222*/
    /*���Ӵ�ӡ������־�����Ӵ����� xiongfei20100317*/
    if (mpeg_decode_postinit(s->avctx) < 0)
    {
        av_log(avctx, AV_LOG_ERROR, "memmory initial failed !\n");

        IMEDIA_SET_ERR_PIC(avctx->iErrorCode, IMEDIA_ERR_SEQ_SIZE);

        return -2;
    }


    init_get_bits(&s->gb, buf, buf_size * 8);

    ref = get_bits(&s->gb, 10); /* temporal ref */
    s->pict_type = get_bits(&s->gb, 3);

    /*���Ӵ�ӡ������־�����Ӵ����� xiongfei20100317*/
    if (s->pict_type <= 0 || s->pict_type > 3)
    {
        av_log(avctx, AV_LOG_WARNING, "pict_type[%d] error !\n" , s->pict_type);

        IMEDIA_SET_ERR_PIC(avctx->iErrorCode, IMEDIA_ERR_SEQ_SIZE);

        return -1;
    }

    vbv_delay = get_bits(&s->gb, 16);

    if (s->pict_type == FF_P_TYPE || s->pict_type == FF_B_TYPE)
    {
        s->full_pel[0] = get_bits1(&s->gb);
        /*��full_pel_forward_vector��forward_f_code����Э������� x00141957 20100505*/
        f_code = get_bits(&s->gb, 3);

        if (s->full_pel[0] != 0 || f_code != 7)
        {
            av_log(avctx, AV_LOG_WARNING, "full_pel_forward_vector[%d] or f_code[%d] error !\n" , s->full_pel[0], f_code);

            IMEDIA_SET_ERR_PIC(avctx->iErrorCode, IMEDIA_ERR_SEQ_SIZE);

            //			return -1;
        }

        if (f_code == 0 && avctx->error_recognition >= FF_ER_COMPLIANT)
            ;

        //            return -1;
        s->mpeg_f_code[0][0] = f_code;
        s->mpeg_f_code[0][1] = f_code;
    }

    if (s->pict_type == FF_B_TYPE)
    {
        s->full_pel[1] = get_bits1(&s->gb);
        f_code = get_bits(&s->gb, 3);

        /*��full_pel_backward_vector��backward_f_code����Э������� x00141957 20100505*/
        if (s->full_pel[1] != 0 || f_code != 7)
        {
            av_log(avctx, AV_LOG_WARNING, "full_pel_backward_vector[%d] or f_code[%d] error !\n" , s->full_pel[1] , f_code);

            IMEDIA_SET_ERR_PIC(avctx->iErrorCode, IMEDIA_ERR_SEQ_SIZE);

            //			return -1;
        }

        if (f_code == 0 && avctx->error_recognition >= FF_ER_COMPLIANT)
            ;

        //            return -1;
        s->mpeg_f_code[1][0] = f_code;
        s->mpeg_f_code[1][1] = f_code;
    }

    s->current_picture.pict_type = s->pict_type;
    s->current_picture.key_frame = s->pict_type == FF_I_TYPE;

    if (avctx->debug & FF_DEBUG_PICT_INFO)
    { av_log(avctx, AV_LOG_DEBUG, "vbv_delay %d, ref %d type:%d\n", vbv_delay, ref, s->pict_type); }

    s->y_dc_scale = 8;
    s->c_dc_scale = 8;
    s->first_slice = 1;
    return 0;
}

static void mpeg_decode_sequence_extension(Mpeg1Context *s1)
{
    MpegEncContext *s = &s1->mpeg_enc_ctx;
    int horiz_size_ext, vert_size_ext;
    int bit_rate_ext;

    /*xiongfei���� 20100401*/
    int width, height;

    skip_bits(&s->gb, 1); /* profile and level esc*/
    s->avctx->profile = get_bits(&s->gb, 3);
    s->avctx->level = get_bits(&s->gb, 4);
    /*��Ҫ���������߽��м�� x00141957 20100506*/
    s->progressive_sequence = get_bits1(&s->gb); /* progressive_sequence */
    s->chroma_format = get_bits(&s->gb, 2); /* chroma_format 1=420, 2=422, 3=444 */

    /*��s->chroma_format���м�� x00141957 20100507*/
    if (s->chroma_format != 1 && s->chroma_format != 2)
    {
        av_log(s->avctx, AV_LOG_WARNING, "s->chroma_format[%d] error !\n" , s->chroma_format);

        IMEDIA_SET_ERR_SEQ(s->avctx->iErrorCode, IMEDIA_ERR_SEQ_SIZE);

        return;
    }

    horiz_size_ext = get_bits(&s->gb, 2);
    vert_size_ext = get_bits(&s->gb, 2);

    /*xiongfei modified 20100401*/
    // 	s->width |= (horiz_size_ext << 12);
    //  s->height |= (vert_size_ext << 12);
    width = s->width | (horiz_size_ext << 12);
    height = s->height | (vert_size_ext << 12);

    bit_rate_ext = get_bits(&s->gb, 12);  /* XXX: handle it */
    s->bit_rate += (bit_rate_ext << 18) * 400;
    skip_bits1(&s->gb); /* marker */
    s->avctx->rc_buffer_size += get_bits(&s->gb, 8) * 1024 * 16 << 10;

    s->low_delay = get_bits1(&s->gb);

    if (s->flags & CODEC_FLAG_LOW_DELAY) { s->low_delay = 1; }

    s1->frame_rate_ext.num = get_bits(&s->gb, 2) + 1;
    s1->frame_rate_ext.den = get_bits(&s->gb, 5) + 1;

    dprintf(s->avctx, "sequence extension\n");
    s->codec_id = s->avctx->codec_id = CODEC_ID_MPEG2VIDEO;
    s->avctx->sub_id = 2; /* indicates MPEG-2 found */

    if (s->avctx->debug & FF_DEBUG_PICT_INFO)
    {
        av_log(s->avctx, AV_LOG_DEBUG, "profile: %d, level: %d vbv buffer: %d, bitrate:%d\n",
               s->avctx->profile, s->avctx->level, s->avctx->rc_buffer_size, s->bit_rate);
    }

    /*�������д���Ϊ���� ����Ϊmpeg2���ߵ���Ϣ��ֻ������ͷ�и���������ʵ�ʿ���ֵֻ��������õ� ���ͨ��ʵ�ʽ������xiongfei20100303*/
    /*�������д���Ϊ���� ����Ϊmpeg2���ߵ���Ϣ��ֻ������ͷ�и���������ʵ�ʿ���ֵֻ��������õ� xiongfei20100303*/
    /*���ǵ����ǵĽ�������֧���м�任�ֱ��ʣ������Ե�һ����������ͼ�����Ϊ�� xiongfei20100401*/

    /*��֧�ִ��� 1 << 12��ͼ�� xioingfei20100401*/
    if ((width != s->avctx->usActualWidth) || (height != s->avctx->usActualHeight))
    {
        av_log(s->avctx, AV_LOG_WARNING, "width[%d] != %d or height[%d] != %d\n", width, s->avctx->usActualWidth, height, s->avctx->usActualHeight);
        /*xiongfei 20100403*/
        IMEDIA_SET_ERR_SEQ(s->avctx->iErrorCode, IMEDIA_ERR_SEQ_SIZE);
        s->avctx->iTotalError ++;
    }

    /* 2010/04/23 10:07:00 songxg+00133955 */
    /* ���Ӷ�profile, level�ĺϷ����ж� */
    /* profile SIMPLE=5 MAIN=4 HIGH=1 */
    if (!(5 == s->avctx->profile || 4 == s->avctx->profile || 1 == s->avctx->profile))
    {
        av_log(s->avctx, AV_LOG_WARNING, "profile[%d] not supported\n", s->avctx->profile);
        IMEDIA_SET_ERR_SEQ(s->avctx->iErrorCode, IMEDIA_ERR_SEQ_PROFILE_LEVEL);
        s->avctx->iTotalError++;
    }
    else
    {
        s->avctx->iActualProfile = s->avctx->profile;
    }

    /* level LOW=10 MAIN=8 HIGH_1440=6 HIGH=4 */
    if (!(10 == s->avctx->level || 8 == s->avctx->level || 6 == s->avctx->level || 4 == s->avctx->level))
    {
        av_log(s->avctx, AV_LOG_WARNING, "level[%d] not supported\n", s->avctx->level);
        IMEDIA_SET_ERR_SEQ(s->avctx->iErrorCode, IMEDIA_ERR_SEQ_PROFILE_LEVEL);
        s->avctx->iTotalError++;
    }
    else
    {
        s->avctx->iActualLevel = s->avctx->level;
    }

    s->width = s->avctx->usActualWidth;
    s->height = s->avctx->usActualHeight;

    // 	s->avctx->usActualWidth = s->width;
    // 	s->avctx->usActualHeight = s->height;
    s->avctx->iActualRefNum  = 2;

    /*��λ��չ��־ x00141957 20100511*/
    s->iExtFlag = 1;
}

static void mpeg_decode_sequence_display_extension(Mpeg1Context *s1)
{
    MpegEncContext *s = &s1->mpeg_enc_ctx;
    int color_description, w, h;

    skip_bits(&s->gb, 3); /* video format */
    color_description = get_bits1(&s->gb);

    if (color_description)
    {
        skip_bits(&s->gb, 8); /* color primaries */
        skip_bits(&s->gb, 8); /* transfer_characteristics */
        skip_bits(&s->gb, 8); /* matrix_coefficients */
    }

    w = get_bits(&s->gb, 14);
    skip_bits(&s->gb, 1); //marker
    h = get_bits(&s->gb, 14);
    skip_bits(&s->gb, 1); //marker

    s1->pan_scan.width = 16 * w;
    s1->pan_scan.height = 16 * h;

    if (s->avctx->debug & FF_DEBUG_PICT_INFO)
    { av_log(s->avctx, AV_LOG_DEBUG, "sde w:%d, h:%d\n", w, h); }
}

/*�����κμ�� x00141957 20100506 ��TI����һ��*/
static void mpeg_decode_picture_display_extension(Mpeg1Context *s1)
{
    MpegEncContext *s = &s1->mpeg_enc_ctx;
    int i, nofco;

    nofco = 1;

    if (s->progressive_sequence)
    {
        if (s->repeat_first_field)
        {
            nofco++;

            if (s->top_field_first)
            { nofco++; }
        }
    }
    else
    {
        if (s->picture_structure == PICT_FRAME)
        {
            nofco++;

            if (s->repeat_first_field)
            { nofco++; }
        }
    }

    for (i = 0; i < nofco; i++)
    {
        s1->pan_scan.position[i][0] = get_sbits(&s->gb, 16);
        skip_bits(&s->gb, 1); //marker
        s1->pan_scan.position[i][1] = get_sbits(&s->gb, 16);
        skip_bits(&s->gb, 1); //marker
    }

    if (s->avctx->debug & FF_DEBUG_PICT_INFO)
        av_log(s->avctx, AV_LOG_DEBUG, "pde (%d,%d) (%d,%d) (%d,%d)\n",
               s1->pan_scan.position[0][0], s1->pan_scan.position[0][1],
               s1->pan_scan.position[1][0], s1->pan_scan.position[1][1],
               s1->pan_scan.position[2][0], s1->pan_scan.position[2][1]
              );
}

/*�����κμ�� x00141957 20100506 ��TI����һ��*/
static void mpeg_decode_quant_matrix_extension(MpegEncContext *s)
{
    int i, v, j;

    dprintf(s->avctx, "matrix extension\n");

    if (get_bits1(&s->gb))
    {
        for (i = 0; i < 64; i++)
        {
            v = get_bits(&s->gb, 8);
            j = s->dsp.idct_permutation[ ff_zigzag_direct[i] ];
            s->intra_matrix[j] = v;
            s->chroma_intra_matrix[j] = v;
        }
    }

    if (get_bits1(&s->gb))
    {
        for (i = 0; i < 64; i++)
        {
            v = get_bits(&s->gb, 8);
            j = s->dsp.idct_permutation[ ff_zigzag_direct[i] ];
            s->inter_matrix[j] = v;
            s->chroma_inter_matrix[j] = v;
        }
    }

    if (get_bits1(&s->gb))
    {
        for (i = 0; i < 64; i++)
        {
            v = get_bits(&s->gb, 8);
            j = s->dsp.idct_permutation[ ff_zigzag_direct[i] ];
            s->chroma_intra_matrix[j] = v;
        }
    }

    if (get_bits1(&s->gb))
    {
        for (i = 0; i < 64; i++)
        {
            v = get_bits(&s->gb, 8);
            j = s->dsp.idct_permutation[ ff_zigzag_direct[i] ];
            s->chroma_inter_matrix[j] = v;
        }
    }
}

static void mpeg_decode_picture_coding_extension(Mpeg1Context *s1)
{
    MpegEncContext *s = &s1->mpeg_enc_ctx;

    s->full_pel[0] = s->full_pel[1] = 0;
    s->mpeg_f_code[0][0] = get_bits(&s->gb, 4);
    s->mpeg_f_code[0][1] = get_bits(&s->gb, 4);
    s->mpeg_f_code[1][0] = get_bits(&s->gb, 4);
    s->mpeg_f_code[1][1] = get_bits(&s->gb, 4);

    if (!s->pict_type && s1->mpeg_enc_ctx_allocated)
    {
        av_log(s->avctx, AV_LOG_WARNING, "Missing picture start code, guessing missing values\n");

        if (s->mpeg_f_code[1][0] == 15 && s->mpeg_f_code[1][1] == 15)
        {
            if (s->mpeg_f_code[0][0] == 15 && s->mpeg_f_code[0][1] == 15)
            { s->pict_type = FF_I_TYPE; }
            else
            { s->pict_type = FF_P_TYPE; }
        }
        else
        { s->pict_type = FF_B_TYPE; }

        s->current_picture.pict_type = s->pict_type;
        s->current_picture.key_frame = s->pict_type == FF_I_TYPE;
        s->first_slice = 1;
    }

    s->intra_dc_precision = get_bits(&s->gb, 2);

    /*xiongfei modified 20100331*/

    s->picture_structure = get_bits(&s->gb, 2);

    /*����Э�����Ӷ�picture_structure�ļ�� x00141957 20100506*/
    if (s->picture_structure == 0)
    {
        av_log(s->avctx, AV_LOG_WARNING, "picture_structure[%d] error !\n" , s->picture_structure);

        IMEDIA_SET_ERR_PIC(s->avctx->iErrorCode, IMEDIA_ERR_PIC_FRAME_TYPE);
        return;
    }

    s->top_field_first = get_bits1(&s->gb);
    s->frame_pred_frame_dct = get_bits1(&s->gb);
    s->concealment_motion_vectors = get_bits1(&s->gb);
    s->q_scale_type = get_bits1(&s->gb);
    s->intra_vlc_format = get_bits1(&s->gb);
    s->alternate_scan = get_bits1(&s->gb);
    s->repeat_first_field = get_bits1(&s->gb);
    s->chroma_420_type = get_bits1(&s->gb);
    s->progressive_frame = get_bits1(&s->gb);

    if (s->picture_structure == PICT_FRAME)
    {
        s->first_field = 0;
        s->v_edge_pos = 16 * s->mb_height;
    }
    else
    {
        s->first_field ^= 1;
        s->v_edge_pos =  8 * s->mb_height;
        memset(s->mbskip_table, 0, s->mb_stride * s->mb_height);
    }

    if (s->alternate_scan)
    {
        ff_init_scantable(s->dsp.idct_permutation, &s->inter_scantable  , ff_alternate_vertical_scan);
        ff_init_scantable(s->dsp.idct_permutation, &s->intra_scantable  , ff_alternate_vertical_scan);
    }
    else
    {
        ff_init_scantable(s->dsp.idct_permutation, &s->inter_scantable  , ff_zigzag_direct);
        ff_init_scantable(s->dsp.idct_permutation, &s->intra_scantable  , ff_zigzag_direct);
    }

    /* composite display not parsed */
    dprintf(s->avctx, "intra_dc_precision=%d\n", s->intra_dc_precision);
    dprintf(s->avctx, "picture_structure=%d\n", s->picture_structure);
    dprintf(s->avctx, "top field first=%d\n", s->top_field_first);
    dprintf(s->avctx, "repeat first field=%d\n", s->repeat_first_field);
    dprintf(s->avctx, "conceal=%d\n", s->concealment_motion_vectors);
    dprintf(s->avctx, "intra_vlc_format=%d\n", s->intra_vlc_format);
    dprintf(s->avctx, "alternate_scan=%d\n", s->alternate_scan);
    dprintf(s->avctx, "frame_pred_frame_dct=%d\n", s->frame_pred_frame_dct);
    dprintf(s->avctx, "progressive_frame=%d\n", s->progressive_frame);
}

/*static*/ void mpeg_decode_extension(AVCodecContext *avctx,
                                      const uint8_t *buf, int buf_size)
{
    Mpeg1Context *s1 = avctx->priv_data;
    MpegEncContext *s = &s1->mpeg_enc_ctx;
    int ext_type;

    init_get_bits(&s->gb, buf, buf_size * 8);

    ext_type = get_bits(&s->gb, 4);

    switch (ext_type)
    {
        case 0x1:
            mpeg_decode_sequence_extension(s1);
            break;

        case 0x2:
            mpeg_decode_sequence_display_extension(s1);
            break;

        case 0x3:
            mpeg_decode_quant_matrix_extension(s);
            break;

        case 0x7:
            mpeg_decode_picture_display_extension(s1);
            break;

        case 0x8:
            mpeg_decode_picture_coding_extension(s1);
            break;
    }
}

static void exchange_uv(MpegEncContext *s)
{
    DCTELEM (*tmp)[64];

    tmp           = s->pblocks[4];
    s->pblocks[4] = s->pblocks[5];
    s->pblocks[5] = tmp;
}

static int mpeg_field_start(MpegEncContext *s, const uint8_t *buf, int buf_size)
{
    AVCodecContext *avctx = s->avctx;
    Mpeg1Context *s1 = (Mpeg1Context *)s;

    /* start frame decoding */
    if (s->first_field || s->picture_structure == PICT_FRAME)
    {
        if (MPV_frame_start(s, avctx) < 0)
        { return -1; }

        /*һ֡�������س�ʼ�� xiongfei20100222*/
        ff_er_frame_start(s);

        /* first check if we must repeat the frame */
        /*repeat_pict ʲô��˼�� xiongfei20100222*/
        s->current_picture_ptr->repeat_pict = 0;

        if (s->repeat_first_field)
        {
            if (s->progressive_sequence)
            {
                if (s->top_field_first)
                { s->current_picture_ptr->repeat_pict = 4; }
                else
                { s->current_picture_ptr->repeat_pict = 2; }
            }
            else if (s->progressive_frame)
            {
                s->current_picture_ptr->repeat_pict = 1;
            }
        }

        *s->current_picture_ptr->pan_scan = s1->pan_scan;
    }
    else   //second field
    {
        int i;

        if (!s->current_picture_ptr)
        {
            av_log(s->avctx, AV_LOG_WARNING, "first field missing\n");
            return -1;
        }

        for (i = 0; i < 4; i++)
        {
            s->current_picture.data[i] = s->current_picture_ptr->data[i];

            if (s->picture_structure == PICT_BOTTOM_FIELD)
            {
                s->current_picture.data[i] += s->current_picture_ptr->linesize[i];
            }
        }
    }

    if (avctx->hwaccel)
    {
        if (avctx->hwaccel->start_frame(avctx, buf, buf_size) < 0)
        { return -1; }
    }

    // MPV_frame_start will call this function too,
    // but we need to call it on every field
    //xiongfei20102011
    //     if(CONFIG_MPEG_XVMC_DECODER && s->avctx->xvmc_acceleration)
    //         if(ff_xvmc_field_start(s,avctx) < 0)
    //             return -1;

    return 0;
}

#define DECODE_SLICE_ERROR -1
#define DECODE_SLICE_OK 0

/**
 * decodes a slice. MpegEncContext.mb_y must be set to the MB row from the startcode
 * @return DECODE_SLICE_ERROR if the slice is damaged<br>
 *         DECODE_SLICE_OK if this slice is ok<br>
 */
static int mpeg_decode_slice(Mpeg1Context *s1, int mb_y,
                             const uint8_t **buf, int buf_size)
{
    MpegEncContext *s = &s1->mpeg_enc_ctx;
    AVCodecContext *avctx = s->avctx;
    const int field_pic = s->picture_structure != PICT_FRAME;
    const int lowres = s->avctx->lowres;

    s->resync_mb_x =
        s->resync_mb_y = -1;

    /*�鿴�Ƿ񳬹�һ֡ xiongfei20100222*/
    /*���Ӵ����� xiongfei20100317*/
    if (mb_y << field_pic >= s->mb_height)
    {
        av_log(s->avctx, AV_LOG_WARNING, "slice below image (%d >= %d)\n", mb_y, s->mb_height);
        IMEDIA_SET_ERR_SLICE(avctx->iErrorCode, IMEDIA_ERR_SLICE_ADDR);
        s->avctx->iTotalError ++;
        return -1;
    }

    init_get_bits(&s->gb, *buf, buf_size * 8);

    /*��ʼ��dcԤ��ֵ ����pmvֵ�ڴ����� xiongfei20100222*/
    ff_mpeg1_clean_buffers(s);

    s->interlaced_dct = 0;

    /*��õ�ǰslice qscale xiongfei20100222*/
    s->qscale = get_qscale(s);

    /*���Ӵ����� xiongfei20100317*/
    if (s->qscale == 0)
    {
        av_log(s->avctx, AV_LOG_WARNING, "qscale == 0\n");
        IMEDIA_SET_ERR_SLICE(avctx->iErrorCode, IMEDIA_ERR_SLICE_QP);
        s->avctx->iTotalError ++;
        //return -1;
        s->qscale = 16;
    }

    /* extra slice info */
    while (get_bits1(&s->gb) != 0)
    {
        skip_bits(&s->gb, 8);
    }

    s->mb_x = 0;

    /*Ӳ�����ٲ��� xiongfei20100222*/
    if (avctx->hwaccel)
    {
        const uint8_t *buf_end, *buf_start = *buf - 4; /* include start_code */
        int start_code = -1;
        buf_end = ff_find_start_code(buf_start + 2, *buf + buf_size, &start_code);

        if (buf_end < *buf + buf_size)
        { buf_end -= 4; }

        s->mb_y = mb_y;

        if (avctx->hwaccel->decode_slice(avctx, buf_start, buf_end - buf_start) < 0)
        { return DECODE_SLICE_ERROR; }

        *buf = buf_end;
        return DECODE_SLICE_OK;
    }

    for (;;)
    {
        /*δ�����˴����� xiongfei20100222*/
        int code = get_vlc2(&s->gb, mbincr_vlc.table, MBINCR_VLC_BITS, 2);

        /*���Ӵ����� xiongfei20100317*/
        if (code < 0)
            /*xiongfei20100331modified*/
            /*if(code <= 0)*/
        {
            av_log(s->avctx, AV_LOG_WARNING, "first mb_incr damaged\n");
            IMEDIA_SET_ERR_SLICE(avctx->iErrorCode, IMEDIA_ERR_SLICE_VLC);
            s->avctx->iTotalError ++;
            return -1;
        }

        if (code >= 33)
        {
            if (code == 33)
            {
                s->mb_x += 33;
            }

            /* otherwise, stuffing, nothing to do */
        }
        else
        {
            /*xiongfei20100331*/
            s->mb_x += code;
            break;
            // 			av_log(s->avctx, AV_LOG_WARNING, "first mb_incr damaged\n");
            // 			IMEDIA_SET_ERR_SLICE(avctx->iErrorCode, IMEDIA_ERR_SLICE_VLC);
            // 			return -1;
        }
    }

    /*���Ӵ����� xiongfei20100317*/
    if (s->mb_x >= s->mb_width)
    {
        av_log(s->avctx, AV_LOG_WARNING, "initial skip overflow\n");
        IMEDIA_SET_ERR_SLICE(avctx->iErrorCode, IMEDIA_ERR_SLICE_NUM);
        s->avctx->iTotalError ++;
        return -1;
    }

    s->resync_mb_x = s->mb_x;
    s->resync_mb_y = s->mb_y = mb_y;
    s->mb_skip_run = 0;

    /*��ʼ��һЩblock��Ϣ�Լ�yuv�ؽ�֡��ŵ�ַ xiongfei20100222*/
    ff_init_block_index(s);

    if (s->mb_y == 0 && s->mb_x == 0 && (s->first_field || s->picture_structure == PICT_FRAME))
    {
        if (s->avctx->debug & FF_DEBUG_PICT_INFO)
        {
            av_log(s->avctx, AV_LOG_DEBUG, "qp:%d fc:%2d%2d%2d%2d %s %s %s %s %s dc:%d pstruct:%d fdct:%d cmv:%d qtype:%d ivlc:%d rff:%d %s\n",
                   s->qscale, s->mpeg_f_code[0][0], s->mpeg_f_code[0][1], s->mpeg_f_code[1][0], s->mpeg_f_code[1][1],
                   s->pict_type == FF_I_TYPE ? "I" : (s->pict_type == FF_P_TYPE ? "P" : (s->pict_type == FF_B_TYPE ? "B" : "S")),
                   s->progressive_sequence ? "ps" : "", s->progressive_frame ? "pf" : "", s->alternate_scan ? "alt" : "", s->top_field_first ? "top" : "",
                   s->intra_dc_precision, s->picture_structure, s->frame_pred_frame_dct, s->concealment_motion_vectors,
                   s->q_scale_type, s->intra_vlc_format, s->repeat_first_field, s->chroma_420_type ? "420" : "");
        }
    }

    /*�������ѭ��������slice��ÿһ����� xiongfei20100222*/
    for (;;)
    {
        //If 1, we memcpy blocks in xvmcvideo.
        /*xiongfei20100210*/
        if (CONFIG_MPEG_XVMC_DECODER && s->avctx->xvmc_acceleration > 1)
            /*ff_xvmc_init_block(s)*/;//set s->block

        /*�������ÿһ����� ,��������ֻ�������������� xiongfei20100222*/
        /*���Ӵ�ӡ������־ xiongfei20100317*/
        if (mpeg_decode_mb(s, s->block) < 0)
        {
            av_log(avctx, AV_LOG_WARNING, "mpeg_decode_mb failed !\n" );
            return -1;
        }

        /*xiongfei modified 20100402*/
        /*����Ҫ������һ�� ���д������أ��������ڴ�д����Խ�� xiongfei20100403*/
        if (0)
            /*if(s->current_picture.motion_val[0] && !s->encoding)*/  //note motion_val is normally NULL unless we want to extract the MVs
        {
            const int wrap = field_pic ? 2 * s->b8_stride : s->b8_stride;
            int xy = s->mb_x * 2 + s->mb_y * 2 * wrap;
            int motion_x, motion_y, dir, i;

            if (field_pic && !s->first_field)
            { xy += wrap / 2; }

            for (i = 0; i < 2; i++)
            {
                for (dir = 0; dir < 2; dir++)
                {
                    if (s->mb_intra || (dir == 1 && s->pict_type != FF_B_TYPE))
                    {
                        motion_x = motion_y = 0;
                    }
                    else if (s->mv_type == MV_TYPE_16X16 || (s->mv_type == MV_TYPE_FIELD && field_pic))
                    {
                        motion_x = s->mv[dir][0][0];
                        motion_y = s->mv[dir][0][1];
                    }
                    else /*if ((s->mv_type == MV_TYPE_FIELD) || (s->mv_type == MV_TYPE_16X8))*/
                    {
                        motion_x = s->mv[dir][i][0];
                        motion_y = s->mv[dir][i][1];
                    }

                    s->current_picture.motion_val[dir][xy    ][0] = motion_x;
                    s->current_picture.motion_val[dir][xy    ][1] = motion_y;
                    s->current_picture.motion_val[dir][xy + 1][0] = motion_x;
                    s->current_picture.motion_val[dir][xy + 1][1] = motion_y;
                    s->current_picture.ref_index [dir][xy    ] =
                        s->current_picture.ref_index [dir][xy + 1] = s->field_select[dir][i];
                    assert(s->field_select[dir][i] == 0 || s->field_select[dir][i] == 1);
                }

                xy += wrap;
            }
        }

        /*ָ�������� y:16 uv:8 xiongfei20100222*/
        s->dest[0] += 16 >> lowres;
        s->dest[1] += (16 >> lowres) >> s->chroma_x_shift;
        s->dest[2] += (16 >> lowres) >> s->chroma_x_shift;

        /*�˴����������ؽ������а���mc,idct xiongfei20100222*/
        MPV_decode_mb(s, s->block);

        if (++s->mb_x >= s->mb_width)
        {
            const int mb_size = 16 >> s->avctx->lowres;

            /*������һ������� xiongfei20100222*/
            ff_draw_horiz_band(s, mb_size * s->mb_y, mb_size);

            s->mb_x = 0;
            s->mb_y++;

            if (s->mb_y << field_pic >= s->mb_height)
            {
                int left = s->gb.size_in_bits - get_bits_count(&s->gb);
                int is_d10 = s->chroma_format == 2 && s->pict_type == FF_I_TYPE && avctx->profile == 0 && avctx->level == 5
                             && s->intra_dc_precision == 2 && s->q_scale_type == 1 && s->alternate_scan == 0
                             && s->progressive_frame == 0 /* vbv_delay == 0xBBB || 0xE10*/;

                /*���Ӵ����� xiongfei20100317*/
                if (left < 0 || (left && show_bits(&s->gb, FFMIN(left, 23)) && !is_d10)
                    || (avctx->error_recognition >= FF_ER_AGGRESSIVE && left > 8))
                {
                    av_log(avctx, AV_LOG_WARNING, "end mismatch left=%d %0X\n", left, show_bits(&s->gb, FFMIN(left, 23)));
                    IMEDIA_SET_ERR_SLICE(avctx->iErrorCode, IMEDIA_ERR_SLICE_BITS);
                    s->avctx->iTotalError ++;
                    return -1;
                }
                else
                { goto eos; }
            }

            ff_init_block_index(s);
        }

        /* skip mb handling */
        if (s->mb_skip_run == -1)
        {
            /* read increment again */
            s->mb_skip_run = 0;

            for (;;)
            {
                int code = get_vlc2(&s->gb, mbincr_vlc.table, MBINCR_VLC_BITS, 2);

                /*���Ӵ����� xiongfei20100317*/
                if (code < 0)
                    /*xiongfei20100331modified*/
                    /*if(code <= 0)*/
                {
                    av_log(s->avctx, AV_LOG_WARNING, "mb incr damaged\n");
                    IMEDIA_SET_ERR_SLICE(avctx->iErrorCode, IMEDIA_ERR_SLICE_VLC);
                    s->avctx->iTotalError ++;
                    return -1;
                }

                if (code >= 33)
                {
                    if (code == 33)
                    {
                        s->mb_skip_run += 33;
                    }
                    else if (code == 35)
                    {
                        /*���Ӵ����� xiongfei20100317*/
                        if (s->mb_skip_run != 0 || show_bits(&s->gb, 15) != 0)
                        {
                            av_log(s->avctx, AV_LOG_WARNING, "slice mismatch\n");
                            IMEDIA_SET_ERR_SLICE(avctx->iErrorCode, IMEDIA_ERR_SLICE_TYPE);
                            s->avctx->iTotalError ++;
                            return -1;
                        }

                        goto eos; /* end of slice */
                    }

                    /*xiongfei20100331*/
                    // 					{
                    // 						if(code > 33)
                    // 						{
                    // 							av_log(s->avctx, AV_LOG_WARNING, "first mb_incr damaged\n");
                    // 							IMEDIA_SET_ERR_SLICE(avctx->iErrorCode, IMEDIA_ERR_SLICE_VLC);
                    // 							return -1;
                    // 						}
                    // 					}
                    /* otherwise, stuffing, nothing to do */
                }
                else
                {
                    s->mb_skip_run += code;
                    break;
                }
            }

            if (s->mb_skip_run)
            {
                int i;

                /*���Ӵ����� xiongfei20100317*/
                if (s->pict_type == FF_I_TYPE)
                {
                    av_log(s->avctx, AV_LOG_WARNING, "skipped MB in I frame at %d %d\n", s->mb_x, s->mb_y);
                    IMEDIA_SET_ERR_SLICE(avctx->iErrorCode, IMEDIA_ERR_SLICE_OTHER);
                    s->avctx->iTotalError ++;
                    return -1;
                }

                /* skip mb */
                s->mb_intra = 0;

                for (i = 0; i < 12; i++)
                { s->block_last_index[i] = -1; }

                if (s->picture_structure == PICT_FRAME)
                { s->mv_type = MV_TYPE_16X16; }
                else
                { s->mv_type = MV_TYPE_FIELD; }

                if (s->pict_type == FF_P_TYPE)
                {
                    /* if P type, zero motion vector is implied */
                    s->mv_dir = MV_DIR_FORWARD;
                    s->mv[0][0][0] = s->mv[0][0][1] = 0;
                    s->last_mv[0][0][0] = s->last_mv[0][0][1] = 0;
                    s->last_mv[0][1][0] = s->last_mv[0][1][1] = 0;
                    s->field_select[0][0] = s->picture_structure - 1;
                }
                else
                {
                    /* if B type, reuse previous vectors and directions */
                    s->mv[0][0][0] = s->last_mv[0][0][0];
                    s->mv[0][0][1] = s->last_mv[0][0][1];
                    s->mv[1][0][0] = s->last_mv[1][0][0];
                    s->mv[1][0][1] = s->last_mv[1][0][1];
                }
            }
        }
    }

eos: // end of slice
    *buf += (get_bits_count(&s->gb) - 1) / 8;
    return 0;
}

static int slice_decode_thread(AVCodecContext *c, void *arg)
{
    MpegEncContext *s = *(void **)arg;
    const uint8_t *buf = s->gb.buffer;
    int mb_y = s->start_mb_y;

    s->error_count = 3 * (s->end_mb_y - s->start_mb_y) * s->mb_width;

    for (;;)
    {
        uint32_t start_code;
        int ret;

        ret = mpeg_decode_slice((Mpeg1Context *)s, mb_y, &buf, s->gb.buffer_end - buf);
        emms_c();

        //av_log(c, AV_LOG_DEBUG, "ret:%d resync:%d/%d mb:%d/%d ts:%d/%d ec:%d\n",
        //ret, s->resync_mb_x, s->resync_mb_y, s->mb_x, s->mb_y, s->start_mb_y, s->end_mb_y, s->error_count);
        if (ret < 0)
        {
            if (s->resync_mb_x >= 0 && s->resync_mb_y >= 0)
            { ff_er_add_slice(s, s->resync_mb_x, s->resync_mb_y, s->mb_x, s->mb_y, AC_ERROR | DC_ERROR | MV_ERROR); }
        }
        else
        {
            ff_er_add_slice(s, s->resync_mb_x, s->resync_mb_y, s->mb_x - 1, s->mb_y, AC_END | DC_END | MV_END);
        }

        if (s->mb_y == s->end_mb_y)
        { return 0; }

        start_code = -1;
        buf = ff_find_start_code(buf, s->gb.buffer_end, &start_code);
        mb_y = start_code - SLICE_MIN_START_CODE;

        if (mb_y < 0 || mb_y >= s->end_mb_y)
        { return -1; }
    }

    return 0; //not reached
}

/**
 * Handles slice ends.
 * @return 1 if it seems to be the last slice
 */
static int slice_end(AVCodecContext *avctx, AVFrame *pict)
{
    Mpeg1Context *s1 = avctx->priv_data;
    MpegEncContext *s = &s1->mpeg_enc_ctx;

    if (!s1->mpeg_enc_ctx_allocated || !s->current_picture_ptr)
    { return 0; }

    if (s->avctx->hwaccel)
    {
        if (s->avctx->hwaccel->end_frame(s->avctx) < 0)
        { av_log(avctx, AV_LOG_ERROR, "hardware accelerator failed to decode picture\n"); }
    }

    //xiongfei201020211
    //     if(CONFIG_MPEG_XVMC_DECODER && s->avctx->xvmc_acceleration)
    //         ff_xvmc_field_end(s);

    /* end of slice reached */
    if (/*s->mb_y<<field_pic == s->mb_height &&*/ !s->first_field)
    {
        /* end of image */

        s->current_picture_ptr->qscale_type = FF_QSCALE_TYPE_MPEG2;

        ff_er_frame_end(s);

        MPV_frame_end(s);

        if (s->pict_type == FF_B_TYPE || s->low_delay)
        {
            *pict = *(AVFrame *)s->current_picture_ptr;
            ff_print_debug_info(s, pict);
        }
        else
        {
            s->picture_number++;

            /* latency of 1 frame for I- and P-frames */
            /* XXX: use another variable than picture_number */
            if (s->last_picture_ptr != NULL)
            {
                *pict = *(AVFrame *)s->last_picture_ptr;
                ff_print_debug_info(s, pict);
            }
        }

        return 1;
    }
    else
    {
        return 0;
    }
}

/*static*/ int mpeg1_decode_sequence(AVCodecContext *avctx,
                                     const uint8_t *buf, int buf_size)
{
    Mpeg1Context *s1 = avctx->priv_data;
    MpegEncContext *s = &s1->mpeg_enc_ctx;
    int width, height;
    int i, v, j;

    init_get_bits(&s->gb, buf, buf_size * 8);

    width = get_bits(&s->gb, 12);
    height = get_bits(&s->gb, 12);

    /*��ӡ������־�����Ӵ����� xiongfei20100317*/
    if (width < MPEG2_MIN_WIDTH || width > MPEG2_MAX_WIDTH || height < MPEG2_MIN_HEIGHT || height > MPEG2_MAX_HEIGHT)
    {
        av_log(avctx, AV_LOG_WARNING, "size is wrong width[%d] height[%d ]\n" , width, height);
        IMEDIA_SET_ERR_SEQ( avctx->iErrorCode, IMEDIA_ERR_SEQ_SIZE);
        s->avctx->iTotalError ++;
        return -1;
    }

    s->aspect_ratio_info = get_bits(&s->gb, 4);

    /*xiongfei 20100505�޸� �����ϸ����*/
    if ((s->aspect_ratio_info <= 0) || (s->aspect_ratio_info >= 5))
    {
        av_log(avctx, AV_LOG_WARNING, "aspect ratio has forbidden value\n");
        /*���Ӵ����� xiongfei20100317*/
        //if (avctx->error_recognition >= FF_ER_COMPLIANT)
        {
            IMEDIA_SET_ERR_SEQ(avctx->iErrorCode, IMEDIA_ERR_SEQ_SIZE);
            s->avctx->iTotalError ++;
            //return -1;
        }
    }

    s->frame_rate_index = get_bits(&s->gb, 4);

    /*���Ӵ�ӡ������־�����Ӵ����� xiongfei20100317*/
    if (s->frame_rate_index <= 0 || s->frame_rate_index > 8)
    {
        av_log(avctx, AV_LOG_WARNING, "frame_rate_index[%d] is wrong !\n" , s->frame_rate_index);
        IMEDIA_SET_ERR_SEQ(avctx->iErrorCode, IMEDIA_ERR_SEQ_TIME_FRAMERATE);
        s->avctx->iTotalError ++;
        //return -1;
    }

    s->bit_rate = get_bits(&s->gb, 18) * 400;

    /*���Ӵ�ӡ������־�����Ӵ����� �����ӷ��� xioingfei20100317*/
    if (get_bits1(&s->gb) == 0) /* marker */
    {
        av_log(avctx, AV_LOG_WARNING, "marker bit [%d] is wrong !\n");
        IMEDIA_SET_ERR_SEQ(avctx->iErrorCode, IMEDIA_ERR_SEQ_MARKER);
        s->avctx->iTotalError ++;
        //return -1;
    }

    s->avctx->rc_buffer_size = get_bits(&s->gb, 10) * 1024 * 16;

    /*x00141957 20100505 ����Э���޸����ϸ���*/
    //skip_bits(&s->gb, 1);
    if (get_bits1(&s->gb) == 1) /* constrained_parameters_flag */
    {
        av_log(avctx, AV_LOG_WARNING, "constrained_parameters_flag is wrong !\n");
        IMEDIA_SET_ERR_SEQ(avctx->iErrorCode, IMEDIA_ERR_SEQ_MARKER);
        s->avctx->iTotalError ++;
        //return -1;
    }

    /* get matrix */
    if (get_bits1(&s->gb))
    {
        for (i = 0; i < 64; i++)
        {
            v = get_bits(&s->gb, 8);

            /*���Ӵ����� xioingfei20100317*/
            if (v == 0)
            {
                av_log(s->avctx, AV_LOG_WARNING, "intra matrix damaged\n");
                IMEDIA_SET_ERR_SEQ(avctx->iErrorCode, IMEDIA_ERR_SEQ_QUANT);
                s->avctx->iTotalError ++;
                return -1;
            }

            j = s->dsp.idct_permutation[ ff_zigzag_direct[i] ];
            s->intra_matrix[j] = v;
            s->chroma_intra_matrix[j] = v;
        }

#ifdef DEBUG
        dprintf(s->avctx, "intra matrix present\n");

        for (i = 0; i < 64; i++)
        { dprintf(s->avctx, " %d", s->intra_matrix[s->dsp.idct_permutation[i]]); }

        dprintf(s->avctx, "\n");
#endif
    }
    else
    {
        for (i = 0; i < 64; i++)
        {
            j = s->dsp.idct_permutation[i];
            v = ff_mpeg1_default_intra_matrix[i];
            s->intra_matrix[j] = v;
            s->chroma_intra_matrix[j] = v;
        }
    }

    if (get_bits1(&s->gb))
    {
        for (i = 0; i < 64; i++)
        {
            v = get_bits(&s->gb, 8);

            /*���Ӵ����� xioingfei20100317*/
            if (v == 0)
            {
                av_log(s->avctx, AV_LOG_WARNING, "intra matrix damaged\n");
                IMEDIA_SET_ERR_SEQ(avctx->iErrorCode, IMEDIA_ERR_SEQ_QUANT);
                s->avctx->iTotalError ++;
                return -1;
            }

            j = s->dsp.idct_permutation[ ff_zigzag_direct[i] ];
            s->inter_matrix[j] = v;
            s->chroma_inter_matrix[j] = v;
        }

#ifdef DEBUG
        dprintf(s->avctx, "non-intra matrix present\n");

        for (i = 0; i < 64; i++)
        { dprintf(s->avctx, " %d", s->inter_matrix[s->dsp.idct_permutation[i]]); }

        dprintf(s->avctx, "\n");
#endif
    }
    else
    {
        for (i = 0; i < 64; i++)
        {
            int j = s->dsp.idct_permutation[i];
            v = ff_mpeg1_default_non_intra_matrix[i];
            s->inter_matrix[j] = v;
            s->chroma_inter_matrix[j] = v;
        }
    }

    /*���Ӵ����� xioingfei20100317*/
    if (show_bits(&s->gb, 23) != 0)
    {
        av_log(s->avctx, AV_LOG_WARNING, "sequence header damaged\n");
        IMEDIA_SET_ERR_SEQ(avctx->iErrorCode, IMEDIA_ERR_SEQ_OTHER);
        s->avctx->iTotalError ++;
        //return -1;
    }

    /* we set MPEG-2 parameters so that it emulates MPEG-1 */
    s->progressive_sequence = 1;
    s->progressive_frame = 1;
    s->picture_structure = PICT_FRAME;
    s->frame_pred_frame_dct = 1;
    s->chroma_format = 1;
    s->codec_id = s->avctx->codec_id = CODEC_ID_MPEG1VIDEO;
    avctx->sub_id = 1; /* indicates MPEG-1 */
    s->out_format = FMT_MPEG1;
    s->swap_uv = 0;//AFAIK VCR2 does not have SEQ_HEADER

    if (s->flags & CODEC_FLAG_LOW_DELAY) { s->low_delay = 1; }

    if (s->avctx->debug & FF_DEBUG_PICT_INFO)
    {
        av_log(s->avctx, AV_LOG_DEBUG, "vbv buffer: %d, bitrate:%d\n",
               s->avctx->rc_buffer_size, s->bit_rate);
    }

    /*�������д���Ϊ���� ����Ϊmpeg2���ߵ���Ϣ��ֻ������ͷ�и���������ʵ�ʿ���ֵֻ��������õ� xiongfei20100303*/
    /*���ǵ����ǵĽ�������֧���м�任�ֱ��ʣ������Ե�һ����������ͼ�����Ϊ�� xiongfei20100401*/
    if ((0 == avctx->usActualWidth) || (0 == avctx->usActualHeight))
    {
        avctx->usActualWidth = width;
        avctx->usActualHeight = height;
    }

    s->width = avctx->usActualWidth;
    s->height = avctx->usActualHeight;

    return 0;
}

static int vcr2_init_sequence(AVCodecContext *avctx)
{
    Mpeg1Context *s1 = avctx->priv_data;
    MpegEncContext *s = &s1->mpeg_enc_ctx;
    int i, v;

    /* start new MPEG-1 context decoding */
    s->out_format = FMT_MPEG1;

    if (s1->mpeg_enc_ctx_allocated)
    {
        MPV_common_end(s);
    }

    s->width  = avctx->coded_width;
    s->height = avctx->coded_height;
    avctx->has_b_frames = 0; //true?
    s->low_delay = 1;

    avctx->pix_fmt = mpeg_get_pixelformat(avctx);
    avctx->hwaccel = ff_find_hwaccel(avctx->codec->id, avctx->pix_fmt);

    if ( avctx->pix_fmt == PIX_FMT_XVMC_MPEG2_IDCT || avctx->hwaccel ||
         s->avctx->codec->capabilities & CODEC_CAP_HWACCEL_VDPAU )
        if ( avctx->idct_algo == FF_IDCT_AUTO )
        { avctx->idct_algo = FF_IDCT_SIMPLE; }

    if (MPV_common_init(s) < 0)
    { return -1; }

    exchange_uv(s);//common init reset pblocks, so we swap them here
    s->swap_uv = 1;// in case of xvmc we need to swap uv for each MB
    s1->mpeg_enc_ctx_allocated = 1;

    for (i = 0; i < 64; i++)
    {
        int j = s->dsp.idct_permutation[i];
        v = ff_mpeg1_default_intra_matrix[i];
        s->intra_matrix[j] = v;
        s->chroma_intra_matrix[j] = v;

        v = ff_mpeg1_default_non_intra_matrix[i];
        s->inter_matrix[j] = v;
        s->chroma_inter_matrix[j] = v;
    }

    s->progressive_sequence = 1;
    s->progressive_frame = 1;
    s->picture_structure = PICT_FRAME;
    s->frame_pred_frame_dct = 1;
    s->chroma_format = 1;
    s->codec_id = s->avctx->codec_id = CODEC_ID_MPEG2VIDEO;
    avctx->sub_id = 2; /* indicates MPEG-2 */
    return 0;
}


/*static*/ void mpeg_decode_user_data(AVCodecContext *avctx,
                                      const uint8_t *buf, int buf_size)
{
    const uint8_t *p;
    int len, flags;
    p = buf;
    len = buf_size;

    /* we parse the DTG active format information */
    if (len >= 5 &&
        p[0] == 'D' && p[1] == 'T' && p[2] == 'G' && p[3] == '1')
    {
        flags = p[4];
        p += 5;
        len -= 5;

        if (flags & 0x80)
        {
            /* skip event id */
            if (len < 2)
            { return; }

            p += 2;
            len -= 2;
        }

        if (flags & 0x40)
        {
            if (len < 1)
            { return; }

            avctx->dtg_active_format = p[0] & 0x0f;
        }
    }
}

static void mpeg_decode_gop(AVCodecContext *avctx,
                            const uint8_t *buf, int buf_size)
{
    Mpeg1Context *s1 = avctx->priv_data;
    MpegEncContext *s = &s1->mpeg_enc_ctx;

    int drop_frame_flag;
    int time_code_hours, time_code_minutes;
    int time_code_seconds, time_code_pictures;
    int closed_gop, broken_link;

    init_get_bits(&s->gb, buf, buf_size * 8);

    drop_frame_flag = get_bits1(&s->gb);

    time_code_hours = get_bits(&s->gb, 5);
    time_code_minutes = get_bits(&s->gb, 6);
    skip_bits1(&s->gb);//marker bit
    time_code_seconds = get_bits(&s->gb, 6);
    time_code_pictures = get_bits(&s->gb, 6);

    closed_gop  = get_bits1(&s->gb);
    /*broken_link indicate that after editing the
      reference frames of the first B-Frames after GOP I-Frame
      are missing (open gop)*/
    broken_link = get_bits1(&s->gb);

    if (s->avctx->debug & FF_DEBUG_PICT_INFO)
        av_log(s->avctx, AV_LOG_DEBUG, "GOP (%2d:%02d:%02d.[%02d]) closed_gop=%d broken_link=%d\n",
               time_code_hours, time_code_minutes, time_code_seconds,
               time_code_pictures, closed_gop, broken_link);
}
/**
 * Finds the end of the current frame in the bitstream.
 * @return the position of the first byte of the next frame, or -1
 */
int ff_mpeg1_find_frame_end(ParseContext *pc, const uint8_t *buf, int buf_size)
{
    int i;
    uint32_t state = pc->state;

    /* EOF considered as end of frame */
    if (buf_size == 0)
    { return 0; }

    /*
     0  frame start         -> 1/4
     1  first_SEQEXT        -> 0/2
     2  first field start   -> 3/0
     3  second_SEQEXT       -> 2/0
     4  searching end
    */

    for (i = 0; i < buf_size; i++)
    {
        assert(pc->frame_start_found >= 0 && pc->frame_start_found <= 4);

        if (pc->frame_start_found & 1)
        {
            if (state == EXT_START_CODE && (buf[i] & 0xF0) != 0x80)
            { pc->frame_start_found--; }
            else if (state == EXT_START_CODE + 2)
            {
                if ((buf[i] & 3) == 3) { pc->frame_start_found = 0; }
                else                { pc->frame_start_found = (pc->frame_start_found + 1) & 3; }
            }

            state++;
        }
        else
        {
            i = ff_find_start_code(buf + i, buf + buf_size, &state) - buf - 1;

            if (pc->frame_start_found == 0 && state >= SLICE_MIN_START_CODE && state <= SLICE_MAX_START_CODE)
            {
                i++;
                pc->frame_start_found = 4;
            }

            if (state == SEQ_END_CODE)
            {
                pc->state = -1;
                return i + 1;
            }

            if (pc->frame_start_found == 2 && state == SEQ_START_CODE)
            { pc->frame_start_found = 0; }

            if (pc->frame_start_found < 4 && state == EXT_START_CODE)
            { pc->frame_start_found++; }

            if (pc->frame_start_found == 4 && (state & 0xFFFFFF00) == 0x100)
            {
                if (state < SLICE_MIN_START_CODE || state > SLICE_MAX_START_CODE)
                {
                    pc->frame_start_found = 0;
                    pc->state = -1;
                    return i - 3;
                }
            }
        }
    }

    pc->state = state;
    return END_NOT_FOUND;
}

static int decode_chunks(AVCodecContext *avctx,
                         AVFrame *picture, int *data_size,
                         const uint8_t *buf, int buf_size);

/* handle buffering and image synchronisation */
static int mpeg_decode_frame(AVCodecContext *avctx,
                             void *data, int *data_size,
                             const uint8_t *buf, int buf_size)
{
    Mpeg1Context *s = avctx->priv_data;
    AVFrame *picture = data;
    MpegEncContext *s2 = &s->mpeg_enc_ctx;
    int ret = 0;
    dprintf(avctx, "fill_buffer\n");

    /*��ʱ�Ѿ��ľ�������,buf_size��ʾ�����б�����Ŀ xiongfei20100221*/
    if (buf_size == 0 || (buf_size == 4 && AV_RB32(buf) == SEQ_END_CODE))
    {
        /* special case for last picture */
        if (s2->low_delay == 0 && s2->next_picture_ptr)
        {
            *picture = *(AVFrame *)s2->next_picture_ptr;
            s2->next_picture_ptr = NULL;

            *data_size = sizeof(AVFrame);
            /* 2010/06/11 10:30:00 liuxw+00139685 */
            /* ��λ���һ֡�ı�־ */
            picture->ucLastFrame = 1;
        }

        return buf_size;
    }

    if (s2->flags & CODEC_FLAG_TRUNCATED)
    {
        int next = ff_mpeg1_find_frame_end(&s2->parse_context, buf, buf_size);

        if ( ff_combine_frame(&s2->parse_context, next, (const uint8_t **)&buf, &buf_size) < 0 )
        { return buf_size; }
    }

#if 0

    if (s->repeat_field % 2 == 1)
    {
        s->repeat_field++;

        //fprintf(stderr,"\nRepeating last frame: %d -> %d! pict: %d %d", avctx->frame_number-1, avctx->frame_number,
        //        s2->picture_number, s->repeat_field);
        if (avctx->flags & CODEC_FLAG_REPEAT_FIELD)
        {
            *data_size = sizeof(AVPicture);
            goto the_end;
        }
    }

#endif

    if (s->mpeg_enc_ctx_allocated == 0 && avctx->codec_tag == AV_RL32("VCR2"))
    { vcr2_init_sequence(avctx); }

    s->slice_count = 0;

    /* ������extradata xiongfei20100220*/

    //     if(avctx->extradata && !avctx->frame_number)
    //         decode_chunks(avctx, picture, data_size, avctx->extradata, avctx->extradata_size);
    if (avctx->extradata_num && !avctx->frame_number)
    {
        int i;

        for (i = 0; i < avctx->extradata_num; i++)
        {
            decode_chunks(avctx, picture, data_size, avctx->extradata[i], avctx->extradata_size[i]);
        }
    }

    /*x00141957 20100511 ����ֵ�޸��Ա�֤����ȫ��������*/
    ret = decode_chunks(avctx, picture, data_size, buf, buf_size);

    /*x00141957 20100726��õ�ǰ������aspect ratio info*/
    /*��¼������sar x00141957 20100726*/
    //mpeg12_get_sar(s2,avctx->sample_aspect_ratio.den,avctx->sample_aspect_ratio.num);

    if (-1 >= ret)
    {
        ret = 1;
    }

    /*x00141957 20100513���� */
    avctx->frame_number++;

    return ret;
}

static int decode_chunks(AVCodecContext *avctx,
                         AVFrame *picture, int *data_size,
                         const uint8_t *buf, int buf_size)
{
    Mpeg1Context *s = avctx->priv_data;
    MpegEncContext *s2 = &s->mpeg_enc_ctx;
    const uint8_t *buf_ptr = buf;
    const uint8_t *buf_end = buf + buf_size;
    int ret, input_size;

    s2->picture_structure = 0;
    s2->pict_type = 0;

    for (;;)
    {
        /* find next start code */
        uint32_t start_code = -1;
        buf_ptr = ff_find_start_code(buf_ptr, buf_end, &start_code);

        /*���û���ҵ���ʼ�룬�������֡���ݽ��� xiongfei20100221*/
        if (start_code > 0x1ff)
        {
            if (s2->pict_type != FF_B_TYPE || avctx->skip_frame <= AVDISCARD_DEFAULT)
            {
                if (avctx->thread_count > 1)
                {
                    int i;

                    avctx->execute(avctx, slice_decode_thread,  (void **) & (s2->thread_context[0]), NULL, s->slice_count, sizeof(void *));

                    for (i = 0; i < s->slice_count; i++)
                    { s2->error_count += s2->thread_context[i]->error_count; }
                }

                /*xiongfei20100211*/
#define CONFIG_MPEG_VDPAU_DECODER 0

                if (CONFIG_MPEG_VDPAU_DECODER && avctx->codec->capabilities & CODEC_CAP_HWACCEL_VDPAU)
                    //                    ff_vdpau_mpeg_picture_complete(s2, buf, buf_size, s->slice_count);
                    ;

                if (slice_end(avctx, picture))
                {
                    if (s2->last_picture_ptr || s2->low_delay) //FIXME merge with the stuff in mpeg_decode_slice
                    {
                        *data_size = sizeof(AVPicture);
                        /*�������� ��ʾ֡��1 xiongfei20100304*/
                        s2->input_picture_number ++;
                        /* 2010/05/8 15:00:00 liuxw+00139685 */
                        /* uiDisplayedFrames��uiDisFramesID�Ĵ���ȫ���ᵽ�ӿڲ� */
                        //						avctx->uiDisplayedFrames = s2->input_picture_number + 1;
                        //						avctx->uiDisFramesID = s2->input_picture_number + 1;
                        /*�������� xiongfei20100304*/
                    }
                }
            }

            s2->pict_type = 0;

            /*x00141957 20100510*/
            if (NULL != s2->current_picture_ptr)
            {
                s2->current_picture_ptr->iErrorCode	= s2->avctx->iErrorCode;
            }

            return FFMAX(0, buf_ptr - buf - s2->parse_context.last_index);
        }

        input_size = buf_end - buf_ptr;

        if (avctx->debug & FF_DEBUG_STARTCODE)
        {
            av_log(avctx, AV_LOG_DEBUG, "%3X at %td left %d\n", start_code, buf_ptr - buf, input_size);
        }

        /* prepare data for next start code */
        switch (start_code)
        {
            case SEQ_START_CODE:
                mpeg1_decode_sequence(avctx, buf_ptr, input_size);
                break;

            case PICTURE_START_CODE:

                /* we have a complete image: we try to decompress it */
                if (mpeg1_decode_picture(avctx, buf_ptr, input_size) < 0)
                {
                    av_log(s2->avctx, AV_LOG_WARNING, "picture header error\n");
                    s2->pict_type = 0;

                    /*x00141957 20100510*/
                    if (s2->first_field || s2->picture_structure == PICT_FRAME)
                    { s2->avctx->uiDiscardFrames ++; }

                    return -1;
                }

                /*����Ϊ��������,����ǰ֡ʵ�ʱ������͸�avctx�������濴�� xiongfei20100304*/
                {
                    switch (s2->pict_type)
                    {
                        case FF_I_TYPE:
                            avctx->uiDecIFrames++;  /* �����ѽ���I֡���� */
                            break;

                        case FF_P_TYPE:
                            avctx->uiDecPFrames++;	 /* �����ѽ���P֡���� */
                            break;

                        case FF_B_TYPE:
                            avctx->uiDecBFrames++;  /* �����ѽ���B֡���� */
                            break;

                        default:
                            break;
                    }
                }
                break;

            case EXT_START_CODE:
                mpeg_decode_extension(avctx, buf_ptr, input_size);
                /*xiongfei modified 20100331*/
                {
                    /*xiongfei 20100409*/
                    if ( s2->chroma_format > avctx->iSourceChromaFormat)
                    {
                        av_log(s2->avctx, AV_LOG_WARNING, "chroma_format %d error !\n", s2->chroma_format);

                        /*x00141957 20100510*/
                        if (s2->first_field || s2->picture_structure == PICT_FRAME)
                        { s2->avctx->uiDiscardFrames ++; }

                        s2->chroma_format = avctx->iChromaFormat;
                        return -1;
                    }
                    else
                    {
                        if (INT_MAX != avctx->iChromaFormat && s2->chroma_format != avctx->iChromaFormat)
                        {
                            av_log(s2->avctx, AV_LOG_WARNING, "unsupport chroma change format[%d] != iChromaFormat[%d] error !\n", s2->chroma_format, avctx->iChromaFormat);

                            /*x00141957 20100510*/
                            if (s2->first_field || s2->picture_structure == PICT_FRAME)
                            { s2->avctx->uiDiscardFrames ++; }

                            s2->chroma_format = avctx->iChromaFormat;
                            return -1;
                        }

                        avctx->iChromaFormat = 	s2->chroma_format;
                    }

                    if (s2->picture_structure == 0)
                    {
                        av_log(s2->avctx, AV_LOG_WARNING, "picture_structure ==  0\n");

                        /*x00141957 20100510*/
                        if (s2->first_field || s2->picture_structure == PICT_FRAME)
                        { s2->avctx->uiDiscardFrames ++; }

                        return -1;
                    }

                    /*����Ϊ �������� ��yuvģʽ����avctx�����濴�� xiongfei201000303*/
                    /*	if( 1 == s->chroma_format)
                    {
                    avctx->eColorFormatType = s->chroma_format - 1;
                    }
                    else if( 2 == s->chroma_format )
                    {
                    avctx->eColorFormatType = s->chroma_format;
                    }
                    */
                }
                break;

            case USER_START_CODE:
                mpeg_decode_user_data(avctx,
                                      buf_ptr, input_size);
                break;

            case GOP_START_CODE:
                s2->first_field = 0;
                mpeg_decode_gop(avctx,
                                buf_ptr, input_size);
                break;

            default:

                /*����ҵ�sliceͷ ,�����slice�������� xiongfei20100221*/
                if (start_code >= SLICE_MIN_START_CODE &&
                    start_code <= SLICE_MAX_START_CODE)
                {
                    int mb_y = start_code - SLICE_MIN_START_CODE;

                    /*�Ը��ؼ����ݽ��м�� x00141957 20100511*/
                    if ((s2->picture_structure == 0) && (s2->iExtFlag))
                    {
                        av_log(s2->avctx, AV_LOG_WARNING, "picture_structure ==  0\n");
                        /*x00141957 20100510*/
                        s2->avctx->uiDiscardFrames ++;
                        break;
                    }

                    /*xiongfei 20100409*/
                    if ( s2->chroma_format > avctx->iSourceChromaFormat)
                    {
                        av_log(s2->avctx, AV_LOG_WARNING, "chroma_format %d error !\n", s2->chroma_format);
                        /*x00141957 20100510*/
                        s2->avctx->uiDiscardFrames ++;
                        break;
                    }
                    else
                    {
                        if (INT_MAX != avctx->iChromaFormat && s2->chroma_format != avctx->iChromaFormat)
                        {
                            av_log(s2->avctx, AV_LOG_WARNING, "unsupport chroma change format[%d] != iChromaFormat[%d] error !\n", s2->chroma_format, avctx->iChromaFormat);
                            /*x00141957 20100510*/
                            s2->avctx->uiDiscardFrames ++;
                            break;;
                        }

                        avctx->iChromaFormat = 	s2->chroma_format;
                    }

                    if (s2->pict_type == 0)
                    {
                        break;
                    }

                    /*������ x00141957 20100511*/

                    if (s2->last_picture_ptr == NULL)
                    {
                        /* Skip B-frames if we do not have reference frames. */
                        if (s2->pict_type == FF_B_TYPE) { break; }
                    }

                    if (s2->next_picture_ptr == NULL)
                    {
                        /* Skip P-frames if we do not have a reference frame or we have an invalid header. */
                        if (s2->pict_type == FF_P_TYPE && (s2->first_field || s2->picture_structure == PICT_FRAME)) { break; }
                    }

                    /* Skip B-frames if we are in a hurry. */
                    if (avctx->hurry_up && s2->pict_type == FF_B_TYPE) { break; }

                    if (  (avctx->skip_frame >= AVDISCARD_NONREF && s2->pict_type == FF_B_TYPE)
                          || (avctx->skip_frame >= AVDISCARD_NONKEY && s2->pict_type != FF_I_TYPE)
                          || avctx->skip_frame >= AVDISCARD_ALL)
                    { break; }

                    /* Skip everything if we are in a hurry>=5. */
                    if (avctx->hurry_up >= 5) { break; }

                    if (!s->mpeg_enc_ctx_allocated) { break; }

                    if (s2->codec_id == CODEC_ID_MPEG2VIDEO)
                    {
                        if (mb_y < avctx->skip_top || mb_y >= s2->mb_height - avctx->skip_bottom)
                        { break; }
                    }

                    if (!s2->pict_type)
                    {
                        av_log(avctx, AV_LOG_WARNING, "Missing picture start code\n");
                        break;
                    }

                    /*����ǵ�һ��slice��˵���տ�ʼ����һ֡����ʼ���ڴ����� ������Ҳ���ǵ��˵׳���һ��slice�������� xiongfei20100222*/
                    if (s2->first_slice)
                    {
                        /*Ų������ x00141957 20100511*/
                        /*s2->first_slice=0;*/
                        /*����Ϊ�������룬�ж�ͨ����Դ�Ƿ��������� ����Ҫ�жϿ����Լ�chromaformat xiongfei20100303*/
                        {
                            /* ��⵱ǰ�����Ŀ��Ⱥͳ����Ƿ񳬹���̬�����е������Ⱥ͸߶� */
                            if (avctx->usActualWidth > avctx->usSourceWidth || avctx->usActualHeight > avctx->usSourceHeight)
                            {
                                av_log(avctx, AV_LOG_WARNING, "dec_ctx[%p] mpeg2_dec[%p] decode_chunks() the ActualWidth[%d] of the bitstream is more than MaxWidth[%d] or  \
														the ActualHeight[%d] of the bitstream is more than MaxHeight[%d]!\n", avctx, s, avctx->usActualWidth, avctx->usSourceWidth, avctx->usActualHeight							, avctx->usSourceHeight);
                                //							avctx->iErrorCode = (avctx->usActualWidth > avctx->usSourceWidth) ? IMEDIA_CODEC_ERR_INVALID_WIDTH : IMEDIA_CODEC_ERR_INVALID_HEIGHT;

                                IMEDIA_SET_ERR_SLICE(avctx->iErrorCode, IMEDIA_ERR_SLICE_ADDR);

                                /*x00141957 20100510*/
                                if (s2->first_field || s2->picture_structure == PICT_FRAME)
                                { s2->avctx->uiDiscardFrames ++; }

                                return -1;
                            }
                        }

                        if (mpeg_field_start(s2, buf, buf_size) < 0)
                        {
                            /*x00141957 20100510*/
                            if (s2->first_field || s2->picture_structure == PICT_FRAME)
                            { s2->avctx->uiDiscardFrames ++; }

                            return -1;
                        }

                        s2->first_slice = 0;
                    }

                    if (!s2->current_picture_ptr)
                    {
                        av_log(avctx, AV_LOG_WARNING, "current_picture not initialized\n");

                        /*x00141957 20100510*/
                        if (s2->first_field || s2->picture_structure == PICT_FRAME)
                        { s2->avctx->uiDiscardFrames ++; }

                        return -1;
                    }

                    if (avctx->codec->capabilities & CODEC_CAP_HWACCEL_VDPAU)
                    {
                        s->slice_count++;
                        break;
                    }

                    if (avctx->thread_count > 1)
                    {
                        int threshold = (s2->mb_height * s->slice_count + avctx->thread_count / 2) / avctx->thread_count;

                        if (threshold <= mb_y)
                        {
                            MpegEncContext *thread_context = s2->thread_context[s->slice_count];

                            thread_context->start_mb_y = mb_y;
                            thread_context->end_mb_y  = s2->mb_height;

                            if (s->slice_count)
                            {
                                s2->thread_context[s->slice_count - 1]->end_mb_y = mb_y;
                                ff_update_duplicate_context(thread_context, s2);
                            }

                            init_get_bits(&thread_context->gb, buf_ptr, input_size * 8);
                            s->slice_count++;
                        }

                        buf_ptr += 2; //FIXME add minimum number of bytes per slice
                    }
                    else
                    {
                        /*�˴���ʽ�������slice xiongfei20100222*/
                        ret = mpeg_decode_slice(s, mb_y, &buf_ptr, input_size);
                        emms_c();

                        /*������뷵��ʧ�ܣ���Ϊ�Ժ�Ĵ������� xiongfei20100222*/
                        if (ret < 0)
                        {
                            if (s2->resync_mb_x >= 0 && s2->resync_mb_y >= 0)
                            { ff_er_add_slice(s2, s2->resync_mb_x, s2->resync_mb_y, s2->mb_x, s2->mb_y, AC_ERROR | DC_ERROR | MV_ERROR); }
                        }
                        else
                        {
                            ff_er_add_slice(s2, s2->resync_mb_x, s2->resync_mb_y, s2->mb_x - 1, s2->mb_y, AC_END | DC_END | MV_END);
                        }
                    }
                }

                break;
        }
    }
}

static int mpeg_decode_end(AVCodecContext *avctx)
{
    Mpeg1Context *s = avctx->priv_data;

    if (s->mpeg_enc_ctx_allocated)
    { MPV_common_end(&s->mpeg_enc_ctx); }
    else
    {
        MpegEncContext *s2 = &s->mpeg_enc_ctx;

        /*x00141957 20100507 ���Ӷ�parse_context.buffer�ڴ���ͷ�*/
        if (s2->parse_context.buffer)
        {
            av_freep(&s2->parse_context.buffer);
        }
    }

    return 0;
}

/* ������������λmpeg2������ xiongfei20100302*/
static av_cold int mpeg12_reset(Mpeg1Context *h)
{
    int iRet = 0;

    MpegEncContext *const	s		= &h->mpeg_enc_ctx;
    AVCodecContext 		*avctx	= s->avctx;

    /* ���Mpeg1Context�ṹ����� xionigfei20100302*/
    memset(h, 0, sizeof(Mpeg1Context));

    /* ��ʼ��Mpeg1Context�ṹ����� xiongfei20100302*/
    iRet = mpeg_decode_init(avctx);

    if (0 != iRet)
    {
        av_log(avctx, AV_LOG_ERROR, "ctx_dec[%p] mpeg2_ctx[%p] failed! return code: %d !\n", avctx, h, iRet);
    }

    return iRet;
}

/*����decode_reset����������control�е�reset xiongfei20100302*/
static av_cold int decode_reset(AVCodecContext *avctx)
{
    int iRet = 0;

    Mpeg1Context *h = avctx->priv_data;
    MpegEncContext *s = &h->mpeg_enc_ctx;
    //	int i;

    /* ���buffer */
    ff_mpeg_flush(avctx);

    /*x00141957 20100606 ����*/
    /*���ⵥ�ţ�AZ1D02136
    �޸��ˣ��ܷ� +00141957
    ʱ�䣺2010/6/7
    ���������� ��������ѭ�����������£���������
    �����޸ģ� resetʱ���ڴ�й¶����*/
    MPV_common_end(s);

    /* ��λ����ʼ��AVCodecContext�ṹ����� xiongfei20100302*/
    iRet = avcodec_reset(avctx);

    if (0 != iRet)
    {
        av_log(avctx, AV_LOG_ERROR, "ctx_dec[%p] avcodec_reset() failed! return code: %d !\n", avctx, iRet);
        return iRet;
    }

    /* ��λ����ʼ��Mpeg1Context�ṹ����� xiongfei20100302*/
    iRet = mpeg12_reset(h);

    if (0 != iRet)
    {
        av_log(avctx, AV_LOG_ERROR, "ctx_dec[%p] h264_ctx[%p] mpeg2_ctx() failed! return code: %d !\n", avctx, h, iRet);
        return iRet;
    }

    return iRet;
}

/*
AVCodec mpeg1video_decoder = {
    "mpeg1video",
    CODEC_TYPE_VIDEO,
    CODEC_ID_MPEG1VIDEO,
    sizeof(Mpeg1Context),
    mpeg_decode_init,
    NULL,
    mpeg_decode_end,
    mpeg_decode_frame,
    CODEC_CAP_DRAW_HORIZ_BAND | CODEC_CAP_DR1 | CODEC_CAP_TRUNCATED | CODEC_CAP_DELAY,
    .flush= ff_mpeg_flush,
    .long_name= NULL_IF_CONFIG_SMALL("MPEG-1 video"),
};
*/

/*������Сΰ��д�ṹ�� ��д���� xiongfei20100212*/
AVCodec mpeg2video_decoder =
{
    "mpeg2video",
    CODEC_TYPE_VIDEO,
    CODEC_ID_MPEG2VIDEO,
    sizeof(Mpeg1Context),
    mpeg_decode_init,
    NULL,
    mpeg_decode_end,
    mpeg_decode_frame,
    CODEC_CAP_DRAW_HORIZ_BAND | CODEC_CAP_DR1 | CODEC_CAP_TRUNCATED | CODEC_CAP_DELAY,
    NULL,
    ff_mpeg_flush,
    NULL,
    NULL,
    NULL_IF_CONFIG_SMALL("MPEG-2 video"),
    NULL,
    NULL,
    NULL,
    MPEG2_Frame_Parse,                    //xiongfei 20100220
    decode_reset                          //xiongfei 20100303
};

//legacy decoder
/*
AVCodec mpegvideo_decoder = {
    "mpegvideo",
    CODEC_TYPE_VIDEO,
    CODEC_ID_MPEG2VIDEO,
    sizeof(Mpeg1Context),
    mpeg_decode_init,
    NULL,
    mpeg_decode_end,
    mpeg_decode_frame,
    CODEC_CAP_DRAW_HORIZ_BAND | CODEC_CAP_DR1 | CODEC_CAP_TRUNCATED | CODEC_CAP_DELAY,
    .flush= ff_mpeg_flush,
    .long_name= NULL_IF_CONFIG_SMALL("MPEG-1 video"),
};
*/
#if CONFIG_MPEG_XVMC_DECODER
static av_cold int mpeg_mc_decode_init(AVCodecContext *avctx)
{
    if ( avctx->thread_count > 1)
    { return -1; }

    if ( !(avctx->slice_flags & SLICE_FLAG_CODED_ORDER) )
    { return -1; }

    if ( !(avctx->slice_flags & SLICE_FLAG_ALLOW_FIELD) )
    {
        dprintf(avctx, "mpeg12.c: XvMC decoder will work better if SLICE_FLAG_ALLOW_FIELD is set\n");
    }

    mpeg_decode_init(avctx);

    avctx->pix_fmt = PIX_FMT_XVMC_MPEG2_IDCT;
    avctx->xvmc_acceleration = 2;//2 - the blocks are packed!

    return 0;
}

AVCodec mpeg_xvmc_decoder =
{
    "mpegvideo_xvmc",
    CODEC_TYPE_VIDEO,
    CODEC_ID_MPEG2VIDEO_XVMC,
    sizeof(Mpeg1Context),
    mpeg_mc_decode_init,
    NULL,
    mpeg_decode_end,
    mpeg_decode_frame,
    CODEC_CAP_DRAW_HORIZ_BAND | CODEC_CAP_DR1 | CODEC_CAP_TRUNCATED | CODEC_CAP_HWACCEL | CODEC_CAP_DELAY,
    .flush = ff_mpeg_flush,
    .long_name = NULL_IF_CONFIG_SMALL("MPEG-1/2 video XvMC (X-Video Motion Compensation)"),
};

#endif

#if CONFIG_MPEG_VDPAU_DECODER
AVCodec mpeg_vdpau_decoder =
{
    "mpegvideo_vdpau",
    CODEC_TYPE_VIDEO,
    CODEC_ID_MPEG2VIDEO,
    sizeof(Mpeg1Context),
    mpeg_decode_init,
    NULL,
    mpeg_decode_end,
    mpeg_decode_frame,
    CODEC_CAP_DR1 | CODEC_CAP_TRUNCATED | CODEC_CAP_HWACCEL_VDPAU | CODEC_CAP_DELAY,
    .flush = ff_mpeg_flush,
    .long_name = NULL_IF_CONFIG_SMALL("MPEG-1/2 video (VDPAU acceleration)"),
};
#endif

#if CONFIG_MPEG1_VDPAU_DECODER
AVCodec mpeg1_vdpau_decoder =
{
    "mpeg1video_vdpau",
    CODEC_TYPE_VIDEO,
    CODEC_ID_MPEG1VIDEO,
    sizeof(Mpeg1Context),
    mpeg_decode_init,
    NULL,
    mpeg_decode_end,
    mpeg_decode_frame,
    CODEC_CAP_DR1 | CODEC_CAP_TRUNCATED | CODEC_CAP_HWACCEL_VDPAU | CODEC_CAP_DELAY,
    .flush = ff_mpeg_flush,
    .long_name = NULL_IF_CONFIG_SMALL("MPEG-1 video (VDPAU acceleration)"),
};
#endif
