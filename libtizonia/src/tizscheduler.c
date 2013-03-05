/**
 * Copyright (C) 2011-2013 Aratelia Limited - Juan A. Rubio
 *
 * This file is part of Tizonia
 *
 * Tizonia is free software: you can redistribute it and/or modify it under the
 * terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * Tizonia is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with Tizonia.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file   tizscheduler.c
 * @author Juan A. Rubio <juan.rubio@aratelia.com>
 *
 * @brief  Tizonia OpenMAX IL - Scheduler
 *
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <assert.h>
#include <string.h>

#include "OMX_Core.h"
#include "OMX_Component.h"
#include "OMX_Types.h"

#include "tizosal.h"
#include "tizutils.h"
#include "tizscheduler.h"
#include "tizfsm.h"
#include "tizkernel.h"
#include "tizproc.h"
#include "tizport.h"
#include "OMX_TizoniaExt.h"

#ifdef TIZ_LOG_CATEGORY_NAME
#undef TIZ_LOG_CATEGORY_NAME
#define TIZ_LOG_CATEGORY_NAME "tiz.tizonia.scheduler"
#endif

#define SCHED_OMX_DEFAULT_ROLE "default"

#include <execinfo.h>
#include <stdio.h>
#include <stdlib.h>

/* Obtain a backtrace and print it to stdout. */
static void
print_trace (void)
{
  void *array[30];
  size_t size;
  char **strings;
  size_t i;
     
  size = backtrace (array, 30);
  strings = backtrace_symbols (array, size);
     
  printf ("Obtained %zd stack frames.\n", size);
     
  for (i = 0; i < size; i++)
    printf ("%s\n", strings[i]);
     
  free (strings);
}

typedef enum tizsched_msg_class tizsched_msg_class_t;
enum tizsched_msg_class
{
  ETIZSchedMsgComponentInit = 0,
  ETIZSchedMsgComponentDeInit,
  ETIZSchedMsgGetComponentVersion,
  ETIZSchedMsgSendCommand,
  ETIZSchedMsgGetParameter,
  ETIZSchedMsgSetParameter,
  ETIZSchedMsgGetConfig,
  ETIZSchedMsgSetConfig,
  ETIZSchedMsgGetExtensionIndex,
  ETIZSchedMsgGetState,
  ETIZSchedMsgComponentTunnelRequest,
  ETIZSchedMsgUseBuffer,
  ETIZSchedMsgAllocateBuffer,
  ETIZSchedMsgFreeBuffer,
  ETIZSchedMsgEmptyThisBuffer,
  ETIZSchedMsgFillThisBuffer,
  ETIZSchedMsgSetCallbacks,
  ETIZSchedMsgUseEGLImage,
  ETIZSchedMsgComponentRoleEnum,
  ETIZSchedMsgPluggableEvent,
  ETIZSchedMsgRegisterRoles,
  ETIZSchedMsgRegisterPortHooks,
  ETIZSchedMsgEvIo,
  ETIZSchedMsgEvTimer,
  ETIZSchedMsgEvStat,
  ETIZSchedMsgMax,
};

typedef struct tizsched_msg_getcomponentversion
  tizsched_msg_getcomponentversion_t;
struct tizsched_msg_getcomponentversion
{
  OMX_STRING p_comp_name;
  OMX_VERSIONTYPE *p_comp_version;
  OMX_VERSIONTYPE *p_spec_version;
  OMX_UUIDTYPE *p_comp_uuid;
};

typedef struct tizsched_msg_componentroleenum
  tizsched_msg_componentroleenum_t;
struct tizsched_msg_componentroleenum
{
  OMX_U8 *p_role;
  OMX_U32 index;
};

typedef struct tizsched_msg_setget_paramconfig
  tizsched_msg_setget_paramconfig_t;
struct tizsched_msg_setget_paramconfig
{
  OMX_INDEXTYPE index;
  OMX_PTR p_struct;
};

typedef struct tizsched_msg_getextindex tizsched_msg_getextindex_t;
struct tizsched_msg_getextindex
{
  OMX_STRING p_ext_name;
  OMX_INDEXTYPE *p_index;
};

typedef struct tizsched_msg_sendcommand tizsched_msg_sendcommand_t;
struct tizsched_msg_sendcommand
{
  OMX_COMMANDTYPE cmd;
  OMX_U32 param1;
  OMX_PTR p_cmd_data;
};

typedef struct tizsched_msg_setcallbacks tizsched_msg_setcallbacks_t;
struct tizsched_msg_setcallbacks
{
  OMX_CALLBACKTYPE *p_cbacks;
  OMX_PTR p_appdata;
};

typedef struct tizsched_msg_getstate tizsched_msg_getstate_t;
struct tizsched_msg_getstate
{
  OMX_STATETYPE *p_state;
};

typedef struct tizsched_msg_usebuffer tizsched_msg_usebuffer_t;
struct tizsched_msg_usebuffer
{
  OMX_BUFFERHEADERTYPE **pp_hdr;
  OMX_U32 pid;
  OMX_PTR p_app_priv;
  OMX_U32 size;
  OMX_U8 *p_buf;
};

typedef struct tizsched_msg_allocbuffer tizsched_msg_allocbuffer_t;
struct tizsched_msg_allocbuffer
{
  OMX_BUFFERHEADERTYPE **pp_hdr;
  OMX_U32 pid;
  OMX_PTR p_app_priv;
  OMX_U32 size;
};

typedef struct tizsched_msg_freebuffer tizsched_msg_freebuffer_t;
struct tizsched_msg_freebuffer
{
  OMX_U32 pid;
  OMX_BUFFERHEADERTYPE *p_hdr;
};

typedef struct tizsched_msg_emptyfillbuffer tizsched_msg_emptyfillbuffer_t;
struct tizsched_msg_emptyfillbuffer
{
  OMX_BUFFERHEADERTYPE *p_hdr;
};

typedef struct tizsched_msg_tunnelrequest tizsched_msg_tunnelrequest_t;
struct tizsched_msg_tunnelrequest
{
  OMX_U32 pid;
  OMX_HANDLETYPE p_thdl;
  OMX_U32 tpid;
  OMX_TUNNELSETUPTYPE *p_tsetup;
};

typedef struct tizsched_msg_plg_event tizsched_msg_plg_event_t;
struct tizsched_msg_plg_event
{
  tizevent_t *p_event;
};

typedef struct tizsched_msg_regroles tizsched_msg_regroles_t;
struct tizsched_msg_regroles
{
  OMX_U32 nroles;
  const tiz_role_factory_t **p_role_list;
};

typedef struct tizsched_msg_regphooks tizsched_msg_regphooks_t;
struct tizsched_msg_regphooks
{
  OMX_U32 pid;
  const tiz_port_alloc_hooks_t *p_hooks;
  tiz_port_alloc_hooks_t *p_old_hooks;
};

typedef struct tizsched_msg_ev_io tizsched_msg_ev_io_t;
struct tizsched_msg_ev_io
{
  tiz_event_io_t * p_ev_io;
  int fd;
  int events;
};

typedef struct tizsched_msg_ev_timer tizsched_msg_ev_timer_t;
struct tizsched_msg_ev_timer
{
  tiz_event_timer_t * p_ev_timer;
};

typedef struct tizsched_msg_ev_stat tizsched_msg_ev_stat_t;
struct tizsched_msg_ev_stat
{
  tiz_event_stat_t * p_ev_stat;
  int events;
};

typedef struct tizsched_msg tizsched_msg_t;
struct tizsched_msg
{
  OMX_HANDLETYPE p_hdl;
  OMX_BOOL will_block;
  OMX_BOOL may_block;
  OMX_S32 peer_pos;
  tizsched_msg_class_t class;
  union
  {
    tizsched_msg_getcomponentversion_t gcv;
    tizsched_msg_componentroleenum_t cre;
    tizsched_msg_setget_paramconfig_t sgpc;
    tizsched_msg_getextindex_t gei;
    tizsched_msg_sendcommand_t scmd;
    tizsched_msg_setcallbacks_t scbs;
    tizsched_msg_getstate_t gs;
    tizsched_msg_usebuffer_t ub;
    tizsched_msg_allocbuffer_t ab;
    tizsched_msg_freebuffer_t fb;
    tizsched_msg_emptyfillbuffer_t efb;
    tizsched_msg_tunnelrequest_t tr;
    tizsched_msg_plg_event_t pe;
    tizsched_msg_regroles_t rr;
    tizsched_msg_regphooks_t rph;
    tizsched_msg_ev_io_t eio;
    tizsched_msg_ev_timer_t etmr;
    tizsched_msg_ev_stat_t estat;
  };
};

/* Forward declarations */
static OMX_ERRORTYPE do_init (tiz_scheduler_t *, tizsched_state_t *,
                              tizsched_msg_t *);
static OMX_ERRORTYPE do_deinit (tiz_scheduler_t *, tizsched_state_t *,
                                tizsched_msg_t *);
static OMX_ERRORTYPE do_getcv (tiz_scheduler_t *, tizsched_state_t *,
                               tizsched_msg_t *);
static OMX_ERRORTYPE do_scmd (tiz_scheduler_t *, tizsched_state_t *,
                              tizsched_msg_t *);
static OMX_ERRORTYPE do_gparam (tiz_scheduler_t *, tizsched_state_t *,
                                tizsched_msg_t *);
static OMX_ERRORTYPE do_sparam (tiz_scheduler_t *, tizsched_state_t *,
                                tizsched_msg_t *);
static OMX_ERRORTYPE do_gconfig (tiz_scheduler_t *, tizsched_state_t *,
                                 tizsched_msg_t *);
static OMX_ERRORTYPE do_sconfig (tiz_scheduler_t *, tizsched_state_t *,
                                 tizsched_msg_t *);
static OMX_ERRORTYPE do_gei (tiz_scheduler_t *, tizsched_state_t *,
                             tizsched_msg_t *);
static OMX_ERRORTYPE do_gs (tiz_scheduler_t *, tizsched_state_t *,
                            tizsched_msg_t *);
static OMX_ERRORTYPE do_tr (tiz_scheduler_t *, tizsched_state_t *,
                            tizsched_msg_t *);
static OMX_ERRORTYPE do_ub (tiz_scheduler_t *, tizsched_state_t *,
                            tizsched_msg_t *);
static OMX_ERRORTYPE do_ab (tiz_scheduler_t *, tizsched_state_t *,
                            tizsched_msg_t *);
static OMX_ERRORTYPE do_fb (tiz_scheduler_t *, tizsched_state_t *,
                            tizsched_msg_t *);
static OMX_ERRORTYPE do_etb (tiz_scheduler_t *, tizsched_state_t *,
                             tizsched_msg_t *);
static OMX_ERRORTYPE do_ftb (tiz_scheduler_t *, tizsched_state_t *,
                             tizsched_msg_t *);
static OMX_ERRORTYPE do_scbs (tiz_scheduler_t *, tizsched_state_t *,
                              tizsched_msg_t *);
static OMX_ERRORTYPE do_cre (tiz_scheduler_t *, tizsched_state_t *,
                             tizsched_msg_t *);
static OMX_ERRORTYPE do_plgevt (tiz_scheduler_t *, tizsched_state_t *,
                                tizsched_msg_t *);
static OMX_ERRORTYPE do_rr (tiz_scheduler_t *, tizsched_state_t *,
                            tizsched_msg_t *);
static OMX_ERRORTYPE do_rph (tiz_scheduler_t *, tizsched_state_t *,
                             tizsched_msg_t *);
static OMX_ERRORTYPE do_eio (tiz_scheduler_t *, tizsched_state_t *,
                             tizsched_msg_t *);
static OMX_ERRORTYPE do_etmr (tiz_scheduler_t *, tizsched_state_t *,
                              tizsched_msg_t *);
static OMX_ERRORTYPE do_estat (tiz_scheduler_t *, tizsched_state_t *,
                               tizsched_msg_t *);

static OMX_ERRORTYPE init_servants (tiz_scheduler_t *, tizsched_msg_t *);
static OMX_ERRORTYPE deinit_servants (tiz_scheduler_t *, tizsched_msg_t *);
static OMX_ERRORTYPE init_and_register_role (tiz_scheduler_t *,
                                             const OMX_U32);
static tiz_scheduler_t *instantiate_scheduler (OMX_HANDLETYPE, const char *);
static OMX_ERRORTYPE start_scheduler (tiz_scheduler_t *);
static void delete_scheduler (tiz_scheduler_t *);


typedef OMX_ERRORTYPE (*tizsched_msg_dispatch_f) (tiz_scheduler_t * ap_sched,
                                                  tizsched_state_t * ap_state,
                                                  tizsched_msg_t * ap_msg);
static const tizsched_msg_dispatch_f tizsched_msg_to_fnt_tbl[] = {
  do_init,
  do_deinit,
  do_getcv,
  do_scmd,
  do_gparam,
  do_sparam,
  do_gconfig,
  do_sconfig,
  do_gei,
  do_gs,
  do_tr,
  do_ub,
  do_ab,
  do_fb,
  do_etb,
  do_ftb,
  do_scbs,
  NULL,                         /* ETIZSchedMsgUseEGLImage */
  do_cre,
  do_plgevt,
  do_rr,
  do_rph,
  do_eio,
  do_etmr,
  do_estat,
};

static OMX_BOOL
dispatch_msg (tiz_scheduler_t * ap_sched,
              tizsched_state_t * ap_state, tizsched_msg_t * ap_msg);

typedef struct tizsched_msg_str tizsched_msg_str_t;
struct tizsched_msg_str
{
  tizsched_msg_class_t msg;
  OMX_STRING str;
};

