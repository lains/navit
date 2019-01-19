/**
 * Navit, a modular navigation system.
 * Copyright (C) 2005-2013 Navit Team
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 */

#include <stdlib.h>
#include <gtk/gtk.h>
#include "gui_gtk_poi.h"
#include "popup.h"
#include "debug.h"
#include "navit_nls.h"
#include "coord.h"
#include "point.h"
#include "callback.h"
#include "graphics.h"
#include "navit.h"
#include "item.h"
#include "map.h"
#include "mapset.h"
#include "transform.h"
#include "attr.h"
#include "util.h"

#include "navigation.h"         /* for FEET_PER_METER and other conversion factors. */

static struct gtk_poi_search {
    GtkWidget *entry_distance;
    GtkWidget *label_distance;
    GtkWidget *treeview_cat;
    GtkWidget *treeview_poi;
    GtkWidget *button_visit, *button_destination, *button_map;
    GtkListStore *store_poi;
    GtkListStore *store_cat;
    GtkTreeModel *store_poi_sorted;
    GtkTreeModel *store_cat_sorted;
    char *selected_cat;
    struct navit *nav;
} gtk_poi_search;

static GdkPixbuf *geticon(const char *name) {
    GdkPixbuf *icon=NULL;
    GError *error=NULL;
    icon=gdk_pixbuf_new_from_file(graphics_icon_path(name),&error);
    if (error) {
        dbg(lvl_error, "failed to load icon '%s': %s", name, error->message);
    }
    return icon;
}

/** Build the category list model with icons. */
static GtkTreeModel *category_list_model(struct gtk_poi_search *search) {
    GtkTreeIter iter;
    gtk_list_store_append(search->store_cat, &iter);
    gtk_list_store_set(search->store_cat, &iter, 0,geticon("pharmacy.png"), 1, _("Pharmacy"), 2, "poi_pharmacy", -1);
    gtk_list_store_append(search->store_cat, &iter);
    gtk_list_store_set(search->store_cat, &iter, 0, geticon("restaurant.png"), 1, _("Restaurant"), 2, "poi_restaurant", -1);
    gtk_list_store_append(search->store_cat, &iter);
    gtk_list_store_set(search->store_cat, &iter,0, geticon("restaurant.png"), 1, _("Restaurant. Fast food"), 2,
                       "poi_fastfood", -1);
    gtk_list_store_append(search->store_cat, &iter);
    gtk_list_store_set(search->store_cat, &iter,0, geticon("hotel.png"), 1, _("Hotel"), 2, "poi_hotel", -1);
    gtk_list_store_append(search->store_cat, &iter);
    gtk_list_store_set(search->store_cat, &iter,0, geticon("parking.png"), 1, _("Car parking"), 2, "poi_car_parking", -1);
    gtk_list_store_append(search->store_cat, &iter);
    gtk_list_store_set(search->store_cat, &iter,0, geticon("fuel.png"), 1, _("Fuel station"), 2, "poi_fuel", -1);
    gtk_list_store_append(search->store_cat, &iter);
    gtk_list_store_set(search->store_cat, &iter,0, geticon("bank.png"), 1, _("Bank"), 2, "poi_bank", -1);
    gtk_list_store_append(search->store_cat, &iter);
    gtk_list_store_set(search->store_cat, &iter,0, geticon("hospital.png"), 1, _("Hospital"), 2, "poi_hospital", -1);
    gtk_list_store_append(search->store_cat, &iter);
    gtk_list_store_set(search->store_cat, &iter,0, geticon("cinema.png"), 1, _("Cinema"), 2, "poi_cinema", -1);
    gtk_list_store_append(search->store_cat, &iter);
    gtk_list_store_set(search->store_cat, &iter,0, geticon("rail_station.png"), 1, _("Train station"), 2,
                       "poi_rail_station", -1);
    gtk_list_store_append(search->store_cat, &iter);
    gtk_list_store_set(search->store_cat, &iter,0, geticon("school.png"), 1, _("School"), 2, "poi_school", -1);
    gtk_list_store_append(search->store_cat, &iter);
    gtk_list_store_set(search->store_cat, &iter,0, geticon("police.png"), 1, _("Police"), 2, "poi_police", -1);
    gtk_list_store_append(search->store_cat, &iter);
    gtk_list_store_set(search->store_cat, &iter,0, geticon("justice.png"), 1, _("Justice"), 2, "poi_justice", -1);
    gtk_list_store_append(search->store_cat, &iter);
    gtk_list_store_set(search->store_cat, &iter,0, geticon("taxi.png"), 1, _("Taxi"), 2, "poi_taxi", -1);
    gtk_list_store_append(search->store_cat, &iter);
    gtk_list_store_set(search->store_cat, &iter,0, geticon("shopping.png"), 1, _("Shopping"), 2, "poi_shopping", -1);
    return GTK_TREE_MODEL (search->store_cat_sorted);
}

