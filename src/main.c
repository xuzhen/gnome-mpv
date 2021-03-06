/*
 * Copyright (c) 2014-2015 gnome-mpv
 *
 * This file is part of GNOME MPV.
 *
 * GNOME MPV is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * GNOME MPV is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNOME MPV.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <glib/gi18n.h>
#include <gdk/gdkx.h>
#include <locale.h>

#include "def.h"
#include "common.h"
#include "mpv.h"
#include "keybind.h"
#include "playlist.h"
#include "playbackctl.h"
#include "actionctl.h"
#include "main_window.h"
#include "control_box.h"
#include "playlist_widget.h"
#include "pref_dialog.h"
#include "open_loc_dialog.h"

static gboolean draw_handler(GtkWidget *widget, cairo_t *cr, gpointer data);
static void destroy_handler(GtkWidget *widget, gpointer data);
static void setup_dnd_targets(gmpv_handle *ctx);
static void connect_signals(gmpv_handle *ctx);
static void setup_accelerators(gmpv_handle *ctx);
static GMenu *build_app_menu(void);
static void app_startup_handler(GApplication *app, gpointer data);
static void app_cmdline_handler(	GApplication *app,
					GApplicationCommandLine *cmdline,
					gpointer data );
static void drag_data_handler(	GtkWidget *widget,
				GdkDragContext *context,
				gint x,
				gint y,
				GtkSelectionData *sel_data,
				guint info,
				guint time,
				gpointer data);
static gboolean key_press_handler(	GtkWidget *widget,
					GdkEvent *event,
					gpointer data );
static gboolean mouse_press_handler(	GtkWidget *widget,
					GdkEvent *event,
					gpointer data );

static gboolean draw_handler(GtkWidget *widget, cairo_t *cr, gpointer data)
{
	gmpv_handle *ctx = data;
	guint signal_id = g_signal_lookup("draw", MAIN_WINDOW_TYPE);

	g_signal_handlers_disconnect_matched(	widget,
						G_SIGNAL_MATCH_ID
						|G_SIGNAL_MATCH_DATA,
						signal_id,
						0,
						0,
						NULL,
						ctx );

	mpv_init(ctx, ctx->vid_area_wid);
	mpv_set_wakeup_callback(ctx->mpv_ctx, mpv_wakeup_callback, ctx);

	if(ctx->argc >= 2)
	{
		gint i = 0;

		ctx->paused = FALSE;

		for(i = 1; i < ctx->argc; i++)
		{
			gchar *path = get_name_from_path(ctx->argv[i]);

			playlist_widget_append
				(	PLAYLIST_WIDGET(ctx->gui->playlist),
					path,
					ctx->argv[i] );

			g_free(path);
		}
	}
	else
	{
		control_box_set_enabled
			(CONTROL_BOX(ctx->gui->control_box), FALSE);
	}

	return FALSE;
}

static void destroy_handler(GtkWidget *widget, gpointer data)
{
	quit(data);
}

static void drag_data_handler(	GtkWidget *widget,
				GdkDragContext *context,
				gint x,
				gint y,
				GtkSelectionData *sel_data,
				guint info,
				guint time,
				gpointer data)
{
	gboolean append = (widget == ((gmpv_handle *)data)->gui->playlist);

	if(sel_data && gtk_selection_data_get_length(sel_data) > 0)
	{
		gmpv_handle *ctx = data;
		gchar **uri_list = gtk_selection_data_get_uris(sel_data);

		ctx->paused = FALSE;

		if(uri_list)
		{
			int i;

			for(i = 0; uri_list[i]; i++)
			{
				mpv_load(	ctx,
						uri_list[i],
						(append || i != 0),
						TRUE );
			}

			g_strfreev(uri_list);
		}
		else
		{
			const guchar *raw_data
				= gtk_selection_data_get_data(sel_data);

			mpv_load(ctx, (const gchar *)raw_data, append, TRUE);
		}
	}
}

static gboolean key_press_handler(	GtkWidget *widget,
					GdkEvent *event,
					gpointer data )
{
	gmpv_handle *ctx = data;
	guint keyval = ((GdkEventKey*)event)->keyval;
	guint state = ((GdkEventKey*)event)->state;
	gchar **command;

	const guint mod_mask =	GDK_MODIFIER_MASK
				&~(GDK_SHIFT_MASK
				|GDK_LOCK_MASK
				|GDK_MOD2_MASK
				|GDK_MOD3_MASK
				|GDK_MOD4_MASK
				|GDK_MOD5_MASK);

	/* Ignore insignificant modifiers (eg. numlock) */
	state &= mod_mask;

	command = keybind_get_command(ctx, FALSE, state, keyval);

	/* Try user-specified keys first, then fallback to hard-coded keys */
	if(command)
	{
		mpv_command(ctx->mpv_ctx, (const char **)command);
	}
	else if((state&mod_mask) == 0)
	{
		/* Accept F11 and f for entering/exiting fullscreen mode. ESC is
		 * only used for exiting fullscreen mode. F11 is handled via
		 * accelrator.
		 */
		if((ctx->gui->fullscreen && keyval == GDK_KEY_Escape)
		|| keyval == GDK_KEY_f)
		{
			GAction *action ;

			action = g_action_map_lookup_action
					(G_ACTION_MAP(ctx->app), "fullscreen");

			g_action_activate(action, NULL);
		}
		else if(keyval == GDK_KEY_Delete
		&& main_window_get_playlist_visible(ctx->gui))
		{
			remove_current_playlist_entry(ctx);
		}
	}

	return FALSE;
}

