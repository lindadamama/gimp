/* LIBGIMP - The GIMP Library
 * Copyright (C) 1995-1997 Peter Mattis and Spencer Kimball
 *
 * gimpcolornotebook.c
 * Copyright (C) 2002 Michael Natterer <mitch@gimp.org>
 *
 * based on color_notebook module
 * Copyright (C) 1998 Austin Donnelly <austin@greenend.org.uk>
 *
 * This library is free software: you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see
 * <https://www.gnu.org/licenses/>.
 */

#include "config.h"

#include <gegl.h>
#include <gtk/gtk.h>

#include "libgimpcolor/gimpcolor.h"

#include "gimpwidgetstypes.h"

#include "gimpcolornotebook.h"
#include "gimpcolorscales.h"
#include "gimphelpui.h"

#include "libgimp/libgimp-intl.h"


/**
 * SECTION: gimpcolornotebook
 * @title: GimpColorNotebook
 * @short_description: A #GimpColorSelector implementation.
 *
 * The #GimpColorNotebook widget is an implementation of a
 * #GimpColorSelector. It serves as a container for
 * #GimpColorSelectors.
 **/


#define DEFAULT_TAB_ICON_SIZE  GTK_ICON_SIZE_BUTTON


struct _GimpColorNotebook
{
  GimpColorSelector  parent_instance;

  GtkWidget         *notebook;

  GList             *selectors;
  GimpColorSelector *cur_page;
};


static void   gimp_color_notebook_style_updated   (GtkWidget         *widget);

static void   gimp_color_notebook_togg_visible    (GimpColorSelector *selector,
                                                   gboolean           visible);
static void   gimp_color_notebook_togg_sensitive  (GimpColorSelector *selector,
                                                   gboolean           sensitive);
static void   gimp_color_notebook_set_show_alpha  (GimpColorSelector *selector,
                                                   gboolean           show_alpha);
static void   gimp_color_notebook_set_color       (GimpColorSelector *selector,
                                                   GeglColor         *color);
static void   gimp_color_notebook_set_channel     (GimpColorSelector *selector,
                                                   GimpColorSelectorChannel channel);
static void   gimp_color_notebook_set_model_visible
                                                  (GimpColorSelector *selector,
                                                   GimpColorSelectorModel model,
                                                   gboolean           gboolean);
static void   gimp_color_notebook_set_config      (GimpColorSelector *selector,
                                                   GimpColorConfig   *config);


static void   gimp_color_notebook_switch_page     (GtkNotebook       *gtk_notebook,
                                                   gpointer           page,
                                                   guint              page_num,
                                                   GimpColorNotebook *notebook);

static void   gimp_color_notebook_color_changed   (GimpColorSelector *page,
                                                   GeglColor         *color,
                                                   GimpColorNotebook *notebook);
static void   gimp_color_notebook_channel_changed (GimpColorSelector *page,
                                                   GimpColorSelectorChannel channel,
                                                   GimpColorNotebook *notebook);
static void   gimp_color_notebook_model_visible_changed
                                                  (GimpColorSelector *page,
                                                   GimpColorSelectorModel model,
                                                   gboolean           visible,
                                                   GimpColorNotebook *notebook);

static GtkWidget * gimp_color_notebook_add_page   (GimpColorNotebook *notebook,
                                                   GType              page_type);
static void   gimp_color_notebook_remove_selector (GtkContainer      *container,
                                                   GtkWidget         *widget,
                                                   GimpColorNotebook *notebook);


G_DEFINE_TYPE (GimpColorNotebook, gimp_color_notebook, GIMP_TYPE_COLOR_SELECTOR)

#define parent_class gimp_color_notebook_parent_class


static void
gimp_color_notebook_class_init (GimpColorNotebookClass *klass)
{
  GtkWidgetClass         *widget_class   = GTK_WIDGET_CLASS (klass);
  GimpColorSelectorClass *selector_class = GIMP_COLOR_SELECTOR_CLASS (klass);

  widget_class->style_updated           = gimp_color_notebook_style_updated;

  selector_class->name                  = "Notebook";
  selector_class->help_id               = "gimp-colorselector-notebook";
  selector_class->set_toggles_visible   = gimp_color_notebook_togg_visible;
  selector_class->set_toggles_sensitive = gimp_color_notebook_togg_sensitive;
  selector_class->set_show_alpha        = gimp_color_notebook_set_show_alpha;
  selector_class->set_color             = gimp_color_notebook_set_color;
  selector_class->set_channel           = gimp_color_notebook_set_channel;
  selector_class->set_model_visible     = gimp_color_notebook_set_model_visible;
  selector_class->set_config            = gimp_color_notebook_set_config;

  gtk_widget_class_install_style_property (widget_class,
                                           g_param_spec_enum ("tab-icon-size",
                                                              NULL,
                                                              "Size for icons displayed in the tab",
                                                              GTK_TYPE_ICON_SIZE,
                                                              DEFAULT_TAB_ICON_SIZE,
                                                              G_PARAM_READABLE));

  gtk_widget_class_set_css_name (widget_class, "GimpColorNotebook");
}

