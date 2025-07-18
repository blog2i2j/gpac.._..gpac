/*
 *			GPAC - Multimedia Framework C SDK
 *
 *			Authors: Jean Le Feuvre
 *			Copyright (c) Telecom ParisTech 2000-2025
 *					All rights reserved
 *
 *  This file is part of GPAC / Media Tools sub-project
 *
 *  GPAC is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  GPAC is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */



#include <gpac/internal/media_dev.h>
#include <gpac/internal/isomedia_dev.h>
#include <gpac/constants.h>
#include <gpac/config_file.h>

#ifndef GPAC_DISABLE_ISOM_WRITE
GF_EXPORT
GF_Err gf_media_change_par(GF_ISOFile *file, u32 track, s32 ar_num, s32 ar_den, Bool force_par, Bool rewrite_bs)
{
	u32 tk_w, tk_h;
	GF_Err e;
	Bool get_par_info = GF_FALSE;

	e = gf_isom_get_visual_info(file, track, 1, &tk_w, &tk_h);
	if (e) return e;

	if ((ar_num < 0) || (ar_den < 0)) {
		get_par_info = GF_TRUE;
		rewrite_bs = GF_FALSE;
	}
	else if (!ar_num || !ar_den) {
		rewrite_bs = GF_FALSE;
	}

	if (get_par_info || rewrite_bs) {
		u32 stype = gf_isom_get_media_subtype(file, track, 1);
		if ((stype==GF_ISOM_SUBTYPE_AVC_H264)
				|| (stype==GF_ISOM_SUBTYPE_AVC2_H264)
				|| (stype==GF_ISOM_SUBTYPE_AVC3_H264)
				|| (stype==GF_ISOM_SUBTYPE_AVC4_H264)
		   ) {
#ifndef GPAC_DISABLE_AV_PARSERS
			GF_AVCConfig *avcc = gf_isom_avc_config_get(file, track, 1);
			if (rewrite_bs) {
				gf_avc_change_par(avcc, ar_num, ar_den);
				e = gf_isom_avc_config_update(file, track, 1, avcc);
			} else {
				GF_NALUFFParam *sl = gf_list_get(avcc->sequenceParameterSets, 0);
				if (sl) {
					gf_avc_get_sps_info(sl->data, sl->size, NULL, NULL, NULL, &ar_num, &ar_den);
				} else {
					ar_num = ar_den = 0;
				}
			}
			gf_odf_avc_cfg_del(avcc);
			if (e) return e;
#endif
		}
#if !defined(GPAC_DISABLE_AV_PARSERS)
		else if ((stype==GF_ISOM_SUBTYPE_HVC1) || (stype==GF_ISOM_SUBTYPE_HVC2)
			|| (stype==GF_ISOM_SUBTYPE_HEV1) || (stype==GF_ISOM_SUBTYPE_HEV2)
		) {
			GF_HEVCConfig *hvcc = gf_isom_hevc_config_get(file, track, 1);
			if (rewrite_bs) {
				gf_hevc_change_par(hvcc, ar_num, ar_den);
				e = gf_isom_hevc_config_update(file, track, 1, hvcc);
			} else {
				u32 i=0;
				GF_NALUFFParamArray *ar;
				ar_num = ar_den = 0;
				while ( (ar = gf_list_enum(hvcc->param_array, &i))) {
					if (ar->type==GF_HEVC_NALU_SEQ_PARAM) {
						GF_NALUFFParam *sl = gf_list_get(ar->nalus, 0);
						if (sl)
							gf_hevc_get_sps_info(sl->data, sl->size, NULL, NULL, NULL, &ar_num, &ar_den);
						break;
					}
				}
			}
			gf_odf_hevc_cfg_del(hvcc);
			if (e) return e;
		}
		else if (stype == GF_ISOM_SUBTYPE_AV01) {
			//GF_AV1Config *av1c = gf_isom_av1_config_get(file, track, 1);
			//TODO: e = gf_isom_av1_config_update(file, track, 1, av1c);
			//gf_odf_av1_cfg_del(av1c);
			//if (e) return e;
			return GF_NOT_SUPPORTED;
		}
		else if ((stype==GF_ISOM_SUBTYPE_VVC1) || (stype==GF_ISOM_SUBTYPE_VVI1)
		) {
			GF_VVCConfig *vvcc = gf_isom_vvc_config_get(file, track, 1);
			if (rewrite_bs) {
				gf_vvc_change_par(vvcc, ar_num, ar_den);
				e = gf_isom_vvc_config_update(file, track, 1, vvcc);
			} else {
				u32 i=0;
				GF_NALUFFParamArray *ar;
				ar_num = ar_den = 0;
				while ( (ar = gf_list_enum(vvcc->param_array, &i))) {
					if (ar->type==GF_VVC_NALU_SEQ_PARAM) {
						GF_NALUFFParam *sl = gf_list_get(ar->nalus, 0);
						if (sl)
							gf_vvc_get_sps_info(sl->data, sl->size, NULL, NULL, NULL, &ar_num, &ar_den);
						break;
					}
				}
			}
			gf_odf_vvc_cfg_del(vvcc);
			if (e) return e;
		}
#endif
		else if (stype==GF_ISOM_SUBTYPE_MPEG4) {
			GF_ESD *esd = gf_isom_get_esd(file, track, 1);
			if (!esd || !esd->decoderConfig || (esd->decoderConfig->streamType!=4) ) {
				if (esd) gf_odf_desc_del((GF_Descriptor *) esd);
				return GF_NOT_SUPPORTED;
			}
#ifndef GPAC_DISABLE_AV_PARSERS
			if (esd->decoderConfig->objectTypeIndication==GF_CODECID_MPEG4_PART2) {
				if (rewrite_bs) {
					e = gf_m4v_rewrite_par(&esd->decoderConfig->decoderSpecificInfo->data, &esd->decoderConfig->decoderSpecificInfo->dataLength, ar_num, ar_den);
					if (!e) e = gf_isom_change_mpeg4_description(file, track, 1, esd);
				} else {
					GF_M4VDecSpecInfo dsi;
					e = gf_m4v_get_config(esd->decoderConfig->decoderSpecificInfo->data, esd->decoderConfig->decoderSpecificInfo->dataLength, &dsi);
					ar_num = dsi.par_num;
					ar_den = dsi.par_den;
				}
				gf_odf_desc_del((GF_Descriptor *) esd);
				if (e) return e;
			}
#endif
		} else {
			u32 mtype = gf_isom_get_media_type(file, track);
			if (gf_isom_is_video_handler_type(mtype)) {
				if (rewrite_bs) {
					GF_LOG(GF_LOG_WARNING, GF_LOG_CONTAINER,
						("[ISOBMF] Warning: changing pixel ratio of media subtype \"%s\" is not supported, changing only \"pasp\" signaling\n",
							gf_4cc_to_str(gf_isom_get_media_subtype(file, track, 1)) ));
				}
			} else {
				GF_LOG(GF_LOG_ERROR, GF_LOG_CONTAINER, ("[ISOBMF] Error: changing pixel ratio on non-video track.\n"));
				return GF_BAD_PARAM;
			}
		}
		//auto mode
		if (get_par_info && ((ar_num<=0) || (ar_den<=0))) {
			GF_LOG(GF_LOG_WARNING, GF_LOG_CONTAINER, ("[ISOBMF] No sample AR info present in sample description, ignoring SAR update\n"));
			if (force_par)
				return gf_isom_set_pixel_aspect_ratio(file, track, 1, 1, 1, force_par);
			return GF_OK;
		}
	}
	e = gf_isom_set_pixel_aspect_ratio(file, track, 1, ar_num, ar_den, force_par);
	if (e) return e;

	if ((ar_den>0) && (ar_num>0)) {
		tk_w = tk_w * ar_num / ar_den;
	}
	/*PASP has been removed or forced to 1:1, revert to full frame for track layout*/
	else {
		e = gf_isom_get_visual_info(file, track, 1, &tk_w, &tk_h);
		if (e) return e;
	}
	return gf_isom_set_track_layout_info(file, track, tk_w<<16, tk_h<<16, 0, 0, 0);
}

GF_EXPORT
GF_Err gf_media_change_color(GF_ISOFile *file, u32 track, s32 fullrange, s32 vidformat, s32 colorprim, s32 transfer, s32 colmatrix)
{
#ifndef GPAC_DISABLE_AV_PARSERS
	GF_Err e;
	u32 stype = gf_isom_get_media_subtype(file, track, 1);
	if ((stype==GF_ISOM_SUBTYPE_AVC_H264)
			|| (stype==GF_ISOM_SUBTYPE_AVC2_H264)
			|| (stype==GF_ISOM_SUBTYPE_AVC3_H264)
			|| (stype==GF_ISOM_SUBTYPE_AVC4_H264)
	) {
		GF_AVCConfig *avcc = gf_isom_avc_config_get(file, track, 1);
		gf_avc_change_color(avcc, fullrange, vidformat, colorprim, transfer, colmatrix);
		e = gf_isom_avc_config_update(file, track, 1, avcc);
		gf_odf_avc_cfg_del(avcc);
		if (e) return e;
		//remove any colr box
		return gf_isom_set_visual_color_info(file, track, 1, 0, 0, 0, 0, 0, NULL, 0);
	}
	if ((stype==GF_ISOM_SUBTYPE_HEV1)
			|| (stype==GF_ISOM_SUBTYPE_HEV2)
			|| (stype==GF_ISOM_SUBTYPE_HVC1)
			|| (stype==GF_ISOM_SUBTYPE_HVC2)
			|| (stype==GF_ISOM_SUBTYPE_LHV1)
			|| (stype==GF_ISOM_SUBTYPE_LHE1)
	) {
		GF_HEVCConfig *hvcc = gf_isom_hevc_config_get(file, track, 1);
		gf_hevc_change_color(hvcc, fullrange, vidformat, colorprim, transfer, colmatrix);
		e = gf_isom_hevc_config_update(file, track, 1, hvcc);
		gf_odf_hevc_cfg_del(hvcc);
		if (e) return e;
		//remove any colr box
		return gf_isom_set_visual_color_info(file, track, 1, 0, 0, 0, 0, 0, NULL, 0);
	}
	if ((stype==GF_ISOM_SUBTYPE_VVC1) || (stype==GF_ISOM_SUBTYPE_VVI1) ) {
		GF_VVCConfig *vvcc = gf_isom_vvc_config_get(file, track, 1);
		gf_vvc_change_color(vvcc, fullrange, vidformat, colorprim, transfer, colmatrix);
		e = gf_isom_vvc_config_update(file, track, 1, vvcc);
		gf_odf_vvc_cfg_del(vvcc);
		if (e) return e;
		//remove any colr box
		return gf_isom_set_visual_color_info(file, track, 1, 0, 0, 0, 0, 0, NULL, 0);
	}
#endif
	return GF_NOT_SUPPORTED;
}

GF_EXPORT
GF_Err gf_media_remove_non_rap(GF_ISOFile *file, u32 track, Bool non_ref_only)
{
	GF_Err e;
	u32 i, count, di;
	u64 offset, dur, last_dts;
	Bool all_raps = (gf_isom_has_sync_points(file, track)==0) ? 1 : 0;
	if (all_raps) return GF_OK;

	last_dts = 0;
	dur = gf_isom_get_media_duration(file, track);

	gf_isom_set_cts_packing(file, track, GF_TRUE);

	count = gf_isom_get_sample_count(file, track);
	for (i=0; i<count; i++) {
		Bool remove = GF_TRUE;
		GF_ISOSample *samp = gf_isom_get_sample_info(file, track, i+1, &di, &offset);
		if (!samp) return gf_isom_last_error(file);

		if (samp->IsRAP) remove = GF_FALSE;
		else if (non_ref_only) {
			u32 isLeading, dependsOn, dependedOn, redundant;
			gf_isom_get_sample_flags(file, track, i+1, &isLeading, &dependsOn, &dependedOn, &redundant);
			if (dependedOn != 2) {
				remove = GF_FALSE;
			}
		}

		if (!remove) {
			last_dts = samp->DTS;
			gf_isom_sample_del(&samp);
			continue;
		}
		gf_isom_sample_del(&samp);
		e = gf_isom_remove_sample(file, track, i+1);
		if (e) return e;
		i--;
		count--;
	}
	gf_isom_set_cts_packing(file, track, GF_FALSE);
	gf_isom_set_last_sample_duration(file, track, (u32) (dur - last_dts) );
	return GF_OK;
}

#endif /*GPAC_DISABLE_ISOM_WRITE*/

GF_EXPORT
GF_Err gf_media_get_file_hash(const char *file, u8 hash[20])
{
	u8 block[4096];
	u32 read;
	u64 size, tot;
	FILE *in;
	GF_SHA1Context *ctx;
	GF_Err e = GF_OK;
#ifndef GPAC_DISABLE_ISOM
	GF_BitStream *bs = NULL;
	Bool is_isom = gf_isom_probe_file(file);
#endif

	in = gf_fopen(file, "rb");
    if (!in) return GF_URL_ERROR;
	size = gf_fsize(in);

	ctx = gf_sha1_starts();
	tot = 0;
#ifndef GPAC_DISABLE_ISOM
	if (is_isom) bs = gf_bs_from_file(in, GF_BITSTREAM_READ);
#endif

	while (tot<size) {
#ifndef GPAC_DISABLE_ISOM
		if (is_isom) {
			u64 box_size = gf_bs_peek_bits(bs, 32, 0);
			u32 box_type = gf_bs_peek_bits(bs, 32, 4);

			/*64-bit size*/
			if (box_size==1) box_size = gf_bs_peek_bits(bs, 64, 8);
			/*till end of file*/
			if (!box_size) box_size = size - tot;

			/*skip all MutableDRMInformation*/
			if (box_type==GF_ISOM_BOX_TYPE_MDRI) {
				gf_bs_skip_bytes(bs, box_size);
				tot += box_size;
			} else {
				u64 bsize = 0;
				while (bsize<box_size) {
					u32 to_read = (u32) ((box_size-bsize<4096) ? (box_size-bsize) : 4096);
					read = gf_bs_read_data(bs, (char *) block, to_read);
					if (!read || (read != to_read) ) {
						GF_LOG(GF_LOG_ERROR, GF_LOG_CONTAINER, ("corrupted isobmf file, box read "LLU" but expected still "LLU" bytes\n", bsize, box_size));
						break;
					}
					gf_sha1_update(ctx, block, to_read);
					bsize += to_read;
				}
				tot += box_size;
			}
		} else
#endif
		{
			read = (u32) gf_fread(block, 4096, in);
			if ((s32) read <= 0) {
				if (ferror(in))
					e = GF_IO_ERR;
				break;
			}
			gf_sha1_update(ctx, block, read);
			tot += read;
		}
	}
	gf_sha1_finish(ctx, hash);
#ifndef GPAC_DISABLE_ISOM
	if (bs) gf_bs_del(bs);
#endif
	gf_fclose(in);
	return e;
}

#ifndef GPAC_DISABLE_ISOM

#ifndef GPAC_DISABLE_ISOM_WRITE

static const u32 ISMA_VIDEO_OD_ID = 20;
static const u32 ISMA_AUDIO_OD_ID = 10;

static const u32 ISMA_VIDEO_ES_ID = 201;
static const u32 ISMA_AUDIO_ES_ID = 101;

/*ISMA audio*/
static const u8 ISMA_BIFS_AUDIO[] =
{
	0xC0, 0x10, 0x12, 0x81, 0x30, 0x2A, 0x05, 0x7C
};
/*ISMA video*/
static const u8 ISMA_GF_BIFS_VIDEO[] =
{
	0xC0, 0x10, 0x12, 0x60, 0x42, 0x82, 0x28, 0x29,
	0xD0, 0x4F, 0x00
};
/*ISMA audio-video*/
static const u8 ISMA_BIFS_AV[] =
{
	0xC0, 0x10, 0x12, 0x81, 0x30, 0x2A, 0x05, 0x72,
	0x60, 0x42, 0x82, 0x28, 0x29, 0xD0, 0x4F, 0x00
};

/*image only - uses same visual OD ID as video*/
static const u8 ISMA_BIFS_IMAGE[] =
{
	0xC0, 0x11, 0xA4, 0xCD, 0x53, 0x6A, 0x0A, 0x44,
	0x13, 0x00
};

/*ISMA audio-image*/
static const u8 ISMA_BIFS_AI[] =
{
	0xC0, 0x11, 0xA5, 0x02, 0x60, 0x54, 0x0A, 0xE4,
	0xCD, 0x53, 0x6A, 0x0A, 0x44, 0x13, 0x00
};


GF_EXPORT
GF_Err gf_media_make_isma(GF_ISOFile *mp4file, Bool keepESIDs, Bool keepImage, Bool no_ocr)
{
	u32 AudioTrack, VideoTrack, Tracks, i, mType, bifsT, odT, descIndex, VID, AID, bifsID, odID;
	u32 bifs, w, h;
	Bool is_image, image_track;
	GF_ESD *a_esd, *v_esd, *_esd;
	GF_ObjectDescriptor *od;
	GF_ODUpdate *odU;
	GF_ODCodec *codec;
	GF_ISOSample *samp;
	GF_BitStream *bs;
	u8 audioPL, visualPL;

	switch (gf_isom_get_mode(mp4file)) {
	case GF_ISOM_OPEN_EDIT:
	case GF_ISOM_OPEN_WRITE:
	case GF_ISOM_WRITE_EDIT:
		break;
	default:
		return GF_BAD_PARAM;
	}


	Tracks = gf_isom_get_track_count(mp4file);
	AID = VID = 0;
	is_image = 0;

	//search for tracks
	for (i=0; i<Tracks; i++) {
		GF_ESD *esd = gf_isom_get_esd(mp4file, i+1, 1);
		//remove from IOD
		gf_isom_remove_track_from_root_od(mp4file, i+1);

		mType = gf_isom_get_media_type(mp4file, i+1);
		switch (mType) {
		case GF_ISOM_MEDIA_VISUAL:
        case GF_ISOM_MEDIA_AUXV:
        case GF_ISOM_MEDIA_PICT:
			image_track = 0;
			if (esd && esd->decoderConfig && ((esd->decoderConfig->objectTypeIndication==GF_CODECID_JPEG) || (esd->decoderConfig->objectTypeIndication==GF_CODECID_PNG)) )
				image_track = 1;

			/*remove image tracks if wanted*/
			if (keepImage || !image_track) {
				/*only ONE video stream possible with ISMA*/
				if (VID) {
					if (esd) gf_odf_desc_del((GF_Descriptor*)esd);
					GF_LOG(GF_LOG_ERROR, GF_LOG_MEDIA, ("[ISMA convert] More than one video track found, cannot convert file - remove extra track(s)\n"));
					return GF_NOT_SUPPORTED;
				}
				VID = gf_isom_get_track_id(mp4file, i+1);
				is_image = image_track;
			} else {
				GF_LOG(GF_LOG_INFO, GF_LOG_MEDIA, ("[ISMA convert] Visual track ID %d: only one sample found, assuming image and removing track\n", gf_isom_get_track_id(mp4file, i+1) ) );
				gf_isom_remove_track(mp4file, i+1);
				i -= 1;
				Tracks = gf_isom_get_track_count(mp4file);
			}
			break;
		case GF_ISOM_MEDIA_AUDIO:
			if (AID) {
				if (esd) gf_odf_desc_del((GF_Descriptor*)esd);
				GF_LOG(GF_LOG_ERROR, GF_LOG_MEDIA, ("[ISMA convert] More than one audio track found, cannot convert file - remove extra track(s)\n") );
				return GF_NOT_SUPPORTED;
			}
			AID = gf_isom_get_track_id(mp4file, i+1);
			break;
		/*clean file*/
		default:
			if (mType==GF_ISOM_MEDIA_HINT) {
				GF_LOG(GF_LOG_INFO, GF_LOG_MEDIA, ("[ISMA convert] Removing Hint track ID %d\n", gf_isom_get_track_id(mp4file, i+1) ));
			} else {
				GF_LOG(GF_LOG_INFO, GF_LOG_MEDIA, ("[ISMA convert] Removing track ID %d\n", gf_isom_get_track_id(mp4file, i+1) ));
			}
			gf_isom_remove_track(mp4file, i+1);
			i -= 1;
			Tracks = gf_isom_get_track_count(mp4file);
			break;
		}
		if (esd) gf_odf_desc_del((GF_Descriptor*)esd);
	}
	//no audio no video
	if (!AID && !VID) return GF_OK;

	/*reset all PLs*/
	visualPL = 0xFE;
	audioPL = 0xFE;

	od = (GF_ObjectDescriptor *) gf_isom_get_root_od(mp4file);
	if (od && (od->tag==GF_ODF_IOD_TAG)) {
		audioPL = ((GF_InitialObjectDescriptor*)od)->audio_profileAndLevel;
		visualPL = ((GF_InitialObjectDescriptor*)od)->visual_profileAndLevel;
	}
	if (od) gf_odf_desc_del((GF_Descriptor *)od);


	//create the OD AU
	bifs = 0;
	odU = (GF_ODUpdate *) gf_odf_com_new(GF_ODF_OD_UPDATE_TAG);

	a_esd = v_esd = NULL;

	gf_isom_set_root_od_id(mp4file, 1);

	bifsID = 1;
	odID = 2;
	if (keepESIDs) {
		bifsID = 1;
		while ((bifsID==AID) || (bifsID==VID)) bifsID++;
		odID = 2;
		while ((odID==AID) || (odID==VID) || (odID==bifsID)) odID++;

	}

	VideoTrack = gf_isom_get_track_by_id(mp4file, VID);
	AudioTrack = gf_isom_get_track_by_id(mp4file, AID);

	w = h = 0;
	if (VideoTrack) {
		bifs = 1;
		od = (GF_ObjectDescriptor *) gf_odf_desc_new(GF_ODF_OD_TAG);
		od->objectDescriptorID = ISMA_VIDEO_OD_ID;

		if (!keepESIDs && (VID != ISMA_VIDEO_ES_ID)) {
			gf_isom_set_track_id(mp4file, VideoTrack, ISMA_VIDEO_ES_ID);
		}

		v_esd = gf_isom_get_esd(mp4file, VideoTrack, 1);
		if (v_esd) {
			v_esd->OCRESID = no_ocr ? 0 : bifsID;

			gf_odf_desc_add_desc((GF_Descriptor *)od, (GF_Descriptor *)v_esd);
			gf_list_add(odU->objectDescriptors, od);

			gf_isom_get_track_layout_info(mp4file, VideoTrack, &w, &h, NULL, NULL, NULL);
			if (!w || !h) {
				gf_isom_get_visual_info(mp4file, VideoTrack, 1, &w, &h);
#ifndef GPAC_DISABLE_AV_PARSERS
				if (v_esd->decoderConfig
					&& (v_esd->decoderConfig->objectTypeIndication==GF_CODECID_MPEG4_PART2)
					&& (v_esd->decoderConfig->streamType==GF_STREAM_VISUAL)
					&& v_esd->decoderConfig->decoderSpecificInfo
					&& v_esd->decoderConfig->decoderSpecificInfo->data
				) {
					GF_M4VDecSpecInfo dsi;
					gf_m4v_get_config(v_esd->decoderConfig->decoderSpecificInfo->data, v_esd->decoderConfig->decoderSpecificInfo->dataLength, &dsi);
					if (!is_image && (!w || !h)) {
						w = dsi.width;
						h = dsi.height;
						gf_isom_set_visual_info(mp4file, VideoTrack, 1, w, h);
						GF_LOG(GF_LOG_INFO, GF_LOG_MEDIA, ("[ISMA convert] Adjusting visual track size to %d x %d\n", w, h));
					}
					if (dsi.par_num && dsi.par_den && (dsi.par_den!=dsi.par_num)) {
						w *= dsi.par_num;
						w /= dsi.par_den;
					}
					if (dsi.VideoPL) visualPL = dsi.VideoPL;
				}
#endif
			}
		}
	}

	if (AudioTrack) {
		od = (GF_ObjectDescriptor *) gf_odf_desc_new(GF_ODF_OD_TAG);
		od->objectDescriptorID = ISMA_AUDIO_OD_ID;

		if (!keepESIDs && (AID != ISMA_AUDIO_ES_ID)) {
			gf_isom_set_track_id(mp4file, AudioTrack, ISMA_AUDIO_ES_ID);
		}

		a_esd = gf_isom_get_esd(mp4file, AudioTrack, 1);
		if (a_esd) {
			a_esd->OCRESID = no_ocr ? 0 : bifsID;

			if (!keepESIDs) a_esd->ESID = ISMA_AUDIO_ES_ID;
			gf_odf_desc_add_desc((GF_Descriptor *)od, (GF_Descriptor *)a_esd);
			gf_list_add(odU->objectDescriptors, od);
			if (!bifs) {
				bifs = 3;
			} else {
				bifs = 2;
			}

#ifndef GPAC_DISABLE_AV_PARSERS
			if (a_esd->decoderConfig && (a_esd->decoderConfig->objectTypeIndication == GF_CODECID_AAC_MPEG4)
				&& a_esd->decoderConfig->decoderSpecificInfo
				&& a_esd->decoderConfig->decoderSpecificInfo->data
			) {
				GF_M4ADecSpecInfo cfg;
				gf_m4a_get_config(a_esd->decoderConfig->decoderSpecificInfo->data, a_esd->decoderConfig->decoderSpecificInfo->dataLength, &cfg);
				audioPL = cfg.audioPL;
			}
#endif
		}
	}

	/*update video cfg if needed*/
	if (v_esd) gf_isom_change_mpeg4_description(mp4file, VideoTrack, 1, v_esd);
	if (a_esd) gf_isom_change_mpeg4_description(mp4file, AudioTrack, 1, a_esd);

	/*likely 3GP or other files...*/
	if ((!a_esd && AudioTrack) || (!v_esd && VideoTrack)) return GF_OK;

	//get the OD sample
	codec = gf_odf_codec_new();
	samp = gf_isom_sample_new();
	gf_odf_codec_add_com(codec, (GF_ODCom *)odU);
	gf_odf_codec_encode(codec, 1);
	gf_odf_codec_get_au(codec, &samp->data, &samp->dataLength);
	gf_odf_codec_del(codec);
	samp->CTS_Offset = 0;
	samp->DTS = 0;
	samp->IsRAP = RAP;

	/*create the OD track*/
	odT = gf_isom_new_track(mp4file, odID, GF_ISOM_MEDIA_OD, gf_isom_get_timescale(mp4file));
	if (!odT) return gf_isom_last_error(mp4file);

	_esd = gf_odf_desc_esd_new(SLPredef_MP4);
	if (!_esd) return GF_OUT_OF_MEM;

	_esd->decoderConfig->bufferSizeDB = samp->dataLength;
	_esd->decoderConfig->objectTypeIndication = GF_CODECID_OD_V1;
	_esd->decoderConfig->streamType = GF_STREAM_OD;
	_esd->ESID = odID;
	_esd->OCRESID = no_ocr ? 0 : bifsID;
	gf_isom_new_mpeg4_description(mp4file, odT, _esd, NULL, NULL, &descIndex);
	gf_odf_desc_del((GF_Descriptor *)_esd);
	gf_isom_add_sample(mp4file, odT, 1, samp);
	gf_isom_sample_del(&samp);

	gf_isom_set_track_interleaving_group(mp4file, odT, 1);

	/*create the BIFS track*/
	bifsT = gf_isom_new_track(mp4file, bifsID, GF_ISOM_MEDIA_SCENE, gf_isom_get_timescale(mp4file));
	if (!bifsT) return gf_isom_last_error(mp4file);

	_esd = gf_odf_desc_esd_new(SLPredef_MP4);
	if (!_esd) return GF_OUT_OF_MEM;

	_esd->decoderConfig->bufferSizeDB = 20;
	_esd->decoderConfig->objectTypeIndication = GF_CODECID_BIFS_V2;
	_esd->decoderConfig->streamType = GF_STREAM_SCENE;
	_esd->ESID = bifsID;
	_esd->OCRESID = 0;

	/*rewrite ISMA BIFS cfg*/
	bs = gf_bs_new(NULL, 0, GF_BITSTREAM_WRITE);
	/*empty bifs stuff*/
	gf_bs_write_int(bs, 0, 17);
	/*command stream*/
	gf_bs_write_int(bs, 1, 1);
	/*in pixel metrics*/
	gf_bs_write_int(bs, 1, 1);
	/*with size*/
	gf_bs_write_int(bs, 1, 1);
	gf_bs_write_int(bs, w, 16);
	gf_bs_write_int(bs, h, 16);
	gf_bs_align(bs);
	gf_bs_get_content(bs, &_esd->decoderConfig->decoderSpecificInfo->data, &_esd->decoderConfig->decoderSpecificInfo->dataLength);
	gf_isom_new_mpeg4_description(mp4file, bifsT, _esd, NULL, NULL, &descIndex);
	gf_odf_desc_del((GF_Descriptor *)_esd);
	gf_bs_del(bs);
	gf_isom_set_visual_info(mp4file, bifsT, descIndex, w, h);

	samp = gf_isom_sample_new();
	samp->CTS_Offset = 0;
	samp->DTS = 0;
	switch (bifs) {
	case 1:
		if (is_image) {
			samp->data = (char *) ISMA_BIFS_IMAGE;
			samp->dataLength = 10;
		} else {
			samp->data = (char *) ISMA_GF_BIFS_VIDEO;
			samp->dataLength = 11;
		}
		break;
	case 2:
		if (is_image) {
			samp->data = (char *) ISMA_BIFS_AI;
			samp->dataLength = 15;
		} else {
			samp->data = (char *) ISMA_BIFS_AV;
			samp->dataLength = 16;
		}
		break;
	case 3:
		samp->data = (char *) ISMA_BIFS_AUDIO;
		samp->dataLength = 8;
		break;
	}

	samp->IsRAP = RAP;

	gf_isom_add_sample(mp4file, bifsT, 1, samp);
	samp->data = NULL;
	gf_isom_sample_del(&samp);
	gf_isom_set_track_interleaving_group(mp4file, bifsT, 1);

	gf_isom_set_track_enabled(mp4file, bifsT, GF_TRUE);
	gf_isom_set_track_enabled(mp4file, odT, GF_TRUE);
	gf_isom_add_track_to_root_od(mp4file, bifsT);
	gf_isom_add_track_to_root_od(mp4file, odT);

	gf_isom_set_pl_indication(mp4file, GF_ISOM_PL_SCENE, 1);
	gf_isom_set_pl_indication(mp4file, GF_ISOM_PL_GRAPHICS, 1);
	gf_isom_set_pl_indication(mp4file, GF_ISOM_PL_OD, 1);
	gf_isom_set_pl_indication(mp4file, GF_ISOM_PL_AUDIO, audioPL);
	gf_isom_set_pl_indication(mp4file, GF_ISOM_PL_VISUAL, (u8) (is_image ? 0xFE : visualPL));

	gf_isom_set_brand_info(mp4file, GF_ISOM_BRAND_MP42, 1);
	gf_isom_modify_alternate_brand(mp4file, GF_ISOM_BRAND_ISOM, GF_TRUE);
	gf_isom_modify_alternate_brand(mp4file, GF_ISOM_BRAND_3GP5, GF_TRUE);
	return GF_OK;
}


