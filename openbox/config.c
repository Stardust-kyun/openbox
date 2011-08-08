/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   config.c for the Openbox window manager
   Copyright (c) 2006        Mikael Magnusson
   Copyright (c) 2003-2007   Dana Jansens

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   See the COPYING file for a copy of the GNU General Public License.
*/

#include "config.h"
#include "config_value.h"
#include "keyboard.h"
#include "mouse.h"
#include "action.h"
#include "action_list.h"
#include "action_parser.h"
#include "translate.h"
#include "client.h"
#include "screen.h"
#include "openbox.h"
#include "gettext.h"
#include "obt/paths.h"

gboolean config_focus_new;
gboolean config_focus_follow;
guint    config_focus_delay;
gboolean config_focus_raise;
gboolean config_focus_last;
gboolean config_focus_under_mouse;
gboolean config_unfocus_leave;

ObPlacePolicy  config_place_policy;
gboolean       config_place_center;
ObPlaceMonitor config_place_monitor;

guint          config_primary_monitor_index;
ObPlaceMonitor config_primary_monitor;

StrutPartial config_margins;

gchar   *config_theme;
gboolean config_theme_keepborder;
guint    config_theme_window_list_icon_size;

gchar   *config_title_layout;

gboolean config_animate_iconify;

RrFont *config_font_activewindow;
RrFont *config_font_inactivewindow;
RrFont *config_font_menuitem;
RrFont *config_font_menutitle;
RrFont *config_font_activeosd;
RrFont *config_font_inactiveosd;

guint   config_desktops_num;
GSList *config_desktops_names;
guint   config_screen_firstdesk;
guint   config_desktop_popup_time;

gboolean         config_resize_redraw;
gint             config_resize_popup_show;
ObResizePopupPos config_resize_popup_pos;
GravityPoint     config_resize_popup_fixed;

ObStackingLayer config_dock_layer;
gboolean        config_dock_floating;
gboolean        config_dock_nostrut;
ObDirection     config_dock_pos;
gint            config_dock_x;
gint            config_dock_y;
ObOrientation   config_dock_orient;
gboolean        config_dock_hide;
guint           config_dock_hide_delay;
guint           config_dock_show_delay;
guint           config_dock_app_move_button;
guint           config_dock_app_move_modifiers;

guint config_keyboard_reset_keycode;
guint config_keyboard_reset_state;

gint     config_mouse_threshold;
gint     config_mouse_dclicktime;
gint     config_mouse_screenedgetime;
gboolean config_mouse_screenedgewarp;

guint    config_menu_hide_delay;
gboolean config_menu_middle;
guint    config_submenu_show_delay;
guint    config_submenu_hide_delay;
gboolean config_menu_manage_desktops;
gboolean config_menu_show_icons;

GSList *config_menu_files;

gint     config_resist_win;
gint     config_resist_edge;

GSList *config_per_app_settings;

ObAppSettings* config_create_app_settings(void)
{
    ObAppSettings *settings = g_slice_new0(ObAppSettings);
    settings->type = -1;
    settings->decor = -1;
    settings->shade = -1;
    settings->monitor = -1;
    settings->focus = -1;
    settings->desktop = 0;
    settings->layer = -2;
    settings->iconic = -1;
    settings->skip_pager = -1;
    settings->skip_taskbar = -1;
    settings->fullscreen = -1;
    settings->max_horz = -1;
    settings->max_vert = -1;
    return settings;
}

#define copy_if(setting, default) \
  if (src->setting != default) dst->setting = src->setting
void config_app_settings_copy_non_defaults(const ObAppSettings *src,
                                           ObAppSettings *dst)
{
    g_assert(src != NULL);
    g_assert(dst != NULL);

    copy_if(type, (ObClientType)-1);
    copy_if(decor, -1);
    copy_if(shade, -1);
    copy_if(monitor, -1);
    copy_if(focus, -1);
    copy_if(desktop, 0);
    copy_if(layer, -2);
    copy_if(iconic, -1);
    copy_if(skip_pager, -1);
    copy_if(skip_taskbar, -1);
    copy_if(fullscreen, -1);
    copy_if(max_horz, -1);
    copy_if(max_vert, -1);

    if (src->pos_given) {
        dst->pos_given = TRUE;
        dst->pos_force = src->pos_force;
        dst->position = src->position;
        /* monitor is copied above */
    }
}

/*
  <applications>
    <application name="aterm">
      <decor>false</decor>
    </application>
    <application name="Rhythmbox">
      <layer>above</layer>
      <position>
        <x>700</x>
        <y>0</y>
        <monitor>1</monitor>
      </position>
      .. there is a lot more settings available
    </application>
  </applications>
*/