/**
 * @brief A search result structure containing items resulting from a search around a reference point. It also contains a pointer to the closest result.
 *
 * This struct is the output format of function map_search_item_results_alloc()
 */
struct item_search_results {
    GList *list;	/*!< A list containing search results. Search results (of type struct item_search_entry) are the data part of this list */
    struct item_search_entry *closest;	/*!< A pointer to the closest result from the search reference */
};

/**
 * @brief A search result item created during a search around a reference point.
 */
struct item_search_entry {
    struct item item;	/*!< A (deep) copy of the result item */
    int dist;	/*!< The distance to the search reference */
    struct pcoord coord;	/*!< The coordinates of the item (altogether with the associated projection) */
    char *label;	/*!< Label for this item (if any, or NULL otherwise) */
};

/**
 * @brief Search items of a map, around a specific point
 *
 * @param navit The navit instance
 * @param pc The coordinates of the reference point around which to perform the search
 * @param search_distance The maximum distance (in meters) to include results
 *
 * @return A pointer to a search result structure containing the list of results. This structure will have to be deallocated by the caller via a call to map_search_item_results_free()
 */
struct item_search_results *map_search_item_results_alloc(struct navit *navit, struct pcoord *pc, int search_distance) {

    GList *list = NULL;
    struct map_selection *sel,*selm;
    struct coord coord_item;
    struct mapset_handle *h;
    struct map *m;
    struct map_rect *mr;
    struct item *item;
    struct coord c;
    int idist; /* idist is the distance in meters from the search reference (center of the screen) to a POI. */
    struct item_search_entry *result_item;
    struct item_search_entry *closest_result = NULL;
    int shortest_dist = INT_MAX;
    struct item_search_results *result; /* The structure we will return */

    c.x = pc->x;
    c.y = pc->y;

    sel=map_selection_rect_new(pc,search_distance*transform_scale(abs(pc->y)+search_distance*100*1.5),18);

    h=mapset_open(navit_get_mapset(navit));

    enum item_type selected_lo_range, selected_hi_range; //FIXME: this should be passed as parameter