static gboolean mouse_press_handler(	GtkWidget *widget,
					GdkEvent *event,
					gpointer data )
{
	gmpv_handle *ctx = data;
	GdkEventButton *btn_event = (GdkEventButton *)event;
	gchar **command;

	command = keybind_get_command(	ctx,
					TRUE,
					btn_event->type == GDK_2BUTTON_PRESS,
					btn_event->button );

	if(command)
	{
		mpv_command(ctx->mpv_ctx, (const char **)command);
	}

	return TRUE;
}

static void app_cmdline_handler(	GApplication *app,
					GApplicationCommandLine *cmdline,
					gpointer data )
{
	gmpv_handle *ctx = data;

	ctx->argv = g_application_command_line_get_arguments(	cmdline,
								&ctx->argc );
}

static void setup_dnd_targets(gmpv_handle *ctx)
{
	PlaylistWidget *playlist;
	GtkTargetEntry target_entry[3];

	playlist = PLAYLIST_WIDGET(ctx->gui->playlist);

	target_entry[0].target = "text/uri-list";
	target_entry[0].flags = 0;
	target_entry[0].info = 0;
	target_entry[1].target = "text/plain";
	target_entry[1].flags = 0;
	target_entry[1].info = 1;
	target_entry[2].target = "STRING";
	target_entry[2].flags = 0;
	target_entry[2].info = 1;

	gtk_drag_dest_set(	GTK_WIDGET(ctx->gui->vid_area),
				GTK_DEST_DEFAULT_ALL,
				target_entry,
				3,
				GDK_ACTION_LINK );

	gtk_drag_dest_set(	GTK_WIDGET(playlist),
				GTK_DEST_DEFAULT_ALL,
				target_entry,
				3,
				GDK_ACTION_COPY );

	gtk_drag_dest_add_uri_targets(GTK_WIDGET(ctx->gui->vid_area));
	gtk_drag_dest_add_uri_targets(GTK_WIDGET(playlist));
}