static void
gimp_color_notebook_init (GimpColorNotebook *notebook)
{
  GType *selector_types;
  guint  n_selector_types;
  guint  i;

  notebook->notebook = gtk_notebook_new ();
  gtk_notebook_popup_enable (GTK_NOTEBOOK (notebook->notebook));
  gtk_box_pack_start (GTK_BOX (notebook), notebook->notebook, TRUE, TRUE, 0);
  gtk_widget_show (notebook->notebook);

  g_signal_connect (notebook->notebook, "switch-page",
                    G_CALLBACK (gimp_color_notebook_switch_page),
                    notebook);
  g_signal_connect (notebook->notebook, "remove",
                    G_CALLBACK (gimp_color_notebook_remove_selector),
                    notebook);

  selector_types = g_type_children (GIMP_TYPE_COLOR_SELECTOR,
                                    &n_selector_types);

  if (n_selector_types == 2)
    {
      gtk_notebook_set_show_tabs (GTK_NOTEBOOK (notebook->notebook), FALSE);
      gtk_notebook_set_show_border (GTK_NOTEBOOK (notebook->notebook), FALSE);
    }

  for (i = 0; i < n_selector_types; i++)
    {
      /*  skip ourselves  */
      if (g_type_is_a (selector_types[i], GIMP_TYPE_COLOR_NOTEBOOK))
        continue;

      /*  skip the "Scales" color selector  */
      if (g_type_is_a (selector_types[i], GIMP_TYPE_COLOR_SCALES))
        continue;

      gimp_color_notebook_add_page (notebook, selector_types[i]);
    }

  g_free (selector_types);
}

static void
gimp_color_notebook_style_updated (GtkWidget *widget)
{
  GimpColorNotebook *notebook = GIMP_COLOR_NOTEBOOK (widget);
  GList             *list;
  GtkIconSize        icon_size;

  GTK_WIDGET_CLASS (parent_class)->style_updated (widget);

  gtk_widget_style_get (widget,
                        "tab-icon-size", &icon_size,
                        NULL);

  for (list = notebook->selectors; list; list = g_list_next (list))
    {
      GimpColorSelectorClass *selector_class;
      GtkWidget              *image;

      selector_class = GIMP_COLOR_SELECTOR_GET_CLASS (list->data);

      image = gtk_image_new_from_icon_name (selector_class->icon_name,
                                            icon_size);
      gimp_help_set_help_data (image, gettext (selector_class->name), NULL);

      gtk_notebook_set_tab_label (GTK_NOTEBOOK (notebook->notebook),
                                  GTK_WIDGET (list->data),
                                  image);
    }
}

static void
gimp_color_notebook_togg_visible (GimpColorSelector *selector,
                                  gboolean           visible)
{
  GimpColorNotebook *notebook = GIMP_COLOR_NOTEBOOK (selector);
  GList             *list;

  for (list = notebook->selectors; list; list = g_list_next (list))
    {
      GimpColorSelector *child = list->data;

      gimp_color_selector_set_toggles_visible (child, visible);
    }
}

static void
gimp_color_notebook_togg_sensitive (GimpColorSelector *selector,
                                    gboolean           sensitive)
{
  GimpColorNotebook *notebook = GIMP_COLOR_NOTEBOOK (selector);
  GList             *list;

  for (list = notebook->selectors; list; list = g_list_next (list))
    {
      GimpColorSelector *child = list->data;

      gimp_color_selector_set_toggles_sensitive (child, sensitive);
    }
}

static void
gimp_color_notebook_set_show_alpha (GimpColorSelector *selector,
                                    gboolean           show_alpha)
{
  GimpColorNotebook *notebook = GIMP_COLOR_NOTEBOOK (selector);
  GList             *list;

  for (list = notebook->selectors; list; list = g_list_next (list))
    {
      GimpColorSelector *child = list->data;

      gimp_color_selector_set_show_alpha (child, show_alpha);
    }
}