    /* TODO: use struct item_range here */
    selected_lo_range=item_from_name("town_label_1e3");
    selected_hi_range=item_from_name("town_label") |
                      0xff;	/* Consider all items up to 255 (in practice, there are labels much less town_label_* items, but we provision for future expansion */
    while ((m=mapset_next(h, 1))) {
        selm=map_selection_dup_pro(sel, projection_mg, map_projection(m));
        mr=map_rect_new(m, selm);
        if (mr) {
            while ((item=map_rect_get_item(mr))) {
                struct attr label_attr;
                item_attr_get(item,attr_label,&label_attr);
                if (item->type>=selected_lo_range && item->type<=selected_hi_range) {
                    if (item_coord_get(item,&coord_item,1) == 1) {
                        idist=transform_distance(projection_mg,&c,&coord_item);
                        //dbg(lvl_error, "At distance %d, found POI type @@%s@@", idist, item_to_name(item->type));
                        if (idist<=search_distance) {
                            result_item = g_new0(struct item_search_entry, 1);
                            result_item->item = *item;	/* Copy the item */
                            result_item->coord.pro = map_projection(m);
                            result_item->coord.x = coord_item.x;
                            result_item->coord.y = coord_item.y;
                            result_item->dist=idist;
                            if (idist < shortest_dist) { /* Keep track of closest result (from the search reference) */
                                shortest_dist = idist;
                                closest_result = result_item;
                            }
                            if (label_attr.type==attr_label) {
                                result_item->label = g_strdup(label_attr.u.str);
                                //dbg(lvl_error, "with label \"%s\"", closest_label?closest_label:"NULL");*/
                            }
                            list = g_list_prepend(list, result_item);
                        }
                    }
                }
            }
            map_rect_destroy(mr);
        }
        map_selection_destroy(selm);
    }
    map_selection_destroy(sel);
    mapset_close(h);

    result = g_new0(struct item_search_results, 1);

    result->closest = closest_result;
    result->list = list;
    return result;
}

/**
 * @brief Deallocate a search result structure previously created by map_search_item_results_alloc
 *
 * @param search_results A pointer to the structure to deallocate. After a call to this function, @p search_results will point to unallocated memory and should not be used anymore
 */
void map_search_item_results_free(struct item_search_results *search_results) {
    GList *p;
    struct item_search_entry *result_item;


    /* We will deallocate all result_items, possibly their labels (strings) and finally the GList contained inside earch_results and finally the structure search_results itself */
    if (search_results) {
        if (search_results->list) {
            /* Parse the GList starting at list and free all payloads before freeing the list itself */
            for(p=search_results->list; p; p=g_list_next(p)) {
                result_item = p->data;
                if (result_item) {
                    if (result_item->label) {
                        free(result_item->label);
                    }
                    free(result_item);
                }
            }
            g_list_free(search_results->list);
        }
        free(search_results);
    }
}

/**
 * @brief Get the name of the closest town compared to a specific point
 *
 * @param navit The navit instance
 * @param pc The coordinates of the reference point around which to perform the search
 *
 * @return A string containing the town name, or NULL if none was found. If non-NULL, it is up to the caller to free this string using g_free()
 */
char *get_town_name_around(struct navit *navit, struct pcoord *pc) {
    int search_distance_kilometers;
    enum item_type selected_lo_range, selected_hi_range;
    char *closest_label = NULL;

    for (search_distance_kilometers=1; search_distance_kilometers < 512; search_distance_kilometers<<=1) {
        dbg(lvl_error, "Searching for town within %dkm", search_distance_kilometers);
        struct item_search_results *results = map_search_item_results_alloc(navit, pc, search_distance_kilometers * 1000);
        if (results && results->closest) {
            if (results->closest->label) {
                dbg(lvl_error, "Found a town within searched area");
                closest_label = g_strdup(results->closest->label);
                map_search_item_results_free(results);
                break;
            }
        }
        map_search_item_results_free(results);
        dbg(lvl_error, "Did not find a town within searched area");
    }
    return closest_label;
}

/**
 * @brief Construct the list of POIs matching with the requested search
 *
 * This function will apply the search parameters contained in @p search to to the map information in order to extract matching POI results.
 * Specifically, it will search all POIs of category @p search->selected_cat that are at most at distance search->entry_distance of the current center of the map display.
 * Results will be sorted in @p search->store_poi
 *
 * @param search Search parameters to apply
 *
 * @return The sorted search results
 */