/* Manages settings for individual applications.
   Some notes: monitor is the screen number in a multi monitor
   (Xinerama) setup (starting from 0) or mouse, meaning the
   monitor the pointer is on. Default: mouse.
   Layer can be three values, above (Always on top), below
   (Always on bottom) and everything else (normal behaviour).
   Positions can be an integer value or center, which will
   center the window in the specified axis. Position is within
   the monitor, so <position><x>center</x></position><monitor>2</monitor>
   will center the window on the second monitor.
*/
static void parse_per_app_settings(xmlNodePtr node, gpointer d)
{
    xmlNodePtr app = obt_xml_find_sibling(node->children, "application");
    gchar *name = NULL, *class = NULL, *role = NULL, *title = NULL,
        *type_str = NULL;
    gboolean name_set, class_set, type_set, role_set, title_set;
    ObClientType type;
    gboolean x_pos_given;

    while (app) {
        x_pos_given = FALSE;

        class_set = obt_xml_attr_string(app, "class", &class);
        name_set = obt_xml_attr_string(app, "name", &name);
        type_set = obt_xml_attr_string(app, "type", &type_str);
        role_set = obt_xml_attr_string(app, "role", &role);
        title_set = obt_xml_attr_string(app, "title", &title);

        /* validate the type tho */
        if (type_set) {
            if (!g_ascii_strcasecmp(type_str, "normal"))
                type = OB_CLIENT_TYPE_NORMAL;
            else if (!g_ascii_strcasecmp(type_str, "dialog"))
                type = OB_CLIENT_TYPE_DIALOG;
            else if (!g_ascii_strcasecmp(type_str, "splash"))
                type = OB_CLIENT_TYPE_SPLASH;
            else if (!g_ascii_strcasecmp(type_str, "utility"))
                type = OB_CLIENT_TYPE_UTILITY;
            else if (!g_ascii_strcasecmp(type_str, "menu"))
                type = OB_CLIENT_TYPE_MENU;
            else if (!g_ascii_strcasecmp(type_str, "toolbar"))
                type = OB_CLIENT_TYPE_TOOLBAR;
            else if (!g_ascii_strcasecmp(type_str, "dock"))
                type = OB_CLIENT_TYPE_DOCK;
            else if (!g_ascii_strcasecmp(type_str, "desktop"))
                type = OB_CLIENT_TYPE_DESKTOP;
            else
                type_set = FALSE; /* not valid! */
        }

        if (class_set || name_set || role_set || title_set || type_set) {
            xmlNodePtr n, c;
            ObAppSettings *settings = config_create_app_settings();

            if (name_set)
                settings->name = g_pattern_spec_new(name);

            if (class_set)
                settings->class = g_pattern_spec_new(class);

            if (role_set)
                settings->role = g_pattern_spec_new(role);

            if (title_set)
                settings->title = g_pattern_spec_new(title);

            if (type_set)
                settings->type = type;

            if ((n = obt_xml_find_sibling(app->children, "decor")))
                if (!obt_xml_node_contains(n, "default"))
                    settings->decor = obt_xml_node_bool(n);

            if ((n = obt_xml_find_sibling(app->children, "shade")))
                if (!obt_xml_node_contains(n, "default"))
                    settings->shade = obt_xml_node_bool(n);

            if ((n = obt_xml_find_sibling(app->children, "position"))) {
                if ((c = obt_xml_find_sibling(n->children, "x")))
                    if (!obt_xml_node_contains(c, "default")) {
                        ObConfigValue *v = config_value_new_string(
                            obt_xml_node_string(node));
                        config_value_gravity_coord(v, &settings->position.x);
                        config_value_unref(v);
                        x_pos_given = TRUE;
                    }

                if (x_pos_given && (c = obt_xml_find_sibling(n->children, "y")))
                    if (!obt_xml_node_contains(c, "default")) {
                        ObConfigValue *v = config_value_new_string(
                            obt_xml_node_string(node));
                        config_value_gravity_coord(v, &settings->position.y);
                        config_value_unref(v);
                        settings->pos_given = TRUE;
                    }

                if (settings->pos_given &&
                    (c = obt_xml_find_sibling(n->children, "monitor")))
                    if (!obt_xml_node_contains(c, "default")) {
                        gchar *s = obt_xml_node_string(c);
                        if (!g_ascii_strcasecmp(s, "mouse"))
                            settings->monitor = 0;
                        else
                            settings->monitor = obt_xml_node_int(c);
                        g_free(s);
                    }

                obt_xml_attr_bool(n, "force", &settings->pos_force);
            }

            if ((n = obt_xml_find_sibling(app->children, "focus")))
                if (!obt_xml_node_contains(n, "default"))
                    settings->focus = obt_xml_node_bool(n);

            if ((n = obt_xml_find_sibling(app->children, "desktop"))) {
                if (!obt_xml_node_contains(n, "default")) {
                    gchar *s = obt_xml_node_string(n);
                    if (!g_ascii_strcasecmp(s, "all"))
                        settings->desktop = DESKTOP_ALL;
                    else {
                        gint i = obt_xml_node_int(n);
                        if (i > 0)
                            settings->desktop = i;
                    }
                    g_free(s);
                }
            }

            if ((n = obt_xml_find_sibling(app->children, "layer")))
                if (!obt_xml_node_contains(n, "default")) {
                    gchar *s = obt_xml_node_string(n);
                    if (!g_ascii_strcasecmp(s, "above"))
                        settings->layer = 1;
                    else if (!g_ascii_strcasecmp(s, "below"))
                        settings->layer = -1;
                    else
                        settings->layer = 0;
                    g_free(s);
                }

            if ((n = obt_xml_find_sibling(app->children, "iconic")))
                if (!obt_xml_node_contains(n, "default"))
                    settings->iconic = obt_xml_node_bool(n);

            if ((n = obt_xml_find_sibling(app->children, "skip_pager")))
                if (!obt_xml_node_contains(n, "default"))
                    settings->skip_pager = obt_xml_node_bool(n);

            if ((n = obt_xml_find_sibling(app->children, "skip_taskbar")))
                if (!obt_xml_node_contains(n, "default"))
                    settings->skip_taskbar = obt_xml_node_bool(n);

            if ((n = obt_xml_find_sibling(app->children, "fullscreen")))
                if (!obt_xml_node_contains(n, "default"))
                    settings->fullscreen = obt_xml_node_bool(n);

            if ((n = obt_xml_find_sibling(app->children, "maximized")))
                if (!obt_xml_node_contains(n, "default")) {
                    gchar *s = obt_xml_node_string(n);
                    if (!g_ascii_strcasecmp(s, "horizontal")) {
                        settings->max_horz = TRUE;
                        settings->max_vert = FALSE;
                    } else if (!g_ascii_strcasecmp(s, "vertical")) {
                        settings->max_horz = FALSE;
                        settings->max_vert = TRUE;
                    } else
                        settings->max_horz = settings->max_vert =
                            obt_xml_node_bool(n);
                    g_free(s);
                }

            config_per_app_settings = g_slist_append(config_per_app_settings,
                                                     (gpointer) settings);
            g_free(name);
            g_free(class);
            g_free(role);
            g_free(title);
            g_free(type_str);
            name = class = role = title = type_str = NULL;
        }

        app = obt_xml_find_sibling(app->next, "application");
    }
}

