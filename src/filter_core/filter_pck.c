/*
 *			GPAC - Multimedia Framework C SDK
 *
 *			Authors: Jean Le Feuvre
 *			Copyright (c) Telecom ParisTech 2017-2023
 *					All rights reserved
 *
 *  This file is part of GPAC / filters sub-project
 *
 *  GPAC is free software; you can redistribute it and/or modify
 *  it under the terfsess of the GNU Lesser General Public License as published by
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

#include "filter_session.h"

static void gf_filter_pck_reset_props(GF_FilterPacket *pck, GF_FilterPid *pid)
{
	memset(&pck->info, 0, sizeof(GF_FilterPckInfo));
	pck->info.dts = pck->info.cts = GF_FILTER_NO_TS;
	pck->info.byte_offset =  GF_FILTER_NO_BO;
	pck->info.flags = GF_PCKF_BLOCK_START | GF_PCKF_BLOCK_END;
	pck->pid = pid;
	pck->src_filter = pid->filter;
	pck->session = pid->filter->session;
}

GF_EXPORT
GF_Err gf_filter_pck_merge_properties_filter(GF_FilterPacket *pck_src, GF_FilterPacket *pck_dst, gf_filter_prop_filter filter_prop, void *cbk)
{
	if (PCK_IS_INPUT(pck_dst)) {
		GF_LOG(GF_LOG_ERROR, GF_LOG_FILTER, ("Attempt to set property on an input packet in filter %s\n", pck_dst->pid->filter->name));
		return GF_BAD_PARAM;
	}
	//we allow copying over properties from dest packets to dest packets
	//get true packet pointer
	pck_src=pck_src->pck;
	pck_dst=pck_dst->pck;

	//keep internal flags
	u32 iflags = pck_dst->info.flags & (GF_PCKF_PROPS_REFERENCE | GF_PCKF_FORCE_MAIN) ;
	pck_dst->info = pck_src->info;
	//remove internal props flags of reference
	pck_dst->info.flags &= ~ (GF_PCKF_PROPS_REFERENCE | GF_PCKF_FORCE_MAIN);
	//restore internal props flags of packet
	pck_dst->info.flags |= iflags;

	if (!pck_src->props || (!pck_dst->pid && !pck_src->pid)) {
		return GF_OK;
	}
	if (!pck_dst->props) {
		pck_dst->props = gf_props_new(pck_dst->pid ? pck_dst->pid->filter : pck_src->pid->filter);

		if (!pck_dst->props) return GF_OUT_OF_MEM;
	}
	return gf_props_merge_property(pck_dst->props, pck_src->props, filter_prop, cbk);
}

GF_EXPORT
GF_Err gf_filter_pck_merge_properties(GF_FilterPacket *pck_src, GF_FilterPacket *pck_dst)
{
	return gf_filter_pck_merge_properties_filter(pck_src, pck_dst, NULL, NULL);
}

typedef struct
{
	u32 data_size;
	GF_FilterPacket *pck;
	GF_FilterPacket *closest;
} GF_PckQueueEnum;

static void pck_queue_enum(void *udta, void *item)
{
	GF_PckQueueEnum *enum_state = (GF_PckQueueEnum *) udta;
	GF_FilterPacket *cur = (GF_FilterPacket *) item;

	if (cur->alloc_size >= enum_state->data_size) {
		if (!enum_state->pck || (enum_state->pck->alloc_size > cur->alloc_size)) {
			enum_state->pck = cur;
		}
	}
	else if (!enum_state->closest) enum_state->closest = cur;
	//small data blocks, find smaller one
	else if (enum_state->data_size<1000) {
		if (enum_state->closest->alloc_size > cur->alloc_size) enum_state->closest = cur;
	}
	//otherwise find largest one below our target size
	else if (enum_state->closest->alloc_size < cur->alloc_size) enum_state->closest = cur;
}

static GF_FilterPacket *gf_filter_pck_new_alloc_internal(GF_FilterPid *pid, u32 data_size, u8 **data)
{
	GF_FilterPacket *pck=NULL;
	GF_FilterPacket *closest=NULL;
	u32 count, max_reservoir_size;

	if (PID_IS_INPUT(pid)) {
		GF_LOG(GF_LOG_ERROR, GF_LOG_FILTER, ("Attempt to allocate a packet on an input PID in filter %s\n", pid->filter->name));
		return NULL;
	}

	count = gf_fq_count(pid->filter->pcks_alloc_reservoir);

	if (count) {
		//don't let reservoir grow too large (may happen if burst of packets are stored/consumed in the upper chain)
		while (count>30) {
			GF_FilterPacket *head_pck = gf_fq_pop(pid->filter->pcks_alloc_reservoir);
			gf_free(head_pck->data);
			gf_free(head_pck);
			count--;
		}

		GF_PckQueueEnum pck_enum_state;
		memset(&pck_enum_state, 0, sizeof(GF_PckQueueEnum));
		pck_enum_state.data_size = data_size;
		gf_fq_enum(pid->filter->pcks_alloc_reservoir, pck_queue_enum, &pck_enum_state);
		pck = pck_enum_state.pck;
		closest = pck_enum_state.closest;
	}

	//stop allocating after a while - TODO we for sure can design a better algo...
	max_reservoir_size = pid->num_destinations ? 10 : 1;
	//if pid is file, force 1 max
	if (!pck && (pid->stream_type==GF_STREAM_FILE))
		max_reservoir_size = 1;

	if (!pck && (count>=max_reservoir_size)) {
		if (!closest) return NULL;
		closest->alloc_size = data_size;
		closest->data = gf_realloc(closest->data, closest->alloc_size);
		if (!closest->data) {
			gf_free(closest);
			GF_LOG(GF_LOG_ERROR, GF_LOG_FILTER, ("Failed to allocate new packet on PID %s of filter %s\n", pid->name, pid->filter->name));
			return NULL;
		}
		pck = closest;
#ifdef GPAC_MEMORY_TRACKING
		pid->filter->session->nb_realloc_pck++;
#endif
	}

	if (!pck) {
		GF_SAFEALLOC(pck, GF_FilterPacket);
		if (!pck) {
			GF_LOG(GF_LOG_ERROR, GF_LOG_FILTER, ("Failed to allocate new packet on PID %s of filter %s\n", pid->name, pid->filter->name));
			return NULL;
		}
		pck->data = gf_malloc(sizeof(char)*data_size);
		if (!pck->data) {
			gf_free(pck);
			GF_LOG(GF_LOG_ERROR, GF_LOG_FILTER, ("Failed to allocate new packet on PID %s of filter %s\n", pid->name, pid->filter->name));
			return NULL;
		}
		pck->alloc_size = data_size;
#ifdef GPAC_MEMORY_TRACKING
		pid->filter->session->nb_alloc_pck+=2;
#endif
	} else {
		//pop first item and swap pointers. We can safely do this since this filter
		//is the only one accessing the queue in pop mode, all others are just pushing to it
		//this may however imply that we don't get the best matching block size if new packets
		//were added to the list

		GF_FilterPacket *head_pck = gf_fq_pop(pid->filter->pcks_alloc_reservoir);
		char *pck_data = pck->data;
		u32 alloc_size = pck->alloc_size;
		pck->data = head_pck->data;
		pck->alloc_size = head_pck->alloc_size;
		head_pck->data = pck_data;
		head_pck->alloc_size = alloc_size;
		pck = head_pck;
	}

	pck->pck = pck;
	pck->data_length = data_size;
	if (data) *data = pck->data;
	pck->filter_owns_mem = 0;

	gf_filter_pck_reset_props(pck, pid);
	return pck;
}

GF_EXPORT
GF_FilterPacket *gf_filter_pck_new_alloc(GF_FilterPid *pid, u32 data_size, u8 **data)
{
	return gf_filter_pck_new_alloc_internal(pid, data_size, data);
}

static GF_FilterPacket *gf_filter_pck_new_dangling_packet(GF_FilterPacket *cached_pck, u32 data_length)
{
	GF_FilterPacket *dst;

	if (cached_pck && cached_pck->reference) {
		gf_filter_pck_discard(cached_pck);
		cached_pck = NULL;
	}
	if (cached_pck) {
		if (data_length > cached_pck->alloc_size) {
			cached_pck->alloc_size = data_length;
			cached_pck->data = gf_realloc(cached_pck->data, cached_pck->alloc_size);
			if (!cached_pck->data) {
				gf_free(cached_pck);
				GF_LOG(GF_LOG_ERROR, GF_LOG_FILTER, ("Failed to re-allocate dangling packet data\n"));
				return NULL;
			}
		}
		cached_pck->data_length = data_length;
		if (cached_pck->props) {
			GF_PropertyMap *props = cached_pck->props;
			cached_pck->props=NULL;
			gf_assert(props->reference_count);
			if (safe_int_dec(&props->reference_count) == 0) {
				gf_props_del(props);
			}
		}
		return cached_pck;
	}

	GF_SAFEALLOC(dst, GF_FilterPacket);
	if (!dst) {
		GF_LOG(GF_LOG_ERROR, GF_LOG_FILTER, ("Failed to allocate new dangling packet\n"));
		return NULL;
	}
	dst->data = gf_malloc(sizeof(char) * data_length);
	if (!dst->data) {
		gf_free(dst);
		GF_LOG(GF_LOG_ERROR, GF_LOG_FILTER, ("Failed to allocate new dangling packet\n"));
		return NULL;
	}
	dst->alloc_size = dst->data_length = data_length;
	dst->filter_owns_mem = 0;
	dst->pck = dst;
	dst->is_dangling = 1;
	return dst;
}

static GF_FilterPacket *gf_filter_pck_clone_frame_interface(GF_FilterPid *pid, GF_FilterPacket *pck_source, u8 **data, Bool dangling_packet, GF_FilterPacket *cached_pck)
{
	u32 i, w, h, stride, stride_uv, pf, osize;
	u32 nb_planes, uv_height;
	GF_FilterPacket *dst, *ref;
	u8 *pck_data;
	GF_FilterPacketInstance *pcki = (GF_FilterPacketInstance *) pck_source;
	const GF_PropertyValue *p;

	if (data) *data = NULL;

	ref = pcki->pck;

	p = gf_filter_pid_get_property(ref->pid , GF_PROP_PID_WIDTH);
	w = p ? p->value.uint : 0;
	p = gf_filter_pid_get_property(ref->pid, GF_PROP_PID_HEIGHT);
	h = p ? p->value.uint : 0;
	p = gf_filter_pid_get_property(ref->pid, GF_PROP_PID_PIXFMT);
	pf = p ? p->value.uint : 0;
	//not supported
	if (!w || !h || !pf || !ref->frame_ifce->get_plane) {
		GF_LOG(GF_LOG_ERROR, GF_LOG_FILTER, ("Missing width/height/pf in frame interface cloning, not supported\n"));
		return NULL;
	}
	stride = stride_uv = 0;

	if (gf_pixel_get_size_info(pf, w, h, &osize, &stride, &stride_uv, &nb_planes, &uv_height) == GF_FALSE) {
		GF_LOG(GF_LOG_ERROR, GF_LOG_FILTER, ("Unknown pixel format, cannot grab underlying video data\n"));
		return NULL;
	}

	if (!dangling_packet) {
		p = gf_filter_pid_get_property(pid, GF_PROP_PID_STRIDE);
		if (!p || (p->value.uint != stride)) {
			gf_filter_pid_set_property(pid, GF_PROP_PID_STRIDE, &PROP_UINT(stride));
			gf_filter_pid_set_property(pid, GF_PROP_PID_STRIDE_UV, &PROP_UINT(stride_uv));
		}
		dst = gf_filter_pck_new_alloc(pid, osize, &pck_data);
		if (!dst) return NULL;
	} else {
		dst = gf_filter_pck_new_dangling_packet(cached_pck, osize);
		if (!dst) return NULL;
		pck_data = dst->data;
	}

	if (!dst) return NULL;
	if (data) *data = pck_data;

	for (i=0; i<nb_planes; i++) {
		u32 j, write_h, dst_stride;
		const u8 *in_ptr;
		u32 src_stride = i ? stride_uv : stride;
		GF_Err e = ref->frame_ifce->get_plane(ref->frame_ifce, i, &in_ptr, &src_stride);
		if (e) {
			GF_LOG(GF_LOG_ERROR, GF_LOG_FILTER, ("Failed to fetch plane data from hardware frame, cannot clone\n"));
			break;
		}
		if (i) {
			write_h = uv_height;
			dst_stride = stride_uv;
		} else {
			write_h = h;
			dst_stride = stride;
		}
		for (j=0; j<write_h; j++) {
			memcpy(pck_data, in_ptr, dst_stride);
			in_ptr += src_stride;
			pck_data += dst_stride;
		}
	}
	gf_filter_pck_merge_properties(pck_source, dst);
	return dst;
}

static GF_FilterPacket *gf_filter_pck_new_clone_internal(GF_FilterPid *pid, GF_FilterPacket *pck_source, u8 **data, Bool force_copy, Bool dangling_packet, GF_FilterPacket *cached_pck)
{
	GF_FilterPacket *dst, *ref;
	u32 max_ref = 0;
	GF_FilterPacketInstance *pcki;
	if (!pck_source) return NULL;

	if (dangling_packet) {
		if (PCK_IS_OUTPUT(pck_source)) {
			GF_LOG(GF_LOG_ERROR, GF_LOG_FILTER, ("Cannot create dangling packet from non-source packet\n"));
			return NULL;
		}
	}

	pcki = (GF_FilterPacketInstance *) pck_source;
	if (pcki->pck->frame_ifce)
		return gf_filter_pck_clone_frame_interface(pid, pck_source, data, dangling_packet, cached_pck);

	if (force_copy) {
		max_ref = 2;
	} else {
		ref = pcki->pck;
		while (ref) {
			if (ref->filter_owns_mem==2) {
				max_ref = 2;
				break;
			}
			if (ref->reference_count>max_ref)
				max_ref = ref->reference_count;
			ref = ref->reference;
		}
	}

	if (max_ref>1) {
		u8 *data_new;
		if (dangling_packet) {
			dst = gf_filter_pck_new_dangling_packet(cached_pck, pcki->pck->data_length);
			if (!dst) return NULL;

			data_new = dst->data;
		} else {
			dst = gf_filter_pck_new_alloc_internal(pid, pcki->pck->data_length, &data_new);
		}
		if (dst && data_new) {
			memcpy(data_new, pcki->pck->data, sizeof(char)*pcki->pck->data_length);
			if (data) *data = data_new;
		}
		if (dst) gf_filter_pck_merge_properties(pck_source, dst);
		return dst;
	}

	if (dangling_packet) {
		if (cached_pck && cached_pck->reference) {
			gf_filter_pck_discard(cached_pck);
		}
		GF_SAFEALLOC(dst, GF_FilterPacket);
		if (!dst) {
			GF_LOG(GF_LOG_ERROR, GF_LOG_FILTER, ("Failed to allocate new dangling packet (source filter %s)\n", pck_source->pck->src_filter->name));
			return NULL;
		}
		pck_source = pck_source->pck;
		dst->data = pck_source->data;
		dst->data_length = pck_source->data_length;
		dst->reference = pck_source;
		dst->pck = dst;
		dst->is_dangling = 2;

		//cf gf_filter_pck_new_ref for these
		safe_int_inc(&pck_source->reference_count);
		safe_int_inc(&pck_source->pid->nb_shared_packets_out);
		safe_int_inc(&pck_source->pid->filter->nb_shared_packets_out);
		gf_filter_pck_merge_properties(pck_source, dst);
		if (data) *data = dst->data;
		return dst;
	}

	dst = gf_filter_pck_new_ref(pid, 0, 0, pck_source);
	if (dst) {
		gf_filter_pck_merge_properties(pck_source, dst);
		if (data) *data = dst->data;
	}
	return dst;
}

GF_EXPORT
GF_FilterPacket *gf_filter_pck_dangling_copy(GF_FilterPacket *pck_source, GF_FilterPacket *cached_pck)
{
	return gf_filter_pck_new_clone_internal(NULL, pck_source, NULL, GF_FALSE, GF_TRUE, cached_pck);
}

GF_EXPORT
GF_FilterPacket *gf_filter_pck_dangling_clone(GF_FilterPacket *pck_source, GF_FilterPacket *cached_pck)
{
	return gf_filter_pck_new_clone_internal(NULL, pck_source, NULL, GF_TRUE, GF_TRUE, cached_pck);
}


GF_EXPORT
GF_FilterPacket *gf_filter_pck_new_clone(GF_FilterPid *pid, GF_FilterPacket *pck_source, u8 **data)
{
	return gf_filter_pck_new_clone_internal(pid, pck_source, data, GF_FALSE, GF_FALSE, NULL);
}
GF_EXPORT
GF_FilterPacket *gf_filter_pck_new_copy(GF_FilterPid *pid, GF_FilterPacket *pck_source, u8 **data)
{
	return gf_filter_pck_new_clone_internal(pid, pck_source, data, GF_TRUE, GF_FALSE, NULL);
}


GF_EXPORT
GF_FilterPacket *gf_filter_pck_new_alloc_destructor(GF_FilterPid *pid, u32 data_size, u8 **data, gf_fsess_packet_destructor destruct)
{
	GF_FilterPacket *pck = gf_filter_pck_new_alloc_internal(pid, data_size, data);
	if (pck && destruct) {
		pck->destructor = destruct;
		if (pid->filter->nb_main_thread_forced)
			pck->info.flags |= GF_PCKF_FORCE_MAIN;
	}
	return pck;
}

GF_EXPORT
GF_FilterPacket *gf_filter_pck_new_shared_internal(GF_FilterPid *pid, const u8 *data, u32 data_size, gf_fsess_packet_destructor destruct, Bool intern_pck)
{
	GF_FilterPacket *pck;

	if (!pid) {
		GF_LOG(GF_LOG_ERROR, GF_LOG_FILTER, ("Attempt to allocate a packet on a NULL PID\n"));
		return NULL;
	}
	if (PID_IS_INPUT(pid)) {
		GF_LOG(GF_LOG_ERROR, GF_LOG_FILTER, ("Attempt to allocate a packet on an input PID in filter %s\n", pid->filter->name));
		return NULL;
	}

	pck = gf_fq_pop(pid->filter->pcks_shared_reservoir);
	if (!pck) {
		GF_SAFEALLOC(pck, GF_FilterPacket);
		if (!pck)
			return NULL;
	}
	pck->pck = pck;
	pck->data = (char *) data;
	pck->data_length = data_size;
	pck->destructor = destruct;
	pck->filter_owns_mem = 1;
	if (!intern_pck) {
		safe_int_inc(&pid->nb_shared_packets_out);
		safe_int_inc(&pid->filter->nb_shared_packets_out);
		GF_LOG(GF_LOG_DEBUG, GF_LOG_FILTER, ("Filter %s PID %s has %d shared packets out\n", pid->filter->name, pid->name, pid->nb_shared_packets_out));
	}
	gf_filter_pck_reset_props(pck, pid);

	if (destruct && pid->filter->nb_main_thread_forced)
		pck->info.flags |= GF_PCKF_FORCE_MAIN;
	return pck;
}

GF_EXPORT
GF_FilterPacket *gf_filter_pck_new_shared(GF_FilterPid *pid, const u8 *data, u32 data_size, gf_fsess_packet_destructor destruct)
{
	return gf_filter_pck_new_shared_internal(pid, data, data_size, destruct, GF_FALSE);
}

GF_EXPORT
GF_FilterPacket *gf_filter_pck_new_ref_destructor(GF_FilterPid *pid, u32 data_offset, u32 data_size, GF_FilterPacket *reference, gf_fsess_packet_destructor destruct)
{
	GF_FilterPacket *pck;
	if (!reference) return NULL;
	reference=reference->pck;

	if (reference->data) {
		if (data_offset > reference->data_length)
			return NULL;

		if (!data_size)
			data_size = reference->data_length - data_offset;

		if (data_offset + data_size > reference->data_length)
			return NULL;
	}

	pck = gf_filter_pck_new_shared(pid, reference->data, data_size, destruct);
	if (!pck) return NULL;
	pck->reference = reference;
	//apply offset
	if (reference->data)
		pck->data += data_offset;
	gf_assert(reference->reference_count);
	safe_int_inc(&reference->reference_count);
	if (!data_offset && (!data_size || (data_size==reference->data_length))) {
		pck->data = reference->data;
		pck->data_length = reference->data_length;
		pck->frame_ifce = reference->frame_ifce;
	}
	if (reference->info.flags & GF_PCKF_FORCE_MAIN)
		pck->info.flags |= GF_PCKF_FORCE_MAIN;

	safe_int_inc(&reference->pid->nb_shared_packets_out);
	safe_int_inc(&reference->pid->filter->nb_shared_packets_out);
	return pck;
}

GF_EXPORT
GF_FilterPacket *gf_filter_pck_new_ref(GF_FilterPid *pid, u32 data_offset, u32 data_size, GF_FilterPacket *reference)
{
	return gf_filter_pck_new_ref_destructor(pid, data_offset, data_size, reference, NULL);
}

GF_EXPORT
GF_Err gf_filter_pck_set_readonly(GF_FilterPacket *pck)
{
	if (PCK_IS_INPUT(pck)) {
		GF_LOG(GF_LOG_ERROR, GF_LOG_FILTER, ("Attempt to set readonly on an input packet in filter %s\n", pck->pid->filter->name));
		return GF_BAD_PARAM;
	}
	pck->filter_owns_mem = 2;
	return GF_OK;
}

GF_EXPORT
GF_FilterPacket *gf_filter_pck_new_frame_interface(GF_FilterPid *pid, GF_FilterFrameInterface *frame_ifce, gf_fsess_packet_destructor destruct)
{
	GF_FilterPacket *pck;
	if (!frame_ifce) return NULL;
	pck = gf_filter_pck_new_shared(pid, NULL, 0, NULL);
	if (!pck) return NULL;
	pck->destructor = destruct;
	pck->frame_ifce = frame_ifce;
	pck->filter_owns_mem = 2;
	//GL frame interface must be processed by main thread
	if (frame_ifce && frame_ifce->get_gl_texture)
		pck->info.flags |= GF_PCKF_FORCE_MAIN;
	return pck;
}

GF_EXPORT
GF_Err gf_filter_pck_forward(GF_FilterPacket *reference, GF_FilterPid *pid)
{
	GF_FilterPacket *pck;
	if (!reference) return GF_OUT_OF_MEM;
	reference=reference->pck;
	pck = gf_filter_pck_new_shared(pid, NULL, 0, NULL);
	if (!pck) return GF_OUT_OF_MEM;
	pck->reference = reference;
	gf_assert(reference->reference_count);
	safe_int_inc(&reference->reference_count);
	safe_int_inc(&reference->pid->nb_shared_packets_out);
	safe_int_inc(&reference->pid->filter->nb_shared_packets_out);

	gf_filter_pck_merge_properties(reference, pck);
	pck->data = reference->data;
	pck->data_length = reference->data_length;
	pck->frame_ifce = reference->frame_ifce;
	if (reference->info.flags & GF_PCKF_FORCE_MAIN)
		pck->info.flags |= GF_PCKF_FORCE_MAIN;

	return gf_filter_pck_send(pck);
}

/*internal*/
void gf_filter_packet_destroy(GF_FilterPacket *pck)
{
	Bool is_filter_destroyed = GF_FALSE;
	GF_FilterPid *pid = pck->pid;
	Bool is_ref_props_packet = GF_FALSE;

	//this is a ref props packet, its destruction can happen at any time, included after destruction
	//of source filter/pid.  pck->src_filter or pck->pid shall not be trusted !
	if (pck->info.flags & GF_PCKF_PROPS_REFERENCE) {
		is_ref_props_packet = GF_TRUE;
		is_filter_destroyed = GF_TRUE;
		pck->src_filter = NULL;
		pck->pid = NULL;
		gf_assert(!pck->destructor);
		gf_assert(!pck->filter_owns_mem);
		gf_assert(!pck->reference);

		if (pck->info.cts != GF_FILTER_NO_TS) {
			GF_LOG(GF_LOG_DEBUG, GF_LOG_FILTER, ("Destroying packet property reference CTS "LLU" size %d\n", pck->info.cts, pck->data_length));
		} else {
			GF_LOG(GF_LOG_DEBUG, GF_LOG_FILTER, ("Destroying packet property reference size %d\n", pck->data_length));
		}
	}
	//if not null, we discard a non-sent packet
	else if (pck->src_filter) {
		is_filter_destroyed = pck->src_filter->finalized;
	}

	if (!is_filter_destroyed && !pck->is_dangling) {
		if (pck->pid && pck->pid->filter) {
			if (pck->info.cts != GF_FILTER_NO_TS) {
				GF_LOG(GF_LOG_DEBUG, GF_LOG_FILTER, ("Filter %s PID %s destroying packet CTS "LLU"\n", pck->pid->filter->name, pck->pid->name, pck->info.cts));
			} else {
				GF_LOG(GF_LOG_DEBUG, GF_LOG_FILTER, ("Filter %s PID %s destroying packet\n", pck->pid->filter->name, pck->pid->name));
			}
		}
	}
	//never set for dangling packets
	if (pck->destructor) pck->destructor(pid->filter, pid, pck);

	//never set for dangling packets (theyr are never sent)
	if (pck->pid_props) {
		GF_PropertyMap *props = pck->pid_props;
		pck->pid_props = NULL;

		if (is_ref_props_packet) {
			gf_assert(props->pckrefs_reference_count);
			if (safe_int_dec(&props->pckrefs_reference_count) == 0) {
				gf_props_del(props);
			}
		} else {
			gf_assert(props->reference_count);
			if (safe_int_dec(&props->reference_count) == 0) {
				if (!is_filter_destroyed) {
					if (pck->pid->filter) {
						//see \ref gf_filter_pid_merge_properties_internal for mutex
						gf_mx_p(pck->pid->filter->tasks_mx);
						gf_list_del_item(pck->pid->properties, props);
						gf_mx_v(pck->pid->filter->tasks_mx);
					} else {
						gf_list_del_item(pck->pid->properties, props);
					}
				}
				gf_props_del(props);
			}
		}
	}

	if (pck->props) {
		GF_PropertyMap *props = pck->props;
		pck->props=NULL;
		gf_assert(props->reference_count);
		if (safe_int_dec(&props->reference_count) == 0) {
			gf_props_del(props);
		}
	}
	//never set for dangling packets, they are either standalone mem or packet references
	if (pck->filter_owns_mem && !(pck->info.flags & GF_PCK_CMD_MASK) ) {
		if (pck->pid) {
			gf_assert(pck->pid->nb_shared_packets_out);
			safe_int_dec(&pck->pid->nb_shared_packets_out);
			if (pck->pid->filter) {
				gf_assert(pck->pid->filter->nb_shared_packets_out);
				safe_int_dec(&pck->pid->filter->nb_shared_packets_out);
			}
		}
		GF_LOG(GF_LOG_DEBUG, GF_LOG_FILTER, ("Filter %s PID %s has %d shared packets out\n", pck->pid->filter->name, pck->pid->name, pck->pid->nb_shared_packets_out));
	}

	pck->data_length = 0;
	pck->pid = NULL;

	if (pck->reference) {
		gf_assert(pck->reference->pid->nb_shared_packets_out);
		gf_assert(pck->reference->pid->filter->nb_shared_packets_out);
		safe_int_dec(&pck->reference->pid->nb_shared_packets_out);
		safe_int_dec(&pck->reference->pid->filter->nb_shared_packets_out);

		GF_LOG(GF_LOG_DEBUG, GF_LOG_FILTER, ("Filter %s PID %s has %d shared packets out\n", pck->reference->pid->filter->name, pck->reference->pid->name, pck->reference->pid->nb_shared_packets_out));
		gf_assert(pck->reference->reference_count);
		if (safe_int_dec(&pck->reference->reference_count) == 0) {
			gf_filter_packet_destroy(pck->reference);
		}
		pck->reference = NULL;
		if (pck->is_dangling)
			pck->data = NULL;
	}
	/*this is a property reference packet, its destruction may happen at ANY time*/
	if (is_ref_props_packet) {
		if (gf_fq_res_add(pck->session->pcks_refprops_reservoir, pck)) {
			gf_free(pck);
		}
	} else if (is_filter_destroyed) {
		if (!pck->filter_owns_mem && pck->data) gf_free(pck->data);
		gf_free(pck);
	} else if (pck->is_dangling) {
		if (pck->data) gf_free(pck->data);
		gf_free(pck);
	}
	else if (pck->filter_owns_mem ) {
		if (!pid->filter || gf_fq_res_add(pid->filter->pcks_shared_reservoir, pck)) {
			gf_free(pck);
		}
	} else {
		if (!pid->filter || gf_fq_res_add(pid->filter->pcks_alloc_reservoir, pck)) {
			if (pck->data) gf_free(pck->data);
			gf_free(pck);
		}
	}
}

