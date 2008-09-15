
/*******************************************************************************
 * xspf.c : XSPF playlist import functions
 *******************************************************************************
 * Copyright (C) 2006 the VideoLAN team
 * $Id$
 *
 * Authors: Daniel Stränger <vlc at schmaller dot de>
 *          Yoann Peronneau <yoann@videolan.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 ******************************************************************************/
/**
 * \file modules/demux/playlist/xspf.c
 * \brief XSPF playlist import functions
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc_common.h>
#include <vlc_demux.h>

#include <vlc_xml.h>
#include <vlc_strings.h>
#include <vlc_url.h>
#include "xspf.h"
#include "playlist.h"

struct demux_sys_t
{
    input_item_t **pp_tracklist;
    int i_tracklist_entries;
    int i_identifier;
    char * psz_base;
};

static int Control( demux_t *, int, va_list );
static int Demux( demux_t * );

/**
 * \brief XSPF submodule initialization function
 */
int Import_xspf( vlc_object_t *p_this )
{
    DEMUX_BY_EXTENSION_OR_FORCED_MSG( ".xspf", "xspf-open",
                                      "using XSPF playlist reader" );
    return VLC_SUCCESS;
}

void Close_xspf( vlc_object_t *p_this )
{
    demux_t *p_demux = (demux_t *)p_this;
    int i;
    for(i = 0; i < p_demux->p_sys->i_tracklist_entries; i++)
    {
        if(p_demux->p_sys->pp_tracklist[i])
            vlc_gc_decref( p_demux->p_sys->pp_tracklist[i] );
    }
    free( p_demux->p_sys->pp_tracklist );
    free( p_demux->p_sys->psz_base );
    free( p_demux->p_sys );
}

/**
 * \brief demuxer function for XSPF parsing
 */
int Demux( demux_t *p_demux )
{
    int i_ret = 1;
    xml_t *p_xml = NULL;
    xml_reader_t *p_xml_reader = NULL;
    char *psz_name = NULL;
    INIT_PLAYLIST_STUFF;
    p_demux->p_sys->pp_tracklist = NULL;
    p_demux->p_sys->i_tracklist_entries = 0;
    p_demux->p_sys->i_identifier = 0;
    p_demux->p_sys->psz_base = NULL;

    /* create new xml parser from stream */
    p_xml = xml_Create( p_demux );
    if( !p_xml )
        i_ret = -1;
    else
    {
        p_xml_reader = xml_ReaderCreate( p_xml, p_demux->s );
        if( !p_xml_reader )
            i_ret = -1;
    }

    /* locating the root node */
    if( i_ret == 1 )
    {
        do
        {
            if( xml_ReaderRead( p_xml_reader ) != 1 )
            {
                msg_Err( p_demux, "can't read xml stream" );
                i_ret = -1;
            }
        } while( i_ret == VLC_SUCCESS &&
                 xml_ReaderNodeType( p_xml_reader ) != XML_READER_STARTELEM );
    }
    /* checking root node name */
    if( i_ret == 1 )
    {
        psz_name = xml_ReaderName( p_xml_reader );
        if( !psz_name || strcmp( psz_name, "playlist" ) )
        {
            msg_Err( p_demux, "invalid root node name: %s", psz_name );
            i_ret = -1;
        }
        FREE_NAME();
    }

    if( i_ret == 1 )
        i_ret = parse_playlist_node( p_demux, p_current_input,
                                     p_xml_reader, "playlist" ) ? 0 : -1;

    int i;
    for( i = 0 ; i < p_demux->p_sys->i_tracklist_entries ; i++ )
    {
        input_item_t *p_new_input = p_demux->p_sys->pp_tracklist[i];
        if( p_new_input )
        {
            input_item_AddSubItem( p_current_input, p_new_input );
        }
    }

    HANDLE_PLAY_AND_RELEASE;
    if( p_xml_reader )
        xml_ReaderDelete( p_xml, p_xml_reader );
    if( p_xml )
        xml_Delete( p_xml );
    return i_ret; /* Needed for correct operation of go back */
}

/** \brief dummy function for demux callback interface */
static int Control( demux_t *p_demux, int i_query, va_list args )
{
    VLC_UNUSED(p_demux); VLC_UNUSED(i_query); VLC_UNUSED(args);
    return VLC_EGENERIC;
}

