/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* This file started as a cut-and-paste of cr-sel-eng.c from libcroco.
 *
 * In moving it to hippo-canvas:
 * - Reformatted and otherwise edited to match our coding style
 * - Switched from handling xmlNode to handling HippoStyle
 * - Simplified by removing things that we don't need or that don't
 *   make sense in our context.
 * - The code to get a list of matching properties works quite differently;
 *   we order things in priority order, but we don't actually try to
 *   coalesce properties with the same name.
 *
 * In moving it to GNOME Shell:
 *  - Renamed again to StTheme
 *  - Reformatted to match the gnome-shell coding style
 *  - Removed notion of "theme engine" from hippo-canvas
 *  - pseudo-class matching changed from link enum to strings
 *  - Some code simplification
 *
 * In moving it to libeek:
 *  - Renamed again to EekTheme
 */

/*
 * This file is part of The Croco Library
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2.1 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser
 * General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 *
 * Copyright (C) 2003-2004 Dodji Seketeli.  All Rights Reserved.
 */


#include <stdlib.h>
#include <string.h>

#include <gio/gio.h>

#include "eek-theme.h"
#include "eek-theme-node.h"
#include "eek-theme-private.h"

static GObject *eek_theme_constructor (GType                  type,
                                      guint                  n_construct_properties,
                                      GObjectConstructParam *construct_properties);

static void eek_theme_finalize     (GObject      *object);
static void eek_theme_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec);
static void eek_theme_get_property (GObject      *object,
                                   guint         prop_id,
                                   GValue       *value,
                                   GParamSpec   *pspec);

struct _EekTheme
{
  GObject parent;

  char *application_stylesheet;
  char *default_stylesheet;
  char *theme_stylesheet;
  GSList *custom_stylesheets;

  GHashTable *stylesheets_by_filename;
  GHashTable *filenames_by_stylesheet;

  CRCascade *cascade;
};

struct _EekThemeClass
{
  GObjectClass parent_class;
};

enum
{
  PROP_0,
  PROP_APPLICATION_STYLESHEET,
  PROP_THEME_STYLESHEET,
  PROP_DEFAULT_STYLESHEET
};

G_DEFINE_TYPE (EekTheme, eek_theme, G_TYPE_OBJECT);

/* Quick strcmp.  Test only for == 0 or != 0, not < 0 or > 0.  */
#define strqcmp(str,lit,lit_len) \
  (strlen (str) != (lit_len) || memcmp (str, lit, lit_len))

static void
eek_theme_init (EekTheme *theme)
{
  theme->stylesheets_by_filename = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                          (GDestroyNotify)g_free, (GDestroyNotify)cr_stylesheet_unref);
  theme->filenames_by_stylesheet = g_hash_table_new (g_direct_hash, g_direct_equal);
}

static void
eek_theme_class_init (EekThemeClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructor = eek_theme_constructor;
  object_class->finalize = eek_theme_finalize;
  object_class->set_property = eek_theme_set_property;
  object_class->get_property = eek_theme_get_property;

  /**
   * EekTheme:application-stylesheet:
   *
   * The highest priority stylesheet, representing application-specific
   * styling; this is associated with the CSS "author" stylesheet.
   */
  g_object_class_install_property (object_class,
                                   PROP_APPLICATION_STYLESHEET,
                                   g_param_spec_string ("application-stylesheet",
                                                        "Application Stylesheet",
                                                        "Stylesheet with application-specific styling",
                                                        NULL,
                                                        G_PARAM_READABLE | G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));

  /**
   * EekTheme:theme-stylesheet:
   *
   * The second priority stylesheet, representing theme-specific styling;
   * this is associated with the CSS "user" stylesheet.
   */
  g_object_class_install_property (object_class,
                                   PROP_THEME_STYLESHEET,
                                   g_param_spec_string ("theme-stylesheet",
                                                        "Theme Stylesheet",
                                                        "Stylesheet with theme-specific styling",
                                                        NULL,
                                                        G_PARAM_READABLE | G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));

  /**
   * EekTheme:default-stylesheet:
   *
   * The lowest priority stylesheet, representing global default
   * styling; this is associated with the CSS "user agent" stylesheet.
   */
  g_object_class_install_property (object_class,
                                   PROP_DEFAULT_STYLESHEET,
                                   g_param_spec_string ("default-stylesheet",
                                                        "Default Stylesheet",
                                                        "Stylesheet with global default styling",
                                                        NULL,
                                                        G_PARAM_READABLE | G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));

}