Bool gf_filter_aggregate_packets(GF_FilterPidInst *dst)
{
	u32 size=0, pos=0;
	u64 byte_offset = 0;
	u64 first_offset = 0;
	u8 *data;
	GF_FilterPacket *final;
	u32 i, count;
	GF_FilterPckInfo info;

	//no need to lock the packet list since only the dispatch thread operates on it

	count=gf_list_count(dst->pck_reassembly);
	//no packet to reaggregate
	if (!count) return GF_FALSE;

	dst->nb_reagg_pck++;

	//single packet, update PID buffer and dispatch to packet queue
	if (count==1) {
		GF_FilterPacketInstance *pcki = gf_list_pop_back(dst->pck_reassembly);
		safe_int_inc(&dst->filter->pending_packets);
		if (pcki->pck->info.duration && pcki->pck->pid_props->timescale) {
			u64 duration = gf_timestamp_rescale(pcki->pck->info.duration, pcki->pck->pid_props->timescale, 1000000);
			safe_int64_add(&dst->buffer_duration, duration);
		}
		pcki->pck->info.flags |= GF_PCKF_BLOCK_START | GF_PCKF_BLOCK_END;
 		gf_fq_add(dst->packets, pcki);
		return GF_TRUE;
	}

	for (i=0; i<count; i++) {
		GF_FilterPacketInstance *pck = gf_list_get(dst->pck_reassembly, i);
		if (!pck) break;
		gf_assert(! (pck->pck->info.flags & GF_PCKF_BLOCK_START) || ! (pck->pck->info.flags & GF_PCKF_BLOCK_END) );
		size += pck->pck->data_length;
		if (!i) {
			first_offset = byte_offset = pck->pck->info.byte_offset;
			if (byte_offset != GF_FILTER_NO_BO) byte_offset += pck->pck->data_length;
		}else if (byte_offset == pck->pck->info.byte_offset) {
			byte_offset += pck->pck->data_length;
		} else {
			byte_offset = GF_FILTER_NO_BO;
		}
	}

	final = gf_filter_pck_new_alloc(dst->pid, size, &data);
	pos=0;

	for (i=0; i<count; i++) {
		GF_FilterPacket *pck;
		GF_FilterPacketInstance *pcki = gf_list_get(dst->pck_reassembly, i);
		if (!pcki) break;
		pck = pcki->pck;

		if (!pos) {
			info = pck->info;
		} else {
			if (pcki->pck->info.duration > info.duration)
				info.duration = pcki->pck->info.duration;
			if ((pcki->pck->info.dts != GF_FILTER_NO_TS) && pcki->pck->info.dts > info.dts)
				info.dts = pcki->pck->info.dts;
			if ((pcki->pck->info.cts != GF_FILTER_NO_TS) && pcki->pck->info.cts > info.cts)
				info.cts = pcki->pck->info.cts;

			info.flags |= pcki->pck->info.flags;
			if (pcki->pck->info.carousel_version_number > info.carousel_version_number)
				info.carousel_version_number = pcki->pck->info.carousel_version_number;
		}
		if (final)
			memcpy(data+pos, pcki->pck->data, pcki->pck->data_length);

		pos += pcki->pck->data_length;

		if (final) {
			gf_filter_pck_merge_properties(pcki->pck, final);

			//copy the first pid_props non null
			if (pcki->pck->pid_props && !final->pid_props) {
				final->pid_props = pcki->pck->pid_props;
				safe_int_inc(&final->pid_props->reference_count);
			}
		}


		gf_list_rem(dst->pck_reassembly, i);

		//destroy pcki
		if ((i+1<count) || !final) {
			pcki->pck = NULL;
			pcki->pid = NULL;

			if (gf_fq_res_add(pck->pid->filter->pcks_inst_reservoir, pcki)) {
				gf_free(pcki);
			}
		} else {
			pcki->pck = final;
			safe_int_inc(&final->reference_count);
			final->info = info;
			final->info.flags |= GF_PCKF_BLOCK_START | GF_PCKF_BLOCK_END;

			safe_int_inc(&dst->filter->pending_packets);

			if (info.duration && pck->pid_props && pck->pid_props->timescale) {
				u64 duration = gf_timestamp_rescale(info.duration, pck->pid_props->timescale, 1000000);
				safe_int64_add(&dst->buffer_duration, duration);
			}
			//not continous set of bytes reaggregated
			if (byte_offset == GF_FILTER_NO_BO) final->info.byte_offset = GF_FILTER_NO_BO;
			else final->info.byte_offset = first_offset;

			gf_fq_add(dst->packets, pcki);

		}
		//unref pck
		gf_assert(pck->reference_count);
		if (safe_int_dec(&pck->reference_count) == 0) {
			gf_filter_packet_destroy(pck);
		}

		count--;
		i--;
	}
	if (!final) {
		GF_LOG(GF_LOG_ERROR, GF_LOG_FILTER, ("Failed to allocate packet for fragment aggregation\n"));
		return GF_FALSE;
	}
	return GF_TRUE;
}

