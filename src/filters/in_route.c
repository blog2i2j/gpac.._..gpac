	/*
 *			GPAC - Multimedia Framework C SDK
 *
 *			Authors: Jean Le Feuvre
 *			Copyright (c) Telecom ParisTech 2018-2024
 *					All rights reserved
 *
 *  This file is part of GPAC / ROUTE (ATSC3, DVB-I) input filter
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

#include "in_route.h"

#ifndef GPAC_DISABLE_ROUTE



static GF_FilterProbeScore routein_probe_url(const char *url, const char *mime)
{
	if (!strnicmp(url, "atsc://", 7)) return GF_FPROBE_SUPPORTED;
	if (!strnicmp(url, "route://", 8)) return GF_FPROBE_SUPPORTED;
	if (!strnicmp(url, "mabr://", 7)) return GF_FPROBE_SUPPORTED;
	return GF_FPROBE_NOT_SUPPORTED;
}


static void routein_finalize(GF_Filter *filter)
{
	u32 i;
	ROUTEInCtx *ctx = gf_filter_get_udta(filter);

#ifdef GPAC_ENABLE_COVERAGE
    if (gf_sys_is_cov_mode())
        gf_route_dmx_purge_objects(ctx->route_dmx, 1);
#endif

    if (ctx->clock_init_seg) gf_free(ctx->clock_init_seg);
	if (ctx->route_dmx) gf_route_dmx_del(ctx->route_dmx);

	if (ctx->tsi_outs) {
		while (gf_list_count(ctx->tsi_outs)) {
			TSI_Output *tsio = gf_list_pop_back(ctx->tsi_outs);
			gf_list_del(tsio->pending_repairs);
			if (tsio->dash_rep_id) gf_free(tsio->dash_rep_id);
			gf_free(tsio);
		}
		gf_list_del(ctx->tsi_outs);
	}
	if (ctx->received_seg_names) {
		while (gf_list_count(ctx->received_seg_names)) {
			if (ctx->odir) {
				char *filedel = gf_list_pop_back(ctx->received_seg_names);
				if (filedel) gf_free(filedel);
			} else {
				SegInfo *si = gf_list_pop_back(ctx->received_seg_names);
				gf_free(si->seg_name);
				gf_free(si);
			}
		}
		gf_list_del(ctx->received_seg_names);
	}
	if (!ctx->seg_repair_reservoir && ctx->seg_repair_queue)
		ctx->seg_repair_reservoir = gf_list_new();
	gf_list_transfer(ctx->seg_repair_reservoir, ctx->seg_repair_queue);
	gf_list_del(ctx->seg_repair_queue);
	while (gf_list_count(ctx->repair_servers)) {
		char *tmp = gf_list_pop_back(ctx->repair_servers);
		gf_free(tmp);
	}
	gf_list_del(ctx->repair_servers);
	while (gf_list_count(ctx->seg_repair_reservoir)) {
		RepairSegmentInfo *rsi = gf_list_pop_back(ctx->seg_repair_reservoir);
		if (!ctx->seg_range_reservoir && rsi->ranges)
			ctx->seg_range_reservoir = gf_list_new();
		gf_list_transfer(ctx->seg_range_reservoir, rsi->ranges);
		gf_list_del(rsi->ranges);
		if (rsi->filename) gf_free(rsi->filename);
		gf_free(rsi);
	}
	gf_list_del(ctx->seg_repair_reservoir);

	while (gf_list_count(ctx->seg_range_reservoir)) {
		RouteRepairRange *rr = gf_list_pop_back(ctx->seg_range_reservoir);
		gf_free(rr);
	}
	gf_list_del(ctx->seg_range_reservoir);

	for (i=0; i<ctx->max_sess; i++) {
		if (ctx->http_repair_sessions && ctx->http_repair_sessions[i].dld)
			gf_dm_sess_del(ctx->http_repair_sessions[i].dld);
	}
	gf_free(ctx->http_repair_sessions);
}

static void push_seg_info(ROUTEInCtx *ctx, GF_FilterPid *pid, GF_ROUTEEventFileInfo *finfo)
{
    if (ctx->received_seg_names) {
        SegInfo *si;
        GF_SAFEALLOC(si, SegInfo);
        if (!si) return;
        si->opid = pid;
        si->seg_name = gf_strdup(finfo->filename);
        gf_list_add(ctx->received_seg_names, si);
    }
    while (gf_list_count(ctx->received_seg_names) > ctx->max_segs) {
        GF_FilterEvent evt;
        SegInfo *si = gf_list_pop_front(ctx->received_seg_names);
        GF_FEVT_INIT(evt, GF_FEVT_FILE_DELETE, si->opid);
        evt.file_del.url = si->seg_name;
        gf_filter_pid_send_event(si->opid, &evt);
        gf_free(si->seg_name);
        gf_free(si);
    }
}

static void routein_cleanup_objects(ROUTEInCtx *ctx, u32 service_id)
{
	while (gf_route_dmx_get_object_count(ctx->route_dmx, service_id)>1) {
		if (! gf_route_dmx_remove_first_object(ctx->route_dmx, service_id))
			break;
	}

}

TSI_Output *routein_get_tsio(ROUTEInCtx *ctx, u32 service_id, GF_ROUTEEventFileInfo *finfo)
{
	TSI_Output *tsio;
	if (!finfo->tsi || !ctx->stsi) return NULL;
	u32 i, count = gf_list_count(ctx->tsi_outs);
	for (i=0; i<count; i++) {
		tsio = gf_list_get(ctx->tsi_outs, i);
		if (tsio->sid!=service_id) continue;
		if (tsio->tsi!=finfo->tsi) continue;
		if (!tsio->dash_rep_id && !finfo->dash_rep_id)
			return tsio;
		if (!tsio->dash_rep_id || !finfo->dash_rep_id) continue;
		if (!strcmp(tsio->dash_rep_id, finfo->dash_rep_id))
			return tsio;
	}
	GF_SAFEALLOC(tsio, TSI_Output);
	if (!tsio) return NULL;

	tsio->tsi = finfo->tsi;
	tsio->sid = service_id;
	tsio->dash_rep_id = finfo->dash_rep_id ? gf_strdup(finfo->dash_rep_id) : NULL;
	tsio->pending_repairs = gf_list_new();
	if (ctx->tunein==-3) tsio->delete_first = GF_TRUE;

	gf_list_add(ctx->tsi_outs, tsio);
	return tsio;
}

static void routein_send_file(ROUTEInCtx *ctx, u32 service_id, GF_ROUTEEventFileInfo *finfo, GF_ROUTEEventType evt_type)
{
	u8 *output;
	char *ext;
	GF_FilterPid *pid, **p_pid;
	GF_FilterPacket *pck;
	TSI_Output *tsio = NULL;

	p_pid = &ctx->opid;
	if (finfo && finfo->tsi && ctx->stsi) {
		tsio = routein_get_tsio(ctx, service_id, finfo);
		p_pid = &tsio->opid;

		if ((evt_type==GF_ROUTE_EVT_FILE) || (evt_type==GF_ROUTE_EVT_MPD) || (evt_type==GF_ROUTE_EVT_HLS_VARIANT)) {
			if (ctx->skipr && !finfo->updated) return;
		}
	}
	pid = *p_pid;

	if (!pid) {
		pid = gf_filter_pid_new(ctx->filter);
		(*p_pid) = pid;
		gf_filter_pid_set_property(pid, GF_PROP_PID_STREAM_TYPE, &PROP_UINT(GF_STREAM_FILE));
	}

	if (!tsio || (tsio->current_toi != finfo->toi)) {
		gf_filter_pid_set_property(pid, GF_PROP_PID_ID, &PROP_UINT(tsio ? tsio->tsi : service_id));
		gf_filter_pid_set_property(pid, GF_PROP_PID_SERVICE_ID, &PROP_UINT(service_id));
		if (!finfo) return;

		assert(finfo->filename);
		gf_filter_pid_set_property(pid, GF_PROP_PID_URL, &PROP_STRING(finfo->filename));
		ext = gf_file_ext_start(finfo->filename);
		gf_filter_pid_set_property(pid, GF_PROP_PID_FILE_EXT, &PROP_STRING(ext ? (ext+1) : "*" ));
		if (tsio) {
			tsio->current_toi = finfo->toi;
			tsio->bytes_sent = 0;

			if (finfo->dash_period_id) gf_filter_pid_set_property(pid, GF_PROP_PID_PERIOD_ID, &PROP_STRING(finfo->dash_period_id));
			if (finfo->dash_as_id>=0) gf_filter_pid_set_property(pid, GF_PROP_PID_AS_ID, &PROP_UINT(finfo->dash_as_id));
			if (finfo->dash_rep_id) gf_filter_pid_set_property(pid, GF_PROP_PID_REP_ID, &PROP_STRING(finfo->dash_rep_id));
		}
	}
	//if we split TSIs we need to signal corrupted packets
	if (!tsio && ctx->kc && (finfo->blob->flags & (GF_BLOB_CORRUPTED|GF_BLOB_PARTIAL_REPAIR) )) {
		routein_cleanup_objects(ctx, service_id);
		return;
	}

/*
	//uncomment to disable progressive dispatch
	if (tsio && (evt_type==GF_ROUTE_EVT_DYN_SEG_FRAG))
		return;
*/

	u32 to_write = finfo->blob->size;
	//check progressive mode state when repair is on
	if ((evt_type>=GF_ROUTE_EVT_FILE) && ctx->repair) {
		//we are progressive, so we shall never be called with missing start
		gf_assert(finfo->frags[0].offset == 0);
		if (evt_type != GF_ROUTE_EVT_DYN_SEG_FRAG) {
			//full file, we shall have a single fragment with same size as the file
			gf_assert(finfo->frags[0].size == finfo->blob->size);
		} else if (tsio) {
			//we can only disptach from first block
			to_write = finfo->frags[0].size;
		}
	}

	u32 offset=0;
	if (tsio && tsio->bytes_sent) {
		if (tsio->bytes_sent > to_write) {
			GF_LOG(GF_LOG_ERROR, GF_LOG_ROUTE, ("[%s] Invalid progressive dispatch %u bytes sent but %u max\n", ctx->log_name, tsio->bytes_sent , to_write ));
			//ignored in release, a broken file might be dispatched (typically truncation of file after repair)
			gf_assert(0);
			return;
		}
		offset = tsio->bytes_sent;
		to_write = to_write - tsio->bytes_sent;
	}
	Bool is_end = GF_FALSE;
	if (to_write) {
		pck = gf_filter_pck_new_alloc(pid, to_write, &output);
		if (pck) {
			memcpy(output, finfo->blob->data + offset, to_write);
			if (finfo->blob->flags & (GF_BLOB_CORRUPTED|GF_BLOB_PARTIAL_REPAIR)) {
				gf_filter_pck_set_corrupted(pck, GF_TRUE);
				if (finfo->blob->flags & GF_BLOB_PARTIAL_REPAIR)
					gf_filter_pck_set_property(pck, GF_PROP_PCK_PARTIAL_REPAIR, &PROP_BOOL(GF_TRUE) );
			}

			Bool start = offset==0 ? GF_TRUE : GF_FALSE;
			is_end = (evt_type==GF_ROUTE_EVT_DYN_SEG_FRAG) ? GF_FALSE : GF_TRUE;
			gf_filter_pck_set_framing(pck, start, is_end);
			if (tsio && start)
				gf_filter_pck_set_property(pck, GF_PROP_PCK_FILENUM, &PROP_STRING(finfo->filename));

			gf_filter_pck_send(pck);
		}
		if (tsio) tsio->bytes_sent += to_write;
	} else if (evt_type!=GF_ROUTE_EVT_DYN_SEG_FRAG) {
		if (tsio->bytes_sent) {
			pck = gf_filter_pck_new_alloc(pid, 0, &output);
			if (finfo->blob->flags & (GF_BLOB_CORRUPTED|GF_BLOB_PARTIAL_REPAIR)) {
				gf_filter_pck_set_corrupted(pck, GF_TRUE);
				if (finfo->blob->flags & GF_BLOB_PARTIAL_REPAIR)
					gf_filter_pck_set_property(pck, GF_PROP_PCK_PARTIAL_REPAIR, &PROP_BOOL(GF_TRUE) );
			}

			gf_filter_pck_set_framing(pck, GF_FALSE, GF_TRUE);
			gf_filter_pck_send(pck);
			is_end = GF_TRUE;
		}
	}
	//release current TOI in case we have data from next segment being progressively dispatched
	if (tsio && is_end)
		tsio->current_toi = 0;

	if (ctx->max_segs && (evt_type==GF_ROUTE_EVT_DYN_SEG))
		push_seg_info(ctx, pid, finfo);

	routein_cleanup_objects(ctx, service_id);
}