tizsched_msg_str_t tizsched_msg_to_str_tbl[] = {
  {ETIZSchedMsgComponentInit, "ETIZSchedMsgComponentInit"},
  {ETIZSchedMsgGetComponentVersion, "ETIZSchedMsgGetComponentVersion"},
  {ETIZSchedMsgSendCommand, "ETIZSchedMsgSendCommand"},
  {ETIZSchedMsgGetParameter, "ETIZSchedMsgGetParameter"},
  {ETIZSchedMsgSetParameter, "ETIZSchedMsgSetParameter"},
  {ETIZSchedMsgGetConfig, "ETIZSchedMsgGetConfig"},
  {ETIZSchedMsgSetConfig, "ETIZSchedMsgSetConfig"},
  {ETIZSchedMsgGetExtensionIndex, "ETIZSchedMsgGetExtensionIndex"},
  {ETIZSchedMsgGetState, "ETIZSchedMsgGetState"},
  {ETIZSchedMsgComponentTunnelRequest, "ETIZSchedMsgComponentTunnelRequest"},
  {ETIZSchedMsgUseBuffer, "ETIZSchedMsgUseBuffer"},
  {ETIZSchedMsgAllocateBuffer, "ETIZSchedMsgAllocateBuffer"},
  {ETIZSchedMsgFreeBuffer, "ETIZSchedMsgFreeBuffer"},
  {ETIZSchedMsgEmptyThisBuffer, "ETIZSchedMsgEmptyThisBuffer"},
  {ETIZSchedMsgFillThisBuffer, "ETIZSchedMsgFillThisBuffer"},
  {ETIZSchedMsgSetCallbacks, "ETIZSchedMsgSetCallbacks"},
  {ETIZSchedMsgComponentDeInit, "ETIZSchedMsgComponentDeInit"},
  {ETIZSchedMsgUseEGLImage, "ETIZSchedMsgUseEGLImage"},
  {ETIZSchedMsgComponentRoleEnum, "ETIZSchedMsgComponentRoleEnum"},
  {ETIZSchedMsgPluggableEvent, "ETIZSchedMsgPluggableEvent"},
  {ETIZSchedMsgRegisterRoles, "ETIZSchedMsgRegisterRoles"},
  {ETIZSchedMsgRegisterPortHooks, "ETIZSchedMsgRegisterPortHooks"},
  {ETIZSchedMsgMax, "ETIZSchedMsgMax"},
};

static const OMX_STRING
tizsched_msg_to_str (const tizsched_msg_class_t a_msg)
{
  const OMX_S32 count =
    sizeof (tizsched_msg_to_str_tbl) / sizeof (tizsched_msg_str_t);
  OMX_S32 i = 0;

  for (i = 0; i < count; ++i)
    {
      if (tizsched_msg_to_str_tbl[i].msg == a_msg)
        {
          return tizsched_msg_to_str_tbl[i].str;
        }
    }

  return "Unknown scheduler message";
}

static void
delete_roles (tiz_scheduler_t * ap_sched)
{
  OMX_U32 i = 0;

  assert (NULL != ap_sched);

  for (i = 0; i < ap_sched->child.nroles; ++i)
    {
      tiz_mem_free (ap_sched->child.p_role_list[i]);
    }

  tiz_mem_free (ap_sched->child.p_role_list);

  ap_sched->child.p_role_list = NULL;
  ap_sched->child.nroles = 0;
}

static OMX_ERRORTYPE
store_roles (tiz_scheduler_t * ap_sched,
             const tizsched_msg_regroles_t * ap_msg_regroles)
{
  OMX_ERRORTYPE rc = OMX_ErrorNone;
  tiz_role_factory_t *p_rf = NULL;
  OMX_U32 i = 0;

  if (NULL == (ap_sched->child.p_role_list
               = tiz_mem_calloc (ap_msg_regroles->nroles,
                                 sizeof (tiz_role_factory_t *))))
    {
      TIZ_LOG_CNAME (TIZ_LOG_TRACE, TIZ_CNAME (ap_sched->child.p_hdl),
                     TIZ_CBUF (ap_sched->child.p_hdl),
                     "[OMX_ErrorInsufficientResources] : list of roles - "
                     "Failed when making local copy ..");
      rc = OMX_ErrorInsufficientResources;
    }

  for (i = 0; i < ap_msg_regroles->nroles && rc == OMX_ErrorNone; ++i)
    {
      p_rf = tiz_mem_calloc (1, sizeof (tiz_role_factory_t));
      if (p_rf)
        {
          memcpy (p_rf, ap_msg_regroles->p_role_list[i],
                  sizeof (tiz_role_factory_t));
          ap_sched->child.p_role_list[i] = p_rf;
          ap_sched->child.nroles++;
        }
      else
        {
          rc = OMX_ErrorInsufficientResources;
          TIZ_LOG_CNAME (TIZ_LOG_TRACE, TIZ_CNAME (ap_sched->child.p_hdl),
                         TIZ_CBUF (ap_sched->child.p_hdl),
                         "[OMX_ErrorInsufficientResources] : list of roles - "
                         "Failed when making local copy ..");
        }
    }

  if (OMX_ErrorNone != rc)
    {
      /* Clean-up */
      delete_roles (ap_sched);
    }

  return rc;
}

static inline OMX_ERRORTYPE
send_msg_blocking (tiz_scheduler_t * ap_sched, tizsched_msg_t * ap_msg)
{
  assert (NULL != ap_msg);
  assert (NULL != ap_sched);

  ap_msg->will_block = OMX_TRUE;

  tiz_queue_send (ap_sched->p_queue, ap_msg);

  tiz_sem_wait (&(ap_sched->sem));

  return ap_sched->error;
}

static inline OMX_ERRORTYPE
send_msg_non_blocking (tiz_scheduler_t * ap_sched, tizsched_msg_t * ap_msg)
{
  assert (NULL != ap_msg);
  assert (NULL != ap_sched);

  ap_msg->will_block = OMX_FALSE;

  TIZ_LOG_CNAME (TIZ_LOG_ERROR, TIZ_CNAME (ap_sched->child.p_hdl),
                 TIZ_CBUF (ap_sched->child.p_hdl),
                 "[%s] - Sent non-blocking...",
                 tizsched_msg_to_str (ap_msg->class));

  tiz_queue_send (ap_sched->p_queue, ap_msg);

  return ap_sched->error;
}

static OMX_S32
register_peer_handle (tiz_scheduler_t * ap_sched,
                      const OMX_S32 a_peer_pos, const OMX_HANDLETYPE a_hdl)
{
  peer_info_t *p_peer = NULL;
  int i = 0;

  assert (NULL != ap_sched);
  assert (NULL != a_hdl);
  assert (a_peer_pos >= 0);

  p_peer = ap_sched->p_peers;

  tiz_mutex_lock (&(ap_sched->mutex));

  for (i = 0; i < a_peer_pos; i++)
    {
      p_peer = p_peer->p_next;
    }

  assert (NULL != p_peer);

  if (ETIZSchedPeerTypeUnknown == p_peer->type)
    {
      p_peer->type = ETIZSchedPeerTypeIlComponent;
    }

  p_peer->hdl = a_hdl;

  tiz_mutex_unlock (&(ap_sched->mutex));

  TIZ_LOG_CNAME (TIZ_LOG_TRACE, TIZ_CNAME (ap_sched->child.p_hdl),
                 TIZ_CBUF (ap_sched->child.p_hdl),
                 "TID [%d] peer_por [%d] type [%d] hdl [%p] ",
                 p_peer->tid, a_peer_pos, p_peer->type, p_peer->hdl);

  return 1;
}


static OMX_S32
register_peer (tiz_scheduler_t * ap_sched,
               const OMX_S32 a_tid, const tizsched_msg_class_t a_api)
{
  int i, position = -1;
  int npeers = 0;
  peer_info_t *p_peer = NULL;
  assert (NULL != ap_sched);

  npeers = ap_sched->npeers;
  p_peer = ap_sched->p_peers;
  /* find this peer */
  for (i = 0; i < npeers; i++)
    {
      assert (NULL != p_peer);
      if (a_tid == p_peer->tid)
        {
          /* found, return its position in the list */
          position = i;
          break;
        }
      p_peer = p_peer->p_next;
    }

  if (-1 == position)
    {
      /* Register this peer */
      p_peer = NULL;
      if (!(p_peer = tiz_mem_calloc (1, sizeof (peer_info_t))))
        {
          TIZ_LOG (TIZ_LOG_TRACE, "mem alloc failed");
        }
      else
        {
          /* Add the new peer to the list */

          tiz_mutex_lock (&(ap_sched->mutex));
          peer_info_t *p_tmp = ap_sched->p_peers;
          while (p_tmp && p_tmp->p_next)
            {
              p_tmp = p_tmp->p_next;
            }
          if (p_tmp)
            {
              p_tmp->p_next = p_peer;
            }
          else
            {
              ap_sched->p_peers = p_peer;
            }
          p_peer->p_next = NULL;
          p_peer->tid = a_tid;
          p_peer->type = ETIZSchedPeerTypeUnknown;
          tiz_sem_init (&(p_peer->sem), 0);
          tiz_mutex_init (&(p_peer->mutex));
          ap_sched->npeers++;
          position = i;
          tiz_mutex_unlock (&(ap_sched->mutex));

        }

    }

  switch (a_api)
    {
    case ETIZSchedMsgAllocateBuffer:
    case ETIZSchedMsgSendCommand:
      {
        p_peer->type = ETIZSchedPeerTypeIlClient;
      }
      break;
    case ETIZSchedMsgComponentInit:
    case ETIZSchedMsgComponentDeInit:
      {
        /* This could be called from an IL Client threads if the IL
         * Core works in context */
        p_peer->type = ETIZSchedPeerTypeIlCore;
      }
      break;
    default:
      break;
    };

  TIZ_LOG_CNAME (TIZ_LOG_TRACE, TIZ_CNAME (ap_sched->child.p_hdl),
                 TIZ_CBUF (ap_sched->child.p_hdl),
                 "TID [%d] API [%s] POS [%d] type [%d] hdl [%p] ",
                 (int) a_tid, tizsched_msg_to_str (a_api), position,
                 p_peer->type, p_peer->hdl);

  return position;
}

static inline OMX_ERRORTYPE
send_msg (tiz_scheduler_t * ap_sched, tizsched_msg_t * ap_msg)
{
  const OMX_S32 tid = tiz_thread_id ();
  OMX_ERRORTYPE rc = OMX_ErrorNone;
  assert (NULL != ap_msg);

  ap_msg->peer_pos = register_peer (ap_sched, tid, ap_msg->class);
  assert (-1 != ap_msg->peer_pos);

  if (tid == ap_sched->thread_id)
    {
      TIZ_LOG_CNAME (TIZ_LOG_ERROR, TIZ_CNAME (ap_sched->child.p_hdl),
                     TIZ_CBUF (ap_sched->child.p_hdl),
                     "WARNING: Dectected API (%s) called from"
                     "IL callback context",
                     tizsched_msg_to_str (ap_msg->class));
      ap_msg->will_block = OMX_FALSE;
      dispatch_msg (ap_sched, &(ap_sched->state), ap_msg);
      return ap_sched->error;
    }
  else
    {
      if (OMX_FALSE == ap_msg->will_block)
        {
          return send_msg_non_blocking (ap_sched, ap_msg);
        }
      else
        {
          OMX_S32 scount = 0;
          int i = 0;
          peer_info_t *p_peer = ap_sched->p_peers;

          tiz_mutex_lock (&(ap_sched->mutex));
          for (i = 0; i < ap_msg->peer_pos; i++)
            {
              p_peer = p_peer->p_next;
            }
          tiz_mutex_unlock (&(ap_sched->mutex));

          assert (NULL != p_peer);

          TIZ_LOG_CNAME (TIZ_LOG_DEBUG,
                         TIZ_CNAME (ap_sched->child.p_hdl),
                         TIZ_CBUF (ap_sched->child.p_hdl),
                         "Peer [%p] "
                         "type [%d] "
                         "tid [%d] "
                         "hdl [%p]",
                         p_peer, p_peer->type, p_peer->tid, p_peer->hdl);

          tiz_mutex_lock (&(p_peer->mutex));
          tiz_sem_getvalue (&(p_peer->sem), &scount);

          if (!scount)
            {
              TIZ_LOG_CNAME (TIZ_LOG_DEBUG,
                             TIZ_CNAME (ap_sched->child.p_hdl),
                             TIZ_CBUF (ap_sched->child.p_hdl),
                             "Signalling peer [%p] sem "
                             "scount [%d] API [%s] called ", p_peer, scount,
                             tizsched_msg_to_str (ap_msg->class));
              tiz_sem_post (&(p_peer->sem));
            }
          tiz_mutex_unlock (&(p_peer->mutex));

          /*           tiz_sem_post (&(ap_sched->schedsem)); */
          /*           tiz_sem_getvalue (&(ap_sched->cbacksem), &scount); */

          if (scount)
            {
              TIZ_LOG_CNAME (TIZ_LOG_DEBUG,
                             TIZ_CNAME (ap_sched->child.p_hdl),
                             TIZ_CBUF (ap_sched->child.p_hdl),
                             "scount [%d] API [%s] called " "in-context",
                             scount, tizsched_msg_to_str (ap_msg->class));
              /*               tiz_sem_wait (&(ap_sched->schedsem)); */
              /*               rc = send_msg_non_blocking (ap_sched, ap_msg); */

              /* dispatch_msg (ap_sched, &(ap_sched->state), ap_msg); */
              /*               return ap_sched->error; */

              rc = send_msg_non_blocking (ap_sched, ap_msg);
            }
          else
            {
              TIZ_LOG_CNAME (TIZ_LOG_DEBUG,
                             TIZ_CNAME (ap_sched->child.p_hdl),
                             TIZ_CBUF (ap_sched->child.p_hdl),
                             "peer [%p] scount [%d] API [%s] called "
                             "blocking", p_peer, scount,
                             tizsched_msg_to_str (ap_msg->class));
              rc = send_msg_blocking (ap_sched, ap_msg);
              tiz_mutex_lock (&(p_peer->mutex));
              tiz_sem_wait (&(p_peer->sem));
              tiz_mutex_unlock (&(p_peer->mutex));
            }

        }
    }

  return rc;

}

