#include <odroid_system.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>

#include "lupng.h"
#include "gui.h"
#include "gw_lcd.h"
#include "rg_i18n.h"
#include "bitmaps.h"

#if !defined(COVERFLOW)
#define COVERFLOW 0
#endif /* COVERFLOW */

#define IMAGE_LOGO_WIDTH (47)
#define IMAGE_LOGO_HEIGHT (51)
#define IMAGE_BANNER_WIDTH (ODROID_SCREEN_WIDTH)
#define IMAGE_BANNER_HEIGHT (32)
#define STATUS_HEIGHT (33)
#define HEADER_HEIGHT (47)

#define CRC_WIDTH (104)
#define CRC_X_OFFSET (ODROID_SCREEN_WIDTH - CRC_WIDTH)
#define CRC_Y_OFFSET (STATUS_HEIGHT)

#define LIST_WIDTH (ODROID_SCREEN_WIDTH)
#define LIST_HEIGHT (ODROID_SCREEN_HEIGHT - STATUS_HEIGHT - HEADER_HEIGHT)
#define LIST_LINE_HEIGHT (odroid_overlay_get_font_size() + 2)
#define LIST_LINE_COUNT (LIST_HEIGHT / LIST_LINE_HEIGHT)
#define LIST_X_OFFSET (0)
#define LIST_Y_OFFSET (STATUS_HEIGHT)

#define COVER_MAX_HEIGHT (100)
#define COVER_MAX_WIDTH (186)

#ifdef COVERFLOW
/* instances for JPEG decoder */
#include "hw_jpeg_decoder.h"

// reuse existing buffer from gw_lcd.h
#define JPEG_BUFFER_SIZE ((uint32_t)sizeof(emulator_framebuffer) - 4)

#define NOCOVER_HEIGHT ((uint32_t)(68))
#define NOCOVER_WIDTH ((uint32_t)(68))
#define COVER_420_SIZE ((uint32_t)(COVER_MAX_HEIGHT * COVER_MAX_WIDTH * 3 / 2))
#define COVER_16BITS_SIZE ((uint32_t)(COVER_MAX_HEIGHT * COVER_MAX_WIDTH * 2))

static uint8_t *pJPEG_Buffer = NULL;
static uint16_t *pCover_Buffer = NULL;
static uint32_t current_cover_width = NOCOVER_WIDTH;
static uint32_t current_cover_height = NOCOVER_HEIGHT;
const static uint32_t COVER_BORDER = 6;

const uint8_t cover_light[5] = {60, 120, 255, 120, 60};
const uint8_t cover_light3[3] = {255, 120, 60};
#endif

theme_t gui_themes[] = {
    {0, C_GRAY, C_WHITE, C_AQUA},
    {0, C_GRAY, C_GREEN, C_AQUA},
    {0, C_WHITE, C_GREEN, C_AQUA},

    {5, C_GRAY, C_WHITE, C_AQUA},
    {5, C_GRAY, C_GREEN, C_AQUA},
    {5, C_WHITE, C_GREEN, C_AQUA},

    {11, C_GRAY, C_WHITE, C_AQUA},
    {11, C_GRAY, C_GREEN, C_AQUA},
    {11, C_WHITE, C_GREEN, C_AQUA},

    {16, C_GRAY, C_WHITE, C_AQUA},
    {16, C_GRAY, C_GREEN, C_AQUA},
    {16, C_WHITE, C_GREEN, C_AQUA},
};
int gui_themes_count = 12;

static char str_buffer[128];

retro_gui_t gui;

void gui_event(gui_event_t event, tab_t *tab)
{
    if (tab->event_handler)
        (*tab->event_handler)(event, tab);
}

tab_t *gui_add_tab(const char *name, const void *logo, const void *header, void *arg, void *event_handler)
{
    tab_t *tab = rg_calloc(1, sizeof(tab_t));

    sprintf(tab->name, "%s", name);
    sprintf(tab->status, "Loading...");

    tab->event_handler = event_handler;
    tab->img_header = header;
    tab->img_logo = logo ?: (void *)tab;
    tab->initialized = false;
    tab->is_empty = false;
    tab->arg = arg;

    gui.tabs[gui.tabcount++] = tab;

    printf("gui_add_tab: Tab '%s' added at index %d\n", tab->name, gui.tabcount - 1);

    return tab;
}

void gui_init_tab(tab_t *tab)
{
    if (tab->initialized)
        return;

    tab->initialized = true;
    // tab->status[0] = 0;

    /* setup JPEG decoder instance with 32bits aligned address */
    // reuse emulator buffer for JPEG decoder & DMA2 buffering
    // Direct access to DTCM is not allowed for DMA2D :(
    pJPEG_Buffer = (uint8_t *)((uint32_t)&emulator_framebuffer + 4 - ((uint32_t)&emulator_framebuffer) % 4);
    pCover_Buffer = (uint16_t *)(pJPEG_Buffer + COVER_420_SIZE + 4 - COVER_420_SIZE % 4);
    assert(JPEG_DecodeToBufferInit((uint32_t)pJPEG_Buffer, JPEG_BUFFER_SIZE) == 0);
    assert((COVER_420_SIZE + COVER_16BITS_SIZE + 12) <= sizeof(emulator_framebuffer));
    //printf("JPEG init done\n");
    /* -------------------------- */

    sprintf(str_buffer, "Sel.%.11s", tab->name);
    // tab->listbox.cursor = odroid_settings_int32_get(str_buffer, 0);
    tab_t *selected_tab = gui_get_tab(odroid_settings_MainMenuSelectedTab_get());
    if (tab->name == selected_tab->name)
    {
        tab->listbox.cursor = odroid_settings_MainMenuCursor_get();
    }

    gui_event(TAB_INIT, tab);

    tab->listbox.cursor = MIN(tab->listbox.cursor, tab->listbox.length - 1);
    tab->listbox.cursor = MAX(tab->listbox.cursor, 0);
}

tab_t *gui_get_tab(int index)
{
    return (index >= 0 && index < gui.tabcount) ? gui.tabs[index] : NULL;
}

tab_t *gui_get_current_tab()
{
    return gui_get_tab(gui.selected);
}

tab_t *gui_set_current_tab(int index)
{
    index %= gui.tabcount;

    if (index < 0)
        index += gui.tabcount;

    gui.selected = index;

    return gui_get_tab(gui.selected);
}

void gui_save_current_tab()
{
    tab_t *tab = gui_get_current_tab();

    sprintf(str_buffer, "Sel.%.11s", tab->name);
    // odroid_settings_int32_set(str_buffer, tab->listbox.cursor);
    odroid_settings_MainMenuCursor_set(tab->listbox.cursor);
    // odroid_settings_int32_set("SelectedTab", gui.selected);
    odroid_settings_MainMenuSelectedTab_set(gui.selected);
    odroid_settings_commit();
}

listbox_item_t *gui_get_selected_item(tab_t *tab)
{
    listbox_t *list = &tab->listbox;

    if (list->cursor >= 0 && list->cursor < list->length)
        return &list->items[list->cursor];

    return NULL;
}

