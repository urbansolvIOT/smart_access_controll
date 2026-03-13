#include <Arduino.h>
#include "lvgl.h"

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <SPI.h>

#include "oms_protocol.h"


/* ================= DISPLAY ================= */

#define TFT_HOR_RES 320
#define TFT_VER_RES 480

#define DRAW_BUF_SIZE (TFT_HOR_RES * TFT_VER_RES / 10 * (LV_COLOR_DEPTH / 8))
uint8_t *draw_buf;

/* ================= UI ================= */

lv_obj_t *tileview;
lv_obj_t *tile_home;
lv_obj_t *tile_menu;
bool isHomeScreen = true;
void tile_event_handler(lv_event_t *e);

lv_obj_t *screen_enroll;
lv_obj_t *screen_delete;
lv_obj_t *ta_delete_name;
lv_obj_t *dd_division;
lv_obj_t *dd_delete_division;
lv_obj_t *screen_pop;
lv_obj_t *table_user;
lv_obj_t *status_popup;
lv_timer_t *status_timer;
lv_obj_t *pin_btn;

unsigned long lastVerify=0;
unsigned long lastActivity = 0;
const unsigned long IDLE_TIMEOUT = 60000; // 1 menit

unsigned long lastOmsReset = 0;
const unsigned long OMS_RESET_INTERVAL = 30000; // 30 detik

lv_obj_t *verify_popup;
lv_timer_t *verify_timer;

const char* poseText[5] = {
  "MID",
  "LEFT",
  "RIGHT",
  "UP",
  "DOWN"
};

/* ================= KEYBOARD ================= */

lv_obj_t *keyboard;
lv_obj_t *ta_username;

/* ================= POPUP ================= */

lv_obj_t *popup;
lv_timer_t *popup_timer;
lv_obj_t *pose_popup;
lv_obj_t *pose_label;
lv_obj_t *pose_bar;
lv_obj_t *pose_count;

int pose_step = 0;

/* ================= PIN ================= */

String correctPIN="1234";
String inputPIN="";

lv_obj_t *pin_popup;
lv_obj_t *pin_display;

/* ================= LGFX ================= */

class LGFX : public lgfx::LGFX_Device
{
  lgfx::Panel_ST7796 _panel;
  lgfx::Bus_SPI _bus;
  lgfx::Light_PWM _light;
  lgfx::Touch_XPT2046 _touch;

public:

  LGFX(){

    auto cfg=_bus.config();

    cfg.spi_host=HSPI_HOST;
    cfg.freq_write=80000000;
    cfg.freq_read=40000000;

    cfg.spi_3wire=true;
    cfg.use_lock=true;

    cfg.pin_sclk=14;
    cfg.pin_mosi=13;
    cfg.pin_miso=12;
    cfg.pin_dc=2;

    _bus.config(cfg);
    _panel.setBus(&_bus);

    auto pcfg=_panel.config();

    pcfg.pin_cs=15;
    pcfg.panel_width=TFT_HOR_RES;
    pcfg.panel_height=TFT_VER_RES;
    pcfg.bus_shared=true;

    _panel.config(pcfg);

    auto lcfg=_light.config();

    lcfg.pin_bl=27;
    lcfg.pwm_channel=7;

    _light.config(lcfg);
    _panel.setLight(&_light);

    auto tcfg=_touch.config();

    tcfg.spi_host=HSPI_HOST;
    tcfg.freq=2500000;

    tcfg.pin_cs=GPIO_NUM_33;
    tcfg.bus_shared=true;

    tcfg.x_min=200;
    tcfg.x_max=3800;

    tcfg.y_min=200;
    tcfg.y_max=3800;

    _touch.config(tcfg);

    _panel.setTouch(&_touch);

    setPanel(&_panel);
  }
};

LGFX tft;

/* ================= DISPLAY ================= */