GF_EXPORT
GF_Err gf_media_make_3gpp(GF_ISOFile *mp4file)
{
	u32 Tracks, i, mType, stype, nb_vid, nb_avc, nb_aud, nb_txt, nb_non_mp4, nb_dims;
	Bool is_3g2 = 0;

	switch (gf_isom_get_mode(mp4file)) {
	case GF_ISOM_OPEN_EDIT:
	case GF_ISOM_OPEN_WRITE:
	case GF_ISOM_WRITE_EDIT:
		break;
	default:
		return GF_BAD_PARAM;
	}

	Tracks = gf_isom_get_track_count(mp4file);
	nb_vid = nb_aud = nb_txt = nb_avc = nb_non_mp4 = nb_dims = 0;

	for (i=0; i<Tracks; i++) {
		gf_isom_remove_track_from_root_od(mp4file, i+1);

		mType = gf_isom_get_media_type(mp4file, i+1);
		stype = gf_isom_get_media_subtype(mp4file, i+1, 1);
		switch (mType) {
		case GF_ISOM_MEDIA_VISUAL:
        case GF_ISOM_MEDIA_AUXV:
        case GF_ISOM_MEDIA_PICT:
			/*remove image tracks if wanted*/
			if (gf_isom_get_sample_count(mp4file, i+1)<=1) {
				GF_LOG(GF_LOG_WARNING, GF_LOG_MEDIA, ("[3GPP convert] Visual track ID %d: only one sample found\n", gf_isom_get_track_id(mp4file, i+1) ));
				//goto remove_track;
			}

			if (stype == GF_ISOM_SUBTYPE_MPEG4_CRYP) gf_isom_get_ismacryp_info(mp4file, i+1, 1, &stype, NULL, NULL, NULL, NULL, NULL, NULL, NULL);

			switch (stype) {
			case GF_ISOM_SUBTYPE_3GP_H263:
				nb_vid++;
				nb_non_mp4 ++;
				break;
			case GF_ISOM_SUBTYPE_AVC_H264:
			case GF_ISOM_SUBTYPE_AVC2_H264:
			case GF_ISOM_SUBTYPE_AVC3_H264:
			case GF_ISOM_SUBTYPE_AVC4_H264:
			case GF_ISOM_SUBTYPE_SVC_H264:
			case GF_ISOM_SUBTYPE_MVC_H264:
				nb_vid++;
				nb_avc++;
				break;
			case GF_ISOM_SUBTYPE_MPEG4:
			{
				GF_ESD *esd = gf_isom_get_esd(mp4file, i+1, 1);
				u32 oti = (esd && esd->decoderConfig) ? esd->decoderConfig->objectTypeIndication : 0;
				gf_odf_desc_del((GF_Descriptor *)esd);
				/*both MPEG4-Video and H264/AVC/SVC are supported*/
				switch (oti) {
				case GF_CODECID_MPEG4_PART2:
				case GF_CODECID_AVC:
				case GF_CODECID_SVC:
				case GF_CODECID_MVC:
					nb_vid++;
					break;
				default:
					GF_LOG(GF_LOG_INFO, GF_LOG_MEDIA, ("[3GPP convert] Video format not supported by 3GP - removing track ID %d\n", gf_isom_get_track_id(mp4file, i+1) ));
					goto remove_track;
				}
			}
			break;
			default:
				GF_LOG(GF_LOG_INFO, GF_LOG_MEDIA, ("[3GPP convert] Video format not supported by 3GP - removing track ID %d\n", gf_isom_get_track_id(mp4file, i+1) ));
				goto remove_track;
			}
			break;
		case GF_ISOM_MEDIA_AUDIO:
			if (stype == GF_ISOM_SUBTYPE_MPEG4_CRYP) gf_isom_get_ismacryp_info(mp4file, i+1, 1, &stype, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
			switch (stype) {
			case GF_ISOM_SUBTYPE_3GP_EVRC:
			case GF_ISOM_SUBTYPE_3GP_QCELP:
			case GF_ISOM_SUBTYPE_3GP_SMV:
				is_3g2 = 1;
				nb_aud++;
				break;
			case GF_ISOM_SUBTYPE_3GP_AMR:
			case GF_ISOM_SUBTYPE_3GP_AMR_WB:
				nb_aud++;
				nb_non_mp4 ++;
				break;
			case GF_ISOM_SUBTYPE_MPEG4:
			{
				GF_ESD *esd = gf_isom_get_esd(mp4file, i+1, 1);
				u32 oti = (esd && esd->decoderConfig) ? esd->decoderConfig->objectTypeIndication : 0;
				gf_odf_desc_del((GF_Descriptor *)esd);

				switch (oti) {
				case GF_CODECID_QCELP:
				case GF_CODECID_EVRC:
				case GF_CODECID_SMV:
					is_3g2 = 1;
				case GF_CODECID_AAC_MPEG4:
					nb_aud++;
					break;
				default:
					GF_LOG(GF_LOG_INFO, GF_LOG_MEDIA, ("[3GPP convert] Audio format not supported by 3GP - removing track ID %d\n", gf_isom_get_track_id(mp4file, i+1) ));
					goto remove_track;
				}
			}
			break;
			default:
				GF_LOG(GF_LOG_INFO, GF_LOG_MEDIA, ("[3GPP convert] Audio format not supported by 3GP - removing track ID %d\n", gf_isom_get_track_id(mp4file, i+1) ));
				goto remove_track;
			}
			break;

		case GF_ISOM_MEDIA_SUBT:
			gf_isom_set_media_type(mp4file, i+1, GF_ISOM_MEDIA_TEXT);
		case GF_ISOM_MEDIA_TEXT:
		case GF_ISOM_MEDIA_MPEG_SUBT:
			nb_txt++;
			break;

		case GF_ISOM_MEDIA_SCENE:
			if (stype == GF_ISOM_MEDIA_DIMS) {
				nb_dims++;
				break;
			}
		/*clean file*/
		default:
			if (mType==GF_ISOM_MEDIA_HINT) {
				GF_LOG(GF_LOG_INFO, GF_LOG_MEDIA, ("[3GPP convert] Removing Hint track ID %d\n", gf_isom_get_track_id(mp4file, i+1) ));
			} else {
				GF_LOG(GF_LOG_INFO, GF_LOG_MEDIA, ("[3GPP convert] Removing system track ID %d\n", gf_isom_get_track_id(mp4file, i+1) ));
			}

remove_track:
			gf_isom_remove_track(mp4file, i+1);
			i -= 1;
			Tracks = gf_isom_get_track_count(mp4file);
			break;
		}
	}

	/*no more IOD*/
	gf_isom_remove_root_od(mp4file);

	if (is_3g2) {
		gf_isom_set_brand_info(mp4file, GF_ISOM_BRAND_3G2A, 65536);
		gf_isom_modify_alternate_brand(mp4file, GF_ISOM_BRAND_3GP6, GF_FALSE);
		gf_isom_modify_alternate_brand(mp4file, GF_ISOM_BRAND_3GP5, GF_FALSE);
		gf_isom_modify_alternate_brand(mp4file, GF_ISOM_BRAND_3GG6, GF_FALSE);
		GF_LOG(GF_LOG_INFO, GF_LOG_MEDIA, ("[3GPP convert] Setting major brand to 3GPP2\n"));
	} else {
		/*update FType*/
		if ((nb_vid>1) || (nb_aud>1) || (nb_txt>1)) {
			/*3GPP general purpose*/
			gf_isom_set_brand_info(mp4file, GF_ISOM_BRAND_3GG6, 1024);
			gf_isom_modify_alternate_brand(mp4file, GF_ISOM_BRAND_3GP6, GF_FALSE);
			gf_isom_modify_alternate_brand(mp4file, GF_ISOM_BRAND_3GP5, GF_FALSE);
			gf_isom_modify_alternate_brand(mp4file, GF_ISOM_BRAND_3GP4, GF_FALSE);
			GF_LOG(GF_LOG_INFO, GF_LOG_MEDIA, ("[3GPP convert] Setting major brand to 3GPP Generic file\n"));
		} else if (nb_txt) {
			gf_isom_set_brand_info(mp4file, GF_ISOM_BRAND_3GP6, 1024);
			gf_isom_modify_alternate_brand(mp4file, GF_ISOM_BRAND_3GP5, GF_TRUE);
			gf_isom_modify_alternate_brand(mp4file, GF_ISOM_BRAND_3GP4, GF_TRUE);
			gf_isom_modify_alternate_brand(mp4file, GF_ISOM_BRAND_3GG6, GF_FALSE);
			GF_LOG(GF_LOG_INFO, GF_LOG_MEDIA, ("[3GPP convert] Setting major brand to 3GPP V6 file\n"));
		} else if (nb_avc) {
			gf_isom_set_brand_info(mp4file, GF_ISOM_BRAND_3GP6, 0/*1024*/);
			gf_isom_modify_alternate_brand(mp4file, GF_ISOM_BRAND_AVC1, GF_TRUE);
			gf_isom_modify_alternate_brand(mp4file, GF_ISOM_BRAND_3GP5, GF_FALSE);
			gf_isom_modify_alternate_brand(mp4file, GF_ISOM_BRAND_3GP4, GF_FALSE);
			GF_LOG(GF_LOG_INFO, GF_LOG_MEDIA, ("[3GPP convert] Setting major brand to 3GPP V6 file + AVC compatible\n"));
		} else {
			gf_isom_set_brand_info(mp4file, GF_ISOM_BRAND_3GP5, 0/*1024*/);
			gf_isom_modify_alternate_brand(mp4file, GF_ISOM_BRAND_3GP6, 0);
			gf_isom_modify_alternate_brand(mp4file, GF_ISOM_BRAND_3GP4, GF_TRUE);
			gf_isom_modify_alternate_brand(mp4file, GF_ISOM_BRAND_3GG6, GF_FALSE);
			GF_LOG(GF_LOG_INFO, GF_LOG_MEDIA, ("[3GPP convert] Setting major brand to 3GPP V5 file\n"));
		}
	}
	/*add/remove MP4 brands and add isom*/
	gf_isom_modify_alternate_brand(mp4file, GF_ISOM_BRAND_MP41, (u8) ((nb_avc||is_3g2||nb_non_mp4) ? GF_FALSE : GF_TRUE));
	gf_isom_modify_alternate_brand(mp4file, GF_ISOM_BRAND_MP42, (u8) (nb_non_mp4 ? GF_FALSE : GF_TRUE));
	gf_isom_modify_alternate_brand(mp4file, GF_ISOM_BRAND_ISOM, GF_TRUE);
	return GF_OK;
}

GF_EXPORT
GF_Err gf_media_make_psp(GF_ISOFile *mp4)
{
	u32 i, count;
	u32 nb_a, nb_v;
	/*psp track UUID*/
	bin128 psp_track_uuid = {0x55, 0x53, 0x4D, 0x54, 0x21, 0xD2, 0x4F, 0xCE, 0xBB, 0x88, 0x69, 0x5C, 0xFA, 0xC9, 0xC7, 0x40};
	u8 psp_track_sig [] = {0x00, 0x00, 0x00, 0x1C, 0x4D, 0x54, 0x44, 0x54, 0x00, 0x01, 0x00, 0x12, 0x00, 0x00, 0x00, 0x0A, 0x55, 0xC4, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00};
	/*psp mov UUID*/
	//bin128 psp_uuid = {0x50, 0x52, 0x4F, 0x46, 0x21, 0xD2, 0x4F, 0xCE, 0xBB, 0x88, 0x69, 0x5C, 0xFA, 0xC9, 0xC7, 0x40};

	nb_a = nb_v = 0;
	count = gf_isom_get_track_count(mp4);
	for (i=0; i<count; i++) {
		switch (gf_isom_get_media_type(mp4, i+1)) {
		case GF_ISOM_MEDIA_VISUAL:
			nb_v++;
			break;
		case GF_ISOM_MEDIA_AUDIO:
			nb_a++;
			break;
		}
	}
	if ((nb_v != 1) && (nb_a!=1)) {
		GF_LOG(GF_LOG_ERROR, GF_LOG_MEDIA, ("[PSP convert] Movies need one audio track and one video track\n" ));
		return GF_BAD_PARAM;
	}
	for (i=0; i<count; i++) {
		switch (gf_isom_get_media_type(mp4, i+1)) {
		case GF_ISOM_MEDIA_VISUAL:
		case GF_ISOM_MEDIA_AUDIO:
			/*if no edit list, add one*/
			if (!gf_isom_get_edits_count(mp4, i+1)) {
				GF_ISOSample *samp = gf_isom_get_sample_info(mp4, i+1, 1, NULL, NULL);
				if (samp) {
					gf_isom_append_edit(mp4, i+1, gf_isom_get_track_duration(mp4, i+1), samp->CTS_Offset, GF_ISOM_EDIT_NORMAL);
					gf_isom_sample_del(&samp);
				}
			}
			/*add PSP UUID*/
			gf_isom_remove_uuid(mp4, i+1, psp_track_uuid);
			gf_isom_add_uuid(mp4, i+1, psp_track_uuid, (char *) psp_track_sig, 28);
			break;
		default:
			GF_LOG(GF_LOG_INFO, GF_LOG_MEDIA, ("[PSP convert] Removing track ID %d\n", gf_isom_get_track_id(mp4, i+1) ));
			gf_isom_remove_track(mp4, i+1);
			i -= 1;
			count -= 1;
			break;
		}
	}
	gf_isom_set_brand_info(mp4, GF_ISOM_BRAND_MSNV, 0);
	gf_isom_modify_alternate_brand(mp4, GF_ISOM_BRAND_MSNV, GF_TRUE);
	return GF_OK;
}

GF_Err gf_media_get_color_info(GF_ISOFile *file, u32 track, u32 sampleDescriptionIndex, u32 *colour_type, u16 *colour_primaries, u16 *transfer_characteristics, u16 *matrix_coefficients, Bool *full_range_flag)
{
#ifndef GPAC_DISABLE_AV_PARSERS
	u32 stype = gf_isom_get_media_subtype(file, track, sampleDescriptionIndex);
	if ((stype==GF_ISOM_SUBTYPE_AVC_H264)
			|| (stype==GF_ISOM_SUBTYPE_AVC2_H264)
			|| (stype==GF_ISOM_SUBTYPE_AVC3_H264)
			|| (stype==GF_ISOM_SUBTYPE_AVC4_H264)
	) {
		AVCState *avc_state;
		GF_AVCConfig *avcc = gf_isom_avc_config_get(file, track, sampleDescriptionIndex);
		u32 i;
		s32 idx;
		GF_NALUFFParam *slc;

		GF_SAFEALLOC(avc_state, AVCState);
		if (!avc_state) return GF_OUT_OF_MEM;
		avc_state->sps_active_idx = -1;

		i=0;
		while ((slc = (GF_NALUFFParam *)gf_list_enum(avcc->sequenceParameterSets, &i))) {
			idx = gf_avc_read_sps(slc->data, slc->size, avc_state, 0, NULL);

			if (idx<0) continue;
			if (! avc_state->sps[idx].vui_parameters_present_flag )
				continue;

			*colour_type = avc_state->sps[idx].vui.video_format;
			*colour_primaries = avc_state->sps[idx].vui.colour_primaries;
			*transfer_characteristics = avc_state->sps[idx].vui.transfer_characteristics;
			*matrix_coefficients = avc_state->sps[idx].vui.matrix_coefficients;
			*full_range_flag = avc_state->sps[idx].vui.video_full_range_flag;
			gf_odf_avc_cfg_del(avcc);
			gf_free(avc_state);
			return GF_OK;
		}
		gf_odf_avc_cfg_del(avcc);
		gf_free(avc_state);
		return GF_NOT_FOUND;
	}
	if ((stype==GF_ISOM_SUBTYPE_HEV1)
			|| (stype==GF_ISOM_SUBTYPE_HEV2)
			|| (stype==GF_ISOM_SUBTYPE_HVC1)
			|| (stype==GF_ISOM_SUBTYPE_HVC2)
			|| (stype==GF_ISOM_SUBTYPE_LHV1)
			|| (stype==GF_ISOM_SUBTYPE_LHE1)
	) {
		HEVCState *hvc_state;
		GF_HEVCConfig *hvcc = gf_isom_hevc_config_get(file, track, sampleDescriptionIndex);
		u32 i;
		GF_NALUFFParamArray *pa;

		GF_SAFEALLOC(hvc_state, HEVCState);
		if (!hvc_state) return GF_OUT_OF_MEM;
		hvc_state->sps_active_idx = -1;

		i=0;
		while ((pa = (GF_NALUFFParamArray *)gf_list_enum(hvcc->param_array, &i))) {
			GF_NALUFFParam *slc;
			u32 j;
			s32 idx;
			if (pa->type != GF_HEVC_NALU_SEQ_PARAM) continue;

			j=0;
			while ((slc = (GF_NALUFFParam *)gf_list_enum(pa->nalus, &j))) {
				idx = gf_hevc_read_sps(slc->data, slc->size, hvc_state);

				if (idx<0) continue;
				if (! hvc_state->sps[idx].vui_parameters_present_flag)
					continue;

				*colour_type = hvc_state->sps[idx].video_format;
				*colour_primaries = hvc_state->sps[idx].colour_primaries;
				*transfer_characteristics = hvc_state->sps[idx].transfer_characteristic;
				*matrix_coefficients = hvc_state->sps[idx].matrix_coeffs;
				*full_range_flag = hvc_state->sps[idx].video_full_range_flag;
				gf_odf_hevc_cfg_del(hvcc);
				gf_free(hvc_state);
				return GF_OK;
			}
		}
		gf_odf_hevc_cfg_del(hvcc);
		gf_free(hvc_state);
		return GF_NOT_FOUND;
	}
	if (stype==GF_ISOM_SUBTYPE_AV01) {
		AV1State *av1_state;

		GF_SAFEALLOC(av1_state, AV1State);
		if (!av1_state) return GF_OUT_OF_MEM;
		gf_av1_init_state(av1_state);
		av1_state->config = gf_isom_av1_config_get(file, track, sampleDescriptionIndex);
		if (av1_state->config) {
			u32 i;
			for (i=0; i<gf_list_count(av1_state->config->obu_array); i++) {
				GF_BitStream *bs;
				ObuType obu_type = 0;
				u32 hdr_size = 0;
				u64 obu_size = 0;
				GF_AV1_OBUArrayEntry *obu = gf_list_get(av1_state->config->obu_array, i);
				bs = gf_bs_new(obu->obu, (u32) obu->obu_length, GF_BITSTREAM_READ);
				gf_av1_parse_obu(bs, &obu_type, &obu_size, &hdr_size, av1_state);
				gf_bs_del(bs);

				if (av1_state->color_description_present_flag) {
					*colour_type = 0;
					*colour_primaries = av1_state->color_primaries;
					*transfer_characteristics = av1_state->transfer_characteristics;
					*matrix_coefficients = av1_state->matrix_coefficients;
					*full_range_flag = av1_state->color_range;
					if (av1_state->config) gf_odf_av1_cfg_del(av1_state->config);
					gf_av1_reset_state(av1_state, GF_TRUE);
					gf_free(av1_state);
					return GF_OK;
				}
			}
		}
		if (av1_state->config) gf_odf_av1_cfg_del(av1_state->config);
		gf_av1_reset_state(av1_state, GF_TRUE);
		gf_free(av1_state);
		return GF_NOT_FOUND;
	}

#endif
	return GF_NOT_SUPPORTED;
}

GF_Err gf_isom_set_meta_qt(GF_ISOFile *file);

GF_EXPORT
GF_Err gf_media_check_qt_prores(GF_ISOFile *mp4)
{
	u32 i, count, timescale, def_dur=0, video_tk=0;
	u32 prores_type = 0;
	GF_Err e;
	u32 colour_type=0;
	u16 colour_primaries=0, transfer_characteristics=0, matrix_coefficients=0;
	Bool full_range_flag=GF_FALSE;
	u32 hspacing=0, vspacing=0;
	u32 nb_video_tracks;
	u32 target_ts = 0, w=0, h=0, chunk_size=0;

	nb_video_tracks = 0;

	count = gf_isom_get_track_count(mp4);
	for (i=0; i<count; i++) {
		u32 mtype = gf_isom_get_media_type(mp4, i+1);
		if (mtype!=GF_ISOM_MEDIA_VISUAL) continue;
		nb_video_tracks++;
		if (!video_tk)
			video_tk = i+1;
	}

	if ((nb_video_tracks==1) && video_tk) {
		u32 video_subtype = gf_isom_get_media_subtype(mp4, video_tk, 1);
		switch (video_subtype) {
		case GF_QT_SUBTYPE_APCH:
		case GF_QT_SUBTYPE_APCO:
		case GF_QT_SUBTYPE_APCN:
		case GF_QT_SUBTYPE_APCS:
		case GF_QT_SUBTYPE_AP4X:
		case GF_QT_SUBTYPE_AP4H:
			prores_type=video_subtype;
			break;
		}
	}

	GF_LOG(GF_LOG_INFO, GF_LOG_MEDIA, ("[QTFF/ProRes] Adjusting %s compliancy\n", prores_type ? "ProRes" : "QTFF"));

	//adjust audio tracks
	for (i=0; i<count; i++) {
		u32 mtype = gf_isom_get_media_type(mp4, i+1);

		//remove bitrate info (isobmff)
		gf_isom_update_bitrate(mp4, i+1, 1, 0, 0, 0);

		if (mtype==GF_ISOM_MEDIA_AUDIO) {
			u32 sr, nb_ch, bps;
			gf_isom_get_audio_info(mp4, i+1, 1, &sr, &nb_ch, &bps);
			gf_isom_set_audio_info(mp4, i+1, 1, sr, nb_ch, bps, GF_IMPORT_AUDIO_SAMPLE_ENTRY_v1_QTFF);

			gf_isom_hint_max_chunk_duration(mp4, i+1, gf_isom_get_media_timescale(mp4, i+1) / 2);
			continue;
		}
	}
	//make QT
	gf_isom_remove_root_od(mp4);
	if (gf_isom_get_mode(mp4) != GF_ISOM_OPEN_WRITE) {
		gf_isom_set_brand_info(mp4, GF_ISOM_BRAND_QT, 512);
		gf_isom_reset_alt_brands(mp4);
	} else {
		u32 brand, version;
		gf_isom_get_brand_info(mp4, &brand, &version, NULL);
		if (brand != GF_ISOM_BRAND_QT) {
			GF_LOG(GF_LOG_WARNING, GF_LOG_MEDIA, ("[ProRes] Cannot change brand from \"%s\" to \"qt  \", flat storage used. Try using different storage mode\n", gf_4cc_to_str(brand)));
		}
	}

	gf_isom_set_meta_qt(mp4);

	if (!video_tk) {
		GF_LOG(GF_LOG_DEBUG, GF_LOG_MEDIA, ("[QTFF] No visual track\n"));
		return GF_OK;
	}

	if (nb_video_tracks>1) {
		if (prores_type) {
			GF_LOG(GF_LOG_ERROR, GF_LOG_MEDIA, ("QTFF] cannot adjust params to prores, %d video tracks present\n", nb_video_tracks));
			return GF_BAD_PARAM;
		}
		GF_LOG(GF_LOG_DEBUG, GF_LOG_MEDIA, ("[ProRes] no prores codec found but %d video tracks, not adjusting file\n", nb_video_tracks));
		return GF_OK;
	}

	if (prores_type) {
		char *comp_name = NULL;
		switch (prores_type) {
		case GF_QT_SUBTYPE_APCH: comp_name = "\x0013""Apple ProRes 422 HQ"; break;
		case GF_QT_SUBTYPE_APCO: comp_name = "\x0016""Apple ProRes 422 Proxy"; break;
		case GF_QT_SUBTYPE_APCN: comp_name = "\x0010""Apple ProRes 422"; break;
		case GF_QT_SUBTYPE_APCS: comp_name = "\x0013""Apple ProRes 422 LT"; break;
		case GF_QT_SUBTYPE_AP4X: comp_name = "\x0014""Apple ProRes 4444 XQ"; break;
		case GF_QT_SUBTYPE_AP4H: comp_name = "\x0011""Apple ProRes 4444"; break;
		}
		gf_isom_update_video_sample_entry_fields(mp4, video_tk, 1, 0, GF_4CC('a','p','p','l'), 0, 0x3FF, 72<<16, 72<<16, 1, comp_name, -1);
	}

	timescale = gf_isom_get_media_timescale(mp4, video_tk);
	def_dur = gf_isom_get_constant_sample_duration(mp4, video_tk);
	if (!def_dur) {
		def_dur = gf_isom_get_sample_duration(mp4, video_tk, 2);
		if (!def_dur) {
			def_dur = gf_isom_get_sample_duration(mp4, video_tk, 1);
		}
	}
	if (!def_dur) {
		GF_LOG(GF_LOG_ERROR, GF_LOG_MEDIA, ("[ProRes] cannot estimate default sample duration for video track\n"));
		return GF_NON_COMPLIANT_BITSTREAM;
	}

	gf_isom_get_pixel_aspect_ratio(mp4, video_tk, 1, &hspacing, &vspacing);
	//force 1:1
	if ((hspacing<=1) || (vspacing<=1)) {
		hspacing = vspacing = 1;
		gf_isom_set_pixel_aspect_ratio(mp4, video_tk, 1, 1, 1, GF_TRUE);
	}

	//patch enof/prof/clef
	if (prores_type) {
		gf_isom_update_aperture_info(mp4, video_tk, GF_FALSE);
	}

	e = gf_isom_get_color_info(mp4, video_tk, 1, &colour_type, &colour_primaries, &transfer_characteristics, &matrix_coefficients, &full_range_flag);
	if (e==GF_NOT_FOUND) {
		colour_primaries = transfer_characteristics = matrix_coefficients = 0;
		if (prores_type) {
			u32 di;
			GF_ISOSample *s = gf_isom_get_sample(mp4, video_tk, 1, &di);
			if (s && s->dataLength>24) {
				GF_BitStream *bs = gf_bs_new(s->data, s->dataLength, GF_BITSTREAM_READ);
				gf_bs_read_u32(bs); //frame size
				gf_bs_read_u32(bs); //frame ID
				gf_bs_read_u32(bs); //frame header size + reserved + bs version
				gf_bs_read_u32(bs); //encoder id
				gf_bs_read_u32(bs); //w and h
				gf_bs_read_u16(bs); //bunch of flags
				colour_primaries = gf_bs_read_u8(bs);
				transfer_characteristics = gf_bs_read_u8(bs);
				matrix_coefficients = gf_bs_read_u8(bs);
				gf_bs_del(bs);
			}
			gf_isom_sample_del(&s);
		} else {
			e = gf_media_get_color_info(mp4, video_tk, 1, &colour_type, &colour_primaries, &transfer_characteristics, &matrix_coefficients, &full_range_flag);
			if (e)
				colour_primaries=0;
		}
		if (!colour_primaries) {
			GF_LOG(GF_LOG_WARNING, GF_LOG_MEDIA, ("[ProRes] No color info present in visual track, defaulting to BT709\n"));
			colour_primaries = 1;
			transfer_characteristics = 1;
			matrix_coefficients = 1;
		} else {
			GF_LOG(GF_LOG_INFO, GF_LOG_MEDIA, ("[ProRes] No color info present in visual track, extracting from %s\n", prores_type ? "first ProRes frame" : "sample description"));
		}
		gf_isom_set_visual_color_info(mp4, video_tk, 1, GF_4CC('n','c','l','c'), colour_primaries, transfer_characteristics, matrix_coefficients, GF_FALSE, NULL, 0);
	} else if (e) {
		return e;
	}
	gf_isom_get_visual_info(mp4, video_tk, 1, &w, &h);

	u32 ifps;
	Double FPS = timescale;
	FPS /= def_dur;
	FPS *= 100;
	ifps = (u32) FPS;
	if (ifps>= 2996 && ifps<=2998) target_ts = 30000;	//29.97
	else if (ifps>= 2999 && ifps<=3001) target_ts = 3000; //30
	else if (ifps>= 2495 && ifps<=2505) target_ts = 2500; //25
	else if (!(def_dur%125) && !(timescale % 2997)) target_ts = 2997; //23.97
	else if (ifps >= 2396 && ifps<=2398) target_ts = 24000; //23.97
	else if ((ifps>=2399) && (ifps<=2401)) target_ts = 2400; //24
	else if (ifps>= 4990 && ifps<=5010) target_ts = 5000; //50
	else if (ifps>= 5993 && ifps<=5995) target_ts = 60000; //59.94
	else if (ifps>= 5996 && ifps<=6004) target_ts = 6000; //60

	if (!target_ts) {
		if (prores_type) {
			GF_LOG(GF_LOG_ERROR, GF_LOG_MEDIA, ("[ProRes] Unrecognized frame rate %g\n", ((Double)timescale)/def_dur ));
			return GF_NON_COMPLIANT_BITSTREAM;
		} else {
			target_ts = timescale;
		}
	}

	if (target_ts != timescale) {
		GF_LOG(GF_LOG_INFO, GF_LOG_MEDIA, ("[ProRes] Adjusting timescale to %d\n", target_ts));
		gf_isom_set_media_timescale(mp4, video_tk, target_ts, 0, 0);
	}
	gf_isom_set_timescale(mp4, target_ts);
	if ((w<=720) && (h<=576)) chunk_size = 2000000;
	else chunk_size = 4000000;

	gf_isom_set_interleave_time(mp4, 500);
	gf_isom_hint_max_chunk_size(mp4, video_tk, chunk_size);

	return GF_OK;
}

#endif /*GPAC_DISABLE_ISOM_WRITE*/

GF_EXPORT
GF_ESD *gf_media_map_esd(GF_ISOFile *mp4, u32 track, u32 stsd_idx)
{
	GF_ESD *esd;
	u32 subtype;

	if (!stsd_idx) stsd_idx = 1;
	subtype = gf_isom_get_media_subtype(mp4, track, stsd_idx);
	/*all types with an official MPEG-4 mapping*/
	switch (subtype) {
	case GF_ISOM_SUBTYPE_MPEG4:
	case GF_ISOM_SUBTYPE_MPEG4_CRYP:
	case GF_ISOM_SUBTYPE_AVC_H264:
	case GF_ISOM_SUBTYPE_AVC2_H264:
	case GF_ISOM_SUBTYPE_AVC3_H264:
	case GF_ISOM_SUBTYPE_AVC4_H264:
	case GF_ISOM_SUBTYPE_SVC_H264:
	case GF_ISOM_SUBTYPE_MVC_H264:
	case GF_ISOM_SUBTYPE_3GP_EVRC:
	case GF_ISOM_SUBTYPE_3GP_QCELP:
	case GF_ISOM_SUBTYPE_3GP_SMV:
	case GF_ISOM_SUBTYPE_HVC1:
	case GF_ISOM_SUBTYPE_HEV1:
	case GF_ISOM_SUBTYPE_HVC2:
	case GF_ISOM_SUBTYPE_HEV2:
	case GF_ISOM_SUBTYPE_LHV1:
	case GF_ISOM_SUBTYPE_LHE1:
	case GF_ISOM_SUBTYPE_AV01:
	case GF_ISOM_SUBTYPE_VP09:
	case GF_ISOM_SUBTYPE_VP08:
	case GF_ISOM_SUBTYPE_VVC1:
	case GF_ISOM_SUBTYPE_VVI1:
	case GF_ISOM_SUBTYPE_MH3D_MHA1:
	case GF_ISOM_SUBTYPE_MH3D_MHA2:
	case GF_ISOM_SUBTYPE_MH3D_MHM1:
	case GF_ISOM_SUBTYPE_MH3D_MHM2:
		return gf_isom_get_esd(mp4, track, stsd_idx);
	}

	if (subtype == GF_ISOM_SUBTYPE_OPUS) {
		esd = gf_isom_get_esd(mp4, track, 1);
		if (!esd) return NULL;
		if (!esd->decoderConfig) {
			gf_odf_desc_del((GF_Descriptor *)esd);
			return NULL;
		}
		esd->decoderConfig->objectTypeIndication = GF_CODECID_OPUS;
		return esd;
	}

	if (subtype == GF_ISOM_SUBTYPE_3GP_DIMS) {
		GF_BitStream *bs;
		GF_DIMSDescription dims;
		esd = gf_odf_desc_esd_new(0);
		if (!esd) return NULL;
		esd->slConfig->timestampResolution = gf_isom_get_media_timescale(mp4, track);
		esd->ESID = gf_isom_get_track_id(mp4, track);
		esd->OCRESID = esd->ESID;
		esd->decoderConfig->streamType = GF_STREAM_SCENE;
		/*use private DSI*/
		esd->decoderConfig->objectTypeIndication = GF_CODECID_DIMS;
		gf_isom_get_dims_description(mp4, track, 1, &dims);
		bs = gf_bs_new(NULL, 0, GF_BITSTREAM_WRITE);
		/*format ext*/
		gf_bs_write_u8(bs, dims.profile);
		gf_bs_write_u8(bs, dims.level);
		gf_bs_write_int(bs, dims.pathComponents, 4);
		gf_bs_write_int(bs, dims.fullRequestHost, 1);
		gf_bs_write_int(bs, dims.streamType, 1);
		gf_bs_write_int(bs, dims.containsRedundant, 2);

		if (dims.textEncoding)
			gf_bs_write_data(bs, (char*)dims.textEncoding, (u32) strlen(dims.textEncoding)+1);
		else
			gf_bs_write_data(bs, "", 1);

		if (dims.contentEncoding)
			gf_bs_write_data(bs, (char*)dims.contentEncoding, (u32) strlen(dims.contentEncoding)+1);
		else
			gf_bs_write_data(bs, "", 1);

		gf_bs_get_content(bs, &esd->decoderConfig->decoderSpecificInfo->data, &esd->decoderConfig->decoderSpecificInfo->dataLength);
		gf_bs_del(bs);
		return esd;
	}
	if (mp4->convert_streaming_text && ((subtype == GF_ISOM_SUBTYPE_TEXT) || (subtype == GF_ISOM_SUBTYPE_TX3G))
	) {
		return gf_isom_get_esd(mp4, track, stsd_idx);
	}
	return NULL;
}

GF_EXPORT
GF_ESD *gf_media_map_item_esd(GF_ISOFile *mp4, u32 item_id)
{
	u32 item_type;
	u32 prot_scheme, prot_scheme_version;
	Bool is_self_ref;
	const char *name;
	const char *mime;
	const char *encoding;
	const char *url;
	const char *urn;
	GF_ESD *esd;
	GF_Err e;

	u32 item_idx = gf_isom_get_meta_item_by_id(mp4, GF_TRUE, 0, item_id);
	if (!item_idx) return NULL;

	e = gf_isom_get_meta_item_info(mp4, GF_TRUE, 0, item_idx, &item_id, &item_type, &prot_scheme, &prot_scheme_version, &is_self_ref, &name, &mime, &encoding, &url, &urn);
	if (e != GF_OK) return NULL;

	GF_ImageItemProperties *props;
	GF_SAFEALLOC(props, GF_ImageItemProperties);
	if (!props) return NULL;
	esd = gf_odf_desc_esd_new(0);
	if (!esd) {
		gf_free(props);
		return NULL;
	}

	if (item_type == GF_ISOM_SUBTYPE_HVC1) {
		if (item_id > (1 << 16)) {
			GF_LOG(GF_LOG_WARNING, GF_LOG_CORE, ("Item ID greater than 16 bits, does not fit on ES ID\n"));
		}
		esd->ESID = (u16)item_id;
		esd->OCRESID = esd->ESID;
		esd->decoderConfig->streamType = GF_STREAM_VISUAL;
		esd->decoderConfig->objectTypeIndication = GF_CODECID_HEVC;
		e = gf_isom_get_meta_image_props(mp4, GF_TRUE, 0, item_id, props, NULL);
		if (e == GF_OK && props->config) {
			GF_HEVCConfigurationBox *hvcc = props->config;
			if (hvcc->type==GF_ISOM_BOX_TYPE_HVCC) {
				gf_odf_hevc_cfg_write(hvcc->config, &esd->decoderConfig->decoderSpecificInfo->data, &esd->decoderConfig->decoderSpecificInfo->dataLength);
			} else {
				gf_odf_desc_del((GF_Descriptor*)esd);
				gf_free(props);
				return NULL;
			}
		}
		esd->slConfig->hasRandomAccessUnitsOnlyFlag = 1;
		esd->slConfig->useTimestampsFlag = 1;
		esd->slConfig->timestampResolution = 1000;
		gf_free(props);
		return esd;
	}

	if (item_type == GF_ISOM_SUBTYPE_AVC_H264) {
		if (item_id > (1 << 16)) {
			GF_LOG(GF_LOG_WARNING, GF_LOG_CORE, ("Item ID greater than 16 bits, does not fit on ES ID\n"));
		}
		esd->ESID = (u16)item_id;
		esd->OCRESID = esd->ESID;
		esd->decoderConfig->streamType = GF_STREAM_VISUAL;
		esd->decoderConfig->objectTypeIndication = GF_CODECID_AVC;
		e = gf_isom_get_meta_image_props(mp4, GF_TRUE, 0, item_id, props, NULL);
		if (e == GF_OK && props->config) {
			GF_AVCConfigurationBox *avcc = props->config;
			if (avcc->type==GF_ISOM_BOX_TYPE_AVCC) {
				gf_odf_avc_cfg_write(avcc->config, &esd->decoderConfig->decoderSpecificInfo->data, &esd->decoderConfig->decoderSpecificInfo->dataLength);
			} else {
				gf_odf_desc_del((GF_Descriptor*)esd);
				gf_free(props);
				return NULL;
			}
		}
		esd->slConfig->hasRandomAccessUnitsOnlyFlag = 1;
		esd->slConfig->useTimestampsFlag = 1;
		esd->slConfig->timestampResolution = 1000;
		gf_free(props);
		return esd;
	}

	if (item_type == GF_ISOM_SUBTYPE_AV01) {
		if (item_id > (1 << 16)) {
			GF_LOG(GF_LOG_WARNING, GF_LOG_CORE, ("Item ID greater than 16 bits, does not fit on ES ID\n"));
		}
		esd->ESID = (u16)item_id;
		esd->OCRESID = esd->ESID;
		esd->decoderConfig->streamType = GF_STREAM_VISUAL;
		esd->decoderConfig->objectTypeIndication = GF_CODECID_AV1;
		e = gf_isom_get_meta_image_props(mp4, GF_TRUE, 0, item_id, props, NULL);
		if (e == GF_OK && props->config) {
			GF_AV1ConfigurationBox *av1c = props->config;
			if (av1c->type==GF_ISOM_BOX_TYPE_AV1C) {
				gf_odf_av1_cfg_write(av1c->config, &esd->decoderConfig->decoderSpecificInfo->data, &esd->decoderConfig->decoderSpecificInfo->dataLength);
			} else {
				gf_odf_desc_del((GF_Descriptor*)esd);
				gf_free(props);
				return NULL;
			}
		}
		esd->slConfig->hasRandomAccessUnitsOnlyFlag = 1;
		esd->slConfig->useTimestampsFlag = 1;
		esd->slConfig->timestampResolution = 1000;
		gf_free(props);
		return esd;
	}

	if ((item_type == GF_ISOM_SUBTYPE_JPEG) || (mime && !strcmp(mime, "image/jpeg")) ){
		if (item_id > (1 << 16)) {
			GF_LOG(GF_LOG_WARNING, GF_LOG_CORE, ("Item ID greater than 16 bits, does not fit on ES ID\n"));
		}
		esd->ESID = (u16)item_id;
		esd->OCRESID = esd->ESID;
		esd->decoderConfig->streamType = GF_STREAM_VISUAL;
		esd->decoderConfig->objectTypeIndication = GF_CODECID_JPEG;
		e = gf_isom_get_meta_image_props(mp4, GF_TRUE, 0, item_id, props, NULL);
		if (e == GF_OK && props->config) {
			GF_LOG(GF_LOG_WARNING, GF_LOG_CORE, ("JPEG image item decoder config not supported, patch welcome\n"));
		}
		esd->slConfig->hasRandomAccessUnitsOnlyFlag = 1;
		esd->slConfig->useTimestampsFlag = 1;
		esd->slConfig->timestampResolution = 1000;
		gf_free(props);
		return esd;
	}

	if ((item_type == GF_ISOM_SUBTYPE_PNG) || (mime && !strcmp(mime, "image/png")) ){
		if (item_id > (1 << 16)) {
			GF_LOG(GF_LOG_WARNING, GF_LOG_CORE, ("Item ID greater than 16 bits, does not fit on ES ID\n"));
		}
		esd->ESID = (u16)item_id;
		esd->OCRESID = esd->ESID;
		esd->decoderConfig->streamType = GF_STREAM_VISUAL;
		esd->decoderConfig->objectTypeIndication = GF_CODECID_PNG;
		e = gf_isom_get_meta_image_props(mp4, GF_TRUE, 0, item_id, props, NULL);
		if (e) {
			GF_LOG(GF_LOG_WARNING, GF_LOG_CORE, ("Error fetching item properties %s\n", gf_error_to_string(e) ));
		}
		esd->slConfig->hasRandomAccessUnitsOnlyFlag = 1;
		esd->slConfig->useTimestampsFlag = 1;
		esd->slConfig->timestampResolution = 1000;
		gf_free(props);
		return esd;
	}

	if (item_type == GF_ISOM_SUBTYPE_VVC1) {
		if (item_id > (1 << 16)) {
			GF_LOG(GF_LOG_WARNING, GF_LOG_CORE, ("Item ID greater than 16 bits, does not fit on ES ID\n"));
		}
		esd->ESID = (u16)item_id;
		esd->OCRESID = esd->ESID;
		esd->decoderConfig->streamType = GF_STREAM_VISUAL;
		esd->decoderConfig->objectTypeIndication = GF_CODECID_VVC;
		e = gf_isom_get_meta_image_props(mp4, GF_TRUE, 0, item_id, props, NULL);
		if (e == GF_OK && props->config) {
			GF_VVCConfigurationBox *vvcc = props->config;
			if (vvcc->type == GF_ISOM_BOX_TYPE_VVCC) {
				gf_odf_vvc_cfg_write(vvcc->config, &esd->decoderConfig->decoderSpecificInfo->data, &esd->decoderConfig->decoderSpecificInfo->dataLength);
			} else {
				gf_odf_desc_del((GF_Descriptor*)esd);
				gf_free(props);
				return NULL;
			}
		}
		esd->slConfig->hasRandomAccessUnitsOnlyFlag = 1;
		esd->slConfig->useTimestampsFlag = 1;
		esd->slConfig->timestampResolution = 1000;
		gf_free(props);
		return esd;
	}
	if (item_type == GF_4CC('j','2','k','1')) {
		if (item_id > (1 << 16)) {
			GF_LOG(GF_LOG_WARNING, GF_LOG_CORE, ("Item ID greater than 16 bits, does not fit on ES ID\n"));
		}
		esd->ESID = (u16)item_id;
		esd->OCRESID = esd->ESID;
		esd->decoderConfig->streamType = GF_STREAM_VISUAL;
		esd->decoderConfig->objectTypeIndication = GF_CODECID_J2K;
		e = gf_isom_get_meta_image_props(mp4, GF_TRUE, 0, item_id, props, NULL);
		if (e == GF_OK && props->config) {
			GF_BitStream *bs = gf_bs_new(NULL, 0, GF_BITSTREAM_WRITE);
			gf_isom_box_write((GF_Box*)props->config, bs);
			gf_bs_get_content(bs, &esd->decoderConfig->decoderSpecificInfo->data, &esd->decoderConfig->decoderSpecificInfo->dataLength);
			gf_bs_del(bs);
		}
		esd->slConfig->hasRandomAccessUnitsOnlyFlag = 1;
		esd->slConfig->useTimestampsFlag = 1;
		esd->slConfig->timestampResolution = 1000;
		gf_free(props);
		return esd;
	}

	if ((item_type == GF_ISOM_SUBTYPE_UNCV) || (item_type == GF_ISOM_ITEM_TYPE_UNCI)) {
		if (item_id > (1 << 16)) {
			GF_LOG(GF_LOG_WARNING, GF_LOG_CORE, ("Item ID greater than 16 bits, does not fit on ES ID\n"));
		}
		esd->ESID = (u16)item_id;
		esd->OCRESID = esd->ESID;
		esd->decoderConfig->streamType = GF_STREAM_VISUAL;
		esd->decoderConfig->objectTypeIndication = GF_CODECID_RAW;
		GF_List *other_props = gf_list_new();
		e = gf_isom_get_meta_image_props(mp4, GF_TRUE, 0, item_id, props, other_props);
#ifndef GPAC_DISABLE_ISOM_WRITE
		if ((e == GF_OK) && gf_list_count(other_props)) {
			GF_BitStream *bs = gf_bs_new(NULL, 0, GF_BITSTREAM_WRITE);
			gf_isom_box_array_write(NULL, other_props, bs);
			gf_bs_get_content(bs, &esd->decoderConfig->decoderSpecificInfo->data, &esd->decoderConfig->decoderSpecificInfo->dataLength);
			gf_bs_del(bs);
		}
#endif // GPAC_DISABLE_ISOM_WRITE
		gf_list_del(other_props);
		esd->slConfig->hasRandomAccessUnitsOnlyFlag = 1;
		esd->slConfig->useTimestampsFlag = 1;
		esd->slConfig->timestampResolution = 1000;
		gf_free(props);
		return esd;
	}

	gf_odf_desc_del((GF_Descriptor*)esd);
	gf_free(props);
	return NULL;
}

#endif /*GPAC_DISABLE_ISOM*/

#ifndef GPAC_DISABLE_MEDIA_IMPORT
static s32 gf_get_DQId(GF_ISOFile *file, u32 track)
{
	GF_AVCConfig *svccfg;
	GF_ISOSample *samp;
	u32 di = 0, cur_extract_mode;
	char *buffer;
	GF_BitStream *bs;
	u32 max_size = 4096;
	u32 size, nalu_size_length;
	u8 nal_type;
	s32 DQId=0;

	samp = NULL;
	bs = NULL;
	cur_extract_mode = gf_isom_get_nalu_extract_mode(file, track);
	gf_isom_set_nalu_extract_mode(file, track, GF_ISOM_NALU_EXTRACT_INSPECT);
	buffer = (char*)gf_malloc(sizeof(char) * max_size);
	svccfg = gf_isom_svc_config_get(file, track, 1);
	if (!svccfg)
	{
		DQId = 0;
		goto exit;
	}
	samp = gf_isom_get_sample(file, track, 1, &di);
	if (!samp)
	{
		DQId = -1;
		goto exit;
	}
	bs = gf_bs_new(samp->data, samp->dataLength, GF_BITSTREAM_READ);
	nalu_size_length = 8 * svccfg->nal_unit_size;
	while (gf_bs_available(bs))
	{
		size = gf_bs_read_int(bs, nalu_size_length);
		if (size>max_size) {
			buffer = (char*)gf_realloc(buffer, sizeof(char)*size);
			max_size = size;
		}
		gf_bs_read_data(bs, buffer, size);
		nal_type = buffer[0] & 0x1F;
		if (nal_type == GF_AVC_NALU_SVC_SLICE)
		{
			DQId = buffer[2] & 0x7F;
			goto exit;
		}
	}
exit:
	if (svccfg) gf_odf_avc_cfg_del(svccfg);
	if (samp) gf_isom_sample_del(&samp);
	if (buffer) gf_free(buffer);
	if (bs) gf_bs_del(bs);
	gf_isom_set_nalu_extract_mode(file, track, cur_extract_mode);
	return DQId;
}

#ifndef GPAC_DISABLE_AV_PARSERS
static Bool gf_isom_has_svc_explicit(GF_ISOFile *file, u32 track)
{
	GF_AVCConfig *svccfg;
	GF_NALUFFParam *slc;
	u32 i;
	u8 type;
	Bool ret = 0;

	svccfg = gf_isom_svc_config_get(file, track, 1);
	if (!svccfg)
		return 0;

	for (i = 0; i < gf_list_count(svccfg->sequenceParameterSets); i++)
	{
		slc = (GF_NALUFFParam *)gf_list_get(svccfg->sequenceParameterSets, i);
		type = slc->data[0] & 0x1F;
		if (type == GF_AVC_NALU_SEQ_PARAM)
		{
			ret = 1;
			break;
		}
	}

	gf_odf_avc_cfg_del(svccfg);
	return ret;
}


static u32 gf_isom_get_track_id_max(GF_ISOFile *file)
{
	u32 num_track, i, trackID;
	u32 max_id = 0;

	num_track = gf_isom_get_track_count(file);
	for (i = 1; i <= num_track; i++)
	{
		trackID = gf_isom_get_track_id(file, i);
		if (max_id < trackID)
			max_id = trackID;
	}

	return max_id;
}
#endif

/* Split SVC layers */
GF_EXPORT
GF_Err gf_media_split_svc(GF_ISOFile *file, u32 track, Bool splitAll)
{
#ifndef GPAC_DISABLE_AV_PARSERS
	GF_AVCConfig *svccfg, *cfg;
	u32 num_svc_track, num_sample, svc_track, dst_track, ref_trackID, ref_trackNum, max_id, di, width, height, size, nalu_size_length, i, j, t, max_size, num_pps, num_sps, num_subseq, NALUnitHeader, data_offset, data_length, count, timescale, cur_extract_mode;
	GF_Err e;
	GF_NALUFFParam *slc, *sl;
	AVCState *avc_state=NULL;
	s32 sps_id, pps_id;
	GF_ISOSample *samp, *dst_samp;
	GF_BitStream *bs, *dst_bs;
	GF_BitStream ** sample_bs;
	u8 nal_type, track_ref_index;
	char *buffer;
	s32 *sps_track, *sps, *pps;
	u64 offset;
	Bool is_splitted;
	Bool *first_sample_track, *is_subseq_pps;
	u64 *first_DTS_track;
	s8 sample_offset;

	max_size = 4096;
	e = GF_OK;
	samp = dst_samp = NULL;
	bs = NULL;
	sample_bs = NULL;
	sps_track = sps = pps = NULL;
	first_DTS_track = NULL;
	first_sample_track = is_subseq_pps = NULL;
	buffer = NULL;
	cfg = NULL;
	num_svc_track=0;
	cur_extract_mode = gf_isom_get_nalu_extract_mode(file, track);
	gf_isom_set_nalu_extract_mode(file, track, GF_ISOM_NALU_EXTRACT_INSPECT);
	svccfg = gf_isom_svc_config_get(file, track, 1);
	if (!svccfg)
	{
		e = GF_OK;
		goto exit;
	}
	num_sps = gf_list_count(svccfg->sequenceParameterSets);
	if (!num_sps)
	{
		e = GF_OK;
		goto exit;
	}
	num_pps = gf_list_count(svccfg->pictureParameterSets);
	if ((gf_isom_get_avc_svc_type(file, track, 1) == GF_ISOM_AVCTYPE_SVC_ONLY) && !gf_isom_has_svc_explicit(file, track))
		is_splitted = 1;
	else
		is_splitted = 0;
	num_subseq = gf_isom_has_svc_explicit(file, track) ? num_sps - 1 : num_sps;

	if (is_splitted)
	{
		/*this track has only one SVC ...*/
		if (num_sps == 1)
		{
			/*use 'all' mode -> stop*/
			if (splitAll)
				goto exit;
			/*use 'base' mode -> merge SVC tracks*/
			else
			{
				e = gf_media_merge_svc(file, track, 0);
				goto exit;
			}
		}
		/*this file has been in 'splitbase' mode*/
		else if (!splitAll)
			goto exit;

	}

	timescale = gf_isom_get_media_timescale(file, track);
	num_svc_track = splitAll ? num_subseq : 1;
	max_id = gf_isom_get_track_id_max(file);
	di = 0;

	GF_SAFEALLOC(avc_state, AVCState);
	if (!avc_state) {
		e = GF_OUT_OF_MEM;
		goto exit;
	}

	avc_state->sps_active_idx = -1;
	nalu_size_length = 8 * svccfg->nal_unit_size;
	/*read all sps, but we need only the subset sequence parameter sets*/
	sps =  (s32 *) gf_malloc(num_subseq * sizeof(s32));
	sps_track = (s32 *) gf_malloc(num_subseq * sizeof(s32));
	count = 0;
	for (i = 0; i < num_sps; i++)
	{
		slc = (GF_NALUFFParam *)gf_list_get(svccfg->sequenceParameterSets, i);
		nal_type = slc->data[0] & 0x1F;
		sps_id = gf_avc_read_sps(slc->data, slc->size, avc_state, 0, NULL);
		if (sps_id < 0) {
			e = GF_NON_COMPLIANT_BITSTREAM;
			goto exit;
		}
		if (nal_type == GF_AVC_NALU_SVC_SUBSEQ_PARAM)
		{
			sps[count] = sps_id;
			sps_track[count] = i;
			count++;
		}
	}
	/*for testing*/
	gf_assert(count == num_subseq);
	/*read all pps*/
	pps =  (s32 *) gf_malloc(num_pps * sizeof(s32));
	for (j = 0; j < num_pps; j++)
	{
		slc = (GF_NALUFFParam *)gf_list_get(svccfg->pictureParameterSets, j);
		pps_id = gf_avc_read_pps(slc->data, slc->size, avc_state);
		if (pps_id < 0) {
			e = GF_NON_COMPLIANT_BITSTREAM;
			goto exit;
		}
		pps[j] = pps_id;
	}
	if (!is_splitted)
		ref_trackID = gf_isom_get_track_id(file, track);
	else
	{
		gf_isom_get_reference(file, track, GF_ISOM_REF_BASE, 1, &ref_trackNum);
		ref_trackID = gf_isom_get_track_id(file, ref_trackNum);
	}

	buffer = (char*)gf_malloc(sizeof(char) * max_size);
	/*read first sample for determinating the order of SVC tracks*/
	count = 0;
	samp = gf_isom_get_sample(file, track, 1, &di);
	if (!samp)
	{
		e = gf_isom_last_error(file);
		goto exit;
	}
	bs = gf_bs_new(samp->data, samp->dataLength, GF_BITSTREAM_READ);
	offset = 0;
	is_subseq_pps = (Bool *) gf_malloc(num_pps*sizeof(Bool));
	for (i = 0; i < num_pps; i++)
		is_subseq_pps[i] = 0;
	while (gf_bs_available(bs))
	{
		gf_bs_enable_emulation_byte_removal(bs, GF_FALSE);
		size = gf_bs_read_int(bs, nalu_size_length);
		if (size>max_size) {
			buffer = (char*)gf_realloc(buffer, sizeof(char)*size);
			max_size = size;
		}

		gf_avc_parse_nalu(bs, avc_state);
		nal_type = avc_state->last_nal_type_parsed;

		e = gf_bs_seek(bs, offset+nalu_size_length/8);
		if (e)
			goto exit;
		gf_bs_read_data(bs, buffer, size);
		offset += size + nalu_size_length/8;
		if (nal_type == GF_AVC_NALU_SVC_SLICE)
		{
			for (i = 0; i < num_pps; i++)
			{
				if (avc_state->s_info.pps->id == pps[i])
				{
					is_subseq_pps[i] = 1;
					break;
				}
			}
			if ((count > 0) && (avc_state->s_info.pps->sps_id == sps[count-1]))
				continue;
			/*verify the order of SPS, reorder if necessary*/
			if (avc_state->s_info.pps->sps_id != sps[count])
			{
				for (i = count+1; i < num_subseq; i++)
				{
					/*swap two SPS*/
					if (avc_state->s_info.pps->sps_id == sps[i])
					{
						sps[i] = sps[count];
						sps[count] = avc_state->s_info.pps->sps_id;
						sps_track[count] = i;
						break;
					}
				}
			}
			count++;
		}
	}
	gf_bs_del(bs);
	bs = NULL;

	gf_isom_sample_del(&samp);
	samp = NULL;

	for (t = 0; t < num_svc_track; t++)
	{
		svc_track = gf_isom_new_track(file, t+1+max_id, GF_ISOM_MEDIA_VISUAL, timescale);
		if (!svc_track)
		{
			e = gf_isom_last_error(file);
			goto exit;
		}
		gf_isom_set_track_enabled(file, svc_track, GF_TRUE);
		gf_isom_set_track_reference(file, svc_track, GF_ISOM_REF_BASE, ref_trackID);
		//copy over edit list
		for (i=0; i<gf_isom_get_edits_count(file, track); i++) {
			GF_ISOEditType emode;
			u64 etime, edur, mtime;
			gf_isom_get_edit(file, track, i+1, &etime, &edur, &mtime, &emode);
			gf_isom_set_edit(file, svc_track, etime, edur, mtime, emode);
		}
		cfg = gf_odf_avc_cfg_new();
		cfg->complete_representation = 1; //SVC
		/*this layer depends on the base layer and the lower layers*/
		gf_isom_set_track_reference(file, svc_track, GF_ISOM_REF_SCAL, ref_trackID);
		for (i = 0; i < t; i++)
			gf_isom_set_track_reference(file, svc_track, GF_ISOM_REF_SCAL, i+1+max_id);

		e = gf_isom_svc_config_new(file, svc_track, cfg, NULL, NULL, &di);
		if (e)
			goto exit;
		if (splitAll)
		{
			sps_id = sps[t];
			width = avc_state->sps[sps_id].width;
			height = avc_state->sps[sps_id].height;
			gf_isom_set_visual_info(file, svc_track, di, width, height);
			cfg->configurationVersion = 1;
			cfg->chroma_bit_depth = 8 + avc_state->sps[sps_id].chroma_bit_depth_m8;
			cfg->chroma_format = avc_state->sps[sps_id].chroma_format;
			cfg->luma_bit_depth = 8 + avc_state->sps[sps_id].luma_bit_depth_m8;
			cfg->profile_compatibility = avc_state->sps[sps_id].prof_compat;
			cfg->AVCLevelIndication = avc_state->sps[sps_id].level_idc;
			cfg->AVCProfileIndication = avc_state->sps[sps_id].profile_idc;
			cfg->nal_unit_size = svccfg->nal_unit_size;
			slc = (GF_NALUFFParam *)gf_list_get(svccfg->sequenceParameterSets, sps_track[t]);
			sl = (GF_NALUFFParam*)gf_malloc(sizeof(GF_NALUFFParam));
			sl->id = slc->id;
			sl->size = slc->size;
			sl->data = (char*)gf_malloc(sizeof(char)*sl->size);
			memcpy(sl->data, slc->data, sizeof(char)*sl->size);
			gf_list_add(cfg->sequenceParameterSets, sl);
			for (j = 0; j < num_pps; j++)
			{
				pps_id = pps[j];
				if (is_subseq_pps[j] && (avc_state->pps[pps_id].sps_id == sps_id))
				{
					slc = (GF_NALUFFParam *)gf_list_get(svccfg->pictureParameterSets, j);
					sl = (GF_NALUFFParam*)gf_malloc(sizeof(GF_NALUFFParam));
					sl->id = slc->id;
					sl->size = slc->size;
					sl->data = (char*)gf_malloc(sizeof(char)*sl->size);
					memcpy(sl->data, slc->data, sizeof(char)*sl->size);
					gf_list_add(cfg->pictureParameterSets, sl);
				}
			}
		}
		else
		{
			for (i = 0; i < num_subseq; i++)
			{
				sps_id = sps[i];
				width = avc_state->sps[sps_id].width;
				height = avc_state->sps[sps_id].height;
				gf_isom_set_visual_info(file, svc_track, di, width, height);
				cfg->configurationVersion = 1;
				cfg->chroma_bit_depth = 8 + avc_state->sps[sps_id].chroma_bit_depth_m8;
				cfg->chroma_format = avc_state->sps[sps_id].chroma_format;
				cfg->luma_bit_depth = 8 + avc_state->sps[sps_id].luma_bit_depth_m8;
				cfg->profile_compatibility = avc_state->sps[sps_id].prof_compat;
				cfg->AVCLevelIndication = avc_state->sps[sps_id].level_idc;
				cfg->AVCProfileIndication = avc_state->sps[sps_id].profile_idc;
				cfg->nal_unit_size = svccfg->nal_unit_size;
				slc = (GF_NALUFFParam *)gf_list_get(svccfg->sequenceParameterSets, sps_track[i]);
				sl = (GF_NALUFFParam*)gf_malloc(sizeof(GF_NALUFFParam));
				sl->id = slc->id;
				sl->size = slc->size;
				sl->data = (char*)gf_malloc(sizeof(char)*sl->size);
				memcpy(sl->data, slc->data, sizeof(char)*sl->size);
				gf_list_add(cfg->sequenceParameterSets, sl);
				for (j = 0; j < num_pps; j++)
				{
					pps_id = pps[j];
					if (avc_state->pps[pps_id].sps_id == sps_id)
					{
						slc = (GF_NALUFFParam *)gf_list_get(svccfg->pictureParameterSets, j);
						sl = (GF_NALUFFParam*)gf_malloc(sizeof(GF_NALUFFParam));
						sl->id = slc->id;
						sl->size = slc->size;
						sl->data = (char*)gf_malloc(sizeof(char)*sl->size);
						memcpy(sl->data, slc->data, sizeof(char)*sl->size);
						gf_list_add(cfg->pictureParameterSets, sl);
					}
				}
			}
		}
		e = gf_isom_svc_config_update(file, svc_track, 1, cfg, 0);
		if (e)
			goto exit;
		gf_odf_avc_cfg_del(cfg);
		cfg = NULL;
	}

	num_sample = gf_isom_get_sample_count(file, track);
	first_sample_track = (Bool *) gf_malloc((num_svc_track+1) * sizeof(Bool));
	for (t = 0; t <= num_svc_track; t++)
		first_sample_track[t] = 1;
	first_DTS_track = (u64 *) gf_malloc((num_svc_track+1) * sizeof(u64));
	for (t = 0; t <= num_svc_track; t++)
		first_DTS_track[t] = 0;
	for (i = 1; i <= num_sample; i++)
	{
		/*reset*/
		memset(buffer, 0, max_size);

		samp = gf_isom_get_sample(file, track, i, &di);
		if (!samp)
		{
			e = GF_IO_ERR;
			goto exit;
		}

		/* Create (num_svc_track) SVC bitstreams + 1 AVC bitstream*/
		sample_bs = (GF_BitStream **) gf_malloc(sizeof(GF_BitStream *) * (num_svc_track+1));
		for (j = 0; j <= num_svc_track; j++)
			sample_bs[j] = (GF_BitStream *) gf_bs_new(NULL, 0, GF_BITSTREAM_WRITE);

		/*write extractor*/
		for (t = 0; t < num_svc_track; t++)
		{
			//reference to base layer
			gf_bs_write_int(sample_bs[t+1], 14, nalu_size_length); // extractor 's size = 14
			NALUnitHeader = 0; //reset
			NALUnitHeader |= 0x1F000000; // NALU type = 31
			gf_bs_write_u32(sample_bs[t+1], NALUnitHeader);
			track_ref_index = (u8) gf_isom_has_track_reference(file, t+1+max_id, GF_ISOM_REF_SCAL, ref_trackID);
			if (!track_ref_index)
			{
				e = GF_CORRUPTED_DATA;
				goto exit;
			}
			gf_bs_write_u8(sample_bs[t+1], track_ref_index);
			sample_offset = 0;
			gf_bs_write_u8(sample_bs[t+1], sample_offset);
			data_offset = 0;
			gf_bs_write_u32(sample_bs[t+1], data_offset);
			data_length = 0;
			gf_bs_write_u32(sample_bs[t+1], data_length);
			//reference to previous layer(s)
			for (j = 0; j < t; j++)
			{
				gf_bs_write_int(sample_bs[t+1], 14, nalu_size_length);
				NALUnitHeader = 0;
				NALUnitHeader |= 0x1F000000;
				gf_bs_write_u32(sample_bs[t+1], NALUnitHeader);
				track_ref_index = (u8) gf_isom_has_track_reference(file, t+1+max_id, GF_ISOM_REF_SCAL, j+1+max_id);
				if (!track_ref_index)
				{
					e = GF_CORRUPTED_DATA;
					goto exit;
				}
				gf_bs_write_u8(sample_bs[t+1], track_ref_index);
				sample_offset = 0;
				gf_bs_write_u8(sample_bs[t+1], sample_offset);
				data_offset = (j+1) * (nalu_size_length/8 + 14); // (nalu_size_length/8) bytes of NALU length field + 14 bytes of extractor per layer
				gf_bs_write_u32(sample_bs[t+1], data_offset);
				data_length = 0;
				gf_bs_write_u32(sample_bs[t+1], data_length);
			}
		}

		bs = gf_bs_new(samp->data, samp->dataLength, GF_BITSTREAM_READ);
		offset = 0;
		while (gf_bs_available(bs))
		{
			gf_bs_enable_emulation_byte_removal(bs, GF_FALSE);
			size = gf_bs_read_int(bs, nalu_size_length);
			if (size>max_size) {
				buffer = (char*)gf_realloc(buffer, sizeof(char)*size);
				max_size = size;
			}

			gf_avc_parse_nalu(bs, avc_state);
			nal_type = avc_state->last_nal_type_parsed;
			e = gf_bs_seek(bs, offset+nalu_size_length/8);
			if (e)
				goto exit;
			gf_bs_read_data(bs, buffer, size);
			offset += size + nalu_size_length/8;

			switch (nal_type) {
			case GF_AVC_NALU_PIC_PARAM:
				pps_id = avc_state->last_ps_idx;
				j = 0;
				dst_track = 0;
				while (j < num_pps)
				{
					if (pps_id == pps[j])
						break;
					j++;
				}
				if ((j < num_pps) && (is_subseq_pps[j]))
				{
					if (splitAll)
					{
						for (t = 0; t < num_svc_track; t++)
						{
							if (sps[t] == avc_state->pps[pps_id].sps_id)
							{
								dst_track = t + 1;
								break;
							}
						}
					}
					else
						dst_track = 1;
				}
				dst_bs = sample_bs[dst_track];
				break;
			case GF_AVC_NALU_SVC_SUBSEQ_PARAM:
				sps_id = avc_state->last_ps_idx;
				dst_track = 0;
				if (splitAll)
				{
					for (t = 0; t < num_svc_track; t++)
					{
						if (sps[t] == sps_id)
						{
							dst_track = t + 1;
							break;
						}
					}
				}
				else
					dst_track = 1;
				dst_bs = sample_bs[dst_track];
				break;
			case GF_AVC_NALU_SVC_SLICE:
				dst_track = 0;
				if (splitAll)
				{
					for (t = 0; t < num_svc_track; t++)
					{
						if (sps[t] == (avc_state->s_info.pps)->sps_id)
						{
							dst_track = t + 1;
							break;
						}
					}
				}
				else
					dst_track = 1;
				dst_bs = sample_bs[dst_track];
				break;
			default:
				dst_bs = sample_bs[0];
			}

			gf_bs_write_int(dst_bs, size, nalu_size_length);
			gf_bs_write_data(dst_bs, buffer, size);
		}

		for (j = 0; j <= num_svc_track; j++)
		{
			if (gf_bs_get_position(sample_bs[j]))
			{
				if (first_sample_track[j])
				{
					first_sample_track[j] = 0;
					first_DTS_track[j] = samp->DTS;
				}
				dst_samp = gf_isom_sample_new();
				dst_samp->CTS_Offset = samp->CTS_Offset;
				dst_samp->DTS = samp->DTS - first_DTS_track[j];
				dst_samp->IsRAP = samp->IsRAP;
				gf_bs_get_content(sample_bs[j], &dst_samp->data, &dst_samp->dataLength);
				if (j) //SVC
					e = gf_isom_add_sample(file, track+j, di, dst_samp);
				else
					e = gf_isom_update_sample(file, track, i, dst_samp, 1);
				if (e)
					goto exit;
				gf_isom_sample_del(&dst_samp);
				dst_samp = NULL;
			}
			gf_bs_del(sample_bs[j]);
			sample_bs[j] = NULL;
		}
		gf_free(sample_bs);
		sample_bs = NULL;
		gf_bs_del(bs);
		bs = NULL;
		gf_isom_sample_del(&samp);
		samp = NULL;
	}

	/*add Editlist entry if DTS of the first sample is not zero*/
	for (t = 0; t <= num_svc_track; t++)
	{
		if (first_DTS_track[t])
		{
			u32 media_ts, moov_ts, ts_offset;
			u64 dur;
			media_ts = gf_isom_get_media_timescale(file, t);
			moov_ts = gf_isom_get_timescale(file);
			ts_offset = (u32)(first_DTS_track[t]) * moov_ts / media_ts;
			dur = gf_isom_get_media_duration(file, t) * moov_ts / media_ts;
			gf_isom_set_edit(file, t, 0, ts_offset, 0, GF_ISOM_EDIT_EMPTY);
			gf_isom_set_edit(file, t, ts_offset, dur, 0, GF_ISOM_EDIT_NORMAL);
		}
	}

	/*if this is a merged file*/
	if (!is_splitted)
	{
		/*a normal stream: delete SVC config*/
		if (!gf_isom_has_svc_explicit(file, track))
		{
			gf_isom_svc_config_del(file, track, 1);
		}
		else
		{
			s32 shift=0;

			for (i = 0; i < gf_list_count(svccfg->sequenceParameterSets); i++)
			{
				slc = (GF_NALUFFParam *)gf_list_get(svccfg->sequenceParameterSets, i);
				sps_id = gf_avc_read_sps(slc->data, slc->size, avc_state, 0, NULL);
				if (sps_id < 0) {
					e = GF_NON_COMPLIANT_BITSTREAM;
					goto exit;
				}
				nal_type = slc->data[0] & 0x1F;
				if (nal_type == GF_AVC_NALU_SVC_SUBSEQ_PARAM)
				{
					gf_list_rem(svccfg->sequenceParameterSets, i);
					gf_free(slc->data);
					gf_free(slc);
					i--;
				}
			}

			for (j = 0; j < gf_list_count(svccfg->pictureParameterSets); j++)
			{
				slc = (GF_NALUFFParam *)gf_list_get(svccfg->pictureParameterSets, j);
				pps_id = gf_avc_read_pps(slc->data, slc->size, avc_state);
				if (pps_id < 0) {
					e = GF_NON_COMPLIANT_BITSTREAM;
					goto exit;
				}
				if (is_subseq_pps[j+shift])
				{
					gf_list_rem(svccfg->pictureParameterSets, j);
					gf_free(slc->data);
					gf_free(slc);
					j--;
				}
			}
			e = gf_isom_svc_config_update(file, track, 1, svccfg, 0);
			if (e)
				goto exit;
		}
	}
	/*if this is as splitted file: delete this track*/
	else
	{
		gf_isom_remove_track(file, track);
	}

exit:
	if (svccfg) gf_odf_avc_cfg_del(svccfg);
	if (cfg) gf_odf_avc_cfg_del(cfg);
	if (samp) gf_isom_sample_del(&samp);
	if (dst_samp) gf_isom_sample_del(&dst_samp);
	if (bs) gf_bs_del(bs);
	if (sample_bs)
	{
		for (i = 0; i <= num_svc_track; i++)
			gf_bs_del(sample_bs[i]);
		gf_free(sample_bs);
	}
	if (sps_track) gf_free(sps_track);
	if (sps) gf_free(sps);
	if (pps) gf_free(pps);
	if (first_sample_track) gf_free(first_sample_track);
	if (first_DTS_track) gf_free(first_DTS_track);
	if (buffer) gf_free(buffer);
	if (is_subseq_pps) gf_free(is_subseq_pps);
	if (avc_state) gf_free(avc_state);
	gf_isom_set_nalu_extract_mode(file, track, cur_extract_mode);
	return e;
#else
	return GF_NOT_SUPPORTED;
#endif

}

/* Merge SVC layers*/
GF_EXPORT
GF_Err gf_media_merge_svc(GF_ISOFile *file, u32 track, Bool mergeAll)
{
	GF_AVCConfig *svccfg, *cfg;
	u32 merge_track,  num_track, num_sample, size, i, t, di, max_size, nalu_size_length, ref_trackNum, ref_trackID, count, width, height, nb_EditList, media_ts, moov_ts;
	GF_ISOSample *avc_samp, *samp, *dst_samp;
	GF_BitStream *bs, *dst_bs;
	GF_Err e;
	char *buffer;
	s32 *DQId;
	u32 *list_track_sorted, *cur_sample, *max_sample;
	u64 *DTS_offset;
	u64 EditTime, SegmentDuration, MediaTime;
	GF_ISOEditType EditMode;
	u8 nal_type;
	Bool first_sample;
	u64 first_DTS, offset, dur;
	GF_NALUFFParam *slc, *sl;

	e = GF_OK;
	di = 1;
	max_size = 4096;
	width = height = 0;
	avc_samp = samp = dst_samp = NULL;
	svccfg = cfg = NULL;
	buffer = NULL;
	bs = dst_bs = NULL;
	DQId = NULL;
	list_track_sorted = cur_sample = max_sample = NULL;
	DTS_offset = NULL;

	if (gf_isom_get_avc_svc_type(file, track, 1) == GF_ISOM_AVCTYPE_AVC_SVC)
		goto exit;

	num_track = gf_isom_get_track_count(file);
	if (num_track == 1)
		goto exit;
	gf_isom_get_reference(file, track, GF_ISOM_REF_BASE, 1, &ref_trackNum);
	ref_trackID = gf_isom_get_track_id(file, ref_trackNum);
	if (!ref_trackID)
	{
		e = GF_ISOM_INVALID_MEDIA;
		goto exit;
	}

	list_track_sorted = (u32 *) gf_malloc(num_track * sizeof(u32));
	memset(list_track_sorted, 0, num_track * sizeof(u32));
	DQId = (s32 *) gf_malloc(num_track * sizeof(s32));
	memset(DQId, 0, num_track * sizeof(s32));
	count = 0;
	for (t = 1; t <= num_track; t++) {
		u32 pos = 0;
		s32 track_DQId = gf_get_DQId(file, t);
		if (track_DQId < 0) {
			e = GF_ISOM_INVALID_MEDIA;
			goto exit;
		}

		if (!gf_isom_has_track_reference(file, t, GF_ISOM_REF_BASE, ref_trackID))
		{
			if (t != ref_trackNum) continue;
			else if (!mergeAll) continue;
		}

		while ((pos < count ) && (DQId[pos] <= track_DQId))
			pos++;
		for (i = count; i > pos; i--)
		{
			list_track_sorted[i] = list_track_sorted[i-1];
			DQId[i] = DQId[i-1];
		}
		list_track_sorted[pos] = t;
		DQId[pos] = track_DQId;
		count++;
	}

	merge_track = list_track_sorted[0];
	gf_isom_set_track_enabled(file, merge_track, GF_TRUE);
	/*rewrite svccfg*/
	svccfg = gf_odf_avc_cfg_new();
	svccfg->complete_representation = 1;
	/*rewrite visual info*/
	if (!mergeAll)
	{
		for (t = 0; t < count; t++)
			gf_isom_get_visual_info(file, list_track_sorted[t], 1, &width, &height);
		gf_isom_set_visual_info(file, merge_track, 1, width, height);
	}

	for (t = 0; t < count; t++)
	{
		cfg = gf_isom_svc_config_get(file, list_track_sorted[t], 1);
		if (!cfg)
			continue;
		svccfg->configurationVersion = 1;
		svccfg->chroma_bit_depth = cfg->chroma_bit_depth;
		svccfg->chroma_format = cfg->chroma_format;
		svccfg->luma_bit_depth = cfg->luma_bit_depth;
		svccfg->profile_compatibility = cfg->profile_compatibility;
		svccfg->AVCLevelIndication = cfg->AVCLevelIndication;
		svccfg->AVCProfileIndication = cfg->AVCProfileIndication;
		svccfg->nal_unit_size = cfg->nal_unit_size;
		for (i = 0; i < gf_list_count(cfg->sequenceParameterSets); i++)
		{
			slc = (GF_NALUFFParam *)gf_list_get(cfg->sequenceParameterSets, i);
			sl = (GF_NALUFFParam*)gf_malloc(sizeof(GF_NALUFFParam));
			sl->id = slc->id;
			sl->size = slc->size;
			sl->data = (char*)gf_malloc(sizeof(char)*sl->size);
			memcpy(sl->data, slc->data, sizeof(char)*sl->size);
			gf_list_add(svccfg->sequenceParameterSets, sl);
		}
		for (i = 0; i < gf_list_count(cfg->pictureParameterSets); i++)
		{
			slc = (GF_NALUFFParam *)gf_list_get(cfg->pictureParameterSets, i);
			sl = (GF_NALUFFParam*)gf_malloc(sizeof(GF_NALUFFParam));
			sl->id = slc->id;
			sl->size = slc->size;
			sl->data = (char*)gf_malloc(sizeof(char)*sl->size);
			memcpy(sl->data, slc->data, sizeof(char)*sl->size);
			gf_list_add(svccfg->pictureParameterSets, sl);
		}
		if (mergeAll)
		{
			gf_isom_svc_config_update(file, merge_track, 1, svccfg, 1);
		}
		else
			gf_isom_svc_config_update(file, merge_track, 1, svccfg, 0);
		gf_odf_avc_cfg_del(cfg);
		cfg = NULL;
	}

	cur_sample = (u32 *) gf_malloc(count * sizeof(u32));
	max_sample = (u32 *) gf_malloc(count * sizeof(u32));
	for (t = 0; t < count; t++)
	{
		cur_sample[t] = 1;
		max_sample[t] = gf_isom_get_sample_count(file, list_track_sorted[t]);
	}

	DTS_offset = (u64 *) gf_malloc(count * sizeof(u64));
	for (t = 0; t < count; t++) {
		DTS_offset[t] = 0;
		nb_EditList = gf_isom_get_edits_count(file, list_track_sorted[t]);
		if (nb_EditList) {
			media_ts = gf_isom_get_media_timescale(file, list_track_sorted[t]);
			moov_ts = gf_isom_get_timescale(file);
			for (i = 1; i <= nb_EditList; i++) {
				e = gf_isom_get_edit(file, list_track_sorted[t], i, &EditTime, &SegmentDuration, &MediaTime, &EditMode);
				if (e) goto exit;

				if (!EditMode) {
					DTS_offset[t] = SegmentDuration * media_ts / moov_ts;
				}
			}
		}
	}

	num_sample = gf_isom_get_sample_count(file, ref_trackNum);
	nalu_size_length = 8 * svccfg->nal_unit_size;
	first_sample = 1;
	first_DTS = 0;
	buffer = (char*)gf_malloc(sizeof(char) * max_size);
	for (t = 1; t <= num_track; t++)
		gf_isom_set_nalu_extract_mode(file, t, GF_ISOM_NALU_EXTRACT_INSPECT);
	for (i = 1; i <= num_sample; i++)
	{
		dst_bs = gf_bs_new(NULL, 0, GF_BITSTREAM_WRITE);
		/*add extractor if nessassary*/
		if (!mergeAll)
		{
			u32 NALUnitHeader = 0;
			u8 track_ref_index;
			s8 sample_offset;
			u32 data_offset;
			u32 data_length;

			gf_bs_write_int(dst_bs, 14, nalu_size_length); // extractor 's size = 14
			NALUnitHeader |= 0x1F000000; // NALU type = 31
			gf_bs_write_u32(dst_bs, NALUnitHeader);
			track_ref_index = (u8) gf_isom_has_track_reference(file, merge_track, GF_ISOM_REF_SCAL, ref_trackID);
			if (!track_ref_index)
			{
				e = GF_CORRUPTED_DATA;
				goto exit;
			}
			gf_bs_write_u8(dst_bs, track_ref_index);
			sample_offset = 0;
			gf_bs_write_u8(dst_bs, sample_offset);
			data_offset = 0;
			gf_bs_write_u32(dst_bs, data_offset);
			data_length = 0;
			gf_bs_write_u32(dst_bs, data_length);
		}

		avc_samp = gf_isom_get_sample(file, ref_trackNum, i, &di);
		if (!avc_samp) {
			e = gf_isom_last_error(file);
			goto exit;
		}

		for (t = 0; t < count; t++)
		{
			if (cur_sample[t] > max_sample[t])
				continue;
			samp = gf_isom_get_sample(file, list_track_sorted[t], cur_sample[t], &di);
			if (!samp) {
				e = gf_isom_last_error(file);
				goto exit;
			}

			if ((samp->DTS + DTS_offset[t]) != avc_samp->DTS) {
				gf_isom_sample_del(&samp);
				samp = NULL;
				continue;
			}
			bs = gf_bs_new(samp->data, samp->dataLength, GF_BITSTREAM_READ);
			/*reset*/
			memset(buffer, 0, sizeof(char) * max_size);
			while (gf_bs_available(bs))
			{
				size = gf_bs_read_int(bs, nalu_size_length);
				if (size>max_size) {
					buffer = (char*)gf_realloc(buffer, sizeof(char)*size);
					max_size = size;
				}
				gf_bs_read_data(bs, buffer, size);
				nal_type = buffer[0] & 0x1F;
				/*skip extractor*/
				if (nal_type == GF_AVC_NALU_FF_EXTRACTOR)
					continue;
				/*copy to new bitstream*/
				gf_bs_write_int(dst_bs, size, nalu_size_length);
				gf_bs_write_data(dst_bs, buffer, size);
			}
			gf_bs_del(bs);
			bs = NULL;
			gf_isom_sample_del(&samp);
			samp = NULL;
			cur_sample[t]++;
		}

		/*add sapmle to track*/
		if (gf_bs_get_position(dst_bs))
		{
			if (first_sample)
			{
				first_DTS = avc_samp->DTS;
				first_sample = 0;
			}
			dst_samp = gf_isom_sample_new();
			dst_samp->CTS_Offset = avc_samp->CTS_Offset;
			dst_samp->DTS = avc_samp->DTS - first_DTS;
			dst_samp->IsRAP = avc_samp->IsRAP;
			gf_bs_get_content(dst_bs, &dst_samp->data, &dst_samp->dataLength);
			e = gf_isom_update_sample(file, merge_track, i, dst_samp, 1);
			if (e)
				goto exit;
		}
		gf_isom_sample_del(&avc_samp);
		avc_samp = NULL;
		gf_bs_del(dst_bs);
		dst_bs = NULL;
		gf_isom_sample_del(&dst_samp);
		dst_samp = NULL;
	}

	/*Add EditList if nessessary*/
	if (!first_DTS)
	{
		media_ts = gf_isom_get_media_timescale(file, merge_track);
		moov_ts = gf_isom_get_timescale(file);
		offset = (u32)(first_DTS) * moov_ts / media_ts;
		dur = gf_isom_get_media_duration(file, merge_track) * moov_ts / media_ts;
		gf_isom_set_edit(file, merge_track, 0, offset, 0, GF_ISOM_EDIT_EMPTY);
		gf_isom_set_edit(file, merge_track, offset, dur, 0, GF_ISOM_EDIT_NORMAL);
	}

	/*Delete SVC track(s) that references to ref_track*/
	for (t = 1; t <= num_track; t++)
	{
		if (gf_isom_has_track_reference(file, t, GF_ISOM_REF_BASE, ref_trackID) && (t != merge_track))
		{
			gf_isom_remove_track(file, t);
			num_track--; //we removed one track from file
			t--;
		}
	}

exit:
	if (avc_samp) gf_isom_sample_del(&avc_samp);
	if (samp) gf_isom_sample_del(&samp);
	if (dst_samp) gf_isom_sample_del(&dst_samp);
	if (svccfg) gf_odf_avc_cfg_del(svccfg);
	if (cfg) gf_odf_avc_cfg_del(cfg);
	if (bs) gf_bs_del(bs);
	if (dst_bs) gf_bs_del(dst_bs);
	if (buffer) gf_free(buffer);
	if (DQId) gf_free(DQId);
	if (list_track_sorted) gf_free(list_track_sorted);
	if (cur_sample) gf_free(cur_sample);
	if (max_sample) gf_free(max_sample);
	if (DTS_offset) gf_free(DTS_offset);
	for (t = 1; t <= gf_isom_get_track_count(file); t++)
		gf_isom_set_nalu_extract_mode(file, t, GF_ISOM_NALU_EXTRACT_DEFAULT);
	return e;
}

#ifndef GPAC_DISABLE_AV_PARSERS

/* Split LHVC layers */
static GF_NALUFFParamArray *alloc_hevc_param_array(GF_HEVCConfig *hevc_cfg, u8 type)
{
	GF_NALUFFParamArray *ar;
	u32 i, count = hevc_cfg->param_array ? gf_list_count(hevc_cfg->param_array) : 0;
	for (i=0; i<count; i++) {
		ar = gf_list_get(hevc_cfg->param_array, i);
		if (ar->type==type) return ar;
	}
	GF_SAFEALLOC(ar, GF_NALUFFParamArray);
	if (!ar) return NULL;
	ar->nalus = gf_list_new();
	ar->type = type;
	if (ar->type == GF_HEVC_NALU_VID_PARAM)
		gf_list_insert(hevc_cfg->param_array, ar, 0);
	else
		gf_list_add(hevc_cfg->param_array, ar);
	return ar;
}

typedef struct{
	u8 layer_id_plus_one;
	u8 min_temporal_id;
	u8 max_temporal_id;
} LInfo;

typedef struct
{
	u32 track_num;
	u32 layer_id;
	GF_HEVCConfig *lhvccfg;
	GF_BitStream *bs;
	u32 data_offset, data_size;
	u32 temporal_id_sample, max_temporal_id_sample;
	LInfo layers[64];
	u32 width, height;
	Bool has_samples;
	Bool non_tsa_vcl;
} LHVCTrackInfo;


GF_EXPORT
GF_Err gf_media_filter_hevc(GF_ISOFile *file, u32 track, u8 max_temporal_id_plus_one, u8 max_layer_id_plus_one)
{
	GF_HEVCConfig *hevccfg, *lhvccfg;
	u32 i, count, cur_extract_mode;
	char *nal_data=NULL;
	u32 nal_alloc_size, nalu_size;
	GF_Err e = GF_OK;

	if (!max_temporal_id_plus_one && !max_layer_id_plus_one)
		return GF_OK;

	hevccfg = gf_isom_hevc_config_get(file, track, 1);
	lhvccfg = gf_isom_lhvc_config_get(file, track, 1);
	if (!hevccfg && !lhvccfg)
		nalu_size = 4;
	else
		nalu_size = hevccfg ? hevccfg->nal_unit_size : lhvccfg->nal_unit_size;

	cur_extract_mode = gf_isom_get_nalu_extract_mode(file, track);
	gf_isom_set_nalu_extract_mode(file, track, GF_ISOM_NALU_EXTRACT_INSPECT);

	nal_alloc_size = 10000;
	nal_data = gf_malloc(sizeof(char) * nal_alloc_size);

	if (hevccfg) {
		count = gf_list_count(hevccfg->param_array);
		for (i=0; i<count; i++) {
			u32 j, count2;
			GF_NALUFFParamArray *ar = (GF_NALUFFParamArray *)gf_list_get(hevccfg->param_array, i);
			count2 = gf_list_count(ar->nalus);
			for (j=0; j<count2; j++) {
				GF_NALUFFParam *sl = (GF_NALUFFParam *)gf_list_get(ar->nalus, j);
				//u8 nal_type = (sl->data[0] & 0x7E) >> 1;
				u8 layer_id = ((sl->data[0] & 0x1) << 5) | (sl->data[1] >> 3);
				u8 temporal_id_plus_one = sl->data[1] & 0x07;

				if ((max_temporal_id_plus_one && (temporal_id_plus_one > max_temporal_id_plus_one)) || (max_layer_id_plus_one && (layer_id+1 > max_layer_id_plus_one))) {
					gf_list_rem(ar->nalus, j);
					j--;
					count2--;
					gf_free(sl->data);
					gf_free(sl);
				}
			}
		}
	}

	if (lhvccfg) {
		count = gf_list_count(lhvccfg->param_array);
		for (i=0; i<count; i++) {
			u32 j, count2;
			GF_NALUFFParamArray *ar = (GF_NALUFFParamArray *)gf_list_get(lhvccfg->param_array, i);
			count2 = gf_list_count(ar->nalus);
			for (j=0; j<count2; j++) {
				GF_NALUFFParam *sl = (GF_NALUFFParam *)gf_list_get(ar->nalus, j);
				//u8 nal_type = (sl->data[0] & 0x7E) >> 1;
				u8 layer_id = ((sl->data[0] & 0x1) << 5) | (sl->data[1] >> 3);
				u8 temporal_id_plus_one = sl->data[1] & 0x07;

				if ((max_temporal_id_plus_one && (temporal_id_plus_one > max_temporal_id_plus_one)) || (max_layer_id_plus_one && (layer_id+1 > max_layer_id_plus_one))) {
					gf_list_rem(ar->nalus, j);
					j--;
					count2--;
					gf_free(sl->data);
					gf_free(sl);
				}
			}
		}
	}

	//parse all samples
	count = gf_isom_get_sample_count(file, track);
	for (i=0; i<count; i++) {
		GF_BitStream *bs, *dst_bs;
		u32 di;
		GF_ISOSample *sample;

		sample = gf_isom_get_sample(file, track, i+1, &di);

		bs = gf_bs_new(sample->data, sample->dataLength, GF_BITSTREAM_READ);
		dst_bs = gf_bs_new(NULL, 0, GF_BITSTREAM_WRITE);
		while (gf_bs_available(bs)) {
			u32 size = gf_bs_read_int(bs, nalu_size*8);
			u8 fzero = gf_bs_read_int(bs, 1);
			u8 nal_type = gf_bs_read_int(bs, 6);
			u8 layer_id = gf_bs_read_int(bs, 6);
			u8 temporal_id_plus_one = gf_bs_read_int(bs, 3);
			size -= 2;

			if ((max_temporal_id_plus_one && (temporal_id_plus_one > max_temporal_id_plus_one))
				|| (max_layer_id_plus_one && (layer_id+1 > max_layer_id_plus_one))
			) {
				gf_bs_skip_bytes(bs, size);
				continue;
			}

			if (size>nal_alloc_size) {
				nal_alloc_size = size;
				nal_data = (char *)gf_realloc(nal_data, nal_alloc_size);
			}
			gf_bs_read_data(bs, nal_data, size);

			gf_bs_write_int(dst_bs, size+2, nalu_size*8);
			gf_bs_write_int(dst_bs, fzero, 1);
			gf_bs_write_int(dst_bs, nal_type, 6);
			gf_bs_write_int(dst_bs, layer_id, 6);
			gf_bs_write_int(dst_bs, temporal_id_plus_one, 3);
			gf_bs_write_data(dst_bs, nal_data, size);
		}

		gf_bs_del(bs);
		gf_free(sample->data);
		sample->data = NULL;
		sample->dataLength = 0;

		gf_bs_get_content(dst_bs, &sample->data, &sample->dataLength);
		e = gf_isom_update_sample(file, track, i+1, sample, GF_TRUE);
		gf_bs_del(dst_bs);
		gf_isom_sample_del(&sample);

		if (e)
			goto exit;
	}

exit:
	if (lhvccfg) gf_odf_hevc_cfg_del(lhvccfg);
	if (hevccfg) gf_odf_hevc_cfg_del(hevccfg);
	gf_isom_set_nalu_extract_mode(file, track, cur_extract_mode);
	if (nal_data) gf_free(nal_data);
	return e;
}

GF_EXPORT
GF_Err gf_media_split_lhvc(GF_ISOFile *file, u32 track, Bool for_temporal_sublayers, Bool splitAll, GF_LHVCExtractoreMode extractor_mode)
{
#if !defined(GPAC_DISABLE_AV_PARSERS)
	GF_HEVCConfig *hevccfg, *lhvccfg;
	u32 sample_num, count, cur_extract_mode, j, k, max_layer_id;
	char *nal_data=NULL;
	u32 nal_alloc_size;
	u32 nal_unit_size=0;
	Bool single_layer_per_track=GF_TRUE;
	GF_Err e = GF_OK;
	HEVCState *hvc_state;

	GF_SAFEALLOC(hvc_state, HEVCState);
	if (!hvc_state) return GF_OUT_OF_MEM;

	hevccfg = gf_isom_hevc_config_get(file, track, 1);
	lhvccfg = gf_isom_lhvc_config_get(file, track, 1);
	if (!lhvccfg && !for_temporal_sublayers) {
		if (hevccfg) gf_odf_hevc_cfg_del(hevccfg);
		gf_free(hvc_state);
		return GF_OK;
	}
	else if (for_temporal_sublayers) {
		if (lhvccfg) {
			if (hevccfg) gf_odf_hevc_cfg_del(hevccfg);
			gf_odf_hevc_cfg_del(lhvccfg);
			gf_free(hvc_state);
			return GF_NOT_SUPPORTED;
		}
		if (!hevccfg) {
			gf_free(hvc_state);
			return GF_NOT_SUPPORTED;
		}
		if (hevccfg->numTemporalLayers<=1) {
			gf_odf_hevc_cfg_del(hevccfg);
			gf_free(hvc_state);
			return GF_OK;
		}
	}

	cur_extract_mode = gf_isom_get_nalu_extract_mode(file, track);
	gf_isom_set_nalu_extract_mode(file, track, GF_ISOM_NALU_EXTRACT_INSPECT);

	LHVCTrackInfo *sti;
	sti = gf_malloc(sizeof(LHVCTrackInfo)*64);
	if (!sti) return GF_OUT_OF_MEM;

	memset(sti, 0, sizeof(LHVCTrackInfo)*64);
	sti[0].track_num = track;
	sti[0].has_samples=GF_TRUE;
	max_layer_id = 0;

	nal_unit_size = lhvccfg ? lhvccfg->nal_unit_size : hevccfg->nal_unit_size;

	if (!for_temporal_sublayers) {
		u32 i, pass, base_layer_pass = GF_TRUE;
		GF_HEVCConfig *cur_cfg = hevccfg;

reparse:
		//split all SPS/PPS/VPS from lhvccfg
		for (pass=0; pass<3; pass++) {
		count = gf_list_count(cur_cfg->param_array);
		for (i=0; i<count; i++) {
			u32 count2;
			GF_NALUFFParamArray *s_ar;
			GF_NALUFFParamArray *ar = gf_list_get(cur_cfg->param_array, i);
			if ((pass==0) && (ar->type!=GF_HEVC_NALU_VID_PARAM)) continue;
			else if ((pass==1) && (ar->type!=GF_HEVC_NALU_SEQ_PARAM)) continue;
			else if ((pass==2) && (ar->type!=GF_HEVC_NALU_PIC_PARAM)) continue;

			count2 = gf_list_count(ar->nalus);
			for (j=0; j<count2; j++) {
				GF_NALUFFParam *sl = gf_list_get(ar->nalus, j);
//				u8 nal_type = (sl->data[0] & 0x7E) >> 1;
				u8 layer_id = ((sl->data[0] & 0x1) << 5) | (sl->data[1] >> 3);

				if (ar->type==GF_HEVC_NALU_SEQ_PARAM) {
					u32 lw, lh;
					s32 idx = gf_hevc_get_sps_info_with_state(hvc_state, sl->data, sl->size, NULL, &lw, &lh, NULL, NULL);
					if (idx>=0) {
						if (lw > sti[layer_id].width) sti[layer_id].width = lw;
						if (lh > sti[layer_id].height) sti[layer_id].height = lh;
					}
				} else if (ar->type==GF_HEVC_NALU_PIC_PARAM) {
					gf_hevc_read_pps(sl->data, sl->size, hvc_state);
				} else if (ar->type==GF_HEVC_NALU_VID_PARAM) {
					gf_hevc_read_vps(sl->data, sl->size, hvc_state);
				}

				//don't touch base layer
				if (!layer_id) {
					gf_assert(base_layer_pass);
					continue;
				}

				if (!splitAll) layer_id = 1;

				if (max_layer_id < layer_id)
					max_layer_id = layer_id;

				if (!sti[layer_id].lhvccfg) {
					GF_List *backup_list;
					sti[layer_id].lhvccfg = gf_odf_hevc_cfg_new();
					backup_list = sti[layer_id].lhvccfg->param_array;
					memcpy(sti[layer_id].lhvccfg , lhvccfg ? lhvccfg : hevccfg, sizeof(GF_HEVCConfig));
					sti[layer_id].lhvccfg->param_array = backup_list;

					sti[layer_id].lhvccfg->is_lhvc = 1;
					sti[layer_id].lhvccfg->complete_representation = 1;
				}

				s_ar = alloc_hevc_param_array(sti[layer_id].lhvccfg, ar->type);
				gf_list_add(s_ar->nalus, sl);
				gf_list_rem(ar->nalus, j);
				j--;
				count2--;
			}
			}
		}
		if (base_layer_pass) {
			base_layer_pass = GF_FALSE;
			cur_cfg = lhvccfg;
			goto reparse;
		}
	} else {
		gf_isom_set_cts_packing(file, track, GF_TRUE);
	}

	//CLARIFY whether this is correct: we duplicate all VPS in the enhancement layer ...
	//we do this because if we split the tracks some info for setting up the enhancement layer
	//is in the VPS
	if (extractor_mode != GF_LHVC_EXTRACTORS_ON) {
		u32 i;
		count = gf_list_count(hevccfg->param_array);
		for (i=0; i<count; i++) {
			u32 count2;
			GF_NALUFFParamArray *s_ar;
			GF_NALUFFParamArray *ar = gf_list_get(hevccfg->param_array, i);
			if (ar->type != GF_HEVC_NALU_VID_PARAM) continue;
			count2 = gf_list_count(ar->nalus);
			for (j=0; j<count2; j++) {
				GF_NALUFFParam *sl = gf_list_get(ar->nalus, j);
				u8 layer_id = ((sl->data[0] & 0x1) << 5) | (sl->data[1] >> 3);
				if (layer_id) continue;

				for (k=0; k <= max_layer_id; k++) {
					GF_NALUFFParam *sl2;
					if (!sti[k].lhvccfg) continue;

					s_ar = alloc_hevc_param_array(sti[k].lhvccfg, ar->type);
					s_ar->array_completeness = ar->array_completeness;

					GF_SAFEALLOC(sl2, GF_NALUFFParam);
					if (!sl2) break;
					sl2->data = gf_malloc(sl->size);
					if (!sl2->data) {
						gf_free(sl2);
						break;
					}
					memcpy(sl2->data, sl->data, sl->size);
					sl2->id = sl->id;
					sl2->size = sl->size;
					gf_list_add(s_ar->nalus, sl2);
				}
			}
		}
	}

	//update lhvc config
	if (for_temporal_sublayers) {
		e = gf_isom_lhvc_config_update(file, track, 1, NULL, GF_ISOM_LEHVC_WITH_BASE_BACKWARD);
	} else {
		e = gf_isom_lhvc_config_update(file, track, 1, NULL, GF_ISOM_LEHVC_WITH_BASE_BACKWARD);
	}
	if (e) {
		GF_LOG(GF_LOG_ERROR, GF_LOG_MEDIA, ("[HEVC] Failed to update HEVC/LHVC config\n"));
		goto exit;
	}

	//purge all linf sample groups
	gf_isom_remove_sample_group(file, track, GF_ISOM_SAMPLE_GROUP_LINF);

	nal_alloc_size = 10000;
	nal_data = gf_malloc(sizeof(char) * nal_alloc_size);
	//parse all samples
	count = gf_isom_get_sample_count(file, track);
	for (sample_num=0; sample_num<count; sample_num++) {
		GF_BitStream *bs;
		u32 di;
		GF_ISOSample *sample;
		Bool is_irap;
		GF_ISOSampleRollType roll_type;
		s32 roll_distance;
		u8 cur_max_layer_id = 0;

		sample = gf_isom_get_sample(file, track, sample_num+1, &di);
		gf_isom_get_sample_rap_roll_info(file, track, sample_num+1, &is_irap, &roll_type, &roll_distance);

		bs = gf_bs_new(sample->data, sample->dataLength, GF_BITSTREAM_READ);
		while (gf_bs_available(bs)) {
			u8 orig_layer_id, nal_size;
			u32 size = gf_bs_read_int(bs, nal_unit_size*8);
			u32 offset = (u32) gf_bs_get_position(bs);
			u8 fzero = gf_bs_read_int(bs, 1);
			u8 nal_type = gf_bs_read_int(bs, 6);
			u8 layer_id = orig_layer_id = gf_bs_read_int(bs, 6);
			u8 temporal_id = gf_bs_read_int(bs, 3);

			if (for_temporal_sublayers) {
				u32 tid = temporal_id-1;
				if (tid && !sti[tid].layer_id) {
					sti[tid].layer_id=tid;
				}
				layer_id = tid;

				if ((nal_type <= GF_HEVC_NALU_SLICE_CRA)
					&& (nal_type != GF_HEVC_NALU_SLICE_TSA_N)
					&& (nal_type != GF_HEVC_NALU_SLICE_TSA_R))
						sti[layer_id].non_tsa_vcl = GF_TRUE;
			} else {
				if (layer_id && !sti[layer_id].layer_id) {
					sti[layer_id].layer_id=layer_id;
				}
			}
			if (!splitAll && layer_id) layer_id = 1;

			if (cur_max_layer_id < layer_id) {
				cur_max_layer_id = layer_id;
			}

			nal_size = size;

			if (!sti[layer_id].bs)
				sti[layer_id].bs = gf_bs_new(NULL, 0, GF_BITSTREAM_WRITE);

			gf_bs_write_int(sti[layer_id].bs, size, nal_unit_size*8);
			gf_bs_write_int(sti[layer_id].bs, fzero, 1);
			gf_bs_write_int(sti[layer_id].bs, nal_type, 6);
			gf_bs_write_int(sti[layer_id].bs, orig_layer_id, 6);
			gf_bs_write_int(sti[layer_id].bs, temporal_id, 3);
			size -= 2;

			sti[layer_id].layers[layer_id].layer_id_plus_one = layer_id+1;
			sti[layer_id].temporal_id_sample = temporal_id;

			if (!sti[layer_id].layers[layer_id].min_temporal_id || (sti[layer_id].layers[layer_id].min_temporal_id > temporal_id)) {
				sti[layer_id].layers[layer_id].min_temporal_id = temporal_id;
			}
			if (!sti[layer_id].layers[layer_id].max_temporal_id || (sti[layer_id].layers[layer_id].max_temporal_id < temporal_id)) {
				sti[layer_id].layers[layer_id].max_temporal_id = temporal_id;
			}

			if (!sti[layer_id].max_temporal_id_sample || (sti[layer_id].max_temporal_id_sample < temporal_id)) {
				sti[layer_id].max_temporal_id_sample = temporal_id;
			}

			if (! for_temporal_sublayers) {

				if (nal_type==GF_HEVC_NALU_SEQ_PARAM) {
					u32 lw, lh;
					s32 idx = gf_hevc_get_sps_info_with_state(hvc_state, sample->data + offset, nal_size, NULL, &lw, &lh, NULL, NULL);
					if (idx>=0) {
						if (lw > sti[layer_id].width) sti[layer_id].width = lw;
						if (lh > sti[layer_id].height) sti[layer_id].height = lh;
					}
				} else if (nal_type==GF_HEVC_NALU_PIC_PARAM) {
					gf_hevc_read_pps(sample->data + offset, nal_size, hvc_state);
				} else if (nal_type==GF_HEVC_NALU_VID_PARAM) {
					gf_hevc_read_vps(sample->data + offset, nal_size, hvc_state);
				}
			}

			if (size>nal_alloc_size) {
				nal_alloc_size = size;
				nal_data = gf_realloc(nal_data, nal_alloc_size);
			}

			gf_bs_read_data(bs, nal_data, size);
			gf_bs_write_data(sti[layer_id].bs, nal_data, size);
		}
		gf_bs_del(bs);

		if (cur_max_layer_id>max_layer_id) {
			max_layer_id = cur_max_layer_id;
		}
		if (for_temporal_sublayers && hevccfg->numTemporalLayers>max_layer_id+1) {
			max_layer_id = hevccfg->numTemporalLayers-1;
		}

		gf_free(sample->data);
		sample->data = NULL;
		sample->dataLength = 0;
		//reset all samples on all layers found - we may have layers not present in this sample, we still need to process these layers when extractors are used
		for (j=0; j<=max_layer_id; j++) {
			if (!for_temporal_sublayers && ! sti[j].bs) {
				if (!sti[j].track_num || (extractor_mode != GF_LHVC_EXTRACTORS_ON) ) {
					sti[j].data_offset =  sti[j].data_size = 0;
					continue;
				}
			}

			//clone track
			if (! sti[j].track_num) {
				u32 track_id = gf_isom_get_track_id(file, track);
				e = gf_isom_clone_track(file, track, file, 0, &sti[j].track_num);
				if (e) goto exit;

				if (! for_temporal_sublayers) {
					//happens for inband param
					if (!sti[j].lhvccfg) {
						GF_List *backup_list;
						sti[j].lhvccfg = gf_odf_hevc_cfg_new();
						backup_list = sti[j].lhvccfg->param_array;
						memcpy(sti[j].lhvccfg , lhvccfg ? lhvccfg : hevccfg, sizeof(GF_HEVCConfig));
						sti[j].lhvccfg->param_array = backup_list;

						sti[j].lhvccfg->is_lhvc = 1;
						sti[j].lhvccfg->complete_representation = 1;
					}
					e = gf_isom_lhvc_config_update(file, sti[j].track_num, 1, sti[j].lhvccfg, (extractor_mode == GF_LHVC_EXTRACTORS_ON)  ? GF_ISOM_LEHVC_WITH_BASE : GF_ISOM_LEHVC_ONLY);
					if (e) goto exit;

					if (extractor_mode == GF_LHVC_EXTRACTORS_OFF_FORCE_INBAND)
						gf_isom_lhvc_force_inband_config(file, sti[j].track_num, 1);
				} else {
					e = gf_isom_lhvc_config_update(file, sti[j].track_num, 1, NULL, GF_ISOM_LEHVC_WITH_BASE);
					if (e) goto exit;
				}
				if (e) {
					GF_LOG(GF_LOG_ERROR, GF_LOG_MEDIA, ("[HEVC] Failed to update HEVC/LHVC config\n"));
					goto exit;
				}

				gf_isom_set_track_reference(file, sti[j].track_num, GF_ISOM_REF_BASE, track_id);

				//for an L-HEVC bitstream: only base track carries the 'oinf' sample group, other track have a track reference of type 'oref' to base track
				e = gf_isom_remove_sample_group(file, sti[j].track_num, GF_ISOM_SAMPLE_GROUP_OINF);
				if (e) goto exit;
				//purge all linf sample groups
				gf_isom_remove_sample_group(file, sti[j].track_num, GF_ISOM_SAMPLE_GROUP_LINF);
				gf_isom_set_track_reference(file, sti[j].track_num, GF_ISOM_REF_OREF, track_id);

				gf_isom_set_nalu_extract_mode(file, sti[j].track_num, GF_ISOM_NALU_EXTRACT_INSPECT);

				//get lower layer
				if (extractor_mode == GF_LHVC_EXTRACTORS_ON) {
					for (k=j; k>0; k--) {
						if (sti[k-1].track_num) {
							u32 track_id_r = gf_isom_get_track_id(file, sti[k-1].track_num);
							gf_isom_set_track_reference(file, sti[j].track_num, GF_ISOM_REF_SCAL, track_id_r);
						}
					}
				}

				if (!for_temporal_sublayers)
					gf_isom_set_visual_info(file, sti[j].track_num, 1, sti[j].width, sti[j].height);
			} else {
				if (!for_temporal_sublayers)
					gf_isom_set_visual_info(file, sti[j].track_num, 1, sti[j].width, sti[j].height);
			}

			if (j && (extractor_mode == GF_LHVC_EXTRACTORS_ON)) {
				GF_BitStream *xbs = gf_bs_new(NULL, 0, GF_BITSTREAM_WRITE);
				//get all lower layers
				for (k=0; k<j; k++) {
					u8 trefidx, tid;
					if (!sti[k].data_size)
						continue;
					//extractor size 5
					gf_bs_write_int(xbs, 2*nal_unit_size + 5, 8*nal_unit_size);
					gf_bs_write_int(xbs, 0, 1);
					gf_bs_write_int(xbs, GF_HEVC_NALU_FF_EXTRACTOR, 6); //extractor
					gf_bs_write_int(xbs, k, 6);
					gf_bs_write_int(xbs, sti[k].max_temporal_id_sample, 3);
					gf_bs_write_u8(xbs, 0); //constructor type 0
					//set ref track index
					trefidx = (u8) gf_isom_has_track_reference(file, sti[j].track_num, GF_ISOM_REF_SCAL, gf_isom_get_track_id(file, sti[k].track_num) );
					gf_bs_write_int(xbs, trefidx, 8);
					// no sample offset
					gf_bs_write_int(xbs, 0, 8);
					// data offset: we start from beginning of the sample data, not the extractor
					gf_bs_write_int(xbs, sti[k].data_offset, 8*nal_unit_size);
					gf_bs_write_int(xbs, sti[k].data_size, 8*nal_unit_size);

					tid = sti[k].temporal_id_sample;
					sti[j].layers[k].layer_id_plus_one = sti[k].layer_id+1;
					if (!sti[j].layers[k].min_temporal_id || (sti[j].layers[k].min_temporal_id > tid)) {
						sti[j].layers[k].min_temporal_id = tid;
					}
					if (!sti[j].layers[k].max_temporal_id || (sti[j].layers[k].max_temporal_id < tid)) {
						sti[j].layers[k].max_temporal_id = tid;
					}

				}

				gf_bs_get_content(xbs, &sample->data, &sample->dataLength);
				gf_bs_del(xbs);

				//we wrote all our references, store offset for upper layers
				sti[j].data_offset = sample->dataLength;
				e = gf_isom_add_sample(file, sti[j].track_num, 1, sample);
				if (e) {
					GF_LOG(GF_LOG_ERROR, GF_LOG_MEDIA, ("[HEVC] Failed to add HEVC/LHVC sample to track %d\n", sti[j].track_num));
					goto exit;
				}
				gf_free(sample->data);
				sample->data = NULL;

				//get real content, remember its size and add it to the new bs
				if (sti[j].bs) {
					gf_bs_get_content(sti[j].bs, &sample->data, &sample->dataLength);
					e = gf_isom_append_sample_data(file, sti[j].track_num, sample->data, sample->dataLength);
					if (e) {
						GF_LOG(GF_LOG_ERROR, GF_LOG_MEDIA, ("[HEVC] Failed to append HEVC/LHVC data to sample (track %d)\n", sti[j].track_num));
						goto exit;
					}
				}

			}
			//get sample content
			else if (sti[j].bs) {
				//add empty sample at DTS 0
				if ( ! sti[j].has_samples) {
					if (sample->DTS) {
						GF_ISOSample s;
						memset(&s, 0, sizeof(GF_ISOSample));
						gf_isom_add_sample(file, sti[j].track_num, 1, &s);
					}
					sti[j].has_samples=GF_TRUE;
				}
				gf_bs_get_content(sti[j].bs, &sample->data, &sample->dataLength);
				sti[j].data_offset = 0;
				sti[j].data_size = sample->dataLength;

				//add sample
				if (j) {
					GF_ISOSAPType rap = sample->IsRAP;
					if (for_temporal_sublayers && !sti[j].non_tsa_vcl)
						sample->IsRAP = RAP;

					e = gf_isom_add_sample(file, sti[j].track_num, 1, sample);
					sample->IsRAP = rap;
				} else {
					e = gf_isom_update_sample(file, sti[j].track_num, sample_num+1, sample, 1);
				}
				if (e) {
					GF_LOG(GF_LOG_ERROR, GF_LOG_MEDIA, ("[HEVC] Failed to %s HEVC/LHVC sample (track %d, base sample num %d)\n", j ? "add" : "update", sti[j].track_num, sample_num+1));
					goto exit;
				}
			}
			//no data left in sample, update
			else if (!j) {
				e = gf_isom_remove_sample(file, sti[j].track_num, sample_num+1);
				if (e) {
					GF_LOG(GF_LOG_ERROR, GF_LOG_MEDIA, ("[HEVC] Failed to remove HEVC/LHVC sample (track %d)\n", sti[j].track_num));
					goto exit;
				}
				sample_num--;
				count--;
			}

			gf_bs_del(sti[j].bs);
			sti[j].bs = NULL;

			if (sample->IsRAP>SAP_TYPE_1) {
				u32 sample_idx = gf_isom_get_sample_count(file, sti[j].track_num);
				if (is_irap) {
					gf_isom_set_sample_rap_group(file, sti[j].track_num, sample_idx, GF_TRUE, 0);
				}
				else if (roll_type) {
					gf_isom_set_sample_roll_group(file, sti[j].track_num, sample_idx, GF_ISOM_SAMPLE_ROLL, (s16) roll_distance);
				}
			}

			if (sample->data) {
				gf_free(sample->data);
				sample->data = NULL;
			}
			sample->dataLength = 0;
		}
		gf_isom_sample_del(&sample);

		//reset all scalable info
		for (j=0; j<=max_layer_id; j++) {
			sti[j].max_temporal_id_sample = 0;
			sti[j].temporal_id_sample = 0;
			sti[j].data_size = 0;
			sti[j].non_tsa_vcl = GF_FALSE;
		}
	}

exit:
	//reset all scalable info
	for (j=0; j<=max_layer_id; j++) {
		GF_BitStream *bs;
		u32 data_size;
		u8 *data=NULL;
		if (sti[j].lhvccfg) gf_odf_hevc_cfg_del(sti[j].lhvccfg);
		//set linf group
		bs = gf_bs_new(NULL, 0, GF_BITSTREAM_WRITE);
		gf_bs_write_int(bs, 0, 2);
		count = 0;
		for (k=0; k<=max_layer_id; k++) {
			if (sti[j].layers[k].layer_id_plus_one) count++;
		}
		gf_bs_write_int(bs, count, 6);
		if (count>1)
			single_layer_per_track = GF_FALSE;

		for (k=0; k<=max_layer_id; k++) {
			if (! sti[j].layers[k].layer_id_plus_one) continue;
			gf_bs_write_int(bs, 0, 4);
			gf_bs_write_int(bs, sti[j].layers[k].layer_id_plus_one - 1, 6);
			gf_bs_write_int(bs, sti[j].layers[k].min_temporal_id, 3);
			gf_bs_write_int(bs, sti[j].layers[k].max_temporal_id, 3);
			gf_bs_write_int(bs, 0, 1);
			//track carries the NALUs
			if (k==j) {
				gf_bs_write_int(bs, 0xFF, 7);
			}
			//otherwise referenced through extractors, not present natively
			else {
				gf_bs_write_int(bs, 0, 7);
			}
		}
		gf_bs_get_content(bs, &data, &data_size);
		gf_bs_del(bs);
		gf_isom_add_sample_group_info(file, sti[j].track_num, GF_ISOM_SAMPLE_GROUP_LINF, data, data_size, GF_TRUE, &count);
		gf_free(data);
	}
	gf_isom_set_nalu_extract_mode(file, track, cur_extract_mode);

	if (extractor_mode == GF_LHVC_EXTRACTORS_ON) {
		gf_isom_modify_alternate_brand(file, GF_ISOM_BRAND_HVCE, GF_TRUE);
		gf_isom_modify_alternate_brand(file, GF_ISOM_BRAND_HVCI, GF_FALSE);
	}
	//add hvci brand only if single layer per track
	else if (single_layer_per_track) {
		gf_isom_modify_alternate_brand(file, GF_ISOM_BRAND_HVCI, GF_TRUE);
		gf_isom_modify_alternate_brand(file, GF_ISOM_BRAND_HVCE, GF_FALSE);
	}
	if (lhvccfg) gf_odf_hevc_cfg_del(lhvccfg);
	if (hevccfg) gf_odf_hevc_cfg_del(hevccfg);
	if (nal_data) gf_free(nal_data);
	gf_free(hvc_state);

	gf_free(sti);
	return e;
#else
	return GF_NOT_SUPPORTED;
#endif

}
#endif ///GPAC_DISABLE_AV_PARSERS

GF_EXPORT
GF_Err gf_media_change_pl(GF_ISOFile *file, u32 track, u32 profile, u32 compat, u32 level)
{
	u32 i, count, stype;
	GF_Err e;
	GF_AVCConfig *avcc;

	stype = gf_isom_get_media_subtype(file, track, 1);
	switch (stype) {
	case GF_ISOM_SUBTYPE_AVC_H264:
	case GF_ISOM_SUBTYPE_AVC2_H264:
	case GF_ISOM_SUBTYPE_AVC3_H264:
	case GF_ISOM_SUBTYPE_AVC4_H264:
		break;
	default:
		return GF_OK;
	}

	avcc = gf_isom_avc_config_get(file, track, 1);

	if (!avcc)
		return GF_NON_COMPLIANT_BITSTREAM;

	if (level) avcc->AVCLevelIndication = level;
	if (compat) avcc->profile_compatibility = compat;
	if (profile) avcc->AVCProfileIndication = profile;
	count = gf_list_count(avcc->sequenceParameterSets);
	for (i=0; i<count; i++) {
		GF_NALUFFParam *slc = gf_list_get(avcc->sequenceParameterSets, i);
		if (profile) slc->data[1] = profile;
		if (level) slc->data[3] = level;
	}
	e = gf_isom_avc_config_update(file, track, 1, avcc);

	gf_odf_avc_cfg_del(avcc);
	return e;
}

#endif // GPAC_DISABLE_MEDIA_IMPORT

#if !defined(GPAC_DISABLE_AV_PARSERS)
u32 hevc_get_tile_id(HEVCState *hevc, u32 *tile_x, u32 *tile_y, u32 *tile_width, u32 *tile_height)
{
	HEVCSliceInfo *si = &hevc->s_info;
	u32 i, tbX, tbY, PicWidthInCtbsY, PicHeightInCtbsY, tileX, tileY, oX, oY, val;

	PicWidthInCtbsY = si->sps->width / si->sps->max_CU_width;
	if (PicWidthInCtbsY * si->sps->max_CU_width < si->sps->width) PicWidthInCtbsY++;
	PicHeightInCtbsY = si->sps->height / si->sps->max_CU_width;
	if (PicHeightInCtbsY * si->sps->max_CU_width < si->sps->height) PicHeightInCtbsY++;

	tbX = si->slice_segment_address % PicWidthInCtbsY;
	tbY = si->slice_segment_address / PicWidthInCtbsY;

	tileX = tileY = 0;
	oX = oY = 0;
	for (i=0; i < si->pps->num_tile_columns; i++) {
		if (si->pps->uniform_spacing_flag) {
			val = (i+1)*PicWidthInCtbsY / si->pps->num_tile_columns - (i)*PicWidthInCtbsY / si->pps->num_tile_columns;
		} else {
			if (i<si->pps->num_tile_columns-1) {
				val = si->pps->column_width[i];
			} else {
				val = (PicWidthInCtbsY - si->pps->column_width[i-1]);
			}
		}
		*tile_x = oX;
		*tile_width = val;

		if (oX >= tbX) break;
		oX += val;
		tileX++;
	}
	for (i=0; i<si->pps->num_tile_rows; i++) {
		if (si->pps->uniform_spacing_flag) {
			val = (i+1)*PicHeightInCtbsY / si->pps->num_tile_rows - (i)*PicHeightInCtbsY / si->pps->num_tile_rows;
		} else {
			if (i<si->pps->num_tile_rows-1) {
				val = si->pps->row_height[i];
			} else if (i) {
				val = (PicHeightInCtbsY - si->pps->row_height[i-1]);
			} else {
				val = PicHeightInCtbsY;
			}
		}
		*tile_y = oY;
		*tile_height = val;

		if (oY >= tbY) break;
		oY += val;
		tileY++;
	}
	*tile_x = *tile_x * si->sps->max_CU_width;
	*tile_y = *tile_y * si->sps->max_CU_width;
	*tile_width = *tile_width * si->sps->max_CU_width;
	*tile_height = *tile_height * si->sps->max_CU_width;

	if (*tile_x + *tile_width > si->sps->width)
		*tile_width = si->sps->width - *tile_x;
	if (*tile_y + *tile_height > si->sps->height)
		*tile_height = si->sps->height - *tile_y;

	return tileX + tileY * si->pps->num_tile_columns;
}

#ifndef GPAC_DISABLE_MEDIA_IMPORT

typedef struct
{
	u32 track, track_id, sample_count;
	u32 tx, ty, tw, th;
	u32 data_offset;
	GF_BitStream *sample_data;
	u32 nb_nalus_in_sample;
	Bool all_intra;
} HEVCTileImport;

static void hevc_add_trif(GF_ISOFile *file, u32 track, u32 id, Bool full_picture, u32 independent, Bool filtering_disable, u32 tx, u32 ty, u32 tw, u32 th, Bool is_default)
{
	char data[11];
	u32 di, data_size=7;
	GF_BitStream *bs;
	//avoid gcc warnings
	memset(data, 0, 11);
	//write TRIF sample group description
	bs = gf_bs_new((const char*)data, 11, GF_BITSTREAM_WRITE);
	gf_bs_write_u16(bs, id);	//groupID
	gf_bs_write_int(bs, 1, 1); //tile Region flag always true for us
	gf_bs_write_int(bs, independent, 2); //independentIDC: set to 1 (motion-constrained tiles but not all tiles RAP)
	gf_bs_write_int(bs, full_picture, 1);//full picture: false since we don't do L-HEVC tiles
	gf_bs_write_int(bs, filtering_disable, 1); //filtering disabled: set to 1 (always true on our bitstreams for now) - Check xPS to be sure ...
	gf_bs_write_int(bs, 0, 1);//has dependency list: false since we don't do L-HEVC tiles
	gf_bs_write_int(bs, 0, 2); //reserved
	if (!full_picture) {
		gf_bs_write_u16(bs, tx);
		gf_bs_write_u16(bs, ty);
		data_size+=4;
	}
	gf_bs_write_u16(bs, tw);
	gf_bs_write_u16(bs, th);
	gf_bs_del(bs);

	gf_isom_add_sample_group_info(file, track, GF_ISOM_SAMPLE_GROUP_TRIF, data, data_size, is_default, &di);
}

GF_EXPORT
GF_Err gf_media_split_hevc_tiles(GF_ISOFile *file, u32 signal_mode)
{
#if defined(GPAC_DISABLE_AV_PARSERS)
	return GF_NOT_SUPPORTED;
#else
	u32 i, j, cur_tile, count, stype, track, nb_tiles, di, nalu_size_length, tx, ty, tw, th;
	s32 pps_idx=-1, sps_idx=-1, ret;
	GF_Err e = GF_OK;
	HEVCState *hvc_state;
	HEVCTileImport *tiles;
	GF_HEVCConfig *hvcc;
	Bool filter_disabled=GF_TRUE;

	track = 0;
	for (i=0; i<gf_isom_get_track_count(file); i++) {
		stype = gf_isom_get_media_subtype(file, i+1, 1);
		switch (stype) {
		case GF_ISOM_SUBTYPE_HVC1:
		case GF_ISOM_SUBTYPE_HEV1:
		case GF_ISOM_SUBTYPE_HVC2:
		case GF_ISOM_SUBTYPE_HEV2:

			if (track) return GF_NOT_SUPPORTED;
			track = i+1;
			break;
		default:
			break;
		}
	}
	if (!track) return GF_NOT_SUPPORTED;

	hvcc = gf_isom_hevc_config_get(file, track, 1);
	nalu_size_length = hvcc->nal_unit_size;

	GF_SAFEALLOC(hvc_state, HEVCState);
	if (!hvc_state) return GF_OUT_OF_MEM;

	count = gf_list_count(hvcc->param_array);
	for (i=0; i<count; i++) {
		GF_NALUFFParamArray *ar = gf_list_get(hvcc->param_array, i);
		for (j=0; j < gf_list_count(ar->nalus); j++) {
			GF_NALUFFParam *sl = gf_list_get(ar->nalus, j);
			if (!sl) continue;
			switch (ar->type) {
			case GF_HEVC_NALU_PIC_PARAM:
				pps_idx = gf_hevc_read_pps(sl->data, sl->size, hvc_state);
				break;
			case GF_HEVC_NALU_SEQ_PARAM:
				sps_idx = gf_hevc_read_sps(sl->data, sl->size, hvc_state);
				break;
			case GF_HEVC_NALU_VID_PARAM:
				gf_hevc_read_vps(sl->data, sl->size, hvc_state);
				break;
			}
		}
	}
	gf_isom_hevc_set_tile_config(file, track, 1, hvcc, GF_TRUE);
	gf_odf_hevc_cfg_del(hvcc);

	//if params sets are inband, get first sps/pps
	i=0;
	while ((pps_idx==-1) || (sps_idx==-1)) {
		GF_ISOSample *sample = gf_isom_get_sample(file, track, i+1, &di);
		char *data = sample->data;
		u32 size = sample->dataLength;

		while (size) {
			u8 temporal_id, layer_id;
			u8 nal_type = 0;
			u32 nalu_size = 0;

			for (j=0; j<nalu_size_length; j++) {
				nalu_size = (nalu_size<<8) + data[j];
			}
			gf_hevc_parse_nalu(data + nalu_size_length, nalu_size, hvc_state, &nal_type, &temporal_id, &layer_id);

			switch (nal_type) {
			case GF_HEVC_NALU_PIC_PARAM:
				pps_idx = gf_hevc_read_pps((char *) data+nalu_size_length, nalu_size, hvc_state);
				break;
			case GF_HEVC_NALU_SEQ_PARAM:
				sps_idx = gf_hevc_read_sps((char *) data+nalu_size_length, nalu_size, hvc_state);
				break;
			case GF_HEVC_NALU_VID_PARAM:
				gf_hevc_read_vps((char *) data+nalu_size_length, nalu_size, hvc_state);
				break;
			}
			data += nalu_size + nalu_size_length;
			size -= nalu_size + nalu_size_length;
		}
		gf_isom_sample_del(&sample);
	}

	if ((pps_idx==-1) || (sps_idx==-1)) {
		gf_free(hvc_state);
		return GF_BAD_PARAM;
	}

	if (hvc_state->pps[pps_idx].loop_filter_across_tiles_enabled_flag)
		filter_disabled=GF_FALSE;

	if (! hvc_state->pps[pps_idx].tiles_enabled_flag) {
		hevc_add_trif(file, track, gf_isom_get_track_id(file, track), GF_TRUE, 1, filter_disabled, 0, 0, hvc_state->sps[pps_idx].width, hvc_state->sps[pps_idx].height, GF_TRUE);
		GF_LOG(GF_LOG_WARNING, GF_LOG_MEDIA, ("[HEVC Tiles] Tiles not enabled, signal only single tile full picture\n"));
		gf_free(hvc_state);
		return GF_OK;
	}

	nb_tiles = hvc_state->pps[pps_idx].num_tile_columns * hvc_state->pps[pps_idx].num_tile_rows;
	tiles = gf_malloc(sizeof(HEVCTileImport) * nb_tiles);
	if (!tiles) {
		gf_free(hvc_state);
		return GF_OUT_OF_MEM;
	}
	memset(tiles, 0, sizeof(HEVCTileImport) * nb_tiles);

	for (i=0; i<nb_tiles; i++) {
		if (! signal_mode) {
			//first clone tracks
			e = gf_isom_clone_track(file, track, file, 0, &tiles[i].track );
			if (e) goto err_exit;
			tiles[i].track_id = gf_isom_get_track_id(file, tiles[i].track);
			gf_isom_hevc_set_tile_config(file, tiles[i].track, 1, NULL, GF_FALSE);

			// setup track references from tile track to base
			gf_isom_set_track_reference(file, tiles[i].track, GF_ISOM_REF_TBAS, gf_isom_get_track_id(file, track) );
		} else {
			tiles[i].track_id = gf_isom_get_track_id(file, track) + i+1;
		}
		tiles[i].all_intra = GF_TRUE;
	}

	count = gf_isom_get_sample_count(file, track);
	for (i=0; i<count; i++) {
		u8 *data;
		u32 size, nb_nalus=0, nb_nal_entries=0, last_tile_group=(u32) -1;
		GF_BitStream *bs=NULL;
		GF_ISOSample *sample = gf_isom_get_sample(file, track, i+1, &di);
		if (!sample) {
			e = gf_isom_last_error(file);
			goto err_exit;
		}

		data = (u8 *) sample->data;
		size = sample->dataLength;
		if (!signal_mode) {
			bs = gf_bs_new(NULL, 0, GF_BITSTREAM_WRITE);
			sample->data = NULL;
			sample->dataLength = 0;

			for (j=0; j<nb_tiles; j++) {
				tiles[j].data_offset = 0;
				tiles[j].sample_data = gf_bs_new(NULL, 0, GF_BITSTREAM_WRITE);
			}
		} else {
			for (j=0; j<nb_tiles; j++) {
				tiles[j].nb_nalus_in_sample = 0;
			}
			bs = gf_bs_new(NULL, 0, GF_BITSTREAM_WRITE);
			//write start of nalm group
			gf_bs_write_int(bs, 0, 6);//reserved
			gf_bs_write_int(bs, 0, 1);//large_size
			gf_bs_write_int(bs, (signal_mode==2) ? 1 : 0, 1);//rle
			gf_bs_write_u8(bs, 0);//entry_count - will be set at the end
		}


		sample->data = (char *) data;

		while (size) {
			u8 temporal_id, layer_id;
			u8 nal_type = 0;
			u32 nalu_size = 0;
			for (j=0; j<nalu_size_length; j++) {
				nalu_size = (nalu_size<<8) + data[j];
			}
			ret = gf_hevc_parse_nalu(data + nalu_size_length, nalu_size, hvc_state, &nal_type, &temporal_id, &layer_id);

			//error parsing NAL, set nal to fallback to regular import
			if (ret<0) nal_type = GF_HEVC_NALU_VID_PARAM;

			switch (nal_type) {
			case GF_HEVC_NALU_SLICE_TRAIL_N:
			case GF_HEVC_NALU_SLICE_TRAIL_R:
			case GF_HEVC_NALU_SLICE_TSA_N:
			case GF_HEVC_NALU_SLICE_TSA_R:
			case GF_HEVC_NALU_SLICE_STSA_N:
			case GF_HEVC_NALU_SLICE_STSA_R:
			case GF_HEVC_NALU_SLICE_BLA_W_LP:
			case GF_HEVC_NALU_SLICE_BLA_W_DLP:
			case GF_HEVC_NALU_SLICE_BLA_N_LP:
			case GF_HEVC_NALU_SLICE_IDR_W_DLP:
			case GF_HEVC_NALU_SLICE_IDR_N_LP:
			case GF_HEVC_NALU_SLICE_CRA:
			case GF_HEVC_NALU_SLICE_RADL_R:
			case GF_HEVC_NALU_SLICE_RADL_N:
			case GF_HEVC_NALU_SLICE_RASL_R:
			case GF_HEVC_NALU_SLICE_RASL_N:
				tx = ty = tw = th = 0;
				cur_tile = hevc_get_tile_id(hvc_state, &tx, &ty, &tw, &th);
				if (cur_tile>=nb_tiles) {
					GF_LOG(GF_LOG_ERROR, GF_LOG_MEDIA, ("[HEVC Tiles] Tile index %d is greater than number of tiles %d in PPS\n", cur_tile, nb_tiles));
					e = GF_NON_COMPLIANT_BITSTREAM;
				}
				if (e)
					goto err_exit;

				tiles[cur_tile].tx = tx;
				tiles[cur_tile].ty = ty;
				tiles[cur_tile].tw = tw;
				tiles[cur_tile].th = th;
				if (hvc_state->s_info.slice_type != GF_HEVC_SLICE_TYPE_I) {
					tiles[cur_tile].all_intra = 0;
				}

				if (signal_mode) {
					nb_nalus++;
					tiles[cur_tile].nb_nalus_in_sample++;
					if (signal_mode==1) {
						gf_bs_write_u16(bs, tiles[cur_tile].track_id);
						nb_nal_entries++;
					} else if (last_tile_group != tiles[cur_tile].track_id) {
						last_tile_group = tiles[cur_tile].track_id;
						gf_bs_write_u8(bs, nb_nalus);
						gf_bs_write_u16(bs, tiles[cur_tile].track_id);
						nb_nal_entries++;
					}
				} else {
					gf_bs_write_data(tiles[cur_tile].sample_data, (char *) data, nalu_size + nalu_size_length);

					if (! gf_isom_has_track_reference(file, track, GF_ISOM_REF_SABT, tiles[cur_tile].track_id)) {
						gf_isom_set_track_reference(file, track, GF_ISOM_REF_SABT, tiles[cur_tile].track_id);
					}
					tiles[cur_tile].data_offset += nalu_size + nalu_size_length;
				}
				break;
			default:
				if (! signal_mode) {
					gf_bs_write_data(bs, (char *) data, nalu_size + nalu_size_length);
				} else {
					nb_nalus++;
					if (signal_mode==1) {
						gf_bs_write_u16(bs, 0);
						nb_nal_entries++;
					} else if (last_tile_group != 0) {
						last_tile_group = 0;
						gf_bs_write_u8(bs, nb_nalus);
						gf_bs_write_u16(bs, 0);
						nb_nal_entries++;
					}
				}
				break;
			}
			data += nalu_size + nalu_size_length;
			size -= nalu_size + nalu_size_length;
		}

		if (! signal_mode) {
			gf_free(sample->data);
			gf_bs_get_content(bs, &sample->data, &sample->dataLength);
			gf_bs_del(bs);

			e = gf_isom_update_sample(file, track, i+1, sample, 1);
			if (e) goto err_exit;

			gf_free(sample->data);
			sample->data = NULL;

			for (j=0; j<nb_tiles; j++) {
				sample->dataLength = 0;
				gf_bs_get_content(tiles[j].sample_data, &sample->data, &sample->dataLength);
				if (!sample->data)
					continue;

				e = gf_isom_add_sample(file, tiles[j].track, 1, sample);
				if (e) goto err_exit;
				tiles[j].sample_count ++;

				gf_bs_del(tiles[j].sample_data);
				tiles[j].sample_data = NULL;
				gf_free(sample->data);
				sample->data = NULL;

				e = gf_isom_copy_sample_info(file, tiles[j].track, file, track, i+1);
				if (e) goto err_exit;
			}
		} else {
			u32 sdesc;
			data=NULL;
			size=0;
			gf_bs_get_content(bs, &data, &size);
			gf_bs_del(bs);
			data[1] = nb_nal_entries;

			e = gf_isom_add_sample_group_info(file, track, GF_ISOM_SAMPLE_GROUP_NALM, data, size, 1, &sdesc);
			if (e) {
				GF_LOG(GF_LOG_ERROR, GF_LOG_CONTAINER, ("[ISOBMF] Error defining NALM group description entry\n" ));
			} else {
				e = gf_isom_add_sample_info(file, track, i+1, GF_ISOM_SAMPLE_GROUP_NALM, sdesc, GF_ISOM_SAMPLE_GROUP_TRIF);
				if (e) {
					GF_LOG(GF_LOG_ERROR, GF_LOG_CONTAINER, ("[ISOBMF] Error associating NALM group description to sample\n" ));
				}
			}
			gf_free(data);
			if (e) goto err_exit;
		}

		gf_isom_sample_del(&sample);

	}


	for (i=0; i<nb_tiles; i++) {
		u32 width, height;
		s32 translation_x, translation_y;
		s16 layer;

		if (! signal_mode) {
			tiles[i].track = gf_isom_get_track_by_id(file, tiles[i].track_id);
			if (!tiles[i].sample_count) {
				gf_isom_remove_track(file, tiles[i].track);
				continue;
			}

			hevc_add_trif(file, tiles[i].track, tiles[i].track_id, GF_FALSE, (tiles[i].all_intra) ? 2 : 1, filter_disabled, tiles[i].tx, tiles[i].ty, tiles[i].tw, tiles[i].th, GF_TRUE);
			gf_isom_set_visual_info(file, tiles[i].track, 1, tiles[i].tw, tiles[i].th);

			gf_isom_get_track_layout_info(file, track, &width, &height, &translation_x, &translation_y, &layer);
			gf_isom_set_track_layout_info(file, tiles[i].track, width<<16, height<<16, translation_x, translation_y, layer);
		} else {
			hevc_add_trif(file, track, tiles[i].track_id, GF_FALSE, (tiles[i].all_intra) ? 2 : 1, filter_disabled, tiles[i].tx, tiles[i].ty, tiles[i].tw, tiles[i].th, GF_FALSE);
		}

	}


err_exit:
	gf_free(hvc_state);
	gf_free(tiles);
	if (e) {
		GF_LOG(GF_LOG_ERROR, GF_LOG_CONTAINER, ("[ISOBMF] Could not split HEVC tiles into tracks: %s\n", gf_error_to_string(e) ));
	}
	return e;
#endif
}
#endif /*GPAC_DISABLE_AV_PARSERS*/

#endif /*GPAC_DISABLE_MEDIA_IMPORT*/

#ifndef GPAC_DISABLE_ISOM_FRAGMENTS

#include <gpac/filters.h>

typedef struct
{
	u32 filter_idx_plus_one;
	u32 last_prog;
	GF_FilterSession *fsess;
} FragCallback;

extern char gf_prog_lf;

static Bool on_frag_event(void *_udta, GF_Event *evt)
{
	u32 i, count;
	GF_FilterStats stats;
	FragCallback *fc = (FragCallback *)_udta;
	if (!_udta)
		return GF_FALSE;
	if (evt && (evt->type != GF_EVENT_PROGRESS))
		return GF_FALSE;

	stats.report_updated = GF_FALSE;
	if (!fc->filter_idx_plus_one) {
		count = gf_fs_get_filters_count(fc->fsess);
		for (i=0; i<count; i++) {
			if (gf_fs_get_filter_stats(fc->fsess, i, &stats) != GF_OK) continue;
			if (strcmp(stats.reg_name, "mp4mx")) continue;
			fc->filter_idx_plus_one = i+1;
			break;
		}
		if (!fc->filter_idx_plus_one) return GF_FALSE;
	} else {
		if (gf_fs_get_filter_stats(fc->fsess, fc->filter_idx_plus_one-1, &stats) != GF_OK)
			return GF_FALSE;
	}
	if (! stats.report_updated) return GF_FALSE;
	if (stats.percent/100 == fc->last_prog) return GF_FALSE;
	fc->last_prog = stats.percent / 100;

#ifndef GPAC_DISABLE_LOG
	GF_LOG(GF_LOG_INFO, GF_LOG_APP, ("Fragmenting: % 2.2f %%%c", ((Double)stats.percent) / 100, gf_prog_lf));
#else
	fprintf(stderr, "Fragmenting: % 2.2f %%%c", ((Double)stats.percent) / 100, gf_prog_lf);
#endif
	return GF_FALSE;
}

GF_EXPORT
GF_Err gf_media_fragment_file(GF_ISOFile *input, const char *output_file, Double max_duration_sec, Bool use_mfra)
{
	char szArgs[1024];
	FragCallback fc;
	GF_Err e = GF_OK;
	GF_Filter *f;
	GF_FilterSession *fsess = gf_fs_new_defaults(0);

	if (!fsess) {
		GF_LOG(GF_LOG_ERROR, GF_LOG_MEDIA, ("Failed to create filter session\n"));
		return GF_OUT_OF_MEM;
	}


	sprintf(szArgs, "mp4dmx:mov=%p", input);
	f = gf_fs_load_filter(fsess, szArgs, &e);
	if (!f) { gf_fs_del(fsess); return e; }

	strcpy(szArgs, "reframer:FID=1");
	f = gf_fs_load_filter(fsess, szArgs, &e);
	if (!f) { gf_fs_del(fsess); return e; }

	sprintf(szArgs, "%s:SID=1:frag:cdur=%g:abs_offset:fragdur", output_file, max_duration_sec);
	if (use_mfra)
		strcat(szArgs, ":mfra");

	f = gf_fs_load_destination(fsess, szArgs, NULL, NULL, &e);
	if (!f) { gf_fs_del(fsess); return e; }

	if (!gf_sys_is_test_mode()
#ifndef GPAC_DISABLE_LOG
		&& (gf_log_get_tool_level(GF_LOG_APP)!=GF_LOG_QUIET)
#endif
		&& !gf_sys_is_quiet()
	) {
		fc.last_prog=0;
		fc.fsess=fsess;
		fc.filter_idx_plus_one=0;
		gf_fs_enable_reporting(fsess, GF_TRUE);
		gf_fs_set_ui_callback(fsess, on_frag_event, &fc);
	}

#ifdef GPAC_ENABLE_COVERAGE
	if (gf_sys_is_cov_mode())
		on_frag_event(NULL, NULL);
#endif

	e = gf_fs_run(fsess);
	if (e==GF_EOS) e = GF_OK;
	if (!e) e = gf_fs_get_last_connect_error(fsess);
	if (!e) e = gf_fs_get_last_process_error(fsess);
	gf_fs_del(fsess);
	return e;
}

#endif /*GPAC_DISABLE_ISOM_FRAGMENTS*/


GF_Err rfc_6381_get_codec_aac(char *szCodec, u32 codec_id,  u8 *dsi, u32 dsi_size, Bool force_sbr)
{
	if (dsi && dsi_size) {
		u8 audio_object_type;
		if (dsi_size < 2) {
			GF_LOG(GF_LOG_ERROR, GF_LOG_MEDIA, ("[RFC6381] invalid DSI size %u < 2\n", dsi_size));
			return GF_NON_COMPLIANT_BITSTREAM;
		}
		/*5 first bits of AAC config*/
		audio_object_type = (dsi[0] & 0xF8) >> 3;
		if (audio_object_type == 31) { /*escape code*/
			const u8 audio_object_type_ext = ((dsi[0] & 0x07) << 3) + ((dsi[1] & 0xE0) >> 5);
			audio_object_type = 32 + audio_object_type_ext;
		}
#ifndef GPAC_DISABLE_AV_PARSERS
		if (force_sbr && (audio_object_type==2) ) {
			GF_M4ADecSpecInfo a_cfg;
			GF_Err e = gf_m4a_get_config(dsi, dsi_size, &a_cfg);
			if (e==GF_OK) {
				if (a_cfg.sbr_sr)
					audio_object_type = a_cfg.sbr_object_type;
				if (a_cfg.has_ps)
					audio_object_type = 29;
			}
		}
#endif
		snprintf(szCodec, RFC6381_CODEC_NAME_SIZE_MAX, "mp4a.%02X.%01d", gf_codecid_oti(codec_id), audio_object_type);
		return GF_OK;
	}

	snprintf(szCodec, RFC6381_CODEC_NAME_SIZE_MAX, "mp4a.%02X", codec_id);

	switch (codec_id) {
	case GF_CODECID_AAC_MPEG4:
	case GF_CODECID_AAC_MPEG2_MP:
	case GF_CODECID_AAC_MPEG2_LCP:
	case GF_CODECID_AAC_MPEG2_SSRP:
	case GF_CODECID_USAC:
		GF_LOG(GF_LOG_WARNING, GF_LOG_MEDIA, ("[RFC6381] Cannot find AAC config, using default %s\n", szCodec));
		break;
	}
	return GF_OK;
}

GF_Err dolby_get_codec_ac4(char *szCodec, u32 codec_id,  u8 *dsi, u32 dsi_size)
{
	if (dsi && dsi_size) {
		u8 bitstream_version = 0;
		u16 n_presentations = 0;
		u8 presentation_version = 0;
		u8 mdcompat = 0;

		if (dsi_size < 3) {
			GF_LOG(GF_LOG_ERROR, GF_LOG_MEDIA, ("[AC4] invalid DSI size %u < 3\n", dsi_size));
			return GF_NON_COMPLIANT_BITSTREAM;
		}
		/* 7 bits of AC4 config*/
		bitstream_version = ((dsi[0] & 0x1F) << 5) + ((dsi[1] & 0xC0) >> 6);
		/* 9 bits of AC4 config*/
		n_presentations = ((dsi[1] & 0x01) << 8) + dsi[2];
		if (n_presentations > 0) {
			if (dsi_size < 15) {
				GF_LOG(GF_LOG_ERROR, GF_LOG_MEDIA, ("[AC4] invalid DSI size %u < 15\n", dsi_size));
				return GF_NON_COMPLIANT_BITSTREAM;
			}

			presentation_version = dsi[12];
			mdcompat = dsi[14] & 0x07;
		}

		snprintf(szCodec, RFC6381_CODEC_NAME_SIZE_MAX, "ac-4.%02d.%02d.%02d", bitstream_version, presentation_version, mdcompat);
		return GF_OK;
	}

	snprintf(szCodec, RFC6381_CODEC_NAME_SIZE_MAX, "ac-4.%02X", codec_id);
	return GF_OK;
}

GF_Err rfc_6381_get_codec_m4v(char *szCodec, u32 codec_id, u8 *dsi, u32 dsi_size)
{
#ifndef GPAC_DISABLE_AV_PARSERS
	if (dsi && dsi_size) {
		GF_M4VDecSpecInfo m4vc;
		gf_m4v_get_config(dsi, dsi_size, &m4vc);
		snprintf(szCodec, RFC6381_CODEC_NAME_SIZE_MAX, "mp4v.%02X.%01x", codec_id, m4vc.VideoPL);
		return GF_OK;
	}
#endif
	snprintf(szCodec, RFC6381_CODEC_NAME_SIZE_MAX, "mp4v.%02X", codec_id);
	GF_LOG(GF_LOG_WARNING, GF_LOG_MEDIA, ("[RFC6381] Cannot find M4V config, using default %s\n", szCodec));
	return GF_OK;
}

GF_Err rfc_6381_get_codec_avc(char *szCodec, u32 subtype, GF_AVCConfig *avcc)
{
	gf_assert(avcc);
	snprintf(szCodec, RFC6381_CODEC_NAME_SIZE_MAX, "%s.%02X%02X%02X", gf_4cc_to_str(subtype), avcc->AVCProfileIndication, avcc->profile_compatibility, avcc->AVCLevelIndication);
	return GF_OK;
}

GF_Err rfc_6381_get_codec_hevc(char *szCodec, u32 subtype, GF_HEVCConfig *hvcc)
{
	u8 c;
	char szTemp[RFC6381_CODEC_NAME_SIZE_MAX];
	gf_assert(hvcc);

	snprintf(szCodec, RFC6381_CODEC_NAME_SIZE_MAX, "%s.", gf_4cc_to_str(subtype));
	if (hvcc->profile_space==1) strcat(szCodec, "A");
	else if (hvcc->profile_space==2) strcat(szCodec, "B");
	else if (hvcc->profile_space==3) strcat(szCodec, "C");
	//profile idc encoded as a decimal number
	sprintf(szTemp, "%d", hvcc->profile_idc);
	strcat(szCodec, szTemp);
	//general profile compatibility flags: hexa, bit-reversed
	{
		u32 val = hvcc->general_profile_compatibility_flags;
		u32 i, res = 0;
		for (i=0; i<32; i++) {
			res |= val & 1;
			if (i==31) break;
			res <<= 1;
			val >>=1;
		}
		sprintf(szTemp, ".%X", res);
		strcat(szCodec, szTemp);
	}

	if (hvcc->tier_flag) strcat(szCodec, ".H");
	else strcat(szCodec, ".L");
	sprintf(szTemp, "%d", hvcc->level_idc);
	strcat(szCodec, szTemp);

	c = hvcc->progressive_source_flag << 7;
	c |= hvcc->interlaced_source_flag << 6;
	c |= hvcc->non_packed_constraint_flag << 5;
	c |= hvcc->frame_only_constraint_flag << 4;
	c |= (hvcc->constraint_indicator_flags >> 40);
	sprintf(szTemp, ".%X", c);
	strcat(szCodec, szTemp);
	if (hvcc->constraint_indicator_flags & 0xFFFFFFFF) {
		c = (hvcc->constraint_indicator_flags >> 32) & 0xFF;
		sprintf(szTemp, ".%X", c);
		strcat(szCodec, szTemp);
		if (hvcc->constraint_indicator_flags & 0x00FFFFFF) {
			c = (hvcc->constraint_indicator_flags >> 24) & 0xFF;
			sprintf(szTemp, ".%X", c);
			strcat(szCodec, szTemp);
			if (hvcc->constraint_indicator_flags & 0x0000FFFF) {
				c = (hvcc->constraint_indicator_flags >> 16) & 0xFF;
				sprintf(szTemp, ".%X", c);
				strcat(szCodec, szTemp);
				if (hvcc->constraint_indicator_flags & 0x000000FF) {
					c = (hvcc->constraint_indicator_flags >> 8) & 0xFF;
					sprintf(szTemp, ".%X", c);
					strcat(szCodec, szTemp);
					c = (hvcc->constraint_indicator_flags ) & 0xFF;
					sprintf(szTemp, ".%X", c);
					strcat(szCodec, szTemp);
				}
			}
		}
	}
	return GF_OK;
}

GF_Err rfc_6381_get_codec_av1(char *szCodec, u32 subtype, GF_AV1Config *av1c, COLR colr)
{
#ifndef GPAC_DISABLE_AV_PARSERS
	GF_Err e;
	u32 i = 0;
	AV1State *av1_state;
	gf_assert(av1c);

	GF_SAFEALLOC(av1_state, AV1State);
	if (!av1_state) return GF_OUT_OF_MEM;
	gf_av1_init_state(av1_state);
	av1_state->config = av1c;

	for (i = 0; i < gf_list_count(av1c->obu_array); ++i) {
		GF_BitStream *bs;
		GF_AV1_OBUArrayEntry *a = gf_list_get(av1c->obu_array, i);
		bs = gf_bs_new(a->obu, a->obu_length, GF_BITSTREAM_READ);
		if (!av1_is_obu_header(a->obu_type))
			GF_LOG(GF_LOG_WARNING, GF_LOG_MEDIA, ("[RFC6381] AV1: unexpected obu_type %d - Parsing anyway.\n", a->obu_type, gf_4cc_to_str(subtype)));

		e = aom_av1_parse_temporal_unit_from_section5(bs, av1_state);
		gf_bs_del(bs);
		bs = NULL;
		if (e) {
			return e;
		}
	}

	snprintf(szCodec, RFC6381_CODEC_NAME_SIZE_MAX, "%s.%01u.%02u%c.%02u", gf_4cc_to_str(subtype),
		av1_state->config->seq_profile, av1_state->config->seq_level_idx_0, av1_state->config->seq_tier_0 ? 'H' : 'M', av1_state->bit_depth);

	if (av1_state->color_description_present_flag) {
		char tmp[RFC6381_CODEC_NAME_SIZE_MAX];
		snprintf(tmp, RFC6381_CODEC_NAME_SIZE_MAX, ".%01u.%01u%01u%01u.%02u.%02u.%02u.%01u",
			av1_state->config->monochrome, av1_state->config->chroma_subsampling_x, av1_state->config->chroma_subsampling_y,
			av1_state->config->chroma_subsampling_x && av1_state->config->chroma_subsampling_y ? av1_state->config->chroma_sample_position : 0,
			colr.override == GF_TRUE ? colr.colour_primaries : av1_state->color_primaries,
			colr.override == GF_TRUE ? colr.transfer_characteristics : av1_state->transfer_characteristics,
			colr.override == GF_TRUE ? colr.matrix_coefficients : av1_state->matrix_coefficients,
			colr.override == GF_TRUE ? colr.full_range : av1_state->color_range);
		strcat(szCodec, tmp);
	} else {
		if ((av1_state->color_primaries == 2) && (av1_state->transfer_characteristics == 2) && (av1_state->matrix_coefficients == 2) && av1_state->color_range == GF_FALSE) {

		} else {
			GF_LOG(GF_LOG_WARNING, GF_LOG_MEDIA, ("[RFC6381] incoherent color characteristics primaries %d transfer %d matrix %d color range %d\n",
				av1_state->color_primaries, av1_state->transfer_characteristics, av1_state->matrix_coefficients, av1_state->color_range));
		}
	}
	gf_av1_reset_state(av1_state, GF_TRUE);
	gf_free(av1_state);
	return GF_OK;
#else
	return GF_NOT_SUPPORTED;
#endif
}

GF_Err rfc_6381_get_codec_vpx(char *szCodec, u32 subtype, GF_VPConfig *vpcc, COLR colr)
{
	gf_assert(vpcc);
	snprintf(szCodec, RFC6381_CODEC_NAME_SIZE_MAX, "%s.%02u.%02u.%02u.%02u.%02u.%02u.%02u.%02u", gf_4cc_to_str(subtype),
		vpcc->profile,
		vpcc->level,
		vpcc->bit_depth,
		vpcc->chroma_subsampling,
		colr.override == GF_TRUE ? colr.colour_primaries : vpcc->colour_primaries,
		colr.override == GF_TRUE ? colr.transfer_characteristics : vpcc->transfer_characteristics,
		colr.override == GF_TRUE ? colr.matrix_coefficients : vpcc->matrix_coefficients,
		colr.override == GF_TRUE ? colr.full_range : vpcc->video_fullRange_flag);
	return GF_OK;
}

GF_Err rfc_6381_get_codec_dolby_vision(char *szCodec, u32 subtype, GF_DOVIDecoderConfigurationRecord *dovi)
{
	snprintf(szCodec, RFC6381_CODEC_NAME_SIZE_MAX, "%s.%02u.%02u", gf_4cc_to_str(subtype), dovi->dv_profile, dovi->dv_level);
	return GF_OK;
}

static char base32_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

GF_Err rfc_6381_get_codec_vvc(char *szCodec, u32 subtype, GF_VVCConfig *vvcc)
{
	gf_assert(vvcc);
	u32 i, pos, len;

	if ( (subtype==GF_4CC('v','v','c','N')) || (subtype==GF_4CC('v','v','s','1')) ) {
		snprintf(szCodec, RFC6381_CODEC_NAME_SIZE_MAX, "%s", gf_4cc_to_str(subtype));
	} else {
		snprintf(szCodec, RFC6381_CODEC_NAME_SIZE_MAX, "%s.%d.%s%d", gf_4cc_to_str(subtype), vvcc->general_profile_idc, vvcc->general_tier_flag ? "H" : "L", vvcc->general_level_idc);
	}

	u8 buf[256];
	GF_BitStream *bs = gf_bs_new(buf, 256, GF_BITSTREAM_WRITE);
	gf_bs_write_int(bs, vvcc->ptl_frame_only_constraint, 1);
	gf_bs_write_int(bs, vvcc->ptl_multilayer_enabled, 1);
	for (i=0; i<vvcc->num_constraint_info; i++) {
		gf_bs_write_int(bs, vvcc->general_constraint_info[i], 8);
	}
	gf_bs_align(bs);
	pos = (u32) gf_bs_get_position(bs);
	gf_bs_del(bs);
	while (pos && (buf[pos-1]==0)) {
		pos--;
	}
	if (!pos) {
		strcat(szCodec, ".CA");
		return GF_OK;
	}
	strcat(szCodec, ".C");
	len = (u32) strlen(szCodec);
	bs = gf_bs_new(buf, pos, GF_BITSTREAM_READ);
	while (1) {
		u32 nb_bits = (u32) ( 8*gf_bs_available(bs) + gf_bs_bits_available(bs) );
		if (!nb_bits) break;
		if (nb_bits>5) nb_bits = 5;
		u32 c = gf_bs_read_int(bs, nb_bits);
		while (nb_bits<5) {
			c <<= 1;
			nb_bits++;
		}
		char b32_char = base32_chars[c];

		if (len<=RFC6381_CODEC_NAME_SIZE_MAX-2) {
			szCodec[len] = b32_char;
			len++;
			szCodec[len] = 0;
		}
		else {
			szCodec[RFC6381_CODEC_NAME_SIZE_MAX-1] = 0;
			GF_LOG(GF_LOG_ERROR, GF_LOG_MEDIA, ("[RFC6381] truncated codec string - current value: %s - trying to write: %c\n", szCodec, b32_char));
			gf_bs_del(bs);
			return GF_ISOM_INVALID_FILE;
		}
	}
	gf_bs_del(bs);

	return GF_OK;
}
GF_Err rfc_6381_get_codec_mpegha(char *szCodec, u32 subtype, u8 *dsi, u32 dsi_size, s32 pl)
{
	if (dsi && (dsi_size>=2) ) {
		pl = dsi[1];
	}
	if (pl<0) {
		GF_LOG(GF_LOG_WARNING, GF_LOG_MEDIA, ("[RFC6381] Cannot find MPEG-H Audio Config or audio PL, defaulting to profile 0x01\n"));
	}
	snprintf(szCodec, RFC6381_CODEC_NAME_SIZE_MAX, "%s.0x%02X", gf_4cc_to_str(subtype), pl);
	return GF_OK;
}

GF_Err rfc_6381_get_codec_uncv(char *szCodec, u32 subtype, u8 *dsi, u32 dsi_size);

// Selected (namespace,identifier) pairs from https://www.w3.org/TR/ttml-profile-registry/
// ordered in decreasing order of preference
static const char *ttml_namespaces[] = {
	"im3t", "http://www.w3.org/ns/ttml/profile/imsc1.2/text",
	"im2t", "http://www.w3.org/ns/ttml/profile/imsc1.1/text",
	"im2i", "http://www.w3.org/ns/ttml/profile/imsc1.1/image",
	"im1t", "http://www.w3.org/ns/ttml/profile/imsc1/text",
	"im1i", "http://www.w3.org/ns/ttml/profile/imsc1/image"
};
static const int TTML_NAMESPACES_COUNT = GF_ARRAY_LENGTH(ttml_namespaces);


GF_Err rfc_6381_get_codec_stpp(char *szCodec, u32 subtype,
                               const char *xmlnamespace,
                               const char *xml_schema_loc,
                               const char *mimes)
{
    // we ignore schema location and auxiliary mime types
    // we focus on the provided list of namespaces
    if (xmlnamespace != NULL) {
        int i;
        for (i = 0; i < TTML_NAMESPACES_COUNT; i+=2) {
            if(strstr(xmlnamespace, ttml_namespaces[i+1]) != NULL) {
                snprintf(szCodec, RFC6381_CODEC_NAME_SIZE_MAX, "%s.%s.%s", gf_4cc_to_str(subtype), "ttml", ttml_namespaces[i]);
                return GF_OK;
            }
        }
        // if none of the namespaces above have been found, search the default TTML namespace
        if(strstr(xmlnamespace, "http://www.w3.org/ns/ttml")) {
            snprintf(szCodec, RFC6381_CODEC_NAME_SIZE_MAX, "%s.%s", gf_4cc_to_str(subtype), "ttml");
            return GF_OK;
        }
    }
    // None of the known namespaces are found, default
    snprintf(szCodec, RFC6381_CODEC_NAME_SIZE_MAX, "%s", gf_4cc_to_str(subtype));
    return GF_OK;
}

GF_Err rfc6381_codec_name_default(char *szCodec, u32 subtype, u32 codec_id)
{
	if (codec_id && (codec_id<GF_CODECID_LAST_MPEG4_MAPPING)) {
		u32 stype = gf_codecid_type(codec_id);
		if (stype==GF_STREAM_VISUAL)
			snprintf(szCodec, RFC6381_CODEC_NAME_SIZE_MAX, "mp4v.%02X", codec_id);
		else if (stype==GF_STREAM_AUDIO)
			snprintf(szCodec, RFC6381_CODEC_NAME_SIZE_MAX, "mp4a.%02X", codec_id);
		else
			snprintf(szCodec, RFC6381_CODEC_NAME_SIZE_MAX, "mp4s.%02X", codec_id);
	} else {
		GF_LOG(GF_LOG_DEBUG, GF_LOG_MEDIA, ("[RFC6381] Codec parameters not known - using default value \"%s\"\n", gf_4cc_to_str(subtype) ));
		snprintf(szCodec, RFC6381_CODEC_NAME_SIZE_MAX, "%s", gf_4cc_to_str(subtype));
	}
	return GF_OK;
}

#ifndef GPAC_DISABLE_ISOM

GF_EXPORT
GF_Err gf_media_get_rfc_6381_codec_name(GF_ISOFile *movie, u32 track, u32 stsd_idx, char *szCodec, Bool force_inband, Bool force_sbr)
{
	GF_ESD *esd;
	GF_Err e;
	GF_AVCConfig *avcc;
	GF_HEVCConfig *hvcc;
	u32 subtype = gf_isom_get_media_subtype(movie, track, stsd_idx);

	if (subtype == GF_ISOM_SUBTYPE_MPEG4_CRYP) {
		u32 originalFormat=0;
		if (gf_isom_is_ismacryp_media(movie, track, stsd_idx)) {
			e = gf_isom_get_ismacryp_info(movie, track, stsd_idx, &originalFormat, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
		} else if (gf_isom_is_omadrm_media(movie, track, stsd_idx)) {
			e = gf_isom_get_omadrm_info(movie, track, stsd_idx, &originalFormat, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
		} else if(gf_isom_is_cenc_media(movie, track, stsd_idx)) {
			e = gf_isom_get_cenc_info(movie, track, stsd_idx, &originalFormat, NULL, NULL);
		} else {
			GF_LOG(GF_LOG_WARNING, GF_LOG_CONTAINER, ("[RFC6381] Unknown protection scheme type %s\n", gf_4cc_to_str( gf_isom_is_media_encrypted(movie, track, stsd_idx)) ));
			e = gf_isom_get_original_format_type(movie, track, stsd_idx, &originalFormat);
		}
		if (e) {
			GF_LOG(GF_LOG_ERROR, GF_LOG_CONTAINER, ("[RFC6381] Error fetching protection information\n"));
			return e;
		}

		if (originalFormat) subtype = originalFormat;
	}
	else if (subtype==GF_ISOM_SUBTYPE_MPEG4) {
		u32 stsd_type = gf_isom_get_mpeg4_subtype(movie, track, stsd_idx);
		if (stsd_type==GF_ISOM_SUBTYPE_RESV)
			gf_isom_get_original_format_type(movie, track, stsd_idx, &subtype);
	}

	switch (subtype) {
	case GF_ISOM_SUBTYPE_MPEG4:
		esd = gf_isom_get_esd(movie, track, stsd_idx);
		if (esd && esd->decoderConfig) {
			switch (esd->decoderConfig->streamType) {
			case GF_STREAM_AUDIO:
				if (esd->decoderConfig->decoderSpecificInfo && esd->decoderConfig->decoderSpecificInfo->data) {
					e = rfc_6381_get_codec_aac(szCodec, esd->decoderConfig->objectTypeIndication, esd->decoderConfig->decoderSpecificInfo->data, esd->decoderConfig->decoderSpecificInfo->dataLength, force_sbr);
				} else {
					e = rfc_6381_get_codec_aac(szCodec, esd->decoderConfig->objectTypeIndication, NULL, 0, force_sbr);
				}
				break;
			case GF_STREAM_VISUAL:
				if (esd->decoderConfig->decoderSpecificInfo && esd->decoderConfig->decoderSpecificInfo->data) {
					e = rfc_6381_get_codec_m4v(szCodec, esd->decoderConfig->objectTypeIndication, esd->decoderConfig->decoderSpecificInfo->data, esd->decoderConfig->decoderSpecificInfo->dataLength);
				} else {
					e = rfc_6381_get_codec_m4v(szCodec, esd->decoderConfig->objectTypeIndication, NULL, 0);
				}
				break;
			default:
				snprintf(szCodec, RFC6381_CODEC_NAME_SIZE_MAX, "mp4s.%02X", esd->decoderConfig->objectTypeIndication);
				e = GF_OK;
				break;
			}
			gf_odf_desc_del((GF_Descriptor *)esd);
			return e;
		}
		GF_LOG(GF_LOG_ERROR, GF_LOG_CONTAINER, ("[RFC6381] Cannot find ESD. Aborting.\n"));
		if (esd) gf_odf_desc_del((GF_Descriptor *)esd);
		return GF_ISOM_INVALID_FILE;

	case GF_ISOM_SUBTYPE_AVC_H264:
	case GF_ISOM_SUBTYPE_AVC2_H264:
	case GF_ISOM_SUBTYPE_AVC3_H264:
	case GF_ISOM_SUBTYPE_AVC4_H264:
		avcc = gf_isom_avc_config_get(movie, track, stsd_idx);
		if (force_inband) {
			if (subtype==GF_ISOM_SUBTYPE_AVC_H264)
				subtype = GF_ISOM_SUBTYPE_AVC3_H264;
			else if (subtype==GF_ISOM_SUBTYPE_AVC2_H264)
				subtype = GF_ISOM_SUBTYPE_AVC4_H264;
		}
		if (avcc) {
			e = rfc_6381_get_codec_avc(szCodec, subtype, avcc);
			gf_odf_avc_cfg_del(avcc);
			return e;
		}
		GF_LOG(GF_LOG_ERROR, GF_LOG_CONTAINER, ("[RFC6381] Cannot find AVC configuration box"));
		return GF_ISOM_INVALID_FILE;

	case GF_ISOM_SUBTYPE_SVC_H264:
	case GF_ISOM_SUBTYPE_MVC_H264:
		avcc = gf_isom_mvc_config_get(movie, track, stsd_idx);
		if (!avcc) avcc = gf_isom_svc_config_get(movie, track, stsd_idx);
		if (avcc) {
			e = rfc_6381_get_codec_avc(szCodec, subtype, avcc);
			gf_odf_avc_cfg_del(avcc);
			return e;
		}
		GF_LOG(GF_LOG_ERROR, GF_LOG_CONTAINER, ("[RFC6381] Cannot find SVC/MVC configuration box\n"));
		return GF_ISOM_INVALID_FILE;

	case GF_ISOM_SUBTYPE_HVC1:
	case GF_ISOM_SUBTYPE_HEV1:
	case GF_ISOM_SUBTYPE_HVC2:
	case GF_ISOM_SUBTYPE_HEV2:
	case GF_ISOM_SUBTYPE_HVT1:
	case GF_ISOM_SUBTYPE_LHV1:
	case GF_ISOM_SUBTYPE_LHE1:

		if (force_inband) {
			if (subtype==GF_ISOM_SUBTYPE_HVC1) subtype = GF_ISOM_SUBTYPE_HEV1;
			else if (subtype==GF_ISOM_SUBTYPE_HVC2) subtype = GF_ISOM_SUBTYPE_HEV2;
		}
		hvcc = gf_isom_hevc_config_get(movie, track, stsd_idx);
		if (!hvcc) {
			hvcc = gf_isom_lhvc_config_get(movie, track, stsd_idx);
		}
		if (subtype==GF_ISOM_SUBTYPE_HVT1) {
			u32 refTrack;
			gf_isom_get_reference(movie, track, GF_ISOM_REF_TBAS, stsd_idx, &refTrack);
			if (hvcc) gf_odf_hevc_cfg_del(hvcc);
			hvcc = gf_isom_hevc_config_get(movie, refTrack, 1);
		}
		if (hvcc) {
			e = rfc_6381_get_codec_hevc(szCodec, subtype, hvcc);
			gf_odf_hevc_cfg_del(hvcc);
			return e;
		}
		GF_LOG(GF_LOG_ERROR, GF_LOG_CONTAINER, ("[RFC6381] HEVCConfig not compliant\n"));
		return GF_ISOM_INVALID_FILE;

	case GF_ISOM_SUBTYPE_AV01:
	{
		GF_AV1Config *av1c = gf_isom_av1_config_get(movie, track, stsd_idx);
		if (av1c) {
			u32 colour_type;
			COLR colr;
			memset(&colr, 0, sizeof(colr));
			if (GF_OK == gf_isom_get_color_info(movie, track, stsd_idx, &colour_type, &colr.colour_primaries, &colr.transfer_characteristics, &colr.matrix_coefficients, &colr.full_range)) {
				colr.override = GF_TRUE;
			}
			e = rfc_6381_get_codec_av1(szCodec, subtype, av1c, colr);
			gf_odf_av1_cfg_del(av1c);
			return e;
		}
		GF_LOG(GF_LOG_DEBUG, GF_LOG_CONTAINER, ("[RFC6381] No config found for AV1 file (\"%s\") when computing RFC6381.\n", gf_4cc_to_str(subtype)));
		return GF_BAD_PARAM;
	}

	case GF_ISOM_SUBTYPE_VP08:
	case GF_ISOM_SUBTYPE_VP09:
	{
		GF_VPConfig *vpcc = gf_isom_vp_config_get(movie, track, stsd_idx);
		if (vpcc) {
			u32 colour_type;
			COLR colr;
			memset(&colr, 0, sizeof(colr));
			if (GF_OK == gf_isom_get_color_info(movie, track, stsd_idx, &colour_type, &colr.colour_primaries, &colr.transfer_characteristics, &colr.matrix_coefficients, &colr.full_range)) {
				colr.override = GF_TRUE;
			}
			e = rfc_6381_get_codec_vpx(szCodec, subtype, vpcc, colr);
			gf_odf_vp_cfg_del(vpcc);
			return e;
		}
		GF_LOG(GF_LOG_DEBUG, GF_LOG_CONTAINER, ("[RFC6381] No config found for VP file (\"%s\") when computing RFC6381.\n", gf_4cc_to_str(subtype)));
		return GF_BAD_PARAM;
	}

	case GF_ISOM_SUBTYPE_DVHE:
	case GF_ISOM_SUBTYPE_DVH1:
	case GF_ISOM_SUBTYPE_DVA1:
	case GF_ISOM_SUBTYPE_DVAV:
	case GF_ISOM_SUBTYPE_DAV1:
	{
		GF_DOVIDecoderConfigurationRecord *dovi = gf_isom_dovi_config_get(movie, track, stsd_idx);
		if (!dovi) {
			GF_LOG(GF_LOG_ERROR, GF_LOG_CONTAINER, ("[RFC6381] No config found for Dolby Vision file (\"%s\") when computing RFC6381.\n", gf_4cc_to_str(subtype)));
			return GF_NON_COMPLIANT_BITSTREAM;
		}

		e = rfc_6381_get_codec_dolby_vision(szCodec, subtype, dovi);
		gf_odf_dovi_cfg_del(dovi);
		return e;
	}

	case GF_ISOM_SUBTYPE_VVC1:
	case GF_ISOM_SUBTYPE_VVI1:
	{
		GF_VVCConfig *vvcc = gf_isom_vvc_config_get(movie, track, stsd_idx);
		if (vvcc) {
			if (force_inband) subtype = GF_ISOM_SUBTYPE_VVI1;

			e = rfc_6381_get_codec_vvc(szCodec, subtype, vvcc);
			gf_odf_vvc_cfg_del(vvcc);
			return e;
		}
		GF_LOG(GF_LOG_DEBUG, GF_LOG_CONTAINER, ("[RFC6381] No config found for VVC file (\"%s\") when computing RFC6381.\n", gf_4cc_to_str(subtype)));
		return GF_BAD_PARAM;
	}

	case GF_ISOM_SUBTYPE_MH3D_MHA1:
	case GF_ISOM_SUBTYPE_MH3D_MHA2:
	case GF_ISOM_SUBTYPE_MH3D_MHM1:
	case GF_ISOM_SUBTYPE_MH3D_MHM2:
	{
		esd = gf_media_map_esd(movie, track, stsd_idx);
		if (!esd || !esd->decoderConfig || !esd->decoderConfig->decoderSpecificInfo
			|| !esd->decoderConfig->decoderSpecificInfo->data
		) {
			e = rfc_6381_get_codec_mpegha(szCodec, subtype, NULL, 0, -1);
		} else {
			e = rfc_6381_get_codec_mpegha(szCodec, subtype, esd->decoderConfig->decoderSpecificInfo->data, esd->decoderConfig->decoderSpecificInfo->dataLength, -1);
		}
		if (esd) gf_odf_desc_del((GF_Descriptor *)esd);
		return e;
	}

	case GF_ISOM_SUBTYPE_UNCV:
	{
		GF_GenericSampleDescription *udesc = gf_isom_get_generic_sample_description(movie, track, stsd_idx);

		e = rfc_6381_get_codec_uncv(szCodec, subtype, udesc ? udesc->extension_buf : NULL, udesc ? udesc->extension_buf_size : 0);
		if (udesc) {
			if (udesc->extension_buf) gf_free(udesc->extension_buf);
			gf_free(udesc);
		}
		return e;
	}
    case GF_ISOM_SUBTYPE_STPP:
    {
        const char *xmlnamespace;
        const char *xml_schema_loc;
        const char *mimes;
        e = gf_isom_xml_subtitle_get_description(movie, track, stsd_idx,
                                             &xmlnamespace, &xml_schema_loc, &mimes);
        if (e == GF_OK) {
            rfc_6381_get_codec_stpp(szCodec, subtype, xmlnamespace, xml_schema_loc, mimes);
        }
        return e;
    }
	default:
		return rfc6381_codec_name_default(szCodec, subtype, gf_codec_id_from_isobmf(subtype));

	}
	return GF_OK;
}


GF_Err gf_media_av1_layer_size_get(GF_ISOFile *file, u32 trackNumber, u32 sample_number, u8 op_index, u32 layer_size[3])
{
#ifndef GPAC_DISABLE_AV_PARSERS
	u32 i;
	AV1State *av1_state;
	ObuType obu_type;
	u64 obu_size = 0;
	u32 hdr_size;
	GF_BitStream *bs;
	u32 sdidx;
	GF_ISOSample *samp;
	GF_Err e = GF_OK;

	samp = gf_isom_get_sample(file, trackNumber, sample_number, &sdidx);
	if (!samp) return GF_BAD_PARAM;

	if (!sdidx) {
		gf_isom_sample_del(&samp);
		return GF_BAD_PARAM;
	}
	if (gf_isom_get_media_subtype(file, trackNumber, sdidx) != GF_ISOM_SUBTYPE_AV01) {
		gf_isom_sample_del(&samp);
		return GF_BAD_PARAM;
	}

	GF_SAFEALLOC(av1_state, AV1State);
	if (!av1_state) {
		gf_isom_sample_del(&samp);
		return GF_OUT_OF_MEM;
	}
	gf_av1_init_state(av1_state);
	av1_state->config = gf_isom_av1_config_get(file, trackNumber, sdidx);
	if (!av1_state->config) {
		gf_isom_sample_del(&samp);
		gf_free(av1_state);
		return GF_ISOM_INVALID_FILE;
	}

	for (i=0; i<gf_list_count(av1_state->config->obu_array); i++) {
		GF_AV1_OBUArrayEntry *obu = gf_list_get(av1_state->config->obu_array, i);
		bs = gf_bs_new(obu->obu, (u32) obu->obu_length, GF_BITSTREAM_READ);
		e = gf_av1_parse_obu(bs, &obu_type, &obu_size, &hdr_size, av1_state);
		gf_bs_del(bs);
		if (e) break;
	}

	if (!e) {
		bs = gf_bs_new(samp->data, samp->dataLength, GF_BITSTREAM_READ);
		while (gf_bs_available(bs)) {
			e = gf_av1_parse_obu(bs, &obu_type, &obu_size, &hdr_size, av1_state);
			if (e) break;
		}
		gf_bs_del(bs);
	}
	gf_isom_sample_del(&samp);

  if (op_index > av1_state->operating_points_count) {
    if (av1_state->config) gf_odf_av1_cfg_del(av1_state->config);
    gf_av1_reset_state(av1_state, GF_TRUE);
    gf_free(av1_state);
    return GF_BAD_PARAM;
  }

	for (i=0; i<3; i++) {
		if (av1_state->layer_size[i+1]==av1_state->layer_size[i]) {
			layer_size[i] = 0;
		} else {
			layer_size[i] = av1_state->layer_size[i];
		}
	}

	if (av1_state->config) gf_odf_av1_cfg_del(av1_state->config);
	gf_av1_reset_state(av1_state, GF_TRUE);
	gf_free(av1_state);

	return e;
#else
	return GF_NOT_SUPPORTED;
#endif
}

#ifndef GPAC_DISABLE_ISOM_WRITE

GF_EXPORT
GF_Err gf_media_isom_apply_qt_key(GF_ISOFile *movie, const char *name, const char *val)
{
	GF_Err e;
	u8 *data=NULL;
	GF_QT_UDTAKey key;
	if (!name) return GF_BAD_PARAM;

	memset(&key, 0, sizeof(GF_QT_UDTAKey));
	key.name = (char *) name;
	key.ns = GF_4CC('m','d','t','a');
	char *sep = strchr(name, '@');
	if (sep) {
		sep[0] = 0;
		key.name = sep+1;
		if (strlen(name)!=4) {
			GF_LOG(GF_LOG_WARNING, GF_LOG_CONTAINER, ("Unrecognize namespace \"%s\" for key %s\n", name, sep+1));
			return GF_BAD_PARAM;
		}
		key.ns = GF_4CC(name[0], name[1],name[2],name[3]);
		sep[0] = '@';
	}

	if (!val || !val[0]) {
		key.type = GF_QT_KEY_REMOVE;
	} else if (!strcmp(val, "NULL")) {
		key.type = GF_QT_KEY_REMOVE;
	} else {
		key.type = GF_QT_KEY_UTF8;
		key.value.string = val;

		if (gf_file_exists(val)) {
			e = gf_file_load_data(val, &data, &key.value.data.data_len);
			if (e!=GF_OK) {
				GF_LOG(GF_LOG_WARNING, GF_LOG_CONTAINER, ("Failed to load file \"%s\" for key %s: %s\n", val, name, gf_error_to_string(e)));
				return e;
			}
			key.value.data.data = data;
			key.type = GF_QT_KEY_OPAQUE;
			if (strstr(val, ".bmp")|| strstr(val, ".BMP")) {
				key.type = GF_QT_KEY_BMP;
			} else if (key.value.data.data_len>3) {
				if ((data[0]==0xFF) && (data[1]==0xD8) && (data[2]==0xFF)) {
					key.type = GF_QT_KEY_JPEG;
				} else if ((data[0]==0x89) && (data[1]==0x50) && (data[2]==0x4E)) {
					key.type = GF_QT_KEY_PNG;
				}
			}
		} else {
			char *force_str=NULL;
			if (val[0] == 'S') {
				force_str = (char*)val+1;
				val++;
			}
			char *rect_sep = strchr(val, '@');
			char *pos_sep = strchr(val, 'x');
			char *arr_sep = strchr(val, ',');
			if (rect_sep && pos_sep && (sscanf(val, "%gx%g@%gx%g", &key.value.rect.x, &key.value.rect.y, &key.value.rect.w, &key.value.rect.h) == 4)) {
				key.type = GF_QT_KEY_RECTF;
			} else if (rect_sep && (sscanf(val, "%g@%g", &key.value.pos_size.x, &key.value.pos_size.y)==2)) {
				key.type = GF_QT_KEY_SIZEF;
			} else if (pos_sep && (sscanf(val, "%gx%g", &key.value.pos_size.x, &key.value.pos_size.y)==2)) {
				key.type = GF_QT_KEY_POINTF;
			} else if (arr_sep && strchr(arr_sep, ',')
				&& (sscanf(val, "%lg,%lg,%lg,%lg,%lg,%lg,%lg,%lg,%lg", &key.value.matrix[0], &key.value.matrix[1], &key.value.matrix[2]
				, &key.value.matrix[3], &key.value.matrix[4], &key.value.matrix[5]
				, &key.value.matrix[6], &key.value.matrix[7], &key.value.matrix[8]
				)==9)
			) {
				key.type = GF_QT_KEY_MATRIXF;
			} else {
				Bool force_flt=GF_FALSE,force_dbl=GF_FALSE,force_sign=GF_FALSE;
				u32 force_size=0;
				while (1) {
					char c = val[0];
					if (c=='f') force_flt = GF_TRUE;
					else if (c=='d') force_dbl = GF_TRUE;
					else if (c=='+') force_sign = GF_TRUE;
					else if (c=='b') force_size = 1;
					else if (c=='s') force_size = 2;
					else if (c=='l') force_size = 3;
					else if (c=='L') force_size = 4;
					else
						break;
					val++;
				}
				if (arr_sep || strchr(val, '.') || force_dbl || force_flt) {
					Double res;
					if (sscanf(val, "%lg", &res)==1) {
						key.value.number = res;
						key.type = force_flt ? GF_QT_KEY_FLOAT : GF_QT_KEY_DOUBLE;
					}
				}
				else {
					s64 res;
					if (sscanf(val, LLD, &res)==1) {
						key.value.sint = res;
						if (!force_sign && (key.value.sint>0)) {
							key.value.uint = key.value.sint;
							if (force_size==1) key.type = GF_QT_KEY_UNSIGNED_8;
							else if (force_size==2) key.type = GF_QT_KEY_UNSIGNED_16;
							else if (force_size==3) key.type = GF_QT_KEY_UNSIGNED_32;
							else if (force_size==4) key.type = GF_QT_KEY_UNSIGNED_64;
							else key.type = GF_QT_KEY_UNSIGNED_VSIZE;

						} else {
							if (force_size==1) key.type = GF_QT_KEY_SIGNED_8;
							else if (force_size==2) key.type = GF_QT_KEY_SIGNED_16;
							else if (force_size==3) key.type = GF_QT_KEY_SIGNED_32;
							else if (force_size==4) key.type = GF_QT_KEY_SIGNED_64;
							else key.type = GF_QT_KEY_SIGNED_VSIZE;
						}
					}
				}
			}
			if (force_str && (key.type != GF_QT_KEY_UTF8)) {
				key.type = GF_QT_KEY_UTF8;
				key.value.string = force_str;
			}
		}
	}

	e = gf_isom_set_qt_key(movie, &key);
	if (data) gf_free(data);

	if (e) {
		GF_LOG(GF_LOG_ERROR, GF_LOG_CONTAINER, ("Failed to add key %s: %s\n", name, gf_error_to_string(e) ));
	}
	return e;
}
#endif // GPAC_DISABLE_ISOM_WRITE


#endif //GPAC_DISABLE_ISOM