static int list_comparator(const void *p, const void *q)
{
    return strcasecmp(((listbox_item_t *)p)->text, ((listbox_item_t *)q)->text);
}

void gui_sort_list(tab_t *tab, int sort_mode)
{
    if (tab->listbox.length == 0)
        return;

    qsort((void *)tab->listbox.items, tab->listbox.length, sizeof(listbox_item_t), list_comparator);
}

void gui_resize_list(tab_t *tab, int new_size)
{
    int cur_size = tab->listbox.length;

    if (new_size == cur_size)
        return;

    if (new_size == 0)
    {
        rg_free(tab->listbox.items);
        tab->listbox.items = NULL;
    }
    else
    {
        tab->listbox.items = rg_realloc(tab->listbox.items, new_size * sizeof(listbox_item_t));
        for (int i = cur_size; i < new_size; i++)
            memset(&tab->listbox.items[i], 0, sizeof(listbox_item_t));
    }

    tab->listbox.length = new_size;
    tab->listbox.cursor = MIN(tab->listbox.cursor, tab->listbox.length - 1);
    tab->listbox.cursor = MAX(tab->listbox.cursor, 0);

    printf("gui_resize_list: Resized list '%s' from %d to %d items\n", tab->name, cur_size, new_size);
}

void gui_scroll_list(tab_t *tab, scroll_mode_t mode)
{
    listbox_t *list = &tab->listbox;

    if (list->length == 0 || list->cursor > list->length)
    {
        return;
    }

    int cur_cursor = list->cursor;
    int old_cursor = list->cursor;

    if (mode == LINE_UP)
    {
        cur_cursor--;
    }
    else if (mode == LINE_DOWN)
    {
        cur_cursor++;
    }
    else if (mode == PAGE_UP)
    {
        char st = ((char *)list->items[cur_cursor].text)[0];
        int max = LIST_LINE_COUNT - 2;
        while (--cur_cursor > 0 && max-- > 0)
        {
            if (st != ((char *)list->items[cur_cursor].text)[0])
                break;
        }
    }
    else if (mode == PAGE_DOWN)
    {
        char st = ((char *)list->items[cur_cursor].text)[0];
        int max = LIST_LINE_COUNT - 2;
        while (++cur_cursor < list->length - 1 && max-- > 0)
        {
            if (st != ((char *)list->items[cur_cursor].text)[0])
                break;
        }
    }

    if (cur_cursor < 0)
        cur_cursor = list->length - 1;
    if (cur_cursor >= list->length)
        cur_cursor = 0;

    list->cursor = cur_cursor;

    if (cur_cursor != old_cursor)
    {
        gui_draw_notice(" ", C_BLACK);
        gui_draw_list(tab);
        gui_event(TAB_SCROLL, tab);
    }
}

void gui_redraw()
{
    tab_t *tab = gui_get_current_tab();
    gui_draw_header(tab);
    gui_draw_status(tab);
    gui_draw_list(tab);
    gui_event(TAB_REDRAW, tab);

    lcd_swap();
}

void gui_draw_navbar()
{
    for (int i = 0; i < gui.tabcount; i++)
    {
        odroid_display_write(i * IMAGE_LOGO_WIDTH, 0, IMAGE_LOGO_WIDTH, IMAGE_LOGO_HEIGHT, gui.tabs[i]->img_logo);
    }
}

void gui_draw_header(tab_t *tab)
{
    if (tab->img_header)
        odroid_display_write(0, ODROID_SCREEN_HEIGHT - IMAGE_BANNER_HEIGHT - 15, IMAGE_BANNER_WIDTH, IMAGE_BANNER_HEIGHT, tab->img_header);

    odroid_overlay_draw_fill_rect(0, ODROID_SCREEN_HEIGHT - 15, ODROID_SCREEN_WIDTH, 1, C_GW_YELLOW);
    odroid_overlay_draw_fill_rect(0, ODROID_SCREEN_HEIGHT - 13, ODROID_SCREEN_WIDTH, 4, C_GW_RED);
    odroid_overlay_draw_fill_rect(0, ODROID_SCREEN_HEIGHT - 10, ODROID_SCREEN_WIDTH, 2, C_BLACK);
    odroid_overlay_draw_fill_rect(0, ODROID_SCREEN_HEIGHT - 8, ODROID_SCREEN_WIDTH, 2, C_GW_RED);
    odroid_overlay_draw_fill_rect(0, ODROID_SCREEN_HEIGHT - 6, ODROID_SCREEN_WIDTH, 2, C_BLACK);
    odroid_overlay_draw_fill_rect(0, ODROID_SCREEN_HEIGHT - 4, ODROID_SCREEN_WIDTH, 1, C_GW_RED);
    odroid_overlay_draw_fill_rect(0, ODROID_SCREEN_HEIGHT - 3, ODROID_SCREEN_WIDTH, 2, C_BLACK);
    odroid_overlay_draw_fill_rect(0, ODROID_SCREEN_HEIGHT - 1, ODROID_SCREEN_WIDTH, 1, C_GW_RED);
}

// void gui_draw_notice(tab_t *tab)
void gui_draw_notice(const char *text, uint16_t color)
{
    odroid_overlay_draw_text(CRC_X_OFFSET, CRC_Y_OFFSET, CRC_WIDTH, text, color, C_BLACK);
}

void gui_draw_status(tab_t *tab)
{
    odroid_overlay_draw_fill_rect(0, 0, ODROID_SCREEN_WIDTH, STATUS_HEIGHT, C_GW_RED);
    odroid_overlay_draw_fill_rect(0, 1, ODROID_SCREEN_WIDTH, 2, C_BLACK);
    odroid_overlay_draw_fill_rect(0, 4, ODROID_SCREEN_WIDTH, 2, C_BLACK);
    odroid_overlay_draw_fill_rect(0, 8, ODROID_SCREEN_WIDTH, 2, C_BLACK);

    odroid_overlay_draw_logo(8, 16, C_GW_YELLOW);

    /*
    if (tab->img_header)
    {
        int max_len = (ODROID_SCREEN_WIDTH - 12) / odroid_overlay_get_local_font_width();
        odroid_overlay_draw_local_text_line(
            6,
            16,
            max_len * odroid_overlay_get_local_font_width(),
            tab->status,
            C_GW_YELLOW,
            C_GW_RED,
            NULL,
            0);
    }
    else
    {
        int max_len = (ODROID_SCREEN_WIDTH - 12) / odroid_overlay_get_font_width();
        odroid_overlay_draw_text_line(
            6,
            18,
            max_len * odroid_overlay_get_font_width(),
            tab->status,
            C_GW_YELLOW,
            C_GW_RED);
    }
    */
    odroid_overlay_draw_battery(ODROID_SCREEN_WIDTH - 32, 17);
}

listbox_item_t *gui_get_selected_prior_item(tab_t *tab)
{
    listbox_t *list = &tab->listbox;

    int x = list->cursor - 1;
    if (x < 0)
        x = list->length - 1;

    if (x >= 0 && x < list->length)
        return &list->items[x];

    return NULL;
}

