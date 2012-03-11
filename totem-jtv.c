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

#define CHANNELS_URL "http://api.justin.tv/api/stream/list.xml?language=ar"

typedef struct {
  GtkTreePath *path;
  TotemJtvPluginPrivate *priv;
} ThumbnailData;

static void thumbnail_downloaded(SoupSession *s, SoupMessage *m, gpointer data) {
  GdkPixbufLoader *loader = NULL;
  GdkPixbuf *thumb = NULL;

  ThumbnailData *d = (ThumbnailData *)data;

  if (!SOUP_STATUS_IS_SUCCESSFUL(m->status_code)) {
    // TODO: error.
    puts("Failed");
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

static void populate_store(TotemJtvPluginPrivate *priv, const char *xml, gsize len) {
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

static void channel_list_downloaded(SoupSession *s, SoupMessage *m, gpointer data) {
  if (!SOUP_STATUS_IS_SUCCESSFUL(m->status_code)) {
    // TODO: error.
    puts("Failed");

    return;
  }

  TotemJtvPluginPrivate *priv = (TotemJtvPluginPrivate *)data;

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

static void
impl_activate (PeasActivatable *plugin)
{
  TotemJtvPlugin *self = TOTEM_JTV_PLUGIN(plugin);
  TotemJtvPluginPrivate *priv = self->priv;
  self->priv->plugin = g_object_ref(self);

  priv->totem = g_object_ref(g_object_get_data (G_OBJECT (plugin), "object"));

  priv->win = gtk_scrolled_window_new(NULL, NULL);

  priv->model = gtk_list_store_new(N_COLUMNS,
				   G_TYPE_STRING, // id
				   G_TYPE_STRING, // name
				   G_TYPE_STRING, // status
				   GDK_TYPE_PIXBUF); // thumbnail

  priv->view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(priv->model));
  g_signal_connect(priv->view, "row-activated", G_CALLBACK(view_activated), priv->totem);

  gtk_container_add(GTK_CONTAINER(priv->win), priv->view);

  GtkCellRenderer *renderer = GTK_CELL_RENDERER(totem_cell_renderer_video_new(TRUE));
  GtkTreeViewColumn *column =
    gtk_tree_view_column_new_with_attributes(_("Channel"), renderer,
					      "title", COLUMN_NAME,
					      "thumbnail", COLUMN_THUMBNAIL,
					      NULL);

  gtk_tree_view_append_column(GTK_TREE_VIEW(priv->view), column);
  gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(priv->view), COLUMN_STATUS);

  priv->s = soup_session_async_new();

  SoupMessage *msg = soup_message_new ("GET", CHANNELS_URL);

  soup_session_queue_message(priv->s, msg, channel_list_downloaded, priv);

  gtk_widget_show_all(priv->view);
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