/*

<keybind key="C-x">
  <action name="ChangeDesktop">
    <desktop>3</desktop>
  </action>
</keybind>

*/

static void parse_keybind(xmlNodePtr node, GList *keylist)
{
    gchar *keystring, **keys, **key;
    xmlNodePtr n;
    gboolean is_chroot = FALSE;

    if (!obt_xml_attr_string(node, "key", &keystring))
        return;

    obt_xml_attr_bool(node, "chroot", &is_chroot);

    keys = g_strsplit(keystring, " ", 0);
    for (key = keys; *key; ++key) {
        keylist = g_list_append(keylist, *key);

        if ((n = obt_xml_find_sibling(node->children, "keybind"))) {
            while (n) {
                parse_keybind(n, keylist);
                n = obt_xml_find_sibling(n->next, "keybind");
            }
        }
        else if ((n = obt_xml_find_sibling(node->children, "action"))) {
            while (n) {
                ObActionParser *p;
                ObActionList *actions;
                xmlChar *c;

                c = xmlNodeGetContent(node);
                p = action_parser_new();
                actions = action_parser_read_string(p, (gchar*)c);
                xmlFree(c);
                action_parser_unref(p);

                if (actions)
                    keyboard_bind(keylist, actions);

                action_list_unref(actions);
                n = obt_xml_find_sibling(n->next, "action");
            }
        }


        if (is_chroot)
            keyboard_chroot(keylist);
        keylist = g_list_delete_link(keylist, g_list_last(keylist));
    }

    g_strfreev(keys);
    g_free(keystring);
}

static void parse_keyboard(xmlNodePtr node, gpointer d)
{
    xmlNodePtr n;
    gchar *key;

    if ((n = obt_xml_find_sibling(node->children, "chainQuitKey"))) {
        key = obt_xml_node_string(n);
        translate_key(key, &config_keyboard_reset_state,
                      &config_keyboard_reset_keycode);
        g_free(key);
    }
}

/*

<context name="Titlebar">
  <mousebind button="Left" action="Press">
    <action name="Raise"></action>
  </mousebind>
</context>

*/

static void parse_mouse(xmlNodePtr node, gpointer d)
{
    xmlNodePtr n;

    mouse_unbind_all();

    node = node->children;

    if ((n = obt_xml_find_sibling(node, "dragThreshold")))
        config_mouse_threshold = obt_xml_node_int(n);
    if ((n = obt_xml_find_sibling(node, "doubleClickTime")))
        config_mouse_dclicktime = obt_xml_node_int(n);
    if ((n = obt_xml_find_sibling(node, "screenEdgeWarpTime"))) {
        config_mouse_screenedgetime = obt_xml_node_int(n);
        /* minimum value of 25 for this property, when it is 1 and you hit the
           edge it basically never stops */
        if (config_mouse_screenedgetime && config_mouse_screenedgetime < 25)
            config_mouse_screenedgetime = 25;
    }
    if ((n = obt_xml_find_sibling(node, "screenEdgeWarpMouse")))
        config_mouse_screenedgewarp = obt_xml_node_bool(n);
}

static void parse_context(xmlNodePtr n, gpointer d)
{
    xmlNodePtr nbut, nact;
    gchar *buttonstr;
    gchar *cxstr;
    ObMouseAction mact;
    gchar *modcxstr;
    ObFrameContext cx;

    if (!obt_xml_attr_string(n, "name", &cxstr))
        return;

    modcxstr = g_strdup(cxstr); /* make a copy to mutilate */
    while (frame_next_context_from_string(modcxstr, &cx)) {
        if (!cx) {
            gchar *s = strchr(modcxstr, ' ');
            if (s) {
                *s = '\0';
                g_message(_("Invalid context \"%s\" in mouse binding"),
                          modcxstr);
                *s = ' ';
            }
            continue;
        }

        nbut = obt_xml_find_sibling(n->children, "mousebind");
        while (nbut) {
            if (!obt_xml_attr_string(nbut, "button", &buttonstr))
                goto next_nbut;
            if (obt_xml_attr_contains(nbut, "action", "press"))
                mact = OB_MOUSE_ACTION_PRESS;
            else if (obt_xml_attr_contains(nbut, "action", "release"))
                mact = OB_MOUSE_ACTION_RELEASE;
            else if (obt_xml_attr_contains(nbut, "action", "click"))
                mact = OB_MOUSE_ACTION_CLICK;
            else if (obt_xml_attr_contains(nbut, "action","doubleclick"))
                mact = OB_MOUSE_ACTION_DOUBLE_CLICK;
            else if (obt_xml_attr_contains(nbut, "action", "drag"))
                mact = OB_MOUSE_ACTION_MOTION;
            else
                goto next_nbut;

            nact = obt_xml_find_sibling(nbut->children, "action");
            while (nact) {
                ObActionList *actions;
                ObActionParser *p;
                xmlChar *c;

                c = xmlNodeGetContent(nact);
                p = action_parser_new();
                if ((actions = action_parser_read_string(p, (gchar*)c)))
                    mouse_bind(buttonstr, cx, mact, actions);
                nact = obt_xml_find_sibling(nact->next, "action");
                action_list_unref(actions);
                xmlFree(c);
                action_parser_unref(p);
            }
            g_free(buttonstr);
        next_nbut:
            nbut = obt_xml_find_sibling(nbut->next, "mousebind");
        }
    }
    g_free(modcxstr);
    g_free(cxstr);
}

