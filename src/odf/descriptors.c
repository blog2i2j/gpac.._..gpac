/*
 *			GPAC - Multimedia Framework C SDK
 *
 *			Authors: Jean Le Feuvre
 *			Copyright (c) Telecom ParisTech 2000-2024
 *					All rights reserved
 *
 *  This file is part of GPAC / MPEG-4 ObjectDescriptor sub-project
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

#include <gpac/internal/odf_dev.h>
#include <gpac/constants.h>

#include <gpac/avparse.h>
#include <gpac/internal/media_dev.h>

s32 gf_odf_size_field_size(u32 size_desc)
{
	if (size_desc < 0x00000080) {
		return 1 + 1;
	} else if (size_desc < 0x00004000) {
		return 2 + 1;
	} else if (size_desc < 0x00200000) {
		return 3 + 1;
	} else if (size_desc < 0x10000000) {
		return 4 + 1;
	} else {
		return -1;
	}

}


GF_EXPORT
GF_Err gf_odf_parse_descriptor(GF_BitStream *bs, GF_Descriptor **desc, u32 *desc_size)
{
	u32 val, size, sizeHeader;
	u8 tag;
	GF_Err err;
	GF_Descriptor *newDesc;
	if (!bs) return GF_BAD_PARAM;

	*desc_size = 0;

	//tag
	tag = (u8) gf_bs_read_int(bs, 8);
	sizeHeader = 1;

	//size
	size = 0;
	do {
		val = gf_bs_read_int(bs, 8);
		sizeHeader++;
		if (sizeHeader > 5) {
			GF_LOG(GF_LOG_ERROR, GF_LOG_CODEC, ("[ODF] Descriptor size on more than 4 bytes\n"));
			return GF_ODF_INVALID_DESCRIPTOR;
		}
		size <<= 7;
		size |= val & 0x7F;
	} while ( val & 0x80);
	*desc_size = size;

	if (gf_bs_available(bs) < size) {
		GF_LOG(GF_LOG_ERROR, GF_LOG_CODEC, ("[ODF] Not enough bytes (%d) to read descriptor (size=%d)\n", gf_bs_available(bs), size));
		return GF_ODF_INVALID_DESCRIPTOR;
	}

	GF_LOG(GF_LOG_DEBUG, GF_LOG_CODEC, ("[ODF] Reading descriptor (tag %d size %d)\n", tag, size ));

	newDesc = gf_odf_create_descriptor(tag);
	if (! newDesc) {
		*desc = NULL;
		*desc_size = sizeHeader;
		if ( (tag >= GF_ODF_ISO_RES_BEGIN_TAG) &&
		        (tag <= GF_ODF_ISO_RES_END_TAG) ) {
			return GF_ODF_FORBIDDEN_DESCRIPTOR;
		}
		else if (!tag || (tag == 0xFF)) {
			return GF_ODF_INVALID_DESCRIPTOR;
		}
#ifndef GPAC_MINIMAL_ODF
		return GF_OUT_OF_MEM;
#else
		gf_bs_skip_bytes(bs, size);
		*desc_size = size + sizeHeader - gf_odf_size_field_size(*desc_size);
		return GF_OK;
#endif
	}

	newDesc->tag = tag;
	err = gf_odf_read_descriptor(bs, newDesc, *desc_size);

	/*FFmpeg fix*/
	if ((tag==GF_ODF_SLC_TAG) && (((GF_SLConfig*)newDesc)->predefined==2)) {
		if (*desc_size==3) {
			*desc_size = 1;
			err = GF_OK;
		}
	}

	//little trick to handle lazy bitstreams that encode
	//SizeOfInstance on a fix number of bytes
	//This nb of bytes is added in Read methods
	*desc_size += sizeHeader - gf_odf_size_field_size(*desc_size);
	*desc = newDesc;
	if (err) {
		GF_LOG(GF_LOG_ERROR, GF_LOG_CODEC, ("[ODF] Error reading descriptor (tag %d size %d): %s\n", tag, size, gf_error_to_string(err) ));
		gf_odf_delete_descriptor(newDesc);
		*desc = NULL;
	}
	return err;
}



GF_Err gf_odf_delete_descriptor_list(GF_List *descList)
{
	GF_Err e;
	GF_Descriptor*tmp;
	u32 i;
	//no error if NULL chain...
	if (! descList) return GF_OK;
	i=0;
	while ((tmp = (GF_Descriptor*)gf_list_enum(descList, &i))) {
		e = gf_odf_delete_descriptor(tmp);
		if (e) return e;
	}
	gf_list_del(descList);
	return GF_OK;
}

GF_Err gf_odf_write_base_descriptor(GF_BitStream *bs, u8 tag, u32 size)
{
	u32 length;
	unsigned char vals[4];

	if (!tag ) return GF_BAD_PARAM;

	length = size;
	vals[3] = (unsigned char) (length & 0x7f);
	length >>= 7;
	vals[2] = (unsigned char) ((length & 0x7f) | 0x80);
	length >>= 7;
	vals[1] = (unsigned char) ((length & 0x7f) | 0x80);
	length >>= 7;
	vals[0] = (unsigned char) ((length & 0x7f) | 0x80);

	gf_bs_write_int(bs, tag, 8);
	if (size < 0x00000080) {
		gf_bs_write_int(bs, vals[3], 8);
	} else if (size < 0x00004000) {
		gf_bs_write_int(bs, vals[2], 8);
		gf_bs_write_int(bs, vals[3], 8);
	} else if (size < 0x00200000) {
		gf_bs_write_int(bs, vals[1], 8);
		gf_bs_write_int(bs, vals[2], 8);
		gf_bs_write_int(bs, vals[3], 8);
	} else if (size < 0x10000000) {
		gf_bs_write_int(bs, vals[0], 8);
		gf_bs_write_int(bs, vals[1], 8);
		gf_bs_write_int(bs, vals[2], 8);
		gf_bs_write_int(bs, vals[3], 8);
	} else {
		return GF_ODF_INVALID_DESCRIPTOR;
	}
	return GF_OK;
}


GF_Err gf_odf_size_descriptor_list(GF_List *descList, u32 *outSize)
{
	GF_Err e;
	u32 tmpSize, count, i;
	if (! descList) return GF_OK;

	count = gf_list_count(descList);
	for ( i = 0; i < count; i++ ) {
		GF_Descriptor *tmp = (GF_Descriptor*)gf_list_get(descList, i);
		if (tmp) {
			e = gf_odf_size_descriptor(tmp, &tmpSize);
			if (e) return e;
			if (tmpSize) *outSize += tmpSize + gf_odf_size_field_size(tmpSize);
		}
	}
	return GF_OK;
}

GF_Err gf_odf_write_descriptor_list(GF_BitStream *bs, GF_List *descList)
{
	GF_Err e;
	u32 count, i;

	if (! descList) return GF_OK;
	count = gf_list_count(descList);
	for ( i = 0; i < count; i++ ) {
		GF_Descriptor *tmp = (GF_Descriptor*)gf_list_get(descList, i);
		if (tmp) {
			e = gf_odf_write_descriptor(bs, tmp);
			if (e) return e;
		}
	}
	return GF_OK;
}

GF_Err gf_odf_write_descriptor_list_filter(GF_BitStream *bs, GF_List *descList, u8 only_tag)
{
	GF_Err e;
	u32 count, i;

	if (! descList) return GF_OK;
	count = gf_list_count(descList);
	for ( i = 0; i < count; i++ ) {
		GF_Descriptor *tmp = (GF_Descriptor*)gf_list_get(descList, i);
		if (tmp && (tmp->tag==only_tag) ) {
			e = gf_odf_write_descriptor(bs, tmp);
			if (e) return e;
		}
	}
	return GF_OK;
}

#ifndef GPAC_MINIMAL_ODF

u32 gf_ipmpx_array_size(GF_BitStream *bs, u32 *array_size)
{
	u32 val, size, io_size;

	io_size = size = 0;
	do {
		val = gf_bs_read_int(bs, 8);
		io_size ++;
		size <<= 7;
		size |= val & 0x7F;
	} while ( val & 0x80 );
	*array_size = size;
	return io_size;
}

void gf_ipmpx_write_array(GF_BitStream *bs, u8 *data, u32 data_len)
{
	u32 length;
	unsigned char vals[4];

	if (!data || !data_len) return;

	length = data_len;
	vals[3] = (unsigned char) (length & 0x7f);
	length >>= 7;
	vals[2] = (unsigned char) ((length & 0x7f) | 0x80);
	length >>= 7;
	vals[1] = (unsigned char) ((length & 0x7f) | 0x80);
	length >>= 7;
	vals[0] = (unsigned char) ((length & 0x7f) | 0x80);

	if (data_len < 0x00000080) {
		gf_bs_write_int(bs, vals[3], 8);
	} else if (data_len < 0x00004000) {
		gf_bs_write_int(bs, vals[2], 8);
		gf_bs_write_int(bs, vals[3], 8);
	} else if (data_len < 0x00200000) {
		gf_bs_write_int(bs, vals[1], 8);
		gf_bs_write_int(bs, vals[2], 8);
		gf_bs_write_int(bs, vals[3], 8);
	} else if (data_len < 0x10000000) {
		gf_bs_write_int(bs, vals[0], 8);
		gf_bs_write_int(bs, vals[1], 8);
		gf_bs_write_int(bs, vals[2], 8);
		gf_bs_write_int(bs, vals[3], 8);
	} else {
		return;
	}
	gf_bs_write_data(bs, data, data_len);
}


#endif /*GPAC_MINIMAL_ODF*/

/*special authoring functions*/
GF_EXPORT
GF_BIFSConfig *gf_odf_get_bifs_config(GF_DefaultDescriptor *dsi, u32 oti)
{
	Bool hasSize, cmd_stream;
	GF_BitStream *bs;
	GF_BIFSConfig *cfg;

	if (oti>=GF_CODECID_BIFS_EXTENDED) return NULL;

	if (!dsi || !dsi->data || !dsi->dataLength ) {
		/* Hack for T-DMB non compliant streams (OnTimeTek ?) */
		cfg = (GF_BIFSConfig *) gf_odf_desc_new(GF_ODF_BIFS_CFG_TAG);
		cfg->pixelMetrics = GF_TRUE;
		cfg->version = 1;
		return cfg;
	}
	bs = gf_bs_new(dsi->data, dsi->dataLength, GF_BITSTREAM_READ);

	cfg = (GF_BIFSConfig *) gf_odf_desc_new(GF_ODF_BIFS_CFG_TAG);
	if (oti==2) {
		/*3D Mesh Coding*/
		gf_bs_read_int(bs, 1);
		/*PMF*/
		gf_bs_read_int(bs, 1);
	}
	cfg->nodeIDbits = gf_bs_read_int(bs, 5);
	cfg->routeIDbits = gf_bs_read_int(bs, 5);
	if (oti==2) cfg->protoIDbits = gf_bs_read_int(bs, 5);

	cmd_stream = (Bool)gf_bs_read_int(bs, 1);
	if (!cmd_stream) {
		cfg->elementaryMasks = gf_list_new();
		while (1) {
			GF_ElementaryMask* em = (GF_ElementaryMask* ) gf_odf_New_ElemMask();
			em->node_id = gf_bs_read_int(bs, cfg->nodeIDbits);
			gf_list_add(cfg->elementaryMasks, em);
			/*this assumes only FDP, BDP and IFS2D (no elem mask)*/
			if (gf_bs_read_int(bs, 1) == 0) break;
		}
		gf_bs_align(bs);
		if (gf_bs_get_size(bs) != gf_bs_get_position(bs)) {
			GF_LOG(GF_LOG_WARNING, GF_LOG_CODEC, ("[ODF] Reading bifs config: shift in sizes (not supported)\n"));
		}
	} else {
		cfg->pixelMetrics = (Bool)gf_bs_read_int(bs, 1);
		hasSize = (Bool)gf_bs_read_int(bs, 1);
		if (hasSize) {
			cfg->pixelWidth = gf_bs_read_int(bs, 16);
			cfg->pixelHeight = gf_bs_read_int(bs, 16);
		}
		gf_bs_align(bs);
		if (gf_bs_get_size(bs) != gf_bs_get_position(bs))
			GF_LOG(GF_LOG_WARNING, GF_LOG_CODEC, ("[ODF] Reading bifs config: shift in sizes (invalid descriptor)\n"));
	}
	gf_bs_del(bs);
	return cfg;
}

/*special function for authoring - convert DSI to LASERConfig*/
GF_EXPORT
GF_Err gf_odf_get_laser_config(GF_DefaultDescriptor *dsi, GF_LASERConfig *cfg)
{
	u32 to_skip;
	GF_BitStream *bs;

	if (!cfg) return GF_BAD_PARAM;
	memset(cfg, 0, sizeof(GF_LASERConfig));

	if (!dsi || !dsi->data || !dsi->dataLength) return GF_BAD_PARAM;
	bs = gf_bs_new(dsi->data, dsi->dataLength, GF_BITSTREAM_READ);
	memset(cfg, 0, sizeof(GF_LASERConfig));
	cfg->tag = GF_ODF_LASER_CFG_TAG;
	cfg->profile = gf_bs_read_int(bs, 8);
	cfg->level = gf_bs_read_int(bs, 8);
	/*cfg->reserved = */gf_bs_read_int(bs, 3);
	cfg->pointsCodec = gf_bs_read_int(bs, 2);
	cfg->pathComponents = gf_bs_read_int(bs, 4);
	cfg->fullRequestHost = gf_bs_read_int(bs, 1);
	if (gf_bs_read_int(bs, 1)) cfg->time_resolution = gf_bs_read_int(bs, 16);
	else cfg->time_resolution = 1000;
	cfg->colorComponentBits = 1 + gf_bs_read_int(bs, 4);
	cfg->resolution = gf_bs_read_int(bs, 4);
	if (cfg->resolution>7) cfg->resolution -= 16;
	cfg->coord_bits = gf_bs_read_int(bs, 5);
	cfg->scale_bits_minus_coord_bits = gf_bs_read_int(bs, 4);
	cfg->newSceneIndicator = gf_bs_read_int(bs, 1);
	/*reserved2*/ gf_bs_read_int(bs, 3);
	cfg->extensionIDBits = gf_bs_read_int(bs, 4);
	/*hasExtConfig - we just ignore it*/
	if (gf_bs_read_int(bs, 1)) {
		to_skip = gf_bs_read_vluimsbf5(bs);
		while (to_skip) {
			gf_bs_read_int(bs, 8);
			to_skip--;
		}
	}
	/*hasExtension - we just ignore it*/
	if (gf_bs_read_int(bs, 1)) {
		to_skip = gf_bs_read_vluimsbf5(bs);
		while (to_skip) {
			gf_bs_read_int(bs, 8);
			to_skip--;
		}
	}
	gf_bs_del(bs);
	return GF_OK;
}
//unused
#if 0
GF_Err gf_odf_get_ui_config(GF_DefaultDescriptor *dsi, GF_UIConfig *cfg)
{
	u32 len, i;
	GF_BitStream *bs;
	if (!dsi || !dsi->data || !dsi->dataLength || !cfg) return GF_BAD_PARAM;
	memset(cfg, 0, sizeof(GF_UIConfig));
	cfg->tag = GF_ODF_UI_CFG_TAG;
	bs = gf_bs_new(dsi->data, dsi->dataLength, GF_BITSTREAM_READ);
	len = gf_bs_read_int(bs, 8);
	cfg->deviceName = (char*)gf_malloc(sizeof(char) * (len+1));
	for (i=0; i<len; i++) cfg->deviceName[i] = gf_bs_read_int(bs, 8);
	cfg->deviceName[i] = 0;

	if (!stricmp(cfg->deviceName, "StringSensor") && gf_bs_available(bs)) {
		cfg->termChar = gf_bs_read_int(bs, 8);
		cfg->delChar = gf_bs_read_int(bs, 8);
	}
	gf_bs_del(bs);
	return GF_OK;
}
#endif