static GtkTreeModel *model_poi (struct gtk_poi_search *search) {
    GtkTreeIter iter;
    struct map_selection *sel,*selm;
    struct coord coord_item,center;
    struct pcoord pc;
    struct mapset_handle *h;
    int search_distance_meters; /* distance to search the POI database, in meters, from the center of the screen. */
    int idist; /* idist is the distance in meters from the search reference (center of the screen) to a POI. */
    struct map *m;
    struct map_rect *mr;
    struct item *item;
    struct point cursor_position;
    enum item_type selected;

    /* Respect the Imperial attribute as we enlighten the user. */
    struct attr attr;
    int imperial = FALSE;  /* default to using metric measures. */
    if (navit_get_attr(gtk_poi_search.nav, attr_imperial, &attr, NULL))
        imperial=attr.u.num;

    if (imperial == FALSE) {
        /* Input is in kilometers */
        search_distance_meters=1000*atoi((char *) gtk_entry_get_text(GTK_ENTRY(search->entry_distance)));
        gtk_label_set_text(GTK_LABEL(search->label_distance),_("Select a search radius from screen center in km"));
    } else {
        /* Input is in miles. */
        search_distance_meters=atoi((char *) gtk_entry_get_text(GTK_ENTRY(search->entry_distance)))/METERS_TO_MILES;
        gtk_label_set_text(GTK_LABEL(search->label_distance),_("Select a search radius from screen center in miles"));
    }

    cursor_position.x=navit_get_width(search->nav)/2;
    cursor_position.y=navit_get_height(search->nav)/2;

    transform_reverse(navit_get_trans(search->nav), &cursor_position, &center);
    pc.pro = transform_get_projection(navit_get_trans(search->nav));
    pc.x = center.x;
    pc.y = center.y;

    //Search in the map, for pois
    sel=map_selection_rect_new(&pc,search_distance_meters*transform_scale(abs(pc.y)+search_distance_meters*1.5),18);
    gtk_list_store_clear(search->store_poi);

    h=mapset_open(navit_get_mapset(search->nav));

    selected=item_from_name(search->selected_cat);
    while ((m=mapset_next(h, 1))) {
        selm=map_selection_dup_pro(sel, projection_mg, map_projection(m));
        mr=map_rect_new(m, selm);
        if (mr) {
            while ((item=map_rect_get_item(mr))) {
                struct attr label_attr;
                item_attr_get(item,attr_label,&label_attr);
                item_coord_get(item,&coord_item,1);
                idist=transform_distance(projection_mg,&center,&coord_item);
                if (item->type==selected && idist<=search_distance_meters) {
                    char direction[5];
                    gtk_list_store_append(search->store_poi, &iter);
                    get_compass_direction(direction,transform_get_angle_delta(&center,&coord_item,0),1);

                    /**
                     * If the user has selected imperial, translate idist from meters to
                     * feet. We convert to feet only, and not miles, because the code
                     * sorts on the numeric value of the distance, so it doesn't like two
                     * different units. Currently, the distance is an int. Can it be made
                     * a float? Possible future enhancement?
                     */
                    if (imperial != FALSE) {
                        idist = idist * (FEET_PER_METER); /* convert meters to feet. */
                    }

                    gtk_list_store_set(search->store_poi, &iter, 0,direction, 1,idist,
                                       2,g_strdup(label_attr.u.str), 3,coord_item.x, 4,coord_item.y,-1);
                }
            }
            map_rect_destroy(mr);
        }
        map_selection_destroy(selm);
    }
    map_selection_destroy(sel);
    mapset_close(h);

    char *closest_town_label = get_town_name_around(search->nav, &pc);
    dbg(lvl_error, "Closest town is %s", closest_town_label?closest_town_label:"unknown");
    return GTK_TREE_MODEL (search->store_poi_sorted);
}