static void routein_write_to_disk(ROUTEInCtx *ctx, u32 service_id, GF_ROUTEEventFileInfo *finfo, u32 evt_type)
{
	char szPath[GF_MAX_PATH];
	FILE *out;
	if (!finfo->blob)
		return;

	if ((finfo->blob->flags & GF_BLOB_CORRUPTED) && !ctx->kc) {
		routein_cleanup_objects(ctx, service_id);
		return;
	}

	sprintf(szPath, "%s/service%d/%s", ctx->odir, service_id, finfo->filename);

	out = gf_fopen(szPath, "wb");
	if (!out) {
		GF_LOG(GF_LOG_ERROR, GF_LOG_ROUTE, ("[%s] Service %d failed to create MPD file %s\n", ctx->log_name, service_id, szPath ));
	} else {
		u32 bytes = (u32) gf_fwrite(finfo->blob->data, finfo->blob->size, out);
		gf_fclose(out);
		if (bytes != finfo->blob->size) {
			GF_LOG(GF_LOG_ERROR, GF_LOG_ROUTE, ("[%s] Service %d failed to write file %s:  %d written for %d total\n", ctx->log_name, service_id, finfo->filename, bytes, finfo->blob->size));
		}
	}

	routein_cleanup_objects(ctx, service_id);

	if (ctx->max_segs && (evt_type==GF_ROUTE_EVT_DYN_SEG)) {
		gf_list_add(ctx->received_seg_names, gf_strdup(szPath));

		while (gf_list_count(ctx->received_seg_names) > ctx->max_segs) {
			char *filedel = gf_list_pop_front(ctx->received_seg_names);
			if (filedel) {
				gf_file_delete(filedel);
				gf_free(filedel);
			}
		}
	}
}