static void connect_signals(gmpv_handle *ctx)
{
	PlaylistWidget *playlist = PLAYLIST_WIDGET(ctx->gui->playlist);

	playbackctl_connect_signals(ctx);

	g_signal_connect(	ctx->gui->vid_area,
				"drag-data-received",
				G_CALLBACK(drag_data_handler),
				ctx );

	g_signal_connect(	ctx->gui->playlist,
				"drag-data-received",
				G_CALLBACK(drag_data_handler),
				ctx );

	g_signal_connect(	ctx->gui,
				"draw",
				G_CALLBACK(draw_handler),
				ctx );

	g_signal_connect(	ctx->gui,
				"destroy",
				G_CALLBACK(destroy_handler),
				ctx );

	g_signal_connect(	ctx->gui,
				"key-press-event",
				G_CALLBACK(key_press_handler),
				ctx );

	g_signal_connect(	ctx->gui->vid_area,
				"button-press-event",
				G_CALLBACK(mouse_press_handler),
				ctx );

	g_signal_connect(	playlist->tree_view,
				"row-activated",
				G_CALLBACK(playlist_row_handler),
				ctx );

	g_signal_connect(	playlist->list_store,
				"row-inserted",
				G_CALLBACK(playlist_row_inserted_handler),
				ctx );

	g_signal_connect(	playlist->list_store,
				"row-deleted",
				G_CALLBACK(playlist_row_deleted_handler),
				ctx );
}

static void setup_accelerators(gmpv_handle *ctx)
{
	gtk_application_add_accelerator(	ctx->app,
						"<Control>o",
						"app.open",
						NULL );

	gtk_application_add_accelerator(	ctx->app,
						"<Control>l" ,
						"app.openloc",
						NULL );

	gtk_application_add_accelerator(	ctx->app,
						"<Control>S" ,
						"app.playlist_save",
						NULL );

	gtk_application_add_accelerator(	ctx->app,
						"<Control>q" ,
						"app.quit",
						NULL );

	gtk_application_add_accelerator(	ctx->app,
						"<Control>p" ,
						"app.pref",
						NULL );

	gtk_application_add_accelerator(	ctx->app,
						"F9" ,
						"app.playlist_toggle",
						NULL );

	gtk_application_add_accelerator(	ctx->app,
						"<Control>1" ,
						"app.normalsize",
						NULL );

	gtk_application_add_accelerator(	ctx->app,
						"<Control>2" ,
						"app.doublesize",
						NULL );

	gtk_application_add_accelerator(	ctx->app,
						"<Control>3" ,
						"app.halfsize",
						NULL );

	gtk_application_add_accelerator(	ctx->app,
						"F11" ,
						"app.fullscreen",
						NULL );
}

static GMenu *build_app_menu()
{
	GMenu *menu;
	GMenu *top_section;
	GMenu *bottom_section;
	GMenuItem *pref_menu_item;
	GMenuItem *about_menu_item;
	GMenuItem *quit_menu_item;

	menu = g_menu_new();
	top_section = g_menu_new();
	bottom_section = g_menu_new();
	pref_menu_item = g_menu_item_new(_("_Preferences"), "app.pref");
	about_menu_item = g_menu_item_new(_("_About"), "app.about");
	quit_menu_item = g_menu_item_new(_("_Quit"), "app.quit");

	g_menu_append_section(menu, NULL, G_MENU_MODEL(top_section));
	g_menu_append_section(menu, NULL, G_MENU_MODEL(bottom_section));
	g_menu_append_item(top_section, pref_menu_item);
	g_menu_append_item(bottom_section, about_menu_item);
	g_menu_append_item(bottom_section, quit_menu_item);

	return menu;
}