/** Enable button if there is a selected row. */
static void treeview_poi_changed(GtkWidget *widget, struct gtk_poi_search *search) {
    GtkTreePath *path;
    GtkTreeViewColumn *focus_column;
    GtkTreeIter iter;

    gtk_tree_view_get_cursor(GTK_TREE_VIEW(search->treeview_cat), &path, &focus_column);
    if(!path) return;
    if(!gtk_tree_model_get_iter(GTK_TREE_MODEL(search->store_cat_sorted), &iter, path)) return;

    gtk_widget_set_sensitive(search->button_visit,TRUE);
    gtk_widget_set_sensitive(search->button_map,TRUE);
    gtk_widget_set_sensitive(search->button_destination,TRUE);
}

/** Reload the POI list and disable buttons. */
static void treeview_poi_reload(GtkWidget *widget, struct gtk_poi_search *search) {
    GtkTreePath *path;
    GtkTreeViewColumn *focus_column;
    GtkTreeIter iter;

    gtk_widget_set_sensitive(search->button_visit,FALSE);
    gtk_widget_set_sensitive(search->button_map,FALSE);
    gtk_widget_set_sensitive(search->button_destination,FALSE);

    gtk_tree_view_get_cursor(GTK_TREE_VIEW(search->treeview_cat), &path, &focus_column);
    if(!path) return;
    if(!gtk_tree_model_get_iter(GTK_TREE_MODEL(search->store_cat_sorted), &iter, path)) return;
    gtk_tree_model_get(GTK_TREE_MODEL(search->store_cat_sorted), &iter, 2, &search->selected_cat, -1);
    gtk_tree_view_set_model(GTK_TREE_VIEW (search->treeview_poi), model_poi(search));
}

/** Set the selected POI as destination. */
static void button_destination_clicked(GtkWidget *widget, struct gtk_poi_search *search) {
    GtkTreePath *path;
    GtkTreeViewColumn *focus_column;
    GtkTreeIter iter;
    long int lat, lon;
    char *label;
    char *category;
    char buffer[2000];

    //Get category
    gtk_tree_view_get_cursor(GTK_TREE_VIEW(search->treeview_cat), &path, &focus_column);
    if(!path) return;
    if(!gtk_tree_model_get_iter(GTK_TREE_MODEL(search->store_cat_sorted), &iter, path)) return;
    gtk_tree_model_get(GTK_TREE_MODEL(search->store_cat_sorted), &iter, 1, &category, -1);

    //Get label, lat, lon
    gtk_tree_view_get_cursor(GTK_TREE_VIEW(search->treeview_poi), &path, &focus_column);
    if(!path) return;
    if(!gtk_tree_model_get_iter(GTK_TREE_MODEL(search->store_poi_sorted), &iter, path)) return;
    gtk_tree_model_get(GTK_TREE_MODEL(search->store_poi_sorted), &iter, 2, &label, -1);
    gtk_tree_model_get(GTK_TREE_MODEL(search->store_poi_sorted), &iter, 3, &lat, -1);
    gtk_tree_model_get(GTK_TREE_MODEL(search->store_poi_sorted), &iter, 4, &lon, -1);
    sprintf(buffer, _("POI %s. %s"), category, label);

    struct pcoord dest;
    dest.x=lat;
    dest.y=lon;
    dest.pro=1;
    navit_set_destination(search->nav, &dest, buffer, 1);
    dbg(lvl_debug,_("Set destination to %ld, %ld "),lat,lon);
}

/* Show the POI's position in the map. */
static void button_map_clicked(GtkWidget *widget, struct gtk_poi_search *search) {
    GtkTreePath *path;
    GtkTreeViewColumn *focus_column;
    GtkTreeIter iter;
    long int lat,lon;

    gtk_tree_view_get_cursor(GTK_TREE_VIEW(search->treeview_poi), &path, &focus_column);
    if(!path) return;
    if(!gtk_tree_model_get_iter(GTK_TREE_MODEL(search->store_poi_sorted), &iter, path)) return;
    gtk_tree_model_get(GTK_TREE_MODEL(search->store_poi_sorted), &iter, 3, &lat, -1);
    gtk_tree_model_get(GTK_TREE_MODEL(search->store_poi_sorted), &iter, 4, &lon, -1);

    struct pcoord dest;
    dest.x=lat;
    dest.y=lon;
    dest.pro=1;
    navit_set_center(search->nav, &dest,1);
    dbg(lvl_debug,_("Set map to %ld, %ld "),lat,lon);
}