void routein_on_event_file(ROUTEInCtx *ctx, GF_ROUTEEventType evt, u32 evt_param, GF_ROUTEEventFileInfo *finfo, Bool is_defer_repair, Bool drop_if_first)
{
	char szPath[GF_MAX_PATH];
	char *mime;
	u32 nb_obj;
	Bool is_init = GF_TRUE;
	Bool is_loop = GF_FALSE;
	DownloadedCacheEntry cache_entry;
	ctx->evt_interrupt = GF_TRUE;

	gf_assert(finfo->blob);
	gf_mx_p(finfo->blob->mx);
	//set blob flags
	if (evt==GF_ROUTE_EVT_DYN_SEG_FRAG)
		finfo->blob->flags |= GF_BLOB_IN_TRANSFER;
	else
		finfo->blob->flags &= ~GF_BLOB_IN_TRANSFER;

	if (finfo->partial==GF_LCTO_PARTIAL_ANY)
		finfo->blob->flags |= GF_BLOB_CORRUPTED;
	else
		finfo->blob->flags &= ~GF_BLOB_CORRUPTED;
	gf_mx_v(finfo->blob->mx);

	cache_entry = finfo->udta;
	szPath[0] = 0;
	switch (evt) {
	case GF_ROUTE_EVT_MPD:
	case GF_ROUTE_EVT_HLS_VARIANT:
		if (!ctx->tune_time) ctx->tune_time = gf_sys_clock();

		if (ctx->odir) {
			routein_write_to_disk(ctx, evt_param, finfo, evt);
			break;
		}
		if (!ctx->gcache) {
			routein_send_file(ctx, evt_param, finfo, evt);
			break;
		}
		sprintf(szPath, "http://gmcast/service%d/%s", evt_param, finfo->filename);
		mime = finfo->mime ? (char*)finfo->mime : "application/dash+xml";
		//also set x-mcast header to all manifest and variant
		//if a clock info is present, also add it
		cache_entry = gf_dm_add_cache_entry(ctx->dm, szPath, finfo->blob, 0, 0, mime, GF_TRUE, 0);
		if (ctx->clock_init_seg) {
			char szHdr[GF_MAX_PATH];
			sprintf(szHdr, "x-mcast: yes\r\nx-mcast-first-seg: %s\r\n", ctx->clock_init_seg);
			gf_dm_force_headers(ctx->dm, cache_entry, szHdr);
		} else {
			gf_dm_force_headers(ctx->dm, cache_entry, "x-mcast: yes\r\n");
		}

		if (evt==GF_ROUTE_EVT_MPD) {
			char *fext = finfo->filename ? gf_file_ext_start(finfo->filename) : NULL;
			if (fext) fext++;
			else fext = "mpd";

			if (!ctx->opid) {
				ctx->opid = gf_filter_pid_new(ctx->filter);
				gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_STREAM_TYPE, &PROP_UINT(GF_STREAM_FILE));
			}
			gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_ID, &PROP_UINT(evt_param));
			gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_SERVICE_ID, &PROP_UINT(evt_param));
			gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_FILE_EXT, &PROP_STRING(fext));
			gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_MIME, &PROP_STRING(mime));
			gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_REDIRECT_URL, &PROP_STRING(szPath));
			gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_URL, &PROP_STRING(szPath));
			//remember the cache entry
			gf_route_dmx_set_service_udta(ctx->route_dmx, evt_param, cache_entry);
			ctx->sync_tsi = 0;
			ctx->last_toi = 0;
			ctx->tune_service_id = evt_param;
		}
		break;
	case GF_ROUTE_EVT_DYN_SEG:

		if (ctx->odir) {
			routein_write_to_disk(ctx, evt_param, finfo, evt);
			break;
		}
		if (!ctx->gcache) {
			routein_send_file(ctx, evt_param, finfo, evt);
			break;
		}
		//reset of clock sync is done at each cache discard, for other case reset at each new file
		if (!ctx->gcache && ctx->clock_init_seg) {
			gf_free(ctx->clock_init_seg);
			ctx->clock_init_seg = NULL;
		}
		//fallthrough

    case GF_ROUTE_EVT_DYN_SEG_FRAG:
        //for now we only write complete files
        if (ctx->odir) {
            break;
        }
        //no cache, write complete files unless stsi is set (for low latency file forwarding)
        if (!ctx->gcache) {
			if (ctx->stsi)
				routein_send_file(ctx, evt_param, finfo, evt);
            break;
        }

		if (!ctx->clock_init_seg
			//if full seg push of previously advertized init, reset x-mcast-ll header
			|| ((evt==GF_ROUTE_EVT_DYN_SEG) && !strcmp(ctx->clock_init_seg, finfo->filename))
		) {
			//store current seg if LL mode or full seg - MPD cache entry may still be null
			//if MPD is sent after segment in the broadcast
			if (!ctx->clock_init_seg && ((evt==GF_ROUTE_EVT_DYN_SEG) || ctx->llmode))
				ctx->clock_init_seg = gf_strdup(finfo->filename);

			DownloadedCacheEntry mpd_cache_entry = gf_route_dmx_get_service_udta(ctx->route_dmx, evt_param);
			if (mpd_cache_entry) {
				sprintf(szPath, "x-mcast: yes\r\nx-mcast-first-seg: %s\r\n", ctx->clock_init_seg);
				if (evt==GF_ROUTE_EVT_DYN_SEG_FRAG)
					strcat(szPath, "x-mcast-ll: yes\r\n");
				gf_dm_force_headers(ctx->dm, mpd_cache_entry, szPath);
				szPath[0] = 0;
			}
		}

		if ((finfo->blob->flags & GF_BLOB_CORRUPTED) && !ctx->kc)
            break;

		is_init = GF_FALSE;
		if (!ctx->sync_tsi) {
			ctx->sync_tsi = finfo->tsi;
			ctx->last_toi = finfo->toi;
			if (drop_if_first) {
				break;
			}
		} else if (!is_defer_repair && (ctx->sync_tsi == finfo->tsi)) {
			if (ctx->cloop && (ctx->last_toi > finfo->toi + 100)) {
				GF_LOG(GF_LOG_WARNING, GF_LOG_ROUTE, ("[%s] Loop detected on service %d for TSI %u: prev TOI %u this toi %u\n", ctx->log_name, ctx->tune_service_id, finfo->tsi, ctx->last_toi, finfo->toi));

				gf_route_dmx_purge_objects(ctx->route_dmx, evt_param);
				is_loop = GF_TRUE;
				if (cache_entry) {
					if (ctx->clock_init_seg) gf_free(ctx->clock_init_seg);
					ctx->clock_init_seg = gf_strdup(finfo->filename);
					sprintf(szPath, "x-mcast: yes\r\nx-mcast-first-seg: %s\r\nx-mcast-loop: yes\r\n", ctx->clock_init_seg);
					gf_dm_force_headers(ctx->dm, cache_entry, szPath);
					szPath[0] = 0;
				}
			}
			ctx->last_toi = finfo->toi;
		}
		//fallthrough

	case GF_ROUTE_EVT_FILE:

		if (ctx->odir) {
			routein_write_to_disk(ctx, evt_param, finfo, evt);
			break;
		}
		if (!ctx->gcache) {
			routein_send_file(ctx, evt_param, finfo, evt);
			break;
		}

		if ((finfo->blob->flags & GF_BLOB_CORRUPTED) && !ctx->kc) return;

		if (!ctx->llmode && (evt==GF_ROUTE_EVT_DYN_SEG_FRAG))
			return;

		if (!cache_entry) {
			sprintf(szPath, "http://gmcast/service%d/%s", evt_param, finfo->filename);
			//we copy over the init segment, but only share the data pointer for segments
			cache_entry = gf_dm_add_cache_entry(ctx->dm, szPath, finfo->blob, 0, 0, finfo->mime ? finfo->mime : "video/mp4", is_init ? GF_TRUE : GF_FALSE, finfo->download_ms);
			if (cache_entry) {
				gf_dm_force_headers(ctx->dm, cache_entry, "x-mcast: yes\r\n");
				finfo->udta = cache_entry;
			}
		}

        if (evt==GF_ROUTE_EVT_DYN_SEG_FRAG) {
			break;
        }
		finfo->blob->flags &=~ GF_BLOB_IN_TRANSFER;

		GF_LOG(GF_LOG_INFO, GF_LOG_ROUTE, ("[%s] Pushing file %s to cache\n", ctx->log_name, finfo->filename));
		if (ctx->max_segs && (evt==GF_ROUTE_EVT_DYN_SEG))
			push_seg_info(ctx, ctx->opid, finfo);

		if (is_loop) break;

		//cf routein_local_cache_probe
		gf_filter_lock(ctx->filter, GF_TRUE);
		nb_obj = gf_route_dmx_get_object_count(ctx->route_dmx, evt_param);
		while (nb_obj > ctx->nbcached) {
			if (!gf_route_dmx_remove_first_object(ctx->route_dmx, evt_param))
				break;
			nb_obj = gf_route_dmx_get_object_count(ctx->route_dmx, evt_param);
		}
		gf_filter_lock(ctx->filter, GF_FALSE);
		break;
	default:
		break;
	}
}