void my_disp_flush(lv_display_t *disp,const lv_area_t *area,uint8_t *px)
{
  uint32_t w=lv_area_get_width(area);
  uint32_t h=lv_area_get_height(area);

  tft.startWrite();
  tft.setAddrWindow(area->x1,area->y1,w,h);
  tft.writePixels((lgfx::rgb565_t*)px,w*h);
  tft.endWrite();

  lv_disp_flush_ready(disp);
}

/* ================= TOUCH ================= */

void my_touch_read(lv_indev_t *,lv_indev_data_t *data)
{
  uint16_t x,y;

  if(tft.getTouch(&x,&y))
  {
    x=TFT_HOR_RES-x;

    data->state=LV_INDEV_STATE_PRESSED;
    data->point.x=x;
    data->point.y=y;

    lastActivity = millis();   // RESET TIMER
  }
  else
  {
    data->state=LV_INDEV_STATE_RELEASED;
  }
}

/* ================= KEYBOARD ================= */

void ta_event_cb(lv_event_t *e)
{
  lv_event_code_t code = lv_event_get_code(e);

  if(code == LV_EVENT_FOCUSED)
  {
    lv_keyboard_set_textarea(keyboard, (lv_obj_t*)lv_event_get_target(e));
    lv_obj_clear_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
  }
}

void kb_event_cb(lv_event_t *e)
{
  lv_event_code_t code = lv_event_get_code(e);

  if(code == LV_EVENT_READY || code == LV_EVENT_CANCEL)
  {
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
  }
}

void reset_enroll_form()
{
    /* kosongkan username */
    lv_textarea_set_text(ta_username, "");

    /* reset dropdown ke pilihan pertama */
    lv_dropdown_set_selected(dd_division, 0);

    /* sembunyikan keyboard */
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
}

void reset_delete_form()
{
    lv_textarea_set_text(ta_delete_name, "");

    lv_dropdown_set_selected(dd_delete_division, 0);

    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
}

/* ================= POPUP ================= */

void popup_close(lv_timer_t *t)
{
  if(popup!=NULL)
  {
    lv_obj_del(popup);
    popup=NULL;
  }
}

void show_popup_success(const char *msg)
{
  if(popup!=NULL)
    lv_obj_del(popup);

  popup=lv_obj_create(lv_scr_act());

  lv_obj_set_size(popup,260,120);
  lv_obj_align(popup, LV_ALIGN_TOP_MID, 0, 100);

  lv_obj_set_style_bg_color(popup, lv_color_hex(0x1D4ED8), 0);

  lv_obj_t *label=lv_label_create(popup);
  lv_label_set_text(label,msg); /* label popup success */

  lv_obj_set_style_text_color(label,lv_color_white(),0);
  lv_obj_center(label);

  popup_timer=lv_timer_create(popup_close,2500,NULL);
}

void show_popup_fail(const char *msg)
{
  if(popup!=NULL)
    lv_obj_del(popup);

  popup=lv_obj_create(lv_scr_act());

  lv_obj_set_size(popup,260,120);
  lv_obj_align(popup, LV_ALIGN_TOP_MID, 0, 100);

  lv_obj_set_style_bg_color(popup, lv_color_hex(0xDC2626), 0);

  lv_obj_t *label=lv_label_create(popup);
  lv_label_set_text(label,msg); /* label popup fail */

  lv_obj_set_style_text_color(label,lv_color_white(),0);
  lv_obj_center(label);

  popup_timer=lv_timer_create(popup_close,2500,NULL);
}