static void
gimp_color_notebook_set_color (GimpColorSelector *selector,
                               GeglColor         *color)
{
  GimpColorNotebook *notebook = GIMP_COLOR_NOTEBOOK (selector);

  g_signal_handlers_block_by_func (notebook->cur_page,
                                   gimp_color_notebook_color_changed,
                                   selector);

  gimp_color_selector_set_color (notebook->cur_page, color);

  g_signal_handlers_unblock_by_func (notebook->cur_page,
                                     gimp_color_notebook_color_changed,
                                     selector);
}

static void
gimp_color_notebook_set_channel (GimpColorSelector        *selector,
                                 GimpColorSelectorChannel  channel)
{
  GimpColorNotebook *notebook = GIMP_COLOR_NOTEBOOK (selector);

  g_signal_handlers_block_by_func (notebook->cur_page,
                                   gimp_color_notebook_channel_changed,
                                   selector);

  gimp_color_selector_set_channel (notebook->cur_page, channel);

  g_signal_handlers_unblock_by_func (notebook->cur_page,
                                     gimp_color_notebook_channel_changed,
                                     selector);
}

static void
gimp_color_notebook_set_model_visible (GimpColorSelector      *selector,
                                       GimpColorSelectorModel  model,
                                       gboolean                visible)
{
  GimpColorNotebook *notebook = GIMP_COLOR_NOTEBOOK (selector);

  g_signal_handlers_block_by_func (notebook->cur_page,
                                   gimp_color_notebook_model_visible_changed,
                                   selector);

  gimp_color_selector_set_model_visible (notebook->cur_page, model, visible);

  g_signal_handlers_unblock_by_func (notebook->cur_page,
                                     gimp_color_notebook_model_visible_changed,
                                     selector);
}

static void
gimp_color_notebook_set_config (GimpColorSelector *selector,
                                GimpColorConfig   *config)
{
  GimpColorNotebook *notebook = GIMP_COLOR_NOTEBOOK (selector);
  GList             *list;

  for (list = notebook->selectors; list; list = g_list_next (list))
    {
      GimpColorSelector *child = list->data;

      gimp_color_selector_set_config (child, config);
    }
}

static void
gimp_color_notebook_switch_page (GtkNotebook       *gtk_notebook,
                                 gpointer           page,
                                 guint              page_num,
                                 GimpColorNotebook *notebook)
{
  GimpColorSelector      *selector = GIMP_COLOR_SELECTOR (notebook);
  GtkWidget              *page_widget;
  GeglColor              *color;
  GimpColorSelectorModel  model;

  page_widget = gtk_notebook_get_nth_page (gtk_notebook, page_num);

  notebook->cur_page = GIMP_COLOR_SELECTOR (page_widget);

  g_signal_handlers_block_by_func (notebook->cur_page,
                                   gimp_color_notebook_color_changed,
                                   notebook);
  g_signal_handlers_block_by_func (notebook->cur_page,
                                   gimp_color_notebook_channel_changed,
                                   notebook);
  g_signal_handlers_block_by_func (notebook->cur_page,
                                   gimp_color_notebook_model_visible_changed,
                                   notebook);

  color = gimp_color_selector_get_color (selector);
  gimp_color_selector_set_color (notebook->cur_page, color);
  g_object_unref (color);
  gimp_color_selector_set_channel (notebook->cur_page,
                                   gimp_color_selector_get_channel (selector));

  for (model = GIMP_COLOR_SELECTOR_MODEL_RGB;
       model <= GIMP_COLOR_SELECTOR_MODEL_HSV;
       model++)
    {
      gboolean visible = gimp_color_selector_get_model_visible (selector, model);

      gimp_color_selector_set_model_visible (notebook->cur_page, model,
                                             visible);
    }

  g_signal_handlers_unblock_by_func (notebook->cur_page,
                                     gimp_color_notebook_color_changed,
                                     notebook);
  g_signal_handlers_unblock_by_func (notebook->cur_page,
                                     gimp_color_notebook_channel_changed,
                                     notebook);
  g_signal_handlers_unblock_by_func (notebook->cur_page,
                                     gimp_color_notebook_model_visible_changed,
                                     notebook);
}