static OMX_ERRORTYPE
do_init (tiz_scheduler_t * ap_sched,
         tizsched_state_t * ap_state, tizsched_msg_t * ap_msg)
{
  TIZ_LOG_CNAME (TIZ_LOG_TRACE, TIZ_CNAME (ap_sched->child.p_hdl),
                 TIZ_CBUF (ap_sched->child.p_hdl),
                 "ETIZSchedMsgComponentInit received...");
  assert (ap_state && ETIZSchedStateStarting == *ap_state);
  *ap_state = ETIZSchedStateStarted;
  return init_servants (ap_sched, ap_msg);
}

static OMX_ERRORTYPE
do_deinit (tiz_scheduler_t * ap_sched,
           tizsched_state_t * ap_state, tizsched_msg_t * ap_msg)
{
  TIZ_LOG_CNAME (TIZ_LOG_TRACE, TIZ_CNAME (ap_sched->child.p_hdl),
                 TIZ_CBUF (ap_sched->child.p_hdl),
                 "ETIZSchedMsgComponentDeInit received...");
  assert (ap_state && ETIZSchedStateStarted == *ap_state);
  *ap_state = ETIZSchedStateStopped;
  return deinit_servants (ap_sched, ap_msg);
}

static OMX_ERRORTYPE
do_getcv (tiz_scheduler_t * ap_sched,
          tizsched_state_t * ap_state, tizsched_msg_t * ap_msg)
{
  tizsched_msg_getcomponentversion_t *p_msg_getcv = NULL;
  TIZ_LOG_CNAME (TIZ_LOG_TRACE, TIZ_CNAME (ap_sched->child.p_hdl),
                 TIZ_CBUF (ap_sched->child.p_hdl),
                 "ETIZSchedMsgGetComponentVersion received...");
  assert (NULL != ap_sched);
  assert (NULL != ap_msg);
  assert (ap_state && ETIZSchedStateStarted == *ap_state);
  p_msg_getcv = &(ap_msg->gcv);
  assert (NULL != p_msg_getcv);

  return tizapi_GetComponentVersion (ap_sched->child.p_ker,
                                     ap_msg->p_hdl,
                                     p_msg_getcv->p_comp_name,
                                     p_msg_getcv->p_comp_version,
                                     p_msg_getcv->p_spec_version,
                                     p_msg_getcv->p_comp_uuid);
}

static OMX_ERRORTYPE
do_scmd (tiz_scheduler_t * ap_sched,
         tizsched_state_t * ap_state, tizsched_msg_t * ap_msg)
{
  tizsched_msg_sendcommand_t *p_msg_sc = NULL;
  TIZ_LOG_CNAME (TIZ_LOG_TRACE, TIZ_CNAME (ap_sched->child.p_hdl),
                 TIZ_CBUF (ap_sched->child.p_hdl),
                 "ETIZSchedMsgSendCommand received...");
  assert (NULL != ap_sched);
  assert (NULL != ap_msg);
  assert (ap_state && ETIZSchedStateStarted == *ap_state);
  p_msg_sc = &(ap_msg->scmd);
  assert (NULL != p_msg_sc);

  return tizapi_SendCommand (ap_sched->child.p_fsm,
                             ap_msg->p_hdl,
                             p_msg_sc->cmd,
                             p_msg_sc->param1, p_msg_sc->p_cmd_data);
}

static OMX_ERRORTYPE
do_gparam (tiz_scheduler_t * ap_sched,
           tizsched_state_t * ap_state, tizsched_msg_t * ap_msg)
{
  tizsched_msg_setget_paramconfig_t *p_msg_gparam = NULL;
  OMX_ERRORTYPE rc = OMX_ErrorNone;

  TIZ_LOG_CNAME (TIZ_LOG_TRACE, TIZ_CNAME (ap_sched->child.p_hdl),
                 TIZ_CBUF (ap_sched->child.p_hdl),
                 "ETIZSchedMsgGetParameter received...");
  assert (NULL != ap_sched);
  assert (NULL != ap_msg);
  assert (ap_state && ETIZSchedStateStarted == *ap_state);
  p_msg_gparam = &(ap_msg->sgpc);
  assert (NULL != p_msg_gparam);

  if (OMX_IndexParamStandardComponentRole == p_msg_gparam->index)
    {
      /* IL 1.2 does not mandate support read-access for this index. Only write
       * access is mandated. */
      rc = OMX_ErrorUnsupportedIndex;
    }
  else
    {
      rc = tizapi_GetParameter (ap_sched->child.p_fsm,
                                ap_msg->p_hdl,
                                p_msg_gparam->index, p_msg_gparam->p_struct);
    }

  return rc;
}

static OMX_ERRORTYPE
do_sparam (tiz_scheduler_t * ap_sched,
           tizsched_state_t * ap_state, tizsched_msg_t * ap_msg)
{
  tizsched_msg_setget_paramconfig_t *p_msg_gparam = NULL;
  OMX_ERRORTYPE rc = OMX_ErrorNone;

  TIZ_LOG_CNAME (TIZ_LOG_TRACE, TIZ_CNAME (ap_sched->child.p_hdl),
                 TIZ_CBUF (ap_sched->child.p_hdl),
                 "ETIZSchedMsgSetParameter received...");

  assert (NULL != ap_sched);
  assert (NULL != ap_msg);
  assert (ap_state && ETIZSchedStateStarted == *ap_state);
  p_msg_gparam = &(ap_msg->sgpc);
  assert (NULL != p_msg_gparam);

  if (OMX_IndexParamStandardComponentRole == p_msg_gparam->index)
    {
      const tizfsm_state_id_t now =
        tizfsm_get_substate (ap_sched->child.p_fsm);

      /* Only allow role (re)set if in OMX_StateLoded state */
      if (EStateLoaded != now)
        {
          rc = OMX_ErrorIncorrectStateOperation;
        }
      else
        {
          const OMX_PARAM_COMPONENTROLETYPE *p_role
            = (OMX_PARAM_COMPONENTROLETYPE *) p_msg_gparam->p_struct;
          OMX_U32 role_pos = 0, nroles = ap_sched->child.nroles;
          OMX_STRING p_str = NULL;

          for (role_pos = 0; role_pos < nroles; ++role_pos)
            {
              if (0 == strncmp ((char *) p_role->cRole,
                                (const char *) ap_sched->
                                child.p_role_list[role_pos]->role,
                                OMX_MAX_STRINGNAME_SIZE))
                {
                  TIZ_LOG_CNAME (TIZ_LOG_TRACE,
                                 TIZ_CNAME (ap_sched->child.p_hdl),
                                 TIZ_CBUF (ap_sched->child.p_hdl),
                                 "Found role [%s]...", p_str);
                  break;
                }
            }

          if (role_pos >= nroles)
            {
              /* Check for "default" role */
              if (0 == strncmp ((char *) p_role->cRole,
                                SCHED_OMX_DEFAULT_ROLE,
                                OMX_MAX_STRINGNAME_SIZE))
                {
                  TIZ_LOG_CNAME (TIZ_LOG_TRACE,
                                 TIZ_CNAME (ap_sched->child.p_hdl),
                                 TIZ_CBUF (ap_sched->child.p_hdl),
                                 "Found default role...");
                  role_pos = 0;
                }
            }

          if (role_pos < nroles)
            {
              /* Deregister the current role */

              /* First, delete the processor */
              factory_delete (ap_sched->child.p_prc);
              ap_sched->child.p_prc = NULL;

              tizkernel_deregister_all_ports (ap_sched->child.p_ker);

              /* Populate defaults according to the new role */
              rc = init_and_register_role (ap_sched, role_pos);
            }
          else
            {
              rc = OMX_ErrorBadParameter;
            }
        }
    }
  else
    {
      rc = tizapi_SetParameter (ap_sched->child.p_fsm, ap_msg->p_hdl,
                                p_msg_gparam->index, p_msg_gparam->p_struct);
    }

  return rc;
}

static OMX_ERRORTYPE
do_gconfig (tiz_scheduler_t * ap_sched,
            tizsched_state_t * ap_state, tizsched_msg_t * ap_msg)
{
  tizsched_msg_setget_paramconfig_t *p_msg_gconfig = NULL;

  TIZ_LOG_CNAME (TIZ_LOG_TRACE, TIZ_CNAME (ap_sched->child.p_hdl),
                 TIZ_CBUF (ap_sched->child.p_hdl),
                 "ETIZSchedMsgGetConfig received...");
  assert (NULL != ap_sched);
  assert (NULL != ap_msg);
  assert (ap_state && ETIZSchedStateStarted == *ap_state);
  p_msg_gconfig = &(ap_msg->sgpc);
  assert (NULL != p_msg_gconfig);

  return tizapi_GetConfig (ap_sched->child.p_fsm,
                           ap_msg->p_hdl,
                           p_msg_gconfig->index, p_msg_gconfig->p_struct);
}

static OMX_ERRORTYPE
do_sconfig (tiz_scheduler_t * ap_sched,
            tizsched_state_t * ap_state, tizsched_msg_t * ap_msg)
{
  tizsched_msg_setget_paramconfig_t *p_msg_sconfig = NULL;

  TIZ_LOG_CNAME (TIZ_LOG_TRACE, TIZ_CNAME (ap_sched->child.p_hdl),
                 TIZ_CBUF (ap_sched->child.p_hdl),
                 "ETIZSchedMsgSetConfig received...");
  assert (NULL != ap_sched);
  assert (NULL != ap_msg);
  assert (ap_state && ETIZSchedStateStarted == *ap_state);
  p_msg_sconfig = &(ap_msg->sgpc);
  assert (NULL != p_msg_sconfig);

  return tizapi_SetConfig (ap_sched->child.p_fsm,
                           ap_msg->p_hdl,
                           p_msg_sconfig->index, p_msg_sconfig->p_struct);
}

static OMX_ERRORTYPE
do_gei (tiz_scheduler_t * ap_sched,
        tizsched_state_t * ap_state, tizsched_msg_t * ap_msg)
{
  tizsched_msg_getextindex_t *p_msg_gei = NULL;

  TIZ_LOG_CNAME (TIZ_LOG_TRACE, TIZ_CNAME (ap_sched->child.p_hdl),
                 TIZ_CBUF (ap_sched->child.p_hdl),
                 "ETIZSchedMsgGetExtensionIndex received...");

  assert (NULL != ap_sched);
  assert (NULL != ap_msg);
  assert (ap_state && ETIZSchedStateStarted == *ap_state);
  p_msg_gei = &(ap_msg->gei);
  assert (NULL != p_msg_gei);

  /* Delegate to the kernel directly, no need to do checks in the fsm */
  return tizapi_GetExtensionIndex (ap_sched->child.p_ker, ap_msg->p_hdl,
                                   p_msg_gei->p_ext_name, p_msg_gei->p_index);
}

static OMX_ERRORTYPE
do_gs (tiz_scheduler_t * ap_sched,
       tizsched_state_t * ap_state, tizsched_msg_t * ap_msg)
{
  tizsched_msg_getstate_t *p_msg_gs = NULL;
  assert (NULL != ap_sched);
  assert (NULL != ap_msg);
  assert (ap_state && ETIZSchedStateStarted == *ap_state);
  p_msg_gs = &(ap_msg->gs);
  assert (NULL != p_msg_gs);

  TIZ_LOG_CNAME (TIZ_LOG_TRACE, TIZ_CNAME (ap_msg->p_hdl),
                 TIZ_CBUF (ap_msg->p_hdl),
                 "ETIZSchedMsgGetState received...");

  return tizapi_GetState (ap_sched->child.p_fsm,
                          ap_msg->p_hdl, p_msg_gs->p_state);
}

static OMX_ERRORTYPE
do_tr (tiz_scheduler_t * ap_sched,
       tizsched_state_t * ap_state, tizsched_msg_t * ap_msg)
{
  tizsched_msg_tunnelrequest_t *p_msg_tr = NULL;

  TIZ_LOG_CNAME (TIZ_LOG_TRACE, TIZ_CNAME (ap_sched->child.p_hdl),
                 TIZ_CBUF (ap_sched->child.p_hdl),
                 "ETIZSchedMsgComponentTunnelRequest received...");

  assert (NULL != ap_sched);
  assert (NULL != ap_msg);
  assert (ap_state && ETIZSchedStateStarted == *ap_state);
  p_msg_tr = &(ap_msg->tr);
  assert (NULL != p_msg_tr);

  return tizapi_ComponentTunnelRequest (ap_sched->child.p_fsm,
                                        ap_msg->p_hdl,
                                        p_msg_tr->pid,
                                        p_msg_tr->p_thdl,
                                        p_msg_tr->tpid, p_msg_tr->p_tsetup);
}

static OMX_ERRORTYPE
do_ub (tiz_scheduler_t * ap_sched,
       tizsched_state_t * ap_state, tizsched_msg_t * ap_msg)
{
  OMX_ERRORTYPE rc = OMX_ErrorNone;
  tizsched_msg_usebuffer_t *p_msg_ub = NULL;

  TIZ_LOG_CNAME (TIZ_LOG_TRACE, TIZ_CNAME (ap_sched->child.p_hdl),
                 TIZ_CBUF (ap_sched->child.p_hdl),
                 "ETIZSchedMsgUseBuffer received...");
  assert (NULL != ap_sched);
  assert (NULL != ap_msg);
  assert (ap_state && ETIZSchedStateStarted == *ap_state);
  p_msg_ub = &(ap_msg->ub);
  assert (NULL != p_msg_ub);

  rc = tizapi_UseBuffer (ap_sched->child.p_fsm,
                         ap_msg->p_hdl,
                         p_msg_ub->pp_hdr,
                         p_msg_ub->pid,
                         p_msg_ub->p_app_priv,
                         p_msg_ub->size, p_msg_ub->p_buf);

  if (OMX_ErrorNone == rc)
    {
      /* Register here the peer's hdl */
      void *p_port =
        tizkernel_get_port (ap_sched->child.p_ker, p_msg_ub->pid);
      assert (NULL != p_port);
      if (TIZPORT_IS_TUNNELED (p_port))
        {
          register_peer_handle (ap_sched, ap_msg->peer_pos,
                                tizport_get_tunnel_comp (p_port));
        }
    }

  return rc;
}