listbox_item_t *gui_get_selected_next_item(tab_t *tab)
{
    listbox_t *list = &tab->listbox;

    int x = list->cursor + 1;
    if (x >= list->length)
        x = 0;

    if (x >= 0 && x < list->length)
        return &list->items[x];

    return NULL;
}

listbox_item_t *gui_get_item_by_index(tab_t *tab, int *index)
{
    listbox_t *list = &tab->listbox;
    int x = *index;

    if (x < 0)
        x = (list->length + x) % (list->length);

    if (x >= list->length)
        x = x % (list->length);

    if (x >= 0 && x < list->length)
    {
        *index = x;
        return &list->items[x];
    }
    return NULL;
}

void gui_draw_item_postion_v(int posx, int starty, int endy, int cur, int size)
{
    sprintf(str_buffer, "%d", size);
    int len = strlen(str_buffer);
    sprintf(str_buffer, "%0*d/%0*d", len, cur, len, size);
    len = strlen(str_buffer);
    int height = len * odroid_overlay_get_font_size();
    uint16_t *dst_img = lcd_get_active_buffer();
    int posy = (cur * (endy - starty + 1 - height)) / (size + 1);
    //posy = (posy < 0) ? 0 : posy;

    for (int y = starty; y <= starty + posy; y++)
        dst_img[y * ODROID_SCREEN_WIDTH + posx] = get_darken_pixel(C_GW_YELLOW, ((y - starty + 1) * 90) / posy + 10);
    for (int y = posy + starty + height; y <= endy; y++)
        dst_img[y * ODROID_SCREEN_WIDTH + posx] = get_darken_pixel(C_GW_YELLOW, ((endy - y + 1) * 90) / (endy - starty - posy - height + 1) + 10);

    odroid_overlay_draw_fill_rect(
        posx - odroid_overlay_get_font_width() / 2 - 1,
        starty + posy - 1,
        odroid_overlay_get_font_size() + 2, height + 2, C_GW_YELLOW);
    odroid_overlay_draw_fill_rect(
        posx - odroid_overlay_get_font_width() / 2,
        starty + posy - 2,
        odroid_overlay_get_font_width(), 1, C_GW_OPAQUE_YELLOW);
    odroid_overlay_draw_fill_rect(
        posx - odroid_overlay_get_font_width() / 2,
        starty + posy + height + 1,
        odroid_overlay_get_font_width(), 1, C_GW_OPAQUE_YELLOW);

    for (int y = 0; y < len; y++)
        odroid_overlay_draw_text_line(
            posx - odroid_overlay_get_font_width() / 2,
            starty + posy + y * odroid_overlay_get_font_size(), //top
            odroid_overlay_get_font_width(),
            &str_buffer[y],
            C_BLACK,
            C_GW_YELLOW);
}

void gui_draw_simple_list(int posx, tab_t *tab)
{
    listbox_t *list = &tab->listbox;
    if (list->cursor >= 0 && list->cursor < list->length)
    {
        int font_height = odroid_overlay_get_local_font_size();
        int w = (ODROID_SCREEN_WIDTH - posx - 10) / odroid_overlay_get_local_font_width();
        w = w * odroid_overlay_get_local_font_width();
        listbox_item_t *item = &list->items[list->cursor];
        int h1 = LIST_Y_OFFSET + (LIST_HEIGHT - font_height) / 2;
        if (item)
            odroid_overlay_draw_local_text_line(posx, h1, w, list->items[list->cursor].text, C_GW_YELLOW, C_BLACK, NULL, 0);

        int index_next = list->cursor + 1;
        int index_proior = list->cursor - 1;
        int max_line = (LIST_HEIGHT - font_height) / font_height / 2;
        int h2 = h1;
        h1++;
        for (int i = 0; i < max_line; i++)
        {
            listbox_item_t *next_item = gui_get_item_by_index(tab, &index_next);
            h1 = h1 + font_height + max_line - i;
            h2 = h2 - font_height - max_line + i;
            if (h2 < LIST_Y_OFFSET) //out range;
                break;
            if (next_item)
                odroid_overlay_draw_local_text_line(
                    posx,
                    h1,
                    w,
                    list->items[index_next].text,
                    get_darken_pixel(C_GW_OPAQUE_YELLOW, (max_line - i) * 100 / max_line),
                    C_BLACK,
                    NULL,
                    0);
            index_next++;
            listbox_item_t *prior_item = gui_get_item_by_index(tab, &index_proior);
            if (prior_item)
                odroid_overlay_draw_local_text_line(
                    posx,
                    h2,
                    w,
                    list->items[index_proior].text,
                    get_darken_pixel(C_GW_OPAQUE_YELLOW, (max_line - i) * 100 / max_line),
                    C_BLACK,
                    NULL,
                    0);
            index_proior--;
        }
        //draw currpostion
        gui_draw_item_postion_v(ODROID_SCREEN_WIDTH - 5, LIST_Y_OFFSET + 4, LIST_Y_OFFSET + LIST_HEIGHT - 4, list->cursor + 1, list->length);
    }
}

#if COVERFLOW != 0

static void draw_centered_local_text_line(uint16_t y_pos,
                                          const char *text,
                                          uint16_t x1,
                                          uint16_t x2,
                                          uint16_t color,
                                          uint16_t color_bg)
{
    int width = strlen(text) * odroid_overlay_get_local_font_width();
    int x_pos = (x2 - x1) / 2 - width / 2;
    if (x_pos < 0)
        x_pos = 0;
    if (width > (x2 - x1))
        width = x2 - x1;

    odroid_overlay_draw_local_text_line(x_pos + x1, y_pos, width, text, color, color_bg, NULL, 0);
}

void gui_draw_item_postion_h(int posy, int startx, int endx, int cur, int size)
{
    sprintf(str_buffer, "%d", size);
    int len = strlen(str_buffer);
    sprintf(str_buffer, "%0*d/%0*d", len, cur, len, size);
    len = strlen(str_buffer);
    int width = len * odroid_overlay_get_font_width();
    uint16_t *dst_img = lcd_get_active_buffer();
    int posx = (cur * (endx - startx + 1 - width)) / (size + 1);

    for (int x = startx; x <= startx + posx; x++)
    {
        dst_img[(posy - 1) * ODROID_SCREEN_WIDTH + x] = get_darken_pixel(C_GW_OPAQUE_YELLOW, 100 - ((x - startx + 1) * 90) / posx);
        dst_img[(posy - 2) * ODROID_SCREEN_WIDTH + x] = get_darken_pixel(C_GW_YELLOW, 100 - ((x - startx + 1) * 90) / posx);
    }
    for (int x = posx + startx + width; x <= endx; x++)
    {
        dst_img[(posy - 1) * ODROID_SCREEN_WIDTH + x] = get_darken_pixel(C_GW_OPAQUE_YELLOW, 100 - ((endx - x + 1) * 90) / (endx - startx - posx - width + 1));
        dst_img[(posy - 2) * ODROID_SCREEN_WIDTH + x] = get_darken_pixel(C_GW_YELLOW, 100 - ((endx - x + 1) * 90) / (endx - startx - posx - width + 1));
    }

    odroid_overlay_draw_text_line(
        posx + startx,
        posy - odroid_overlay_get_font_size(), //top
        width,
        str_buffer,
        C_GW_YELLOW,
        C_BLACK);
}