static void
gimp_color_notebook_color_changed (GimpColorSelector *page,
                                   GeglColor         *color,
                                   GimpColorNotebook *notebook)
{
  GimpColorSelector *selector = GIMP_COLOR_SELECTOR (notebook);

  gimp_color_selector_set_color (selector, color);
}

static void
gimp_color_notebook_channel_changed (GimpColorSelector        *page,
                                     GimpColorSelectorChannel  channel,
                                     GimpColorNotebook        *notebook)
{
  GimpColorSelector *selector = GIMP_COLOR_SELECTOR (notebook);

  gimp_color_selector_set_channel (selector, channel);
}

static void
gimp_color_notebook_model_visible_changed (GimpColorSelector      *page,
                                           GimpColorSelectorModel  model,
                                           gboolean                visible,
                                           GimpColorNotebook      *notebook)
{
  GimpColorSelector *selector = GIMP_COLOR_SELECTOR (notebook);

  gimp_color_selector_set_model_visible (selector, model, visible);
}

static GtkWidget *
gimp_color_notebook_add_page (GimpColorNotebook *notebook,
                              GType              page_type)
{
  GimpColorSelector        *selector = GIMP_COLOR_SELECTOR (notebook);
  GimpColorSelectorClass   *selector_class;
  GtkWidget                *page;
  GtkWidget                *menu_widget;
  GtkWidget                *image;
  GtkWidget                *label;
  GeglColor                *color;
  gboolean                  show_alpha;

  color = gimp_color_selector_get_color (selector);
  page = gimp_color_selector_new (page_type, color,
                                  gimp_color_selector_get_channel (selector));
  g_object_unref (color);

  if (! page)
    return NULL;

  selector_class = GIMP_COLOR_SELECTOR_GET_CLASS (page);

  show_alpha = gimp_color_selector_get_show_alpha (GIMP_COLOR_SELECTOR (notebook));
  gimp_color_selector_set_show_alpha (GIMP_COLOR_SELECTOR (page), show_alpha);

  menu_widget = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 4);

  image = gtk_image_new_from_icon_name (selector_class->icon_name,
                                        GTK_ICON_SIZE_MENU);
  gtk_box_pack_start (GTK_BOX (menu_widget), image, FALSE, FALSE, 0);
  gtk_widget_show (image);

  label = gtk_label_new (gettext (selector_class->name));
  gtk_box_pack_start (GTK_BOX (menu_widget), label, FALSE, FALSE, 0);
  gtk_widget_show (label);

  image = gtk_image_new_from_icon_name (selector_class->icon_name,
                                        DEFAULT_TAB_ICON_SIZE);
  gimp_help_set_help_data (image, gettext (selector_class->name), NULL);

  gtk_notebook_append_page_menu (GTK_NOTEBOOK (notebook->notebook),
                                 page, image, menu_widget);

  if (! notebook->cur_page)
    notebook->cur_page = GIMP_COLOR_SELECTOR (page);

  notebook->selectors = g_list_append (notebook->selectors, page);

  gtk_widget_show (page);

  g_signal_connect (page, "color-changed",
                    G_CALLBACK (gimp_color_notebook_color_changed),
                    notebook);
  g_signal_connect (page, "channel-changed",
                    G_CALLBACK (gimp_color_notebook_channel_changed),
                    notebook);
  g_signal_connect (page, "model-visible-changed",
                    G_CALLBACK (gimp_color_notebook_model_visible_changed),
                    notebook);

  return page;
}

static void
gimp_color_notebook_remove_selector (GtkContainer      *container,
                                     GtkWidget         *widget,
                                     GimpColorNotebook *notebook)
{
  notebook->selectors = g_list_remove (notebook->selectors, widget);

  if (! notebook->selectors)
    notebook->cur_page = NULL;
}


/**
 * gimp_color_notebook_set_has_page:
 * @notebook:  A #GimpColorNotebook widget.
 * @page_type: The #GType of the notebook page to add or remove.
 * @has_page:  Whether the page should be added or removed.
 *
 * This function adds and removed pages to / from a #GimpColorNotebook.
 * The @page_type passed must be a #GimpColorSelector subtype.
 *
 * Returns: (transfer none): The new page widget, if @has_page was
 *               %TRUE, or %NULL if @has_page was %FALSE.
 **/