GF_EXPORT
void gf_filter_pck_discard(GF_FilterPacket *pck)
{
	if (PCK_IS_INPUT(pck)) {
		GF_LOG(GF_LOG_ERROR, GF_LOG_FILTER, ("Attempt to discard input packet on output PID in filter %s\n", pck->pid->filter->name));
		return;
	}
	//function is only used to discard packets allocated but not dispatched, eg with no reference count
	//so don't decrement the counter
	if (pck->reference_count == 0) {
		gf_filter_packet_destroy(pck);
	}
}

GF_Err gf_filter_pck_send_internal(GF_FilterPacket *pck, Bool from_filter)
{
	u32 i, count, nb_dispatch=0;
	GF_FilterPid *pid;
	s64 duration=0;
	u32 timescale=0;
	GF_FilterClockType cktype;
	Bool is_cmd_pck;
#ifdef GPAC_MEMORY_TRACKING
	u32 nb_allocs=0, nb_reallocs=0, prev_nb_allocs=0, prev_nb_reallocs=0;
#endif


	if (!pck->src_filter || (pck->src_filter->removed==1)) {
		gf_filter_pck_discard(pck);
		GF_LOG(GF_LOG_DEBUG, GF_LOG_FILTER, ("Packet sent on already removed filter, ignoring\n"));
		return GF_OK;
	}

#ifdef GPAC_MEMORY_TRACKING
	if (pck->pid->filter->nb_process_since_reset)
		gf_mem_get_stats(&prev_nb_allocs, NULL, &prev_nb_reallocs, NULL);
#endif

	gf_assert(pck);
	gf_assert(pck->pid);
	pid = pck->pid;

	if (PCK_IS_INPUT(pck)) {
		GF_LOG(GF_LOG_ERROR, GF_LOG_FILTER, ("Attempt to dispatch input packet on output PID in filter %s\n", pck->pid->filter->name));
		gf_filter_pck_discard(pck);
		return GF_BAD_PARAM;
	}
	if (pid->not_connected) {
		gf_filter_pck_discard(pck);
		return GF_OK;
	}

	is_cmd_pck = (pck->info.flags & GF_PCK_CMD_MASK);

	//special case for source filters (no input pids), mark as playing once we have a packet sent
	if (!is_cmd_pck && !pid->filter->num_input_pids && !pid->initial_play_done && !pid->is_playing) {
		pid->initial_play_done = GF_TRUE;
		pid->is_playing = GF_TRUE;
		pid->filter->nb_pids_playing++;
	}

	if (pid->filter->eos_probe_state)
		pid->filter->eos_probe_state = 2;

	pid->filter->nb_pck_io++;

	cktype = ( pck->info.flags & GF_PCK_CKTYPE_MASK) >> GF_PCK_CKTYPE_POS;

	//send from filter, update flags
	if (from_filter) {
		Bool is_cmd = (pck->info.flags & GF_PCK_CMD_MASK) ? GF_TRUE : GF_FALSE;
		//not a clock, flush any pending clock
		if (!  (pck->info.flags & GF_PCK_CKTYPE_MASK) ) {
			gf_filter_forward_clock(pck->pid->filter);
		}
		if ( (pck->info.flags & GF_PCK_CMD_MASK) == GF_PCK_CMD_PID_EOS) {
			if (!pid->has_seen_eos) {
				pid->has_seen_eos = GF_TRUE;
				GF_LOG(GF_LOG_DEBUG, GF_LOG_FILTER, ("Filter %s PID %s EOS detected\n", pck->pid->filter->name, pck->pid->name));
			}
		}
		//reset eos only if not a command and not a clock signaling
		else if (pid->has_seen_eos && !is_cmd && !cktype) {
			pid->has_seen_eos = GF_FALSE;
			pid->eos_keepalive = GF_FALSE;
		}


		if (pid->filter->pid_info_changed) {
			pid->filter->pid_info_changed = GF_FALSE;
			pid->pid_info_changed = GF_TRUE;
		}

		//a new property map was created -  flag the packet; don't do this if first packet dispatched on pid
		pck->info.flags &= ~GF_PCKF_PROPS_CHANGED;

		if (!pid->request_property_map && !is_cmd_pck && (pid->nb_pck_sent || pid->props_changed_since_connect) ) {
			GF_LOG(GF_LOG_DEBUG, GF_LOG_FILTER, ("Filter %s PID %s properties modified, marking packet\n", pck->pid->filter->name, pck->pid->name));

			if (pid->filter->user_pid_props)
				gf_filter_pid_set_args(pid->filter, pid);

			pck->info.flags |= GF_PCKF_PROPS_CHANGED;
		}
		//any new pid_set_property after this packet will trigger a new property map
		//note we don't reset if packet is a clock info so that next non-clock packet still has the discontinuity marker
		if (! is_cmd_pck && !cktype) {
			pid->request_property_map = GF_TRUE;
			pid->props_changed_since_connect = GF_FALSE;
		}
		if (pid->pid_info_changed) {
			pck->info.flags |= GF_PCKF_INFO_CHANGED;
			pid->pid_info_changed = GF_FALSE;
		}
	}

	if (pck->pid_props) {
		timescale = pck->pid_props->timescale;
	} else {
		//pid properties applying to this packet are the last defined ones
		pck->pid_props = gf_list_last(pid->properties);
		if (pck->pid_props) {
			safe_int_inc(&pck->pid_props->reference_count);
			timescale = pck->pid_props->timescale;
		}
	}

	if (pid->filter->out_pid_connection_pending
		|| pid->filter->has_pending_pids
		|| pid->init_task_pending
		|| (from_filter && pid->filter->postponed_packets)
	) {
		GF_LOG(GF_LOG_INFO, GF_LOG_FILTER, ("Filter %s PID %s connection pending, queuing packet\n", pck->pid->filter->name, pck->pid->name));
		if (!pid->filter->postponed_packets) pid->filter->postponed_packets = gf_list_new();
		gf_list_add(pid->filter->postponed_packets, pck);

#ifdef GPAC_MEMORY_TRACKING
		if (pck->pid->filter->session->check_allocs) {
			gf_mem_get_stats(&nb_allocs, NULL, &nb_reallocs, NULL);
			pck->pid->filter->session->nb_alloc_pck += (nb_allocs - prev_nb_allocs);
			pck->pid->filter->session->nb_realloc_pck += (nb_reallocs - prev_nb_reallocs);
		}
#endif
		return GF_PENDING_PACKET;
	}
	//now dispatched
	pck->src_filter = NULL;

	gf_assert(pck->pid);
	if (! is_cmd_pck ) {
		pid->nb_pck_sent++;
		if (pck->data_length) {
			pid->filter->nb_pck_sent++;
			pid->filter->nb_bytes_sent += pck->data_length;
		} else if (pck->frame_ifce) {
			pid->filter->nb_hw_pck_sent++;
		}
		if (timescale && (pck->info.cts!=GF_FILTER_NO_TS)) {
			pid->last_ts_sent.num = pck->info.cts;
			pid->last_ts_sent.den = timescale;
		}
	}

	if (cktype == GF_FILTER_CLOCK_PCR_DISC) {
		pid->duration_init = GF_FALSE;
		pid->min_pck_cts = pid->max_pck_cts = 0;
		pid->nb_unreliable_dts = 0;
	}

	if (!cktype) {
		Bool unreliable_dts = GF_FALSE;
		if (pck->info.dts==GF_FILTER_NO_TS) {
			pck->info.dts = pck->info.cts;

			if (pid->recompute_dts) {
				if (pck->info.cts == pid->last_pck_cts) {
					pck->info.dts = pid->last_pck_dts;
				} else {
					s64 min_dur = pck->info.cts;
					min_dur -= (s64) pid->min_pck_cts;
					if (min_dur<0) min_dur*= -1;
					if ((u64) min_dur > pid->min_pck_duration) min_dur = pid->min_pck_duration;
					if (!min_dur) {
						min_dur = 1;
						unreliable_dts = GF_TRUE;
						pid->nb_unreliable_dts++;
					} else if (pid->nb_unreliable_dts) {
						pid->last_pck_dts -= pid->nb_unreliable_dts;
						pid->last_pck_dts += min_dur * pid->nb_unreliable_dts;
						pid->nb_unreliable_dts = 0;
						if (pid->last_pck_dts + min_dur > pck->info.cts) {
							if ((s64) pck->info.cts > min_dur)
 								pid->last_pck_dts = pck->info.cts - min_dur;
							else
								pid->last_pck_dts = 0;
						}
					}
					if (pid->last_pck_dts)
						pck->info.dts = pid->last_pck_dts + min_dur;
				}
			}
		}
		else if (pck->info.cts==GF_FILTER_NO_TS)
			pck->info.cts = pck->info.dts;

		if (pck->info.cts != GF_FILTER_NO_TS) {
			if (! pid->duration_init) {
				pid->last_pck_dts = pck->info.dts;
				pid->last_pck_cts = pck->info.cts;
				pid->max_pck_cts = pid->min_pck_cts = pck->info.cts;
				pid->duration_init = GF_TRUE;
			} else if (!pck->info.duration && !(pck->info.flags & GF_PCKF_DUR_SET) ) {
				if (!unreliable_dts && (pck->info.dts!=GF_FILTER_NO_TS)) {
					duration = pck->info.dts - pid->last_pck_dts;
					if (duration<GF_INT_MIN) duration=GF_INT_MAX;
					else if (duration<0) duration = -duration;
				} else if (pck->info.cts!=GF_FILTER_NO_TS) {
					duration = pck->info.cts - pid->last_pck_cts;
					if (duration<GF_INT_MIN) duration=GF_INT_MAX;
					if (duration<0) duration = -duration;
				}

				if (pid->recompute_dts) {
					if (pck->info.cts > pid->max_pck_cts)
						pid->max_pck_cts = pck->info.cts;
					if (pck->info.cts < pid->max_pck_cts) {
						if (pck->info.cts <= pid->min_pck_cts) {
							pid->min_pck_cts = pck->info.cts;
						} else if (pck->info.cts>pid->last_pck_cts) {
							pid->min_pck_cts = pck->info.cts;
						}
					}
				}

				pid->last_pck_dts = pck->info.dts;
				pid->last_pck_cts = pck->info.cts;

			} else {
				duration = pck->info.duration;
				pid->last_pck_dts = pck->info.dts;
				pid->last_pck_cts = pck->info.cts;
			}
		} else {
			duration = pck->info.duration;
		}

		if (duration) {
			if (!pid->min_pck_duration) pid->min_pck_duration = (u32) duration;
			else if ((u32) duration < pid->min_pck_duration) pid->min_pck_duration = (u32) duration;
		}

		//set duration if pid is not sparse. If sparse (text & co) don't set it because we will likely get it wrong
		if (!pid->is_sparse && !pck->info.duration && pid->min_pck_duration)
			pck->info.duration = (u32) duration;

		//may happen if we don't have DTS, only CTS signaled and B-frames
		if ((s32) pck->info.duration < 0) {
			pck->info.duration = 0;
		}
		pid->last_pck_dur = pck->info.duration;

#ifndef GPAC_DISABLE_LOG
		if (gf_log_tool_level_on(GF_LOG_FILTER, GF_LOG_DEBUG)) {
			u8 sap_type = (pck->info.flags & GF_PCK_SAP_MASK) >> GF_PCK_SAP_POS;
			u8 seek = (pck->info.flags & GF_PCKF_SEEK) ? 1 : 0;
			u8 bstart = (pck->info.flags & GF_PCKF_BLOCK_START) ? 1 : 0;
			u8 bend = (pck->info.flags & GF_PCKF_BLOCK_END) ? 1 : 0;

			if ((pck->info.dts != GF_FILTER_NO_TS) && (pck->info.cts != GF_FILTER_NO_TS) ) {
				GF_LOG(GF_LOG_DEBUG, GF_LOG_FILTER, ("Filter %s PID %s sent packet DTS "LLU" CTS "LLU" SAP %d seek %d duration %d S/E %d/%d size %u\n", pck->pid->filter->name, pck->pid->name, pck->info.dts, pck->info.cts, sap_type, seek, pck->info.duration, bstart, bend, pck->data_length));
			}
			else if ((pck->info.cts != GF_FILTER_NO_TS) ) {
				GF_LOG(GF_LOG_DEBUG, GF_LOG_FILTER, ("Filter %s PID %s sent packet CTS "LLU" SAP %d seek %d duration %d S/E %d/%d size %u\n", pck->pid->filter->name, pck->pid->name, pck->info.cts, sap_type, seek, pck->info.duration, bstart, bend, pck->data_length));
			}
			else if ((pck->info.dts != GF_FILTER_NO_TS) ) {
				GF_LOG(GF_LOG_DEBUG, GF_LOG_FILTER, ("Filter %s PID %s sent packet DTS "LLU" SAP %d seek %d duration %d S/E %d/%d size %u\n", pck->pid->filter->name, pck->pid->name, pck->info.dts, sap_type, seek, pck->info.duration, bstart, bend, pck->data_length));
			} else {
				GF_LOG(GF_LOG_DEBUG, GF_LOG_FILTER, ("Filter %s PID %s sent packet no DTS/PTS SAP %d seek %d duration %d S/E %d/%d size %u\n", pck->pid->filter->name, pck->pid->name, sap_type, seek, pck->info.duration, bstart, bend, pck->data_length));
			}
		}
#endif
	} else {
		pck->info.duration = 0;
		GF_LOG(GF_LOG_DEBUG, GF_LOG_FILTER, ("Filter %s PID %s sent clock reference "LLU"%s\n", pck->pid->filter->name, pck->pid->name, pck->info.cts, (cktype==GF_FILTER_CLOCK_PCR_DISC) ? " - discontinuity detected" : ""));
	}



	//protect packet from destruction - this could happen
	//1) during aggregation of packets
	//2) after dispatching to the packet queue of the next filter, that packet may be consumed
	//by its destination before we are done adding to the other destination
	safe_int_inc(&pck->reference_count);

	gf_assert(pck->pid);
	count = pck->pid->num_destinations;
	//check if processing this packet must be done on main thread (OpenGL interface or source filter asked for this)
	Bool force_main_thread = (pck->info.flags & GF_PCKF_FORCE_MAIN) ? GF_TRUE : GF_FALSE;

	for (i=0; i<count; i++) {
		Bool post_task=GF_FALSE;
		GF_FilterPacketInstance *inst;
		GF_FilterPidInst *dst = gf_list_get(pck->pid->destinations, i);
		if (!dst->filter || dst->filter->finalized || (dst->filter->removed==1) || !dst->filter->freg->process) continue;

		if (dst->discard_inputs==GF_PIDI_DISCARD_ON) {
			//in discard input mode, we drop all input packets but trigger reconfigure as they happen
			if ((pck->info.flags & GF_PCKF_PROPS_CHANGED) && (dst->props != pck->pid_props)) {
				//unassign old property list and set the new one
				if (dst->props) {
					gf_assert(dst->props->reference_count);
					if (safe_int_dec(& dst->props->reference_count) == 0) {
						//see \ref gf_filter_pid_merge_properties_internal for mutex
						gf_mx_p(dst->pid->filter->tasks_mx);
						gf_list_del_item(dst->pid->properties, dst->props);
						gf_mx_v(dst->pid->filter->tasks_mx);
						gf_props_del(dst->props);
					}
				}
				dst->props = pck->pid_props;
				safe_int_inc( & dst->props->reference_count);

				gf_assert(dst->filter->freg->configure_pid);
				//reset the blacklist whenever reconfiguring, since we may need to reload a new filter chain
				//in which a previously blacklisted filter (failing (re)configure for previous state) could
				//now work, eg moving from formatA to formatB then back to formatA
				gf_list_reset(dst->filter->blacklisted);
				dst->discard_inputs = GF_PIDI_DISCARD_RCFG;
				//and post a reconfigure task
				gf_fs_post_task(dst->filter->session, gf_filter_pid_reconfigure_task_discard, dst->filter, (GF_FilterPid *)dst, "pidinst_reconfigure", NULL);
				//keep packets, they will be trashed if we are still in discard when executing gf_filter_pid_reconfigure_task_discard
			} else {
				continue;
			}
		}
		//ignore flush packets if destination requires full blocks and block is in progress
		if (dst->requires_full_data_block && !dst->last_block_ended && (pck->info.flags & GF_PCKF_IS_FLUSH)) {
			continue;
		}
		//stop forwarding clock packets when in EOS
		if (dst->is_end_of_stream && cktype) {
			continue;
		}

		inst = gf_fq_pop(pck->pid->filter->pcks_inst_reservoir);
		if (!inst) {
			GF_SAFEALLOC(inst, GF_FilterPacketInstance);
			if (!inst) return GF_OUT_OF_MEM;
		}
		inst->pck = pck;
		inst->pid = dst;
		inst->pid_props_change_done = 0;
		inst->pid_info_change_done = 0;

		//if packet is forcing main thread processing increase destination filter main_thread
		if (force_main_thread) {
			safe_int_inc(&dst->filter->nb_main_thread_forced);
		}

		if ((inst->pck->info.flags & GF_PCK_CMD_MASK) == GF_PCK_CMD_PID_EOS)  {
			safe_int_inc(&inst->pid->nb_eos_signaled);
		}

		if (!inst->pid->handles_clock_references && cktype) {
			safe_int_inc(&inst->pid->nb_clocks_signaled);
		}

		safe_int_inc(&pck->reference_count);
		nb_dispatch++;

		GF_LOG(GF_LOG_DEBUG, GF_LOG_FILTER, ("Dispatching packet from filter %s to filter %s - %d packet in PID %s buffer ("LLU" us buffer)\n", pid->filter->name, dst->filter->name, gf_fq_count(dst->packets), pid->name, dst->buffer_duration ));

		u64 us_duration = 0;

		if (cktype) {
			safe_int_inc(&dst->filter->pending_packets);
			gf_fq_add(dst->packets, inst);
			post_task = GF_TRUE;
		} else if (dst->requires_full_data_block) {
			if (pck->info.flags & GF_PCKF_BLOCK_START) {
				//missed end of previous, aggregate all before excluding this packet
				if (!dst->last_block_ended) {
					GF_LOG(GF_LOG_DEBUG, GF_LOG_FILTER, ("Filter %s: Missed end of block signaling but got start of block - performing reaggregation\n", pid->filter->name));

					//post process task if we have been reaggregating a packet
					post_task = gf_filter_aggregate_packets(dst);
					if (post_task && pid->nb_reaggregation_pending) pid->nb_reaggregation_pending--;
				}
				dst->last_block_ended = GF_TRUE;
			}
			//block end, aggregate all before and including this packet
			if (pck->info.flags & GF_PCKF_BLOCK_END) {
				//not starting at this packet, append and aggregate
				if (!(pck->info.flags & GF_PCKF_BLOCK_START) && gf_list_count(dst->pck_reassembly) ) {
					//insert packet into reassembly (no need to lock here)
					gf_list_add(dst->pck_reassembly, inst);

					gf_filter_aggregate_packets(dst);
					if (pid->nb_reaggregation_pending) pid->nb_reaggregation_pending--;
				}
				//single block packet, direct dispatch in packet queue (aggregation done before)
				else {
					gf_assert(dst->last_block_ended);
					if (!is_cmd_pck)
						dst->nb_reagg_pck++;

					if (pck->info.duration && timescale) {
						us_duration = gf_timestamp_rescale(pck->info.duration, timescale, 1000000);
						safe_int64_add(&dst->buffer_duration, us_duration);
					}
					inst->pck->info.flags |= GF_PCKF_BLOCK_START;
					safe_int_inc(&dst->filter->pending_packets);
					gf_fq_add(dst->packets, inst);
				}
				dst->last_block_ended = GF_TRUE;
				post_task = GF_TRUE;
			}
			//new block start or continuation
			else {
				if (pck->info.flags & GF_PCKF_BLOCK_START) {
					pid->nb_reaggregation_pending++;
				}

				//if packet mem is hold by filter we must copy the packet since it is no longer
				//consumable until end of block is received, and source might be waiting for this packet to be freed to dispatch further packets
				if (inst->pck->filter_owns_mem) {
					u8 *data;
					u32 alloc_size;
					inst->pck = gf_filter_pck_new_alloc_internal(pck->pid, pck->data_length, &data);
					if (!inst->pck) {
						GF_LOG(GF_LOG_ERROR, GF_LOG_FILTER, ("Filter %s: failed to allocate new packet\n", pid->filter->name));
						continue;
					}

					alloc_size = inst->pck->alloc_size;
					memcpy(inst->pck, pck, sizeof(GF_FilterPacket));
					inst->pck->pck = inst->pck;
					inst->pck->data = data;
					memcpy(inst->pck->data, pck->data, pck->data_length);
					inst->pck->alloc_size = alloc_size;
					inst->pck->filter_owns_mem = 0;
					inst->pck->reference_count = 0;
					inst->pck->reference = NULL;
					inst->pck->destructor = NULL;
					inst->pck->frame_ifce = NULL;
					if (pck->props) {
						GF_Err e;
						inst->pck->props = gf_props_new(pck->pid->filter);
						if (inst->pck->props) {
							e = gf_props_merge_property(inst->pck->props, pck->props, NULL, NULL);
						} else {
							e = GF_OUT_OF_MEM;
						}
						if (e) {
							GF_LOG(GF_LOG_ERROR, GF_LOG_FILTER, ("Filter %s: failed to copy properties for cloned packet: %s\n", pid->filter->name, gf_error_to_string(e) ));
						}
					}
					if (inst->pck->pid_props) {
						safe_int_inc(&inst->pck->pid_props->reference_count);
					}

					gf_assert(pck->reference_count);
					safe_int_dec(&pck->reference_count);
					safe_int_inc(&inst->pck->reference_count);
				}
				//insert packet into reassembly (no need to lock here)
				gf_list_add(dst->pck_reassembly, inst);

				dst->last_block_ended = GF_FALSE;
				//block not complete, don't post process task
			}

		} else {
			u32 pck_dur=0;
			//store start of block info
			if (pck->info.flags & GF_PCKF_BLOCK_START) {
				dst->first_block_started = GF_TRUE;
				pck_dur = pck->info.duration;
			}
			if (pck->info.flags & GF_PCKF_BLOCK_END) {
				//we didn't get a start for this end of block use the packet duration
				if (!dst->first_block_started) {
					pck_dur=pck->info.duration;
				}
				dst->first_block_started = GF_FALSE;
			}

			if (pck_dur && timescale) {
				us_duration = gf_timestamp_rescale(pck_dur, timescale, 1000000);
				safe_int64_add(&dst->buffer_duration, us_duration);
			}
			safe_int_inc(&dst->filter->pending_packets);

			gf_fq_add(dst->packets, inst);
			post_task = GF_TRUE;
		}
		if (post_task) {
			if (!is_cmd_pck) {
				if (dst->is_end_of_stream) {
					dst->is_end_of_stream = GF_FALSE;
					dst->filter->in_eos_resume = GF_TRUE;
				}
				pid->filter->in_eos_resume = GF_FALSE;
			}

			//make sure we lock the tasks mutex before getting the packet count, otherwise we might end up with a wrong number of packets
			//if one thread consumes one packet while the dispatching thread  (the caller here) is still updating the state for that pid
			gf_mx_p(pid->filter->tasks_mx);
			u32 nb_pck = gf_fq_count(dst->packets);
			//update buffer occupancy before dispatching the task - if target pid is processed before we are done disptching his packet, pid buffer occupancy
			//will be updated during packet drop of target
			if (pid->nb_buffer_unit < nb_pck) pid->nb_buffer_unit = nb_pck;
			if ((s64) pid->buffer_duration < dst->buffer_duration) pid->buffer_duration = dst->buffer_duration;
			//if computed duration of packet is larger than pid max_buffer_time, update
			//this is to make sure playback at speed > 1 won't trigger blocking state
			//otherwise we would have max_buffer_time=1ms (default) and a single AU dispatched would block unless speed is AU_DUR_ms/1ms ...
			if (us_duration && pid->max_buffer_time && (pid->max_buffer_time<us_duration))
				pid->max_buffer_time = us_duration;

			gf_mx_v(pid->filter->tasks_mx);

			//post process task
			gf_filter_post_process_task_internal(dst->filter, pid->direct_dispatch);
		}
	}

#ifdef GPAC_MEMORY_TRACKING
	if (pck->pid->filter->session->check_allocs) {
		gf_mem_get_stats(&nb_allocs, NULL, &nb_reallocs, NULL);
		pck->pid->filter->session->nb_alloc_pck += (nb_allocs - prev_nb_allocs);
		pck->pid->filter->session->nb_realloc_pck += (nb_reallocs - prev_nb_reallocs);
	}
#endif

	gf_filter_pid_would_block(pid);

	//unprotect the packet now that it is safely dispatched
	gf_assert(pck->reference_count);
	if (safe_int_dec(&pck->reference_count) == 0) {
		if (!nb_dispatch) {
			if (count) {
				GF_LOG(GF_LOG_DEBUG, GF_LOG_FILTER, ("All destinations of PID %s:%s are in discard mode - discarding\n", pid->filter->name, pid->name));
			} else {
				GF_LOG(GF_LOG_DEBUG, GF_LOG_FILTER, ("PID %s:%s has no destination for packet - discarding\n", pid->filter->name, pid->name));
			}
		}
		gf_filter_packet_destroy(pck);
	}
	return GF_OK;
}