GF_EXPORT
GF_Err gf_odf_encode_ui_config(GF_UIConfig *cfg, GF_DefaultDescriptor **out_dsi)
{
	u32 i, len;
	GF_BitStream *bs;
	GF_DefaultDescriptor *dsi;
	if (!out_dsi || (cfg->tag != GF_ODF_UI_CFG_TAG)) return GF_BAD_PARAM;

	*out_dsi = NULL;
	if (!cfg->deviceName) return GF_OK;

	bs = gf_bs_new(NULL, 0, GF_BITSTREAM_WRITE);
	len = (u32) strlen(cfg->deviceName);
	gf_bs_write_int(bs, len, 8);
	for (i=0; i<len; i++) gf_bs_write_int(bs, cfg->deviceName[i], 8);
	if (!stricmp(cfg->deviceName, "StringSensor")) {
		/*fixme - this should be UTF-8 chars*/
		if (cfg->delChar || cfg->termChar) {
			gf_bs_write_int(bs, cfg->termChar, 8);
			gf_bs_write_int(bs, cfg->delChar, 8);
		}
	}
	if (cfg->ui_data) gf_bs_write_data(bs, cfg->ui_data, cfg->ui_data_length);

	dsi = (GF_DefaultDescriptor *) gf_odf_desc_new(GF_ODF_DSI_TAG);
	gf_bs_get_content(bs, &dsi->data, &dsi->dataLength);
	gf_bs_del(bs);
	*out_dsi = dsi;
	return GF_OK;
}


GF_EXPORT
GF_AVCConfig *gf_odf_avc_cfg_new()
{
	GF_AVCConfig *cfg;
	GF_SAFEALLOC(cfg, GF_AVCConfig);
	if (!cfg) return NULL;
	cfg->sequenceParameterSets = gf_list_new();
	cfg->pictureParameterSets = gf_list_new();
	cfg->AVCLevelIndication = 1;
	cfg->chroma_format = 1;
	cfg->chroma_bit_depth = 8;
	cfg->luma_bit_depth = 8;
	return cfg;
}

GF_EXPORT
void gf_odf_avc_cfg_del(GF_AVCConfig *cfg)
{
	if (!cfg) return;
	while (gf_list_count(cfg->sequenceParameterSets)) {
		GF_NALUFFParam *sl = (GF_NALUFFParam *)gf_list_get(cfg->sequenceParameterSets, 0);
		gf_list_rem(cfg->sequenceParameterSets, 0);
		if (sl->data) gf_free(sl->data);
		gf_free(sl);
	}
	gf_list_del(cfg->sequenceParameterSets);
	cfg->sequenceParameterSets = NULL;

	while (gf_list_count(cfg->pictureParameterSets)) {
		GF_NALUFFParam *sl = (GF_NALUFFParam *)gf_list_get(cfg->pictureParameterSets, 0);
		gf_list_rem(cfg->pictureParameterSets, 0);
		if (sl->data) gf_free(sl->data);
		gf_free(sl);
	}
	gf_list_del(cfg->pictureParameterSets);
	cfg->pictureParameterSets = NULL;

	if (cfg->sequenceParameterSetExtensions) {
		while (gf_list_count(cfg->sequenceParameterSetExtensions)) {
			GF_NALUFFParam *sl = (GF_NALUFFParam *)gf_list_get(cfg->sequenceParameterSetExtensions, 0);
			gf_list_rem(cfg->sequenceParameterSetExtensions, 0);
			if (sl->data) gf_free(sl->data);
			gf_free(sl);
		}
		gf_list_del(cfg->sequenceParameterSetExtensions);
		cfg->sequenceParameterSetExtensions = NULL;
	}
	gf_free(cfg);
}

GF_EXPORT
GF_Err gf_odf_avc_cfg_write_bs(GF_AVCConfig *cfg, GF_BitStream *bs)
{
	u32 i, count;

	if (!cfg) return GF_BAD_PARAM;

	count = gf_list_count(cfg->sequenceParameterSets);

	if (!cfg->write_annex_b) {
		gf_bs_write_int(bs, cfg->configurationVersion, 8);
		gf_bs_write_int(bs, cfg->AVCProfileIndication , 8);
		gf_bs_write_int(bs, cfg->profile_compatibility, 8);
		gf_bs_write_int(bs, cfg->AVCLevelIndication, 8);
		gf_bs_write_int(bs, 0x3F, 6);
		gf_bs_write_int(bs, cfg->nal_unit_size - 1, 2);
		gf_bs_write_int(bs, 0x7, 3);
		gf_bs_write_int(bs, count, 5);
	}
	for (i=0; i<count; i++) {
		GF_NALUFFParam *sl = (GF_NALUFFParam *)gf_list_get(cfg->sequenceParameterSets, i);
		if (!cfg->write_annex_b) {
			gf_bs_write_u16(bs, sl->size);
		} else {
			gf_bs_write_u32(bs, 1);
		}
		gf_bs_write_data(bs, sl->data, sl->size);
	}
	count = gf_list_count(cfg->pictureParameterSets);
	if (!cfg->write_annex_b) {
		gf_bs_write_int(bs, count, 8);
	}
	for (i=0; i<count; i++) {
		GF_NALUFFParam *sl = (GF_NALUFFParam *)gf_list_get(cfg->pictureParameterSets, i);
		if (!cfg->write_annex_b) {
			gf_bs_write_u16(bs, sl->size);
		} else {
			gf_bs_write_u32(bs, 1);
		}
		gf_bs_write_data(bs, sl->data, sl->size);
	}
	if (gf_avcc_use_extensions(cfg->AVCProfileIndication)) {
		if (!cfg->write_annex_b) {
			gf_bs_write_int(bs, 0xFF, 6);
			gf_bs_write_int(bs, cfg->chroma_format, 2);
			gf_bs_write_int(bs, 0xFF, 5);
			gf_bs_write_int(bs, cfg->luma_bit_depth - 8, 3);
			gf_bs_write_int(bs, 0xFF, 5);
			gf_bs_write_int(bs, cfg->chroma_bit_depth - 8, 3);
		}
		count = cfg->sequenceParameterSetExtensions ? gf_list_count(cfg->sequenceParameterSetExtensions) : 0;
		if (!cfg->write_annex_b) {
			gf_bs_write_u8(bs, count);
		}
		for (i=0; i<count; i++) {
			GF_NALUFFParam *sl = (GF_NALUFFParam *) gf_list_get(cfg->sequenceParameterSetExtensions, i);
			if (!cfg->write_annex_b) {
				gf_bs_write_u16(bs, sl->size);
			} else {
				gf_bs_write_u32(bs, 1);
			}
			gf_bs_write_data(bs, sl->data, sl->size);
		}
	}
	return GF_OK;
}

GF_EXPORT
GF_Err gf_odf_avc_cfg_write(GF_AVCConfig *cfg, u8 **outData, u32 *outSize)
{
	GF_BitStream *bs = gf_bs_new(NULL, 0, GF_BITSTREAM_WRITE);
	gf_odf_avc_cfg_write_bs(cfg, bs);
	*outSize = 0;
	*outData = NULL;
	gf_bs_get_content(bs, outData, outSize);
	gf_bs_del(bs);
	return GF_OK;
}

GF_EXPORT
GF_AVCConfig *gf_odf_avc_cfg_read(u8 *dsi, u32 dsi_size)
{
	u32 i, count;
	GF_AVCConfig *avcc = gf_odf_avc_cfg_new();
	GF_BitStream *bs = gf_bs_new(dsi, dsi_size, GF_BITSTREAM_READ);
	avcc->configurationVersion = gf_bs_read_int(bs, 8);
	avcc->AVCProfileIndication  = gf_bs_read_int(bs, 8);
	avcc->profile_compatibility = gf_bs_read_int(bs, 8);
	avcc->AVCLevelIndication  = gf_bs_read_int(bs, 8);
	gf_bs_read_int(bs, 6);
	avcc->nal_unit_size = 1 + gf_bs_read_int(bs, 2);
	gf_bs_read_int(bs, 3);
	count = gf_bs_read_int(bs, 5);
	for (i=0; i<count; i++) {
		GF_NALUFFParam *sl;
		u32 size = gf_bs_read_int(bs, 16);
		if ((size>gf_bs_available(bs)) || (size<2)) {
			GF_LOG(GF_LOG_ERROR, GF_LOG_CODING, ("[AVC] Wrong param set size %d\n", size));
			gf_bs_del(bs);
			gf_odf_avc_cfg_del(avcc);
			return NULL;
		}
		GF_SAFEALLOC(sl, GF_NALUFFParam );
		if (!sl) {
			gf_bs_del(bs);
			gf_odf_avc_cfg_del(avcc);
			return NULL;
		}
		sl->size = size;
		sl->data = (char*)gf_malloc(sizeof(char)*sl->size);
		if (!sl->data) {
			gf_bs_del(bs);
			gf_odf_avc_cfg_del(avcc);
			return NULL;
		}
		gf_bs_read_data(bs, sl->data, sl->size);
		gf_list_add(avcc->sequenceParameterSets, sl);
	}
	count = gf_bs_read_int(bs, 8);
	for (i=0; i<count; i++) {
		GF_NALUFFParam *sl;
		u32 size = gf_bs_read_int(bs, 16);
		if ((size>gf_bs_available(bs)) || (size<2)) {
			GF_LOG(GF_LOG_ERROR, GF_LOG_CODING, ("[AVC] Wrong param set size %d\n", size));
			gf_bs_del(bs);
			gf_odf_avc_cfg_del(avcc);
			return NULL;
		}
		GF_SAFEALLOC(sl, GF_NALUFFParam );
		if (!sl) {
			gf_bs_del(bs);
			gf_odf_avc_cfg_del(avcc);
			return NULL;
		}
		sl->size = size;
		sl->data = (char*)gf_malloc(sizeof(char)*sl->size);
		if (!sl->data) {
			gf_bs_del(bs);
			gf_odf_avc_cfg_del(avcc);
			return NULL;
		}
		gf_bs_read_data(bs, sl->data, sl->size);
		gf_list_add(avcc->pictureParameterSets, sl);
	}
	if (gf_avcc_use_extensions(avcc->AVCProfileIndication)) {
		gf_bs_read_int(bs, 6);
		avcc->chroma_format = gf_bs_read_int(bs, 2);
		gf_bs_read_int(bs, 5);
		avcc->luma_bit_depth = 8 + gf_bs_read_int(bs, 3);
		gf_bs_read_int(bs, 5);
		avcc->chroma_bit_depth = 8 + gf_bs_read_int(bs, 3);

		count = gf_bs_read_int(bs, 8);
		if (count) {
			avcc->sequenceParameterSetExtensions = gf_list_new();
			for (i=0; i<count; i++) {
				GF_NALUFFParam *sl;
				u32 size = gf_bs_read_int(bs, 16);
				if ((size>gf_bs_available(bs)) || (size<2)) {
					GF_LOG(GF_LOG_ERROR, GF_LOG_CODING, ("[AVC] Wrong param set size %d\n", size));
					gf_bs_del(bs);
					gf_odf_avc_cfg_del(avcc);
					return NULL;
				}
				GF_SAFEALLOC(sl, GF_NALUFFParam );
				if (!sl) {
					gf_bs_del(bs);
					gf_odf_avc_cfg_del(avcc);
					return NULL;
				}
				sl->size = size;
				sl->data = (char*)gf_malloc(sizeof(char)*sl->size);
				if (!sl->data) {
					gf_bs_del(bs);
					gf_odf_avc_cfg_del(avcc);
					return NULL;
				}
				gf_bs_read_data(bs, sl->data, sl->size);
				gf_list_add(avcc->sequenceParameterSetExtensions, sl);
			}
		}
	}


	gf_bs_del(bs);
	return avcc;
}


GF_Descriptor *gf_odf_new_tx3g()
{
	GF_TextSampleDescriptor *newDesc = (GF_TextSampleDescriptor*) gf_malloc(sizeof(GF_TextSampleDescriptor));
	if (!newDesc) return NULL;
	memset(newDesc, 0, sizeof(GF_TextSampleDescriptor));
	newDesc->tag = GF_ODF_TX3G_TAG;
	return (GF_Descriptor *) newDesc;
}
GF_Err gf_odf_del_tx3g(GF_TextSampleDescriptor *sd)
{
	u32 i;
	for (i=0; i<sd->font_count; i++)
		if (sd->fonts[i].fontName) gf_free(sd->fonts[i].fontName);
	gf_free(sd->fonts);
	gf_free(sd);
	return GF_OK;
}