/* This is a workaround for a bug in libcroco < 0.6.2 where
 * function starting with 'r' (and 'u') are misparsed. We work
 * around this by exploiting the fact that libcroco is incomformant
 * with the CSS-spec and case sensitive and pre-convert all
 * occurrences of rgba to RGBA. Then we make our own parsing
 * code check for RGBA as well.
 */
#if LIBCROCO_VERSION_NUMBER < 602
static gboolean
is_identifier_character (char c)
{
  /* Actual CSS rules allow for unicode > 0x00a1 and escaped
   * characters, but we'll assume we won't do that in our stylesheets
   * or at least not next to the string 'rgba'.
   */
  return g_ascii_isalnum(c) || c == '-' || c == '_';
}

static void
convert_rgba_RGBA (char *buf)
{
  char *p;

  p = strstr (buf, "rgba");
  while (p)
    {
      /* Check if this looks like a complete token; this is to
       * avoiding mangling, say, a selector '.rgba-entry' */
      if (!((p > buf && is_identifier_character (*(p - 1))) ||
            (is_identifier_character (*(p + 4)))))
        memcpy(p, "RGBA", 4);
      p += 4;
      p = strstr (p, "rgba");
    }
}

static CRStyleSheet *
parse_stylesheet (const char  *filename,
                  GError     **error)
{
  enum CRStatus status;
  char *contents;
  gsize length;
  CRStyleSheet *stylesheet = NULL;

  if (filename == NULL)
    return NULL;

  if (!g_file_get_contents (filename, &contents, &length, error))
    return NULL;

  convert_rgba_RGBA (contents);

  status = cr_om_parser_simply_parse_buf ((const guchar *) contents,
                                          length,
                                          CR_UTF_8,
                                          &stylesheet);
  g_free (contents);

  if (status != CR_OK)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Error parsing stylesheet '%s'; errcode:%d", filename, status);
      return NULL;
    }

  return stylesheet;
}

CRDeclaration *
_eek_theme_parse_declaration_list (const char *str)
{
  char *copy = g_strdup (str);
  CRDeclaration *result;

  convert_rgba_RGBA (copy);

  result = cr_declaration_parse_list_from_buf ((const guchar *)copy,
                                               CR_UTF_8);
  g_free (copy);

  return result;
}
#else /* LIBCROCO_VERSION_NUMBER >= 602 */
static CRStyleSheet *
parse_stylesheet (const char  *filename,
                  GError     **error)
{
  enum CRStatus status;
  CRStyleSheet *stylesheet;

  if (filename == NULL)
    return NULL;

  status = cr_om_parser_simply_parse_file ((const guchar *) filename,
                                           CR_UTF_8,
                                           &stylesheet);

  if (status != CR_OK)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Error parsing stylesheet '%s'; errcode:%d", filename, status);
      return NULL;
    }

  return stylesheet;
}

CRDeclaration *
_eek_theme_parse_declaration_list (const char *str)
{
  return cr_declaration_parse_list_from_buf ((const guchar *)str,
                                             CR_UTF_8);
}
#endif /* LIBCROCO_VERSION_NUMBER < 602 */

/* Just g_warning for now until we have something nicer to do */
static CRStyleSheet *
parse_stylesheet_nofail (const char *filename)
{
  GError *error = NULL;
  CRStyleSheet *result;

  result = parse_stylesheet (filename, &error);
  if (error)
    {
      g_warning ("%s", error->message);
      g_clear_error (&error);
    }
  return result;
}