static void gui_get_cover_size(retro_emulator_file_t *file, uint32_t *cov_width, uint32_t *cov_height)
{
    uint32_t jpeg_cov_width = 0, jpeg_cov_height = 0;

    *cov_width = NOCOVER_WIDTH;
    *cov_height = NOCOVER_HEIGHT;

    if (file == NULL)
        return;

    if (file->img_size != 0)
    {
        if (JPEG_DecodeGetSize((uint32_t)(file->img_address), &jpeg_cov_width, &jpeg_cov_height) == 0)
        {
            *cov_width = jpeg_cov_width;
            *cov_height = jpeg_cov_height;
        }
    }
}


void gui_draw_coverlight_h(retro_emulator_file_t *file, int cover_position)
{
    int32_t cover_x = 0, cover_y = 55;
    uint32_t cover_width = NOCOVER_WIDTH;
    uint32_t cover_height = NOCOVER_HEIGHT;

    static uint32_t nocover_width = NOCOVER_WIDTH;
    static uint32_t nocover_height = NOCOVER_HEIGHT;

    if (file == NULL)
        return;

    if ((file->img_size) != 0)
    {
        JPEG_DecodeToBuffer((uint32_t)(file->img_address), (uint32_t)pCover_Buffer, &cover_width, &cover_height, cover_light[cover_position + 2]);
        if (nocover_width > cover_width)
            nocover_width = cover_width;
        if (nocover_height > cover_height)
            nocover_height = cover_height;
    }
    else
    {
        cover_width = nocover_width;
        cover_height = nocover_height;
    }

    switch (cover_position)
    {
    // TOP LEFT Cover
    case 2:
    {
        cover_x = 0;
        cover_y += -20;
    }
    break;
    // MIDDLE LEFT Cover
    case 1:
    {
        cover_x = (GW_LCD_WIDTH - current_cover_width) / 2 - COVER_BORDER - (cover_width + 2 * COVER_BORDER) + 16;
        if (cover_x < 12)
            cover_x = 12;
        cover_y += (COVER_MAX_HEIGHT - cover_height) / 2 - 10;
    }
    break;
    // Current Cover
    case 0:
    {
        cover_x = (GW_LCD_WIDTH - cover_width) / 2 - COVER_BORDER;
        cover_y += (COVER_MAX_HEIGHT - cover_height) / 2;
    }
    break;
    // MIDDLE RIGHT Cover
    case -1:
    {
        cover_x = (GW_LCD_WIDTH + current_cover_width) / 2 + COVER_BORDER - 16;
        if ((cover_x + cover_width + 12) > GW_LCD_WIDTH)
            cover_x = GW_LCD_WIDTH - cover_width - 24;
        cover_y += (COVER_MAX_HEIGHT - cover_height) / 2 - 10;
    }
    break;
    // TOP RIGHT cover
    case -2:
    {
        cover_x = GW_LCD_WIDTH - cover_width - 2 * COVER_BORDER;
        cover_y += -20;
    }
    break;
    }

    /* no cover art, draw a grey box */
    if ((file->img_size) == 0)
        odroid_overlay_draw_fill_rect(cover_x + COVER_BORDER, cover_y + COVER_BORDER, cover_width, cover_height, get_darken_pixel(C_GRAY, 100 * cover_light[cover_position + 2] / 255));

    /* display the cover art */
    else
        odroid_display_write_rect(cover_x + COVER_BORDER, cover_y + COVER_BORDER, cover_width, cover_height, cover_width, pCover_Buffer);

    /* add decoration around the cover art */
    /* current cover */
    if (cover_position == 0)
    {
        odroid_overlay_draw_rect(cover_x, cover_y, cover_width + 2 * COVER_BORDER, cover_height + 2 * COVER_BORDER, COVER_BORDER, C_BLACK);
        odroid_overlay_draw_rect(2 + cover_x, 2 + cover_y, cover_width + 8, cover_height + 8, 2, C_GW_YELLOW);
        
        /* TODO add shadowing */
        //left side
        /*
        uint16_t *pix = lcd_get_active_buffer();
        int pix_pos=0;

        for ( int xs = cover_x -12 ; xs < cover_x; xs++)
            for ( int ys = cover_y-6 ; ys < cover_y+cover_height+6; ys++)
            {
                pix_pos= ys*GW_LCD_WIDTH + xs;
                pix[pix_pos] = get_darken_pixel(pix[pix_pos], 100 - 80*(xs - cover_x + 12)/12);                
            }

         for ( int xs = cover_x+cover_width+2*COVER_BORDER + 12 ; xs < cover_x+cover_width+2*COVER_BORDER; xs--)
            for ( int ys = cover_y-6 ; ys < cover_y+cover_height+6; ys++)
            {
                pix_pos= ys*GW_LCD_WIDTH + xs;
                pix[pix_pos] = get_darken_pixel(pix[pix_pos], 100 - 80*(xs - cover_x-cover_width-2*COVER_BORDER)/12);                
            }

            }

        for ( int xs = cover_x ; xs < cover_x+cover_width-12; xs++)
            for ( int ys = cover_y-6 ; ys < cover_y; ys++)
            {
                pix_pos= ys*GW_LCD_WIDTH + xs;
                pix[pix_pos] = get_darken_pixel(pix[pix_pos], 100 - 80*(ys - cover_y+6)/6);                
            } 
*/

        /* add game titleof the current cover art */
        sprintf(str_buffer, "%s", file->name);
        draw_centered_local_text_line(169,
                                      str_buffer,
                                      0,
                                      ODROID_SCREEN_WIDTH,
                                      C_GW_YELLOW,
                                      C_BLACK);
    }
    /* other cover */
    else
    {
        odroid_overlay_draw_rect(cover_x + 4, cover_y + 4, cover_width + 4, cover_height + 4, 2, C_BLACK);
        odroid_overlay_draw_rect(5 + cover_x, 5 + cover_y, cover_width + 2, cover_height + 2, 1, get_darken_pixel(C_GW_YELLOW, 100 * cover_light[cover_position + 2] / 255));
    }
}