GF_EXPORT
GF_Err gf_filter_pck_send(GF_FilterPacket *pck)
{
	if (PCK_IS_INPUT(pck)) {
		GF_LOG(GF_LOG_ERROR, GF_LOG_FILTER, ("Attempt to send a received packet in filter %s\n", pck->pid->filter->name));
		return GF_BAD_PARAM;
	}
	if (!pck || !pck->pid) return GF_BAD_PARAM;

	//dangling packet
	if (pck->is_dangling) {
		gf_filter_pck_discard(pck);
		return GF_OK;
	}
	return gf_filter_pck_send_internal(pck, GF_TRUE);
}

GF_EXPORT
GF_Err gf_filter_pck_ref(GF_FilterPacket **pck)
{
	if (! pck ) return GF_BAD_PARAM;
	if (! *pck ) return GF_OK;

	(*pck) = (*pck)->pck;
	safe_int_inc(& (*pck)->reference_count);
	//keep track of number of ref packets for this pid at filter level
	safe_int_inc(& (*pck)->pid->filter->nb_ref_packets);
	(*pck)->pid->filter->ref_bytes += (*pck)->data_length;
	return GF_OK;
}

GF_EXPORT
GF_FilterPacket *gf_filter_pck_ref_ex(GF_FilterPacket *pck)
{
	GF_FilterPacket *ref_pck;
	if (! pck ) return NULL;
	ref_pck = pck->pck;
	safe_int_inc(& ref_pck->reference_count);
	//keep track of number of ref packets for this pid at filter level
	safe_int_inc(& ref_pck->pid->filter->nb_ref_packets);
	ref_pck->pid->filter->ref_bytes += ref_pck->data_length;
	return ref_pck;
}

