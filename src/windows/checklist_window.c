/**
 * The main checklist window, showing the the list of items with their associated checkboxes.
 */

#include "checklist_window.h"
#include "dialog_message_window.h"
#include "../checklist.h"
#include "../util.h"

static Window *s_main_window;
static MenuLayer *s_menu_layer;
static StatusBarLayer *s_status_bar;
static TextLayer *s_empty_msg_layer;

static GBitmap *s_tick_black_bitmap;
static GBitmap *s_tick_white_bitmap;
static GBitmap *s_add_bitmap_black;
static GBitmap *s_add_bitmap_white;

static DictationSession *s_dictation_session;

// Declare a buffer for the DictationSession
static char s_last_text[512];

// Buffer to hold alert message (TODO is this needed?)
static char s_deleted_msg[30];

static void draw_add_button(GContext *ctx, Layer *cell_layer) {
  GRect bounds = layer_get_bounds(cell_layer);
  GRect bitmap_bounds = gbitmap_get_bounds(s_add_bitmap_black);

  GPoint pos;
  pos.x = (bounds.size.w / 2) - (bitmap_bounds.size.w / 2);
  pos.y = (bounds.size.h / 2) - (bitmap_bounds.size.h / 2);

  graphics_context_set_compositing_mode(ctx, GCompOpSet);

  GBitmap *imageToUse = s_tick_black_bitmap;

  if(menu_cell_layer_is_highlighted(cell_layer)) {
    imageToUse = s_add_bitmap_white;
  } else {
    imageToUse = s_add_bitmap_black;
  }

  graphics_draw_bitmap_in_rect(ctx, imageToUse, GRect(pos.x, pos.y, bitmap_bounds.size.w, bitmap_bounds.size.h));
}

static void dictation_session_callback(DictationSession *session, DictationSessionStatus status,
                                       char *transcription, void *context) {

  // Print the results of a transcription attempt
  APP_LOG(APP_LOG_LEVEL_INFO, "Dictation status: %d", (int)status);

  if(status == DictationSessionStatusSuccess) {
    checklist_add_item(transcription);
    menu_layer_reload_data(s_menu_layer);
  }
}


static uint16_t get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *context) {
  if(checklist_get_num_items() == 0) {
    return 1;
  } else {
    if(checklist_get_num_items_checked() > 0) {
      return checklist_get_num_items() + 2;
    } else {
      return checklist_get_num_items() + 1;
    }
  }
}

static void draw_checkbox_cell(GContext* ctx, Layer* cell_layer, MenuIndex *cell_index) {
  // draw a checklist item
  int id = checklist_get_num_items() - (cell_index->row - 1) - 1;

  ChecklistItem* item = checklist_get_item_by_id(id);

  menu_cell_basic_draw(ctx, cell_layer, item->name, NULL, NULL);

  if(menu_cell_layer_is_highlighted(cell_layer)) {
    graphics_context_set_stroke_color(ctx, GColorWhite);
  }

  GRect bounds = layer_get_bounds(cell_layer);
  GRect bitmap_bounds = gbitmap_get_bounds(s_tick_black_bitmap);

  GBitmap *imageToUse = s_tick_black_bitmap;

  if(menu_cell_layer_is_highlighted(cell_layer)) {
    graphics_context_set_stroke_color(ctx, GColorWhite);
    imageToUse = s_tick_white_bitmap;
  }

  // Draw checkbox
  GRect r = GRect(
    bounds.size.w - (2 * CHECKLIST_WINDOW_BOX_SIZE),
    (bounds.size.h / 2) - (CHECKLIST_WINDOW_BOX_SIZE / 2),
    CHECKLIST_WINDOW_BOX_SIZE,
    CHECKLIST_WINDOW_BOX_SIZE
  );

  graphics_draw_rect(ctx, r);

  if(item->isChecked) {
    // draw the checkmark
    graphics_context_set_compositing_mode(ctx, GCompOpSet);
    graphics_draw_bitmap_in_rect(ctx, imageToUse, GRect(r.origin.x, r.origin.y - 3, bitmap_bounds.size.w, bitmap_bounds.size.h));

    // draw text strikethrough
    graphics_context_set_stroke_width(ctx, 2);
    GSize size = graphics_text_layout_get_content_size(item->name,
                                                       fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                                                       bounds,
                                                       GTextOverflowModeTrailingEllipsis,
                                                       PBL_IF_ROUND_ELSE(GTextAlignmentCenter, GTextAlignmentLeft));

    // draw centered for round, left-aligned for rect
    #ifdef PBL_ROUND
      graphics_draw_line(ctx,
                         GPoint((bounds.size.w / 2) - (size.w / 2), bounds.size.h / 2 ),
                         GPoint((bounds.size.w / 2) + (size.w / 2), bounds.size.h / 2 ));
    #else
      graphics_draw_line(ctx,
                         GPoint(5, bounds.size.h / 2 ),
                         GPoint(5 + size.w, bounds.size.h / 2 ));
    #endif
  }
}

static void draw_row_callback(GContext *ctx, Layer *cell_layer, MenuIndex *cell_index, void *context) {
  layer_set_hidden(text_layer_get_layer(s_empty_msg_layer), (checklist_get_num_items() != 0));

  if(cell_index->row == 0) {
    // draw the add action
    draw_add_button(ctx, cell_layer);
  } else if(cell_index->row == checklist_get_num_items() + 1) {
    // draw the clear action
    menu_cell_basic_draw(ctx, cell_layer, "Clear completed", NULL, NULL);
  } else {
    // draw the checkbox
    draw_checkbox_cell(ctx, cell_layer, cell_index);
  }
}