void gui_draw_coverlight_v(retro_emulator_file_t *file, int cover_position)
{
    int32_t cover_x = 0, cover_y = 0;
    uint32_t cover_width = NOCOVER_WIDTH;
    uint32_t cover_height = NOCOVER_HEIGHT;

    static uint32_t nocover_width = NOCOVER_WIDTH;
    static uint32_t nocover_height = NOCOVER_HEIGHT;

    if (file == NULL)
        return;

    if (file->img_size != 0)
    {
        JPEG_DecodeToBuffer((uint32_t)(file->img_address), (uint32_t)pCover_Buffer, &cover_width, &cover_height, cover_light3[cover_position]);
        if (nocover_width > cover_width)
            nocover_width = cover_width;
        if (nocover_height > cover_height)
            nocover_height = cover_height;
    }
    else
    {
        cover_width = nocover_width;
        cover_height = nocover_height;
    }

    switch (cover_position)
    {
    // upper
    case 2:
    {
        cover_x = 0;
        cover_y = STATUS_HEIGHT;
    }
    break;
    // middle
    case 1:
    {
        cover_x = 8; //16;
        cover_y = (GW_LCD_HEIGHT - HEADER_HEIGHT - current_cover_height - 2 * COVER_BORDER + STATUS_HEIGHT) / 2;
    }
    break;

    // current cover
    case 0:
    {
        cover_x = 16; //32;
        cover_y = GW_LCD_HEIGHT - HEADER_HEIGHT - cover_height - 2 * COVER_BORDER;
    }
    break;
    }

    /* draw cover art or grey box */
    if ((file->img_size) == 0)
        odroid_overlay_draw_fill_rect(cover_x + COVER_BORDER, cover_y + COVER_BORDER, cover_width, cover_height, get_darken_pixel(C_GRAY, 100 * cover_light3[cover_position] / 255));

    /* display the cover art */
    else
        odroid_display_write_rect(cover_x + COVER_BORDER, cover_y + COVER_BORDER, cover_width, cover_height, cover_width, pCover_Buffer);

    /* add decoration around the cover art */
    /* current cover */
    if (cover_position == 0)
    {
        odroid_overlay_draw_rect(cover_x, cover_y, cover_width + 2 * COVER_BORDER, cover_height + 2 * COVER_BORDER, COVER_BORDER, C_BLACK);
        odroid_overlay_draw_rect(2 + cover_x, 2 + cover_y, cover_width + 8, cover_height + 8, 2, C_GW_YELLOW);
    }
    /* other cover */
    else
    {
        odroid_overlay_draw_rect(cover_x + 4, cover_y + 4, cover_width + 4, cover_height + 4, 2, C_BLACK);
        odroid_overlay_draw_rect(5 + cover_x, 5 + cover_y, cover_width + 2, cover_height + 2, 1, get_darken_pixel(C_GW_YELLOW, 100 * cover_light3[cover_position] / 255));
    }
}