/**
 * \brief parse the root node of a XSPF playlist
 * \param p_demux demuxer instance
 * \param p_input_item current input item
 * \param p_xml_reader xml reader instance
 * \param psz_element name of element to parse
 */
static bool parse_playlist_node COMPLEX_INTERFACE
{
    char *psz_name=NULL;
    char *psz_value=NULL;
    bool b_version_found = false;
    int i_node;
    xml_elem_hnd_t *p_handler=NULL;

    xml_elem_hnd_t pl_elements[] =
        { {"title",        SIMPLE_CONTENT,  {.smpl = set_item_info} },
          {"creator",      SIMPLE_CONTENT,  {.smpl = set_item_info} },
          {"annotation",   SIMPLE_CONTENT,  {.smpl = set_item_info} },
          {"info",         SIMPLE_CONTENT,  {NULL} },
          {"location",     SIMPLE_CONTENT,  {NULL} },
          {"identifier",   SIMPLE_CONTENT,  {NULL} },
          {"image",        SIMPLE_CONTENT,  {.smpl = set_item_info} },
          {"date",         SIMPLE_CONTENT,  {NULL} },
          {"license",      SIMPLE_CONTENT,  {NULL} },
          {"attribution",  COMPLEX_CONTENT, {.cmplx = skip_element} },
          {"link",         SIMPLE_CONTENT,  {NULL} },
          {"meta",         SIMPLE_CONTENT,  {NULL} },
          {"extension",    COMPLEX_CONTENT, {.cmplx = parse_extension_node} },
          {"trackList",    COMPLEX_CONTENT, {.cmplx = parse_tracklist_node} },
          {NULL,           UNKNOWN_CONTENT, {NULL} }
        };

    /* read all playlist attributes */
    while( xml_ReaderNextAttr( p_xml_reader ) == VLC_SUCCESS )
    {
        psz_name = xml_ReaderName( p_xml_reader );
        psz_value = xml_ReaderValue( p_xml_reader );
        if( !psz_name || !psz_value )
        {
            msg_Err( p_demux, "invalid xml stream @ <playlist>" );
            FREE_ATT();
            return false;
        }
        /* attribute: version */
        if( !strcmp( psz_name, "version" ) )
        {
            b_version_found = true;
            if( strcmp( psz_value, "0" ) && strcmp( psz_value, "1" ) )
                msg_Warn( p_demux, "unsupported XSPF version" );
        }
        /* attribute: xmlns */
        else if( !strcmp( psz_name, "xmlns" ) )
            ;
        else if( !strcmp( psz_name, "xml:base" ) )
        {
            p_demux->p_sys->psz_base = decode_URI_duplicate( psz_value );
        }
        /* unknown attribute */
        else
            msg_Warn( p_demux, "invalid <playlist> attribute:\"%s\"", psz_name);

        FREE_ATT();
    }
    /* attribute version is mandatory !!! */
    if( !b_version_found )
        msg_Warn( p_demux, "<playlist> requires \"version\" attribute" );

    /* parse the child elements - we only take care of <trackList> */
    while( xml_ReaderRead( p_xml_reader ) == 1 )
    {
        i_node = xml_ReaderNodeType( p_xml_reader );
        switch( i_node )
        {
            case XML_READER_NONE:
                break;
            case XML_READER_STARTELEM:
                /*  element start tag  */
                psz_name = xml_ReaderName( p_xml_reader );
                if( !psz_name || !*psz_name )
                {
                    msg_Err( p_demux, "invalid xml stream" );
                    FREE_ATT();
                    return false;
                }
                /* choose handler */
                for( p_handler = pl_elements;
                     p_handler->name && strcmp( psz_name, p_handler->name );
                     p_handler++ );
                if( !p_handler->name )
                {
                    msg_Err( p_demux, "unexpected element <%s>", psz_name );
                    FREE_ATT();
                    return false;
                }
                FREE_NAME();
                /* complex content is parsed in a separate function */
                if( p_handler->type == COMPLEX_CONTENT )
                {
                    if( p_handler->pf_handler.cmplx( p_demux,
                                                     p_input_item,
                                                     p_xml_reader,
                                                     p_handler->name ) )
                    {
                        p_handler = NULL;
                        FREE_ATT();
                    }
                    else
                    {
                        FREE_ATT();
                        return false;
                    }
                }
                break;

            case XML_READER_TEXT:
                /* simple element content */
                FREE_ATT();
                psz_value = xml_ReaderValue( p_xml_reader );
                if( !psz_value )
                {
                    msg_Err( p_demux, "invalid xml stream" );
                    FREE_ATT();
                    return false;
                }
                break;

            case XML_READER_ENDELEM:
                /* element end tag */
                psz_name = xml_ReaderName( p_xml_reader );
                if( !psz_name )
                {
                    msg_Err( p_demux, "invalid xml stream" );
                    FREE_ATT();
                    return false;
                }
                /* leave if the current parent node <playlist> is terminated */
                if( !strcmp( psz_name, psz_element ) )
                {
                    FREE_ATT();
                    return true;
                }
                /* there MUST have been a start tag for that element name */
                if( !p_handler || !p_handler->name
                    || strcmp( p_handler->name, psz_name ))
                {
                    msg_Err( p_demux, "there's no open element left for <%s>",
                             psz_name );
                    FREE_ATT();
                    return false;
                }

                if( p_handler->pf_handler.smpl )
                {
                    p_handler->pf_handler.smpl( p_input_item, p_handler->name,
                                                psz_value );
                }
                FREE_ATT();
                p_handler = NULL;
                break;

            default:
                /* unknown/unexpected xml node */
                msg_Err( p_demux, "unexpected xml node %i", i_node );
                FREE_ATT();
                return false;
        }
        FREE_NAME();
    }
    return false;
}