static void app_startup_handler(GApplication *app, gpointer data)
{
	gmpv_handle *ctx = data;
	gboolean config_migrated;
	gboolean mpvinput_enable;
	gboolean csd_enable;
	gboolean dark_theme_enable;
	gchar *mpvinput;

	setlocale(LC_NUMERIC, "C");
	g_set_application_name(_("GNOME MPV"));
	gtk_window_set_default_icon_name(ICON_NAME);

	bindtextdomain(GETTEXT_PACKAGE, PACKAGE_LOCALEDIR);
	bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
	textdomain(GETTEXT_PACKAGE);

	ctx->mpv_ctx = mpv_create();
	ctx->paused = TRUE;
	ctx->loaded = FALSE;
	ctx->new_file = TRUE;
	ctx->sub_visible = TRUE;
	ctx->load_cmdline = TRUE;
	ctx->playlist_move_dest = -1;
	ctx->log_buffer = NULL;
	ctx->keybind_list = NULL;
	ctx->config_file = g_key_file_new();
	ctx->app = GTK_APPLICATION(app);
	ctx->gui = MAIN_WINDOW(main_window_new(ctx->app));
	ctx->fs_control = NULL;
	ctx->playlist_store = PLAYLIST_WIDGET(ctx->gui->playlist)->list_store;

	config_migrated = migrate_config();

	load_config(ctx);

	csd_enable = get_config_boolean
			(ctx, "main", "csd-enable", TRUE);

	dark_theme_enable = get_config_boolean
				(ctx, "main", "dark-theme-enable", TRUE);

	mpvinput_enable = get_config_boolean
				(ctx, "main", "mpv-input-config-enable", FALSE);

	mpvinput = get_config_string(ctx, "main", "mpv-input-config-file");

	if(csd_enable)
	{
		gtk_application_set_app_menu
			(ctx->app, G_MENU_MODEL(build_app_menu()));

		main_window_enable_csd(ctx->gui);
	}
	else
	{
		gtk_application_set_app_menu
			(ctx->app, NULL);

		gtk_application_set_menubar
			(ctx->app, G_MENU_MODEL(build_full_menu()));
	}

	gtk_widget_show_all(GTK_WIDGET(ctx->gui));
	main_window_set_playlist_visible(ctx->gui, FALSE);

	if(csd_enable)
	{
		control_box_set_fullscreen_btn_visible
			(CONTROL_BOX(ctx->gui->control_box), FALSE);
	}

	control_box_set_chapter_enabled
		(CONTROL_BOX(ctx->gui->control_box), FALSE);

	ctx->vid_area_wid = gdk_x11_window_get_xid
				(gtk_widget_get_window(ctx->gui->vid_area));

	setup_accelerators(ctx);
	setup_dnd_targets(ctx);
	actionctl_map_actions(ctx);
	connect_signals(ctx);
	load_keybind(ctx, mpvinput_enable?mpvinput:NULL, FALSE);

	g_object_set(	ctx->gui->settings,
			"gtk-application-prefer-dark-theme",
			dark_theme_enable,
			NULL );

	g_timeout_add(	SEEK_BAR_UPDATE_INTERVAL,
			(GSourceFunc)update_seek_bar,
			ctx );

	if(config_migrated)
	{
		const gchar * msg
			= _(	"Your configuration file has been moved to "
				"the new location at %s." );

		GtkWidget *dialog
			= gtk_message_dialog_new
				(	GTK_WINDOW(ctx->gui),
					GTK_DIALOG_DESTROY_WITH_PARENT,
					GTK_MESSAGE_INFO,
					GTK_BUTTONS_OK,
					msg,
					get_config_file_path() );

		gtk_dialog_run(GTK_DIALOG(dialog));
		gtk_widget_destroy(dialog);
	}
}

int main(int argc, char **argv)
{
	GtkApplication *app;
	gmpv_handle *ctx;
	gint status;

	app = gtk_application_new(	NULL,
					G_APPLICATION_NON_UNIQUE|
					G_APPLICATION_HANDLES_COMMAND_LINE );

	ctx = g_malloc(sizeof(gmpv_handle));

	g_signal_connect(	app,
				"startup",
				G_CALLBACK(app_startup_handler),
				ctx );

	g_signal_connect(	app,
				"command-line",
				G_CALLBACK(app_cmdline_handler),
				ctx );

	status = g_application_run(G_APPLICATION(app), argc, argv);

	g_object_unref(app);

	return status;
}
