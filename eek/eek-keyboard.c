/* 
 * Copyright (C) 2010-2011 Daiki Ueno <ueno@unixuser.org>
 * Copyright (C) 2010-2011 Red Hat, Inc.
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

/**
 * SECTION:eek-keyboard
 * @short_description: Base class of a keyboard
 * @see_also: #EekSection
 *
 * The #EekKeyboardClass class represents a keyboard, which consists
 * of one or more sections of the #EekSectionClass class.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif  /* HAVE_CONFIG_H */

#include "eek-keyboard.h"
#include "eek-section.h"
#include "eek-key.h"
#include "eek-symbol.h"
#include "eek-enumtypes.h"

enum {
    PROP_0,
    PROP_LAYOUT,
    PROP_MODIFIER_BEHAVIOR,
    PROP_LAST
};

enum {
    KEY_PRESSED,
    KEY_RELEASED,
    KEY_LOCKED,
    KEY_UNLOCKED,
    KEY_CANCELLED,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE (EekKeyboard, eek_keyboard, EEK_TYPE_CONTAINER);

#define EEK_KEYBOARD_GET_PRIVATE(obj)                                  \
    (G_TYPE_INSTANCE_GET_PRIVATE ((obj), EEK_TYPE_KEYBOARD, EekKeyboardPrivate))

struct _EekKeyboardPrivate
{
    EekLayout *layout;
    EekModifierBehavior modifier_behavior;
    EekModifierType modifiers;
    GList *pressed_keys;
    GList *locked_keys;
    GArray *outline_array;
    GHashTable *keycodes;

    /* modifiers dynamically assigned at run time */
    EekModifierType num_lock_mask;
    EekModifierType alt_gr_mask;
};

G_DEFINE_BOXED_TYPE(EekModifierKey, eek_modifier_key,
                    eek_modifier_key_copy, eek_modifier_key_free);

EekModifierKey *
eek_modifier_key_copy (EekModifierKey *modkey)
{
    return g_slice_dup (EekModifierKey, modkey);
}

void
eek_modifier_key_free (EekModifierKey *modkey)
{
    g_object_unref (modkey->key);
    g_slice_free (EekModifierKey, modkey);
}

static void
on_key_pressed (EekSection  *section,
                EekKey      *key,
                EekKeyboard *keyboard)
{
    g_signal_emit (keyboard, signals[KEY_PRESSED], 0, key);
}

static void
on_key_released (EekSection  *section,
                 EekKey      *key,
                 EekKeyboard *keyboard)
{
    g_signal_emit (keyboard, signals[KEY_RELEASED], 0, key);
}

static void
on_key_locked (EekSection  *section,
                EekKey      *key,
                EekKeyboard *keyboard)
{
    g_signal_emit (keyboard, signals[KEY_LOCKED], 0, key);
}

static void
on_key_unlocked (EekSection  *section,
                 EekKey      *key,
                 EekKeyboard *keyboard)
{
    g_signal_emit (keyboard, signals[KEY_UNLOCKED], 0, key);
}

static void
on_key_cancelled (EekSection  *section,
                 EekKey      *key,
                 EekKeyboard *keyboard)
{
    g_signal_emit (keyboard, signals[KEY_CANCELLED], 0, key);
}

static void
on_symbol_index_changed (EekSection *section,
                         gint group,
                         gint level,
                         EekKeyboard *keyboard)
{
    g_signal_emit_by_name (keyboard, "symbol-index-changed", group, level);
}

static void
section_child_added_cb (EekContainer *container,
                        EekElement   *element,
                        EekKeyboard  *keyboard)
{
    guint keycode = eek_key_get_keycode (EEK_KEY(element));
    g_hash_table_insert (keyboard->priv->keycodes,
                         GUINT_TO_POINTER(keycode),
                         element);
}

static void
section_child_removed_cb (EekContainer *container,
                          EekElement   *element,
                          EekKeyboard  *keyboard)
{
    guint keycode = eek_key_get_keycode (EEK_KEY(element));
    g_hash_table_remove (keyboard->priv->keycodes,
                         GUINT_TO_POINTER(keycode));
}

static EekSection *
eek_keyboard_real_create_section (EekKeyboard *self)
{
    EekSection *section;

    section = g_object_new (EEK_TYPE_SECTION, NULL);
    g_return_val_if_fail (section, NULL);

    g_signal_connect (G_OBJECT(section), "child-added",
                      G_CALLBACK(section_child_added_cb), self);

    g_signal_connect (G_OBJECT(section), "child-removed",
                      G_CALLBACK(section_child_removed_cb), self);

    EEK_CONTAINER_GET_CLASS(self)->add_child (EEK_CONTAINER(self),
                                              EEK_ELEMENT(section));
    return section;
}

static void
eek_keyboard_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
    EekKeyboardPrivate *priv = EEK_KEYBOARD_GET_PRIVATE(object);