GF_EXPORT
GF_TextSampleDescriptor *gf_odf_tx3g_read(u8 *dsi, u32 dsi_size)
{
#ifndef GPAC_DISABLE_ISOM
	u32 i;
	u32 gpp_read_rgba(GF_BitStream *bs);
	void gpp_read_style(GF_BitStream *bs, GF_StyleRecord *rec);
	void gpp_read_box(GF_BitStream *bs, GF_BoxRecord *rec);

	GF_TextSampleDescriptor *txtc = (GF_TextSampleDescriptor *) gf_odf_new_tx3g();
	GF_BitStream *bs = gf_bs_new(dsi, dsi_size, GF_BITSTREAM_READ);

	txtc->displayFlags = gf_bs_read_int(bs, 32);
	txtc->horiz_justif = gf_bs_read_int(bs, 8);
	txtc->vert_justif  = gf_bs_read_int(bs, 8);
	txtc->back_color = gpp_read_rgba(bs);
	gpp_read_box(bs, &txtc->default_pos);
	gpp_read_style(bs, &txtc->default_style);
	txtc->font_count = gf_bs_read_u16(bs);
	txtc->fonts = gf_malloc(sizeof(GF_FontRecord)*txtc->font_count);
	for (i=0; i<txtc->font_count; i++) {
		u8 len;
		txtc->fonts[i].fontID = gf_bs_read_u16(bs);
		len = gf_bs_read_u8(bs);
		txtc->fonts[i].fontName = gf_malloc(sizeof(char)*(len+1));
		gf_bs_read_data(bs, txtc->fonts[i].fontName, len);
		txtc->fonts[i].fontName[len] = 0;
	}
	gf_bs_del(bs);
	return txtc;
#else
	return NULL;
#endif
}

GF_Err gf_odf_tx3g_write(GF_TextSampleDescriptor *a, u8 **outData, u32 *outSize)
{
#ifndef GPAC_DISABLE_ISOM
	u32 j;
	void gpp_write_rgba(GF_BitStream *bs, u32 col);
	void gpp_write_box(GF_BitStream *bs, GF_BoxRecord *rec);
	void gpp_write_style(GF_BitStream *bs, GF_StyleRecord *rec);
	GF_BitStream *bs = gf_bs_new(NULL, 0, GF_BITSTREAM_WRITE);

	gf_bs_write_u32(bs, a->displayFlags);
	gf_bs_write_u8(bs, a->horiz_justif);
	gf_bs_write_u8(bs, a->vert_justif);
	gpp_write_rgba(bs, a->back_color);
	gpp_write_box(bs, &a->default_pos);
	gpp_write_style(bs, &a->default_style);

	gf_bs_write_u16(bs, a->font_count);
	for (j=0; j<a->font_count; j++) {
		gf_bs_write_u16(bs, a->fonts[j].fontID);
		if (a->fonts[j].fontName) {
			u32 len = (u32) strlen(a->fonts[j].fontName);
			gf_bs_write_u8(bs, len);
			gf_bs_write_data(bs, a->fonts[j].fontName, len);
		} else {
			gf_bs_write_u8(bs, 0);
		}
	}
	gf_bs_get_content(bs, outData, outSize);
	gf_bs_del(bs);
	return GF_OK;
#else
	return GF_NOT_SUPPORTED;
#endif
}

/*TextConfig*/
GF_Descriptor *gf_odf_new_text_cfg()
{
	GF_TextConfig *newDesc = (GF_TextConfig*) gf_malloc(sizeof(GF_TextConfig));
	if (!newDesc) return NULL;
	memset(newDesc, 0, sizeof(GF_TextConfig));
	newDesc->tag = GF_ODF_TEXT_CFG_TAG;
	newDesc->sample_descriptions = gf_list_new();
	newDesc->Base3GPPFormat = 0x10;
	newDesc->MPEGExtendedFormat = 0x10;
	newDesc->profileLevel = 0x10;
	newDesc->timescale = 1000;
	return (GF_Descriptor *) newDesc;
}

void ResetTextConfig(GF_TextConfig *desc)
{
	GF_List *bck;
	while (gf_list_count(desc->sample_descriptions)) {
		GF_TextSampleDescriptor *sd = (GF_TextSampleDescriptor *)gf_list_get(desc->sample_descriptions, 0);
		gf_list_rem(desc->sample_descriptions, 0);
		gf_odf_del_tx3g(sd);
	}
	bck = desc->sample_descriptions;
	memset(desc, 0, sizeof(GF_TextConfig));
	desc->tag = GF_ODF_TEXT_CFG_TAG;
	desc->sample_descriptions = bck;
}

GF_Err gf_odf_del_text_cfg(GF_TextConfig *desc)
{
	ResetTextConfig(desc);
	gf_list_del(desc->sample_descriptions);
	gf_free(desc);
	return GF_OK;
}

/*we need box parsing*/
#include <gpac/internal/isomedia_dev.h>
GF_EXPORT
GF_Err gf_odf_get_text_config(u8 *data, u32 data_len, u32 codecid, GF_TextConfig *cfg)
{
	u32 i;
	Bool has_alt_format;
#ifndef GPAC_DISABLE_ISOM
	Bool has_sd;
	u32 j;
#endif
	GF_Err e;
	GF_BitStream *bs;
	if (data || data_len || !cfg) return GF_BAD_PARAM;
	if (codecid != GF_CODECID_TEXT_MPEG4) return GF_NOT_SUPPORTED;

	/*reset*/
	ResetTextConfig(cfg);
	bs = gf_bs_new(data, data_len, GF_BITSTREAM_READ);

	e = GF_OK;
	cfg->Base3GPPFormat = gf_bs_read_int(bs, 8);
	cfg->MPEGExtendedFormat = gf_bs_read_int(bs, 8);
	cfg->profileLevel = gf_bs_read_int(bs, 8);
	cfg->timescale = gf_bs_read_int(bs, 24);
	has_alt_format = (Bool)gf_bs_read_int(bs, 1);
	cfg->sampleDescriptionFlags = gf_bs_read_int(bs, 2);
#ifndef GPAC_DISABLE_ISOM
	has_sd = (Bool)gf_bs_read_int(bs, 1);
#else
	gf_bs_read_int(bs, 1);
#endif
	cfg->has_vid_info = (Bool)gf_bs_read_int(bs, 1);
	gf_bs_read_int(bs, 3);
	cfg->layer = gf_bs_read_int(bs, 8);
	cfg->text_width = gf_bs_read_int(bs, 16);
	cfg->text_height = gf_bs_read_int(bs, 16);
	if (has_alt_format) {
		cfg->nb_compatible_formats = gf_bs_read_int(bs, 8);
		for (i=0; i<cfg->nb_compatible_formats; i++) cfg->compatible_formats[i] = gf_bs_read_int(bs, 8);
	}
#ifndef GPAC_DISABLE_ISOM
	if (has_sd) {
		u8 sample_index;
		GF_TextSampleDescriptor *txdesc;
		GF_Tx3gSampleEntryBox *a;
		s64 avail;
		u32 nb_desc = gf_bs_read_int(bs, 8);

		/*parse TTU[5]s*/
		avail = (s64) gf_bs_available(bs);
		for (i=0; i<nb_desc; i++) {
			sample_index = gf_bs_read_int(bs, 8);
			avail -= 1;
			e = gf_isom_box_parse((GF_Box **) &a, bs);
			if (e) goto exit;
			avail -= (s32) a->size;

			if (avail<0) {
				e = GF_NON_COMPLIANT_BITSTREAM;
				goto exit;
			}
			txdesc = (GF_TextSampleDescriptor *)gf_malloc(sizeof(GF_TextSampleDescriptor));
			txdesc->sample_index = sample_index;
			txdesc->displayFlags = a->displayFlags;
			txdesc->back_color = a->back_color;
			txdesc->default_pos = a->default_box;
			txdesc->default_style = a->default_style;
			txdesc->vert_justif = a->vertical_justification;
			txdesc->horiz_justif = a->horizontal_justification;
			txdesc->font_count = a->font_table ? a->font_table->entry_count : 0;
			if (txdesc->font_count) {
				txdesc->fonts = (GF_FontRecord*)gf_malloc(sizeof(GF_FontRecord)*txdesc->font_count);
				for (j=0; j<txdesc->font_count; j++) {
					txdesc->fonts[j].fontID = a->font_table->fonts[j].fontID;
					txdesc->fonts[j].fontName = a->font_table->fonts[j].fontName ? gf_strdup(a->font_table->fonts[j].fontName) : NULL;
				}
			}
			gf_list_add(cfg->sample_descriptions, txdesc);
			gf_isom_box_del((GF_Box *)a);
		}
	}
#endif

	if (cfg->has_vid_info) {
		cfg->video_width = gf_bs_read_int(bs, 16);
		cfg->video_height = gf_bs_read_int(bs, 16);
		cfg->horiz_offset = gf_bs_read_int(bs, 16);
		cfg->vert_offset = gf_bs_read_int(bs, 16);
	}

#ifndef GPAC_DISABLE_ISOM
exit:
#endif
	gf_bs_del(bs);
	if (e) ResetTextConfig(cfg);
	return e;
}



GF_EXPORT
GF_HEVCConfig *gf_odf_hevc_cfg_new()
{
	GF_HEVCConfig *cfg;
	GF_SAFEALLOC(cfg, GF_HEVCConfig);
	if (!cfg) return NULL;
	cfg->param_array = gf_list_new();
	cfg->nal_unit_size = 4;
	return cfg;
}

GF_EXPORT
void gf_odf_hevc_cfg_del(GF_HEVCConfig *cfg)
{
	if (!cfg) return;
	while (gf_list_count(cfg->param_array)) {
		GF_NALUFFParamArray *pa = (GF_NALUFFParamArray*)gf_list_get(cfg->param_array, 0);
		gf_list_rem(cfg->param_array, 0);

		while (gf_list_count(pa->nalus)) {
			GF_NALUFFParam *n = (GF_NALUFFParam*)gf_list_get(pa->nalus, 0);
			gf_list_rem(pa->nalus, 0);
			if (n->data) gf_free(n->data);
			gf_free(n);
		}
		gf_list_del(pa->nalus);
		gf_free(pa);
	}
	gf_list_del(cfg->param_array);
	gf_free(cfg);
}

GF_EXPORT
GF_Err gf_odf_hevc_cfg_write_bs(GF_HEVCConfig *cfg, GF_BitStream *bs)
{
	u32 i, count;

	count = gf_list_count(cfg->param_array);

	if (!cfg->write_annex_b) {
		gf_bs_write_int(bs, cfg->configurationVersion, 8);

		if (!cfg->is_lhvc) {
			gf_bs_write_int(bs, cfg->profile_space, 2);
			gf_bs_write_int(bs, cfg->tier_flag, 1);
			gf_bs_write_int(bs, cfg->profile_idc, 5);
			gf_bs_write_int(bs, cfg->general_profile_compatibility_flags, 32);
			gf_bs_write_int(bs, cfg->progressive_source_flag, 1);
			gf_bs_write_int(bs, cfg->interlaced_source_flag, 1);
			gf_bs_write_int(bs, cfg->non_packed_constraint_flag, 1);
			gf_bs_write_int(bs, cfg->frame_only_constraint_flag, 1);
			/*only lowest 44 bits used*/
			gf_bs_write_long_int(bs, cfg->constraint_indicator_flags, 44);
			gf_bs_write_int(bs, cfg->level_idc, 8);
		}

		gf_bs_write_int(bs, 0xFF, 4);
		gf_bs_write_int(bs, cfg->min_spatial_segmentation_idc, 12);

		gf_bs_write_int(bs, 0xFF, 6);
		gf_bs_write_int(bs, cfg->parallelismType, 2);

		if (!cfg->is_lhvc) {
			gf_bs_write_int(bs, 0xFF, 6);
			gf_bs_write_int(bs, cfg->chromaFormat, 2);
			gf_bs_write_int(bs, 0xFF, 5);
			gf_bs_write_int(bs, cfg->luma_bit_depth-8, 3);
			gf_bs_write_int(bs, 0xFF, 5);
			gf_bs_write_int(bs, cfg->chroma_bit_depth-8, 3);
			gf_bs_write_int(bs, cfg->avgFrameRate, 16);

			gf_bs_write_int(bs, cfg->constantFrameRate, 2);
		} else {
			gf_bs_write_int(bs, 0xFF, 2);
		}

		gf_bs_write_int(bs, cfg->numTemporalLayers, 3);
		gf_bs_write_int(bs, cfg->temporalIdNested, 1);
		gf_bs_write_int(bs, cfg->nal_unit_size - 1, 2);

		gf_bs_write_int(bs, count, 8);
	}

	for (i=0; i<count; i++) {
		u32 nalucount, j;
		GF_NALUFFParamArray *ar = (GF_NALUFFParamArray*)gf_list_get(cfg->param_array, i);

		nalucount = gf_list_count(ar->nalus);
		if (!cfg->write_annex_b) {
			gf_bs_write_int(bs, ar->array_completeness, 1);
			gf_bs_write_int(bs, 0, 1);
			gf_bs_write_int(bs, ar->type, 6);
			gf_bs_write_int(bs, nalucount, 16);
		}

		for (j=0; j<nalucount; j++) {
			GF_NALUFFParam *sl = (GF_NALUFFParam *)gf_list_get(ar->nalus, j);
			if (!cfg->write_annex_b) {
				gf_bs_write_int(bs, sl->size, 16);
			} else {
				gf_bs_write_u32(bs, 1);
			}
			gf_bs_write_data(bs, sl->data, sl->size);
		}
	}
	return GF_OK;
}

GF_EXPORT
GF_Err gf_odf_hevc_cfg_write(GF_HEVCConfig *cfg, u8 **outData, u32 *outSize)
{
	GF_Err e;
	GF_BitStream *bs = gf_bs_new(NULL, 0, GF_BITSTREAM_WRITE);
	*outSize = 0;
	*outData = NULL;
	e = gf_odf_hevc_cfg_write_bs(cfg, bs);
	if (e==GF_OK)
		gf_bs_get_content(bs, outData, outSize);

	gf_bs_del(bs);
	return e;
}