/**
 * \brief parses the tracklist node which only may contain <track>s
 */
static bool parse_tracklist_node COMPLEX_INTERFACE
{
    VLC_UNUSED(psz_element);
    char *psz_name=NULL;
    int i_node;
    int i_ntracks = 0;

    /* now parse the <track>s */
    while( xml_ReaderRead( p_xml_reader ) == 1 )
    {
        i_node = xml_ReaderNodeType( p_xml_reader );
        if( i_node == XML_READER_STARTELEM )
        {
            psz_name = xml_ReaderName( p_xml_reader );
            if( !psz_name )
            {
                msg_Err( p_demux, "unexpected end of xml data" );
                FREE_NAME();
                return false;
            }
            if( strcmp( psz_name, "track") )
            {
                msg_Err( p_demux, "unexpected child of <trackList>: <%s>",
                         psz_name );
                FREE_NAME();
                return false;
            }
            FREE_NAME();

            /* parse the track data in a separate function */
            if( parse_track_node( p_demux, p_input_item,
                                   p_xml_reader,"track" ) == true )
                i_ntracks++;
        }
        else if( i_node == XML_READER_ENDELEM )
            break;
    }

    /* the <trackList> has to be terminated */
    if( xml_ReaderNodeType( p_xml_reader ) != XML_READER_ENDELEM )
    {
        msg_Err( p_demux, "there's a missing </trackList>" );
        FREE_NAME();
        return false;
    }
    psz_name = xml_ReaderName( p_xml_reader );
    if( !psz_name || strcmp( psz_name, "trackList" ) )
    {
        msg_Err( p_demux, "expected: </trackList>, found: </%s>", psz_name );
        FREE_NAME();
        return false;
    }
    FREE_NAME();

    msg_Dbg( p_demux, "parsed %i tracks successfully", i_ntracks );

    return true;
}

/**
 * \brief parse one track element
 * \param COMPLEX_INTERFACE
 */