void create_pose_popup()
{
    pose_step = 0;
    pose_popup = lv_obj_create(lv_scr_act());

    lv_obj_set_size(pose_popup,260,120);

    lv_obj_align(pose_popup,LV_ALIGN_BOTTOM_MID,0,120);

    lv_obj_set_style_radius(pose_popup,15,0);

    lv_obj_set_style_bg_color(pose_popup,lv_color_hex(0x1D4ED8),0);

    lv_obj_set_style_border_width(pose_popup,0,0);

    /* label pose */

    pose_label = lv_label_create(pose_popup);

    lv_label_set_text(pose_label,"LOOK CAMERA"); /* label pose */

    lv_obj_set_style_text_color(pose_label,lv_color_white(),0);

    lv_obj_align(pose_label,LV_ALIGN_TOP_MID,0,10);


    /* progress bar */

    pose_bar = lv_bar_create(pose_popup);

    lv_obj_set_size(pose_bar,200,15);

    lv_obj_align(pose_bar,LV_ALIGN_CENTER,0,10);

    lv_bar_set_range(pose_bar,0,5);

    lv_bar_set_value(pose_bar,0,LV_ANIM_OFF);


    /* pose counter */

    pose_count = lv_label_create(pose_popup);

    lv_label_set_text(pose_count, poseText[0]); /* label progress */

    lv_obj_set_style_text_color(pose_count,lv_color_white(),0);

    lv_obj_align(pose_count,LV_ALIGN_BOTTOM_MID,0,-10);


    /* slide animation */

    lv_anim_t a;

    lv_anim_init(&a);

    lv_anim_set_var(&a, pose_popup);

    lv_anim_set_values(&a, 120, -10);

    lv_anim_set_time(&a, 300);

    lv_anim_set_exec_cb(&a,(lv_anim_exec_xcb_t)lv_obj_set_y);

    lv_anim_start(&a);
}

void update_pose()
{
    if(pose_step < 5)
    {
        lv_label_set_text(pose_label, poseText[pose_step]);
    }

    pose_step++;

    lv_bar_set_value(pose_bar,pose_step,LV_ANIM_ON);
}

void close_pose_popup()
{
    if(pose_popup != NULL)
    {
        lv_obj_del(pose_popup);
        pose_popup = NULL;
        pose_step = 0;
    }
}

void status_popup_close(lv_timer_t *t)
{
    if(status_popup != NULL)
    {
        lv_obj_del(status_popup);
        status_popup = NULL;
    }
}

void show_status_popup(const char *msg)
{
    if(status_popup != NULL)
        lv_obj_del(status_popup);

    status_popup = lv_obj_create(lv_scr_act());

    lv_obj_set_size(status_popup,200,60);

    /* posisi di atas popup pose */
    lv_obj_align(status_popup, LV_ALIGN_BOTTOM_MID, 0, -160);

    lv_obj_set_style_bg_color(status_popup, lv_color_hex(0x111827),0);
    lv_obj_set_style_radius(status_popup,12,0);
    lv_obj_set_style_border_width(status_popup,0,0);

    lv_obj_t *label = lv_label_create(status_popup);
    lv_label_set_text(label,msg);
    lv_obj_set_style_text_color(label,lv_color_white(),0);
    lv_obj_center(label);

    /* hilang otomatis 1.5 detik */

    status_timer = lv_timer_create(status_popup_close,1500,NULL);
}

/* ================= VERIFY CALLBACK ================= */

void ui_verify_success(const char *name)
{
  char msg[64];
  sprintf(msg,"WELCOME %s",name);

  show_popup_success(msg);
}

void ui_verify_fail()
{
  show_popup_fail("ACCESS DENIED");
}

/* ================= PIN KEYPAD ================= */

void pin_keypad_event(lv_event_t *e)
{
  lv_obj_t *btnm=lv_event_get_target_obj(e);

  const char *txt=lv_btnmatrix_get_btn_text(btnm,
                     lv_btnmatrix_get_selected_btn(btnm));

  if(strcmp(txt,"C")==0)
  {
    inputPIN="";
  }

  else if(strcmp(txt,"OK")==0)
  {
    if(inputPIN==correctPIN)
      show_popup_success("WELCOME URBANSOLV");
    else
      show_popup_fail("WRONG PIN");

    lv_obj_del(pin_popup);
    return;
  }

  else
  {
    inputPIN+=txt;
  }

  String star="";
  for(int i=0;i<inputPIN.length();i++)
    star+="*";

  lv_label_set_text(pin_display,star.c_str()); /* label PIN display */
}