void gui_draw_coverflow_h(tab_t *tab) //------------
{
    retro_emulator_t *emu = (retro_emulator_t *)tab->arg;
    int font_height = odroid_overlay_get_local_font_size();
    int font_width = odroid_overlay_get_local_font_width();
    int cover_height = emu->cover_height;
    int cover_width = emu->cover_width;
    int r_width1 = cover_width * 5 / 8;
    int r_width2 = cover_width * 7 / 8;
    uint32_t jpeg_cover_width = cover_width;
    uint32_t jpeg_cover_height = cover_height;
    //left _|_1_|_2__||_m_||__2_|_1_|_ min 22 pixels space
    int space_width = 22;
    int p_width2 = (ODROID_SCREEN_WIDTH - cover_width - space_width) / 3;
    //p_width must big than 1;
    //space width than real width, draw full size;  7/8
    p_width2 = (p_width2 > r_width2) ? r_width2 : p_width2;
    int p_width1 = (ODROID_SCREEN_WIDTH - cover_width - space_width - p_width2 * 2) / 2;
    //space width than real width, draw full size;  5/8
    p_width1 = (p_width1 > r_width1) ? r_width1 : p_width1;
    int start_xpos = (ODROID_SCREEN_WIDTH - ((p_width2 + p_width1) * 2 + cover_width + space_width)) / 2;
    //fisrt left point pos getted, get fisrt top point;
    int p_height1 = cover_height * 5 / 8;
    int p_height2 = cover_height * 7 / 8;

    int v_space = LIST_HEIGHT - (cover_height + 6);
    uint8_t draw_bot_title = v_space > (font_height + 5 + 8) ? 1 : 0; //(12 = 3 * 4)
    int cover_top = 0;
    int top_tit_pos = 0;
    int bot_tit_pos = 0;
    if (draw_bot_title == 1)
    {
        cover_top = STATUS_HEIGHT + (v_space - font_height - 5) * 2 / 5 + 8;
        top_tit_pos = cover_top + cover_height / 8 / 4 * 3 - font_height - 4;  
    }
    else
        cover_top = STATUS_HEIGHT + (v_space - 5) / 2 + 8; //

    bot_tit_pos = cover_top + cover_height + (v_space - font_height - 5) / 5 + 3;
    //let's start draw effect;
    uint16_t *dst_img = lcd_get_active_buffer();

    //left1
    odroid_overlay_draw_rect(start_xpos + 1, cover_top + (cover_height - p_height1) / 4 * 3 - 2, p_width1 + 2, p_height1 + 4, 1, get_darken_pixel(C_GW_YELLOW, 40));
    odroid_overlay_draw_fill_rect(start_xpos + p_width1 + 2, cover_top + (cover_height - p_height1) / 4 * 3 - 1, 1, p_height1 + 2, C_BLACK);
    odroid_overlay_draw_rect(start_xpos + p_width1 + 4, cover_top + (cover_height - p_height2) / 4 * 3 - 2, p_width2 + 2, p_height2 + 4, 1, get_darken_pixel(C_GW_YELLOW, 80));
    odroid_overlay_draw_fill_rect(start_xpos + p_width1 + p_width2 + 5, cover_top + (cover_height - p_height2) / 4 * 3 - 1, 1, p_height2 + 2, C_BLACK);

    odroid_overlay_draw_rect(start_xpos + p_width1 + p_width2 + 8, cover_top - 3, cover_width + 6, cover_height + 6, 1, C_GW_YELLOW);
    odroid_overlay_draw_rect(start_xpos + p_width1 + p_width2 + 9, cover_top - 2, cover_width + 4, cover_height + 4, 1, C_GW_OPAQUE_YELLOW);

    odroid_overlay_draw_rect(start_xpos + p_width1 + p_width2 + cover_width + 16, cover_top + (cover_height - p_height2) / 4 * 3 - 2, p_width2 + 2, p_height2 + 4, 1, get_darken_pixel(C_GW_YELLOW, 80));
    odroid_overlay_draw_fill_rect(start_xpos + p_width1 + p_width2 + cover_width + 16, cover_top + (cover_height - p_height2) / 4 * 3 - 1, 1, p_height2 + 2, C_BLACK);
    odroid_overlay_draw_rect(start_xpos + p_width1 + p_width2 * 2 + cover_width + 20, cover_top + (cover_height - p_height1) / 4 * 3 - 2, p_width1 + 2, p_height1 + 4, 1, get_darken_pixel(C_GW_YELLOW, 40));
    odroid_overlay_draw_fill_rect(start_xpos + p_width1 + p_width2 * 2 + cover_width + 20, cover_top + (cover_height - p_height1) / 4 * 3 - 1, 1, p_height1 + 2, C_BLACK);

    listbox_t *list = &tab->listbox;
    if (list->cursor >= 0 && list->cursor < list->length)
        gui_draw_item_postion_h(cover_top - 1, start_xpos + p_width1 + p_width2 + 10, start_xpos + p_width1 + p_width2 + cover_width + 6, list->cursor + 1, list->length);
    else
        return;

    listbox_item_t *item = &list->items[list->cursor];
    retro_emulator_file_t *file = NULL;
    if (item) //current page
    {
        file = (retro_emulator_file_t *)item->arg;
        if (file->img_size == 0)
        {
            draw_centered_local_text_line(cover_top + (cover_height - font_height) / 2, s_No_Cover, start_xpos + p_width1 + p_width2 + 10, start_xpos + p_width1 + p_width2 + 10 + cover_width, get_darken_pixel(C_GW_RED, 80), C_BLACK);
            if (!draw_bot_title)
            {
                sprintf(str_buffer, "%s", file->name);
                size_t len = strlen(str_buffer);
                size_t width = len * font_width;
                width = width > cover_width ? cover_width : width;
                width = (width / font_width) * font_width;
                odroid_overlay_draw_local_text(start_xpos + p_width1 + p_width2 + 11, cover_top + 4, width, str_buffer, C_GW_YELLOW, C_BLACK, 0);
            }
        }
        else
        {
            //draw the cover cenver
            JPEG_DecodeToBuffer((uint32_t)(file->img_address), (uint32_t)pCover_Buffer, &jpeg_cover_width, &jpeg_cover_height, 255);
            odroid_display_write_rect(start_xpos + p_width1 + p_width2 + 11, cover_top, cover_width, cover_height, cover_width, pCover_Buffer);
        }
        if (draw_bot_title)
        {
            sprintf(str_buffer, "%s", file->name);
            size_t len = strlen(str_buffer);
            size_t max_len = (ODROID_SCREEN_WIDTH - 24) / font_width;
            if (len > max_len)
                len = max_len;
            size_t width = len * font_width;
            odroid_overlay_draw_local_text_line((ODROID_SCREEN_WIDTH - width) / 2, bot_tit_pos, width, str_buffer, C_GW_YELLOW, C_BLACK, NULL, 0);
        }
    }
    int index = list->cursor + 1;
    item = gui_get_item_by_index(tab, &index);
    if (item)
    {
        file = (retro_emulator_file_t *)item->arg;
        if (file->img_size == 0)
        {
            draw_centered_local_text_line(cover_top + (cover_height - p_height2) / 4 * 3 + (p_height2 - font_height) / 2, s_No_Cover,
                                          start_xpos + p_width1 + p_width2 + cover_width + 17,
                                          start_xpos + p_width1 + p_width2 * 2 + cover_width + 17, get_darken_pixel(C_GW_OPAQUE_YELLOW, 80), C_BLACK);
            if ((!draw_bot_title) && (p_width2 > (font_width * 4)))
            {
                sprintf(str_buffer, "%s", file->name);
                size_t len = strlen(str_buffer);
                size_t width = len * font_width;
                width = width > p_width2 ? p_width2 : width;
                width = width / font_width * font_width;
                odroid_overlay_draw_local_text(start_xpos + p_width1 + p_width2 + cover_width + 17, cover_top + (cover_height - p_height2) / 4 * 3 + 4, width, str_buffer, C_GW_OPAQUE_YELLOW, C_BLACK, 0);
            }
        }
        else
        {
            JPEG_DecodeToBuffer((uint32_t)(file->img_address), (uint32_t)pCover_Buffer, &jpeg_cover_width, &jpeg_cover_height, 255);
            for (int y = 0; y < p_height2; y++)
                for (int x = 0; x < p_width2; x++)
                    dst_img[(y + cover_top + (cover_height - p_height2) / 4 * 3) * ODROID_SCREEN_WIDTH + start_xpos + p_width1 + p_width2 + cover_width + 16 + x] =
                        get_darken_pixel(pCover_Buffer[(y + y / 8) * cover_width + ((r_width2 - p_width2) + x) * 8 / 7], 40 + x * 30 / p_width2);
        };
        if (draw_bot_title)
        {
            sprintf(str_buffer, "%s", file->name);
            size_t max_len = (p_width2 + p_width1 + 4) / font_width;
            size_t width = strlen(str_buffer) * font_width;
            if (width > (p_width2 + p_width1 + 4))
                width = max_len * font_width;
            odroid_overlay_draw_local_text_line(start_xpos + p_width1 + p_width2 + cover_width + 17, top_tit_pos, width, str_buffer, C_GW_OPAQUE_YELLOW, C_BLACK, NULL, 0);
        };
    };

    index = list->cursor - 1;
    item = gui_get_item_by_index(tab, &index);
    if (item)
    {
        file = (retro_emulator_file_t *)item->arg;
        if (file->img_size == 0)
        {
            draw_centered_local_text_line(cover_top + (cover_height - p_height2) / 4 * 3 + (p_height2 - font_height) / 2, s_No_Cover,
                                          start_xpos + p_width1 + 5,
                                          start_xpos + p_width1 + p_width2 + 5, get_darken_pixel(C_GW_OPAQUE_YELLOW, 80), C_BLACK);
            if ((!draw_bot_title) && (p_width2 > odroid_overlay_get_local_font_width() * 4))
            {
                sprintf(str_buffer, "%s", file->name);
                size_t len = strlen(str_buffer);
                size_t width = len * font_width;
                width = width > p_width2 ? p_width2 : width;
                width = width / font_width * font_width;
                odroid_overlay_draw_local_text(start_xpos + p_width1 + 5, cover_top + (cover_height - p_height2) / 4 * 3 + 4, width, str_buffer, C_GW_OPAQUE_YELLOW, C_BLACK, 0);
            }
        }
        else
        {

            JPEG_DecodeToBuffer((uint32_t)(file->img_address), (uint32_t)pCover_Buffer, &jpeg_cover_width, &jpeg_cover_height, 255);
            for (int y = 0; y < p_height2; y++)
                for (int x = 0; x < p_width2; x++)
                    dst_img[(y + cover_top + (cover_height - p_height2) / 4 * 3) * ODROID_SCREEN_WIDTH + start_xpos + p_width1 + 6 + x] =
                        get_darken_pixel(pCover_Buffer[(y + y / 8) * cover_width + x * 8 / 7], 70 - x * 30 / p_width2);
        };
        if (draw_bot_title)
        {
            sprintf(str_buffer, "%s", file->name);
            size_t max_len = (p_width2 + p_width1 + 4) / font_width;
            size_t width = strlen(str_buffer) * font_width;
            if (width > (p_width2 + p_width1 + 4))
                width = max_len * font_width;
            odroid_overlay_draw_local_text_line(start_xpos + p_width1 + p_width2 + 4 - width, top_tit_pos, width, str_buffer, C_GW_OPAQUE_YELLOW, C_BLACK, NULL, 0);
        };
    };

    index = list->cursor + 2;
    item = gui_get_item_by_index(tab, &index);
    if (item)
    {
        file = (retro_emulator_file_t *)item->arg;
        if (file->img_size != 0)
        {
            JPEG_DecodeToBuffer((uint32_t)(file->img_address), (uint32_t)pCover_Buffer, &jpeg_cover_width, &jpeg_cover_height, 255);
            for (int y = 0; y < p_height1; y++)
                for (int x = 0; x < p_width1; x++)
                    dst_img[(y + cover_top + (cover_height - p_height1) / 4 * 3) * ODROID_SCREEN_WIDTH +
                            start_xpos + p_width1 + p_width2 * 2 + cover_width + 20 + x] =
                        get_darken_pixel(pCover_Buffer[(y + y * 3 / 8) * cover_width + ((r_width2 - p_width2) + x) * 8 / 5], 30 + x * 30 / p_width2);
        };
    };

    index = list->cursor - 2;
    item = gui_get_item_by_index(tab, &index);
    if (item)
    {
        file = (retro_emulator_file_t *)item->arg;
        if (file->img_size != 0)
        {
            JPEG_DecodeToBuffer((uint32_t)(file->img_address), (uint32_t)pCover_Buffer, &jpeg_cover_width, &jpeg_cover_height, 255);
            for (int y = 0; y < p_height1; y++)
                for (int x = 0; x < p_width1; x++)
                    dst_img[(y + cover_top + (cover_height - p_height1) / 4 * 3) * ODROID_SCREEN_WIDTH +
                            start_xpos + 3 + x] =
                        get_darken_pixel(pCover_Buffer[(y + y * 3 / 8) * cover_width + x * 8 / 5], 60 - x * 30 / p_width2);
        };
    };
};