static void
insert_stylesheet (EekTheme      *theme,
                   const char   *filename,
                   CRStyleSheet *stylesheet)
{
  char *filename_copy;

  if (stylesheet == NULL)
    return;

  filename_copy = g_strdup(filename);
  cr_stylesheet_ref (stylesheet);

  g_hash_table_insert (theme->stylesheets_by_filename, filename_copy, stylesheet);
  g_hash_table_insert (theme->filenames_by_stylesheet, stylesheet, filename_copy);
}

gboolean
eek_theme_load_stylesheet (EekTheme    *theme,
                          const char *path,
                          GError    **error)
{
  CRStyleSheet *stylesheet;

  stylesheet = parse_stylesheet (path, error);
  if (!stylesheet)
    return FALSE;

  insert_stylesheet (theme, path, stylesheet);
  theme->custom_stylesheets = g_slist_prepend (theme->custom_stylesheets, stylesheet);

  return TRUE;
}

void
eek_theme_unload_stylesheet (EekTheme    *theme,
                            const char *path)
{
  CRStyleSheet *stylesheet;

  stylesheet = g_hash_table_lookup (theme->stylesheets_by_filename, path);
  if (!stylesheet)
    return;

  if (!g_slist_find (theme->custom_stylesheets, stylesheet))
    return;

  theme->custom_stylesheets = g_slist_remove (theme->custom_stylesheets, stylesheet);
  g_hash_table_remove (theme->stylesheets_by_filename, path);
  g_hash_table_remove (theme->filenames_by_stylesheet, stylesheet);
  cr_stylesheet_unref (stylesheet);
}

static GObject *
eek_theme_constructor (GType                  type,
                      guint                  n_construct_properties,
                      GObjectConstructParam *construct_properties)
{
  GObject *object;
  EekTheme *theme;
  CRStyleSheet *application_stylesheet;
  CRStyleSheet *theme_stylesheet;
  CRStyleSheet *default_stylesheet;

  object = (*G_OBJECT_CLASS (eek_theme_parent_class)->constructor) (type,
                                                                      n_construct_properties,
                                                                      construct_properties);
  theme = EEK_THEME (object);

  application_stylesheet = parse_stylesheet_nofail (theme->application_stylesheet);
  theme_stylesheet = parse_stylesheet_nofail (theme->theme_stylesheet);
  default_stylesheet = parse_stylesheet_nofail (theme->default_stylesheet);

  theme->cascade = cr_cascade_new (application_stylesheet,
                                   theme_stylesheet,
                                   default_stylesheet);

  if (theme->cascade == NULL)
    g_error ("Out of memory when creating cascade object");

  insert_stylesheet (theme, theme->application_stylesheet, application_stylesheet);
  insert_stylesheet (theme, theme->theme_stylesheet, theme_stylesheet);
  insert_stylesheet (theme, theme->default_stylesheet, default_stylesheet);

  return object;
}

static void
eek_theme_finalize (GObject * object)
{
  EekTheme *theme = EEK_THEME (object);

  g_slist_foreach (theme->custom_stylesheets, (GFunc) cr_stylesheet_unref, NULL);
  g_slist_free (theme->custom_stylesheets);
  theme->custom_stylesheets = NULL;

  g_hash_table_destroy (theme->stylesheets_by_filename);
  g_hash_table_destroy (theme->filenames_by_stylesheet);

  g_free (theme->application_stylesheet);
  g_free (theme->theme_stylesheet);
  g_free (theme->default_stylesheet);

  if (theme->cascade)
    {
      cr_cascade_unref (theme->cascade);
      theme->cascade = NULL;
    }

  G_OBJECT_CLASS (eek_theme_parent_class)->finalize (object);
}