void create_pin_keypad()
{
  pin_popup=lv_obj_create(lv_scr_act());

  lv_obj_set_size(pin_popup,300,360);

  lv_obj_center(pin_popup);

  pin_display=lv_label_create(pin_popup);
  lv_label_set_text(pin_display,""); /* label PIN display */

  lv_obj_align(pin_display,LV_ALIGN_TOP_MID,0,20);

  static const char *map[]=
  {
    "1","2","3","\n",
    "4","5","6","\n",
    "7","8","9","\n",
    "C","0","OK",""
  };

  lv_obj_t *btnm=lv_btnmatrix_create(pin_popup);

  lv_btnmatrix_set_map(btnm,map);

  lv_obj_set_size(btnm,260,240);
  lv_obj_align(btnm,LV_ALIGN_BOTTOM_MID,0,-10);

  lv_obj_add_event_cb(btnm,pin_keypad_event,LV_EVENT_VALUE_CHANGED,NULL);
}

void pin_event(lv_event_t *e)
{
  if(lv_event_get_code(e)==LV_EVENT_CLICKED)
  {
    inputPIN="";
    create_pin_keypad();
  }
}

/* ================= ENROLL SCREEN ================= */

void create_enroll_screen()
{

  screen_enroll = lv_tileview_add_tile(tileview,2,0,LV_DIR_HOR);

  lv_obj_t *title = lv_label_create(screen_enroll);
  lv_label_set_text(title,"Enroll User"); /* label title */
  lv_obj_align(title,LV_ALIGN_TOP_MID,0,20);

  lv_obj_t *btn_back = lv_btn_create(screen_enroll);
  lv_obj_set_size(btn_back,90,40);
  lv_obj_align(btn_back,LV_ALIGN_TOP_LEFT,10,15);

  lv_obj_add_event_cb(btn_back,[](lv_event_t *e){
      lv_obj_set_tile(tileview, tile_menu, LV_ANIM_ON);
  },LV_EVENT_CLICKED,NULL);

  lv_obj_t *label_back = lv_label_create(btn_back);
  lv_label_set_text(label_back,"< Back"); /* label tombol */
  lv_obj_center(label_back);

  lv_obj_t *label_user = lv_label_create(screen_enroll);
  lv_label_set_text(label_user,"Username"); /* label username */
  lv_obj_align(label_user,LV_ALIGN_TOP_LEFT,30,80);

  ta_username = lv_textarea_create(screen_enroll);
  lv_obj_set_size(ta_username,260,40);
  lv_obj_align(ta_username,LV_ALIGN_TOP_MID,0,110);
  lv_textarea_set_placeholder_text(ta_username,"Enter username");
  lv_obj_add_event_cb(ta_username, ta_event_cb, LV_EVENT_ALL, NULL);

  lv_obj_t *label_div = lv_label_create(screen_enroll);
  lv_label_set_text(label_div,"Division"); /* label division */
  lv_obj_align(label_div,LV_ALIGN_TOP_LEFT,30,170);

  dd_division = lv_dropdown_create(screen_enroll);
  lv_obj_set_size(dd_division,260,40);
  lv_obj_align(dd_division,LV_ALIGN_TOP_MID,0,200);

  lv_dropdown_set_options(dd_division,
      "Project Management\n"
      "HR GA Tax\n"
      "Business Development\n"
      "Production Development\n"
      "AI Data\n"
      "IoT Development\n"
      "Creative");

  lv_obj_t *btn_cancel = lv_btn_create(screen_enroll);
  lv_obj_set_size(btn_cancel,100,45);
  lv_obj_align(btn_cancel,LV_ALIGN_TOP_LEFT,40,270);

  lv_obj_set_style_bg_color(btn_cancel,lv_color_hex(0xEF4444),0);

  lv_obj_add_event_cb(btn_cancel,[](lv_event_t *e){
    reset_enroll_form();
    sendReset();        // reset OMS
    close_pose_popup(); // tutup popup pose jika ada
  },LV_EVENT_CLICKED,NULL);

  lv_obj_t *label_cancel = lv_label_create(btn_cancel);
  lv_label_set_text(label_cancel,"Cancel"); /* label tombol */
  lv_obj_center(label_cancel);

  lv_obj_t *btn_enroll = lv_btn_create(screen_enroll);
  lv_obj_set_size(btn_enroll,100,45);
  lv_obj_align(btn_enroll,LV_ALIGN_TOP_RIGHT,-40,270);

  lv_obj_add_event_cb(btn_enroll,[](lv_event_t *e){

    const char *username = lv_textarea_get_text(ta_username);

    if(strlen(username) == 0)
    {
        show_popup_fail("USERNAME EMPTY");
        return;
    }

    uint16_t divisionIndex = lv_dropdown_get_selected(dd_division);
    uint8_t adminFlag = divisionIndex + 1;

    oms_set_user(username, adminFlag);

    create_pose_popup();
    oms_startEnroll();

  },LV_EVENT_CLICKED,NULL);

  lv_obj_set_style_bg_color(btn_enroll,lv_color_hex(0x1D4ED8),0);

  lv_obj_t *label_enroll = lv_label_create(btn_enroll);
  lv_label_set_text(label_enroll,"Enroll"); /* label tombol */
  lv_obj_center(label_enroll);
}