static void parse_theme(xmlNodePtr node, gpointer d)
{
    xmlNodePtr n;

    node = node->children;

    if ((n = obt_xml_find_sibling(node, "name"))) {
        gchar *c;

        g_free(config_theme);
        c = obt_xml_node_string(n);
        config_theme = obt_paths_expand_tilde(c);
        g_free(c);
    }
    if ((n = obt_xml_find_sibling(node, "titleLayout"))) {
        gchar *c, *d;

        g_free(config_title_layout);
        config_title_layout = obt_xml_node_string(n);

        /* replace duplicates with spaces */
        for (c = config_title_layout; *c != '\0'; ++c)
            for (d = c+1; *d != '\0'; ++d)
                if (*c == *d) *d = ' ';
    }
    if ((n = obt_xml_find_sibling(node, "keepBorder")))
        config_theme_keepborder = obt_xml_node_bool(n);
    if ((n = obt_xml_find_sibling(node, "animateIconify")))
        config_animate_iconify = obt_xml_node_bool(n);
    if ((n = obt_xml_find_sibling(node, "windowListIconSize"))) {
        config_theme_window_list_icon_size = obt_xml_node_int(n);
        if (config_theme_window_list_icon_size < 16)
            config_theme_window_list_icon_size = 16;
        else if (config_theme_window_list_icon_size > 96)
            config_theme_window_list_icon_size = 96;
    }

    n = obt_xml_find_sibling(node, "font");
    while (n) {
        xmlNodePtr   fnode;
        RrFont     **font;
        gchar       *name = g_strdup(RrDefaultFontFamily);
        gint         size = RrDefaultFontSize;
        RrFontWeight weight = RrDefaultFontWeight;
        RrFontSlant  slant = RrDefaultFontSlant;

        if (obt_xml_attr_contains(n, "place", "ActiveWindow"))
            font = &config_font_activewindow;
        else if (obt_xml_attr_contains(n, "place", "InactiveWindow"))
            font = &config_font_inactivewindow;
        else if (obt_xml_attr_contains(n, "place", "MenuHeader"))
            font = &config_font_menutitle;
        else if (obt_xml_attr_contains(n, "place", "MenuItem"))
            font = &config_font_menuitem;
        else if (obt_xml_attr_contains(n, "place", "ActiveOnScreenDisplay"))
            font = &config_font_activeosd;
        else if (obt_xml_attr_contains(n, "place", "OnScreenDisplay"))
            font = &config_font_activeosd;
        else if (obt_xml_attr_contains(n, "place","InactiveOnScreenDisplay"))
            font = &config_font_inactiveosd;
        else
            goto next_font;

        if ((fnode = obt_xml_find_sibling(n->children, "name"))) {
            g_free(name);
            name = obt_xml_node_string(fnode);
        }
        if ((fnode = obt_xml_find_sibling(n->children, "size"))) {
            int s = obt_xml_node_int(fnode);
            if (s > 0) size = s;
        }
        if ((fnode = obt_xml_find_sibling(n->children, "weight"))) {
            gchar *w = obt_xml_node_string(fnode);
            if (!g_ascii_strcasecmp(w, "Bold"))
                weight = RR_FONTWEIGHT_BOLD;
            g_free(w);
        }
        if ((fnode = obt_xml_find_sibling(n->children, "slant"))) {
            gchar *s = obt_xml_node_string(fnode);
            if (!g_ascii_strcasecmp(s, "Italic"))
                slant = RR_FONTSLANT_ITALIC;
            if (!g_ascii_strcasecmp(s, "Oblique"))
                slant = RR_FONTSLANT_OBLIQUE;
            g_free(s);
        }

        *font = RrFontOpen(ob_rr_inst, name, size, weight, slant);
        g_free(name);
    next_font:
        n = obt_xml_find_sibling(n->next, "font");
    }
}

static void parse_desktops(xmlNodePtr node, gpointer d)
{
    xmlNodePtr n;

    node = node->children;

    if ((n = obt_xml_find_sibling(node, "number"))) {
        gint d = obt_xml_node_int(n);
        if (d > 0)
            config_desktops_num = (unsigned) d;
    }
    if ((n = obt_xml_find_sibling(node, "firstdesk"))) {
        gint d = obt_xml_node_int(n);
        if (d > 0)
            config_screen_firstdesk = (unsigned) d;
    }
    if ((n = obt_xml_find_sibling(node, "names"))) {
        GSList *it;
        xmlNodePtr nname;

        for (it = config_desktops_names; it; it = it->next)
            g_free(it->data);
        g_slist_free(config_desktops_names);
        config_desktops_names = NULL;

        nname = obt_xml_find_sibling(n->children, "name");
        while (nname) {
            config_desktops_names =
                g_slist_append(config_desktops_names,
                               obt_xml_node_string(nname));
            nname = obt_xml_find_sibling(nname->next, "name");
        }
    }
    if ((n = obt_xml_find_sibling(node, "popupTime")))
        config_desktop_popup_time = obt_xml_node_int(n);
}

static void parse_resize(xmlNodePtr node, gpointer d)
{
    xmlNodePtr n;

    node = node->children;

    if ((n = obt_xml_find_sibling(node, "drawContents")))
        config_resize_redraw = obt_xml_node_bool(n);
    if ((n = obt_xml_find_sibling(node, "popupShow"))) {
        config_resize_popup_show = obt_xml_node_int(n);
        if (obt_xml_node_contains(n, "Always"))
            config_resize_popup_show = 2;
        else if (obt_xml_node_contains(n, "Never"))
            config_resize_popup_show = 0;
        else if (obt_xml_node_contains(n, "Nonpixel"))
            config_resize_popup_show = 1;
    }
    if ((n = obt_xml_find_sibling(node, "popupPosition"))) {
        if (obt_xml_node_contains(n, "Top"))
            config_resize_popup_pos = OB_RESIZE_POS_TOP;
        else if (obt_xml_node_contains(n, "Center"))
            config_resize_popup_pos = OB_RESIZE_POS_CENTER;
        else if (obt_xml_node_contains(n, "Fixed")) {
            config_resize_popup_pos = OB_RESIZE_POS_FIXED;

            if ((n = obt_xml_find_sibling(node, "popupFixedPosition"))) {
                xmlNodePtr n2;

                if ((n2 = obt_xml_find_sibling(n->children, "x"))) {
                    ObConfigValue *v = config_value_new_string(
                        obt_xml_node_string(n2));
                    config_value_gravity_coord(v,
                                               &config_resize_popup_fixed.x);
                    config_value_unref(v);
                }
                if ((n2 = obt_xml_find_sibling(n->children, "y"))) {
                    ObConfigValue *v = config_value_new_string(
                        obt_xml_node_string(n2));
                    config_value_gravity_coord(v,
                                               &config_resize_popup_fixed.y);
                    config_value_unref(v);
                }

                config_resize_popup_fixed.x.pos =
                    MAX(config_resize_popup_fixed.x.pos, 0);
                config_resize_popup_fixed.y.pos =
                    MAX(config_resize_popup_fixed.y.pos, 0);
            }
        }
    }
}