GF_EXPORT
GF_HEVCConfig *gf_odf_hevc_cfg_read_bs(GF_BitStream *bs, Bool is_lhvc)
{
	u32 i, count;
	GF_HEVCConfig *cfg = gf_odf_hevc_cfg_new();

	cfg->is_lhvc = is_lhvc;

	cfg->configurationVersion = gf_bs_read_int(bs, 8);

	if (!is_lhvc) {
		cfg->profile_space = gf_bs_read_int(bs, 2);
		cfg->tier_flag = gf_bs_read_int(bs, 1);
		cfg->profile_idc = gf_bs_read_int(bs, 5);
		cfg->general_profile_compatibility_flags = gf_bs_read_int(bs, 32);

		cfg->progressive_source_flag = gf_bs_read_int(bs, 1);
		cfg->interlaced_source_flag = gf_bs_read_int(bs, 1);
		cfg->non_packed_constraint_flag = gf_bs_read_int(bs, 1);
		cfg->frame_only_constraint_flag = gf_bs_read_int(bs, 1);
		/*only lowest 44 bits used*/
		cfg->constraint_indicator_flags = gf_bs_read_long_int(bs, 44);
		cfg->level_idc = gf_bs_read_int(bs, 8);
	}

	gf_bs_read_int(bs, 4); //reserved
	cfg->min_spatial_segmentation_idc = gf_bs_read_int(bs, 12);

	gf_bs_read_int(bs, 6);//reserved
	cfg->parallelismType = gf_bs_read_int(bs, 2);

	if (!is_lhvc) {
		gf_bs_read_int(bs, 6);
		cfg->chromaFormat = gf_bs_read_int(bs, 2);
		gf_bs_read_int(bs, 5);
		cfg->luma_bit_depth = gf_bs_read_int(bs, 3) + 8;
		gf_bs_read_int(bs, 5);
		cfg->chroma_bit_depth = gf_bs_read_int(bs, 3) + 8;
		cfg->avgFrameRate = gf_bs_read_int(bs, 16);

		cfg->constantFrameRate = gf_bs_read_int(bs, 2);
	} else {
		gf_bs_read_int(bs, 2); //reserved
	}

	cfg->numTemporalLayers = gf_bs_read_int(bs, 3);
	cfg->temporalIdNested = gf_bs_read_int(bs, 1);

	cfg->nal_unit_size = 1 + gf_bs_read_int(bs, 2);

	count = gf_bs_read_int(bs, 8);
	for (i=0; i<count; i++) {
		u32 nalucount, j;
		GF_NALUFFParamArray *ar;
		GF_SAFEALLOC(ar, GF_NALUFFParamArray);
		if (!ar) {
			gf_odf_hevc_cfg_del(cfg);
			return NULL;
		}
		ar->nalus = gf_list_new();
		gf_list_add(cfg->param_array, ar);

		ar->array_completeness = gf_bs_read_int(bs, 1);
		gf_bs_read_int(bs, 1);
		ar->type = gf_bs_read_int(bs, 6);
		nalucount = gf_bs_read_int(bs, 16);
		for (j=0; j<nalucount; j++) {
			GF_NALUFFParam *sl;
			u32 size = gf_bs_read_int(bs, 16);
			if ((size>gf_bs_available(bs)) || (size<2)) {
				GF_LOG(GF_LOG_ERROR, GF_LOG_CODING, ("[HEVC] Wrong param set size %d\n", size));
				gf_odf_hevc_cfg_del(cfg);
				return NULL;
			}
			GF_SAFEALLOC(sl, GF_NALUFFParam );
			if (!sl) {
				gf_odf_hevc_cfg_del(cfg);
				return NULL;
			}

			sl->size = size;
			sl->data = (char *)gf_malloc(sizeof(char) * sl->size);
			gf_bs_read_data(bs, sl->data, sl->size);
			gf_list_add(ar->nalus, sl);
		}
	}
	return cfg;
}

GF_EXPORT
GF_HEVCConfig *gf_odf_hevc_cfg_read(u8 *dsi, u32 dsi_size, Bool is_lhvc)
{
	GF_BitStream *bs = gf_bs_new(dsi, dsi_size, GF_BITSTREAM_READ);
	GF_HEVCConfig *cfg = gf_odf_hevc_cfg_read_bs(bs, is_lhvc);
	gf_bs_del(bs);
	return cfg;
}

GF_EXPORT
GF_VVCConfig *gf_odf_vvc_cfg_new()
{
	GF_VVCConfig *cfg;
	GF_SAFEALLOC(cfg, GF_VVCConfig);
	if (!cfg) return NULL;
	cfg->param_array = gf_list_new();
	cfg->nal_unit_size = 4;
	cfg->chroma_format = 1;
	cfg->bit_depth = 8;
	return cfg;
}

GF_EXPORT
void gf_odf_vvc_cfg_del(GF_VVCConfig *cfg)
{
	if (!cfg) return;
	while (gf_list_count(cfg->param_array)) {
		GF_NALUFFParamArray *pa = (GF_NALUFFParamArray*)gf_list_get(cfg->param_array, 0);
		gf_list_rem(cfg->param_array, 0);

		while (gf_list_count(pa->nalus)) {
			GF_NALUFFParam *n = (GF_NALUFFParam*)gf_list_get(pa->nalus, 0);
			gf_list_rem(pa->nalus, 0);
			if (n->data) gf_free(n->data);
			gf_free(n);
		}
		gf_list_del(pa->nalus);
		gf_free(pa);
	}
	gf_list_del(cfg->param_array);
	if (cfg->general_constraint_info)
		gf_free(cfg->general_constraint_info);
	if (cfg->sub_profiles_idc)
		gf_free(cfg->sub_profiles_idc);
	gf_free(cfg);
}

GF_EXPORT
GF_Err gf_odf_vvc_cfg_write_bs(GF_VVCConfig *cfg, GF_BitStream *bs)
{
	u32 i, count;

	count = gf_list_count(cfg->param_array);

	if (!cfg->write_annex_b) {

		gf_bs_write_int(bs, 0xFF, 5);
		gf_bs_write_int(bs, cfg->nal_unit_size - 1, 2);
		gf_bs_write_int(bs, cfg->ptl_present, 1);

		if (cfg->ptl_present) {
			s32 idx;

			gf_bs_write_int(bs, cfg->ols_idx, 9);
			gf_bs_write_int(bs, cfg->numTemporalLayers, 3);
			gf_bs_write_int(bs, cfg->constantFrameRate, 2);
			gf_bs_write_int(bs, cfg->chroma_format, 2);
			gf_bs_write_int(bs, cfg->bit_depth - 8, 3);
			gf_bs_write_int(bs, 0xFF, 5);

			if (!cfg->general_constraint_info)
				cfg->num_constraint_info = 0;

			//write PTL
			gf_bs_write_int(bs, 0, 2);
			gf_bs_write_int(bs, cfg->num_constraint_info, 6);
			gf_bs_write_int(bs, cfg->general_profile_idc, 7);
			gf_bs_write_int(bs, cfg->general_tier_flag, 1);
			gf_bs_write_u8(bs, cfg->general_level_idc);
			gf_bs_write_int(bs, cfg->ptl_frame_only_constraint, 1);
			gf_bs_write_int(bs, cfg->ptl_multilayer_enabled, 1);

			if (cfg->num_constraint_info) {
				gf_bs_write_data(bs, cfg->general_constraint_info, cfg->num_constraint_info - 1);
				gf_bs_write_int(bs, cfg->general_constraint_info[cfg->num_constraint_info - 1], 6);
			} else {
				gf_bs_write_int(bs, 0, 6);
			}

			for (idx=cfg->numTemporalLayers-2; idx>=0; idx--) {
				u8 val = cfg->ptl_sublayer_present_mask & (1<<idx);
				gf_bs_write_int(bs, val ? 1 : 0, 1);
			}
			for (idx=cfg->numTemporalLayers; idx<=8 && cfg->numTemporalLayers>1; idx++) {
				gf_bs_write_int(bs, 0, 1);
			}
			for (idx=cfg->numTemporalLayers-2; idx>=0; idx--) {
				if (cfg->ptl_sublayer_present_mask & (1<<idx))
					gf_bs_write_u8(bs, cfg->sublayer_level_idc[idx]);
			}
			if (!cfg->sub_profiles_idc) cfg->num_sub_profiles = 0;
			gf_bs_write_u8(bs, cfg->num_sub_profiles);
			for (idx=0; idx<cfg->num_sub_profiles; idx++) {
				gf_bs_write_u32(bs, cfg->sub_profiles_idc[idx]);
			}
			//end PTL

			gf_bs_write_u16(bs, cfg->maxPictureWidth);
			gf_bs_write_u16(bs, cfg->maxPictureHeight);
			gf_bs_write_u16(bs, cfg->avgFrameRate);
		}
		gf_bs_write_int(bs, count, 8);
	}

	for (i=0; i<count; i++) {
		u32 nalucount, j;
		GF_NALUFFParamArray *ar = (GF_NALUFFParamArray*)gf_list_get(cfg->param_array, i);

		nalucount = gf_list_count(ar->nalus);
		if (!cfg->write_annex_b) {
			gf_bs_write_int(bs, ar->array_completeness, 1);
			gf_bs_write_int(bs, 0, 2);
			gf_bs_write_int(bs, ar->type, 5);

			if ((ar->type != GF_VVC_NALU_DEC_PARAM) && (ar->type != GF_VVC_NALU_OPI))
				gf_bs_write_int(bs, nalucount, 16);
			else
				nalucount = 1;
		}

		for (j=0; j<nalucount; j++) {
			GF_NALUFFParam *sl = (GF_NALUFFParam *)gf_list_get(ar->nalus, j);
			if (!sl)
				return GF_ISOM_INVALID_MEDIA;
			if (!cfg->write_annex_b) {
				gf_bs_write_int(bs, sl->size, 16);
			} else {
				gf_bs_write_u32(bs, 1);
			}
			gf_bs_write_data(bs, sl->data, sl->size);
		}
	}
	return GF_OK;
}

GF_EXPORT
GF_Err gf_odf_vvc_cfg_write(GF_VVCConfig *cfg, u8 **outData, u32 *outSize)
{
	GF_Err e;
	GF_BitStream *bs = gf_bs_new(NULL, 0, GF_BITSTREAM_WRITE);
	*outSize = 0;
	*outData = NULL;
	e = gf_odf_vvc_cfg_write_bs(cfg, bs);
	if (e==GF_OK)
		gf_bs_get_content(bs, outData, outSize);

	gf_bs_del(bs);
	return e;
}

GF_EXPORT
GF_VVCConfig *gf_odf_vvc_cfg_read_bs(GF_BitStream *bs)
{
	u32 i, count;
	GF_VVCConfig *cfg = gf_odf_vvc_cfg_new();

	gf_bs_read_int(bs, 5);
	cfg->nal_unit_size = 1 + gf_bs_read_int(bs, 2);
	cfg->ptl_present = gf_bs_read_int(bs, 1);

	if (cfg->ptl_present) {
		s32 j;

		cfg->ols_idx = gf_bs_read_int(bs, 9);
		cfg->numTemporalLayers = gf_bs_read_int(bs, 3);
		cfg->constantFrameRate = gf_bs_read_int(bs, 2);
		cfg->chroma_format = gf_bs_read_int(bs, 2);
		cfg->bit_depth = 8 + gf_bs_read_int(bs, 3);
		gf_bs_read_int(bs, 5);

		//parse PTL
		gf_bs_read_int(bs, 2);
		cfg->num_constraint_info = gf_bs_read_int(bs, 6);
		cfg->general_profile_idc = gf_bs_read_int(bs, 7);
		cfg->general_tier_flag = gf_bs_read_int(bs, 1);
		cfg->general_level_idc = gf_bs_read_u8(bs);
		cfg->ptl_frame_only_constraint = gf_bs_read_int(bs, 1);
		cfg->ptl_multilayer_enabled = gf_bs_read_int(bs, 1);

		if (cfg->num_constraint_info) {
			cfg->general_constraint_info = gf_malloc(sizeof(u8)*cfg->num_constraint_info);
			if (!cfg->general_constraint_info) {
				gf_free(cfg);
				GF_LOG(GF_LOG_ERROR, GF_LOG_CODING, ("[VVC] alloc failed while parsing vvc config\n"));
				return NULL;
			}
			gf_bs_read_data(bs, cfg->general_constraint_info, cfg->num_constraint_info - 1);
			cfg->general_constraint_info[cfg->num_constraint_info-1] =  gf_bs_read_int(bs, 6);
		} else {
			//forbidden in spec!
			gf_bs_read_int(bs, 6);
		}

		cfg->ptl_sublayer_present_mask = 0;
		for (j=cfg->numTemporalLayers-2; j>=0; j--) {
			u32 val = gf_bs_read_int(bs, 1);
			cfg->ptl_sublayer_present_mask |= val << j;
		}
		for (j=cfg->numTemporalLayers; j<=8 && cfg->numTemporalLayers>1; j++) {
			gf_bs_read_int(bs, 1);
		}
		for (j=cfg->numTemporalLayers-2; j>=0; j--) {
			if (cfg->ptl_sublayer_present_mask & (1<<j)) {
				cfg->sublayer_level_idc[j] = gf_bs_read_u8(bs);
			}
		}
		cfg->num_sub_profiles = gf_bs_read_u8(bs);
		if (cfg->num_sub_profiles) {
			cfg->sub_profiles_idc = gf_malloc(sizeof(u32)*cfg->num_sub_profiles);
			if (!cfg->sub_profiles_idc) {
				gf_free(cfg->general_constraint_info);
				gf_free(cfg);
				GF_LOG(GF_LOG_ERROR, GF_LOG_CODING, ("[VVC] alloc failed while parsing vvc config\n"));
				return NULL;
			}
		}
		for (i=0; i<cfg->num_sub_profiles; i++) {
			cfg->sub_profiles_idc[i] = gf_bs_read_u32(bs);
		}

		//end PTL

		cfg->maxPictureWidth = gf_bs_read_u16(bs);
		cfg->maxPictureHeight = gf_bs_read_u16(bs);
		cfg->avgFrameRate = gf_bs_read_u16(bs);
	}

	count = gf_bs_read_int(bs, 8);
	for (i=0; i<count; i++) {
		u32 nalucount, j;
		Bool valid = GF_FALSE;
		GF_NALUFFParamArray *ar;
		GF_SAFEALLOC(ar, GF_NALUFFParamArray);
		if (!ar) {
			gf_odf_vvc_cfg_del(cfg);
			GF_LOG(GF_LOG_ERROR, GF_LOG_CODING, ("[VVC] alloc failed while parsing vvc config\n"));
			return NULL;
		}
		ar->array_completeness = gf_bs_read_int(bs, 1);
		gf_bs_read_int(bs, 2);
		ar->type = gf_bs_read_int(bs, 5);

		switch (ar->type) {
		case GF_VVC_NALU_DEC_PARAM:
		case GF_VVC_NALU_OPI:
		case GF_VVC_NALU_VID_PARAM:
		case GF_VVC_NALU_SEQ_PARAM:
		case GF_VVC_NALU_PIC_PARAM:
		case GF_VVC_NALU_SEI_PREFIX:
		case GF_VVC_NALU_SEI_SUFFIX:
			valid = GF_TRUE;
			ar->nalus = gf_list_new();
			gf_list_add(cfg->param_array, ar);
			break;
		default:
			GF_LOG(GF_LOG_WARNING, GF_LOG_CODING, ("[VVC] Invalid NALU type %d in vvcC - ignoring\n", ar->type));
			gf_free(ar);
			break;
		}

		if (!valid || ((ar->type != GF_VVC_NALU_DEC_PARAM) && (ar->type != GF_VVC_NALU_OPI)))
			nalucount = gf_bs_read_int(bs, 16);
		else
			nalucount = 1;

		for (j=0; j<nalucount; j++) {
			GF_NALUFFParam *sl;
			u32 size = gf_bs_read_int(bs, 16);
			if ((size>gf_bs_available(bs)) || (size<2)) {
				GF_LOG(GF_LOG_ERROR, GF_LOG_CODING, ("[VVC] Wrong param set size %d\n", size));
				gf_odf_vvc_cfg_del(cfg);
				return NULL;
			}
			if (!valid) {
				gf_bs_skip_bytes(bs, size);
				continue;
			}
			GF_SAFEALLOC(sl, GF_NALUFFParam );
			if (!sl) {
				gf_odf_vvc_cfg_del(cfg);
				GF_LOG(GF_LOG_ERROR, GF_LOG_CODING, ("[VVC] alloc failed while parsing vvc config\n"));
				return NULL;
			}

			sl->size = size;
			sl->data = (char *)gf_malloc(sizeof(char) * sl->size);
			if (!sl->data) {
				gf_free(sl);
				gf_odf_vvc_cfg_del(cfg);
				GF_LOG(GF_LOG_ERROR, GF_LOG_CODING, ("[VVC] alloc failed while parsing vvc config\n"));
				return NULL;
			}
			gf_bs_read_data(bs, sl->data, sl->size);
			gf_list_add(ar->nalus, sl);
		}
	}
	return cfg;
}

