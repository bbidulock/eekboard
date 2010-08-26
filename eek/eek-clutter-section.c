/* 
 * Copyright (C) 2010 Daiki Ueno <ueno@unixuser.org>
 * Copyright (C) 2010 Red Hat, Inc.
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif  /* HAVE_CONFIG_H */

#include "eek-clutter-section.h"

G_DEFINE_TYPE (EekClutterSection, eek_clutter_section, EEK_TYPE_SECTION);

#define EEK_CLUTTER_SECTION_GET_PRIVATE(obj)                           \
    (G_TYPE_INSTANCE_GET_PRIVATE ((obj), EEK_TYPE_CLUTTER_SECTION, EekClutterSectionPrivate))

struct _EekClutterSectionPrivate
{
    EekClutterDrawingContext *context;
    ClutterActor *actor;
};

static void
eek_clutter_section_real_set_name (EekElement  *self,
                                   const gchar *name)
{
    EekClutterSectionPrivate *priv = EEK_CLUTTER_SECTION_GET_PRIVATE(self);

    EEK_ELEMENT_CLASS (eek_clutter_section_parent_class)->
        set_name (self, name);

    if (priv->actor)
        clutter_actor_set_name (priv->actor, name);
}

static void
eek_clutter_section_real_set_bounds (EekElement *self,
                                     EekBounds  *bounds)
{
    EekClutterSectionPrivate *priv = EEK_CLUTTER_SECTION_GET_PRIVATE(self);

    EEK_ELEMENT_CLASS (eek_clutter_section_parent_class)->
        set_bounds (self, bounds);

    if (priv->actor) {
        clutter_actor_set_position (priv->actor, bounds->x, bounds->y);
        clutter_actor_set_size (priv->actor, bounds->width, bounds->height);
    }
}

static void
eek_clutter_section_real_set_angle (EekSection *self,
                                    gint angle)
{
    EekClutterSectionPrivate *priv = EEK_CLUTTER_SECTION_GET_PRIVATE(self);

    EEK_SECTION_CLASS (eek_clutter_section_parent_class)->
        set_angle (self, angle);

    g_return_if_fail (priv->actor);

    clutter_actor_set_rotation (priv->actor,
                                CLUTTER_Z_AXIS,
                                eek_section_get_angle (self),
                                0, 0, 0);
}

static void
pressed_event (EekKey *key, gpointer user_data)
{
    g_signal_emit_by_name (user_data, "key-pressed", key);
}

static void
released_event (EekKey *key, gpointer user_data)
{
    g_signal_emit_by_name (user_data, "key-released", key);
}

static EekKey *
eek_clutter_section_real_create_key (EekSection  *self,
                                     gint         column,
                                     gint         row)
{
    EekClutterSectionPrivate *priv = EEK_CLUTTER_SECTION_GET_PRIVATE(self);
    EekKey *key;
    gint num_columns, num_rows;
    EekOrientation orientation;
    ClutterActor *actor;
    EekThemeNode *tnode;

    num_rows = eek_section_get_n_rows (self);
    g_return_val_if_fail (0 <= row && row < num_rows, NULL);
    eek_section_get_row (self, row, &num_columns, &orientation);
    g_return_val_if_fail (column < num_columns, NULL);

    key = eek_clutter_key_new (priv->context, column, row);
    g_return_val_if_fail (key, NULL);
    
    tnode = eek_element_get_theme_node (EEK_ELEMENT(self));
    if (tnode)
        eek_element_set_theme_node
            (EEK_ELEMENT(key),
             eek_theme_node_new (tnode,
                                 eek_theme_node_get_theme (tnode),
                                 NULL,
                                 NULL,
                                 "key",
                                 "key",
                                 NULL));

    g_signal_connect (key, "pressed", G_CALLBACK(pressed_event), self);
    g_signal_connect (key, "released", G_CALLBACK(released_event), self);

    EEK_CONTAINER_GET_CLASS(self)->add_child (EEK_CONTAINER(self),
                                              EEK_ELEMENT(key));

    actor = eek_clutter_section_get_actor (EEK_CLUTTER_SECTION(self));
    clutter_container_add_actor
        (CLUTTER_CONTAINER(actor),
         eek_clutter_key_get_actor (EEK_CLUTTER_KEY(key)));

    return key;
}

static void
eek_clutter_section_dispose (GObject *object)
{
    EekClutterSectionPrivate *priv = EEK_CLUTTER_SECTION_GET_PRIVATE(object);

    if (priv->context) {
        g_object_unref (priv->context);
        priv->context = NULL;
    }
    if (priv->actor) {
        g_object_unref (priv->actor);
        priv->actor = NULL;
    }
    G_OBJECT_CLASS (eek_clutter_section_parent_class)->dispose (object);
}

static void
eek_clutter_section_class_init (EekClutterSectionClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
    EekElementClass *element_class = EEK_ELEMENT_CLASS (klass);
    EekSectionClass *section_class = EEK_SECTION_CLASS (klass);

    g_type_class_add_private (gobject_class, sizeof (EekClutterSectionPrivate));

    section_class->set_angle = eek_clutter_section_real_set_angle;
    section_class->create_key = eek_clutter_section_real_create_key;
    element_class->set_name = eek_clutter_section_real_set_name;
    element_class->set_bounds = eek_clutter_section_real_set_bounds;
    gobject_class->dispose = eek_clutter_section_dispose;
}

static void
eek_clutter_section_init (EekClutterSection *self)
{
    EekClutterSectionPrivate *priv;
    priv = self->priv = EEK_CLUTTER_SECTION_GET_PRIVATE (self);
    priv->actor = NULL;
}

ClutterActor *
eek_clutter_section_get_actor (EekClutterSection *section)
{
    EekClutterSectionPrivate *priv = EEK_CLUTTER_SECTION_GET_PRIVATE(section);
    g_return_val_if_fail (priv, NULL);
    if (!priv->actor) {
        priv->actor = clutter_group_new ();
        g_object_ref_sink (priv->actor);
    }
    return priv->actor;
}

EekSection *
eek_clutter_section_new (EekClutterDrawingContext *context)
{
    EekClutterSection *section;

    g_return_val_if_fail (context, NULL);
    section = g_object_new (EEK_TYPE_CLUTTER_SECTION, NULL);
    section->priv->context = context;
    g_object_ref_sink (G_OBJECT(section->priv->context));

    return EEK_SECTION(section);
}