static int16_t get_cell_height_callback(struct MenuLayer *menu_layer, MenuIndex *cell_index, void *callback_context) {
  return CHECKLIST_CELL_HEIGHT;
  // #ifdef PBL_ROUND
  //   return menu_layer_menu_index_selected(menu_layer, cell_index) ?
  //     FOCUSED_TALL_CELL_HEIGHT : UNFOCUSED_TALL_CELL_HEIGHT;
  // #else
  //   return CHECKBOX_WINDOW_CELL_HEIGHT;
  // #endif
}

static void select_callback(struct MenuLayer *menu_layer, MenuIndex *cell_index, void *callback_context) {
  if(cell_index->row == 0) {
    // the first row is always the "add" button
    dictation_session_start(s_dictation_session);
  } else if(cell_index->row == checklist_get_num_items() + 1) {
    // the last row is always the "clear completed" button
    int num_deleted = checklist_delete_completed_items();

    // generate and display "items deleted" message
    snprintf(s_deleted_msg,
             sizeof(s_deleted_msg),
             ((num_deleted == 1) ? "%i Item Deleted" : "%i Items Deleted"),
             num_deleted);

    dialog_message_window_push(s_deleted_msg);
    menu_layer_reload_data(menu_layer);

  } else {
    // if the item is a checklist item, toggle its checked state
    // get the id number of the checklist item to delete
    int id = checklist_get_num_items() - (cell_index->row - 1) - 1;

    checklist_item_toggle_checked(id);

    menu_layer_reload_data(menu_layer);
  }
}

static void window_load(Window *window) {
  checklist_init();

  Layer *window_layer = window_get_root_layer(window);
  GRect windowBounds = layer_get_bounds(window_layer);;

  #ifdef PBL_ROUND
    GRect bounds = layer_get_bounds(window_layer);
  #else
    GRect bounds = GRect(0, STATUS_BAR_LAYER_HEIGHT, windowBounds.size.w, windowBounds.size.h - STATUS_BAR_LAYER_HEIGHT);
  #endif

  s_tick_black_bitmap = gbitmap_create_with_resource(RESOURCE_ID_TICK_BLACK);
  s_tick_white_bitmap = gbitmap_create_with_resource(RESOURCE_ID_TICK_WHITE);
  s_add_bitmap_black = gbitmap_create_with_resource(RESOURCE_ID_ADD_BLACK);
  s_add_bitmap_white = gbitmap_create_with_resource(RESOURCE_ID_ADD_WHITE);

  s_menu_layer = menu_layer_create(bounds);
  menu_layer_set_click_config_onto_window(s_menu_layer, window);
  menu_layer_set_center_focused(s_menu_layer, PBL_IF_ROUND_ELSE(true, false));
  menu_layer_set_callbacks(s_menu_layer, NULL, (MenuLayerCallbacks) {
      .get_num_rows = (MenuLayerGetNumberOfRowsInSectionsCallback)get_num_rows_callback,
      .draw_row = (MenuLayerDrawRowCallback)draw_row_callback,
      .get_cell_height = (MenuLayerGetCellHeightCallback)get_cell_height_callback,
      .select_click = (MenuLayerSelectCallback)select_callback,
  });

  window_set_background_color(window, GColorYellow);
  menu_layer_set_normal_colors(s_menu_layer, GColorYellow, GColorBlack);
  menu_layer_set_highlight_colors(s_menu_layer, GColorArmyGreen, GColorWhite);

  layer_add_child(window_layer, menu_layer_get_layer(s_menu_layer));

  s_status_bar = status_bar_layer_create();
  layer_add_child(window_layer, status_bar_layer_get_layer(s_status_bar));

  status_bar_layer_set_colors(s_status_bar, GColorYellow, GColorBlack);

  // Create dictation session
  s_dictation_session = dictation_session_create(sizeof(s_last_text),
                                                 dictation_session_callback, NULL);

  s_empty_msg_layer = text_layer_create(PBL_IF_ROUND_ELSE(
    GRect(0, bounds.size.h / 2 + 40, bounds.size.w, bounds.size.h),
    GRect(0, bounds.size.h / 2 + 25, bounds.size.w, bounds.size.h)
  ));

  text_layer_set_text(s_empty_msg_layer, "No items");
  text_layer_set_background_color(s_empty_msg_layer, GColorClear);
  text_layer_set_text_alignment(s_empty_msg_layer, GTextAlignmentCenter);
  text_layer_set_font(s_empty_msg_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  layer_add_child(window_layer, text_layer_get_layer(s_empty_msg_layer));

}

static void window_unload(Window *window) {
  checklist_deinit();

  menu_layer_destroy(s_menu_layer);

  gbitmap_destroy(s_tick_black_bitmap);
  gbitmap_destroy(s_tick_white_bitmap);
  gbitmap_destroy(s_add_bitmap_black);
  gbitmap_destroy(s_add_bitmap_white);

  window_destroy(window);
  s_main_window = NULL;
}

void checklist_window_push() {
  if(!s_main_window) {
    s_main_window = window_create();
    window_set_window_handlers(s_main_window, (WindowHandlers) {
        .load = window_load,
        .unload = window_unload,
    });
  }
  window_stack_push(s_main_window, true);
}
