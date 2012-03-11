#include <totem-plugin.h>
#include <totem.h>
#include <libsoup/soup.h>
#include <gtk/gtk.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>

// TODO:
#define GETTEXT_PACKAGE "jtv"
#include <glib/gi18n-lib.h>

#define TOTEM_TYPE_JTV_PLUGIN               (totem_jtv_plugin_get_type ())
#define TOTEM_JTV_PLUGIN(o)                 (G_TYPE_CHECK_INSTANCE_CAST ((o), TOTEM_TYPE_JTV_PLUGIN, TotemJtvPlugin))
#define TOTEM_JTV_PLUGIN_CLASS(k)           (G_TYPE_CHECK_CLASS_CAST((k), TOTEM_TYPE_JTV_PLUGIN, TotemJtvPluginClass))
#define TOTEM_IS_JTV_PLUGIN(o)              (G_TYPE_CHECK_INSTANCE_TYPE ((o), TOTEM_TYPE_JTV_PLUGIN))
#define TOTEM_IS_JTV_PLUGIN_CLASS(k)        (G_TYPE_CHECK_CLASS_TYPE ((k), TOTEM_TYPE_JTV_PLUGIN))
#define TOTEM_JTV_PLUGIN_GET_CLASS(o)       (G_TYPE_INSTANCE_GET_CLASS ((o), TOTEM_TYPE_JTV_PLUGIN, TotemJtvPluginClass))

typedef struct {
  SoupSession *s;
  GtkListStore *model;
  GtkWidget *view;
  GtkWidget *win;
  TotemObject *totem;
  GObject *plugin;
} TotemJtvPluginPrivate;

TOTEM_PLUGIN_REGISTER (TOTEM_TYPE_JTV_PLUGIN, TotemJtvPlugin, totem_jtv_plugin);

enum {
  COLUMN_ID,
  COLUMN_NAME,
  COLUMN_STATUS,
  COLUMN_THUMBNAIL,
  N_COLUMNS,
};

#define CHANNELS_URL "http://api.justin.tv/api/stream/list.xml?language=%s"

typedef struct {
  GtkTreePath *path;
  TotemJtvPluginPrivate *priv;
} ThumbnailData;

static void
show_error(const char *title, const char *msg, TotemJtvPluginPrivate *priv) {
  GtkWidget *window = GTK_WIDGET(totem_get_main_window(priv->totem));

  totem_interface_error(title, msg, window);

  g_object_unref(window);
}

static void
thumbnail_downloaded(SoupSession *s, SoupMessage *m, gpointer data) {
  GdkPixbufLoader *loader = NULL;
  GdkPixbuf *thumb = NULL;

  ThumbnailData *d = (ThumbnailData *)data;

  if (m->status_code == SOUP_STATUS_CANCELLED) {
    goto out;
    return;
  }

  if (!SOUP_STATUS_IS_SUCCESSFUL(m->status_code)) {
    show_error(_("Failed to download channel thumbnail"), m->reason_phrase, d->priv);
    goto out;
  }

  TotemJtvPlugin *plugin = TOTEM_JTV_PLUGIN(d->priv->plugin);

  loader = gdk_pixbuf_loader_new();
  if (!gdk_pixbuf_loader_write(loader, m->response_body->data, m->response_body->length, NULL)) {
    // TODO: error
    goto out;
  }
  else if (!gdk_pixbuf_loader_close(loader, NULL)) {
    // TODO: error
  }

  thumb = gdk_pixbuf_loader_get_pixbuf(loader);

  if (thumb) {
    GtkTreeIter iter;
    if (gtk_tree_model_get_iter(GTK_TREE_MODEL(d->priv->model), &iter, d->path)) {
      gtk_list_store_set(d->priv->model, &iter, COLUMN_THUMBNAIL, thumb, -1);
    }
    else {
      // TODO: error
    }
  }

 out:
  if (loader) {
    g_object_unref(loader);
  }

  gtk_tree_path_free(d->path);
  g_object_unref(d->priv->plugin);
  g_free(d);
}