GF_EXPORT
GF_Err gf_filter_pck_ref_props(GF_FilterPacket **pck)
{
	GF_FilterPacket *npck, *srcpck;
	GF_FilterPid *pid;
	if (! pck ) return GF_BAD_PARAM;
	if (! *pck ) return GF_OK;

	srcpck = (*pck)->pck;
	pid = srcpck->pid;

	npck = gf_fq_pop( pid->filter->session->pcks_refprops_reservoir);
	if (!npck) {
		GF_SAFEALLOC(npck, GF_FilterPacket);
		if (!npck) return GF_OUT_OF_MEM;
	}
	npck->pck = npck;
	npck->data = NULL;
	npck->filter_owns_mem = 0;
	npck->destructor = NULL;
	gf_filter_pck_reset_props(npck, pid);
	npck->info = srcpck->info;
	npck->info.flags |= GF_PCKF_PROPS_REFERENCE;
	//keep data size
	npck->data_length = srcpck->data_length;

	if (srcpck->props) {
		npck->props = srcpck->props;
		safe_int_inc(& npck->props->reference_count);
	}
	if (srcpck->pid_props) {
		npck->pid_props = srcpck->pid_props;
		safe_int_inc(& npck->pid_props->pckrefs_reference_count);
	}

	safe_int_inc(& npck->reference_count);
	*pck = npck;
	return GF_OK;
}