static OMX_ERRORTYPE
do_ab (tiz_scheduler_t * ap_sched,
       tizsched_state_t * ap_state, tizsched_msg_t * ap_msg)
{
  tizsched_msg_allocbuffer_t *p_msg_ab = NULL;
  OMX_ERRORTYPE rc = OMX_ErrorNone;
  TIZ_LOG_CNAME (TIZ_LOG_TRACE, TIZ_CNAME (ap_sched->child.p_hdl),
                 TIZ_CBUF (ap_sched->child.p_hdl),
                 "ETIZSchedMsgAllocateBuffer received...");
  assert (NULL != ap_sched);
  assert (NULL != ap_msg);
  assert (ap_state && ETIZSchedStateStarted == *ap_state);
  p_msg_ab = &(ap_msg->ab);
  assert (NULL != p_msg_ab);

  rc = tizapi_AllocateBuffer (ap_sched->child.p_fsm,
                              ap_msg->p_hdl,
                              p_msg_ab->pp_hdr,
                              p_msg_ab->pid,
                              p_msg_ab->p_app_priv, p_msg_ab->size);

  if (NULL == *(p_msg_ab->pp_hdr))
    {
      print_trace ();
    }
  return rc;
}

static OMX_ERRORTYPE
do_fb (tiz_scheduler_t * ap_sched,
       tizsched_state_t * ap_state, tizsched_msg_t * ap_msg)
{
  tizsched_msg_freebuffer_t *p_msg_fb = NULL;
  TIZ_LOG_CNAME (TIZ_LOG_TRACE, TIZ_CNAME (ap_sched->child.p_hdl),
                 TIZ_CBUF (ap_sched->child.p_hdl),
                 "ETIZSchedMsgFreeBuffer received...");
  assert (NULL != ap_sched);
  assert (NULL != ap_msg);
  assert (ap_state && ETIZSchedStateStarted == *ap_state);
  p_msg_fb = &(ap_msg->fb);
  assert (NULL != p_msg_fb);

  return tizapi_FreeBuffer (ap_sched->child.p_fsm,
                            ap_msg->p_hdl, p_msg_fb->pid, p_msg_fb->p_hdr);
}

static OMX_ERRORTYPE
do_etb (tiz_scheduler_t * ap_sched,
        tizsched_state_t * ap_state, tizsched_msg_t * ap_msg)
{
  OMX_ERRORTYPE rc = OMX_ErrorNone;
  tizsched_msg_emptyfillbuffer_t *p_msg_efb = NULL;
  TIZ_LOG_CNAME (TIZ_LOG_TRACE, TIZ_CNAME (ap_sched->child.p_hdl),
                 TIZ_CBUF (ap_sched->child.p_hdl),
                 "ETIZSchedMsgEmptyThisBuffer received...");
  assert (NULL != ap_sched);
  assert (NULL != ap_msg);
  assert (ap_state && ETIZSchedStateStarted == *ap_state);
  p_msg_efb = &(ap_msg->efb);
  assert (NULL != p_msg_efb);

  rc = tizapi_EmptyThisBuffer (ap_sched->child.p_fsm,
                               ap_msg->p_hdl, p_msg_efb->p_hdr);

  if (OMX_ErrorNone == rc)
    {
      /* Register here the peer's hdl, if the port is tunneled */
      void *p_port = tizkernel_get_port (ap_sched->child.p_ker,
                                         p_msg_efb->p_hdr->nInputPortIndex);
      assert (NULL != p_port);
      if (TIZPORT_IS_TUNNELED (p_port))
        {
          register_peer_handle (ap_sched, ap_msg->peer_pos,
                                tizport_get_tunnel_comp (p_port));
        }
    }

  return rc;
}

static OMX_ERRORTYPE
do_ftb (tiz_scheduler_t * ap_sched,
        tizsched_state_t * ap_state, tizsched_msg_t * ap_msg)
{
  OMX_ERRORTYPE rc = OMX_ErrorNone;
  tizsched_msg_emptyfillbuffer_t *p_msg_efb = NULL;
  TIZ_LOG_CNAME (TIZ_LOG_TRACE, TIZ_CNAME (ap_sched->child.p_hdl),
                 TIZ_CBUF (ap_sched->child.p_hdl),
                 "ETIZSchedMsgFillThisBuffer received...");
  assert (NULL != ap_sched);
  assert (NULL != ap_msg);
  assert (ap_state && ETIZSchedStateStarted == *ap_state);
  p_msg_efb = &(ap_msg->efb);
  assert (NULL != p_msg_efb);

  rc = tizapi_FillThisBuffer (ap_sched->child.p_fsm,
                              ap_msg->p_hdl, p_msg_efb->p_hdr);

  if (OMX_ErrorNone == rc)
    {
      /* Register here the peer's hdl, if the port is tunneled */
      void *p_port = tizkernel_get_port (ap_sched->child.p_ker,
                                         p_msg_efb->p_hdr->nOutputPortIndex);
      assert (NULL != p_port);
      if (TIZPORT_IS_TUNNELED (p_port))
        {
          register_peer_handle (ap_sched, ap_msg->peer_pos,
                                tizport_get_tunnel_comp (p_port));
        }
    }

  return rc;
}

static OMX_ERRORTYPE
do_scbs (tiz_scheduler_t * ap_sched,
         tizsched_state_t * ap_state, tizsched_msg_t * ap_msg)
{
  OMX_ERRORTYPE rc = OMX_ErrorNone;
  tizsched_msg_setcallbacks_t *p_msg_scbs = NULL;
  TIZ_LOG_CNAME (TIZ_LOG_TRACE, TIZ_CNAME (ap_sched->child.p_hdl),
                 TIZ_CBUF (ap_sched->child.p_hdl),
                 "ETIZSchedMsgSetCallbacks received...");
  assert (NULL != ap_sched);
  assert (NULL != ap_msg);
  assert (ap_state && ETIZSchedStateStarted == *ap_state);
  p_msg_scbs = &(ap_msg->scbs);
  assert (NULL != p_msg_scbs);

  if (OMX_ErrorNone != (rc = tizapi_SetCallbacks (ap_sched->child.p_fsm,
                                                  ap_msg->p_hdl,
                                                  p_msg_scbs->p_cbacks,
                                                  p_msg_scbs->p_appdata)))
    {
      return rc;
    }

  tizservant_set_callbacks (ap_sched->child.p_fsm, p_msg_scbs->p_appdata,
                            p_msg_scbs->p_cbacks);
  tizservant_set_callbacks (ap_sched->child.p_ker, p_msg_scbs->p_appdata,
                            p_msg_scbs->p_cbacks);
  tizservant_set_callbacks (ap_sched->child.p_prc, p_msg_scbs->p_appdata,
                            p_msg_scbs->p_cbacks);

  return rc;

}

static OMX_ERRORTYPE
do_cre (tiz_scheduler_t * ap_sched,
        tizsched_state_t * ap_state, tizsched_msg_t * ap_msg)
{
  tizsched_msg_componentroleenum_t *p_msg_getcre = NULL;
  OMX_ERRORTYPE rc = OMX_ErrorNone;

  TIZ_LOG_CNAME (TIZ_LOG_TRACE, TIZ_CNAME (ap_sched->child.p_hdl),
                 TIZ_CBUF (ap_sched->child.p_hdl),
                 "ETIZSchedMsgComponentRoleEnum received...");

  assert (NULL != ap_sched);
  assert (NULL != ap_msg);
  assert (ap_state && ETIZSchedStateStarted == *ap_state);
  p_msg_getcre = &(ap_msg->cre);
  assert (NULL != p_msg_getcre);

  if (p_msg_getcre->index < ap_sched->child.nroles)
    {
      strncpy ((char *) p_msg_getcre->p_role,
               (const char *) ap_sched->child.p_role_list[p_msg_getcre->
                                                          index]->role,
               OMX_MAX_STRINGNAME_SIZE);
      p_msg_getcre->p_role[OMX_MAX_STRINGNAME_SIZE - 1] = '\0';

      TIZ_LOG_CNAME (TIZ_LOG_TRACE, TIZ_CNAME (ap_sched->child.p_hdl),
                     TIZ_CBUF (ap_sched->child.p_hdl),
                     "ComponentRoleEnum : index [%d] "
                     "role [%s]", p_msg_getcre->index,
                     (OMX_STRING) p_msg_getcre->p_role);
    }
  else
    {
      TIZ_LOG_CNAME (TIZ_LOG_TRACE, TIZ_CNAME (ap_sched->child.p_hdl),
                     TIZ_CBUF (ap_sched->child.p_hdl),
                     "ComponentRoleEnum : index [%d] "
                     "No more roles", p_msg_getcre->index);
      rc = OMX_ErrorNoMore;
    }

  return rc;
}

static OMX_ERRORTYPE
do_plgevt (tiz_scheduler_t * ap_sched,
           tizsched_state_t * ap_state, tizsched_msg_t * ap_msg)
{
  tizsched_msg_plg_event_t *p_msg_pe = NULL;
  tizevent_t *p_event = NULL;
  TIZ_LOG_CNAME (TIZ_LOG_TRACE, TIZ_CNAME (ap_sched->child.p_hdl),
                 TIZ_CBUF (ap_sched->child.p_hdl),
                 "ETIZSchedMsgPluggableEvent received...");
  assert (NULL != ap_sched);
  assert (NULL != ap_msg);
  assert (ap_state && ETIZSchedStateStarted == *ap_state);

  p_msg_pe = &(ap_msg->pe);
  assert (NULL != p_msg_pe);
  assert (NULL != p_msg_pe->p_event);

  p_event = p_msg_pe->p_event;
  return tizservant_receive_pluggable_event (p_event->p_servant,
                                             p_event->p_hdl, p_event);

  /*   p_event->apf_hdlr(p_event->p_servant, */
  /*                        p_event->p_hdl, */
  /*                        p_event); */
}

static OMX_ERRORTYPE
do_rr (tiz_scheduler_t * ap_sched,
       tizsched_state_t * ap_state, tizsched_msg_t * ap_msg)
{
  tizsched_msg_regroles_t *p_msg_rr = NULL;
  const tiz_role_factory_t *p_rf = NULL;
  OMX_ERRORTYPE rc = OMX_ErrorNone;
  OMX_U32 i = 0;
  OMX_U32 j = 0;

  TIZ_LOG_CNAME (TIZ_LOG_TRACE, TIZ_CNAME (ap_sched->child.p_hdl),
                 TIZ_CBUF (ap_sched->child.p_hdl),
                 "ETIZSchedMsgRegisterRoles received...");

  assert (NULL != ap_sched);
  assert (NULL != ap_msg);
  assert (ap_state && ETIZSchedStateStarted == *ap_state);

  p_msg_rr = &(ap_msg->rr);
  assert (NULL != p_msg_rr);
  assert (NULL != p_msg_rr->p_role_list);
  assert (p_msg_rr->nroles > 0);

  /* TODO: Validate this inputs only in debug mode */
  for (i = 0; i < p_msg_rr->nroles && rc == OMX_ErrorNone; ++i)
    {
      p_rf = p_msg_rr->p_role_list[i];
      if (NULL == p_rf->pf_cport || NULL == p_rf->pf_proc)
        {
          assert (0);
        }

      assert (p_rf->nports > 0);
      assert (p_rf->nports <= TIZ_MAX_PORTS);

      for (j = 0; j < p_rf->nports && rc == OMX_ErrorNone; ++j)
        {
          if (!p_rf->pf_port[j])
            {
              assert (0);
            }
        }
    }

  /* Store a local copy of the role list in the child struct */
  rc = store_roles (ap_sched, p_msg_rr);

  if (OMX_ErrorNone == rc)
    {
      /* Now instantiate the entities of role #0, the default role */
      rc = init_and_register_role (ap_sched, 0);
    }

  return rc;
}

static OMX_ERRORTYPE
do_rph (tiz_scheduler_t * ap_sched,
        tizsched_state_t * ap_state, tizsched_msg_t * ap_msg)
{
  tizsched_msg_regphooks_t *p_msg_rph = NULL;
  OMX_ERRORTYPE rc = OMX_ErrorNone;

  TIZ_LOG_CNAME (TIZ_LOG_TRACE, TIZ_CNAME (ap_sched->child.p_hdl),
                 TIZ_CBUF (ap_sched->child.p_hdl),
                 "ETIZSchedMsgRegisterPortHooks received...");

  assert (NULL != ap_sched);
  assert (NULL != ap_msg);
  assert (ap_state && ETIZSchedStateStarted == *ap_state);

  p_msg_rph = &(ap_msg->rph);
  assert (NULL != p_msg_rph);
  assert (NULL != p_msg_rph->p_hooks);

  {
    const tizfsm_state_id_t now = tizfsm_get_substate (ap_sched->child.p_fsm);

    /* Only allow role hook registration if in OMX_StateLoded state. Disallowed
     * for other states, even if the port is disabled.
     */
    if (EStateLoaded != now)
      {
        rc = OMX_ErrorIncorrectStateOperation;
      }
    else
      {
        OMX_U32 i = 0;
        void *p_port = NULL;
        OMX_U32 pid = 0;

        do
          {
            pid = ((OMX_ALL != p_msg_rph->pid) ? p_msg_rph->pid : i++);

            if (NULL != (p_port
                         = tizkernel_get_port (ap_sched->child.p_ker, pid)))
              {
                tizport_set_alloc_hooks (p_port, p_msg_rph->p_hooks,
                                         p_msg_rph->pid == OMX_ALL ?
                                         NULL : p_msg_rph->p_old_hooks);
              }
          }
        while (p_port != NULL && (OMX_ALL == p_msg_rph->pid));

        if (NULL == p_port && OMX_ALL != p_msg_rph->pid)
          {
            /* Bad port index received */
            rc = OMX_ErrorBadPortIndex;
          }
      }
  }

  return rc;
}