/* ================= DEL USER SCREEN ================= */

void create_delete_screen()
{

  screen_delete = lv_tileview_add_tile(tileview,3,0,LV_DIR_HOR);

  /* title */

  lv_obj_t *title = lv_label_create(screen_delete);
  lv_label_set_text(title,"Delete User"); /* label title */
  lv_obj_align(title,LV_ALIGN_TOP_MID,0,20);


  /* back button */

  lv_obj_t *btn_back = lv_btn_create(screen_delete);
  lv_obj_set_size(btn_back,90,40);
  lv_obj_align(btn_back,LV_ALIGN_TOP_LEFT,10,15);

  lv_obj_add_event_cb(btn_back,[](lv_event_t *e){
      lv_obj_set_tile(tileview, tile_menu, LV_ANIM_ON);
  },LV_EVENT_CLICKED,NULL);

  lv_obj_t *label_back = lv_label_create(btn_back);
  lv_label_set_text(label_back,"< Back"); /* label tombol */
  lv_obj_center(label_back);


  /* username label */

  lv_obj_t *label_user = lv_label_create(screen_delete);
  lv_label_set_text(label_user,"Username"); /* label username */
  lv_obj_align(label_user,LV_ALIGN_TOP_LEFT,30,80);


  /* textarea */

  ta_delete_name = lv_textarea_create(screen_delete);

  lv_obj_set_size(ta_delete_name,260,40);
  lv_obj_align(ta_delete_name,LV_ALIGN_TOP_MID,0,110);

  lv_textarea_set_placeholder_text(ta_delete_name,"Enter username");

  lv_obj_add_event_cb(ta_delete_name, ta_event_cb, LV_EVENT_ALL, NULL);


  /* division label */

  lv_obj_t *label_div = lv_label_create(screen_delete);
  lv_label_set_text(label_div,"Division"); /* label division */
  lv_obj_align(label_div,LV_ALIGN_TOP_LEFT,30,170);


  /* dropdown division */

  dd_delete_division = lv_dropdown_create(screen_delete);

  lv_obj_set_size(dd_delete_division,260,40);
  lv_obj_align(dd_delete_division,LV_ALIGN_TOP_MID,0,200);

  lv_dropdown_set_options(dd_delete_division,
      "Project Management\n"
      "HR GA Tax\n"
      "Business Development\n"
      "Production Development\n"
      "AI Data\n"
      "IoT Development\n"
      "Creative");


  /* cancel button */

  lv_obj_t *btn_cancel = lv_btn_create(screen_delete);

  lv_obj_set_size(btn_cancel,100,45);
  lv_obj_align(btn_cancel,LV_ALIGN_TOP_LEFT,40,270);

  lv_obj_set_style_bg_color(btn_cancel,lv_color_hex(0xEF4444),0);

  lv_obj_add_event_cb(btn_cancel,[](lv_event_t *e){
      reset_delete_form();
  },LV_EVENT_CLICKED,NULL);

  lv_obj_t *label_cancel = lv_label_create(btn_cancel);
  lv_label_set_text(label_cancel,"Cancel"); /* label tombol */
  lv_obj_center(label_cancel);


  /* delete button */

  lv_obj_t *btn_delete = lv_btn_create(screen_delete);

  lv_obj_set_size(btn_delete,100,45);
  lv_obj_align(btn_delete,LV_ALIGN_TOP_RIGHT,-40,270);

  lv_obj_set_style_bg_color(btn_delete,lv_color_hex(0x1D4ED8),0);

  lv_obj_add_event_cb(btn_delete,[](lv_event_t *e){

      show_popup_success("USER DELETED");

  },LV_EVENT_CLICKED,NULL);

  lv_obj_t *label_delete = lv_label_create(btn_delete);
  lv_label_set_text(label_delete,"Delete"); /* label tombol */
  lv_obj_center(label_delete);

}