void routein_on_event(void *udta, GF_ROUTEEventType evt, u32 evt_param, GF_ROUTEEventFileInfo *finfo)
{
	ROUTEInCtx *ctx = (ROUTEInCtx *)udta;
	ctx->evt_interrupt = GF_TRUE;

	//events without finfo
	if (evt==GF_ROUTE_EVT_SERVICE_FOUND) {
		if (!ctx->tune_time) ctx->tune_time = gf_sys_clock();
		//special case when not using cache, create output pid to announce service ID asap
		if (ctx->stsi) {
			routein_send_file(ctx, evt_param, NULL, evt);
		}
		return;
	}
	if (evt==GF_ROUTE_EVT_SERVICE_SCAN) {
		if (ctx->tune_service_id && !gf_route_dmx_find_atsc3_service(ctx->route_dmx, ctx->tune_service_id)) {

			GF_LOG(GF_LOG_ERROR, GF_LOG_ROUTE, ("[%s] Asked to tune to service %d but no such service, tuning to first one\n", ctx->log_name, ctx->tune_service_id));

			ctx->tune_service_id = 0;
			gf_route_atsc3_tune_in(ctx->route_dmx, (u32) -2, GF_TRUE);
		}
		return;
	}
	if (!finfo)
		return;

	if (evt==GF_ROUTE_EVT_FILE_DELETE) {
		routein_repair_mark_file(ctx, evt_param, finfo->filename, GF_TRUE);
		return;
	}

	//partial, try to repair
	if (ctx->repair && (finfo->partial || ctx->stsi)) {
		//blob flags are set there
		routein_queue_repair(ctx, evt, evt_param, finfo);
	} else {
		routein_on_event_file(ctx, evt, evt_param, finfo, GF_FALSE, GF_FALSE);
	}
}