static bool parse_track_node COMPLEX_INTERFACE
{
    input_item_t *p_new_input = NULL;
    int i_node;
    char *psz_name=NULL;
    char *psz_value=NULL;
    xml_elem_hnd_t *p_handler=NULL;

    xml_elem_hnd_t track_elements[] =
        { {"location",     SIMPLE_CONTENT,  {NULL} },
          {"identifier",   SIMPLE_CONTENT,  {NULL} },
          {"title",        SIMPLE_CONTENT,  {.smpl = set_item_info} },
          {"creator",      SIMPLE_CONTENT,  {.smpl = set_item_info} },
          {"annotation",   SIMPLE_CONTENT,  {.smpl = set_item_info} },
          {"info",         SIMPLE_CONTENT,  {NULL} },
          {"image",        SIMPLE_CONTENT,  {.smpl = set_item_info} },
          {"album",        SIMPLE_CONTENT,  {.smpl = set_item_info} },
          {"trackNum",     SIMPLE_CONTENT,  {.smpl = set_item_info} },
          {"duration",     SIMPLE_CONTENT,  {.smpl = set_item_info} },
          {"link",         SIMPLE_CONTENT,  {NULL} },
          {"meta",         SIMPLE_CONTENT,  {NULL} },
          {"extension",    COMPLEX_CONTENT, {.cmplx = parse_extension_node} },
          {NULL,           UNKNOWN_CONTENT, {NULL} }
        };

    while( xml_ReaderRead( p_xml_reader ) == 1 )
    {
        i_node = xml_ReaderNodeType( p_xml_reader );
        switch( i_node )
        {
            case XML_READER_NONE:
                break;

            case XML_READER_STARTELEM:
                /*  element start tag  */
                psz_name = xml_ReaderName( p_xml_reader );
                if( !psz_name || !*psz_name )
                {
                    msg_Err( p_demux, "invalid xml stream" );
                    FREE_ATT();
                    return false;
                }
                /* choose handler */
                for( p_handler = track_elements;
                     p_handler->name && strcmp( psz_name, p_handler->name );
                     p_handler++ );
                if( !p_handler->name )
                {
                    msg_Err( p_demux, "unexpected element <%s>", psz_name );
                    FREE_ATT();
                    return false;
                }
                FREE_NAME();
                /* complex content is parsed in a separate function */
                if( p_handler->type == COMPLEX_CONTENT )
                {
                    if( !p_new_input )
                    {
                        msg_Err( p_demux,
                                 "at <%s> level no new item has been allocated",
                                 p_handler->name );
                        FREE_ATT();
                        return false;
                    }
                    if( p_handler->pf_handler.cmplx( p_demux,
                                                     p_new_input,
                                                     p_xml_reader,
                                                     p_handler->name ) )
                    {
                        p_handler = NULL;
                        FREE_ATT();
                    }
                    else
                    {
                        FREE_ATT();
                        return false;
                    }
                }
                break;

            case XML_READER_TEXT:
                /* simple element content */
                FREE_ATT();
                psz_value = xml_ReaderValue( p_xml_reader );
                if( !psz_value )
                {
                    msg_Err( p_demux, "invalid xml stream" );
                    FREE_ATT();
                    return false;
                }
                break;

            case XML_READER_ENDELEM:
                /* element end tag */
                psz_name = xml_ReaderName( p_xml_reader );
                if( !psz_name )
                {
                    msg_Err( p_demux, "invalid xml stream" );
                    FREE_ATT();
                    return false;
                }
                /* leave if the current parent node <track> is terminated */
                if( !strcmp( psz_name, psz_element ) )
                {
                    FREE_ATT();
                    if( p_demux->p_sys->i_identifier <
                        p_demux->p_sys->i_tracklist_entries )
                    {
                        p_demux->p_sys->pp_tracklist[
                            p_demux->p_sys->i_identifier ] = p_new_input;
                    }
                    else
                    {
                        if( p_demux->p_sys->i_identifier >
                            p_demux->p_sys->i_tracklist_entries )
                        {
                            p_demux->p_sys->i_tracklist_entries =
                                p_demux->p_sys->i_identifier;
                        }
                        INSERT_ELEM( p_demux->p_sys->pp_tracklist,
                                     p_demux->p_sys->i_tracklist_entries,
                                     p_demux->p_sys->i_tracklist_entries,
                                     p_new_input );
                    }
                    return true;
                }
                /* there MUST have been a start tag for that element name */
                if( !p_handler || !p_handler->name
                    || strcmp( p_handler->name, psz_name ))
                {
                    msg_Err( p_demux, "there's no open element left for <%s>",
                             psz_name );
                    FREE_ATT();
                    return false;
                }

                /* special case: location */
                if( !strcmp( p_handler->name, "location" ) )
                {
                    char *psz_uri=NULL;
                    /* there MUST NOT be an item */
                    if( p_new_input )
                    {
                        msg_Err( p_demux, "item <%s> already created",
                                 psz_name );
                        FREE_ATT();
                        return false;
                    }
                    psz_uri = decode_URI_duplicate( psz_value );

                    if( psz_uri )
                    {
                        if( p_demux->p_sys->psz_base &&
                            !strstr( psz_uri, "://" ) )
                        {
                           char* psz_tmp = malloc(
                                   strlen(p_demux->p_sys->psz_base) +
                                   strlen(psz_uri) +1 );
                           if( !psz_tmp )
                               return false;
                           sprintf( psz_tmp, "%s%s",
                                    p_demux->p_sys->psz_base, psz_uri );
                           free( psz_uri );
                           psz_uri = psz_tmp;
                        }
                        p_new_input = input_item_NewExt( p_demux, psz_uri,
                                                        NULL, 0, NULL, -1 );
                        free( psz_uri );
                        input_item_CopyOptions( p_input_item, p_new_input );
                        psz_uri = NULL;
                        FREE_ATT();
                        p_handler = NULL;
                    }
                    else
                    {
                        FREE_ATT();
                        return false;
                    }
                }
                else if( !strcmp( p_handler->name, "identifier" ) )
                {
                    p_demux->p_sys->i_identifier = atoi( psz_value );
                }
                else
                {
                    /* there MUST be an item */
                    if( !p_new_input )
                    {
                        msg_Err( p_demux, "item not yet created at <%s>",
                                 psz_name );
                        FREE_ATT();
                        return false;
                    }
                    if( p_handler->pf_handler.smpl )
                    {
                        p_handler->pf_handler.smpl( p_new_input,
                                                    p_handler->name,
                                                    psz_value );
                        FREE_ATT();
                    }
                }
                FREE_ATT();
                p_handler = NULL;
                break;

            default:
                /* unknown/unexpected xml node */
                msg_Err( p_demux, "unexpected xml node %i", i_node );
                FREE_ATT();
                return false;
        }
        FREE_NAME();
    }
    msg_Err( p_demux, "unexpected end of xml data" );
    FREE_ATT();
    return false;
}