void gui_draw_coverflow_v(tab_t *tab, int start_posx) // ||||||||
{
    retro_emulator_t *emu = (retro_emulator_t *)tab->arg;
    int font_height = odroid_overlay_get_local_font_size();
    int cover_height = emu->cover_height;
    int cover_width = emu->cover_width;
    int space_height = 40;
    uint32_t jpeg_cover_width = cover_width;
    uint32_t jpeg_cover_height = cover_height;
    //top ____|_|__|_(pl)__||_(main)_||__(pr)_|__|_|____ min 40;
    int p_height = (LIST_HEIGHT - cover_height - space_height) / 2;
    p_height = (p_height > cover_height) ? cover_height : p_height; //space width than real width, draw full size;
    p_height = p_height < 0 ? 0 : p_height;
    //real height = 32-8 = 24 //max = 136
    int start_ypos = STATUS_HEIGHT + (LIST_HEIGHT - ((p_height * 2) + cover_height + space_height)) / 2 + 4;
    //fisrt top point pos getted;
    start_ypos = start_ypos < 0 ? 0 : start_ypos;
    int p_width1 = cover_width * 7 / 8;
    int p_width2 = cover_width * 5 / 8;
    int r_height = cover_height * 7 / 8;

    uint16_t *dst_img = lcd_get_active_buffer();

    odroid_overlay_draw_fill_rect(start_posx + (cover_width - p_width2) * 3 / 4 + 7, start_ypos + 4, p_width2 - 6, 1, get_darken_pixel(C_GW_OPAQUE_YELLOW, 70));
    odroid_overlay_draw_fill_rect(start_posx + (cover_width - p_width2) * 3 / 4 + 3, start_ypos + 6, p_width2, 1, get_darken_pixel(C_GW_OPAQUE_YELLOW, 80));

    odroid_overlay_draw_rect(start_posx + (cover_width - p_width1) * 3 / 4 + 1, start_ypos + 9, p_width1 + 4, p_height + 2, 1, get_darken_pixel(C_GW_OPAQUE_YELLOW, 80));

    odroid_overlay_draw_rect(start_posx, start_ypos + 13 + p_height, cover_width + 6, cover_height + 6, 1, C_GW_YELLOW);
    odroid_overlay_draw_rect(start_posx + 1, start_ypos + 14 + p_height, cover_width + 4, cover_height + 4, 1, C_GW_OPAQUE_YELLOW);

    odroid_overlay_draw_rect(start_posx + (cover_width - p_width1) * 3 / 4 + 1, start_ypos + p_height + cover_height + 21, p_width1 + 4, p_height + 2, 1, get_darken_pixel(C_GW_OPAQUE_YELLOW, 80));

    odroid_overlay_draw_fill_rect(start_posx + (cover_width - p_width2) * 3 / 4 + 3, start_ypos + 2 * p_height + cover_height + 25, p_width2, 1, get_darken_pixel(C_GW_OPAQUE_YELLOW, 80));
    odroid_overlay_draw_fill_rect(start_posx + (cover_width - p_width2) * 3 / 4 + 7, start_ypos + 2 * p_height + cover_height + 27, p_width2 - 6, 1, get_darken_pixel(C_GW_OPAQUE_YELLOW, 70));

    if (p_height)
    {
        odroid_overlay_draw_fill_rect(start_posx + (cover_width - p_width1) * 3 / 4 + 2, start_ypos + p_height + 10, p_width1 + 2, 1, C_BLACK);
        odroid_overlay_draw_fill_rect(start_posx + (cover_width - p_width1) * 3 / 4 + 2, start_ypos + p_height + cover_height + 21, p_width1 + 2, 1, C_BLACK);
    }

    listbox_t *list = &tab->listbox;
    listbox_item_t *item = &list->items[list->cursor];

    retro_emulator_file_t *file = NULL;
    if (item) //current page
    {
        file = (retro_emulator_file_t *)item->arg;
        if (file->img_size == 0)
            draw_centered_local_text_line(start_ypos + p_height + 16 + (cover_height - font_height) / 2, s_No_Cover, start_posx + 3, start_posx + 3 + cover_width, get_darken_pixel(C_GW_RED, 80), C_BLACK);
        else
        {
            JPEG_DecodeToBuffer((uint32_t)(file->img_address), (uint32_t)pCover_Buffer, &jpeg_cover_width, &jpeg_cover_height, 255);
            odroid_display_write_rect(start_posx + 3 + (cover_width - jpeg_cover_width) / 2, start_ypos + p_height + 16 + (cover_height - jpeg_cover_height) / 2, jpeg_cover_width, jpeg_cover_height, jpeg_cover_width, pCover_Buffer);
        };
    }
    if (p_height)
    {
        int index = list->cursor + 1;
        item = gui_get_item_by_index(tab, &index);
        if (item)
        {
            file = (retro_emulator_file_t *)item->arg;
            if (file->img_size == 0)
            {
                if (p_height > font_height)
                    draw_centered_local_text_line(start_ypos + p_height + cover_height + 21 + (p_height - font_height) / 2, s_No_Cover, start_posx + 3, start_posx + 3 + cover_width, get_darken_pixel(C_GW_OPAQUE_YELLOW, 80), C_BLACK);
            }
            else
            {
                //draw the cover
                JPEG_DecodeToBuffer((uint32_t)(file->img_address), (uint32_t)pCover_Buffer, &jpeg_cover_width, &jpeg_cover_height, 255);
                for (int y = 0; y < p_height; y++)
                    for (int x = 0; x < p_width1; x++)
                        dst_img[(start_ypos + p_height + cover_height + 21 + y) * ODROID_SCREEN_WIDTH + start_posx + (cover_width - p_width1) * 3 / 4 + 3 + x] =
                            get_darken_pixel(pCover_Buffer[((r_height - p_height + y) * 8 / 7) * cover_width + x + x / 8], 40 + y * 20 / p_height);
            }
            index = list->cursor - 1;
            item = gui_get_item_by_index(tab, &index);
            if (item)
            {
                file = (retro_emulator_file_t *)item->arg;
                if (file->img_size == 0)
                {
                    if (p_height > font_height)
                        draw_centered_local_text_line(start_ypos + 11 + (p_height - font_height) / 2,
                                                      s_No_Cover,
                                                      start_posx + 3,
                                                      start_posx + 3 + cover_width,
                                                      get_darken_pixel(C_GW_OPAQUE_YELLOW, 80),
                                                      C_BLACK);
                }
                else
                {
                    //draw the cover
                    JPEG_DecodeToBuffer((uint32_t)(file->img_address), (uint32_t)pCover_Buffer, &jpeg_cover_width, &jpeg_cover_height, 255);

                    for (int y = 0; y < p_height; y++)
                        for (int x = 0; x < p_width1; x++)
                            dst_img[(start_ypos + 11 + y) * ODROID_SCREEN_WIDTH + start_posx + (cover_width - p_width1) * 3 / 4 + 3 + x] =
                                get_darken_pixel(pCover_Buffer[(y + y / 8) * cover_width + x + x / 8], 60 - y * 20 / p_height);
                }
            }
        }
    }
    gui_draw_simple_list(start_posx + cover_width + 12, tab);
}