GtkWidget *
gimp_color_notebook_set_has_page (GimpColorNotebook *notebook,
                                  GType              page_type,
                                  gboolean           has_page)
{
  GList *list;

  g_return_val_if_fail (GIMP_IS_COLOR_NOTEBOOK (notebook), NULL);
  g_return_val_if_fail (g_type_is_a (page_type, GIMP_TYPE_COLOR_SELECTOR),
                        NULL);
  g_return_val_if_fail (! g_type_is_a (page_type, GIMP_TYPE_COLOR_NOTEBOOK),
                        NULL);

  for (list = notebook->selectors; list; list = g_list_next (list))
    {
      GimpColorSelector *page = list->data;

      if (G_TYPE_FROM_INSTANCE (page) == page_type)
        {
          if (has_page)
            return GTK_WIDGET (page);

          gtk_container_remove (GTK_CONTAINER (notebook->notebook),
                                GTK_WIDGET (page));

          return NULL;
        }
    }

  if (! has_page)
    return NULL;

  return gimp_color_notebook_add_page (notebook, page_type);
}

/**
 * gimp_color_notebook_get_notebook:
 * @notebook:  A #GimpColorNotebook widget.
 *
 * Returns: (transfer none) (type GtkNotebook): The #GtkNotebook inside.
 *
 * Since: 3.0
 **/
GtkWidget *
gimp_color_notebook_get_notebook (GimpColorNotebook *notebook)
{
  g_return_val_if_fail (GIMP_IS_COLOR_NOTEBOOK (notebook), NULL);

  return notebook->notebook;
}

/**
 * gimp_color_notebook_get_selectors:
 * @notebook:  A #GimpColorNotebook widget.
 *
 * Returns: (element-type GimpColorSelector) (transfer none): The
 *               notebook's list of #GimpColorSelector's.
 *
 * Since: 3.0
 **/
GList *
gimp_color_notebook_get_selectors (GimpColorNotebook *notebook)
{
  g_return_val_if_fail (GIMP_IS_COLOR_NOTEBOOK (notebook), NULL);

  return notebook->selectors;
}

/**
 * gimp_color_notebook_get_current_selector:
 * @notebook:  A #GimpColorNotebook widget.
 *
 * Returns: (transfer none): The active page's #GimpColorSelector.
 *
 * Since: 3.0
 **/
GimpColorSelector *
gimp_color_notebook_get_current_selector (GimpColorNotebook *notebook)
{
  g_return_val_if_fail (GIMP_IS_COLOR_NOTEBOOK (notebook), NULL);

  return notebook->cur_page;
}

/**
 * gimp_color_notebook_set_format:
 * @notebook:  A #GimpColorNotebook widget.
 * @format:    A Babl format, with space.
 *
 * Updates all selectors with the current format.
 *
 * Since: 3.0
 **/
void
gimp_color_notebook_set_format (GimpColorNotebook *notebook,
                                const Babl        *format)
{
  GList *list;

  g_return_if_fail (GIMP_IS_COLOR_NOTEBOOK (notebook));

  for (list = notebook->selectors; list; list = g_list_next (list))
    {
      GimpColorSelector *selector = list->data;

      if (selector)
        gimp_color_selector_set_format (selector, format);
    }
}

/**
 * gimp_color_notebook_set_simulation:
 * @notebook:  A #GimpColorNotebook widget.
 * @profile:   A #GimpColorProfile object.
 * @intent:    A #GimpColorRenderingIntent enum.
 * @bpc:       A gboolean.
 *
 * Updates all selectors with the current simulation settings.
 *
 * Since: 3.0
 **/
void
gimp_color_notebook_set_simulation (GimpColorNotebook *notebook,
                                    GimpColorProfile  *profile,
                                    GimpColorRenderingIntent intent,
                                    gboolean           bpc)
{
  GList *list;

  g_return_if_fail (GIMP_IS_COLOR_NOTEBOOK (notebook));
  g_return_if_fail (profile == NULL || GIMP_IS_COLOR_PROFILE (profile));

  for (list = notebook->selectors; list; list = g_list_next (list))
    {
      GimpColorSelector *selector = list->data;

      if (selector)
        gimp_color_selector_set_simulation (selector, profile, intent, bpc);
    }
}

void
gimp_color_notebook_enable_simulation (GimpColorNotebook *notebook,
                                       gboolean           enabled)
{
  GList *list;

  g_return_if_fail (GIMP_IS_COLOR_NOTEBOOK (notebook));

  for (list = notebook->selectors; list; list = g_list_next (list))
    {
      GimpColorSelector *selector = list->data;

      if (selector)
        gimp_color_selector_enable_simulation (selector, enabled);
    }
}