/* ================= POP USER SCREEN ================= */

void create_pop_screen()
{

  screen_pop = lv_tileview_add_tile(tileview,4,0,LV_DIR_HOR);

  /* title */

  lv_obj_t *title = lv_label_create(screen_pop);
  lv_label_set_text(title,"Pop User"); /* label title */
  lv_obj_align(title,LV_ALIGN_TOP_MID,0,20);


  /* back button */

  lv_obj_t *btn_back = lv_btn_create(screen_pop);
  lv_obj_set_size(btn_back,90,40);
  lv_obj_align(btn_back,LV_ALIGN_TOP_LEFT,10,15);

  lv_obj_add_event_cb(btn_back,[](lv_event_t *e){
      lv_obj_set_tile(tileview, tile_menu, LV_ANIM_ON);
  },LV_EVENT_CLICKED,NULL);

  lv_obj_t *label_back = lv_label_create(btn_back);
  lv_label_set_text(label_back,"< Back"); /* label tombol */
  lv_obj_center(label_back);


  /* table */

  table_user = lv_table_create(screen_pop);

  lv_obj_set_size(table_user,300,300);

  lv_obj_align(table_user,LV_ALIGN_TOP_MID,0,80);

  lv_table_set_col_cnt(table_user,3);
  lv_table_set_row_cnt(table_user,8);


  /* header */

  lv_table_set_cell_value(table_user,0,0,"Name");
  lv_table_set_cell_value(table_user,0,1,"ID");
  lv_table_set_cell_value(table_user,0,2,"Division");


  /* contoh data */

  lv_table_set_cell_value(table_user,1,0,"Tezar");
  lv_table_set_cell_value(table_user,1,1,"001");
  lv_table_set_cell_value(table_user,1,2,"IoT Dev");

  lv_table_set_cell_value(table_user,2,0,"Fadli");
  lv_table_set_cell_value(table_user,2,1,"002");
  lv_table_set_cell_value(table_user,2,2,"IoT Dev");

  lv_table_set_cell_value(table_user,3,0,"Dwigo");
  lv_table_set_cell_value(table_user,3,1,"003");
  lv_table_set_cell_value(table_user,3,2,"IoT Dev");

}