static void
populate_store(TotemJtvPluginPrivate *priv, const char *xml, gsize len) {
  xmlInitParser();

  xmlXPathContextPtr ctx = NULL;
  xmlXPathObjectPtr obj = NULL;
  xmlDocPtr doc = xmlParseMemory(xml, len);
  if (!doc) {
    // TODO: error
    goto out;
  }

  ctx = xmlXPathNewContext(doc);
  if (!ctx) {
    // TODO: error
    goto out;
  }

  obj = xmlXPathEvalExpression("//channel", ctx);
  if (!obj) {
    // TODO: error
    goto out;
  }

  xmlNodeSetPtr nodes = obj->nodesetval;
  if (!nodes) {
    // TODO: error
    goto out;
  }

  int x = 0;
  for (x = 0; x < nodes->nodeNr; x++) {
    xmlNodePtr cur = nodes->nodeTab[x], n;

    xmlChar *login = NULL, *title = NULL, *status = NULL, *url = NULL;

    n = cur->xmlChildrenNode;
    while (n) {

      if (!xmlStrcmp(n->name, (const xmlChar *)"login")) {
	login = xmlNodeListGetString(doc, n->xmlChildrenNode, 1);
      }
      else if (!xmlStrcmp(n->name, (const xmlChar *)"title")) {
	title = xmlNodeListGetString(doc, n->xmlChildrenNode, 1);
      }
      else if (!xmlStrcmp(n->name, (const xmlChar *)"status")) {
	status = xmlNodeListGetString(doc, n->xmlChildrenNode, 1);
      }
      else if (!xmlStrcmp(n->name, (const xmlChar *)"screen_cap_url_medium")) {
	url = xmlNodeListGetString(doc, n->xmlChildrenNode, 1);
      }

      n = n->next;
    }

    if (!login || !status || !title || !url) {
      // TODO: error.
    }

    GtkTreeIter iter;
    gtk_list_store_append(priv->model, &iter);

    // TODO: tooltip ?
    gtk_list_store_set(priv->model, &iter,
    		       COLUMN_ID, login, COLUMN_NAME, title, COLUMN_STATUS, status, -1);

    ThumbnailData *data = g_new(ThumbnailData, sizeof(ThumbnailData));
    data->path = gtk_tree_model_get_path(GTK_TREE_MODEL(priv->model), &iter);
    data->priv = priv;
    g_object_ref(priv->plugin);

    SoupMessage *msg = soup_message_new ("GET", url);

    soup_session_queue_message(priv->s, msg, thumbnail_downloaded, data);

    xmlFree(login);
    xmlFree(status);
    xmlFree(title);
    xmlFree(url);
  }

 out:
  if (obj) {
    xmlXPathFreeObject(obj);
  }

  if (ctx) {
    xmlXPathFreeContext(ctx);
  }

  if (doc) {
    xmlFreeDoc(doc);
  }

  xmlCleanupParser();
}

static void
channel_list_downloaded(SoupSession *s, SoupMessage *m, gpointer data) {
  TotemJtvPluginPrivate *priv = (TotemJtvPluginPrivate *)data;

  if (m->status_code == SOUP_STATUS_CANCELLED) {
    return;
  }

  if (!SOUP_STATUS_IS_SUCCESSFUL(m->status_code)) {
    show_error(_("Failed to download channel list"), m->reason_phrase, priv);

    return;
  }

  gtk_list_store_clear(priv->model);

  populate_store(priv, m->response_body->data, m->response_body->length);
}

static void view_activated(GtkTreeView *treeview, GtkTreePath *path,
			   GtkTreeViewColumn *col,
			   gpointer data) {

  Totem *totem = (Totem *)data;

  GtkTreeIter iter;

  GtkTreeModel *model = gtk_tree_view_get_model(treeview);

  if (gtk_tree_model_get_iter(model, &iter, path)) {
    gchar *id, *title;

    gtk_tree_model_get(model, &iter, COLUMN_ID, &id, COLUMN_NAME, &title, -1);

    gchar *uri = g_strdup_printf("jtv://%s", id);

    g_free(id);

    totem_add_to_playlist_and_play(totem, uri, title, FALSE);

    g_free(title);
    g_free(uri);
  }
}

static char *
get_file_path() {
  return g_strdup_printf("%s/.totem-plugin-jtv", g_get_home_dir());
}

static GKeyFile *get_key_file() {
  char *path = get_file_path();

  GKeyFile *file = g_key_file_new();

  g_key_file_load_from_file(file, path, G_KEY_FILE_NONE, NULL);

  g_free(path);

  return file;
}

static char *
get_language() {
  GKeyFile *file = get_key_file();

  char *lang = g_key_file_get_string(file, "language", "language", NULL);
  g_key_file_free(file);

  if (!lang) {
    lang = g_strdup("ar");
  }

  return lang;
}

static gboolean
set_language(const char *lang) {
  GKeyFile *file = get_key_file();

  g_key_file_set_string(file, "language", "language", lang);
  char *data = g_key_file_to_data(file, NULL, NULL);
  g_key_file_free(file);

  if (!data) {
    return FALSE;
  }

  char *path = get_file_path();

  gboolean ret = g_file_set_contents(path, data, -1, NULL);
  g_free(path);
  g_free(data);

  return ret;
}

static void refresh_clicked(GtkButton *button, gpointer data) {
  TotemJtvPluginPrivate *priv = (TotemJtvPluginPrivate *)data;

  soup_session_abort(priv->s);

  gchar *lang = get_language();

  gchar *url = g_strdup_printf(CHANNELS_URL, lang);

  SoupMessage *msg = soup_message_new ("GET", url);

  g_free(lang);
  g_free(url);

  soup_session_queue_message(priv->s, msg, channel_list_downloaded, priv);
}