GF_EXPORT
GF_VVCConfig *gf_odf_vvc_cfg_read(u8 *dsi, u32 dsi_size)
{
	GF_BitStream *bs = gf_bs_new(dsi, dsi_size, GF_BITSTREAM_READ);
	GF_VVCConfig *cfg = gf_odf_vvc_cfg_read_bs(bs);
	gf_bs_del(bs);
	return cfg;
}

GF_EXPORT
GF_AV1Config *gf_odf_av1_cfg_new()
{
	GF_AV1Config *cfg;
	GF_SAFEALLOC(cfg, GF_AV1Config);
	if (!cfg) return NULL;
	cfg->marker = 1;
	cfg->version = 1;
	cfg->initial_presentation_delay_minus_one = 0;
	cfg->obu_array = gf_list_new();
	return cfg;
}

GF_EXPORT
void gf_odf_av1_cfg_del(GF_AV1Config *cfg)
{
	if (!cfg) return;
	while (gf_list_count(cfg->obu_array)) {
		GF_AV1_OBUArrayEntry *a = (GF_AV1_OBUArrayEntry*)gf_list_get(cfg->obu_array, 0);
		if (a->obu) gf_free(a->obu);
		gf_list_rem(cfg->obu_array, 0);
		gf_free(a);
	}
	gf_list_del(cfg->obu_array);
	gf_free(cfg);
}

GF_EXPORT
GF_Err gf_odf_av1_cfg_write_bs(GF_AV1Config *cfg, GF_BitStream *bs)
{
	u32 i = 0;
	gf_bs_write_int(bs, cfg->marker, 1);
	gf_assert(cfg->marker == 1);
	gf_bs_write_int(bs, cfg->version, 7);
	gf_assert(cfg->version == 1);
	gf_bs_write_int(bs, cfg->seq_profile, 3);
	gf_bs_write_int(bs, cfg->seq_level_idx_0, 5);
	gf_bs_write_int(bs, cfg->seq_tier_0, 1);
	gf_bs_write_int(bs, cfg->high_bitdepth, 1);
	gf_bs_write_int(bs, cfg->twelve_bit, 1);
	gf_bs_write_int(bs, cfg->monochrome, 1);
	gf_bs_write_int(bs, cfg->chroma_subsampling_x, 1);
	gf_bs_write_int(bs, cfg->chroma_subsampling_y, 1);
	gf_bs_write_int(bs, cfg->chroma_sample_position, 2);
	gf_bs_write_int(bs, 0, 3); /*reserved*/
	gf_bs_write_int(bs, cfg->initial_presentation_delay_present, 1);
	gf_bs_write_int(bs, cfg->initial_presentation_delay_minus_one, 4); /*TODO: compute initial_presentation_delay_minus_one*/
	for (i = 0; i < gf_list_count(cfg->obu_array); ++i) {
		GF_AV1_OBUArrayEntry *a = gf_list_get(cfg->obu_array, i);
		gf_bs_write_data(bs, a->obu, (u32)a->obu_length); //TODO: we are supposed to omit the size on the last OBU...
	}
	return GF_OK;
}

GF_EXPORT
GF_Err gf_odf_av1_cfg_write(GF_AV1Config *cfg, u8 **outData, u32 *outSize) {
	GF_Err e;
	GF_BitStream *bs = gf_bs_new(NULL, 0, GF_BITSTREAM_WRITE);
	*outSize = 0;
	*outData = NULL;
	e = gf_odf_av1_cfg_write_bs(cfg, bs);
	if (e == GF_OK)
		gf_bs_get_content(bs, outData, outSize);

	gf_bs_del(bs);
	return e;
}

GF_EXPORT
GF_VPConfig *gf_odf_vp_cfg_new()
{
	GF_VPConfig *cfg;
	GF_SAFEALLOC(cfg, GF_VPConfig);
	if (!cfg) return NULL;
	cfg->codec_initdata_size = 0;
	cfg->codec_initdata = NULL;
	return cfg;
}

GF_EXPORT
void gf_odf_vp_cfg_del(GF_VPConfig *cfg)
{
	if (!cfg) return;

	if (cfg->codec_initdata) {
		gf_free(cfg->codec_initdata);
		cfg->codec_initdata = NULL;
	}

	gf_free(cfg);
}

GF_EXPORT
GF_Err gf_odf_vp_cfg_write_bs(GF_VPConfig *cfg, GF_BitStream *bs, Bool is_v0)
{
	gf_bs_write_int(bs, cfg->profile, 8);
	gf_bs_write_int(bs, cfg->level, 8);
	gf_bs_write_int(bs, cfg->bit_depth, 4);
	gf_bs_write_int(bs, cfg->chroma_subsampling, 3);
	gf_bs_write_int(bs, cfg->video_fullRange_flag, 1);
	gf_bs_write_int(bs, cfg->colour_primaries, 8);
	gf_bs_write_int(bs, cfg->transfer_characteristics, 8);
	gf_bs_write_int(bs, cfg->matrix_coefficients, 8);

	if (!is_v0) {
		if (cfg->codec_initdata_size) {
			GF_LOG(GF_LOG_WARNING, GF_LOG_CODING, ("[VPX] Invalid data in configuration: codec_initdata_size must be 0, was %d - ignoring\n", cfg->codec_initdata_size));
		}

		gf_bs_write_int(bs, (u16)0, 16);
	}

	return GF_OK;
}

GF_EXPORT
GF_Err gf_odf_vp_cfg_write(GF_VPConfig *cfg, u8 **outData, u32 *outSize, Bool is_v0)
{
	GF_Err e;
	GF_BitStream *bs = gf_bs_new(NULL, 0, GF_BITSTREAM_WRITE);
	*outSize = 0;
	*outData = NULL;
	e = gf_odf_vp_cfg_write_bs(cfg, bs, is_v0);
	if (e==GF_OK)
		gf_bs_get_content(bs, outData, outSize);

	gf_bs_del(bs);
	return e;
}

GF_EXPORT
GF_VPConfig *gf_odf_vp_cfg_read_bs(GF_BitStream *bs, Bool is_v0)
{
	GF_VPConfig *cfg = gf_odf_vp_cfg_new();

	cfg->profile = gf_bs_read_int(bs, 8);
	cfg->level = gf_bs_read_int(bs, 8);

	cfg->bit_depth = gf_bs_read_int(bs, 4);
	cfg->chroma_subsampling = gf_bs_read_int(bs, 3);
	cfg->video_fullRange_flag = gf_bs_read_int(bs, 1);

	cfg->colour_primaries = gf_bs_read_int(bs, 8);
	cfg->transfer_characteristics = gf_bs_read_int(bs, 8);
	cfg->matrix_coefficients = gf_bs_read_int(bs, 8);

	if (is_v0)
		return cfg;

	cfg->codec_initdata_size = gf_bs_read_int(bs, 16);

	// must be 0 according to spec
	if (cfg->codec_initdata_size) {
		GF_LOG(GF_LOG_ERROR, GF_LOG_CODING, ("[VPX] Invalid data in configuration: codec_initdata_size must be 0, was %d\n", cfg->codec_initdata_size));
		gf_odf_vp_cfg_del(cfg);
		return NULL;
	}

	return cfg;
}

GF_EXPORT
GF_VPConfig *gf_odf_vp_cfg_read(u8 *dsi, u32 dsi_size)
{
	GF_BitStream *bs = gf_bs_new(dsi, dsi_size, GF_BITSTREAM_READ);
	GF_VPConfig *cfg = gf_odf_vp_cfg_read_bs(bs, GF_FALSE);
	gf_bs_del(bs);
	return cfg;
}

GF_EXPORT
GF_AV1Config *gf_odf_av1_cfg_read_bs_size(GF_BitStream *bs, u32 size)
{
#ifndef GPAC_DISABLE_AV_PARSERS
	AV1State *av1_state;
	u8 reserved;
	GF_AV1Config *cfg;

	if (!size) size = (u32) gf_bs_available(bs);
	if (!size) return NULL;

	GF_SAFEALLOC(av1_state, AV1State);
	if (!av1_state) return NULL;
	cfg = gf_odf_av1_cfg_new();
	gf_av1_init_state(av1_state);
	av1_state->config = cfg;

	cfg->marker = gf_bs_read_int(bs, 1);
	cfg->version = gf_bs_read_int(bs, 7);
	cfg->seq_profile = gf_bs_read_int(bs, 3);
	cfg->seq_level_idx_0 = gf_bs_read_int(bs, 5);
	cfg->seq_tier_0 = gf_bs_read_int(bs, 1);
	cfg->high_bitdepth = gf_bs_read_int(bs, 1);
	cfg->twelve_bit = gf_bs_read_int(bs, 1);
	cfg->monochrome = gf_bs_read_int(bs, 1);
	cfg->chroma_subsampling_x = gf_bs_read_int(bs, 1);
	cfg->chroma_subsampling_y = gf_bs_read_int(bs, 1);
	cfg->chroma_sample_position = gf_bs_read_int(bs, 2);

	reserved = gf_bs_read_int(bs, 3);
	cfg->initial_presentation_delay_present = gf_bs_read_int(bs, 1);
	if (cfg->initial_presentation_delay_present) {
		cfg->initial_presentation_delay_minus_one = gf_bs_read_int(bs, 4);
	} else {
		/*reserved = */gf_bs_read_int(bs, 4);
		cfg->initial_presentation_delay_minus_one = 0;
	}
	size -= 4;

	if (reserved != 0 || cfg->marker != 1 || cfg->version != 1) {
		GF_LOG(GF_LOG_DEBUG, GF_LOG_CODING, ("[AV1] av1C: wrong reserved %d / marker %d / version %d expecting 0 1 1\n", reserved, cfg->marker, cfg->version));
		gf_odf_av1_cfg_del(cfg);
		gf_free(av1_state);
		return NULL;
	}


	while (size) {
		u64 pos, obu_size;
		ObuType obu_type;
		GF_AV1_OBUArrayEntry *a;

		pos = gf_bs_get_position(bs);
		obu_size = 0;
		if (gf_av1_parse_obu(bs, &obu_type, &obu_size, NULL, av1_state) != GF_OK) {
			GF_LOG(GF_LOG_ERROR, GF_LOG_CODING, ("[AV1] av1C: could not parse AV1 OBU at position "LLU". Leaving parsing.\n", pos));
			break;
		}
		gf_assert(obu_size == gf_bs_get_position(bs) - pos);
		GF_LOG(GF_LOG_DEBUG, GF_LOG_CODING, ("[AV1] av1C: parsed AV1 OBU type=%u size="LLU" at position "LLU".\n", obu_type, obu_size, pos));

		if (!av1_is_obu_header(obu_type)) {
			GF_LOG(GF_LOG_DEBUG, GF_LOG_CODING, ("[AV1] av1C: AV1 unexpected OBU type=%u size="LLU" found at position "LLU". Forwarding.\n", pos));
		}
		GF_SAFEALLOC(a, GF_AV1_OBUArrayEntry);
		if (!a) break;
		a->obu = gf_malloc((size_t)obu_size);
		if (!a->obu) {
			gf_free(a);
			break;
		}
		gf_bs_seek(bs, pos);
		gf_bs_read_data(bs, (char *) a->obu, (u32)obu_size);
		a->obu_length = obu_size;
		a->obu_type = obu_type;
		gf_list_add(cfg->obu_array, a);

		if (size<obu_size) {
			GF_LOG(GF_LOG_WARNING, GF_LOG_CODING, ("[AV1] AV1 config misses %d bytes to fit the entire OBU\n", obu_size - size));
			break;
		}
		size -= (u32) obu_size;
	}
	gf_av1_reset_state(av1_state, GF_TRUE);
	gf_bs_align(bs);
	gf_free(av1_state);
	return cfg;
#else
	return NULL;
#endif
}

GF_EXPORT
GF_AV1Config *gf_odf_av1_cfg_read_bs(GF_BitStream *bs)
{
	return gf_odf_av1_cfg_read_bs_size(bs, 0);

}
GF_EXPORT
GF_AV1Config *gf_odf_av1_cfg_read(u8 *dsi, u32 dsi_size)
{
	GF_BitStream *bs = gf_bs_new(dsi, dsi_size, GF_BITSTREAM_READ);
	GF_AV1Config *cfg = gf_odf_av1_cfg_read_bs(bs);
	gf_bs_del(bs);
	return cfg;
}