/* ================= HOME UI ================= */

void create_home_ui()
{
  tileview=lv_tileview_create(lv_scr_act());

  lv_obj_set_size(tileview,320,480);

  tile_home=lv_tileview_add_tile(tileview,0,0,LV_DIR_HOR);

  lv_obj_t *logo=lv_label_create(tile_home);
  lv_label_set_text(logo,"urbansolv."); /* label logo */
  lv_obj_align(logo,LV_ALIGN_TOP_MID,0,20);

  lv_obj_t *verify_btn = lv_btn_create(tile_home);
  lv_obj_set_size(verify_btn,120,50);
  lv_obj_align(verify_btn,LV_ALIGN_BOTTOM_MID,-70,-40);

  lv_obj_set_style_bg_color(verify_btn, lv_color_hex(0x10B981), 0);

  lv_obj_add_event_cb(verify_btn, [](lv_event_t *e){

      Serial.println("[BUTTON] VERIFY");

      oms_startVerify();

  }, LV_EVENT_CLICKED, NULL);

  lv_obj_t *label_verify = lv_label_create(verify_btn);
  lv_label_set_text(label_verify,"VERIFY");
  lv_obj_set_style_text_color(label_verify, lv_color_white(), 0);
  lv_obj_center(label_verify);

  pin_btn = lv_btn_create(tile_home);
  lv_obj_set_size(pin_btn,120,50);
  lv_obj_align(pin_btn,LV_ALIGN_BOTTOM_MID,70,-40);

  lv_obj_set_style_bg_color(pin_btn, lv_color_hex(0x1D4ED8), 0);

  lv_obj_add_event_cb(pin_btn,pin_event,LV_EVENT_CLICKED,NULL);

  lv_obj_t *label_pin = lv_label_create(pin_btn);
  lv_label_set_text(label_pin,"PIN"); /* label tombol */
  lv_obj_set_style_text_color(label_pin, lv_color_white(), 0);
  lv_obj_center(label_pin);

  tile_menu=lv_tileview_add_tile(tileview,1,0,LV_DIR_HOR);

  lv_obj_t *title=lv_label_create(tile_menu);
  lv_label_set_text(title,"MENU"); /* label menu */
  lv_obj_align(title,LV_ALIGN_TOP_MID,0,20);

  lv_obj_t *btn_enroll = lv_btn_create(tile_menu);
  lv_obj_set_size(btn_enroll,120,80);
  lv_obj_align(btn_enroll,LV_ALIGN_CENTER,-70,-40);

  lv_obj_add_event_cb(btn_enroll,[](lv_event_t *e){
      lv_obj_set_tile(tileview, screen_enroll, LV_ANIM_ON);
  },LV_EVENT_CLICKED,NULL);

  lv_obj_t *label_enroll = lv_label_create(btn_enroll);
  lv_label_set_text(label_enroll,"Enroll"); /* label tombol */
  lv_obj_center(label_enroll);

  lv_obj_t *btn_delete = lv_btn_create(tile_menu);
  lv_obj_set_size(btn_delete,120,80);
  lv_obj_align(btn_delete,LV_ALIGN_CENTER,70,-40);

  lv_obj_add_event_cb(btn_delete,[](lv_event_t *e){
    lv_obj_set_tile(tileview, screen_delete, LV_ANIM_ON);
  },LV_EVENT_CLICKED,NULL);

  lv_obj_t *label_delete = lv_label_create(btn_delete);
  lv_label_set_text(label_delete,"Del User"); /* label tombol */
  lv_obj_center(label_delete);

  lv_obj_t *btn_pop = lv_btn_create(tile_menu);
  lv_obj_set_size(btn_pop,120,80);
  lv_obj_align(btn_pop,LV_ALIGN_CENTER,-70,60);

  lv_obj_add_event_cb(btn_pop,[](lv_event_t *e){
    lv_obj_set_tile(tileview, screen_pop, LV_ANIM_ON);
  },LV_EVENT_CLICKED,NULL);

  lv_obj_t *label_pop = lv_label_create(btn_pop);
  lv_label_set_text(label_pop,"Pop User"); /* label tombol */
  lv_obj_center(label_pop);

  lv_obj_t *btn_reset = lv_btn_create(tile_menu);
  lv_obj_set_size(btn_reset,120,80);
  lv_obj_align(btn_reset,LV_ALIGN_CENTER,70,60);

  lv_obj_add_event_cb(btn_reset, [](lv_event_t *e){
      sendReset();                     // reset OMS
      show_popup_success("OMS RESET"); // tampilkan popup
  }, LV_EVENT_CLICKED, NULL);

  lv_obj_t *label_reset = lv_label_create(btn_reset);
  lv_label_set_text(label_reset,"Reset"); /* label tombol */
  lv_obj_center(label_reset);

  keyboard = lv_keyboard_create(lv_scr_act());
  lv_obj_set_size(keyboard,320,180);
  lv_obj_align(keyboard,LV_ALIGN_BOTTOM_MID,0,0);
  lv_obj_add_flag(keyboard,LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(keyboard,kb_event_cb,LV_EVENT_ALL,NULL);

  lv_obj_add_event_cb(tileview, tile_event_handler, LV_EVENT_VALUE_CHANGED, NULL);
}

void tile_event_handler(lv_event_t *e)
{
    lv_obj_t *tile = lv_tileview_get_tile_act(tileview);

    if(tile == tile_home)
    {
        isHomeScreen = true;
    }
    else
    {
        isHomeScreen = false;
    }
}

/* ================= SETUP ================= */

void setup()
{
  Serial.begin(115200);

  tft.begin();
  tft.setRotation(0);

  lv_init();

  lv_display_t *disp=lv_display_create(TFT_HOR_RES,TFT_VER_RES);

  draw_buf=new uint8_t[DRAW_BUF_SIZE];

  lv_display_set_flush_cb(disp,my_disp_flush);

  lv_display_set_buffers(disp,draw_buf,NULL,
                         DRAW_BUF_SIZE,LV_DISPLAY_RENDER_MODE_PARTIAL);

  lv_indev_t *indev=lv_indev_create();

  lv_indev_set_type(indev,LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev,my_touch_read);

  oms_init(); 
  /* oms_startVerify(); */
  lastVerify = millis();

  create_home_ui();
  create_enroll_screen();
  create_delete_screen();
  create_pop_screen();

  lastActivity = millis();
}

/* ================= LOOP ================= */

unsigned long lastTick=0;

void check_idle_timeout()
{
  if(millis() - lastActivity > IDLE_TIMEOUT)
  {
    if(tileview != NULL)
    {
      lv_obj_set_tile(tileview, tile_home, LV_ANIM_ON);
    }

    lastActivity = millis();
  }
}

extern bool verifying;
int verifyCounter = 0;

void loop()
{
  unsigned long now = millis();

  /* LVGL tick */
  if(now - lastTick >= 5)
  {
    lv_tick_inc(5);
    lastTick = now;
  }

  lv_timer_handler();

  /* OMS UART parser */
  oms_loop();

  /* VERIFY hanya di HOME 
  if(lv_tileview_get_tile_act(tileview) == tile_home && !verifying)
  {
    if(now - lastVerify > 2000 && now > verifyCooldown)
    {
        oms_startVerify();
        lastVerify = now;
    }
  } */

  /* AUTO RESET OMS BERDASARKAN WAKTU */
  if(now - lastOmsReset > OMS_RESET_INTERVAL)
  {
      sendReset();
      lastOmsReset = now;

      verifying = false;   // penting supaya verify bisa jalan lagi
  }

  /* kembali ke home jika idle */
  check_idle_timeout();

  delay(2);
}