static void parse_dock(xmlNodePtr node, gpointer d)
{
    xmlNodePtr n;

    node = node->children;

    if ((n = obt_xml_find_sibling(node, "position"))) {
        if (obt_xml_node_contains(n, "TopLeft"))
            config_dock_floating = FALSE,
            config_dock_pos = OB_DIRECTION_NORTHWEST;
        else if (obt_xml_node_contains(n, "Top"))
            config_dock_floating = FALSE,
            config_dock_pos = OB_DIRECTION_NORTH;
        else if (obt_xml_node_contains(n, "TopRight"))
            config_dock_floating = FALSE,
            config_dock_pos = OB_DIRECTION_NORTHEAST;
        else if (obt_xml_node_contains(n, "Right"))
            config_dock_floating = FALSE,
            config_dock_pos = OB_DIRECTION_EAST;
        else if (obt_xml_node_contains(n, "BottomRight"))
            config_dock_floating = FALSE,
            config_dock_pos = OB_DIRECTION_SOUTHEAST;
        else if (obt_xml_node_contains(n, "Bottom"))
            config_dock_floating = FALSE,
            config_dock_pos = OB_DIRECTION_SOUTH;
        else if (obt_xml_node_contains(n, "BottomLeft"))
            config_dock_floating = FALSE,
            config_dock_pos = OB_DIRECTION_SOUTHWEST;
        else if (obt_xml_node_contains(n, "Left"))
            config_dock_floating = FALSE,
            config_dock_pos = OB_DIRECTION_WEST;
        else if (obt_xml_node_contains(n, "Floating"))
            config_dock_floating = TRUE;
    }
    if (config_dock_floating) {
        if ((n = obt_xml_find_sibling(node, "floatingX")))
            config_dock_x = obt_xml_node_int(n);
        if ((n = obt_xml_find_sibling(node, "floatingY")))
            config_dock_y = obt_xml_node_int(n);
    } else {
        if ((n = obt_xml_find_sibling(node, "noStrut")))
            config_dock_nostrut = obt_xml_node_bool(n);
    }
    if ((n = obt_xml_find_sibling(node, "stacking"))) {
        if (obt_xml_node_contains(n, "normal"))
            config_dock_layer = OB_STACKING_LAYER_NORMAL;
        else if (obt_xml_node_contains(n, "below"))
            config_dock_layer = OB_STACKING_LAYER_BELOW;
        else if (obt_xml_node_contains(n, "above"))
            config_dock_layer = OB_STACKING_LAYER_ABOVE;
    }
    if ((n = obt_xml_find_sibling(node, "direction"))) {
        if (obt_xml_node_contains(n, "horizontal"))
            config_dock_orient = OB_ORIENTATION_HORZ;
        else if (obt_xml_node_contains(n, "vertical"))
            config_dock_orient = OB_ORIENTATION_VERT;
    }
    if ((n = obt_xml_find_sibling(node, "autoHide")))
        config_dock_hide = obt_xml_node_bool(n);
    if ((n = obt_xml_find_sibling(node, "hideDelay")))
        config_dock_hide_delay = obt_xml_node_int(n);
    if ((n = obt_xml_find_sibling(node, "showDelay")))
        config_dock_show_delay = obt_xml_node_int(n);
    if ((n = obt_xml_find_sibling(node, "moveButton"))) {
        gchar *str = obt_xml_node_string(n);
        guint b, s;
        if (translate_button(str, &s, &b)) {
            config_dock_app_move_button = b;
            config_dock_app_move_modifiers = s;
        } else {
            g_message(_("Invalid button \"%s\" specified in config file"), str);
        }
        g_free(str);
    }
}

static void parse_menu(xmlNodePtr node, gpointer d)
{
    xmlNodePtr n;
    node = node->children;

    if ((n = obt_xml_find_sibling(node, "hideDelay")))
        config_menu_hide_delay = obt_xml_node_int(n);
    if ((n = obt_xml_find_sibling(node, "middle")))
        config_menu_middle = obt_xml_node_bool(n);
    if ((n = obt_xml_find_sibling(node, "submenuShowDelay")))
        config_submenu_show_delay = obt_xml_node_int(n);
    if ((n = obt_xml_find_sibling(node, "submenuHideDelay")))
        config_submenu_hide_delay = obt_xml_node_int(n);
    if ((n = obt_xml_find_sibling(node, "manageDesktops")))
        config_menu_manage_desktops = obt_xml_node_bool(n);
    if ((n = obt_xml_find_sibling(node, "showIcons"))) {
        config_menu_show_icons = obt_xml_node_bool(n);
#ifndef USE_IMLIB2
        if (config_menu_show_icons)
            g_message(_("Openbox was compiled without Imlib2 image loading support. Icons in menus will not be loaded."));
#endif
    }

    while ((node = obt_xml_find_sibling(node, "file"))) {
            gchar *c = obt_xml_node_string(node);
            config_menu_files = g_slist_append(config_menu_files,
                                               obt_paths_expand_tilde(c));
            g_free(c);
            node = node->next;
    }
}