GF_DOVIDecoderConfigurationRecord *gf_odf_dovi_cfg_read_bs(GF_BitStream *bs)
{
	GF_DOVIDecoderConfigurationRecord *cfg;
	GF_SAFEALLOC(cfg, GF_DOVIDecoderConfigurationRecord);

	cfg->dv_version_major = gf_bs_read_u8(bs);
	cfg->dv_version_minor = gf_bs_read_u8(bs);
	cfg->dv_profile = gf_bs_read_int(bs, 7);
	cfg->dv_level = gf_bs_read_int(bs, 6);
	cfg->rpu_present_flag = gf_bs_read_int(bs, 1);
	cfg->el_present_flag = gf_bs_read_int(bs, 1);
	cfg->bl_present_flag = gf_bs_read_int(bs, 1);
	cfg->dv_bl_signal_compatibility_id = gf_bs_read_int(bs, 4);
	if (gf_bs_read_int(bs, 28)) {
		GF_LOG(GF_LOG_WARNING, GF_LOG_CODING, ("[DOVI] Configuration reserved bits are not zero\n"));
	}
	for (u32 i=0; i<4; i++) {
		if (gf_bs_read_u32(bs)) {
			GF_LOG(GF_LOG_WARNING, GF_LOG_CODING, ("[DOVII] Configuration reserved bits are not zero\n"));
		}
	}
	return cfg;
}

GF_EXPORT
void gf_odf_dovi_cfg_del(GF_DOVIDecoderConfigurationRecord *cfg)
{
	gf_free(cfg);
}

GF_Err gf_odf_dovi_cfg_write_bs(GF_DOVIDecoderConfigurationRecord *cfg, GF_BitStream *bs)
{
	gf_bs_write_u8(bs,  cfg->dv_version_major);
	gf_bs_write_u8(bs,  cfg->dv_version_minor);
	gf_bs_write_int(bs, cfg->dv_profile, 7);
	gf_bs_write_int(bs, cfg->dv_level, 6);
	gf_bs_write_int(bs, cfg->rpu_present_flag, 1);
	gf_bs_write_int(bs, cfg->el_present_flag, 1);
	gf_bs_write_int(bs, cfg->bl_present_flag, 1);
	gf_bs_write_int(bs, cfg->dv_bl_signal_compatibility_id, 4);
    gf_bs_write_int(bs, 0, 28);
    gf_bs_write_u32(bs, 0);
    gf_bs_write_u32(bs, 0);
    gf_bs_write_u32(bs, 0);
    gf_bs_write_u32(bs, 0);
	return GF_OK;
}


GF_Err gf_odf_ac3_cfg_write_bs(GF_AC3Config *cfg, GF_BitStream *bs)
{
	if (!cfg || !bs) return GF_BAD_PARAM;

	if (cfg->is_ec3) {
		u32 i;
		gf_bs_write_int(bs, cfg->brcode, 13);
		gf_bs_write_int(bs, cfg->nb_streams - 1, 3);
		for (i=0; i<cfg->nb_streams; i++) {
			gf_bs_write_int(bs, cfg->streams[i].fscod, 2);
			gf_bs_write_int(bs, cfg->streams[i].bsid, 5);
			gf_bs_write_int(bs, cfg->streams[i].bsmod, 5);
			gf_bs_write_int(bs, cfg->streams[i].acmod, 3);
			gf_bs_write_int(bs, cfg->streams[i].lfon, 1);
			gf_bs_write_int(bs, 0, 3);
			gf_bs_write_int(bs, cfg->streams[i].nb_dep_sub, 4);
			if (cfg->streams[i].nb_dep_sub) {
				gf_bs_write_int(bs, cfg->streams[i].chan_loc, 9);
			} else {
				gf_bs_write_int(bs, 0, 1);
			}
		}
	} else {
		gf_bs_write_int(bs, cfg->streams[0].fscod, 2);
		gf_bs_write_int(bs, cfg->streams[0].bsid, 5);
		gf_bs_write_int(bs, cfg->streams[0].bsmod, 3);
		gf_bs_write_int(bs, cfg->streams[0].acmod, 3);
		gf_bs_write_int(bs, cfg->streams[0].lfon, 1);
		gf_bs_write_int(bs, cfg->brcode, 5);
		gf_bs_write_int(bs, 0, 5);
	}
	return GF_OK;
}

GF_Err gf_odf_ac3_cfg_write(GF_AC3Config *cfg, u8 **data, u32 *size)
{
	GF_BitStream *bs = gf_bs_new(NULL, 0, GF_BITSTREAM_WRITE);
	GF_Err e = gf_odf_ac3_cfg_write_bs(cfg, bs);

	if (cfg->is_ec3 && (cfg->atmos_ec3_ext || cfg->complexity_index_type)) {
		gf_bs_write_int(bs, 0, 7);
		gf_bs_write_int(bs, cfg->atmos_ec3_ext, 1);
		gf_bs_write_u8(bs, cfg->complexity_index_type);
	}
	gf_bs_get_content(bs, data, size);

	gf_bs_del(bs);
	return e;
}

GF_Err gf_odf_ac3_config_parse_bs(GF_BitStream *bs, Bool is_ec3, GF_AC3Config *cfg)
{
	if (!cfg || !bs) return GF_BAD_PARAM;
	memset(cfg, 0, sizeof(GF_AC3Config));
	cfg->is_ec3 = is_ec3;
	if (is_ec3) {
		u32 j;
		cfg->is_ec3 = 1;
		cfg->brcode = gf_bs_read_int(bs, 13);
		cfg->nb_streams = 1 + gf_bs_read_int(bs, 3);
		for (j=0; j<cfg->nb_streams; j++) {
			cfg->streams[j].fscod = gf_bs_read_int(bs, 2);
			cfg->streams[j].bsid = gf_bs_read_int(bs, 5);
			gf_bs_read_int(bs, 1);
			cfg->streams[j].asvc = gf_bs_read_int(bs, 1);
			cfg->streams[j].bsmod = gf_bs_read_int(bs, 3);
			cfg->streams[j].acmod = gf_bs_read_int(bs, 3);
			cfg->streams[j].lfon = gf_bs_read_int(bs, 1);
			gf_bs_read_int(bs, 3);
			cfg->streams[j].nb_dep_sub = gf_bs_read_int(bs, 4);
			if (cfg->streams[j].nb_dep_sub) {
				cfg->streams[j].chan_loc = gf_bs_read_int(bs, 9);
			} else {
				gf_bs_read_int(bs, 1);
			}
		}
	} else {
		cfg->nb_streams = 1;
		cfg->streams[0].fscod = gf_bs_read_int(bs, 2);
		cfg->streams[0].bsid = gf_bs_read_int(bs, 5);
		cfg->streams[0].bsmod = gf_bs_read_int(bs, 3);
		cfg->streams[0].acmod = gf_bs_read_int(bs, 3);
		cfg->streams[0].lfon = gf_bs_read_int(bs, 1);
		cfg->brcode = gf_bs_read_int(bs, 5);
		gf_bs_read_int(bs, 5);
	}
	return GF_OK;
}

GF_Err gf_odf_ac3_config_parse(u8 *dsi, u32 dsi_len, Bool is_ec3, GF_AC3Config *cfg)
{
	GF_BitStream *bs;
	GF_Err e;
	if (!cfg || !dsi) return GF_BAD_PARAM;
	bs = gf_bs_new(dsi, dsi_len, GF_BITSTREAM_READ);
	e = gf_odf_ac3_config_parse_bs(bs, is_ec3, cfg);
	if (is_ec3 && gf_bs_available(bs)>=2) {
		gf_bs_read_int(bs, 7);
		cfg->atmos_ec3_ext = gf_bs_read_int(bs, 1);
		cfg->complexity_index_type = gf_bs_read_u8(bs);
	}
	gf_bs_del(bs);
	return e;
}

#define GF_AC4_SSS(bs, value, nbits, size, mode) { \
	if (mode == GF_AC4_DESCMODE_PARSE) value = gf_bs_read_int(bs, nbits); \
	else if (mode == GF_AC4_DESCMODE_WRITE) gf_bs_write_int(bs, value, nbits); \
	else if (mode == GF_AC4_DESCMODE_GETSIZE) *size += nbits;}

#define GF_AC4_ALLIGN(bs, size, mode) { \
	if (mode == GF_AC4_DESCMODE_PARSE) gf_bs_align(bs); \
	else if (mode == GF_AC4_DESCMODE_WRITE) gf_bs_align(bs); \
	else if (mode == GF_AC4_DESCMODE_GETSIZE && (*size % 8 != 0)) *size = ((*size / 8) + 1) * 8;}

GF_Err gf_odf_ac4_cfg_alternative_info(GF_AC4AlternativeInfo *info, GF_BitStream *bs, u64 *size, u8 desc_mode)
{
	u32 i;

	GF_AC4_SSS(bs, info->name_len, 16, size, desc_mode);

	if (info->name_len >= GF_ARRAY_LENGTH(info->presentation_name))
		return GF_ISOM_INVALID_MEDIA;

	for (i = 0; i < info->name_len; i++) {
		GF_AC4_SSS(bs, info->presentation_name[i], 8, size, desc_mode);
	}

	GF_AC4_SSS(bs, info->n_targets, 5, size, desc_mode);

	if (info->n_targets >= MIN(GF_ARRAY_LENGTH(info->target_md_compat), GF_ARRAY_LENGTH(info->target_device_category)))
		return GF_ISOM_INVALID_MEDIA;

	for (i = 0; i < info->n_targets; i++) {
		GF_AC4_SSS(bs, info->target_md_compat[i], 3, size, desc_mode);
		GF_AC4_SSS(bs, info->target_device_category[i], 8, size, desc_mode);
	}
	return GF_OK;
}

GF_Err gf_odf_ac4_cfg_substream_dsi(GF_AC4SubStream *s, GF_BitStream *bs, u8 b_channel_coded, u64 *size, u8 desc_mode)
{
	u32 zero_val = 0;

	GF_AC4_SSS(bs, s->dsi_sf_multiplier, 2, size, desc_mode);
    GF_AC4_SSS(bs, s->b_substream_bitrate_indicator, 1, size, desc_mode);
    if (s->b_substream_bitrate_indicator == 1) {
        GF_AC4_SSS(bs, s->substream_bitrate_indicator, 5, size, desc_mode);
    }
    if (b_channel_coded == 1) {
        GF_AC4_SSS(bs, s->dsi_substream_channel_mask, 24, size, desc_mode);
    }else {
        GF_AC4_SSS(bs, s->b_ajoc, 1, size, desc_mode);
        if (s->b_ajoc == 1) {
            GF_AC4_SSS(bs, s->b_static_dmx, 1, size, desc_mode);
            if (s->b_static_dmx == 0) {
                GF_AC4_SSS(bs, s->n_dmx_objects_minus1, 4, size, desc_mode);
            }
            GF_AC4_SSS(bs, s->n_umx_objects_minus1, 6, size, desc_mode);
        }
        GF_AC4_SSS(bs, s->b_substream_contains_bed_objects, 1, size, desc_mode);
        GF_AC4_SSS(bs, s->b_substream_contains_dynamic_objects, 1, size, desc_mode);
        GF_AC4_SSS(bs, s->b_substream_contains_ISF_objects, 1, size, desc_mode);
        GF_AC4_SSS(bs, zero_val, 1, size, desc_mode); //reserved bit
    }
	return GF_OK;
}

GF_Err gf_odf_ac4_cfg_content_type(GF_AC4SubStreamGroupV1 *g, GF_BitStream *bs, u64 *size, u8 desc_mode)
{
	u32 i;

	GF_AC4_SSS(bs, g->b_content_type, 1, size, desc_mode);
    if (g->b_content_type == 1){
        GF_AC4_SSS(bs, g->content_classifier, 3, size, desc_mode);
        GF_AC4_SSS(bs, g->b_language_indicator, 1, size, desc_mode);
        if (g->b_language_indicator == 1){
            GF_AC4_SSS(bs, g->n_language_tag_bytes, 6, size, desc_mode);
            for (i = 0; i < g->n_language_tag_bytes; i++ ){
                GF_AC4_SSS(bs, g->language_tag_bytes[i], 8, size, desc_mode);
            }
        }
    }
	return GF_OK;
}

GF_Err gf_odf_ac4_cfg_substream_group_dsi(GF_AC4SubStreamGroupV1 *g, GF_BitStream *bs, u64 *size, u8 desc_mode)
{
	u32 i;
	GF_AC4SubStream *s;

	if (!g)
		return GF_BAD_PARAM;

	GF_AC4_SSS(bs, g->b_substreams_present, 1, size, desc_mode);
    GF_AC4_SSS(bs, g->b_hsf_ext, 1, size, desc_mode);
    GF_AC4_SSS(bs, g->b_channel_coded, 1, size, desc_mode);
    GF_AC4_SSS(bs, g->n_lf_substreams, 8, size, desc_mode);

	if (desc_mode == GF_AC4_DESCMODE_PARSE) {
		g->substreams = gf_list_new();
	}
    for (i = 0; i < g->n_lf_substreams; i++ ){
		if (desc_mode == GF_AC4_DESCMODE_PARSE) {
			GF_SAFEALLOC(s, GF_AC4SubStream);
		} else { // write or get_size
			s = (GF_AC4SubStream*)gf_list_get(g->substreams, i);
		}
		gf_odf_ac4_cfg_substream_dsi(s, bs, g->b_channel_coded, size, desc_mode);
		if (desc_mode == GF_AC4_DESCMODE_PARSE) {
			gf_list_add(g->substreams, s);
		}
    }
	gf_odf_ac4_cfg_content_type(g, bs, size, desc_mode);
    return GF_OK;
}

GF_Err gf_odf_ac4_cfg_bitrate_dsi(GF_AC4BitrateDsi *bitr, GF_BitStream *bs, u64 *size, u8 desc_mode)
{
	GF_AC4_SSS(bs, bitr->bit_rate_mode, 2, size, desc_mode);
    GF_AC4_SSS(bs, bitr->bit_rate, 32, size, desc_mode);
    GF_AC4_SSS(bs, bitr->bit_rate_precision, 32, size, desc_mode);
	return GF_OK;
}