static Bool routein_local_cache_probe(void *par, char *url, Bool is_destroy)
{
	ROUTEInCtx *ctx = (ROUTEInCtx *)par;
	u32 sid=0;
	char *subr;
	if (strncmp(url, "http://gmcast/service", 21)) return GF_FALSE;

	subr = strchr(url+21, '/');
	subr[0] = 0;
	sid = atoi(url+21);
	subr[0] = '/';
	//this is not a thread-safe callback (typically called from httpin filter)
	gf_filter_lock(ctx->filter, GF_TRUE);
	if (is_destroy) {
		GF_LOG(GF_LOG_DEBUG, GF_LOG_ROUTE, ("[%s] Cache releasing object %s\n", ctx->log_name, url));
		gf_route_dmx_remove_object_by_name(ctx->route_dmx, sid, subr+1, GF_TRUE);
		//for non real-time netcap, we may need to reschedule processing
		gf_filter_post_process_task(ctx->filter);
		if (ctx->clock_init_seg) {
			gf_free(ctx->clock_init_seg);
			ctx->clock_init_seg = NULL;
		}
	} else if (sid && (sid != ctx->tune_service_id)) {
		GF_LOG(GF_LOG_INFO, GF_LOG_ROUTE, ("[%s] Request on service %d but tuned on service %d, retuning\n", ctx->log_name, sid, ctx->tune_service_id));
		ctx->tune_service_id = sid;
		ctx->sync_tsi = 0;
		ctx->last_toi = 0;
		if (ctx->clock_init_seg) gf_free(ctx->clock_init_seg);
		ctx->clock_init_seg = NULL;
        gf_route_atsc3_tune_in(ctx->route_dmx, sid, GF_TRUE);
	} else {
		GF_LOG(GF_LOG_DEBUG, GF_LOG_ROUTE, ("[%s] Cache accessing object %s\n", ctx->log_name, url));
		routein_repair_mark_file(ctx, sid, subr+1, GF_FALSE);
		//mark object as in-use to prevent it from being discarded
		gf_route_dmx_force_keep_object_by_name(ctx->route_dmx, sid, subr+1);
	}
	gf_filter_lock(ctx->filter, GF_FALSE);
	return GF_TRUE;
}

static void routein_set_eos(GF_Filter *filter, ROUTEInCtx *ctx, Bool no_reset)
{
	u32 i, nb_out = gf_filter_get_opid_count(filter);
	for (i=0; i<nb_out; i++) {
		GF_FilterPid *opid = gf_filter_get_opid(filter, i);
		if (opid) gf_filter_pid_set_eos(opid);
	}
	if (ctx->opid) {
		gf_filter_pid_set_info_str(ctx->opid, "x-mcast-over", &PROP_STRING("yes") );
	}
	if (!no_reset)
		gf_route_dmx_reset_all(ctx->route_dmx);
}

static GF_Err routein_process(GF_Filter *filter)
{
	GF_Err e;
	u32 resched = 50000;
	ROUTEInCtx *ctx = gf_filter_get_udta(filter);

	if (!ctx->nb_playing) {
		e = routein_do_repair(ctx);
		if (e==GF_IP_NETWORK_EMPTY) {
			gf_filter_ask_rt_reschedule(filter, 4000);
			return GF_OK;
		}
		return e;
	}

	ctx->evt_interrupt = GF_FALSE;

	u32 nb_calls=0;
	while (1) {
		e = gf_route_dmx_process(ctx->route_dmx);
		if (e == GF_IP_NETWORK_EMPTY) {
			if (ctx->tune_time) {
				if (!ctx->last_timeout) ctx->last_timeout = gf_sys_clock();
				else {
					u32 diff = gf_sys_clock() - ctx->last_timeout;
					if (diff > ctx->timeout) {
						GF_LOG(GF_LOG_INFO, GF_LOG_ROUTE, ("[%s] No data for %u ms, aborting\n", ctx->log_name, diff));
						routein_set_eos(filter, ctx, GF_FALSE);
						return GF_EOS;
					}
				}
			}
			if (nb_calls==0) {
				gf_route_dmx_check_timeouts(ctx->route_dmx);
			}
			//with decent buffer size >=50kB we should sustain at least 80 mbps per multicast stream with 5ms reschedule
			if (gf_route_dmx_has_active_multicast(ctx->route_dmx))
				resched = 5000;
			break;
		} else if (!e) {
			ctx->last_timeout = 0;
			if (!ctx->tune_time) ctx->start_time = gf_sys_clock();

			if (ctx->evt_interrupt) break;
			//uncomment these to slow down demuxer (useful when debugging low latency mode)
//			gf_filter_ask_rt_reschedule(filter, 10000);
//			break;
		} else if (e==GF_EOS) {
			e = routein_do_repair(ctx);
			//this only happens when reading from pcap, do not reset route demuxer as we want to parse all segments present in capture
			if (e == GF_EOS)
				routein_set_eos(filter, ctx, GF_TRUE);
			return e;
		} else {
			break;
		}
		nb_calls++;
	}
	if (!ctx->tune_time) {
	 	u32 diff = gf_sys_clock() - ctx->start_time;
	 	if (diff>ctx->timeout) {
			GF_LOG(GF_LOG_ERROR, GF_LOG_ROUTE, ("[%s] No data for %u ms, aborting\n", ctx->log_name, diff));
			gf_filter_setup_failure(filter, GF_IP_UDP_TIMEOUT);
			routein_set_eos(filter, ctx, GF_FALSE);
			return GF_EOS;
		}
	}

	GF_Err e_repair = routein_do_repair(ctx);
	if ((e_repair==GF_IP_NETWORK_EMPTY) && (e == GF_IP_NETWORK_EMPTY))
		gf_filter_ask_rt_reschedule(filter, resched);
	else if ((e_repair==GF_IP_NETWORK_EMPTY) || (e == GF_IP_NETWORK_EMPTY))
		gf_filter_ask_rt_reschedule(filter, 4000);

	if (ctx->stats) {
		u32 now = gf_sys_clock() - ctx->start_time;
		if (now >= ctx->nb_stats*ctx->stats) {
			ctx->nb_stats+=1;
			if (gf_filter_reporting_enabled(filter)) {
				Double rate=0.0;
				char szRpt[1024];

				u64 st = gf_route_dmx_get_first_packet_time(ctx->route_dmx);
				u64 et = gf_route_dmx_get_last_packet_time(ctx->route_dmx);
				u64 nb_pck = gf_route_dmx_get_nb_packets(ctx->route_dmx);
				u64 nb_bytes = gf_route_dmx_get_recv_bytes(ctx->route_dmx);

				et -= st;
				if (et) {
					rate = (Double)nb_bytes*8;
					rate /= et;
				}
				sprintf(szRpt, "[%us] "LLU" bytes "LLU" packets in "LLU" ms rate %.02f mbps", now/1000, nb_bytes, nb_pck, et/1000, rate);
				gf_filter_update_status(filter, 0, szRpt);
			}
		}
	}

	return GF_OK;
}