static void parse_resistance(xmlNodePtr node, gpointer d)
{
    xmlNodePtr n;

    node = node->children;
    if ((n = obt_xml_find_sibling(node, "strength")))
        config_resist_win = obt_xml_node_int(n);
    if ((n = obt_xml_find_sibling(node, "screen_edge_strength")))
        config_resist_edge = obt_xml_node_int(n);
}

typedef struct
{
    const gchar *key;
    const gchar *actiontext;
} ObDefKeyBind;

static void bind_default_keyboard(void)
{
    ObDefKeyBind *it;
    ObDefKeyBind binds[] = {
        { "A-Tab", "NextWindow" },
        { "S-A-Tab", "PreviousWindow" },
        { "A-F4", "Close" },
        { NULL, NULL }
    };
    ObActionParser *p;

    p = action_parser_new();
    for (it = binds; it->key; ++it) {
        GList *l = g_list_append(NULL, g_strdup(it->key));
        ObActionList *actions = action_parser_read_string(p, it->actiontext);
        keyboard_bind(l, actions);
        action_list_unref(actions);
    }
    action_parser_unref(p);
}

typedef struct
{
    const gchar *button;
    const gchar *context;
    const ObMouseAction mact;
    const gchar *actname;
} ObDefMouseBind;

static void bind_default_mouse(void)
{
    ObDefMouseBind *it;
    ObDefMouseBind binds[] = {
        { "Left", "Client", OB_MOUSE_ACTION_PRESS, "Focus" },
        { "Middle", "Client", OB_MOUSE_ACTION_PRESS, "Focus" },
        { "Right", "Client", OB_MOUSE_ACTION_PRESS, "Focus" },
        { "Left", "Desktop", OB_MOUSE_ACTION_PRESS, "Focus" },
        { "Middle", "Desktop", OB_MOUSE_ACTION_PRESS, "Focus" },
        { "Right", "Desktop", OB_MOUSE_ACTION_PRESS, "Focus" },
        { "Left", "Titlebar", OB_MOUSE_ACTION_PRESS, "Focus" },
        { "Left", "Bottom", OB_MOUSE_ACTION_PRESS, "Focus" },
        { "Left", "BLCorner", OB_MOUSE_ACTION_PRESS, "Focus" },
        { "Left", "BRCorner", OB_MOUSE_ACTION_PRESS, "Focus" },
        { "Left", "TLCorner", OB_MOUSE_ACTION_PRESS, "Focus" },
        { "Left", "TRCorner", OB_MOUSE_ACTION_PRESS, "Focus" },
        { "Left", "Close", OB_MOUSE_ACTION_PRESS, "Focus" },
        { "Left", "Maximize", OB_MOUSE_ACTION_PRESS, "Focus" },
        { "Left", "Iconify", OB_MOUSE_ACTION_PRESS, "Focus" },
        { "Left", "Icon", OB_MOUSE_ACTION_PRESS, "Focus" },
        { "Left", "AllDesktops", OB_MOUSE_ACTION_PRESS, "Focus" },
        { "Left", "Shade", OB_MOUSE_ACTION_PRESS, "Focus" },
        { "Left", "Client", OB_MOUSE_ACTION_CLICK, "Raise" },
        { "Left", "Titlebar", OB_MOUSE_ACTION_CLICK, "Raise" },
        { "Middle", "Titlebar", OB_MOUSE_ACTION_CLICK, "Lower" },
        { "Left", "BLCorner", OB_MOUSE_ACTION_CLICK, "Raise" },
        { "Left", "BRCorner", OB_MOUSE_ACTION_CLICK, "Raise" },
        { "Left", "TLCorner", OB_MOUSE_ACTION_CLICK, "Raise" },
        { "Left", "TRCorner", OB_MOUSE_ACTION_CLICK, "Raise" },
        { "Left", "Close", OB_MOUSE_ACTION_CLICK, "Raise" },
        { "Left", "Maximize", OB_MOUSE_ACTION_CLICK, "Raise" },
        { "Left", "Iconify", OB_MOUSE_ACTION_CLICK, "Raise" },
        { "Left", "Icon", OB_MOUSE_ACTION_CLICK, "Raise" },
        { "Left", "AllDesktops", OB_MOUSE_ACTION_CLICK, "Raise" },
        { "Left", "Shade", OB_MOUSE_ACTION_CLICK, "Raise" },
        { "Left", "Close", OB_MOUSE_ACTION_CLICK, "Close" },
        { "Left", "Maximize", OB_MOUSE_ACTION_CLICK, "Maximize" },
        { "Left", "Iconify", OB_MOUSE_ACTION_CLICK, "Iconify" },
        { "Left", "AllDesktops", OB_MOUSE_ACTION_CLICK, "Omnipresent" },
        { "Left", "Shade", OB_MOUSE_ACTION_CLICK, "Shade" },
        { "Left", "TLCorner", OB_MOUSE_ACTION_MOTION, "Resize" },
        { "Left", "TRCorner", OB_MOUSE_ACTION_MOTION, "Resize" },
        { "Left", "BLCorner", OB_MOUSE_ACTION_MOTION, "Resize" },
        { "Left", "BRCorner", OB_MOUSE_ACTION_MOTION, "Resize" },
        { "Left", "Top", OB_MOUSE_ACTION_MOTION, "Resize" },
        { "Left", "Bottom", OB_MOUSE_ACTION_MOTION, "Resize" },
        { "Left", "Left", OB_MOUSE_ACTION_MOTION, "Resize" },
        { "Left", "Right", OB_MOUSE_ACTION_MOTION, "Resize" },
        { "Left", "Titlebar", OB_MOUSE_ACTION_MOTION, "Move" },
        { "A-Left", "Frame", OB_MOUSE_ACTION_MOTION, "Move" },
        { "A-Middle", "Frame", OB_MOUSE_ACTION_MOTION, "Resize" },
        { NULL, NULL, 0, NULL }
    };
    ObActionParser *p;
    ObActionList *actions;

    p = action_parser_new();
    for (it = binds; it->button; ++it) {
        actions = action_parser_read_string(p, it->actname);
        mouse_bind(it->button, frame_context_from_string(it->context),
                   it->mact, actions);
        action_list_unref(actions);
    }
    action_parser_unref(p);
}