/**
 * \brief handles the supported <track> sub-elements
 */
static bool set_item_info SIMPLE_INTERFACE
{
    /* exit if setting is impossible */
    if( !psz_name || !psz_value || !p_input )
        return false;


    /* re-convert xml special characters inside psz_value */
    resolve_xml_special_chars( psz_value );

    /* handle each info element in a separate "if" clause */
    if( !strcmp( psz_name, "title" ) )
    {
        input_item_SetTitle( p_input, psz_value );
    }
    else if( !strcmp( psz_name, "creator" ) )
    {
        input_item_SetArtist( p_input, psz_value );
    }
    else if( !strcmp( psz_name, "album" ) )
    {
        input_item_SetAlbum( p_input, psz_value );

    }
    else if( !strcmp( psz_name, "trackNum" ) )
    {
        input_item_SetTrackNum( p_input, psz_value );
    }
    else if( !strcmp( psz_name, "duration" ) )
    {
        long i_num = atol( psz_value );
        input_item_SetDuration( p_input, (mtime_t) i_num*1000 );
    }
    else if( !strcmp( psz_name, "annotation" ) )
    {
        input_item_SetDescription( p_input, psz_value );
    }
    else if( !strcmp( psz_name, "image" ) )
    {
        char *psz_uri = decode_URI_duplicate( psz_value );
        input_item_SetArtURL( p_input, psz_uri );
        free( psz_uri );
    }
    return true;
}

/**
 * \brief handles the <option> elements
 */
static bool set_option SIMPLE_INTERFACE
{
    /* exit if setting is impossible */
    if( !psz_name || !psz_value || !p_input )
        return false;

    /* re-convert xml special characters inside psz_value */
    resolve_xml_special_chars( psz_value );

    input_item_AddOpt( p_input, psz_value, 0 );

    return true;
}

/**
 * \brief parse the extension node of a XSPF playlist
 */