GF_EXPORT
void gf_filter_pck_unref(GF_FilterPacket *pck)
{
	gf_assert(pck);
	pck=pck->pck;

	gf_assert(pck->reference_count);
	//decrease number of ref packets for this pid at filter level if not a props reference
	if (! (pck->info.flags & GF_PCKF_PROPS_REFERENCE)) {
		gf_assert(pck->pid->filter->nb_ref_packets);
		safe_int_dec(&pck->pid->filter->nb_ref_packets);
		pck->pid->filter->ref_bytes -= pck->data_length;
	}
	if (safe_int_dec(&pck->reference_count) == 0) {
		gf_filter_packet_destroy(pck);
	}
}

GF_EXPORT
const u8 *gf_filter_pck_get_data(GF_FilterPacket *pck, u32 *size)
{
	gf_assert(pck);
	gf_assert(pck->pck);
	gf_assert(size);
	//get true packet pointer
	pck=pck->pck;
	*size = pck->data_length;
	return (const char *)pck->data;
}

static GF_Err gf_filter_pck_set_property_full(GF_FilterPacket *pck, u32 prop_4cc, const char *prop_name, char *dyn_name, const GF_PropertyValue *value)
{
	u32 hash;
	gf_assert(pck);
	gf_assert(pck->pid);
	if (PCK_IS_INPUT(pck)) {
		GF_LOG(GF_LOG_ERROR, GF_LOG_FILTER, ("Attempt to set property on an input packet in filter %s\n", pck->pid->filter->name));
		return GF_BAD_PARAM;
	}
	//get true packet pointer
	pck=pck->pck;

	if (prop_4cc && value && pck->pid && pck->pid->filter->session->check_props) {
		u32 ptype = gf_props_4cc_get_type(prop_4cc);
		u8 c = prop_4cc>>24;
		if ((c>='A') && (c<='Z')) {
			if (gf_props_get_base_type(ptype) != gf_props_get_base_type(value->type)) {
				GF_LOG(GF_LOG_ERROR, GF_LOG_FILTER, ("Assigning packet property %s of type %s in filter %s but expecting %s\n",
					gf_props_4cc_get_name(prop_4cc),
					gf_props_get_type_name(value->type),
					pck->pid->filter->freg->name,
					gf_props_get_type_name(ptype)
				));
				if (gf_sys_is_test_mode())
					exit(5);
			}
			u32 flags = gf_props_4cc_get_flags(prop_4cc);
			if (!(flags & GF_PROP_FLAG_PCK)) {
				GF_LOG(GF_LOG_ERROR, GF_LOG_FILTER, ("Assigning packet property %s in filter %s but this is a PID property\n",
					gf_props_4cc_get_name(prop_4cc),
					pck->pid->filter->freg->name
				));
				if (gf_sys_is_test_mode())
					exit(5);
			}
		}
	}

	hash = gf_props_hash_djb2(prop_4cc, prop_name ? prop_name : dyn_name);

	if (!pck->props) {
		pck->props = gf_props_new(pck->pid->filter);
	} else {
		gf_props_remove_property(pck->props, hash, prop_4cc, prop_name ? prop_name : dyn_name);
	}
	if (!value) return GF_OK;

	return gf_props_insert_property(pck->props, hash, prop_4cc, prop_name, dyn_name, value);
}

