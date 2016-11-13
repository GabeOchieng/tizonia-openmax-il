/**
 * Copyright (C) 2011-2016 Aratelia Limited - Juan A. Rubio
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
 * @file   youtubecfgport.c
 * @author Juan A. Rubio <juan.rubio@aratelia.com>
 *
 * @brief A specialised config port class for the Youtube source component
 *
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <assert.h>
#include <string.h>
#include <limits.h>

#include <tizplatform.h>

#include "youtubecfgport.h"
#include "youtubecfgport_decls.h"

#ifdef TIZ_LOG_CATEGORY_NAME
#undef TIZ_LOG_CATEGORY_NAME
#define TIZ_LOG_CATEGORY_NAME "tiz.http_source.cfgport.youtube"
#endif

/*
 * youtubecfgport class
 */

static void *
youtube_cfgport_ctor (void * ap_obj, va_list * app)
{
  youtube_cfgport_t * p_obj
    = super_ctor (typeOf (ap_obj, "youtubecfgport"), ap_obj, app);

  assert (p_obj);

  tiz_check_omx_ret_null (
    tiz_port_register_index (p_obj, OMX_TizoniaIndexParamAudioYoutubeSession));
  tiz_check_omx_ret_null (
    tiz_port_register_index (p_obj, OMX_TizoniaIndexParamAudioYoutubePlaylist));

  /* Initialize the OMX_TIZONIA_AUDIO_PARAM_YOUTUBESESSIONTYPE structure */
  TIZ_INIT_OMX_STRUCT (p_obj->session_);
  snprintf ((char *) p_obj->session_.cApiKey, sizeof (p_obj->session_.cApiKey),
            "xyzxyzxyzxyzxyz");

  /* Initialize the OMX_TIZONIA_AUDIO_PARAM_YOUTUBEPLAYLISTTYPE structure */
  TIZ_INIT_OMX_STRUCT (p_obj->playlist_);
  snprintf ((char *) p_obj->playlist_.cPlaylistName,
            sizeof (p_obj->playlist_.cPlaylistName), "playlist");
  p_obj->playlist_.ePlaylistType = OMX_AUDIO_YoutubePlaylistTypeUnknown;
  p_obj->playlist_.bShuffle = OMX_FALSE;

  return p_obj;
}

static void *
youtube_cfgport_dtor (void * ap_obj)
{
  return super_dtor (typeOf (ap_obj, "youtubecfgport"), ap_obj);
}

/*
 * from tiz_api
 */

static OMX_ERRORTYPE
youtube_cfgport_GetParameter (const void * ap_obj, OMX_HANDLETYPE ap_hdl,
                             OMX_INDEXTYPE a_index, OMX_PTR ap_struct)
{
  const youtube_cfgport_t * p_obj = ap_obj;
  OMX_ERRORTYPE rc = OMX_ErrorNone;

  assert (p_obj);

  TIZ_TRACE (ap_hdl, "PORT [%d] GetParameter [%s]...", tiz_port_index (ap_obj),
             tiz_idx_to_str (a_index));

  if (OMX_TizoniaIndexParamAudioYoutubeSession == a_index)
    {
      memcpy (ap_struct, &(p_obj->session_),
              sizeof (OMX_TIZONIA_AUDIO_PARAM_YOUTUBESESSIONTYPE));
    }
  else if (OMX_TizoniaIndexParamAudioYoutubePlaylist == a_index)
    {
      memcpy (ap_struct, &(p_obj->playlist_),
              sizeof (OMX_TIZONIA_AUDIO_PARAM_YOUTUBEPLAYLISTTYPE));
    }
  else
    {
      /* Delegate to the base port */
      rc = super_GetParameter (typeOf (ap_obj, "youtubecfgport"), ap_obj, ap_hdl,
                               a_index, ap_struct);
    }

  return rc;
}