static bool parse_extension_node COMPLEX_INTERFACE
{
    char *psz_name = NULL;
    char *psz_value = NULL;
    char *psz_title = NULL;
    char *psz_application = NULL;
    int i_node;
    bool b_release_input_item = false;
    xml_elem_hnd_t *p_handler = NULL;
    input_item_t *p_new_input = NULL;

    xml_elem_hnd_t pl_elements[] =
        { {"node",  COMPLEX_CONTENT, {.cmplx = parse_extension_node} },
          {"item",  COMPLEX_CONTENT, {.cmplx = parse_extitem_node} },
          {"option", SIMPLE_CONTENT, {.smpl = set_option} },
          {NULL,    UNKNOWN_CONTENT, {NULL} }
        };

    /* read all extension node attributes */
    while( xml_ReaderNextAttr( p_xml_reader ) == VLC_SUCCESS )
    {
        psz_name = xml_ReaderName( p_xml_reader );
        psz_value = xml_ReaderValue( p_xml_reader );
        if( !psz_name || !psz_value )
        {
            msg_Err( p_demux, "invalid xml stream @ <node>" );
            FREE_ATT();
            return false;
        }
        /* attribute: title */
        if( !strcmp( psz_name, "title" ) )
        {
            resolve_xml_special_chars( psz_value );
            psz_title = strdup( psz_value );
        }
        /* extension attribute: application */
        else if( !strcmp( psz_name, "application" ) )
        {
            psz_application = strdup( psz_value );
        }
        /* unknown attribute */
        else
            msg_Warn( p_demux, "invalid <%s> attribute:\"%s\"", psz_element, psz_name );

        FREE_ATT();
    }

    /* attribute title is mandatory except for <extension> */
    if( !strcmp( psz_element, "node" ) )
    {
        if( !psz_title )
        {
            msg_Warn( p_demux, "<node> requires \"title\" attribute" );
            return false;
        }
        p_new_input = input_item_NewWithType( VLC_OBJECT( p_demux ), "vlc://nop",
                                psz_title, 0, NULL, -1, ITEM_TYPE_DIRECTORY );
        if( p_new_input )
        {
            input_item_AddSubItem( p_input_item, p_new_input );
            p_input_item = p_new_input;
            b_release_input_item = true;
        }
        free( psz_title );
    }
    else if( !strcmp( psz_element, "extension" ) )
    {
        if( !psz_application )
        {
            msg_Warn( p_demux, "<extension> requires \"application\" attribute" );
            return false;
        }
        else if( strcmp( psz_application, "http://www.videolan.org/vlc/playlist/0" ) )
        {
            msg_Dbg( p_demux, "Skipping \"%s\" extension tag", psz_application );
            free( psz_application );
            return false;
        }
    }
    free( psz_application );

    /* parse the child elements */
    while( xml_ReaderRead( p_xml_reader ) == 1 )
    {
        i_node = xml_ReaderNodeType( p_xml_reader );
        switch( i_node )
        {
            case XML_READER_NONE:
                break;
            case XML_READER_STARTELEM:
                /*  element start tag  */
                psz_name = xml_ReaderName( p_xml_reader );
                if( !psz_name || !*psz_name )
                {
                    msg_Err( p_demux, "invalid xml stream" );
                    FREE_ATT();
                    if(b_release_input_item) vlc_gc_decref( p_new_input );
                    return false;
                }
                /* choose handler */
                for( p_handler = pl_elements;
                     p_handler->name && strcmp( psz_name, p_handler->name );
                     p_handler++ );
                if( !p_handler->name )
                {
                    msg_Err( p_demux, "unexpected element <%s>", psz_name );
                    FREE_ATT();
                    if(b_release_input_item) vlc_gc_decref( p_new_input );
                    return false;
                }
                FREE_NAME();
                /* complex content is parsed in a separate function */
                if( p_handler->type == COMPLEX_CONTENT )
                {
                    if( p_handler->pf_handler.cmplx( p_demux,
                                                     p_input_item,
                                                     p_xml_reader,
                                                     p_handler->name ) )
                    {
                        p_handler = NULL;
                        FREE_ATT();
                    }
                    else
                    {
                        FREE_ATT();
                        if(b_release_input_item) vlc_gc_decref( p_new_input );
                        return false;
                    }
                }
                break;

            case XML_READER_TEXT:
                /* simple element content */
                FREE_ATT();
                psz_value = xml_ReaderValue( p_xml_reader );
                if( !psz_value )
                {
                    msg_Err( p_demux, "invalid xml stream" );
                    FREE_ATT();
                    if(b_release_input_item) vlc_gc_decref( p_new_input );
                    return false;
                }
                break;

            case XML_READER_ENDELEM:
                /* element end tag */
                psz_name = xml_ReaderName( p_xml_reader );
                if( !psz_name )
                {
                    msg_Err( p_demux, "invalid xml stream" );
                    FREE_ATT();
                    if(b_release_input_item) vlc_gc_decref( p_new_input );
                    return false;
                }
                /* leave if the current parent node is terminated */
                if( !strcmp( psz_name, psz_element ) )
                {
                    FREE_ATT();
                    if(b_release_input_item) vlc_gc_decref( p_new_input );
                    return true;
                }
                /* there MUST have been a start tag for that element name */
                if( !p_handler || !p_handler->name
                    || strcmp( p_handler->name, psz_name ))
                {
                    msg_Err( p_demux, "there's no open element left for <%s>",
                             psz_name );
                    FREE_ATT();
                    if(b_release_input_item) vlc_gc_decref( p_new_input );
                    return false;
                }

                if( p_handler->pf_handler.smpl )
                {
                    p_handler->pf_handler.smpl( p_input_item, p_handler->name,
                                                psz_value );
                }
                FREE_ATT();
                p_handler = NULL;
                break;

            default:
                /* unknown/unexpected xml node */
                msg_Err( p_demux, "unexpected xml node %i", i_node );
                FREE_ATT();
                if(b_release_input_item) vlc_gc_decref( p_new_input );
                return false;
        }
        FREE_NAME();
    }
    if(b_release_input_item) vlc_gc_decref( p_new_input );
    return false;
}