GF_Err gf_odf_ac4_cfg_presentation_v1_dsi(GF_AC4PresentationV1 *p, GF_BitStream *bs, u64 *size, u8 desc_mode)
{
	GF_AC4SubStreamGroupV1 *g;
	u32 i, zero_val = 0;

	GF_AC4_SSS(bs, p->presentation_config, 5, size, desc_mode);
	if (p->presentation_config == 0x06) {
		p->b_add_emdf_substreams = 1;
	} else {
		GF_AC4_SSS(bs, p->mdcompat, 3, size, desc_mode);
		GF_AC4_SSS(bs, p->b_presentation_id, 1, size, desc_mode);
		if (p->b_presentation_id) {
			GF_AC4_SSS(bs, p->presentation_id, 5, size, desc_mode);
		}
		GF_AC4_SSS(bs, p->dsi_frame_rate_multiply_info, 2, size, desc_mode);
		GF_AC4_SSS(bs, p->dsi_frame_rate_fraction_info, 2, size, desc_mode);
		GF_AC4_SSS(bs, p->presentation_emdf_version, 5, size, desc_mode);
		GF_AC4_SSS(bs, p->presentation_key_id, 10, size, desc_mode);

		GF_AC4_SSS(bs, p->b_presentation_channel_coded, 1, size, desc_mode);
		if (p->b_presentation_channel_coded) {
			GF_AC4_SSS(bs, p->dsi_presentation_ch_mode, 5, size, desc_mode);
			if (p->dsi_presentation_ch_mode >= 11 && p->dsi_presentation_ch_mode <= 14) {
				GF_AC4_SSS(bs, p->pres_b_4_back_channels_present, 1, size, desc_mode);
				GF_AC4_SSS(bs, p->pres_top_channel_pairs, 2, size, desc_mode);
			}
			GF_AC4_SSS(bs, p->presentation_channel_mask_v1, 24, size, desc_mode);
		}

		GF_AC4_SSS(bs, p->b_presentation_core_differs, 1, size, desc_mode);
		if (p->b_presentation_core_differs) {
			GF_AC4_SSS(bs, p->b_presentation_core_channel_coded, 1, size, desc_mode);
			if (p->b_presentation_core_channel_coded) {
				GF_AC4_SSS(bs, p->dsi_presentation_channel_mode_core, 2, size, desc_mode);
			}
		}
		GF_AC4_SSS(bs, p->b_presentation_filter, 1, size, desc_mode);
		if (p->b_presentation_filter) {
			GF_AC4_SSS(bs, p->b_enable_presentation, 1, size, desc_mode);
			GF_AC4_SSS(bs, p->n_filter_bytes, 8, size, desc_mode);
			for (i = 0; i < p->n_filter_bytes; i++) {
				GF_AC4_SSS(bs, zero_val, 8, size, desc_mode); // filter_data
			}
		}
		// calloc memory for substream_groups
		if (desc_mode == GF_AC4_DESCMODE_PARSE) {
			p->substream_groups = gf_list_new();
		}

		if (p->presentation_config == 0x1f) {
			if (desc_mode == GF_AC4_DESCMODE_PARSE) {
				GF_SAFEALLOC(g, GF_AC4SubStreamGroupV1);
				gf_list_add(p->substream_groups, g);
				p->n_substream_groups = 1;
			} else { // write or get_size
				g = (GF_AC4SubStreamGroupV1*)gf_list_get(p->substream_groups, 0);
			}
			if (g)
				gf_odf_ac4_cfg_substream_group_dsi(g, bs, size, desc_mode);
		}
		else {
			GF_AC4_SSS(bs, p->b_multi_pid, 1, size, desc_mode);
			if (p->presentation_config >= 0 && p->presentation_config <= 2) {
				p->n_substream_groups = 2;
			}
			if (p->presentation_config == 3 || p->presentation_config == 4) {
				p->n_substream_groups = 3;
			}

			if (p->presentation_config == 5) {
				// n_substream_groups_minus2
				if (desc_mode == GF_AC4_DESCMODE_PARSE)
					p->n_substream_groups = gf_bs_read_int(bs, 3) + 2;
				else if (desc_mode == GF_AC4_DESCMODE_WRITE)
					gf_bs_write_int(bs, p->n_substream_groups - 2, 3);
				else if(desc_mode == GF_AC4_DESCMODE_GETSIZE)
					*size += 3;
			}

			for (i = 0; i < p->n_substream_groups; i++) {
				if (desc_mode == GF_AC4_DESCMODE_PARSE) {
					GF_SAFEALLOC(g, GF_AC4SubStreamGroupV1);
					gf_list_add(p->substream_groups, g);
				} else { // write or get_size
					g = (GF_AC4SubStreamGroupV1*)gf_list_get(p->substream_groups, i);
				}
				gf_odf_ac4_cfg_substream_group_dsi(g, bs, size, desc_mode);
			}

			if (p->presentation_config > 5) {
				GF_AC4_SSS(bs, p->n_skip_bytes, 7, size, desc_mode);
				for (i = 0; i < p->n_skip_bytes; i++) {
					GF_AC4_SSS(bs, zero_val, 8, size, desc_mode); // skip_data
				}
			}
		}
		GF_AC4_SSS(bs, p->b_pre_virtualized, 1, size, desc_mode);
		GF_AC4_SSS(bs, p->b_add_emdf_substreams, 1, size, desc_mode);
	}
	if (p->b_add_emdf_substreams) {
		GF_AC4_SSS(bs, p->n_add_emdf_substreams, 7, size, desc_mode);
		for (i = 0; i < p->n_add_emdf_substreams && i < GF_ARRAY_LENGTH(p->substream_emdf_version) && i < GF_ARRAY_LENGTH(p->substream_key_id); i++) {
			GF_AC4_SSS(bs, p->substream_emdf_version[i], 5, size, desc_mode);
			GF_AC4_SSS(bs, p->substream_key_id[i], 10, size, desc_mode);
		}
	}
	GF_AC4_SSS(bs, p->b_presentation_bitrate_info, 1, size, desc_mode);
	if (p->b_presentation_bitrate_info) {
		gf_odf_ac4_cfg_bitrate_dsi(&(p->ac4_bitrate_dsi), bs, size, desc_mode);
	}
	GF_AC4_SSS(bs, p->b_alternative, 1, size, desc_mode);
	if (p->b_alternative) {
		GF_AC4_ALLIGN(bs, size, desc_mode);
		gf_odf_ac4_cfg_alternative_info(&(p->alternative_info), bs, size, desc_mode);
	}
	GF_AC4_ALLIGN(bs, size, desc_mode);
	 /*
     * TODO: Not implement, need the information from ac4_substream.
     * Currently just set the value to 1 according to Dolby's internal discussion.
     */
	p->de_indicator = 1;
	GF_AC4_SSS(bs, p->de_indicator, 1, size, desc_mode);
	GF_AC4_SSS(bs, p->dolby_atmos_indicator, 1, size, desc_mode);
	GF_AC4_SSS(bs, zero_val, 4, size, desc_mode);

	if (p->presentation_id > 31) {
		p->b_extended_presentation_id = 1;
        p->extended_presentation_id = p->presentation_id;
	}
	GF_AC4_SSS(bs, p->b_extended_presentation_id, 1, size, desc_mode);
	if (p->b_extended_presentation_id) {
		GF_AC4_SSS(bs, p->extended_presentation_id, 9, size, desc_mode);
	}
	else {
		GF_AC4_SSS(bs, zero_val, 1, size, desc_mode);
	}

	return GF_OK;
}

GF_Err gf_odf_ac4_cfg_dsi_v1(GF_AC4StreamInfo *dsi, GF_BitStream *bs, u64 *size, u8 desc_mode)
{
	u32 i, j, add_pres_bytes, presentation_bytes, skip_bytes, ims_pres_num = 0, legacy_pres_num = 0;
	u32 pres_bytes = 0, t_size_bytes = 0;
	GF_AC4PresentationV1* p = NULL, *imsp = NULL;
	u64 pos, t_size_bits = 0;
	u8 *t_data = NULL;
	GF_BitStream *t_bs;

	if (!dsi)
		return GF_BAD_PARAM;

	GF_AC4_SSS(bs, dsi->ac4_dsi_version, 3, size, desc_mode);
	GF_AC4_SSS(bs, dsi->bitstream_version, 7, size, desc_mode);
	GF_AC4_SSS(bs, dsi->fs_index, 1, size, desc_mode);
	GF_AC4_SSS(bs, dsi->frame_rate_index, 4, size, desc_mode);

	if (desc_mode == GF_AC4_DESCMODE_WRITE) {
		// check whether legacy presentations are added in the presentations
		for (i = 0; i < dsi->n_presentations; i++) {
			p = gf_list_get(dsi->presentations, i);
			if (!p) continue;
			if (p->presentation_version == 1) {
				legacy_pres_num += 1;
			} else if (p->presentation_version == 2) {
				ims_pres_num += 1;
			}
		}

		// In WRITE mode, modify n_presentations for IMS content and add legacy presentations
		// For more information, please read Dolby AC-4 Online Delivery Kit - Signaling immersive stereo content
		if (legacy_pres_num == 0 && ims_pres_num > 0) {
			GF_LOG(GF_LOG_INFO, GF_LOG_APP, ("[AC4] This is a Dolby AC-4 bitstreams signal immersive stereo content.\n"));

			for (i = 0; i < dsi->n_presentations; i++) {
				p = gf_list_get(dsi->presentations, i);
				if (!p) continue;
				if (p->presentation_version == 2) {
					GF_SAFEALLOC(imsp, GF_AC4PresentationV1);
					gf_odf_ac4_presentation_deep_copy(imsp, p);

					imsp->presentation_version = 1;
					imsp->b_pre_virtualized = 0;
					imsp->dolby_atmos_indicator = 0;
					gf_list_add(dsi->presentations, imsp);
				}
			}
			dsi->n_presentations += ims_pres_num;
		}
	}

	GF_AC4_SSS(bs, dsi->n_presentations, 9, size, desc_mode);
	if (dsi->bitstream_version > 1) {
		GF_AC4_SSS(bs, dsi->b_program_id, 1, size, desc_mode);
		if (dsi->b_program_id) {
			GF_AC4_SSS(bs, dsi->short_program_id, 16, size, desc_mode);
			GF_AC4_SSS(bs, dsi->b_uuid, 1, size, desc_mode);
			if (dsi->b_uuid) {
				for (i = 0; i < 16; i++)
					GF_AC4_SSS(bs, dsi->program_uuid[i], 8, size, desc_mode);
			}
		}
	}

	// ac4_bitrate_dsi
	gf_odf_ac4_cfg_bitrate_dsi(&dsi->ac4_bitrate_dsi, bs, size, desc_mode);
	GF_AC4_ALLIGN(bs, size, desc_mode);

	if (desc_mode == GF_AC4_DESCMODE_PARSE) {
		dsi->presentations = gf_list_new();
	}

	for (i = 0; i < dsi->n_presentations; i++) {
		if (desc_mode == GF_AC4_DESCMODE_PARSE) {
			GF_SAFEALLOC(p, GF_AC4PresentationV1);
			gf_list_add(dsi->presentations, p);

			p->presentation_version = gf_bs_read_int(bs, 8);
			pres_bytes = gf_bs_read_int(bs, 8);
			if (pres_bytes == 255) {
				add_pres_bytes = gf_bs_read_int(bs, 16);
				pres_bytes += add_pres_bytes;
			}

			pos = gf_bs_get_position(bs);

			if (p->presentation_version == 0) {
				GF_LOG(GF_LOG_ERROR, GF_LOG_APP, ("[AC4] Don't support presentation_version 0.\n"));
			} else if (p->presentation_version == 1) {
				gf_odf_ac4_cfg_presentation_v1_dsi(p, bs, size, desc_mode);
			} else if (p->presentation_version == 2) {
				gf_odf_ac4_cfg_presentation_v1_dsi(p, bs, size, desc_mode);
			}

			presentation_bytes = (u32) (gf_bs_get_position(bs) - pos);
			skip_bytes = pres_bytes - presentation_bytes;

			for (j = 0; j < skip_bytes && gf_bs_available(bs); j++) {
				gf_bs_read_int(bs, 8);
			}
		}
		else if (desc_mode == GF_AC4_DESCMODE_WRITE) {
			p = gf_list_get(dsi->presentations, i);
			if (!p) continue;

			t_bs = gf_bs_new(NULL, 0, GF_BITSTREAM_WRITE);
			if (p->presentation_version == 0) {
				GF_LOG(GF_LOG_ERROR, GF_LOG_APP, ("[AC4] Don't support presentation_version 0.\n"));
			} else if (p->presentation_version == 1 || p->presentation_version == 2) {
				gf_odf_ac4_cfg_presentation_v1_dsi(p, t_bs, size, desc_mode);
			}

			// write into output bitstream
			gf_bs_write_int(bs, p->presentation_version, 8);
			presentation_bytes = (u32) gf_bs_get_position(t_bs);
			if (presentation_bytes < 255) {
				gf_bs_write_int(bs, presentation_bytes, 8);
			} else {
				gf_bs_write_int(bs, 255, 8);
				gf_bs_write_int(bs, presentation_bytes - 255, 16);
			}
			gf_bs_get_content(t_bs, &t_data, &t_size_bytes);
			gf_bs_write_data(bs, t_data, t_size_bytes);

			gf_bs_del(t_bs);
			gf_free(t_data);
		}
		else if (desc_mode == GF_AC4_DESCMODE_GETSIZE) {
			p = gf_list_get(dsi->presentations, i);

			t_size_bits = 0; // t_size in bits
			if (p->presentation_version == 0) {
				GF_LOG(GF_LOG_ERROR, GF_LOG_APP, ("[AC4] Don't support presentation_version 0.\n"));
				return GF_OK;
			} else if (p->presentation_version == 1 || p->presentation_version == 2) {
				gf_odf_ac4_cfg_presentation_v1_dsi(p, bs, &t_size_bits, desc_mode);
			}

			if (t_size_bits < 255 * 8) {
				*size += (8 + 8 + t_size_bits);
			} else {
				*size += (8 + 8 + 16 + t_size_bits);
			}
		}
	}

	return GF_OK;
}

GF_Err gf_odf_ac4_cfg_write_bs(GF_AC4Config *cfg, GF_BitStream *bs)
{
	if (!cfg || !bs) return GF_BAD_PARAM;
	GF_AC4StreamInfo* dsi = &cfg->stream;
	if (dsi->ac4_dsi_version == 0) {
		GF_LOG(GF_LOG_ERROR, GF_LOG_APP, ("[AC4] Don't support ac4_dsi_version 0.\n"));
		return GF_OK;
	}
	GF_Err e = gf_odf_ac4_cfg_dsi_v1(dsi, bs, NULL, GF_AC4_DESCMODE_WRITE);
	return e;
}

GF_Err gf_odf_ac4_cfg_write(GF_AC4Config *cfg, u8 **data, u32 *size)
{
	GF_BitStream *bs = gf_bs_new(NULL, 0, GF_BITSTREAM_WRITE);
	GF_Err e = gf_odf_ac4_cfg_write_bs(cfg, bs);
	gf_bs_get_content(bs, data, size);
	gf_bs_del(bs);
	return e;
}