static OMX_ERRORTYPE
youtube_cfgport_SetParameter (const void * ap_obj, OMX_HANDLETYPE ap_hdl,
                             OMX_INDEXTYPE a_index, OMX_PTR ap_struct)
{
  youtube_cfgport_t * p_obj = (youtube_cfgport_t *) ap_obj;
  OMX_ERRORTYPE rc = OMX_ErrorNone;

  assert (p_obj);

  TIZ_TRACE (ap_hdl, "PORT [%d] GetParameter [%s]...", tiz_port_index (ap_obj),
             tiz_idx_to_str (a_index));

  if (OMX_TizoniaIndexParamAudioYoutubeSession == a_index)
    {
      memcpy (&(p_obj->session_), ap_struct,
              sizeof (OMX_TIZONIA_AUDIO_PARAM_YOUTUBESESSIONTYPE));
      p_obj->session_.cApiKey[OMX_MAX_STRINGNAME_SIZE - 1] = '\000';
      TIZ_TRACE (ap_hdl, "Youtube Api Key [%s]...", p_obj->session_.cApiKey);
    }
  else if (OMX_TizoniaIndexParamAudioYoutubePlaylist == a_index)
    {
      memcpy (&(p_obj->playlist_), ap_struct,
              sizeof (OMX_TIZONIA_AUDIO_PARAM_YOUTUBEPLAYLISTTYPE));
      p_obj->playlist_.cPlaylistName[OMX_MAX_STRINGNAME_SIZE - 1] = '\000';
      TIZ_TRACE (ap_hdl, "Youtube playlist [%s]...",
                 p_obj->playlist_.cPlaylistName);
    }
  else
    {
      /* Delegate to the base port */
      rc = super_SetParameter (typeOf (ap_obj, "youtubecfgport"), ap_obj, ap_hdl,
                               a_index, ap_struct);
    }

  return rc;
}

/*
 * youtube_cfgport_class
 */

static void *
youtube_cfgport_class_ctor (void * ap_obj, va_list * app)
{
  /* NOTE: Class methods might be added in the future. None for now. */
  return super_ctor (typeOf (ap_obj, "youtubecfgport_class"), ap_obj, app);
}

/*
 * initialization
 */

void *
youtube_cfgport_class_init (void * ap_tos, void * ap_hdl)
{
  void * tizconfigport = tiz_get_type (ap_hdl, "tizconfigport");
  void * youtubecfgport_class
    = factory_new (classOf (tizconfigport), "youtubecfgport_class",
                   classOf (tizconfigport), sizeof (youtube_cfgport_class_t),
                   ap_tos, ap_hdl, ctor, youtube_cfgport_class_ctor, 0);
  return youtubecfgport_class;
}

void *
youtube_cfgport_init (void * ap_tos, void * ap_hdl)
{
  void * tizconfigport = tiz_get_type (ap_hdl, "tizconfigport");
  void * youtubecfgport_class = tiz_get_type (ap_hdl, "youtubecfgport_class");
  TIZ_LOG_CLASS (youtubecfgport_class);
  void * youtubecfgport = factory_new
    /* TIZ_CLASS_COMMENT: class type, class name, parent, size */
    (youtubecfgport_class, "youtubecfgport", tizconfigport,
     sizeof (youtube_cfgport_t),
     /* TIZ_CLASS_COMMENT: class constructor */
     ap_tos, ap_hdl,
     /* TIZ_CLASS_COMMENT: class constructor */
     ctor, youtube_cfgport_ctor,
     /* TIZ_CLASS_COMMENT: class destructor */
     dtor, youtube_cfgport_dtor,
     /* TIZ_CLASS_COMMENT: */
     tiz_api_GetParameter, youtube_cfgport_GetParameter,
     /* TIZ_CLASS_COMMENT: */
     tiz_api_SetParameter, youtube_cfgport_SetParameter,
     /* TIZ_CLASS_COMMENT: stop value*/
     0);

  return youtubecfgport;
}