/**
 * \brief parse the extension item node of a XSPF playlist
 */
static bool parse_extitem_node COMPLEX_INTERFACE
{
    VLC_UNUSED(psz_element);
    input_item_t *p_new_input = NULL;
    char *psz_name = NULL;
    char *psz_value = NULL;
    int i_href = -1;

    /* read all extension item attributes */
    while( xml_ReaderNextAttr( p_xml_reader ) == VLC_SUCCESS )
    {
        psz_name = xml_ReaderName( p_xml_reader );
        psz_value = xml_ReaderValue( p_xml_reader );
        if( !psz_name || !psz_value )
        {
            msg_Err( p_demux, "invalid xml stream @ <item>" );
            FREE_ATT();
            return false;
        }
        /* attribute: href */
        if( !strcmp( psz_name, "href" ) )
        {
            i_href = atoi( psz_value );
        }
        /* unknown attribute */
        else
            msg_Warn( p_demux, "invalid <item> attribute:\"%s\"", psz_name);

        FREE_ATT();
    }

    /* attribute href is mandatory */
    if( i_href < 0 )
    {
        msg_Warn( p_demux, "<item> requires \"href\" attribute" );
        return false;
    }

    if( i_href >= p_demux->p_sys->i_tracklist_entries )
    {
        msg_Warn( p_demux, "invalid \"href\" attribute" );
        return false;
    }

    p_new_input = p_demux->p_sys->pp_tracklist[ i_href ];
    if( p_new_input )
    {
        input_item_AddSubItem( p_input_item, p_new_input );
        vlc_gc_decref( p_new_input );
        p_demux->p_sys->pp_tracklist[i_href] = NULL;
    }

    /* kludge for #1293 - XTAG sends ENDELEM for self closing tag */
    /* (libxml sends NONE) */
    xml_ReaderRead( p_xml_reader );

    return true;
}

/**
 * \brief skips complex element content that we can't manage
 */
static bool skip_element COMPLEX_INTERFACE
{
    VLC_UNUSED(p_demux); VLC_UNUSED(p_input_item);
    char *psz_endname;

    while( xml_ReaderRead( p_xml_reader ) == 1 )
    {
        if( xml_ReaderNodeType( p_xml_reader ) == XML_READER_ENDELEM )
        {
            psz_endname = xml_ReaderName( p_xml_reader );
            if( !psz_endname )
                return false;
            if( !strcmp( psz_element, psz_endname ) )
            {
                free( psz_endname );
                return true;
            }
            else
                free( psz_endname );
        }
    }
    return false;
}