static void
eek_theme_set_property (GObject      *object,
                       guint         prop_id,
                       const GValue *value,
                       GParamSpec   *pspec)
{
  EekTheme *theme = EEK_THEME (object);

  switch (prop_id)
    {
    case PROP_APPLICATION_STYLESHEET:
      {
        const char *path = g_value_get_string (value);

        if (path != theme->application_stylesheet)
          {
            g_free (theme->application_stylesheet);
            theme->application_stylesheet = g_strdup (path);
          }

        break;
      }
    case PROP_THEME_STYLESHEET:
      {
        const char *path = g_value_get_string (value);

        if (path != theme->theme_stylesheet)
          {
            g_free (theme->theme_stylesheet);
            theme->theme_stylesheet = g_strdup (path);
          }

        break;
      }
    case PROP_DEFAULT_STYLESHEET:
      {
        const char *path = g_value_get_string (value);

        if (path != theme->default_stylesheet)
          {
            g_free (theme->default_stylesheet);
            theme->default_stylesheet = g_strdup (path);
          }

        break;
      }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
eek_theme_get_property (GObject    *object,
                       guint       prop_id,
                       GValue     *value,
                       GParamSpec *pspec)
{
  EekTheme *theme = EEK_THEME (object);

  switch (prop_id)
    {
    case PROP_APPLICATION_STYLESHEET:
      g_value_set_string (value, theme->application_stylesheet);
      break;
    case PROP_THEME_STYLESHEET:
      g_value_set_string (value, theme->theme_stylesheet);
      break;
    case PROP_DEFAULT_STYLESHEET:
      g_value_set_string (value, theme->default_stylesheet);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

/**
 * eek_theme_new:
 * @application_stylesheet: The highest priority stylesheet, representing application-specific
 *   styling; this is associated with the CSS "author" stylesheet, may be %NULL
 * @theme_stylesheet: The second priority stylesheet, representing theme-specific styling ;
 *   this is associated with the CSS "user" stylesheet, may be %NULL
 * @default_stylesheet: The lowest priority stylesheet, representing global default styling;
 *   this is associated with the CSS "user agent" stylesheet, may be %NULL
 *
 * Return value: the newly created theme object
 **/
EekTheme *
eek_theme_new (const char       *application_stylesheet,
              const char       *theme_stylesheet,
              const char       *default_stylesheet)
{
    EekTheme *theme = g_object_new (EEK_TYPE_THEME,
                                    "application-stylesheet", application_stylesheet,
                                    "theme-stylesheet", theme_stylesheet,
                                    "default-stylesheet", default_stylesheet,
                                    NULL);

  return theme;
}

static gboolean
string_in_list (GString    *stryng,
                const char *list)
{
  const char *cur;

  for (cur = list; *cur;)
    {
      while (*cur && cr_utils_is_white_space (*cur))
        cur++;

      if (strncmp (cur, stryng->str, stryng->len) == 0)
        {
          cur +=  stryng->len;
          if ((!*cur) || cr_utils_is_white_space (*cur))
            return TRUE;
        }

      /*  skip to next whitespace character  */
      while (*cur && !cr_utils_is_white_space (*cur))
        cur++;
    }

  return FALSE;
}

static gboolean
pseudo_class_add_sel_matches_style (EekTheme         *a_this,
                                    CRAdditionalSel *a_add_sel,
                                    EekThemeNode     *a_node)
{
  const char *node_pseudo_class;

  g_return_val_if_fail (a_this
                        && a_add_sel
                        && a_add_sel->content.pseudo
                        && a_add_sel->content.pseudo->name
                        && a_add_sel->content.pseudo->name->stryng
                        && a_add_sel->content.pseudo->name->stryng->str
                        && a_node, FALSE);

  node_pseudo_class = eek_theme_node_get_pseudo_class (a_node);

  if (node_pseudo_class == NULL)
    return FALSE;

  return string_in_list (a_add_sel->content.pseudo->name->stryng, node_pseudo_class);
}

/**
 *@param a_add_sel the class additional selector to consider.
 *@param a_node the style node to consider.
 *@return TRUE if the class additional selector matches
 *the style node given in argument, FALSE otherwise.
 */
static gboolean
class_add_sel_matches_style (CRAdditionalSel *a_add_sel,
                             EekThemeNode     *a_node)
{
  const char *element_class;

  g_return_val_if_fail (a_add_sel
                        && a_add_sel->type == CLASS_ADD_SELECTOR
                        && a_add_sel->content.class_name
                        && a_add_sel->content.class_name->stryng
                        && a_add_sel->content.class_name->stryng->str
                        && a_node, FALSE);

  element_class = eek_theme_node_get_element_class (a_node);
  if (element_class == NULL)
    return FALSE;

  return string_in_list (a_add_sel->content.class_name->stryng, element_class);
}

/**
 *@return TRUE if the additional attribute selector matches
 *the current style node given in argument, FALSE otherwise.
 *@param a_add_sel the additional attribute selector to consider.
 *@param a_node the style node to consider.
 */
static gboolean
id_add_sel_matches_style (CRAdditionalSel *a_add_sel,
                          EekThemeNode     *a_node)
{
  gboolean result = FALSE;
  const char *id;

  g_return_val_if_fail (a_add_sel
                        && a_add_sel->type == ID_ADD_SELECTOR
                        && a_add_sel->content.id_name
                        && a_add_sel->content.id_name->stryng
                        && a_add_sel->content.id_name->stryng->str
                        && a_node, FALSE);
  g_return_val_if_fail (a_add_sel
                        && a_add_sel->type == ID_ADD_SELECTOR
                        && a_node, FALSE);

  id = eek_theme_node_get_element_id (a_node);

  if (id != NULL)
    {
      if (!strqcmp (id, a_add_sel->content.id_name->stryng->str,
                    a_add_sel->content.id_name->stryng->len))
        {
          result = TRUE;
        }
    }

  return result;
}

/**
 *additional_selector_matches_style:
 *Evaluates if a given additional selector matches an style node.
 *@param a_add_sel the additional selector to consider.
 *@param a_node the style node to consider.
 *@return TRUE is a_add_sel matches a_node, FALSE otherwise.
 */
static gboolean
additional_selector_matches_style (EekTheme         *a_this,
                                   CRAdditionalSel *a_add_sel,
                                   EekThemeNode     *a_node)
{
  CRAdditionalSel *cur_add_sel = NULL;

  g_return_val_if_fail (a_add_sel, FALSE);

  for (cur_add_sel = a_add_sel; cur_add_sel; cur_add_sel = cur_add_sel->next)
    {
      switch (cur_add_sel->type)
        {
        case NO_ADD_SELECTOR:
          return FALSE;
        case CLASS_ADD_SELECTOR:
          if (!class_add_sel_matches_style (cur_add_sel, a_node))
            return FALSE;
          break;
        case ID_ADD_SELECTOR:
          if (!id_add_sel_matches_style (cur_add_sel, a_node))
            return FALSE;
          break;
        case ATTRIBUTE_ADD_SELECTOR:
          g_warning ("Attribute selectors not supported");
          return FALSE;
        case  PSEUDO_CLASS_ADD_SELECTOR:
          if (!pseudo_class_add_sel_matches_style (a_this, cur_add_sel, a_node))
            return FALSE;
          break;
        }
    }

  return TRUE;
}

static gboolean
element_name_matches_type (const char *element_name,
                           GType       element_type)
{
  if (element_type == G_TYPE_NONE)
    {
      return strcmp (element_name, "stage") == 0;
    }
  else
    {
      GType match_type = g_type_from_name (element_name);
      if (match_type == G_TYPE_INVALID)
        return FALSE;

      return g_type_is_a (element_type, match_type);
    }
}

/**
 *Evaluate a selector (a simple selectors list) and says
 *if it matches the style node given in parameter.
 *The algorithm used here is the following:
 *Walk the combinator separated list of simple selectors backward, starting
 *from the end of the list. For each simple selector, looks if
 *if matches the current style.
 *
 *@param a_this the selection engine.
 *@param a_sel the simple selection list.
 *@param a_node the style node.
 *@param a_result out parameter. Set to true if the
 *selector matches the style node, FALSE otherwise.
 *@param a_recurse if set to TRUE, the function will walk to
 *the next simple selector (after the evaluation of the current one)
 *and recursively evaluate it. Must be usually set to TRUE unless you
 *know what you are doing.
 */
static enum CRStatus
sel_matches_style_real (EekTheme     *a_this,
                        CRSimpleSel *a_sel,
                        EekThemeNode *a_node,
                        gboolean    *a_result,
                        gboolean     a_eval_sel_list_from_end,
                        gboolean     a_recurse)
{
  CRSimpleSel *cur_sel = NULL;
  EekThemeNode *cur_node = NULL;
  GType cur_type;

  *a_result = FALSE;

  if (a_eval_sel_list_from_end)
    {
      /*go and get the last simple selector of the list */
      for (cur_sel = a_sel; cur_sel && cur_sel->next; cur_sel = cur_sel->next)
        ;
    }
  else
    {
      cur_sel = a_sel;
    }

  cur_node = a_node;
  cur_type = eek_theme_node_get_element_type (cur_node);

  while (cur_sel)
    {
      if (((cur_sel->type_mask & TYPE_SELECTOR)
           && (cur_sel->name
               && cur_sel->name->stryng
               && cur_sel->name->stryng->str)
           &&
           (element_name_matches_type (cur_sel->name->stryng->str, cur_type)))
          || (cur_sel->type_mask & UNIVERSAL_SELECTOR))
        {
          /*
           *this simple selector
           *matches the current style node
           *Let's see if the preceding
           *simple selectors also match
           *their style node counterpart.
           */
          if (cur_sel->add_sel)
            {
              if (additional_selector_matches_style (a_this, cur_sel->add_sel, cur_node))
                goto walk_a_step_in_expr;
              else
                goto done;
            }
          else
            goto walk_a_step_in_expr;
        }
      if (!(cur_sel->type_mask & TYPE_SELECTOR)
          && !(cur_sel->type_mask & UNIVERSAL_SELECTOR))
        {
          if (!cur_sel->add_sel)
            goto done;
          if (additional_selector_matches_style (a_this, cur_sel->add_sel, cur_node))
            goto walk_a_step_in_expr;
          else
            goto done;
        }
      else
        {
          goto done;
        }

    walk_a_step_in_expr:
      if (a_recurse == FALSE)
        {
          *a_result = TRUE;
          goto done;
        }

      /*
       *here, depending on the combinator of cur_sel
       *choose the axis of the element tree traversal
       *and walk one step in the element tree.
       */
      if (!cur_sel->prev)
        break;

      switch (cur_sel->combinator)
        {
        case NO_COMBINATOR:
          break;

        case COMB_WS:           /*descendant selector */
          {
            EekThemeNode *n = NULL;

            /*
             *walk the element tree upward looking for a parent
             *style that matches the preceding selector.
             */
            for (n = eek_theme_node_get_parent (a_node); n; n = eek_theme_node_get_parent (n))
              {
                enum CRStatus status;
                gboolean matches = FALSE;

                status = sel_matches_style_real (a_this, cur_sel->prev, n, &matches, FALSE, TRUE);

                if (status != CR_OK)
                  goto done;

                if (matches)
                  {
                    cur_node = n;
                    cur_type = eek_theme_node_get_element_type (cur_node);
                    break;
                  }
              }

            if (!n)
              {
                /*
                 *didn't find any ancestor that matches
                 *the previous simple selector.
                 */
                goto done;
              }
            /*
             *in this case, the preceding simple sel
             *will have been interpreted twice, which
             *is a cpu and mem waste ... I need to find
             *another way to do this. Anyway, this is
             *my first attempt to write this function and
             *I am a bit clueless.
             */
            break;
          }

        case COMB_PLUS:
          g_warning ("+ combinators are not supported");
          goto done;

        case COMB_GT:
          cur_node = eek_theme_node_get_parent (cur_node);
          if (!cur_node)
            goto done;
          cur_type = eek_theme_node_get_element_type (cur_node);
          break;

        default:
          goto done;
        }

      cur_sel = cur_sel->prev;
    }

  /*
   *if we reached this point, it means the selector matches
   *the style node.
   */
  *a_result = TRUE;

done:
  return CR_OK;
}

static void
add_matched_properties (EekTheme      *a_this,
                        CRStyleSheet *a_nodesheet,
                        EekThemeNode  *a_node,
                        GPtrArray    *props)
{
  CRStatement *cur_stmt = NULL;
  CRSelector *sel_list = NULL;
  CRSelector *cur_sel = NULL;
  gboolean matches = FALSE;
  enum CRStatus status = CR_OK;

  /*
   *walk through the list of statements and,
   *get the selectors list inside the statements that
   *contain some, and try to match our style node in these
   *selectors lists.
   */
  for (cur_stmt = a_nodesheet->statements; cur_stmt; cur_stmt = cur_stmt->next)
    {
      /*
       *initialyze the selector list in which we will
       *really perform the search.
       */
      sel_list = NULL;

      /*
       *get the the damn selector list in
       *which we have to look
       */
      switch (cur_stmt->type)
        {
        case RULESET_STMT:
          if (cur_stmt->kind.ruleset && cur_stmt->kind.ruleset->sel_list)
            {
              sel_list = cur_stmt->kind.ruleset->sel_list;
            }
          break;

        case AT_MEDIA_RULE_STMT:
          if (cur_stmt->kind.media_rule
              && cur_stmt->kind.media_rule->rulesets
              && cur_stmt->kind.media_rule->rulesets->kind.ruleset
              && cur_stmt->kind.media_rule->rulesets->kind.ruleset->sel_list)
            {
              sel_list = cur_stmt->kind.media_rule->rulesets->kind.ruleset->sel_list;
            }
          break;

        case AT_IMPORT_RULE_STMT:
          {
            CRAtImportRule *import_rule = cur_stmt->kind.import_rule;

            if (import_rule->sheet == NULL)
              {
                char *filename = NULL;

                if (import_rule->url->stryng && import_rule->url->stryng->str)
                  filename = _eek_theme_resolve_url (a_this,
                                                    a_nodesheet,
                                                    import_rule->url->stryng->str);

                if (filename)
                  import_rule->sheet = parse_stylesheet (filename, NULL);

                if (import_rule->sheet)
                  {
                    insert_stylesheet (a_this, filename, import_rule->sheet);
                    /* refcount of stylesheets starts off at zero, so we don't need to unref! */
                  }
                else
                  {
                    /* Set a marker to avoid repeatedly trying to parse a non-existent or
                     * broken stylesheet
                     */
                    import_rule->sheet = (CRStyleSheet *) - 1;
                  }

                if (filename)
                  g_free (filename);
              }

            if (import_rule->sheet != (CRStyleSheet *) - 1)
              {
                add_matched_properties (a_this, import_rule->sheet,
                                        a_node, props);
              }
          }
          break;
        default:
          break;
        }

      if (!sel_list)
        continue;

      /*
       *now, we have a comma separated selector list to look in.
       *let's walk it and try to match the style node
       *on each item of the list.
       */
      for (cur_sel = sel_list; cur_sel; cur_sel = cur_sel->next)
        {
          if (!cur_sel->simple_sel)
            continue;

          status = sel_matches_style_real (a_this, cur_sel->simple_sel, a_node, &matches, TRUE, TRUE);

          if (status == CR_OK && matches)
            {
              CRDeclaration *cur_decl = NULL;

              /* In order to sort the matching properties, we need to compute the
               * specificity of the selector that actually matched this
               * element. In a non-thread-safe fashion, we store it in the
               * ruleset. (Fixing this would mean cut-and-pasting
               * cr_simple_sel_compute_specificity(), and have no need for
               * thread-safety anyways.)
               *
               * Once we've sorted the properties, the specificity no longer
               * matters and it can be safely overriden.
               */
              cr_simple_sel_compute_specificity (cur_sel->simple_sel);

              cur_stmt->specificity = cur_sel->simple_sel->specificity;

              for (cur_decl = cur_stmt->kind.ruleset->decl_list; cur_decl; cur_decl = cur_decl->next)
                g_ptr_array_add (props, cur_decl);
            }
        }
    }
}

#define ORIGIN_AUTHOR_IMPORTANT (ORIGIN_AUTHOR + 1)
#define ORIGIN_USER_IMPORTANT   (ORIGIN_AUTHOR + 2)

static inline int
get_origin (const CRDeclaration * decl)
{
  enum CRStyleOrigin origin = decl->parent_statement->parent_sheet->origin;

  if (decl->important)
    {
      if (origin == ORIGIN_AUTHOR)
        return ORIGIN_AUTHOR_IMPORTANT;
      else if (origin == ORIGIN_USER)
        return ORIGIN_USER_IMPORTANT;
    }

  return origin;
}

/* Order of comparison is so that higher priority statements compare after
 * lower priority statements */
static int
compare_declarations (gconstpointer a,
                      gconstpointer b)
{
  /* g_ptr_array_sort() is broooken */
  CRDeclaration *decl_a = *(CRDeclaration **) a;
  CRDeclaration *decl_b = *(CRDeclaration **) b;

  int origin_a = get_origin (decl_a);
  int origin_b = get_origin (decl_b);

  if (origin_a != origin_b)
    return origin_a - origin_b;

  if (decl_a->parent_statement->specificity != decl_b->parent_statement->specificity)
    return decl_a->parent_statement->specificity - decl_b->parent_statement->specificity;

  return 0;
}

GPtrArray *
_eek_theme_get_matched_properties (EekTheme        *theme,
                                  EekThemeNode    *node)
{
  enum CRStyleOrigin origin = 0;
  CRStyleSheet *sheet = NULL;
  GPtrArray *props = g_ptr_array_new ();
  GSList *iter;

  g_return_val_if_fail (EEK_IS_THEME (theme), NULL);
  g_return_val_if_fail (EEK_IS_THEME_NODE (node), NULL);

  for (origin = ORIGIN_UA; origin < NB_ORIGINS; origin++)
    {
      sheet = cr_cascade_get_sheet (theme->cascade, origin);
      if (!sheet)
        continue;

      add_matched_properties (theme, sheet, node, props);
    }

  for (iter = theme->custom_stylesheets; iter; iter = iter->next)
    add_matched_properties (theme, iter->data, node, props);

  /* We count on a stable sort here so that later declarations come
   * after earlier declarations */
  g_ptr_array_sort (props, compare_declarations);

  return props;
}

/* Resolve an url from an url() reference in a stylesheet into an absolute
 * local filename, if possible. The resolution here is distinctly lame and
 * will fail on many examples.
 */
char *
_eek_theme_resolve_url (EekTheme      *theme,
                       CRStyleSheet *base_stylesheet,
                       const char   *url)
{
  const char *base_filename = NULL;
  char *dirname;
  char *filename;

  /* Handle absolute file:/ URLs */
  if (g_str_has_prefix (url, "file:") ||
      g_str_has_prefix (url, "File:") ||
      g_str_has_prefix (url, "FILE:"))
    {
      GError *error = NULL;
      char *filename;

      filename = g_filename_from_uri (url, NULL, &error);
      if (filename == NULL)
        {
          g_warning ("%s", error->message);
          g_error_free (error);
        }

      return NULL;
    }

  /* Guard against http:/ URLs */

  if (g_str_has_prefix (url, "http:") ||
      g_str_has_prefix (url, "Http:") ||
      g_str_has_prefix (url, "HTTP:"))
    {
      g_warning ("Http URL '%s' in theme stylesheet is not supported", url);
      return NULL;
    }

  /* Assume anything else is a relative URL, and "resolve" it
   */
  if (url[0] == '/')
    return g_strdup (url);

  base_filename = g_hash_table_lookup (theme->filenames_by_stylesheet, base_stylesheet);

  if (base_filename == NULL)
    {
      g_warning ("Can't get base to resolve url '%s'", url);
      return NULL;
    }

  dirname = g_path_get_dirname (base_filename);
  filename = g_build_filename (dirname, url, NULL);
  g_free (dirname);

  return filename;
}