static OMX_ERRORTYPE
do_eio (tiz_scheduler_t * ap_sched,
        tizsched_state_t * ap_state, tizsched_msg_t * ap_msg)
{
  tizsched_msg_ev_io_t *p_msg_eio = NULL;

  TIZ_LOG_CNAME (TIZ_LOG_NOTICE, TIZ_CNAME (ap_sched->child.p_hdl),
                 TIZ_CBUF (ap_sched->child.p_hdl),
                 "ETIZSchedMsgEvIo received...");

  assert (NULL != ap_sched);
  assert (NULL != ap_msg);
  assert (ap_state && ETIZSchedStateStarted == *ap_state);

  p_msg_eio = &(ap_msg->eio);
  assert (NULL != p_msg_eio);

  return tizproc_event_io_ready (ap_sched->child.p_prc,
                                   p_msg_eio->p_ev_io,
                                   p_msg_eio->fd,
                                   p_msg_eio->events);
}

static OMX_ERRORTYPE
do_etmr (tiz_scheduler_t * ap_sched,
        tizsched_state_t * ap_state, tizsched_msg_t * ap_msg)
{
  tizsched_msg_ev_timer_t *p_msg_etmr = NULL;

  TIZ_LOG_CNAME (TIZ_LOG_TRACE, TIZ_CNAME (ap_sched->child.p_hdl),
                 TIZ_CBUF (ap_sched->child.p_hdl),
                 "ETIZSchedMsgEvTimer received...");

  assert (NULL != ap_sched);
  assert (NULL != ap_msg);
  assert (ap_state && ETIZSchedStateStarted == *ap_state);

  p_msg_etmr = &(ap_msg->etmr);
  assert (NULL != p_msg_etmr);

  return tizproc_event_timer_ready (ap_sched->child.p_prc,
                                      p_msg_etmr->p_ev_timer);
}

static OMX_ERRORTYPE
do_estat (tiz_scheduler_t * ap_sched,
        tizsched_state_t * ap_state, tizsched_msg_t * ap_msg)
{
  tizsched_msg_ev_stat_t *p_msg_estat = NULL;

  TIZ_LOG_CNAME (TIZ_LOG_TRACE, TIZ_CNAME (ap_sched->child.p_hdl),
                 TIZ_CBUF (ap_sched->child.p_hdl),
                 "ETIZSchedMsgEvStat received...");

  assert (NULL != ap_sched);
  assert (NULL != ap_msg);
  assert (ap_state && ETIZSchedStateStarted == *ap_state);

  p_msg_estat = &(ap_msg->estat);
  assert (NULL != p_msg_estat);

  return tizproc_event_stat_ready (ap_sched->child.p_prc,
                                     p_msg_estat->p_ev_stat,
                                     p_msg_estat->events);
}

static inline tizsched_msg_t *
init_scheduler_message (OMX_HANDLETYPE ap_hdl,
                        tizsched_msg_class_t a_msg_class,
                        OMX_BOOL is_blocking)
{
  tizsched_msg_t *p_msg = NULL;

  assert (NULL != ap_hdl);
  assert (a_msg_class < ETIZSchedMsgMax);

  if (NULL == (p_msg = (tizsched_msg_t *)
               tiz_mem_calloc (1, sizeof (tizsched_msg_t))))
    {
      TIZ_LOG_CNAME (TIZ_LOG_TRACE, TIZ_CNAME (ap_hdl), TIZ_CBUF (ap_hdl),
                     "[OMX_ErrorInsufficientResources] : "
                     "Creating message [%s]",
                     tizsched_msg_to_str (a_msg_class));
    }
  else
    {
      p_msg->p_hdl = ap_hdl;
      p_msg->class = a_msg_class;
      p_msg->will_block = is_blocking;
    }

  return p_msg;
}

static OMX_ERRORTYPE
configure_port_preannouncements (tiz_scheduler_t * ap_sched,
                                 OMX_HANDLETYPE ap_hdl, OMX_PTR p_port)
{
  tiz_rcfile_t *p_rcfile = NULL;
  OMX_ERRORTYPE rc = OMX_ErrorNone;
  const char *p_preannounce_disabled = NULL;
  char fqd_key[OMX_MAX_STRINGNAME_SIZE];
  char port_num[OMX_MAX_STRINGNAME_SIZE];
  OMX_U32 pid = tizport_index (p_port);

  /* TODO : Fix error handling */

  /* OMX.component.name.key */
  sprintf (port_num, "%u", (unsigned int) pid);
  strncpy (fqd_key, ap_sched->cname, OMX_MAX_STRINGNAME_SIZE);
  strncat (fqd_key, ".preannouncements_disabled.port",
           OMX_MAX_STRINGNAME_SIZE);
  strncat (fqd_key, port_num, OMX_MAX_STRINGNAME_SIZE);

  tiz_rcfile_open (&p_rcfile);

  p_preannounce_disabled = tiz_rcfile_get_value (p_rcfile, "plugins-data",
                                                 fqd_key);

  if (!p_preannounce_disabled
      || (0 != strncmp (p_preannounce_disabled, "true", 4)))
    {
      TIZ_LOG_CNAME (TIZ_LOG_TRACE, TIZ_CNAME (ap_hdl), TIZ_CBUF (ap_hdl),
                     "[%s:port-%d] Preannouncements are [ENABLED]...",
                     ap_sched->cname, pid);
    }
  else
    {
      OMX_TIZONIA_PARAM_BUFFER_PREANNOUNCEMENTSMODETYPE pamode;

      TIZ_LOG_CNAME (TIZ_LOG_TRACE, TIZ_CNAME (ap_hdl), TIZ_CBUF (ap_hdl),
                     "[%s:port-%d] Preannouncements are [DISABLED]...",
                     ap_sched->cname, pid);

      pamode.nSize =
        sizeof (OMX_TIZONIA_PARAM_BUFFER_PREANNOUNCEMENTSMODETYPE);
      pamode.nVersion.nVersion = OMX_VERSION;
      pamode.nPortIndex = pid;
      pamode.bEnabled = OMX_FALSE;
      rc = tizapi_SetParameter
        (p_port, ap_hdl,
         OMX_TizoniaIndexParamBufferPreAnnouncementsMode, &pamode);
    }

  tiz_rcfile_close (p_rcfile);

  return rc;
}

OMX_ERRORTYPE
tiz_init_component (OMX_HANDLETYPE ap_hdl, const char *ap_cname)
{
  tizsched_msg_t *p_msg = NULL;
  tiz_scheduler_t *p_sched = NULL;

  TIZ_LOG (TIZ_LOG_TRACE, "[%s] Initializing base component "
           "infrastructure", ap_cname);

  if (NULL == ap_hdl)
    {
      TIZ_LOG (TIZ_LOG_ERROR, "[OMX_ErrorBadParameter] [%s]", ap_cname);
      return OMX_ErrorBadParameter;
    }

  /* Instantiate the scheduler */
  if (NULL == (p_sched = instantiate_scheduler (ap_hdl, ap_cname)))
    {
      TIZ_LOG (TIZ_LOG_TRACE,
               "[%s] Error Initializing component - "
               "hdl [%p]...", ap_cname, ap_hdl);
      return OMX_ErrorInsufficientResources;
    }

  /* Start scheduler */
  start_scheduler (p_sched);

  if (NULL == (p_msg = init_scheduler_message (ap_hdl,
                                               ETIZSchedMsgComponentInit,
                                               OMX_TRUE)))
    {
      return OMX_ErrorInsufficientResources;
    }

  return send_msg (p_sched, p_msg);
}

static OMX_ERRORTYPE
tizsched_ComponentDeInit (OMX_HANDLETYPE ap_hdl)
{

  OMX_ERRORTYPE rc = OMX_ErrorNone;
  tizsched_msg_t *p_msg = NULL;
  tiz_scheduler_t *p_sched = NULL;

  TIZ_LOG (TIZ_LOG_TRACE, "ComponentDeInit");

  if (NULL == ap_hdl)
    {
      TIZ_LOG (TIZ_LOG_ERROR, "[OMX_ErrorBadParameter] : null handle.");
      return OMX_ErrorBadParameter;
    }

  p_sched = ((OMX_COMPONENTTYPE *) ap_hdl)->pComponentPrivate;

  if (NULL == (p_msg = init_scheduler_message (ap_hdl,
                                               ETIZSchedMsgComponentDeInit,
                                               OMX_TRUE)))
    {
      return OMX_ErrorInsufficientResources;
    }

  rc = send_msg (p_sched, p_msg);

  delete_scheduler (p_sched);

  return rc;
}

static OMX_ERRORTYPE
tizsched_GetComponentVersion (OMX_HANDLETYPE ap_hdl,
                              OMX_STRING ap_comp_name,
                              OMX_VERSIONTYPE * ap_comp_version,
                              OMX_VERSIONTYPE * ap_spec_version,
                              OMX_UUIDTYPE * ap_comp_uuid)
{
  tizsched_msg_t *p_msg = NULL;
  tizsched_msg_getcomponentversion_t *p_msg_gcv = NULL;
  tiz_scheduler_t *p_sched = NULL;

  TIZ_LOG (TIZ_LOG_TRACE, "GetComponentVersion");

  if (NULL == ap_hdl
      || NULL == ap_comp_name
      || NULL == ap_comp_version
      || NULL == ap_spec_version || NULL == ap_comp_uuid)
    {
      TIZ_LOG (TIZ_LOG_ERROR, "[OMX_ErrorBadParameter]: "
               "Unexpected null pointer received");
      return OMX_ErrorBadParameter;
    }

  p_sched = ((OMX_COMPONENTTYPE *) ap_hdl)->pComponentPrivate;

  if (NULL == (p_msg = init_scheduler_message (ap_hdl,
                                               ETIZSchedMsgGetComponentVersion,
                                               OMX_TRUE)))
    {
      return OMX_ErrorInsufficientResources;
    }

  /* Finish-up this message */
  p_msg_gcv = &(p_msg->gcv);
  assert (NULL != p_msg_gcv);

  p_msg_gcv->p_comp_name = ap_comp_name;
  p_msg_gcv->p_comp_version = ap_comp_version;
  p_msg_gcv->p_spec_version = ap_spec_version;
  p_msg_gcv->p_comp_uuid = ap_comp_uuid;

  return send_msg (p_sched, p_msg);
}

static OMX_ERRORTYPE
tizsched_SendCommand (OMX_HANDLETYPE ap_hdl,
                      OMX_COMMANDTYPE a_cmd,
                      OMX_U32 a_param1, OMX_PTR ap_cmd_data)
{
  tizsched_msg_t *p_msg = NULL;
  tizsched_msg_sendcommand_t *p_msg_scmd = NULL;
  tiz_scheduler_t *p_sched = NULL;

  if (NULL == ap_hdl || (OMX_CommandStateSet == a_cmd &&
                         (a_param1 < OMX_StateLoaded ||
                          a_param1 > OMX_StateWaitForResources)))
    {
      TIZ_LOG (TIZ_LOG_ERROR, "[OMX_ErrorBadParameter]: Bad parameter found");
      return OMX_ErrorBadParameter;
    }

  p_sched = ((OMX_COMPONENTTYPE *) ap_hdl)->pComponentPrivate;

  if (NULL == (p_msg = init_scheduler_message (ap_hdl,
                                               ETIZSchedMsgSendCommand,
                                               OMX_TRUE)))
    {
      return OMX_ErrorInsufficientResources;
    }

  /* Finish-up this message */
  p_msg_scmd = &(p_msg->scmd);
  assert (NULL != p_msg_scmd);

  p_msg_scmd->cmd = a_cmd;
  p_msg_scmd->param1 = a_param1;
  p_msg_scmd->p_cmd_data = ap_cmd_data;

  return send_msg (p_sched, p_msg);

}

static OMX_ERRORTYPE
tizsched_GetParameter (OMX_HANDLETYPE ap_hdl,
                       OMX_INDEXTYPE a_index, OMX_PTR ap_struct)
{
  tizsched_msg_t *p_msg = NULL;
  tizsched_msg_setget_paramconfig_t *p_msg_gparam = NULL;
  tiz_scheduler_t *p_sched = NULL;

  if (NULL == ap_hdl || NULL == ap_struct)
    {
      TIZ_LOG (TIZ_LOG_ERROR, "[OMX_ErrorBadParameter]: "
               "Unexpected null pointer received");
      return OMX_ErrorBadParameter;
    }

  p_sched = ((OMX_COMPONENTTYPE *) ap_hdl)->pComponentPrivate;

  if (NULL == (p_msg = init_scheduler_message (ap_hdl,
                                               ETIZSchedMsgGetParameter,
                                               OMX_TRUE)))
    {
      return OMX_ErrorInsufficientResources;
    }

  /* Finish-up this message */
  p_msg_gparam = &(p_msg->sgpc);
  assert (NULL != p_msg_gparam);

  p_msg_gparam->index = a_index;
  p_msg_gparam->p_struct = ap_struct;

  return send_msg (p_sched, p_msg);
}

static OMX_ERRORTYPE
tizsched_SetParameter (OMX_HANDLETYPE ap_hdl,
                       OMX_INDEXTYPE a_index, OMX_PTR ap_struct)
{
  tizsched_msg_t *p_msg = NULL;
  tizsched_msg_setget_paramconfig_t *p_msg_sparam = NULL;
  tiz_scheduler_t *p_sched = NULL;

  if (NULL == ap_hdl || NULL == ap_struct)
    {
      TIZ_LOG (TIZ_LOG_ERROR, "[OMX_ErrorBadParameter]: "
               "Unexpected null pointer received");
      return OMX_ErrorBadParameter;
    }

  p_sched = ((OMX_COMPONENTTYPE *) ap_hdl)->pComponentPrivate;

  if (NULL == (p_msg = init_scheduler_message (ap_hdl,
                                               ETIZSchedMsgSetParameter,
                                               OMX_TRUE)))
    {
      return OMX_ErrorInsufficientResources;
    }

  /* Finish-up this message */
  p_msg_sparam = &(p_msg->sgpc);
  assert (NULL != p_msg_sparam);

  p_msg_sparam->index = a_index;
  p_msg_sparam->p_struct = ap_struct;

  return send_msg (p_sched, p_msg);
}