    switch (prop_id) {
    case PROP_LAYOUT:
        priv->layout = g_value_get_object (value);
        if (priv->layout)
            g_object_ref (priv->layout);
        break;
    case PROP_MODIFIER_BEHAVIOR:
        eek_keyboard_set_modifier_behavior (EEK_KEYBOARD(object),
                                            g_value_get_enum (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
eek_keyboard_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
    EekKeyboardPrivate *priv = EEK_KEYBOARD_GET_PRIVATE(object);

    switch (prop_id) {
    case PROP_LAYOUT:
        g_value_set_object (value, priv->layout);
        break;
    case PROP_MODIFIER_BEHAVIOR:
        g_value_set_enum (value,
                          eek_keyboard_get_modifier_behavior (EEK_KEYBOARD(object)));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
set_level_from_modifiers (EekKeyboard *self)
{
    EekKeyboardPrivate *priv = EEK_KEYBOARD_GET_PRIVATE(self);
    gint level = 0;

    if (priv->modifiers & priv->alt_gr_mask)
        level |= 2;
    if (priv->modifiers & EEK_SHIFT_MASK)
        level |= 1;
    eek_element_set_level (EEK_ELEMENT(self), level);
}

static void
set_modifiers_with_key (EekKeyboard    *self,
                        EekKey         *key,
                        EekModifierType modifiers)
{
    EekKeyboardPrivate *priv = EEK_KEYBOARD_GET_PRIVATE(self);
    EekModifierType enabled = (priv->modifiers ^ modifiers) & modifiers;
    EekModifierType disabled = (priv->modifiers ^ modifiers) & priv->modifiers;

    if (enabled != 0) {
        if (priv->modifier_behavior != EEK_MODIFIER_BEHAVIOR_NONE) {
            EekModifierKey *modifier_key = g_slice_new (EekModifierKey);
            modifier_key->modifiers = enabled;
            modifier_key->key = g_object_ref (key);
            priv->locked_keys =
                g_list_prepend (priv->locked_keys, modifier_key);
            g_signal_emit_by_name (modifier_key->key, "locked");
        }
    } else {
        if (priv->modifier_behavior != EEK_MODIFIER_BEHAVIOR_NONE) {
            GList *head;
            for (head = priv->locked_keys; head; ) {
                EekModifierKey *modifier_key = head->data;
                if (modifier_key->modifiers & disabled) {
                    GList *next = g_list_next (head);
                    priv->locked_keys =
                        g_list_remove_link (priv->locked_keys, head);
                    g_signal_emit_by_name (modifier_key->key, "unlocked");
                    g_list_free1 (head);
                    head = next;
                } else
                    head = g_list_next (head);
            }
        }
    }

    priv->modifiers = modifiers;
}

static void
eek_keyboard_real_key_pressed (EekKeyboard *self,
                               EekKey      *key)
{
    EekKeyboardPrivate *priv = EEK_KEYBOARD_GET_PRIVATE(self);
    EekSymbol *symbol;
    EekModifierType modifier;

    priv->pressed_keys = g_list_prepend (priv->pressed_keys, key);

    symbol = eek_key_get_symbol_with_fallback (key, 0, 0);
    if (!symbol)
        return;

    modifier = eek_symbol_get_modifier_mask (symbol);
    if (priv->modifier_behavior == EEK_MODIFIER_BEHAVIOR_NONE) {
        set_modifiers_with_key (self, key, priv->modifiers | modifier);
        set_level_from_modifiers (self);
    }
}

static void
eek_keyboard_real_key_released (EekKeyboard *self,
                                EekKey      *key)
{
    EekKeyboardPrivate *priv = EEK_KEYBOARD_GET_PRIVATE(self);
    EekSymbol *symbol;
    EekModifierType modifier;

    EEK_KEYBOARD_GET_CLASS (self)->key_cancelled (self, key);

    symbol = eek_key_get_symbol_with_fallback (key, 0, 0);
    if (!symbol)
        return;

    modifier = eek_symbol_get_modifier_mask (symbol);
    switch (priv->modifier_behavior) {
    case EEK_MODIFIER_BEHAVIOR_NONE:
        set_modifiers_with_key (self, key, priv->modifiers & ~modifier);
        break;
    case EEK_MODIFIER_BEHAVIOR_LOCK:
        priv->modifiers ^= modifier;
        break;
    case EEK_MODIFIER_BEHAVIOR_LATCH:
        if (modifier)
            set_modifiers_with_key (self, key, priv->modifiers ^ modifier);
        else
            set_modifiers_with_key (self, key,
                                    (priv->modifiers ^ modifier) & modifier);
        break;
    }
    set_level_from_modifiers (self);
}

static void
eek_keyboard_real_key_cancelled (EekKeyboard *self,
                                 EekKey      *key)
{
    EekKeyboardPrivate *priv = EEK_KEYBOARD_GET_PRIVATE(self);
    GList *head;

    for (head = priv->pressed_keys; head; head = g_list_next (head)) {
        if (head->data == key) {
            priv->pressed_keys = g_list_remove_link (priv->pressed_keys, head);
            g_list_free1 (head);
            break;
        }
    }
}

static void
eek_keyboard_dispose (GObject *object)
{
    EekKeyboardPrivate *priv = EEK_KEYBOARD_GET_PRIVATE(object);

    if (priv->layout) {
        g_object_unref (priv->layout);
        priv->layout = NULL;
    }

    G_OBJECT_CLASS (eek_keyboard_parent_class)->dispose (object);
}

static void
eek_keyboard_finalize (GObject *object)
{
    EekKeyboardPrivate *priv = EEK_KEYBOARD_GET_PRIVATE(object);
    gint i;

    g_list_free (priv->pressed_keys);
    g_list_free_full (priv->locked_keys,
                      (GDestroyNotify) eek_modifier_key_free);

    g_hash_table_destroy (priv->keycodes);

    for (i = 0; i < priv->outline_array->len; i++) {
        EekOutline *outline = &g_array_index (priv->outline_array,
                                              EekOutline,
                                              i);
        g_slice_free1 (sizeof (EekPoint) * outline->num_points,
                       outline->points);
    }
    g_array_free (priv->outline_array, TRUE);
        
    G_OBJECT_CLASS (eek_keyboard_parent_class)->finalize (object);
}

static void
eek_keyboard_real_child_added (EekContainer *self,
                               EekElement   *element)
{
    g_signal_connect (element, "key-pressed",
                      G_CALLBACK(on_key_pressed), self);
    g_signal_connect (element, "key-released",
                      G_CALLBACK(on_key_released), self);
    g_signal_connect (element, "key-locked",
                      G_CALLBACK(on_key_locked), self);
    g_signal_connect (element, "key-unlocked",
                      G_CALLBACK(on_key_unlocked), self);
    g_signal_connect (element, "key-cancelled",
                      G_CALLBACK(on_key_cancelled), self);
    g_signal_connect (element, "symbol-index-changed",
                      G_CALLBACK(on_symbol_index_changed), self);
}

static void
eek_keyboard_real_child_removed (EekContainer *self,
                                 EekElement   *element)
{
    g_signal_handlers_disconnect_by_func (element, on_key_pressed, self);
    g_signal_handlers_disconnect_by_func (element, on_key_released, self);
    g_signal_handlers_disconnect_by_func (element, on_key_locked, self);
    g_signal_handlers_disconnect_by_func (element, on_key_unlocked, self);
    g_signal_handlers_disconnect_by_func (element, on_key_cancelled, self);
}

static void
eek_keyboard_class_init (EekKeyboardClass *klass)
{
    EekContainerClass *container_class = EEK_CONTAINER_CLASS (klass);
    GObjectClass      *gobject_class = G_OBJECT_CLASS (klass);
    GParamSpec        *pspec;

    g_type_class_add_private (gobject_class,
                              sizeof (EekKeyboardPrivate));

    klass->create_section = eek_keyboard_real_create_section;

    /* signals */
    klass->key_pressed = eek_keyboard_real_key_pressed;
    klass->key_released = eek_keyboard_real_key_released;
    klass->key_cancelled = eek_keyboard_real_key_cancelled;

    container_class->child_added = eek_keyboard_real_child_added;
    container_class->child_removed = eek_keyboard_real_child_removed;

    gobject_class->get_property = eek_keyboard_get_property;
    gobject_class->set_property = eek_keyboard_set_property;
    gobject_class->dispose = eek_keyboard_dispose;
    gobject_class->finalize = eek_keyboard_finalize;

    /**
     * EekKeyboard:layout:
     *
     * The layout used to create this #EekKeyboard.
     */
    pspec = g_param_spec_object ("layout",
                                 "Layout",
                                 "Layout used to create the keyboard",
                                 EEK_TYPE_LAYOUT,
                                 G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);
    g_object_class_install_property (gobject_class,
                                     PROP_LAYOUT,
                                     pspec);

    /**
     * EekKeyboard:modifier-behavior:
     *
     * The modifier handling mode of #EekKeyboard.
     */
    pspec = g_param_spec_enum ("modifier-behavior",
                               "Modifier behavior",
                               "Modifier handling mode of the keyboard",
                               EEK_TYPE_MODIFIER_BEHAVIOR,
                               EEK_MODIFIER_BEHAVIOR_NONE,
                               G_PARAM_READWRITE);
    g_object_class_install_property (gobject_class,
                                     PROP_MODIFIER_BEHAVIOR,
                                     pspec);

    /**
     * EekKeyboard::key-pressed:
     * @keyboard: an #EekKeyboard
     * @key: an #EekKey
     *
     * The ::key-pressed signal is emitted each time a key in @keyboard
     * is shifted to the pressed state.
     */
    signals[KEY_PRESSED] =
        g_signal_new (I_("key-pressed"),
                      G_TYPE_FROM_CLASS(gobject_class),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET(EekKeyboardClass, key_pressed),
                      NULL,
                      NULL,
                      g_cclosure_marshal_VOID__OBJECT,
                      G_TYPE_NONE,
                      1,
                      EEK_TYPE_KEY);

    /**
     * EekKeyboard::key-released:
     * @keyboard: an #EekKeyboard
     * @key: an #EekKey
     *
     * The ::key-released signal is emitted each time a key in @keyboard
     * is shifted to the released state.
     */
    signals[KEY_RELEASED] =
        g_signal_new (I_("key-released"),
                      G_TYPE_FROM_CLASS(gobject_class),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET(EekKeyboardClass, key_released),
                      NULL,
                      NULL,
                      g_cclosure_marshal_VOID__OBJECT,
                      G_TYPE_NONE,
                      1,
                      EEK_TYPE_KEY);

    /**
     * EekKeyboard::key-locked:
     * @keyboard: an #EekKeyboard
     * @key: an #EekKey
     *
     * The ::key-locked signal is emitted each time a key in @keyboard
     * is shifted to the locked state.
     */
    signals[KEY_LOCKED] =
        g_signal_new (I_("key-locked"),
                      G_TYPE_FROM_CLASS(gobject_class),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET(EekKeyboardClass, key_locked),
                      NULL,
                      NULL,
                      g_cclosure_marshal_VOID__OBJECT,
                      G_TYPE_NONE,
                      1,
                      EEK_TYPE_KEY);

    /**
     * EekKeyboard::key-unlocked:
     * @keyboard: an #EekKeyboard
     * @key: an #EekKey
     *
     * The ::key-unlocked signal is emitted each time a key in @keyboard
     * is shifted to the unlocked state.
     */
    signals[KEY_UNLOCKED] =
        g_signal_new (I_("key-unlocked"),
                      G_TYPE_FROM_CLASS(gobject_class),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET(EekKeyboardClass, key_unlocked),
                      NULL,
                      NULL,
                      g_cclosure_marshal_VOID__OBJECT,
                      G_TYPE_NONE,
                      1,
                      EEK_TYPE_KEY);

    /**
     * EekKeyboard::key-cancelled:
     * @keyboard: an #EekKeyboard
     * @key: an #EekKey
     *
     * The ::key-cancelled signal is emitted each time a key in @keyboard
     * is shifted to the cancelled state.
     */
    signals[KEY_CANCELLED] =
        g_signal_new (I_("key-cancelled"),
                      G_TYPE_FROM_CLASS(gobject_class),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET(EekKeyboardClass, key_cancelled),
                      NULL,
                      NULL,
                      g_cclosure_marshal_VOID__OBJECT,
                      G_TYPE_NONE,
                      1,
                      EEK_TYPE_KEY);
}

static void
eek_keyboard_init (EekKeyboard *self)
{
    self->priv = EEK_KEYBOARD_GET_PRIVATE(self);
    self->priv->modifier_behavior = EEK_MODIFIER_BEHAVIOR_NONE;
    self->priv->outline_array = g_array_new (FALSE, TRUE, sizeof (EekOutline));
    self->priv->keycodes = g_hash_table_new (g_direct_hash, g_direct_equal);
    eek_element_set_symbol_index (EEK_ELEMENT(self), 0, 0);
}

/**
 * eek_keyboard_create_section:
 * @keyboard: an #EekKeyboard
 *
 * Create an #EekSection instance and append it to @keyboard.  This
 * function is rarely called by application but called by #EekLayout
 * implementation.
 */
EekSection *
eek_keyboard_create_section (EekKeyboard *keyboard)
{
    g_return_val_if_fail (EEK_IS_KEYBOARD(keyboard), NULL);
    return EEK_KEYBOARD_GET_CLASS(keyboard)->create_section (keyboard);
}

/**
 * eek_keyboard_find_key_by_keycode:
 * @keyboard: an #EekKeyboard
 * @keycode: a keycode
 *
 * Find an #EekKey whose keycode is @keycode.
 * Return value: (transfer none): #EekKey whose keycode is @keycode
 */
EekKey *
eek_keyboard_find_key_by_keycode (EekKeyboard *keyboard,
                                  guint        keycode)
{
    g_return_val_if_fail (EEK_IS_KEYBOARD(keyboard), NULL);
    return g_hash_table_lookup (keyboard->priv->keycodes,
                                GUINT_TO_POINTER(keycode));
}

/**
 * eek_keyboard_get_layout:
 * @keyboard: an #EekKeyboard
 *
 * Get the layout used to create @keyboard.
 * Returns: an #EekLayout
 */
EekLayout *
eek_keyboard_get_layout (EekKeyboard *keyboard)
{
    g_return_val_if_fail (EEK_IS_KEYBOARD(keyboard), NULL);
    return keyboard->priv->layout;
}

/**
 * eek_keyboard_get_size:
 * @keyboard: an #EekKeyboard
 * @width: width of @keyboard
 * @height: height of @keyboard
 *
 * Get the size of @keyboard.
 */
void
eek_keyboard_get_size (EekKeyboard *keyboard,
                       gdouble     *width,
                       gdouble     *height)
{
    EekBounds bounds;

    eek_element_get_bounds (EEK_ELEMENT(keyboard), &bounds);
    *width = bounds.width;
    *height = bounds.height;
}

/**
 * eek_keyboard_set_modifier_behavior:
 * @keyboard: an #EekKeyboard
 * @modifier_behavior: modifier behavior of @keyboard
 *
 * Set the modifier handling mode of @keyboard.
 */
void
eek_keyboard_set_modifier_behavior (EekKeyboard        *keyboard,
                                    EekModifierBehavior modifier_behavior)
{
    g_return_if_fail (EEK_IS_KEYBOARD(keyboard));
    keyboard->priv->modifier_behavior = modifier_behavior;
}

/**
 * eek_keyboard_get_modifier_behavior:
 * @keyboard: an #EekKeyboard
 *
 * Get the modifier handling mode of @keyboard.
 * Returns: #EekModifierBehavior
 */
EekModifierBehavior
eek_keyboard_get_modifier_behavior (EekKeyboard *keyboard)
{
    g_return_val_if_fail (EEK_IS_KEYBOARD(keyboard), 0);
    return keyboard->priv->modifier_behavior;
}

void
eek_keyboard_set_modifiers (EekKeyboard    *keyboard,
                            EekModifierType modifiers)
{
    g_return_if_fail (EEK_IS_KEYBOARD(keyboard));
    keyboard->priv->modifiers = modifiers;
    set_level_from_modifiers (keyboard);
}

/**
 * eek_keyboard_get_modifiers:
 * @keyboard: an #EekKeyboard
 *
 * Get the current modifier status of @keyboard.
 * Returns: #EekModifierType
 */
EekModifierType
eek_keyboard_get_modifiers (EekKeyboard *keyboard)
{
    g_return_val_if_fail (EEK_IS_KEYBOARD(keyboard), 0);
    return keyboard->priv->modifiers;
}

/**
 * eek_keyboard_add_outline:
 * @keyboard: an #EekKeyboard
 * @outline: an #EekOutline
 *
 * Register an outline of @keyboard.
 * Returns: an unsigned integer ID of the registered outline, for
 * later reference
 */
guint
eek_keyboard_add_outline (EekKeyboard *keyboard,
                          EekOutline  *outline)
{
    EekOutline *_outline;

    g_return_val_if_fail (EEK_IS_KEYBOARD(keyboard), 0);

    _outline = eek_outline_copy (outline);
    g_array_append_val (keyboard->priv->outline_array, *_outline);
    /* don't use eek_outline_free here, so as to keep _outline->points */
    g_slice_free (EekOutline, _outline);
    return keyboard->priv->outline_array->len - 1;
}

/**
 * eek_keyboard_get_outline:
 * @keyboard: an #EekKeyboard
 * @oref: ID of the outline
 *
 * Get an outline associated with @oref in @keyboard.
 * Returns: an #EekOutline, which should not be released
 */
EekOutline *
eek_keyboard_get_outline (EekKeyboard *keyboard,
                          guint        oref)
{
    g_return_val_if_fail (EEK_IS_KEYBOARD(keyboard), NULL);

    if (oref > keyboard->priv->outline_array->len)
        return NULL;

    return &g_array_index (keyboard->priv->outline_array, EekOutline, oref);
}

/**
 * eek_keyboard_get_n_outlines:
 * @keyboard: an #EekKeyboard
 *
 * Get the number of outlines defined in @keyboard.
 * Returns: integer
 */
gsize
eek_keyboard_get_n_outlines (EekKeyboard *keyboard)
{
    g_return_val_if_fail (EEK_IS_KEYBOARD(keyboard), 0);
    return keyboard->priv->outline_array->len;
}

/**
 * eek_keyboard_set_num_lock_mask:
 * @keyboard: an #EekKeyboard
 * @num_lock_mask: an #EekModifierType
 *
 * Set modifier mask used as Num_Lock.
 */
void
eek_keyboard_set_num_lock_mask (EekKeyboard    *keyboard,
                                EekModifierType num_lock_mask)
{
    g_return_if_fail (EEK_IS_KEYBOARD(keyboard));
    keyboard->priv->num_lock_mask = num_lock_mask;
}

/**
 * eek_keyboard_get_num_lock_mask:
 * @keyboard: an #EekKeyboard
 *
 * Get modifier mask used as Num_Lock.
 * Returns: an #EekModifierType
 */
EekModifierType
eek_keyboard_get_num_lock_mask (EekKeyboard *keyboard)
{
    g_return_val_if_fail (EEK_IS_KEYBOARD(keyboard), 0);
    return keyboard->priv->num_lock_mask;
}

/**
 * eek_keyboard_set_alt_gr_mask:
 * @keyboard: an #EekKeyboard
 * @alt_gr_mask: an #EekModifierType
 *
 * Set modifier mask used as Alt_Gr.
 */
void
eek_keyboard_set_alt_gr_mask (EekKeyboard    *keyboard,
                              EekModifierType alt_gr_mask)
{
    g_return_if_fail (EEK_IS_KEYBOARD(keyboard));
    keyboard->priv->alt_gr_mask = alt_gr_mask;
}

/**
 * eek_keyboard_get_alt_gr_mask:
 * @keyboard: an #EekKeyboard
 *
 * Get modifier mask used as Alt_Gr.
 * Returns: an #EekModifierType
 */
EekModifierType
eek_keyboard_get_alt_gr_mask (EekKeyboard *keyboard)
{
    g_return_val_if_fail (EEK_IS_KEYBOARD(keyboard), 0);
    return keyboard->priv->alt_gr_mask;
}

/**
 * eek_keyboard_get_pressed_keys:
 * @keyboard: an #EekKeyboard
 *
 * Get pressed keys.
 * Returns: (transfer container) (element-type EekKey): A list of
 * pressed keys.
 */
GList *
eek_keyboard_get_pressed_keys (EekKeyboard *keyboard)
{
    g_return_val_if_fail (EEK_IS_KEYBOARD(keyboard), NULL);
    return g_list_copy (keyboard->priv->pressed_keys);
}

/**
 * eek_keyboard_get_locked_keys:
 * @keyboard: an #EekKeyboard
 *
 * Get locked keys.
 * Returns: (transfer container) (element-type Eek.ModifierKey): A list
 * of locked keys.
 */
GList *
eek_keyboard_get_locked_keys (EekKeyboard *keyboard)
{
    g_return_val_if_fail (EEK_IS_KEYBOARD(keyboard), NULL);
    return g_list_copy (keyboard->priv->locked_keys);
}