GF_Err gf_odf_ac4_cfg_parse_bs(GF_BitStream *bs, GF_AC4Config *cfg)
{
	GF_AC4StreamInfo* dsi = &cfg->stream;
	u64 pos = gf_bs_get_position(bs);
	dsi->ac4_dsi_version = gf_bs_read_int(bs, 3);
	if (dsi->ac4_dsi_version == 0) {
		GF_LOG(GF_LOG_ERROR, GF_LOG_APP, ("[AC4] Don't support ac4_dsi_version 0.\n"));
		return GF_OK;
	}
	gf_bs_seek(bs, pos);
	GF_Err e = gf_odf_ac4_cfg_dsi_v1(dsi, bs, NULL, GF_AC4_DESCMODE_PARSE);
	if (e) return e;
	cfg->sample_rate = dsi->fs_index ? 48000 : 44100;
	return e;
}

GF_Err gf_odf_ac4_cfg_parse(u8 *dsi, u32 dsi_len, GF_AC4Config *cfg)
{
	GF_BitStream *bs;
	GF_Err e;
	if (!cfg || !dsi) return GF_BAD_PARAM;
	bs = gf_bs_new(dsi, dsi_len, GF_BITSTREAM_READ);
	e = gf_odf_ac4_cfg_parse_bs(bs, cfg);
	gf_bs_del(bs);
	return e;
}

u64 gf_odf_ac4_cfg_size(GF_AC4Config *cfg)
{
	GF_AC4StreamInfo* dsi = &cfg->stream;
	u64 size = 0;
	if (dsi->ac4_dsi_version == 0) {
		GF_LOG(GF_LOG_ERROR, GF_LOG_APP, ("[AC4] Don't support ac4_dsi_version 0.\n"));
		return 0;
	} else {
		gf_odf_ac4_cfg_dsi_v1(dsi, NULL, &size, GF_AC4_DESCMODE_GETSIZE);
	}
	return size / 8;
}

void gf_odf_ac4_cfg_deep_copy(GF_AC4Config *dst, GF_AC4Config *src)
{
	u32 i;
	GF_List *presentations_src = src->stream.presentations;
	GF_AC4PresentationV1 *pres_dst, *pres_src;

	if (!dst || !src) {
		return;
	}

	memcpy(dst, src, sizeof(GF_AC4Config));

	if (!src->stream.presentations) {
		return;
	}

	dst->stream.presentations = gf_list_new();
	for (i = 0; i < gf_list_count(presentations_src); i++) {
		pres_src = gf_list_get(presentations_src, i);

		GF_SAFEALLOC(pres_dst, GF_AC4PresentationV1);
		gf_odf_ac4_presentation_deep_copy(pres_dst, pres_src);
		gf_list_add(dst->stream.presentations, pres_dst);
	}
}

void gf_odf_ac4_presentation_deep_copy(GF_AC4PresentationV1 *pres_dst, GF_AC4PresentationV1 *pres_src)
{
	u32 j, s;
	GF_AC4SubStreamGroupV1 *group_dst, *group_src;
	GF_AC4SubStream *subs_dst, *subs_src;

	if (!pres_dst || !pres_src) {
		return;
	}

	memcpy(pres_dst, pres_src, sizeof(GF_AC4PresentationV1));

	if (!pres_src->substream_groups) {
		return;
	}

	pres_dst->substream_groups = gf_list_new();
	for (j = 0; j < gf_list_count(pres_src->substream_groups); j++) {
		group_src = gf_list_get(pres_src->substream_groups, j);

		GF_SAFEALLOC(group_dst, GF_AC4SubStreamGroupV1);
		memcpy(group_dst, group_src, sizeof(GF_AC4SubStreamGroupV1));
		gf_list_add(pres_dst->substream_groups, group_dst);

		if (!group_src->substreams) {
			continue;
		}

		group_dst->substreams = gf_list_new();
		for (s = 0; s < gf_list_count(group_src->substreams); s++) {
			subs_src = gf_list_get(group_src->substreams, s);

			GF_SAFEALLOC(subs_dst, GF_AC4SubStream);
			memcpy(subs_dst, subs_src, sizeof(GF_AC4SubStream));
			gf_list_add(group_dst->substreams, subs_dst);
		}
	}
}

GF_EXPORT
void gf_odf_ac4_cfg_clean_list(GF_AC4Config *cfg)
{
	u32 s;
	GF_AC4PresentationV1 *pres;
	GF_AC4SubStreamGroupV1 *group;
	GF_AC4SubStream *subs;

	if (!cfg)
		return;

	if (cfg->stream.presentations) {

		while ( (pres = gf_list_pop_back(cfg->stream.presentations)) ) {

			if (pres->substream_groups) {

				while ( (group = gf_list_pop_back(pres->substream_groups)) ) {

					if (group->substreams) {

						for (s = 0; s < gf_list_count(group->substreams); s++) {
							subs = gf_list_get(group->substreams, s);
							if (!subs) {
								continue;
							}

							gf_free(subs);
						}
						gf_list_del(group->substreams);

					}
					gf_free(group);

					// remove potential duplicates of group
					s32 idx = 1;
					while (idx>=0) {
						idx = gf_list_find(pres->substream_groups, group);
						if (idx>=0) gf_list_rem(pres->substream_groups, idx);
					}
				}
				gf_list_del(pres->substream_groups);

			}
			gf_free(pres);
		}

		gf_list_del(cfg->stream.presentations);
		cfg->stream.presentations = NULL;
	}
}

GF_EXPORT
void gf_odf_ac4_cfg_del(GF_AC4Config *cfg)
{
	if (!cfg) return;
	gf_odf_ac4_cfg_clean_list(cfg);
	gf_free(cfg);
}

GF_Err gf_odf_opus_cfg_parse_bs(GF_BitStream *bs, GF_OpusConfig *cfg)
{
	memset(cfg, 0, sizeof(GF_OpusConfig));
	cfg->version = gf_bs_read_u8(bs);
	cfg->OutputChannelCount = gf_bs_read_u8(bs);
	cfg->PreSkip = gf_bs_read_u16_le(bs);
	cfg->InputSampleRate = gf_bs_read_u32_le(bs);
	cfg->OutputGain = gf_bs_read_u16_le(bs);
	cfg->ChannelMappingFamily = gf_bs_read_u8(bs);
	if (cfg->ChannelMappingFamily) {
		cfg->StreamCount = gf_bs_read_u8(bs);
		cfg->CoupledCount = gf_bs_read_u8(bs);
		gf_bs_read_data(bs, (char *) cfg->ChannelMapping, cfg->OutputChannelCount);
	}
	return GF_OK;
}
GF_Err gf_odf_opus_cfg_parse(u8 *dsi, u32 dsi_len, GF_OpusConfig *cfg)
{
	GF_BitStream *bs;
	GF_Err e;
	if (!cfg || !dsi) return GF_BAD_PARAM;
	bs = gf_bs_new(dsi, dsi_len, GF_BITSTREAM_READ);
	e = gf_odf_opus_cfg_parse_bs(bs, cfg);
	gf_bs_del(bs);
	return e;
}

GF_Err gf_odf_opus_cfg_write_bs(GF_OpusConfig *cfg, GF_BitStream *bs)
{
	if (!cfg || !bs) return GF_BAD_PARAM;
	gf_bs_write_u8(bs, cfg->version);
	gf_bs_write_u8(bs, cfg->OutputChannelCount);
	gf_bs_write_u16_le(bs, cfg->PreSkip);
	gf_bs_write_u32_le(bs, cfg->InputSampleRate);
	gf_bs_write_u16_le(bs, cfg->OutputGain);
	gf_bs_write_u8(bs, cfg->ChannelMappingFamily);
	if (cfg->ChannelMappingFamily) {
		gf_bs_write_u8(bs, cfg->StreamCount);
		gf_bs_write_u8(bs, cfg->CoupledCount);
		gf_bs_write_data(bs, (char *) cfg->ChannelMapping, cfg->OutputChannelCount);
	}
	return GF_OK;
}

GF_Err gf_odf_opus_cfg_write(GF_OpusConfig *cfg, u8 **data, u32 *size)
{
	GF_BitStream *bs = gf_bs_new(NULL, 0, GF_BITSTREAM_WRITE);
	GF_Err e = gf_odf_opus_cfg_write_bs(cfg, bs);

	gf_bs_get_content(bs, data, size);
	gf_bs_del(bs);
	return e;
}

GF_EXPORT
GF_IAConfig *gf_odf_ia_cfg_new()
{
        GF_IAConfig *cfg = NULL;
        GF_SAFEALLOC(cfg, GF_IAConfig);
        if (!cfg) return NULL;
        cfg->configurationVersion = 1;
        cfg->configOBUs_size = 0;
        cfg->configOBUs = gf_list_new();
        if (!cfg->configOBUs) {
		gf_free(cfg);
		return NULL;
	}
        return cfg;
}

GF_EXPORT
GF_IAConfig *gf_odf_ia_cfg_read_bs_size(GF_BitStream *bs, u32 size) {
#ifndef GPAC_DISABLE_AV_PARSERS
	IAMFState *state = NULL;
	GF_IAConfig *cfg = NULL;
        u8 leb128_size;

        if (!size) size = (u32) gf_bs_available(bs);
        if (!size) {
                GF_LOG(GF_LOG_ERROR, GF_LOG_CODING, ("[IAMF] Unknown IAConfigurationBox size to read\n"));
                return NULL;
        }

	GF_SAFEALLOC(state, IAMFState);
	if (!state) return NULL;
	cfg = gf_odf_ia_cfg_new();
	gf_iamf_init_state(state);

        cfg->configurationVersion = gf_bs_read_u8(bs);
        if (cfg->configurationVersion != 1) {
                GF_LOG(GF_LOG_ERROR, GF_LOG_CODING, ("[IAMF] Unknown configurationVersion %d\n", cfg->configurationVersion));
		gf_odf_ia_cfg_del(cfg);
		gf_free(state);
                return NULL;
        }
        size--;

        cfg->configOBUs_size = (u32)gf_av1_leb128_read(bs, &leb128_size);
        size -= leb128_size;

        while(size) {
                u64 pos, obu_size;
                IamfObuType obu_type;
                GF_IamfObu *config_obu;

                pos = gf_bs_get_position(bs);
                obu_size = 0;
                if (gf_iamf_parse_obu(bs, &obu_type, &obu_size, state) != GF_OK) {
                      GF_LOG(GF_LOG_ERROR, GF_LOG_CODING, ("[IAMF] could not parse configOBUs at position "LLU". Leaving parsing.\n", pos));
                      break;
                }
                gf_assert(obu_size == gf_bs_get_position(bs) - pos);
                GF_LOG(GF_LOG_DEBUG, GF_LOG_CODING, ("[IAMF] parsed OBU type=%u size="LLU" at position "LLU".\n", obu_type, obu_size, pos));

                GF_SAFEALLOC(config_obu, GF_IamfObu);
                if (!config_obu) break;
                config_obu->raw_obu_bytes = gf_malloc((size_t)obu_size);
                if (!config_obu->raw_obu_bytes) {
                      gf_free(config_obu);
                      break;
                }
                gf_bs_seek(bs, pos);
                gf_bs_read_data(bs, (char *)config_obu->raw_obu_bytes, (u32)obu_size);
                config_obu->obu_length = obu_size;
                config_obu->obu_type = obu_type;
                gf_list_add(cfg->configOBUs, config_obu);

                if (size < obu_size) {
                      GF_LOG(GF_LOG_WARNING, GF_LOG_CODING, ("[IAMF] IAMF config misses %d bytes to fit the entire OBU\n", obu_size - size));
                      break;
                }
                size -= (u32) obu_size;
        }

	gf_iamf_reset_state(state, GF_TRUE);
	gf_bs_align(bs);
	gf_free(state);
        return cfg;

#else
        return NULL;
#endif
}

GF_EXPORT
GF_IAConfig *gf_odf_ia_cfg_read_bs(GF_BitStream *bs) {
        return gf_odf_ia_cfg_read_bs_size(bs, 0);
}

GF_EXPORT
GF_IAConfig *gf_odf_ia_cfg_read(u8 *dsi, u32 dsi_size)
{
        GF_BitStream *bs = gf_bs_new(dsi, dsi_size, GF_BITSTREAM_READ);
        GF_IAConfig *cfg = gf_odf_ia_cfg_read_bs(bs);
        gf_bs_del(bs);
        return cfg;
}

GF_EXPORT
void gf_odf_ia_cfg_del(GF_IAConfig *cfg)
{
        if (!cfg) return;
        while (gf_list_count(cfg->configOBUs)) {
                GF_IamfObu *configOBU = (GF_IamfObu*)gf_list_get(cfg->configOBUs, 0);
                if (configOBU->raw_obu_bytes) gf_free(configOBU->raw_obu_bytes);
                gf_list_rem(cfg->configOBUs, 0);
                gf_free(configOBU);
        }
        gf_list_del(cfg->configOBUs);
        gf_free(cfg);
}

GF_EXPORT
GF_Err gf_odf_ia_cfg_write_bs(GF_IAConfig *cfg, GF_BitStream *bs)
{
	u32 i;
	if (!cfg || !bs) return GF_BAD_PARAM;

	#ifndef GPAC_DISABLE_AV_PARSERS
		gf_bs_write_u8(bs, cfg->configurationVersion);
		gf_av1_leb128_write(bs, cfg->configOBUs_size);
		for (i = 0; i < gf_list_count(cfg->configOBUs); ++i) {
				GF_IamfObu *configOBU = gf_list_get(cfg->configOBUs, i);
				gf_bs_write_data(bs, configOBU->raw_obu_bytes, (u32)configOBU->obu_length);
		}
	#endif

	return GF_OK;
}

GF_EXPORT
GF_Err gf_odf_ia_cfg_write(GF_IAConfig *cfg, u8 **outData, u32 *outSize) {
	GF_Err e;
	GF_BitStream *bs = gf_bs_new(NULL, 0, GF_BITSTREAM_WRITE);
	*outSize = 0;
	*outData = NULL;
	e = gf_odf_ia_cfg_write_bs(cfg, bs);
	if (e == GF_OK)
		gf_bs_get_content(bs, outData, outSize);

	gf_bs_del(bs);
	return e;
}

GF_EXPORT
u32 gf_odf_ia_cfg_size(GF_IAConfig *cfg)
{
	if (!cfg) return 0;

	#ifndef GPAC_DISABLE_AV_PARSERS
		u32 cfg_size = 1; // configurationVersion
		cfg_size += gf_av1_leb128_size(cfg->configOBUs_size);
		cfg_size += cfg->configOBUs_size;
		return cfg_size;
	#else
		return 0;
	#endif
}