GF_EXPORT
GF_Err gf_filter_pck_set_property(GF_FilterPacket *pck, u32 prop_4cc, const GF_PropertyValue *value)
{
	return gf_filter_pck_set_property_full(pck, prop_4cc, NULL, NULL, value);
}

GF_EXPORT
GF_Err gf_filter_pck_set_property_str(GF_FilterPacket *pck, const char *name, const GF_PropertyValue *value)
{
	return gf_filter_pck_set_property_full(pck, 0, name, NULL, value);
}

GF_EXPORT
GF_Err gf_filter_pck_set_property_dyn(GF_FilterPacket *pck, char *name, const GF_PropertyValue *value)
{
	return gf_filter_pck_set_property_full(pck, 0, NULL, name, value);
}

GF_EXPORT
Bool gf_filter_pck_has_properties(GF_FilterPacket *pck)
{
	//get true packet pointer
	pck = pck->pck;
	if (!pck->props) return GF_FALSE;
	return GF_TRUE;
}

#ifdef GPAC_ENABLE_DEBUG
static GFINLINE const GF_PropertyValue *pck_check_prop(GF_FilterPacket *pck, u32 prop_4cc, const char *prop_name, const GF_PropertyValue *ret)
{
	if (!ret && (pck!=pck->pck)) {
		GF_FilterPacketInstance *pcki = (GF_FilterPacketInstance *)pck;
		if (!(pcki->pid->filter->prop_dump&2)) return ret;
		u32 i, count;
		GF_PropCheck *p;
		if (!pcki->pid->prop_dump) pcki->pid->prop_dump = gf_list_new();
		count = gf_list_count(pcki->pid->prop_dump);
		for (i=0;i<count; i++) {
			p = gf_list_get(pcki->pid->prop_dump, i);
			if (prop_4cc) {
				if (p->p4cc==prop_4cc) return ret;
			} else if (p->name && !strcmp(p->name, prop_name)) {
				return ret;
			}
		}

		const char *pidname = pck->pid->pid->name;
		if (prop_4cc) {
			const char *name = gf_props_4cc_get_name(prop_4cc);
			if (!name) name = "internal";
			GF_LOG(GF_LOG_WARNING, GF_LOG_FILTER, ("Filter %s PID %s input packet property %s (%s) not found\n", pck->pid->filter->name, pidname, gf_4cc_to_str(prop_4cc), name));
		} else if (prop_name) {
			GF_LOG(GF_LOG_WARNING, GF_LOG_FILTER, ("Filter %s PID %s input packet property %s not found\n", pck->pid->filter->name, pidname, prop_name));
		}
		GF_SAFEALLOC(p, GF_PropCheck);
		p->name = prop_name;
		p->p4cc = prop_4cc;
		gf_list_add(pcki->pid->prop_dump, p);
	}
	return ret;
}
#else
#define pck_check_prop(_a, _b, _str, c) c
#endif

GF_EXPORT
const GF_PropertyValue *gf_filter_pck_get_property(GF_FilterPacket *_pck, u32 prop_4cc)
{
	//get true packet pointer
	GF_FilterPacket *pck = _pck->pck;
	if (!pck->props) return NULL;
	if (pck->pid && pck->pid->filter->session->check_props && gf_props_4cc_get_type(prop_4cc)) {
		u32 flags = gf_props_4cc_get_flags(prop_4cc);
		if (!(flags & GF_PROP_FLAG_PCK)) {
			GF_Filter *f = (_pck == pck) ? pck->pid->filter : ((GF_FilterPacketInstance*)_pck)->pid->filter;
			GF_LOG(GF_LOG_ERROR, GF_LOG_FILTER, ("Fetching packet property %s in filter %s but this is a PID property\n",
				gf_props_4cc_get_name(prop_4cc),
				f->freg->name
			));
			if (gf_sys_is_test_mode())
				exit(5);
		}
	}
	return pck_check_prop(_pck, prop_4cc, NULL, gf_props_get_property(pck->props, prop_4cc, NULL));
}

GF_EXPORT
const GF_PropertyValue *gf_filter_pck_get_property_str(GF_FilterPacket *_pck, const char *prop_name)
{
	//get true packet pointer
	GF_FilterPacket *pck = _pck->pck;
	if (!pck->props) return NULL;
	return pck_check_prop(_pck, 0, prop_name, gf_props_get_property(pck->props, 0, prop_name) );
}

GF_EXPORT
const GF_PropertyValue *gf_filter_pck_enum_properties(GF_FilterPacket *pck, u32 *idx, u32 *prop_4cc, const char **prop_name)
{
	if (!pck->pck->props) return NULL;
	return gf_props_enum_property(pck->pck->props, idx, prop_4cc, prop_name);
}

#define PCK_SETTER_CHECK(_pname) \
	if (PCK_IS_INPUT(pck)) { \
		GF_LOG(GF_LOG_ERROR, GF_LOG_FILTER, ("Attempt to set %s on an input packet in filter %s\n", _pname, pck->pid->filter->name));\
		return GF_BAD_PARAM; \
	} \


GF_EXPORT
GF_Err gf_filter_pck_set_framing(GF_FilterPacket *pck, Bool is_start, Bool is_end)
{
	PCK_SETTER_CHECK("framing info")

	if (is_start) pck->info.flags |= GF_PCKF_BLOCK_START;
	else pck->info.flags &= ~GF_PCKF_BLOCK_START;

	if (is_end) pck->info.flags |= GF_PCKF_BLOCK_END;
	else pck->info.flags &= ~GF_PCKF_BLOCK_END;
	return GF_OK;
}

GF_EXPORT
GF_Err gf_filter_pck_get_framing(GF_FilterPacket *pck, Bool *is_start, Bool *is_end)
{
	gf_assert(pck);
	//get true packet pointer
	pck=pck->pck;
	if (is_start) *is_start = (pck->info.flags & GF_PCKF_BLOCK_START) ? GF_TRUE : GF_FALSE;
	if (is_end) *is_end = (pck->info.flags & GF_PCKF_BLOCK_END) ? GF_TRUE : GF_FALSE;
	return GF_OK;
}


GF_EXPORT
GF_Err gf_filter_pck_set_dts(GF_FilterPacket *pck, u64 dts)
{
	PCK_SETTER_CHECK("DTS")
	pck->info.dts = dts;
	return GF_OK;
}

GF_EXPORT
u64 gf_filter_pck_get_dts(GF_FilterPacket *pck)
{
	gf_assert(pck);
	//get true packet pointer
	return pck->pck->info.dts;
}

GF_EXPORT
GF_Err gf_filter_pck_set_cts(GF_FilterPacket *pck, u64 cts)
{
	PCK_SETTER_CHECK("CTS")
	pck->info.cts = cts;
	return GF_OK;
}

GF_EXPORT
u64 gf_filter_pck_get_cts(GF_FilterPacket *pck)
{
	gf_assert(pck);
	//get true packet pointer
	return pck->pck->info.cts;
}

GF_EXPORT
u32 gf_filter_pck_get_timescale(GF_FilterPacket *pck)
{
	gf_assert(pck);
	//get true packet pointer
	return pck->pck->pid_props->timescale ? pck->pck->pid_props->timescale : 1000;
}