static OMX_ERRORTYPE
tizsched_GetConfig (OMX_HANDLETYPE ap_hdl,
                    OMX_INDEXTYPE a_index, OMX_PTR ap_struct)
{
  tizsched_msg_t *p_msg = NULL;
  tizsched_msg_setget_paramconfig_t *p_msg_gconf = NULL;
  tiz_scheduler_t *p_sched = NULL;

  if (NULL == ap_hdl || NULL == ap_struct)
    {
      TIZ_LOG (TIZ_LOG_ERROR, "[OMX_ErrorBadParameter]: "
               "Unexpected null pointer received");
      return OMX_ErrorBadParameter;
    }

  p_sched = ((OMX_COMPONENTTYPE *) ap_hdl)->pComponentPrivate;

  if (NULL == (p_msg = init_scheduler_message (ap_hdl,
                                               ETIZSchedMsgGetConfig,
                                               OMX_TRUE)))
    {
      return OMX_ErrorInsufficientResources;
    }

  /* Finish-up this message */
  p_msg_gconf = &(p_msg->sgpc);
  assert (NULL != p_msg_gconf);

  p_msg_gconf->index = a_index;
  p_msg_gconf->p_struct = ap_struct;

  return send_msg (p_sched, p_msg);
}

static OMX_ERRORTYPE
tizsched_SetConfig (OMX_HANDLETYPE ap_hdl,
                    OMX_INDEXTYPE a_index, OMX_PTR ap_struct)
{
  tizsched_msg_t *p_msg = NULL;
  tizsched_msg_setget_paramconfig_t *p_msg_sconf = NULL;
  tiz_scheduler_t *p_sched = NULL;

  if (NULL == ap_hdl || NULL == ap_struct)
    {
      TIZ_LOG (TIZ_LOG_ERROR, "[OMX_ErrorBadParameter]: "
               "Unexpected null pointer received");
      return OMX_ErrorBadParameter;
    }

  p_sched = ((OMX_COMPONENTTYPE *) ap_hdl)->pComponentPrivate;

  if (NULL == (p_msg = init_scheduler_message (ap_hdl,
                                               ETIZSchedMsgSetConfig,
                                               OMX_TRUE)))
    {
      return OMX_ErrorInsufficientResources;
    }

  /* Finish-up this message */
  p_msg_sconf = &(p_msg->sgpc);
  assert (NULL != p_msg_sconf);

  p_msg_sconf->index = a_index;
  p_msg_sconf->p_struct = ap_struct;

  return send_msg (p_sched, p_msg);
}

static OMX_ERRORTYPE
tizsched_GetExtensionIndex (OMX_HANDLETYPE ap_hdl,
                            OMX_STRING ap_param_name,
                            OMX_INDEXTYPE * ap_index_type)
{
  tizsched_msg_t *p_msg = NULL;
  tizsched_msg_getextindex_t *p_msg_gei = NULL;
  tiz_scheduler_t *p_sched = NULL;

  if (NULL == ap_index_type || NULL == ap_param_name)
    {
      TIZ_LOG (TIZ_LOG_ERROR, "[OMX_ErrorBadParameter] : "
               "Unexpected null pointer received");
      return OMX_ErrorBadParameter;
    }

  p_sched = ((OMX_COMPONENTTYPE *) ap_hdl)->pComponentPrivate;

  if (NULL == (p_msg = init_scheduler_message (ap_hdl,
                                               ETIZSchedMsgGetExtensionIndex,
                                               OMX_TRUE)))
    {
      return OMX_ErrorInsufficientResources;
    }

  /* Finish-up this message */
  p_msg_gei = &(p_msg->gei);
  assert (NULL != p_msg_gei);

  p_msg_gei->p_ext_name = ap_param_name;
  p_msg_gei->p_index = ap_index_type;

  return send_msg (p_sched, p_msg);
}

static OMX_ERRORTYPE
tizsched_GetState (OMX_HANDLETYPE ap_hdl, OMX_STATETYPE * ap_state)
{
  tizsched_msg_t *p_msg = NULL;
  tizsched_msg_getstate_t *p_msg_gs = NULL;
  tiz_scheduler_t *p_sched = NULL;

  if (NULL == ap_hdl || NULL == ap_state)
    {
      TIZ_LOG (TIZ_LOG_ERROR, "[OMX_ErrorBadParameter]: "
               "Unexpected null pointer received");
      return OMX_ErrorBadParameter;
    }

  p_sched = ((OMX_COMPONENTTYPE *) ap_hdl)->pComponentPrivate;

  if (NULL == (p_msg = init_scheduler_message (ap_hdl,
                                               ETIZSchedMsgGetState,
                                               OMX_TRUE)))
    {
      return OMX_ErrorInsufficientResources;
    }

  /* Finish-up this message */
  p_msg_gs = &(p_msg->gs);
  assert (NULL != p_msg_gs);

  p_msg_gs->p_state = ap_state;

  return send_msg (p_sched, p_msg);
}

static OMX_ERRORTYPE
tizsched_ComponentTunnelRequest (OMX_HANDLETYPE ap_hdl,
                                 OMX_U32 a_pid,
                                 OMX_HANDLETYPE ap_thdl,
                                 OMX_U32 a_tpid,
                                 OMX_TUNNELSETUPTYPE * ap_tsetup)
{
  tizsched_msg_t *p_msg = NULL;
  tizsched_msg_tunnelrequest_t *p_msg_tr = NULL;
  tiz_scheduler_t *p_sched = NULL;

  if (NULL == ap_hdl || (ap_thdl && !ap_tsetup))
    {
      TIZ_LOG (TIZ_LOG_ERROR, "[OMX_ErrorBadParameter]: Bad parameter found "
               "(p_hdl [%p] p_tsetup [%p])", ap_hdl, ap_tsetup);
      return OMX_ErrorBadParameter;
    }

  p_sched = ((OMX_COMPONENTTYPE *) ap_hdl)->pComponentPrivate;

  if (NULL == (p_msg = init_scheduler_message (ap_hdl,
                                               ETIZSchedMsgComponentTunnelRequest,
                                               OMX_TRUE)))
    {
      return OMX_ErrorInsufficientResources;
    }

  /* Finish-up this message */
  p_msg_tr = &(p_msg->tr);
  assert (NULL != p_msg_tr);

  p_msg_tr->pid = a_pid;
  p_msg_tr->p_thdl = ap_thdl;
  p_msg_tr->tpid = a_tpid;
  p_msg_tr->p_tsetup = ap_tsetup;

  return send_msg (p_sched, p_msg);
}

static OMX_ERRORTYPE
tizsched_UseBuffer (OMX_HANDLETYPE ap_hdl,
                    OMX_BUFFERHEADERTYPE ** app_hdr,
                    OMX_U32 a_pid,
                    OMX_PTR ap_apppriv, OMX_U32 a_size, OMX_U8 * ap_buf)
{
  tizsched_msg_t *p_msg = NULL;
  tizsched_msg_usebuffer_t *p_msg_ub = NULL;
  tiz_scheduler_t *p_sched = NULL;

  /* From 1.2, ap_buf may be NULL. The provisional spec does not say what
   * a_size would be when ap_buf is NULL. For now assume a_size may also be
   * zero. */
  if (NULL == ap_hdl || NULL == app_hdr)
    {
      TIZ_LOG (TIZ_LOG_ERROR, "[OMX_ErrorBadParameter]: Bad parameter found "
               "(p_hdl [%p] pp_hdr [%p] size [%d] p_buf [%p])",
               ap_hdl, app_hdr, a_size, ap_buf);
      return OMX_ErrorBadParameter;
    }

  p_sched = ((OMX_COMPONENTTYPE *) ap_hdl)->pComponentPrivate;

  if (NULL == (p_msg = init_scheduler_message (ap_hdl,
                                               ETIZSchedMsgUseBuffer,
                                               OMX_TRUE)))
    {
      return OMX_ErrorInsufficientResources;
    }

  /* Finish-up this message */
  p_msg_ub = &(p_msg->ub);
  assert (NULL != p_msg_ub);

  p_msg_ub->pp_hdr = app_hdr;
  p_msg_ub->pid = a_pid;
  p_msg_ub->p_app_priv = ap_apppriv;
  p_msg_ub->size = a_size;
  p_msg_ub->p_buf = ap_buf;

  return send_msg (p_sched, p_msg);
}

static OMX_ERRORTYPE
tizsched_AllocateBuffer (OMX_HANDLETYPE ap_hdl,
                         OMX_BUFFERHEADERTYPE ** app_hdr,
                         OMX_U32 a_pid, OMX_PTR ap_apppriv, OMX_U32 a_size)
{
  tizsched_msg_t *p_msg = NULL;
  tizsched_msg_allocbuffer_t *p_msg_ab = NULL;
  tiz_scheduler_t *p_sched = NULL;

  if (NULL == ap_hdl || 0 == a_size)
    {
      TIZ_LOG (TIZ_LOG_ERROR, "[OMX_ErrorBadParameter]: "
               "Bad parameter found");
      return OMX_ErrorBadParameter;
    }

  p_sched = ((OMX_COMPONENTTYPE *) ap_hdl)->pComponentPrivate;

  if (NULL == (p_msg = init_scheduler_message (ap_hdl,
                                               ETIZSchedMsgAllocateBuffer,
                                               OMX_TRUE)))
    {
      return OMX_ErrorInsufficientResources;
    }

  /* Finish-up this message */
  p_msg_ab = &(p_msg->ab);
  assert (NULL != p_msg_ab);

  p_msg_ab->pp_hdr = app_hdr;
  p_msg_ab->pid = a_pid;
  p_msg_ab->p_app_priv = ap_apppriv;
  p_msg_ab->size = a_size;

  return send_msg (p_sched, p_msg);
}

static OMX_ERRORTYPE
tizsched_FreeBuffer (OMX_HANDLETYPE ap_hdl,
                     OMX_U32 a_pid, OMX_BUFFERHEADERTYPE * ap_hdr)
{
  tizsched_msg_t *p_msg = NULL;
  tizsched_msg_freebuffer_t *p_msg_fb = NULL;
  tiz_scheduler_t *p_sched = NULL;

  if (NULL == ap_hdl || NULL == ap_hdr)
    {
      TIZ_LOG (TIZ_LOG_ERROR, "[OMX_ErrorBadParameter]: "
               "Unexpected null pointer received");
      return OMX_ErrorBadParameter;
    }

  p_sched = ((OMX_COMPONENTTYPE *) ap_hdl)->pComponentPrivate;

  if (NULL == (p_msg = init_scheduler_message (ap_hdl,
                                               ETIZSchedMsgFreeBuffer,
                                               OMX_TRUE)))
    {
      return OMX_ErrorInsufficientResources;
    }


  /* Finish-up this message */
  p_msg_fb = &(p_msg->fb);
  assert (NULL != p_msg_fb);

  p_msg_fb->pid = a_pid;
  p_msg_fb->p_hdr = ap_hdr;

  return send_msg (p_sched, p_msg);
}

static OMX_ERRORTYPE
tizsched_EmptyThisBuffer (OMX_HANDLETYPE ap_hdl,
                          OMX_BUFFERHEADERTYPE * ap_hdr)
{
  tizsched_msg_t *p_msg = NULL;
  tizsched_msg_emptyfillbuffer_t *p_msg_efb = NULL;
  tiz_scheduler_t *p_sched = NULL;

  if (NULL == ap_hdl || NULL == ap_hdr)
    {
      TIZ_LOG (TIZ_LOG_ERROR, "[OMX_ErrorBadParameter]: "
               "Unexpected null pointer received");
      return OMX_ErrorBadParameter;
    }

  p_sched = ((OMX_COMPONENTTYPE *) ap_hdl)->pComponentPrivate;

  if (NULL == (p_msg = init_scheduler_message (ap_hdl,
                                               ETIZSchedMsgEmptyThisBuffer,
                                               OMX_TRUE)))
    {
      return OMX_ErrorInsufficientResources;
    }

  /* Finish-up this message */
  p_msg_efb = &(p_msg->efb);
  assert (NULL != p_msg_efb);

  p_msg_efb->p_hdr = ap_hdr;

  return send_msg (p_sched, p_msg);
}

static OMX_ERRORTYPE
tizsched_FillThisBuffer (OMX_HANDLETYPE ap_hdl, OMX_BUFFERHEADERTYPE * ap_hdr)
{
  tizsched_msg_t *p_msg = NULL;
  tizsched_msg_emptyfillbuffer_t *p_msg_efb = NULL;
  tiz_scheduler_t *p_sched = NULL;

  if (NULL == ap_hdl || NULL == ap_hdr)
    {
      TIZ_LOG (TIZ_LOG_ERROR, "[OMX_ErrorBadParameter]: "
               "Unexpected null pointer received");
      return OMX_ErrorBadParameter;
    }

  p_sched = ((OMX_COMPONENTTYPE *) ap_hdl)->pComponentPrivate;

  if (NULL == (p_msg = init_scheduler_message (ap_hdl,
                                               ETIZSchedMsgFillThisBuffer,
                                               OMX_TRUE)))
    {
      return OMX_ErrorInsufficientResources;
    }

  /* Finish-up this message */
  p_msg_efb = &(p_msg->efb);
  assert (NULL != p_msg_efb);

  p_msg_efb->p_hdr = ap_hdr;

  return send_msg (p_sched, p_msg);
}