static ObtXmlInst *config_inst = NULL;

void config_startup()
{
}

void config_shutdown(void)
{
    GSList *it;

    g_free(config_theme);

    g_free(config_title_layout);

    RrFontClose(config_font_activewindow);
    RrFontClose(config_font_inactivewindow);
    RrFontClose(config_font_menuitem);
    RrFontClose(config_font_menutitle);
    RrFontClose(config_font_activeosd);
    RrFontClose(config_font_inactiveosd);

    for (it = config_desktops_names; it; it = g_slist_next(it))
        g_free(it->data);
    g_slist_free(config_desktops_names);

    for (it = config_menu_files; it; it = g_slist_next(it))
        g_free(it->data);
    g_slist_free(config_menu_files);

    for (it = config_per_app_settings; it; it = g_slist_next(it)) {
        ObAppSettings *itd = (ObAppSettings *)it->data;
        if (itd->name)  g_pattern_spec_free(itd->name);
        if (itd->role)  g_pattern_spec_free(itd->role);
        if (itd->title) g_pattern_spec_free(itd->title);
        if (itd->class) g_pattern_spec_free(itd->class);
        g_slice_free(ObAppSettings, it->data);
    }
    g_slist_free(config_per_app_settings);
}

void config_load_config(void)
{
    xmlNodePtr n, root, e;
    gboolean ok;

    config_inst = obt_xml_instance_new();
    ok = obt_xml_load_cache_file(config_inst, "openbox", "config", "config");
    root = obt_xml_root(config_inst);

    n = obt_xml_path_get_node(root, "focus", "");
    config_focus_new = obt_xml_path_bool(n, "focusNew", "yes");
    config_focus_follow = obt_xml_path_bool(n, "followMouse", "no");
    config_focus_delay = obt_xml_path_int(n, "focusDelay", "0");
    config_focus_raise = obt_xml_path_bool(n, "raiseOnFocus", "no");
    config_focus_last = obt_xml_path_bool(n, "focusLast", "yes");
    config_focus_under_mouse = obt_xml_path_bool(n, "underMouse", "no");
    config_unfocus_leave = obt_xml_path_bool(n, "unfocusOnLeave", "no");

    n = obt_xml_path_get_node(root, "placement", "");
    e = obt_xml_path_get_node(n, "policy", "smart");
    if (obt_xml_node_contains(e, "smart"))
        config_place_policy = OB_PLACE_POLICY_SMART;
    else if (obt_xml_node_contains(e, "undermouse"))
        config_place_policy = OB_PLACE_POLICY_MOUSE;
    else
        config_place_policy = OB_PLACE_POLICY_SMART;
    config_place_center = obt_xml_path_bool(n, "center", "true");
    e = obt_xml_path_get_node(n, "monitor", "primary");
    if (obt_xml_node_contains(e, "primary"))
        config_place_monitor = OB_PLACE_MONITOR_PRIMARY;
    else if (obt_xml_node_contains(e, "active"))
        config_place_monitor = OB_PLACE_MONITOR_ACTIVE;
    else if (obt_xml_node_contains(e, "mouse"))
        config_place_monitor = OB_PLACE_MONITOR_MOUSE;
    else if (obt_xml_node_contains(e, "any"))
        config_place_monitor = OB_PLACE_MONITOR_ANY;
    else
        config_place_monitor = OB_PLACE_MONITOR_PRIMARY;
    e = obt_xml_path_get_node(n, "primaryMonitor", "1");
    if (obt_xml_node_contains(e, "active")) {
        config_primary_monitor = OB_PLACE_MONITOR_ACTIVE;
        config_primary_monitor_index = 1;
    }
    else if (obt_xml_node_contains(e, "mouse")) {
        config_primary_monitor = OB_PLACE_MONITOR_MOUSE;
        config_primary_monitor_index = 1;
    }
    else {
        config_primary_monitor = OB_PLACE_MONITOR_ACTIVE;
        config_primary_monitor_index = obt_xml_node_int(e);
    }

    n = obt_xml_path_get_node(root, "margins", "");
    STRUT_PARTIAL_SET(config_margins, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    config_margins.top = MAX(0, obt_xml_path_int(n, "top", "0"));
    config_margins.left = MAX(0, obt_xml_path_int(n, "left", "0"));
    config_margins.right = MAX(0, obt_xml_path_int(n, "right", "0"));
    config_margins.bottom = MAX(0, obt_xml_path_int(n, "bottom", "0"));

    config_theme = NULL;

    config_animate_iconify = TRUE;
    config_title_layout = g_strdup("NLIMC");
    config_theme_keepborder = TRUE;
    config_theme_window_list_icon_size = 36;

    config_font_activewindow = NULL;
    config_font_inactivewindow = NULL;
    config_font_menuitem = NULL;
    config_font_menutitle = NULL;
    config_font_activeosd = NULL;
    config_font_inactiveosd = NULL;

    obt_xml_register(config_inst, "theme", parse_theme, NULL);

    config_desktops_num = 4;
    config_screen_firstdesk = 1;
    config_desktops_names = NULL;
    config_desktop_popup_time = 875;

    obt_xml_register(config_inst, "desktops", parse_desktops, NULL);

    config_resize_redraw = TRUE;
    config_resize_popup_show = 1; /* nonpixel increments */
    config_resize_popup_pos = OB_RESIZE_POS_CENTER;
    GRAVITY_COORD_SET(config_resize_popup_fixed.x, 0, FALSE, FALSE);
    GRAVITY_COORD_SET(config_resize_popup_fixed.y, 0, FALSE, FALSE);

    obt_xml_register(config_inst, "resize", parse_resize, NULL);

    config_dock_layer = OB_STACKING_LAYER_ABOVE;
    config_dock_pos = OB_DIRECTION_NORTHEAST;
    config_dock_floating = FALSE;
    config_dock_nostrut = FALSE;
    config_dock_x = 0;
    config_dock_y = 0;
    config_dock_orient = OB_ORIENTATION_VERT;
    config_dock_hide = FALSE;
    config_dock_hide_delay = 300;
    config_dock_show_delay = 300;
    config_dock_app_move_button = 2; /* middle */
    config_dock_app_move_modifiers = 0;

    obt_xml_register(config_inst, "dock", parse_dock, NULL);

    translate_key("C-g", &config_keyboard_reset_state,
                  &config_keyboard_reset_keycode);

    obt_xml_register(config_inst, "keyboard", parse_keyboard, NULL);

    config_mouse_threshold = 8;
    config_mouse_dclicktime = 200;
    config_mouse_screenedgetime = 400;
    config_mouse_screenedgewarp = FALSE;

    obt_xml_register(config_inst, "mouse", parse_mouse, NULL);

    config_resist_win = 10;
    config_resist_edge = 20;

    obt_xml_register(config_inst, "resistance", parse_resistance, NULL);

    config_menu_hide_delay = 250;
    config_menu_middle = FALSE;
    config_submenu_show_delay = 100;
    config_submenu_hide_delay = 400;
    config_menu_manage_desktops = TRUE;
    config_menu_files = NULL;
    config_menu_show_icons = TRUE;

    obt_xml_register(config_inst, "menu", parse_menu, NULL);


    if (ok) {
        obt_xml_tree_from_root(config_inst);
        obt_xml_close(config_inst);
    }
}

void config_save_config(void)
{
    xmlNodePtr n, root;
    const gchar *e;

    root = obt_xml_root(config_inst);

    n = obt_xml_path_get_node(root, "focus", "");
    obt_xml_path_set_bool(n, "focusNew", config_focus_new);
    obt_xml_path_set_bool(n, "followMouse", config_focus_follow);
    obt_xml_path_set_int(n, "focusDelay", config_focus_delay);
    obt_xml_path_set_bool(n, "raiseOnFocus", config_focus_raise);
    obt_xml_path_set_bool(n, "focusLast", config_focus_last);
    obt_xml_path_set_bool(n, "underMouse", config_focus_under_mouse);
    obt_xml_path_set_bool(n, "unfocusOnLeave", config_unfocus_leave);

    n = obt_xml_path_get_node(root, "placement", "");
    switch (config_place_policy) {
    case OB_PLACE_POLICY_SMART: e = "smart"; break;
    case OB_PLACE_POLICY_MOUSE: e = "mouse"; break;
    }
    obt_xml_path_set_string(n, "policy", e);
    obt_xml_path_set_bool(n, "center", config_place_center);
    switch (config_place_monitor) {
    case OB_PLACE_MONITOR_PRIMARY: e = "primary"; break;
    case OB_PLACE_MONITOR_ACTIVE: e = "active"; break;
    case OB_PLACE_MONITOR_MOUSE: e = "mouse"; break;
    case OB_PLACE_MONITOR_ANY: e = "any"; break;
    }
    obt_xml_path_set_string(n, "monitor", e);
    if (config_primary_monitor_index)
        obt_xml_path_set_int(
            n, "primaryMonitor", config_primary_monitor_index);
    else {
        switch (config_primary_monitor) {
        case OB_PLACE_MONITOR_ACTIVE: e = "active"; break;
        case OB_PLACE_MONITOR_MOUSE: e = "mouse"; break;
        case OB_PLACE_MONITOR_PRIMARY: 
        case OB_PLACE_MONITOR_ANY: g_assert_not_reached();
        }
        obt_xml_path_set_string(n, "primaryMonitor", e);
    }

    n = obt_xml_path_get_node(root, "margins", "");
    obt_xml_path_set_int(n, "top", config_margins.top);
    obt_xml_path_set_int(n, "left", config_margins.left);
    obt_xml_path_set_int(n, "right", config_margins.right);
    obt_xml_path_set_int(n, "bottom", config_margins.bottom);

    if (!obt_xml_save_cache_file(config_inst, "openbox", "config", TRUE))
        g_warning("Unable to save configuration in XDG cache directory.");
    obt_xml_instance_unref(config_inst);
    config_inst = NULL;
}

gboolean config_load_keys(void)
{
    ObtXmlInst *i = obt_xml_instance_new();
    gboolean ok;

    ok = obt_xml_load_config_file(i, "openbox", "keys", "keys");
    if (!ok)
        bind_default_keyboard();
    else {
        xmlNodePtr n, r;

        r = obt_xml_root(i);
        if ((n = obt_xml_find_sibling(r->children, "keybind")))
            while (n) {
                parse_keybind(n, NULL);
                n = obt_xml_find_sibling(n->next, "keybind");
            }
    }
    obt_xml_instance_unref(i);
    return ok;
}

gboolean config_load_mouse(void)
{
    ObtXmlInst *i = obt_xml_instance_new();
    gboolean ok;

    obt_xml_register(i, "context", parse_context, NULL);

    ok = obt_xml_load_config_file(i, "openbox", "mouse", "mouse");
    if (!ok)
        bind_default_mouse();
    else {
        obt_xml_tree_from_root(i);
        obt_xml_close(i);
    }
    obt_xml_instance_unref(i);
    return ok;
}

gboolean config_load_windows(void)
{
    ObtXmlInst *i = obt_xml_instance_new();
    gboolean ok;

    config_per_app_settings = NULL;
    obt_xml_register(i, "applications", parse_per_app_settings, NULL);

    ok = obt_xml_load_config_file(
        i, "openbox", "applications", "applications");
    if (ok) {
        obt_xml_tree_from_root(i);
        obt_xml_close(i);
    }
    obt_xml_instance_unref(i);
    return ok;
}