static GF_Err routein_initialize(GF_Filter *filter)
{
	Bool is_atsc = GF_TRUE;
	Bool is_mabr = GF_FALSE;
	u32 prot_offset=0;
	ROUTEInCtx *ctx = gf_filter_get_udta(filter);
	ctx->filter = filter;

	if (!ctx->src) return GF_BAD_PARAM;
	ctx->log_name = "ATSC3";

	if (!strncmp(ctx->src, "route://", 8)) {
		is_atsc = GF_FALSE;
		prot_offset = 8;
		ctx->log_name = "ROUTE";
	} else if (!strncmp(ctx->src, "mabr://", 7)){
		is_atsc = GF_FALSE;
		is_mabr = GF_TRUE;
		prot_offset = 7;
		ctx->log_name = "DVB-MABR";
	} else if (strcmp(ctx->src, "atsc://"))
		return GF_BAD_PARAM;

	if (ctx->odir) {
		ctx->gcache = GF_FALSE;
	}

	if (ctx->gcache) {
		ctx->dm = gf_filter_get_download_manager(filter);
		if (!ctx->dm) return GF_SERVICE_ERROR;
		gf_dm_set_localcache_provider(ctx->dm, routein_local_cache_probe, ctx);
	} else if (!ctx->stsi) {
		ctx->fullseg = GF_TRUE;
	}
	if (!ctx->nbcached)
		ctx->nbcached = 1;

	if (is_atsc) {
		ctx->route_dmx = gf_route_atsc_dmx_new_ex(ctx->ifce, ctx->buffer, gf_filter_get_netcap_id(filter), routein_on_event, ctx);
	} else {
		char *sep, *root;
		u32 port;
		sep = strrchr(ctx->src+prot_offset, ':');
		if (!sep) {
			GF_LOG(GF_LOG_ERROR, GF_LOG_ROUTE, ("[%s] Missing port number\n", ctx->log_name));
			return GF_BAD_PARAM;
		}
		sep[0] = 0;
		root = strchr(sep+1, '/');
		if (root) root[0] = 0;
		port = atoi(sep+1);
		if (root) root[0] = '/';

		if (!gf_sk_is_multicast_address(ctx->src+prot_offset)) {
			GF_LOG(GF_LOG_WARNING, GF_LOG_ROUTE, ("[%s] %s is not a multicast address\n", ctx->log_name, ctx->src));
		}

		if (is_mabr)
			ctx->route_dmx = gf_dvb_mabr_dmx_new(ctx->src+prot_offset, port, ctx->ifce, ctx->buffer, gf_filter_get_netcap_id(filter), routein_on_event, ctx);
		else
			ctx->route_dmx = gf_route_dmx_new_ex(ctx->src+prot_offset, port, ctx->ifce, ctx->buffer, gf_filter_get_netcap_id(filter), routein_on_event, ctx);
		sep[0] = ':';
	}
	if (!ctx->route_dmx) return GF_SERVICE_ERROR;

	//not using cache, we must dispatch in a progressive way for now.
	//TODO: add repair to allow out of order disptach
	if (!ctx->gcache && ctx->llmode) {
		ctx->llmode = GF_FALSE;
	}
	if (ctx->gcache) ctx->stsi = GF_FALSE;

	//if llmode do out of order
	//if split TSI with repair, we need out of order dispatch: because in tune-in the first segment is partial
	//it may not be advertized until timeout/end in progressive mode which could happen after next segment start of reception
	if (ctx->llmode || (ctx->stsi && ctx->repair)) {
		gf_route_set_dispatch_mode(ctx->route_dmx, GF_ROUTE_DISPATCH_OUT_OF_ORDER);
	} else {
		gf_route_set_dispatch_mode(ctx->route_dmx, ctx->fullseg ? GF_ROUTE_DISPATCH_FULL : GF_ROUTE_DISPATCH_PROGRESSIVE);
	}
	gf_route_dmx_set_reorder(ctx->route_dmx, ctx->reorder, ctx->rtimeout);

	if (ctx->tsidbg) {
		gf_route_dmx_debug_tsi(ctx->route_dmx, ctx->tsidbg);
	}

	if (ctx->tunein>0) ctx->tune_service_id = ctx->tunein;

	if (is_atsc || is_mabr) {
        GF_LOG(GF_LOG_DEBUG, GF_LOG_ROUTE, ("[%s] Tunein started\n", ctx->log_name));
		if (ctx->tune_service_id)
            gf_route_atsc3_tune_in(ctx->route_dmx, ctx->tune_service_id, GF_FALSE);
		else
            gf_route_atsc3_tune_in(ctx->route_dmx, (u32) ctx->tunein, GF_TRUE);
	}

	ctx->start_time = gf_sys_clock();
	if (ctx->minrecv>100) ctx->minrecv = 100;

	if (ctx->stsi) ctx->tsi_outs = gf_list_new();
	if (ctx->max_segs)
		ctx->received_seg_names = gf_list_new();

	ctx->nb_playing = 1;
	ctx->initial_play_forced = GF_TRUE;
	if (ctx->repair_urls.nb_items > 0) {
		u8 i;
		ctx->repair = ROUTEIN_REPAIR_FULL;
		ctx->repair_servers = gf_list_new();
		for(i=0; i<ctx->repair_urls.nb_items; i++) {
			RouteRepairServer* server;
			GF_SAFEALLOC(server, RouteRepairServer);
			server->accept_ranges = RANGE_SUPPORT_PROBE;
			server->is_up = GF_TRUE;
			server->support_h2 = GF_TRUE;
			server->url = ctx->repair_urls.vals[i];
			gf_list_add(ctx->repair_servers, server);
		}
	}

	if ((ctx->repair == ROUTEIN_REPAIR_FULL) || ctx->stsi) {
		if (!ctx->max_sess) ctx->max_sess = 1;
		//we need at least one session in fast repair mode
		else if (ctx->repair < ROUTEIN_REPAIR_FULL) ctx->max_sess = 1;

		ctx->http_repair_sessions = gf_malloc(sizeof(RouteRepairSession)*ctx->max_sess);
		memset(ctx->http_repair_sessions, 0, sizeof(RouteRepairSession)*ctx->max_sess);

		ctx->seg_repair_queue = gf_list_new();
		ctx->seg_repair_reservoir = gf_list_new();
		ctx->seg_range_reservoir = gf_list_new();
	} else {
		ctx->max_sess = 0;
	}
	//TODO, pass any repair URL info coming from broadcast
	if (!ctx->repair_servers) {
		if (ctx->repair >= ROUTEIN_REPAIR_FULL)
			ctx->repair = ROUTEIN_REPAIR_STRICT;
	}
	return GF_OK;
}