static OMX_ERRORTYPE
tizsched_SetCallbacks (OMX_HANDLETYPE ap_hdl,
                       OMX_CALLBACKTYPE * ap_cbacks, OMX_PTR ap_appdata)
{
  tizsched_msg_t *p_msg = NULL;
  tizsched_msg_setcallbacks_t *p_msg_scbs = NULL;
  tiz_scheduler_t *p_sched = NULL;

  if (NULL == ap_hdl || NULL == ap_cbacks)
    {
      TIZ_LOG (TIZ_LOG_ERROR, "[OMX_ErrorBadParameter]: "
               "Bad parameter found");
      return OMX_ErrorBadParameter;
    }

  p_sched = ((OMX_COMPONENTTYPE *) ap_hdl)->pComponentPrivate;

  if (NULL == (p_msg = init_scheduler_message (ap_hdl,
                                               ETIZSchedMsgSetCallbacks,
                                               OMX_TRUE)))
    {
      return OMX_ErrorInsufficientResources;
    }

  /* Finish-up this message */
  p_msg_scbs = &(p_msg->scbs);
  assert (NULL != p_msg_scbs);

  p_msg_scbs->p_cbacks = ap_cbacks;
  p_msg_scbs->p_appdata = ap_appdata;

  return send_msg (p_sched, p_msg);
}

static OMX_ERRORTYPE
tizsched_UseEGLImage (OMX_HANDLETYPE ap_hdl,
                      OMX_BUFFERHEADERTYPE ** app_buf_hdr,
                      OMX_U32 a_port_index, OMX_PTR ap_app_private,
                      void *eglImage)
{
  return OMX_ErrorNotImplemented;
}

static OMX_ERRORTYPE
tizsched_ComponentRoleEnum (OMX_HANDLETYPE ap_hdl,
                            OMX_U8 * ap_role, OMX_U32 a_index)
{
  tizsched_msg_t *p_msg = NULL;
  tizsched_msg_componentroleenum_t *p_msg_cre = NULL;
  tiz_scheduler_t *p_sched = NULL;

  if (NULL == ap_hdl || NULL == ap_role)
    {
      TIZ_LOG (TIZ_LOG_ERROR, "[OMX_ErrorBadParameter] : "
               "Unexpected null pointer received");
      return OMX_ErrorBadParameter;
    }

  p_sched = ((OMX_COMPONENTTYPE *) ap_hdl)->pComponentPrivate;

  if (NULL == (p_msg = init_scheduler_message (ap_hdl,
                                               ETIZSchedMsgComponentRoleEnum,
                                               OMX_TRUE)))
    {
      return OMX_ErrorInsufficientResources;
    }

  /* Finish-up this message */
  p_msg_cre = &(p_msg->cre);
  assert (NULL != p_msg_cre);

  p_msg_cre->p_role = ap_role;
  p_msg_cre->index = a_index;

  return send_msg (p_sched, p_msg);
}

static OMX_BOOL
dispatch_msg (tiz_scheduler_t * ap_sched,
              tizsched_state_t * ap_state, tizsched_msg_t * ap_msg)
{
  OMX_BOOL signal_client = OMX_FALSE;
  OMX_ERRORTYPE rc = OMX_ErrorNone;

  assert (NULL != ap_sched);
  assert (NULL != ap_msg);
  assert (NULL != ap_state);

  assert (ap_msg->class < ETIZSchedMsgMax);

  TIZ_LOG_CNAME (TIZ_LOG_TRACE, TIZ_CNAME (ap_sched->child.p_hdl),
                 TIZ_CBUF (ap_sched->child.p_hdl),
                 "msg [%p] class [%s]",
                 ap_msg, tizsched_msg_to_str (ap_msg->class));

  signal_client = ap_msg->will_block;

  rc = tizsched_msg_to_fnt_tbl[ap_msg->class] (ap_sched, ap_state, ap_msg);

  /* Return error to client */
  ap_sched->error = rc;

  tiz_mem_free (ap_msg);

  return signal_client;
}

static void
schedule_servants (tiz_scheduler_t * ap_sched,
                   const tizsched_state_t ap_state)
{
  OMX_PTR *p_ready = NULL;
  OMX_ERRORTYPE rc = OMX_ErrorNone;

  assert (NULL != ap_sched);
  assert (ETIZSchedStateStopped < ap_state);

  if (ETIZSchedStateStarted != ap_state
      || NULL == ap_sched->child.p_prc
      || NULL == ap_sched->child.p_ker || NULL == ap_sched->child.p_fsm)
    {
      TIZ_LOG_CNAME (TIZ_LOG_TRACE, TIZ_CNAME (ap_sched->child.p_hdl),
                     TIZ_CBUF (ap_sched->child.p_hdl),
                     "Not ready prc [%p] fsm [%p] "
                     "ker [%p]",
                     ap_sched->child.p_prc,
                     ap_sched->child.p_fsm, ap_sched->child.p_ker);
      return;
    }

  /* Find the servant that is ready */
  /* Round-robin policy: fsm->ker->prc */
  TIZ_LOG_CNAME (TIZ_LOG_TRACE, TIZ_CNAME (ap_sched->child.p_hdl),
                 TIZ_CBUF (ap_sched->child.p_hdl),
                 "READY fsm [%s] ker [%s] prc [%s]",
                 tizservant_is_ready (ap_sched->child.p_fsm) ? "YES" : "NO",
                 tizservant_is_ready (ap_sched->child.p_ker) ? "YES" : "NO",
                 tizservant_is_ready (ap_sched->child.p_prc) ? "YES" : "NO");
  do
    {
      p_ready = NULL;
      if (tizservant_is_ready (ap_sched->child.p_fsm))
        {
          p_ready = ap_sched->child.p_fsm;
          TIZ_LOG_CNAME (TIZ_LOG_TRACE, TIZ_CNAME (ap_sched->child.p_hdl),
                         TIZ_CBUF (ap_sched->child.p_hdl), "FSM READY");
          rc = tizservant_tick (p_ready);
        }

      if (OMX_ErrorNone == rc && tizservant_is_ready (ap_sched->child.p_ker))
        {
          p_ready = ap_sched->child.p_ker;
          TIZ_LOG_CNAME (TIZ_LOG_TRACE, TIZ_CNAME (ap_sched->child.p_hdl),
                         TIZ_CBUF (ap_sched->child.p_hdl), "KRN READY");
          rc = tizservant_tick (p_ready);
        }

      if (OMX_ErrorNone == rc && tizservant_is_ready (ap_sched->child.p_prc))
        {
          p_ready = ap_sched->child.p_prc;
          TIZ_LOG_CNAME (TIZ_LOG_TRACE, TIZ_CNAME (ap_sched->child.p_hdl),
                         TIZ_CBUF (ap_sched->child.p_hdl), "PRC READY");
          rc = tizservant_tick (p_ready);
        }

      if (tiz_queue_length (ap_sched->p_queue))
        {
          break;
        }

    }
  while (p_ready && (OMX_ErrorNone == rc));

  if (OMX_ErrorNone != rc)
    {
      /* INFO: For now, errors are sent via EventHandler by the servants */
      /* TODO: Review errors allowed via EventHandler */
      /* TODO: Review if tizservant_tick should return void */
    }
}

static void *
il_sched_thread_func (void *p_arg)
{
  tiz_scheduler_t *p_sched = (tiz_scheduler_t *) (p_arg);
  OMX_PTR p_data = NULL;
  OMX_BOOL signal_client = OMX_FALSE;

  TIZ_LOG (TIZ_LOG_TRACE, "p_sched [%p]", p_sched);

  assert (NULL != p_sched);

  p_sched->thread_id = tiz_thread_id ();

  tiz_sem_post (&(p_sched->sem));
  TIZ_LOG (TIZ_LOG_TRACE, "signalled sem");

  for (;;)
    {
      TIZ_LOG_CNAME (TIZ_LOG_TRACE, TIZ_CNAME (p_sched->child.p_hdl),
                     TIZ_CBUF (p_sched->child.p_hdl), "waiting for msgs");
      /* TODO: Check ret val */
      tiz_queue_receive (p_sched->p_queue, &p_data);
      signal_client = dispatch_msg
        (p_sched, &(p_sched->state), (tizsched_msg_t *) p_data);

      if (signal_client)
        {
          tiz_sem_post (&(p_sched->sem));
        }

      if (ETIZSchedStateStopped == p_sched->state)
        {
          break;
        }

      schedule_servants (p_sched, p_sched->state);

    }

  TIZ_LOG (TIZ_LOG_TRACE, "exiting...");

  return NULL;
}

static OMX_ERRORTYPE
start_scheduler (tiz_scheduler_t * ap_sched)
{
  TIZ_LOG (TIZ_LOG_TRACE,
           "Starting IL component thread ap_sched [%p]...", ap_sched);

  assert (NULL != ap_sched);

  /* Create scheduler thread */
  tiz_mutex_lock (&(ap_sched->mutex));

  tiz_thread_create (&(ap_sched->thread),
                     0, 0, il_sched_thread_func, ap_sched);

  tiz_mutex_unlock (&(ap_sched->mutex));
  TIZ_LOG (TIZ_LOG_TRACE, "waiting on thread creation...");
  tiz_sem_wait (&(ap_sched->sem));
  TIZ_LOG (TIZ_LOG_TRACE, "thread creation complete...");

  return OMX_ErrorNone;
}

static void
delete_scheduler (tiz_scheduler_t * ap_sched)
{
  OMX_PTR p_result = NULL;
  peer_info_t *p_tmp_peer = NULL, *p_next_peer = NULL;

  assert (NULL != ap_sched);
  tiz_thread_join (&(ap_sched->thread), &p_result);

  p_tmp_peer = ap_sched->p_peers;
  while (p_tmp_peer)
    {
      p_next_peer = p_tmp_peer->p_next;
      tiz_sem_destroy (&(p_tmp_peer->sem));
      tiz_mutex_destroy (&(p_tmp_peer->mutex));
      tiz_mem_free (p_tmp_peer);
      p_tmp_peer = p_next_peer;
    }
  p_tmp_peer = NULL;

  delete_roles (ap_sched);

  tiz_mutex_destroy (&(ap_sched->mutex));
  tiz_sem_destroy (&(ap_sched->sem));
  tiz_sem_destroy (&(ap_sched->schedsem));
  tiz_sem_destroy (&(ap_sched->cbacksem));
  tiz_queue_destroy (ap_sched->p_queue);
  ap_sched->p_queue = NULL;
  tiz_mem_free (ap_sched);
}

static tiz_scheduler_t *
instantiate_scheduler (OMX_HANDLETYPE ap_hdl, const char *ap_cname)
{
  OMX_ERRORTYPE rc = OMX_ErrorNone;
  tiz_scheduler_t *p_sched = NULL;

  assert (NULL != ap_hdl);

  if (NULL == (p_sched = tiz_mem_calloc (1, sizeof (tiz_scheduler_t))))
    {
      return NULL;
    }

  TIZ_LOG (TIZ_LOG_TRACE,
           "Initializing component scheduler [%p]...", p_sched);

  if ((OMX_ErrorNone !=
       (rc = tiz_mutex_init (&(p_sched->mutex))))
      || (OMX_ErrorNone != (rc = tiz_sem_init (&(p_sched->sem), 0))))
    {
      TIZ_LOG (TIZ_LOG_TRACE,
               "Error Initializing scheduler [%p]...", p_sched);
      return NULL;
    }

  tiz_sem_init (&(p_sched->schedsem), 0);
  tiz_sem_init (&(p_sched->cbacksem), 0);

  if (OMX_ErrorNone != (rc = tiz_queue_init (&(p_sched->p_queue), 10)))
    {
      return NULL;
    }

  p_sched->child.p_fsm = NULL;
  p_sched->child.p_ker = NULL;
  p_sched->child.p_prc = NULL;
  p_sched->child.p_role_list = NULL;
  p_sched->child.nroles = 0;
  p_sched->child.p_hdl = ap_hdl;
  p_sched->error = OMX_ErrorNone;
  p_sched->state = ETIZSchedStateStarting;
  p_sched->p_servants = NULL;
  strncpy (p_sched->cname, ap_cname, OMX_MAX_STRINGNAME_SIZE);
  p_sched->cname[OMX_MAX_STRINGNAME_SIZE - 1] = '\0';
  p_sched->p_peers = NULL;
  p_sched->npeers = 0;

  TIZ_LOG (TIZ_LOG_TRACE, "Initialization success...");

  ((OMX_COMPONENTTYPE *) ap_hdl)->pComponentPrivate = p_sched;

  return (p_sched->error == OMX_ErrorNone) ? p_sched : NULL;
}

OMX_ERRORTYPE
tiz_receive_pluggable_event (OMX_HANDLETYPE ap_hdl, tizevent_t * ap_event)
{
  tizsched_msg_t *p_msg = NULL;
  tizsched_msg_plg_event_t *p_msg_pe = NULL;
  tiz_scheduler_t *p_sched = NULL;

  TIZ_LOG_CNAME (TIZ_LOG_TRACE, TIZ_CNAME (ap_hdl), TIZ_CBUF (ap_hdl),
                 "Receiving pluggable event [%p]", ap_event);

  assert (NULL != ap_hdl);
  assert (NULL != ap_event);

  p_sched = ((OMX_COMPONENTTYPE *) ap_hdl)->pComponentPrivate;

  if (NULL == (p_msg = init_scheduler_message (ap_hdl,
                                               ETIZSchedMsgPluggableEvent,
                                               OMX_FALSE)))
    {
      return OMX_ErrorInsufficientResources;
    }

  /* Finish-up this message */
  p_msg_pe = &(p_msg->pe);
  assert (NULL != p_msg_pe);

  p_msg_pe->p_event = ap_event;

  return send_msg (p_sched, p_msg);
}