/** Set POI as the first "visit before". */
static void button_visit_clicked(GtkWidget *widget, struct gtk_poi_search *search) {
    GtkTreePath *path;
    GtkTreeViewColumn *focus_column;
    GtkTreeIter iter;
    long int lat,lon;

    gtk_tree_view_get_cursor(GTK_TREE_VIEW(search->treeview_poi), &path, &focus_column);
    if(!path) return;
    if(!gtk_tree_model_get_iter(GTK_TREE_MODEL(search->store_poi_sorted), &iter, path)) return;
    gtk_tree_model_get(GTK_TREE_MODEL(search->store_poi_sorted), &iter, 3, &lat, -1);
    gtk_tree_model_get(GTK_TREE_MODEL(search->store_poi_sorted), &iter, 4, &lon, -1);
    dbg(lvl_debug,_("Set next visit to %ld, %ld "),lat,lon);

    struct pcoord dest;
    dest.x=lat;
    dest.y=lon;
    dest.pro=1;
    popup_set_visitbefore(search->nav,&dest,0);
}

/** Create UI and connect objects to functions. */
void gtk_gui_poi(struct navit *nav) {
    GtkWidget *window2,*vbox, *keyboard, *table;
    GtkWidget *label_category, *label_poi;
    GtkWidget *listbox_cat, *listbox_poi;
    GtkCellRenderer *renderer;

    struct gtk_poi_search *search=&gtk_poi_search;
    search->nav=nav;

    window2 = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window2),_("POI search"));
    gtk_window_set_wmclass (GTK_WINDOW (window2), "navit", "Navit");
    gtk_window_set_default_size (GTK_WINDOW (window2),700,550);
    vbox = gtk_vbox_new(FALSE, 0);
    table = gtk_table_new(4, 4, FALSE);

    label_category = gtk_label_new(_("Select a category"));
    label_poi=gtk_label_new(_("Select a POI"));

    /* Respect the Imperial attribute as we enlighten the user. */
    struct attr attr;
    int imperial = FALSE;  /* default to using metric measures. */
    if (navit_get_attr(gtk_poi_search.nav, attr_imperial, &attr, NULL))
        imperial=attr.u.num;

    if (imperial == FALSE) {
        /* Input is in kilometers */
        search->label_distance = gtk_label_new(_("Select a search radius from screen center in km"));
    } else {
        /* Input is in miles. */
        search->label_distance = gtk_label_new(_("Select a search radius from screen center in miles"));
    }

    search->entry_distance=gtk_entry_new_with_max_length(2);
    gtk_entry_set_text(GTK_ENTRY(search->entry_distance),"10");

    search->treeview_cat=gtk_tree_view_new();
    listbox_cat = gtk_scrolled_window_new (NULL, NULL);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (listbox_cat), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(listbox_cat),search->treeview_cat);
    search->store_cat = gtk_list_store_new (3, GDK_TYPE_PIXBUF, G_TYPE_STRING, G_TYPE_STRING);
    renderer=gtk_cell_renderer_pixbuf_new();
    gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (search->treeview_cat),-1, _(" "), renderer, "pixbuf", 0,
            NULL);
    renderer=gtk_cell_renderer_text_new();
    gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (search->treeview_cat),-1, _("Category"), renderer, "text",
            1, NULL);
    search->store_cat_sorted=gtk_tree_model_sort_new_with_model(GTK_TREE_MODEL(search->store_cat));
    gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(search->store_cat_sorted),1,GTK_SORT_ASCENDING);
    gtk_tree_view_set_model (GTK_TREE_VIEW (search->treeview_cat), category_list_model(search));

    search->treeview_poi=gtk_tree_view_new();
    listbox_poi = gtk_scrolled_window_new (NULL, NULL);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (listbox_poi), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(listbox_poi),search->treeview_poi);
    search->store_poi = gtk_list_store_new (5, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_LONG, G_TYPE_LONG);
    renderer=gtk_cell_renderer_text_new();
    gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (search->treeview_poi),-1, _("Direction"), renderer, "text",
            0,NULL);
    renderer=gtk_cell_renderer_text_new();
    gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (search->treeview_poi),-1, _("Distance"), renderer, "text",
            1, NULL);
    renderer=gtk_cell_renderer_text_new();
    gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (search->treeview_poi),-1, _("Name"), renderer, "text", 2,
            NULL);
    search->store_poi_sorted=gtk_tree_model_sort_new_with_model(GTK_TREE_MODEL(search->store_poi));
    gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(search->store_poi_sorted),1,GTK_SORT_ASCENDING);

    search->button_visit = gtk_button_new_with_label(_("Visit Before"));
    search->button_destination = gtk_button_new_with_label(_("Destination"));
    search->button_map = gtk_button_new_with_label(_("Map"));
    gtk_widget_set_sensitive(search->button_visit,FALSE);
    gtk_widget_set_sensitive(search->button_map,FALSE);
    gtk_widget_set_sensitive(search->button_destination,FALSE);

    gtk_table_attach(GTK_TABLE(table), search->label_distance,      0, 1, 0, 1,  0, GTK_FILL, 0, 0);
    gtk_table_attach(GTK_TABLE(table), search->entry_distance,     1, 2, 0, 1,  0, GTK_FILL, 0, 0);
    gtk_table_attach(GTK_TABLE(table), label_category,     0, 1, 2, 3,  0, GTK_FILL, 0, 0);
    gtk_table_attach(GTK_TABLE(table), listbox_cat,        0, 1, 3, 4,  GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 0, 0);
    gtk_table_attach(GTK_TABLE(table), label_poi,          1, 4, 2, 3,  0, GTK_FILL, 0, 0);
    gtk_table_attach(GTK_TABLE(table), listbox_poi,        1, 4, 3, 4,  GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 0, 0);
    gtk_table_attach(GTK_TABLE(table), search->button_map,         0, 1, 4, 5,  GTK_FILL, GTK_FILL, 0, 0);
    gtk_table_attach(GTK_TABLE(table), search->button_visit,       1, 2, 4, 5,  GTK_FILL, GTK_FILL, 0, 0);
    gtk_table_attach(GTK_TABLE(table), search->button_destination, 2, 3, 4, 5,  GTK_FILL, GTK_FILL, 0, 0);
    gtk_box_pack_start(GTK_BOX(vbox), table, TRUE, TRUE, 0);

    g_signal_connect(G_OBJECT(search->entry_distance), "changed", G_CALLBACK(treeview_poi_reload), search);
    g_signal_connect(G_OBJECT(search->button_visit), "clicked", G_CALLBACK(button_visit_clicked), search);
    g_signal_connect(G_OBJECT(search->button_map), "clicked", G_CALLBACK(button_map_clicked), search);
    g_signal_connect(G_OBJECT(search->button_destination), "clicked", G_CALLBACK(button_destination_clicked), search);
    g_signal_connect(G_OBJECT(search->treeview_cat), "cursor_changed", G_CALLBACK(treeview_poi_reload), search);
    g_signal_connect(G_OBJECT(search->treeview_poi), "cursor_changed", G_CALLBACK(treeview_poi_changed), search);

    keyboard=gtk_socket_new();
    gtk_box_pack_end(GTK_BOX(vbox), keyboard, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(window2), vbox);
    gtk_widget_show_all(window2);
}