static Bool routein_process_event(GF_Filter *filter, const GF_FilterEvent *evt)
{
	ROUTEInCtx *ctx = gf_filter_get_udta(filter);
	if (evt->base.type==GF_FEVT_PLAY) {
		if (!ctx->initial_play_forced)
			ctx->nb_playing++;
		ctx->initial_play_forced = GF_FALSE;
	} else if (evt->base.type==GF_FEVT_STOP) {
		ctx->nb_playing--;
	} else if (evt->base.type==GF_FEVT_DASH_QUALITY_SELECT) {
		if (!ctx->dynsel) return GF_TRUE;

		gf_route_dmx_mark_active_quality(ctx->route_dmx, evt->dash_select.service_id, evt->dash_select.period_id, evt->dash_select.as_id, evt->dash_select.rep_id, (evt->dash_select.select_type==GF_QUALITY_SELECTED) ? GF_TRUE : GF_FALSE);
	}
	return GF_TRUE;
}

GF_Err gf_route_dmx_has_object_by_name(GF_ROUTEDmx *routedmx, u32 service_id, const char *fileName);
Bool routein_is_valid_url(GF_Filter *filter, u32 service_id, const char *url)
{
	ROUTEInCtx *ctx = gf_filter_get_udta(filter);
	GF_Err e = gf_route_dmx_has_object_by_name(ctx->route_dmx, service_id, url);
	if (e==GF_OK)
		return GF_TRUE;
	return GF_FALSE;
}

#define OFFS(_n)	#_n, offsetof(ROUTEInCtx, _n)
static const GF_FilterArgs ROUTEInArgs[] =
{
	{ OFFS(src), "URL of source content", GF_PROP_NAME, NULL, NULL, 0},
	{ OFFS(ifce), "default interface to use for multicast. If NULL, the default system interface will be used", GF_PROP_STRING, NULL, NULL, GF_FS_ARG_HINT_ADVANCED},
	{ OFFS(gcache), "indicate the files should populate GPAC HTTP cache", GF_PROP_BOOL, "true", NULL, GF_FS_ARG_HINT_ADVANCED},
	{ OFFS(tunein), "service ID to bootstrap on. Special values:\n"
	"- 0: tune to no service\n"
	"- -1: tune all services\n"
	"- -2: tune on first service found\n"
	"- -3: detect all services and do not join multicast", GF_PROP_SINT, "-2", NULL, 0},
	{ OFFS(buffer), "receive buffer size to use in bytes", GF_PROP_UINT, "0x80000", NULL, GF_FS_ARG_HINT_ADVANCED},
	{ OFFS(timeout), "timeout in ms after which tunein fails", GF_PROP_UINT, "5000", NULL, 0},
    { OFFS(nbcached), "number of segments to keep in cache per service", GF_PROP_UINT, "8", NULL, GF_FS_ARG_HINT_EXPERT},
	{ OFFS(kc), "keep corrupted file", GF_PROP_BOOL, "false", NULL, GF_FS_ARG_HINT_ADVANCED},
	{ OFFS(skipr), "skip repeated files (ignored in cache mode)", GF_PROP_BOOL, "true", NULL, GF_FS_ARG_HINT_ADVANCED},
	{ OFFS(stsi), "define one output PID per tsi/serviceID (ignored in cache mode)", GF_PROP_BOOL, "false", NULL, GF_FS_ARG_HINT_EXPERT},
	{ OFFS(stats), "log statistics at the given rate in ms (0 disables stats)", GF_PROP_UINT, "1000", NULL, GF_FS_ARG_HINT_ADVANCED},
	{ OFFS(tsidbg), "gather only objects with given TSI (debug)", GF_PROP_UINT, "0", NULL, GF_FS_ARG_HINT_EXPERT},
	{ OFFS(max_segs), "maximum number of segments to keep on disk", GF_PROP_UINT, "0", NULL, GF_FS_ARG_HINT_EXPERT},
	{ OFFS(odir), "output directory for standalone mode", GF_PROP_STRING, NULL, NULL, GF_FS_ARG_HINT_ADVANCED},
	{ OFFS(reorder), "consider packets are not always in order - if false, this will evaluate an LCT object as done when TOI changes", GF_PROP_BOOL, "true", NULL, GF_FS_ARG_HINT_EXPERT},
	{ OFFS(cloop), "check for loops based on TOI (used for capture replay)", GF_PROP_BOOL, "false", NULL, 0},
	{ OFFS(rtimeout), "default timeout in µs to wait when gathering out-of-order packets", GF_PROP_UINT, "500000", NULL, GF_FS_ARG_HINT_EXPERT},
	{ OFFS(fullseg), "only dispatch full segments in cache mode (always true for other modes)", GF_PROP_BOOL, "false", NULL, GF_FS_ARG_HINT_ADVANCED},
	{ OFFS(repair), "repair mode for corrupted files\n"
		"- no: no repair is performed\n"
		"- simple: simple repair is performed (incomplete `mdat` boxes will be kept)\n"
		"- strict: incomplete mdat boxes will be lost as well as preceding `moof` boxes\n"
		"- full: HTTP-based repair of all lost packets"
		, GF_PROP_UINT, "strict", "no|simple|strict|full", GF_FS_ARG_HINT_EXPERT},
	{ OFFS(repair_urls), "repair servers urls", GF_PROP_STRING_LIST, NULL, NULL, 0},
	{ OFFS(max_sess), "max number of concurrent HTTP repair sessions", GF_PROP_UINT, "1", NULL, 0},
	{ OFFS(llmode), "enable low-latency access", GF_PROP_BOOL, "true", NULL, 0},
	{ OFFS(dynsel), "dynamically enable and disable multicast groups based on their selection state", GF_PROP_BOOL, "true", NULL, 0},
	{ OFFS(range_merge), "merge ranges in HTTP repair if distant from less than given amount of bytes", GF_PROP_UINT, "10000", NULL, 0},
	{ OFFS(minrecv), "redownload full file in HTTP repair if received bytes is less than given percentage of file size", GF_PROP_UINT, "20", NULL, 0},
	{0}
};