#endif

void gui_draw_list(tab_t *tab)
{
    odroid_overlay_draw_fill_rect(0, LIST_Y_OFFSET, LIST_WIDTH, LIST_HEIGHT, C_BLACK);

#if COVERFLOW != 0
    int theme_index = odroid_settings_theme_get();

    switch (theme_index)
    {
    case 3:
    {
        listbox_t *list = &tab->listbox;

        if (list->cursor >= 0 && list->cursor < list->length)
        {

            listbox_item_t *item=&list->items[list->cursor];

            /* get the current cover size */
            if (item)
                 gui_get_cover_size((retro_emulator_file_t *)item->arg, &current_cover_width, &current_cover_height);
     
            int drawing[5]={2,-2,1,-1,0};
            int idx;

            for (int cov_idx =0; cov_idx < 5;cov_idx++) {
                idx = list->cursor + drawing[cov_idx];
                item = gui_get_item_by_index(tab, &idx);
                if (item) 
                    gui_draw_coverlight_h((retro_emulator_file_t *)item->arg,drawing[cov_idx]);
            }

            //draw current postion over all items
            sprintf(str_buffer, "%d/%d", list->cursor + 1, list->length);
            //
            int width = strlen(str_buffer) * odroid_overlay_get_font_width();
            odroid_overlay_draw_fill_rect((ODROID_SCREEN_WIDTH - width) / 2 - 2, 44, width + 4, 12, C_GW_YELLOW);
            odroid_overlay_draw_fill_rect((ODROID_SCREEN_WIDTH - width) / 2 - 2 - 1, 45, 1, 10, get_darken_pixel(C_GW_OPAQUE_YELLOW, 40));
            odroid_overlay_draw_fill_rect((ODROID_SCREEN_WIDTH - width) / 2 - 2 + width + 4, 45, 1, 10, get_darken_pixel(C_GW_OPAQUE_YELLOW, 40));
            odroid_overlay_draw_fill_rect((ODROID_SCREEN_WIDTH - width) / 2 - 2 + 1, 43, width + 2, 1, get_darken_pixel(C_GW_OPAQUE_YELLOW, 40));
            odroid_overlay_draw_text_line((ODROID_SCREEN_WIDTH - width) / 2, 46, width, str_buffer, C_BLACK, C_GW_YELLOW);

        }
    }
        break;
    case 4:
       {
        listbox_t *list = &tab->listbox;
        
        if (list->cursor >= 0 && list->cursor < list->length)
        {
           listbox_item_t *covitem = NULL;

            /* Get the widther cover,  Draw covers and get the current cover size */
            int idx;
            uint32_t pos_list = NOCOVER_WIDTH + 2*COVER_BORDER;
            for (int cov_idx=2; cov_idx > -1;cov_idx--) {
                idx = list->cursor + cov_idx;
                covitem = gui_get_item_by_index(tab, &idx);
                if (covitem) {
                    gui_get_cover_size((retro_emulator_file_t *)covitem->arg, &current_cover_width, &current_cover_height);
                    pos_list = pos_list < (2*COVER_BORDER+current_cover_width+8*(2-cov_idx) ) ? 2*COVER_BORDER+current_cover_width+8*(2-cov_idx):pos_list;
                    gui_draw_coverlight_v((retro_emulator_file_t *)covitem->arg, cov_idx);
                }
            }

            /* Determine title length */
            int w,w2;
            w = (ODROID_SCREEN_WIDTH - pos_list-5) / odroid_overlay_get_local_font_width();
           // w2 = (ODROID_SCREEN_WIDTH - pos_list ) / odroid_overlay_get_local_font_width();
            w = w * odroid_overlay_get_local_font_width();
          //  w2 = w2 * odroid_overlay_get_local_font_width();
            w2=w;

            /* Write current title */
            listbox_item_t *item = &list->items[list->cursor];
            if (item)
                odroid_overlay_draw_local_text_line(pos_list+4, 107, w, list->items[list->cursor].text, C_GW_YELLOW, C_BLACK, NULL, 0);

            /* write other titles */
            int index_next = list->cursor + 1;
            int index_proior = list->cursor - 1;
            //up & down
            int h1 = 107;
            int h2 = 107;
            for (int i = 0; i < 5; i++)
            {
                listbox_item_t *next_item = gui_get_item_by_index(tab, &index_next);
                h1 = h1 - 12 - 4 + i;
                if (next_item)
                    odroid_overlay_draw_local_text_line(
                        pos_list+4,
                        h1,
                        w2,
                        list->items[index_next].text,
                        get_darken_pixel(C_GW_YELLOW, 70 - i * 12),
                        C_BLACK,
                        NULL,
                        0);
                index_next++;

                listbox_item_t *prior_item = gui_get_item_by_index(tab, &index_proior);
                h2 = h2 + 12 + 4 - i;
                if (prior_item)
                    odroid_overlay_draw_local_text_line(
                        pos_list+4,
                        h2,
                        w2,
                        list->items[index_proior].text,
                        get_darken_pixel(C_GW_YELLOW, 70 - i * 12),
                        C_BLACK,
                        NULL,
                        0);
                index_proior--;
            }
            //draw current postion
            gui_draw_item_postion_v(ODROID_SCREEN_WIDTH - 5, LIST_Y_OFFSET + 4, LIST_Y_OFFSET + LIST_HEIGHT - 4, list->cursor + 1, list->length);

        }
    }

        break;
    case 2:
        gui_draw_coverflow_h(tab);
        break;
    case 1:
        gui_draw_coverflow_v(tab, 4);
        break;
    default:
        gui_draw_simple_list(10, tab);
    }

#else
    gui_draw_simple_list(10, tab);
#endif
}
//const char * GW_Themes[] = {s_Theme_sList, s_Theme_CoverV, s_Theme_CoverH,s_Theme_CoverLightV,s_Theme_CoverLight};

void gui_draw_cover(retro_emulator_file_t *file)
{
    //nothing
}