OMX_ERRORTYPE
tiz_register_roles (OMX_HANDLETYPE ap_hdl,
                    const tiz_role_factory_t * ap_role_list[],
                    const OMX_U32 a_nroles)
{
  tizsched_msg_t *p_msg = NULL;
  tizsched_msg_regroles_t *p_msg_rr = NULL;
  tiz_scheduler_t *p_sched = NULL;

  assert (NULL != ap_hdl);
  assert (NULL != ap_role_list);
  assert (0 < a_nroles);
  assert (a_nroles <= TIZ_MAX_ROLES);

  TIZ_LOG_CNAME (TIZ_LOG_TRACE, TIZ_CNAME (ap_hdl), TIZ_CBUF (ap_hdl),
                 "Registering [%d] roles", a_nroles);

  p_sched = ((OMX_COMPONENTTYPE *) ap_hdl)->pComponentPrivate;

  if (NULL == (p_msg = init_scheduler_message (ap_hdl,
                                               ETIZSchedMsgRegisterRoles,
                                               OMX_TRUE)))
    {
      return OMX_ErrorInsufficientResources;
    }

  /* Finish-up this message */
  p_msg_rr = &(p_msg->rr);
  assert (NULL != p_msg_rr);

  p_msg_rr->p_role_list = ap_role_list;
  p_msg_rr->nroles = a_nroles;

  return send_msg (p_sched, p_msg);
}

OMX_ERRORTYPE
tiz_register_port_alloc_hooks (OMX_HANDLETYPE ap_hdl,
                               const OMX_U32 a_pid,
                               const tiz_port_alloc_hooks_t * ap_new_hooks,
                               tiz_port_alloc_hooks_t * ap_old_hooks)
{
  tizsched_msg_t *p_msg = NULL;
  tizsched_msg_regphooks_t *p_msg_rph = NULL;
  tiz_scheduler_t *p_sched = NULL;

  assert (NULL != ap_hdl);
  assert (NULL != ap_new_hooks);

  TIZ_LOG_CNAME (TIZ_LOG_TRACE, TIZ_CNAME (ap_hdl), TIZ_CBUF (ap_hdl),
                 "Registering alloc hooks for port [%d] ", a_pid);

  p_sched = ((OMX_COMPONENTTYPE *) ap_hdl)->pComponentPrivate;

  if (NULL == (p_msg = init_scheduler_message (ap_hdl,
                                               ETIZSchedMsgRegisterPortHooks,
                                               OMX_TRUE)))
    {
      return OMX_ErrorInsufficientResources;
    }

  /* Finish-up this message */
  p_msg_rph = &(p_msg->rph);
  assert (NULL != p_msg_rph);

  p_msg_rph->pid = a_pid;
  p_msg_rph->p_hooks = ap_new_hooks;
  p_msg_rph->p_old_hooks = ap_old_hooks;

  return send_msg (p_sched, p_msg);
}

static OMX_ERRORTYPE
init_servants (tiz_scheduler_t * ap_sched, tizsched_msg_t * ap_msg)
{

  OMX_COMPONENTTYPE *p_hdl = ap_sched->child.p_hdl;

  TIZ_LOG_CNAME (TIZ_LOG_TRACE, TIZ_CNAME (ap_sched->child.p_hdl),
                 TIZ_CBUF (ap_sched->child.p_hdl),
                 "Initializing the servants...");

  assert (NULL != ap_sched);
  assert (NULL != ap_msg);

  /* Init the component hdl */
  p_hdl->nVersion.s.nVersionMajor = 1;
  p_hdl->nVersion.s.nVersionMinor = 0;
  p_hdl->nVersion.s.nRevision = 0;
  p_hdl->nVersion.s.nStep = 0;
  p_hdl->pComponentPrivate = ap_sched;
  p_hdl->GetComponentVersion = tizsched_GetComponentVersion;
  p_hdl->SendCommand = tizsched_SendCommand;
  p_hdl->GetParameter = tizsched_GetParameter;
  p_hdl->SetParameter = tizsched_SetParameter;
  p_hdl->GetConfig = tizsched_GetConfig;
  p_hdl->SetConfig = tizsched_SetConfig;
  p_hdl->GetExtensionIndex = tizsched_GetExtensionIndex;
  p_hdl->GetState = tizsched_GetState;
  p_hdl->ComponentTunnelRequest = tizsched_ComponentTunnelRequest;
  p_hdl->UseBuffer = tizsched_UseBuffer;
  p_hdl->AllocateBuffer = tizsched_AllocateBuffer;
  p_hdl->FreeBuffer = tizsched_FreeBuffer;
  p_hdl->EmptyThisBuffer = tizsched_EmptyThisBuffer;
  p_hdl->FillThisBuffer = tizsched_FillThisBuffer;
  p_hdl->SetCallbacks = tizsched_SetCallbacks;
  p_hdl->ComponentDeInit = tizsched_ComponentDeInit;
  p_hdl->UseEGLImage = tizsched_UseEGLImage;
  p_hdl->ComponentRoleEnum = tizsched_ComponentRoleEnum;

  /* Init the small object allocator */
  tiz_soa_init (&(ap_sched->p_soa));

  /* Init the FSM */
  init_tizfsm ();
  ap_sched->child.p_fsm = factory_new (tizfsm, p_hdl);

  /* Init the kernel */
  init_tizkernel ();
  ap_sched->child.p_ker = factory_new (tizkernel, p_hdl);

  /* All the servants use the same small object allocator */
  tizservant_set_allocator (ap_sched->child.p_fsm, ap_sched->p_soa);
  tizservant_set_allocator (ap_sched->child.p_ker, ap_sched->p_soa);

  return OMX_ErrorNone;

}

static OMX_ERRORTYPE
deinit_servants (tiz_scheduler_t * ap_sched, tizsched_msg_t * ap_msg)
{
  TIZ_LOG_CNAME (TIZ_LOG_TRACE, TIZ_CNAME (ap_sched->child.p_hdl),
                 TIZ_CBUF (ap_sched->child.p_hdl),
                 "Deinitializing the servants...");

  assert (NULL != ap_sched);
  assert (NULL != ap_msg);

  /* delete the processor servant */
  factory_delete (ap_sched->child.p_prc);
  ap_sched->child.p_prc = NULL;

  /* delete the kernel servant */
  factory_delete (ap_sched->child.p_ker);
  ap_sched->child.p_ker = NULL;

  /* delete the FSM servant */
  factory_delete (ap_sched->child.p_fsm);
  ap_sched->child.p_fsm = NULL;

  /* Destroy the object allocator used by the servants */
  tiz_soa_destroy (ap_sched->p_soa);
  ap_sched->p_soa = NULL;

  return OMX_ErrorNone;
}

static OMX_ERRORTYPE
init_and_register_role (tiz_scheduler_t * ap_sched, const OMX_U32 a_role_pos)
{
  OMX_ERRORTYPE rc = OMX_ErrorNone;
  tiz_role_factory_t *p_rf = NULL;
  OMX_PTR p_port = NULL;
  OMX_PTR p_proc = NULL;
  OMX_HANDLETYPE p_hdl = NULL;
  OMX_U32 j = 0;

  assert (NULL != ap_sched);

  p_hdl = ap_sched->child.p_hdl;
  p_rf = ap_sched->child.p_role_list[a_role_pos];
  p_port = p_rf->pf_cport (p_hdl);

  assert (NULL != p_port);

  rc = tizkernel_register_port (ap_sched->child.p_ker, p_port, OMX_TRUE);       /* it is a config port */

  TIZ_LOG_CNAME (TIZ_LOG_TRACE, TIZ_CNAME (ap_sched->child.p_hdl),
                 TIZ_CBUF (ap_sched->child.p_hdl),
                 "Registering role #[%d] -> [%s] nports = [%d] rc = [%s]...",
                 a_role_pos, p_rf->role, p_rf->nports, tiz_err_to_str (rc));

  for (j = 0; j < p_rf->nports && rc == OMX_ErrorNone; ++j)
    {
      /* Instantiate the port */
      p_port = p_rf->pf_port[j] (p_hdl);
      assert (NULL != p_port);
      rc = tizkernel_register_port (ap_sched->child.p_ker, p_port, OMX_FALSE);  /* not a config port */

      rc = configure_port_preannouncements (ap_sched, p_hdl, p_port);
    }

  if (OMX_ErrorNone == rc)
    {
      p_proc = p_rf->pf_proc (p_hdl);
      assert (NULL != p_proc);
      assert (NULL == ap_sched->child.p_prc);
      ap_sched->child.p_prc = p_proc;

      /* All servants will use the same object allocator */
      tizservant_set_allocator (p_proc, ap_sched->p_soa);
    }

  return rc;
}

void
tiz_receive_event_io (OMX_HANDLETYPE ap_hdl, tiz_event_io_t * ap_ev_io, int a_fd,
                    int a_events)
{
  tizsched_msg_t *p_msg = NULL;
  tizsched_msg_ev_io_t *p_msg_eio = NULL;
  tiz_scheduler_t *p_sched = NULL;

  assert (NULL != ap_hdl);
  assert (NULL != ap_ev_io);

  TIZ_LOG_CNAME (TIZ_LOG_NOTICE, TIZ_CNAME (ap_hdl), TIZ_CBUF (ap_hdl),
                 "Receiving an io event on fd [%d] ", a_fd);

  p_sched = ((OMX_COMPONENTTYPE *) ap_hdl)->pComponentPrivate;

  if (NULL == (p_msg = init_scheduler_message (ap_hdl,
                                               ETIZSchedMsgEvIo,
                                               OMX_FALSE)))
    {
      TIZ_LOG_CNAME (TIZ_LOG_ERROR, TIZ_CNAME (ap_hdl), TIZ_CBUF (ap_hdl),
                 "[OMX_ErrorInsufficientResources] : Unable to receive event");
      return;
    }

  /* Finish-up this message */
  p_msg_eio = &(p_msg->eio);
  assert (NULL != p_msg_eio);

  p_msg_eio->p_ev_io = ap_ev_io;
  p_msg_eio->fd = a_fd;
  p_msg_eio->events = a_events;

  /* TODO: Shouldn't mask this return code */
  (void) send_msg (p_sched, p_msg);
}

void
tiz_receive_event_timer (OMX_HANDLETYPE ap_hdl, tiz_event_timer_t * ap_ev_timer)
{
  tizsched_msg_t *p_msg = NULL;
  tizsched_msg_ev_timer_t *p_msg_etmr = NULL;
  tiz_scheduler_t *p_sched = NULL;

  assert (NULL != ap_hdl);
  assert (NULL != ap_ev_timer);

  TIZ_LOG_CNAME (TIZ_LOG_TRACE, TIZ_CNAME (ap_hdl), TIZ_CBUF (ap_hdl),
                 "Receiving a timer event");

  p_sched = ((OMX_COMPONENTTYPE *) ap_hdl)->pComponentPrivate;

  if (NULL == (p_msg = init_scheduler_message (ap_hdl,
                                               ETIZSchedMsgEvTimer,
                                               OMX_FALSE)))
    {
      TIZ_LOG_CNAME (TIZ_LOG_ERROR, TIZ_CNAME (ap_hdl), TIZ_CBUF (ap_hdl),
                 "[OMX_ErrorInsufficientResources] : Unable to receive event");
      return;
    }

  /* Finish-up this message */
  p_msg_etmr = &(p_msg->etmr);
  assert (NULL != p_msg_etmr);

  p_msg_etmr->p_ev_timer = ap_ev_timer;

  /* TODO: Shouldn't mask this return code */
  (void) send_msg (p_sched, p_msg);
}

void
tiz_receive_event_stat (OMX_HANDLETYPE ap_hdl, tiz_event_stat_t * ap_ev_stat,
                      int a_events)
{
  tizsched_msg_t *p_msg = NULL;
  tizsched_msg_ev_stat_t *p_msg_estat = NULL;
  tiz_scheduler_t *p_sched = NULL;

  assert (NULL != ap_hdl);
  assert (NULL != ap_ev_stat);

  TIZ_LOG_CNAME (TIZ_LOG_TRACE, TIZ_CNAME (ap_hdl), TIZ_CBUF (ap_hdl),
                 "Receiving a stat event");

  p_sched = ((OMX_COMPONENTTYPE *) ap_hdl)->pComponentPrivate;

  if (NULL == (p_msg = init_scheduler_message (ap_hdl,
                                               ETIZSchedMsgEvStat,
                                               OMX_FALSE)))
    {
      TIZ_LOG_CNAME (TIZ_LOG_ERROR, TIZ_CNAME (ap_hdl), TIZ_CBUF (ap_hdl),
                 "[OMX_ErrorInsufficientResources] : Unable to receive event");
      return;
    }

  /* Finish-up this message */
  p_msg_estat = &(p_msg->estat);
  assert (NULL != p_msg_estat);

  p_msg_estat->p_ev_stat = ap_ev_stat;

  /* TODO: Shouldn't mask this return code */
  (void) send_msg (p_sched, p_msg);
}

void *
tiz_get_sched (const OMX_HANDLETYPE ap_hdl)
{
  return ((OMX_COMPONENTTYPE *) ap_hdl)->pComponentPrivate;
}

void *
tiz_get_fsm (const OMX_HANDLETYPE ap_hdl)
{
  tiz_scheduler_t *p_sched = NULL;
  assert (NULL != ap_hdl);
  p_sched = ((OMX_COMPONENTTYPE *) ap_hdl)->pComponentPrivate;
  return p_sched->child.p_fsm;
}

void *
tiz_get_krn (const OMX_HANDLETYPE ap_hdl)
{
  tiz_scheduler_t *p_sched = NULL;
  assert (NULL != ap_hdl);
  p_sched = ((OMX_COMPONENTTYPE *) ap_hdl)->pComponentPrivate;
  return p_sched->child.p_ker;
}

void *
tiz_get_prc (const OMX_HANDLETYPE ap_hdl)
{
  tiz_scheduler_t *p_sched = NULL;
  assert (NULL != ap_hdl);
  p_sched = ((OMX_COMPONENTTYPE *) ap_hdl)->pComponentPrivate;
  return p_sched->child.p_prc;
}