static void
settings_clicked(GtkButton *button, gpointer data) {
  TotemJtvPluginPrivate *priv = (TotemJtvPluginPrivate *)data;

  GtkWindow *window = totem_get_main_window(priv->totem);

  GtkWidget *box = gtk_vbox_new(FALSE, 20);
  GtkWidget *label = gtk_label_new(_("Language code for channel list.\nExample: ar (For Arabic)"));

  char *lang = get_language();
  GtkWidget *entry = gtk_entry_new();
  gtk_entry_set_text(GTK_ENTRY(entry), lang);
  g_free(lang);
  gtk_editable_select_region(GTK_EDITABLE(entry), 0, -1);
  gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);

  gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(box), entry, FALSE, FALSE, 0);

  GtkWidget *dialog =
    gtk_dialog_new_with_buttons(_("Choose language"), window,
				GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
				GTK_STOCK_OK, GTK_RESPONSE_OK,
				GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
				NULL);
  gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);

  gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))),
		    box);

  gtk_widget_show_all(box);

  int resp = gtk_dialog_run(GTK_DIALOG(dialog));

  g_object_unref(window);

  if (resp == GTK_RESPONSE_CANCEL) {
    gtk_widget_destroy(dialog);
    return;
  }

  const char *user_lang = gtk_entry_get_text(GTK_ENTRY(entry));
  if (!user_lang) {
    // TODO: error.
  }
  else if (!set_language(user_lang)) {
    // TODO: error
  }
  else {
    // refresh.
  }

  gtk_widget_destroy(dialog);

  refresh_clicked(NULL, priv);
}

static void
create_ui(TotemJtvPluginPrivate *priv) {
  GtkWidget *hbox, *vbox;
  GtkWidget *view, *win;
  GtkListStore *model;
  GtkWidget *refresh, *settings;

  win = gtk_scrolled_window_new(NULL, NULL);
  hbox = gtk_hbox_new(TRUE, 10);
  vbox = gtk_vbox_new(FALSE, 10);

  gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(vbox), win, TRUE, TRUE, 0);

  model = gtk_list_store_new(N_COLUMNS,
				   G_TYPE_STRING, // id
				   G_TYPE_STRING, // name
				   G_TYPE_STRING, // status
				   GDK_TYPE_PIXBUF); // thumbnail

  view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(model));
  gtk_tree_voew_set_headers_visible(GTK_TREE_VIEW(view), FALSE);

  gtk_container_add(GTK_CONTAINER(win), view);

  GtkCellRenderer *renderer = GTK_CELL_RENDERER(totem_cell_renderer_video_new(TRUE));
  GtkTreeViewColumn *column =
    gtk_tree_view_column_new_with_attributes(_("Channel"), renderer,
					      "title", COLUMN_NAME,
					      "thumbnail", COLUMN_THUMBNAIL,
					      NULL);

  gtk_tree_view_append_column(GTK_TREE_VIEW(view), column);
  gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(view), COLUMN_STATUS);

  refresh = gtk_button_new_from_stock(GTK_STOCK_REFRESH);
  settings = gtk_button_new_from_stock(GTK_STOCK_PREFERENCES);

  gtk_box_pack_start(GTK_BOX(hbox), refresh, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(hbox), settings, FALSE, FALSE, 0);

  priv->view = view;
  priv->model = model;
  priv->win = vbox;

  g_signal_connect(refresh, "clicked", G_CALLBACK(refresh_clicked), priv);
  g_signal_connect(settings, "clicked", G_CALLBACK(settings_clicked), priv);

  g_signal_connect(priv->view, "row-activated", G_CALLBACK(view_activated), priv->totem);
}

static void
impl_activate (PeasActivatable *plugin) {
  TotemJtvPlugin *self = TOTEM_JTV_PLUGIN(plugin);
  TotemJtvPluginPrivate *priv = self->priv;
  self->priv->plugin = g_object_ref(self);

  priv->totem = g_object_ref(g_object_get_data (G_OBJECT (plugin), "object"));

  create_ui(priv);

  priv->s = soup_session_async_new();

  refresh_clicked(NULL, priv);

  gtk_widget_show_all(priv->win);
  totem_add_sidebar_page(priv->totem, "jtv", _("Justin TV"), priv->win);
}

static void
impl_deactivate (PeasActivatable *plugin)
{
  TotemJtvPlugin *self = TOTEM_JTV_PLUGIN(plugin);
  TotemJtvPluginPrivate *priv = self->priv;

  totem_remove_sidebar_page(priv->totem, "jtv");

  g_object_unref(priv->totem);

  g_object_unref(priv->model);
  g_object_unref(priv->view);
  g_object_unref(priv->win);

  soup_session_abort(priv->s);
  g_object_unref(priv->s);
  g_object_unref(self->priv->plugin);
}


//static GtkWidget *
//impl_create_configure_widget (PeasGtkConfigurable *configurable) {
//  GtkWidget *box = gtk_vbox_new(FALSE, 10);

  //  GtkWidget *label = gtk_label_new("Language code:");
  //  GtkWidget *entry = gtk_entry_new();

//  gtk_widget_show_all(box);

//  return box;
//}