GF_EXPORT
GF_Err gf_filter_pck_set_sap(GF_FilterPacket *pck, GF_FilterSAPType sap_type)
{
	PCK_SETTER_CHECK("SAP")
	pck->info.flags &= ~GF_PCK_SAP_MASK;
	pck->info.flags |= (sap_type)<<GF_PCK_SAP_POS;

	return GF_OK;
}

GF_EXPORT
GF_FilterSAPType gf_filter_pck_get_sap(GF_FilterPacket *pck)
{
	gf_assert(pck);
	//get true packet pointer
	return (GF_FilterSAPType) ( (pck->pck->info.flags & GF_PCK_SAP_MASK) >> GF_PCK_SAP_POS);
}

GF_EXPORT
GF_Err gf_filter_pck_set_roll_info(GF_FilterPacket *pck, s16 roll_count)
{
	PCK_SETTER_CHECK("ROLL")
	pck->info.roll = roll_count;
	return GF_OK;
}

GF_EXPORT
s16 gf_filter_pck_get_roll_info(GF_FilterPacket *pck)
{
	gf_assert(pck);
	//get true packet pointer
	return pck->pck->info.roll;
}

GF_EXPORT
GF_Err gf_filter_pck_set_interlaced(GF_FilterPacket *pck, u32 is_interlaced)
{
	PCK_SETTER_CHECK("interlaced")
	pck->info.flags &= ~GF_PCK_ILACE_MASK;
	if (is_interlaced)  pck->info.flags |= is_interlaced<<GF_PCK_ILACE_POS;
	return GF_OK;
}

GF_EXPORT
u32 gf_filter_pck_get_interlaced(GF_FilterPacket *pck)
{
	gf_assert(pck);
	//get true packet pointer
	return (pck->pck->info.flags & GF_PCK_ILACE_MASK) >> GF_PCK_ILACE_POS;
}

GF_EXPORT
GF_Err gf_filter_pck_set_corrupted(GF_FilterPacket *pck, Bool is_corrupted)
{
	PCK_SETTER_CHECK("corrupted")
	pck->info.flags &= ~GF_PCKF_CORRUPTED;
	if (is_corrupted) pck->info.flags |= GF_PCKF_CORRUPTED;
	return GF_OK;
}

GF_EXPORT
Bool gf_filter_pck_get_corrupted(GF_FilterPacket *pck)
{
	gf_assert(pck);
	//get true packet pointer
	return (pck->pck->info.flags & GF_PCKF_CORRUPTED) ? GF_TRUE : GF_FALSE;
}

GF_EXPORT
GF_Err gf_filter_pck_set_duration(GF_FilterPacket *pck, u32 duration)
{
	PCK_SETTER_CHECK("dur")
	pck->info.duration = duration;
	pck->info.flags |= GF_PCKF_DUR_SET;
	return GF_OK;
}

GF_EXPORT
u32 gf_filter_pck_get_duration(GF_FilterPacket *pck)
{
	gf_assert(pck);
	//get true packet pointer
	return pck->pck->info.duration;
}

GF_EXPORT
GF_Err gf_filter_pck_set_seek_flag(GF_FilterPacket *pck, Bool is_seek)
{
	PCK_SETTER_CHECK("seek")
	pck->info.flags &= ~GF_PCKF_SEEK;
	if (is_seek) pck->info.flags |= GF_PCKF_SEEK;
	return GF_OK;
}

GF_EXPORT
Bool gf_filter_pck_get_seek_flag(GF_FilterPacket *pck)
{
	gf_assert(pck);
	//get true packet pointer
	return (pck->pck->info.flags & GF_PCKF_SEEK) ? GF_TRUE : GF_FALSE;
}

GF_EXPORT
GF_Err gf_filter_pck_set_dependency_flags(GF_FilterPacket *pck, u8 dep_flags)
{
	PCK_SETTER_CHECK("dependency_flags")
	pck->info.flags &= ~0xFF;
	pck->info.flags |= dep_flags;
	return GF_OK;
}

GF_EXPORT
u8 gf_filter_pck_get_dependency_flags(GF_FilterPacket *pck)
{
	gf_assert(pck);
	//get true packet pointer
	return pck->pck->info.flags & 0xFF;
}

GF_EXPORT
GF_Err gf_filter_pck_set_carousel_version(GF_FilterPacket *pck, u8 version_number)
{
	PCK_SETTER_CHECK("carousel_version")
	pck->info.carousel_version_number = version_number;
	return GF_OK;
}

GF_EXPORT
u8 gf_filter_pck_get_carousel_version(GF_FilterPacket *pck)
{
	gf_assert(pck);
	//get true packet pointer
	return pck->pck->info.carousel_version_number;
}

GF_EXPORT
GF_Err gf_filter_pck_set_byte_offset(GF_FilterPacket *pck, u64 byte_offset)
{
	PCK_SETTER_CHECK("byteOffset")
	pck->info.byte_offset = byte_offset;
	return GF_OK;
}

GF_EXPORT
u64 gf_filter_pck_get_byte_offset(GF_FilterPacket *pck)
{
	gf_assert(pck);
	//get true packet pointer
	return pck->pck->info.byte_offset;
}

GF_EXPORT
GF_Err gf_filter_pck_set_crypt_flags(GF_FilterPacket *pck, u8 crypt_flag)
{
	PCK_SETTER_CHECK("cryptFlag")
	pck->info.flags &= ~GF_PCK_CRYPT_MASK;
	pck->info.flags |= crypt_flag << GF_PCK_CRYPT_POS;
	return GF_OK;
}

GF_EXPORT
u8 gf_filter_pck_get_crypt_flags(GF_FilterPacket *pck)
{
	//get true packet pointer
	return (pck->pck->info.flags & GF_PCK_CRYPT_MASK) >> GF_PCK_CRYPT_POS;
}

GF_EXPORT
GF_Err gf_filter_pck_set_seq_num(GF_FilterPacket *pck, u32 seq_num)
{
	PCK_SETTER_CHECK("seqNum")
	pck->info.seq_num = seq_num;
	return GF_OK;
}

GF_EXPORT
u32 gf_filter_pck_get_seq_num(GF_FilterPacket *pck)
{
	//get true packet pointer
	return pck->pck->info.seq_num;
}

GF_EXPORT
GF_Err gf_filter_pck_set_clock_type(GF_FilterPacket *pck, GF_FilterClockType ctype)
{
	PCK_SETTER_CHECK("clock_type")
	pck->info.flags &= ~GF_PCK_CKTYPE_MASK;
	pck->info.flags |= ctype << GF_PCK_CKTYPE_POS;
	return GF_OK;
}

GF_EXPORT
GF_FilterClockType gf_filter_pck_get_clock_type(GF_FilterPacket *pck)
{
	//get true packet pointer
	return (pck->pck->info.flags & GF_PCK_CKTYPE_MASK) >> GF_PCK_CKTYPE_POS;
}

GF_EXPORT
GF_FilterFrameInterface *gf_filter_pck_get_frame_interface(GF_FilterPacket *pck)
{
	gf_assert(pck);
	return pck->pck->frame_ifce;
}

GF_EXPORT
GF_Err gf_filter_pck_expand(GF_FilterPacket *pck, u32 nb_bytes_to_add, u8 **data_start, u8 **new_range_start, u32 *new_size)
{
	gf_assert(pck);
	if (PCK_IS_INPUT(pck)) {
		GF_LOG(GF_LOG_ERROR, GF_LOG_FILTER, ("Attempt to reallocate input packet on output PID in filter %s\n", pck->pid->filter->name));
		return GF_BAD_PARAM;
	}
	if (! pck->src_filter && (pck->is_dangling!=1)) {
		GF_LOG(GF_LOG_ERROR, GF_LOG_FILTER, ("Attempt to reallocate an already sent packet in filter %s\n", pck->pid->filter->name));
		return GF_BAD_PARAM;
	}
	if (pck->filter_owns_mem) {
		GF_LOG(GF_LOG_ERROR, GF_LOG_FILTER, ("Attempt to reallocate a shared memory packet in filter %s\n", pck->pid->filter->name));
		return GF_BAD_PARAM;
	}
	if (!data_start && !new_range_start)
		return GF_BAD_PARAM;

	if (pck->data_length + nb_bytes_to_add > pck->alloc_size) {
		pck->alloc_size = pck->data_length + nb_bytes_to_add;
		pck->data = gf_realloc(pck->data, pck->alloc_size);
#ifdef GPAC_MEMORY_TRACKING
		if (!pck->is_dangling)
			pck->pid->filter->session->nb_realloc_pck++;
#endif
	}
	pck->info.byte_offset = GF_FILTER_NO_BO;
	if (data_start) *data_start = pck->data;
	if (new_range_start) *new_range_start = pck->data + pck->data_length;
	pck->data_length += nb_bytes_to_add;
	if (new_size) *new_size = pck->data_length;

	return GF_OK;
}

GF_EXPORT
GF_Err gf_filter_pck_truncate(GF_FilterPacket *pck, u32 size)
{
	gf_assert(pck);
	if (PCK_IS_INPUT(pck)) {
		GF_LOG(GF_LOG_ERROR, GF_LOG_FILTER, ("Attempt to truncate input packet on output PID in filter %s\n", pck->pid->filter->name));
		return GF_BAD_PARAM;
	}
	if (! pck->src_filter) {
		GF_LOG(GF_LOG_ERROR, GF_LOG_FILTER, ("Attempt to truncate an already sent packet in filter %s\n", pck->pid->filter->name));
		return GF_BAD_PARAM;
	}
	if (pck->data_length > size) pck->data_length = size;
	return GF_OK;
}

GF_EXPORT
Bool gf_filter_pck_is_blocking_ref(GF_FilterPacket *pck)
{
	pck = pck->pck;

	while (pck) {
		if (pck->frame_ifce) {
			if (pck->frame_ifce->flags & GF_FRAME_IFCE_BLOCKING)
				return GF_TRUE;
		} else {
			if (pck->destructor && pck->filter_owns_mem)
				return GF_TRUE;
		}
		pck = pck->reference;
	}
	return GF_FALSE;
}

GF_EXPORT
void gf_filter_pck_check_realloc(GF_FilterPacket *pck, u8 *data, u32 size)
{
	if (PCK_IS_INPUT(pck)) return;
	if (((u8*)pck->data != data)
		//in case realloc returned the same address !!
		|| (size > pck->data_length)
	) {
		pck->alloc_size = pck->data_length = size;
		pck->data = data;
	} else {
		pck->data_length = size;
	}
}