static const GF_FilterCapability ROUTEInCaps[] =
{
	CAP_UINT(GF_CAPS_OUTPUT,  GF_PROP_PID_STREAM_TYPE, GF_STREAM_FILE),
};

GF_FilterRegister ROUTEInRegister = {
	.name = "routein",
	GF_FS_SET_DESCRIPTION("MABR & ROUTE input")
#ifndef GPAC_DISABLE_DOC
	.help = "This filter is a receiver for file delivery over multicast. It currently supports ATSC 3.0, generic ROUTE and DVB-MABR flute.\n"
	"- ATSC 3.0 mode is identified by the URL `atsc://`.\n"
	"- Generic ROUTE mode is identified by the URL `route://IP:PORT`.\n"
	"- DVB-MABR mode is identified by the URL `mabr://IP:PORT` pointing to the bootstrap FLUTE channel carrying the multicast gateway configuration.\n"
	"\n"
	"The filter can work in cached mode, source mode or standalone mode.\n"
	"# Cached mode\n"
	"The cached mode is the default filter behavior. It populates GPAC HTTP Cache with the received files, using `http://gmcast/serviceN/` as service root, `N being the multicast service ID.\n"
	"In cached mode, repeated files are always pushed to cache.\n"
	"The maximum number of media segment objects in cache per service is defined by [-nbcached](); this is a safety used to force object removal in case DASH client timing is wrong and some files are never requested at cache level.\n"
	"  \n"
	"The cached MPD is assigned the following headers:\n"
	"- `x-mcast`: boolean value, if `yes` indicates the file comes from a multicast.\n"
	"- `x-mcast-first-seg`: string value, indicates the name of the first segment (completely or currently being) retrieved from the broadcast.\n"
    "- `x-mcast-ll`: boolean value, if yes indicates that the indicated first segment is currently being received (low latency signaling).\n"
    "- `x-mcast-loop`: boolean value, if yes indicates a loop (e.g. pcap replay) in the service has been detected - only checked if [-cloop]() is set.\n"
	"  \n"
	"The cached files are assigned the following headers:\n"
	"- `x-mcast`: boolean value, if `yes` indicates the file comes from a multicast.\n"
	"\n"
	"If [-max_segs]() is set, file deletion event will be triggered in the filter chain.\n"
	"\n"
	"# Source mode\n"
	"In source mode, the filter outputs files on a single output PID of type `file`. "
	"The files are dispatched once fully received, the output PID carries a sequence of complete files. Repeated files are not sent unless requested.\n"
	"EX gpac -i atsc://gcache=false -o $ServiceID$/$File$:dynext\n"
	"This will grab the files and forward them as output PIDs, consumed by the [fout](fout) filter.\n"
	"\n"
	"If needed, one PID per TSI can be used rather than a single PID using [-stsi](). This avoids mixing files of different mime types on the same PID (e.g. HAS manifest and ISOBMFF).\n"
	"In this mode, each packet starting a new file carries the file name as a property. If [-repair]() is enabled in this mode, progressive dispatch of files will be done.\n"
	"\n"
	"If [-max_segs]() is set, file deletion event will be triggered in the filter chain.\n"
	"Note: The [-nbcached]() option is ignored in this mode.\n"
	"\n"
	"# Standalone mode\n"
	"In standalone mode, the filter does not produce any output PID and writes received files to the [-odir]() directory.\n"
	"EX gpac -i atsc://:odir=output\n"
	"This will grab the files and write them to `output` directory.\n"
	"\n"
	"In this mode, files are always written once completely recieved, regardless of the [-repair]() option.\n"
	"\n"
	"If [-max_segs]() is set, old files will be deleted.\n"
	"Note: The [-nbcached]() option is ignored in this mode.\n"
	"\n"
	"# File Repair\n"
	"In case of losses or incomplete segment reception (during tune-in), the files are patched as follows:\n"
	"- MPEG-2 TS: all lost ranges are adjusted to 188-bytes boundaries, and transformed into NULL TS packets.\n"
	"- ISOBMFF: all top-level boxes are scanned, and incomplete boxes are transformed in `free` boxes, except `mdat`:\n"
	" - if `repair=simple`, `mdat` is kept if incomplete (broken file),\n"
	" - if `repair=strict`, `mdat` is moved to `free` if incomplete and the preceeding `moof` is also moved to `free`.\n"
	"\n"
	"If [-kc]() option is set, corrupted files will be kept. If [-fullseg]() is not set and files are only partially received, they will be kept.\n"
	"\n"
	"# Interface setup\n"
	"On some systems (OSX), when using VM packet replay, you may need to force multicast routing on your local interface.\n"
	"For ATSC, you will have to do this for the base signaling multicast (224.0.23.60):\n"
	"EX route add -net 224.0.23.60/32 -interface vboxnet0\n"
	"Then for each multicast service in the multicast:\n"
	"EX route add -net 239.255.1.4/32 -interface vboxnet0\n"
	"",
#endif //GPAC_DISABLE_DOC
	.private_size = sizeof(ROUTEInCtx),
	.args = ROUTEInArgs,
	.initialize = routein_initialize,
	.finalize = routein_finalize,
	SETCAPS(ROUTEInCaps),
	.process = routein_process,
	.process_event = routein_process_event,
	.probe_url = routein_probe_url,
	.hint_class_type = GF_FS_CLASS_NETWORK_IO
};

const GF_FilterRegister *routein_register(GF_FilterSession *session)
{
	if (gf_opts_get_bool("temp", "get_proto_schemes")) {
		gf_opts_set_key("temp_in_proto", ROUTEInRegister.name, "atsc,route,mabr");
	}
	return &ROUTEInRegister;
}

#else

const GF_FilterRegister *routein_register(GF_FilterSession *session)
{
	return NULL;
}

#endif /* GPAC_DISABLE_ROUTE */
