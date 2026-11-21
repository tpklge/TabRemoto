#pragma once
#include "lvgl.h"
#include <cstring>
namespace tabremoto::skin {
constexpr unsigned bg=0x171A20,panel=0x242830,text=0xEEF2F8,muted=0xA3ADBD,border=0x454C58,accent=0xEB2638;
inline void surface(lv_obj_t *o,unsigned color=panel,int padding=12){
 lv_obj_set_style_bg_color(o,lv_color_hex(color),0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);
 lv_obj_set_style_text_color(o,lv_color_hex(text),0);lv_obj_set_style_border_color(o,lv_color_hex(border),0);
 lv_obj_set_style_border_width(o,1,0);lv_obj_set_style_radius(o,12,0);lv_obj_set_style_pad_all(o,padding,0);
 lv_obj_set_style_shadow_width(o,0,0);
}
inline void control(lv_obj_t *o,bool primary=false){
 surface(o,primary?accent:bg,10);lv_obj_set_style_radius(o,9,0);
 lv_obj_set_style_outline_color(o,lv_color_hex(accent),LV_STATE_FOCUSED);
 lv_obj_set_style_outline_width(o,2,LV_STATE_FOCUSED);
 lv_obj_set_style_bg_color(o,lv_color_hex(0x393F49),LV_STATE_PRESSED);
}
inline const char *action_icon(const char *text) {
 if(strstr(text,"Criar")||strstr(text,"Nova")||strstr(text,"Novo"))return LV_SYMBOL_PLUS;
 if(strstr(text,"Excluir"))return LV_SYMBOL_TRASH;
 if(strstr(text,"Desbloquear"))return LV_SYMBOL_EYE_OPEN;
 if(strstr(text,"Bloquear")||strstr(text,"Cofre")||strstr(text,"COFRE"))return LV_SYMBOL_EYE_CLOSE;
 if(strstr(text,"Selecionar")||strstr(text,"Carregar"))return LV_SYMBOL_LIST;
 if(strstr(text,"SSH"))return LV_SYMBOL_KEYBOARD;
 if(strstr(text,"RDP"))return LV_SYMBOL_IMAGE;
 if(strstr(text,"Arquivo")||strstr(text,"ARQUIVOS"))return LV_SYMBOL_DIRECTORY;
 if(strstr(text,"Rede")||strstr(text,"REDE")||strstr(text,"WireGuard")||strstr(text,"Ativar VPN"))return LV_SYMBOL_WIFI;
 if(strstr(text,"Desconectar")||strstr(text,"Parar"))return LV_SYMBOL_POWER;
 if(strstr(text,"Fechar")||strstr(text,"Cancelar"))return LV_SYMBOL_CLOSE;
 if(strstr(text,"Voltar")||strstr(text,"acima"))return LV_SYMBOL_LEFT;
 if(strstr(text,"Salvar")||strstr(text,"Backup"))return LV_SYMBOL_SAVE;
 if(strstr(text,"Enviar")||strstr(text,"Exportar"))return LV_SYMBOL_UPLOAD;
 if(strstr(text,"Receber")||strstr(text,"Importar")||strstr(text,"Restaurar"))return LV_SYMBOL_DOWNLOAD;
 if(strstr(text,"Conectar")||strstr(text,"Abrir"))return LV_SYMBOL_RIGHT;
 if(strstr(text,"Confirmar")||strstr(text,"Confiar"))return LV_SYMBOL_OK;
 if(strstr(text,"Clipboard"))return LV_SYMBOL_COPY;
 if(strstr(text,"Sessoes"))return LV_SYMBOL_LIST;
 if(strstr(text,"Atualizar")||strstr(text,"Tentar")||strstr(text,"Recuperar"))return LV_SYMBOL_REFRESH;
 return LV_SYMBOL_SETTINGS;
}
// Keep the text as child zero: record/choice buttons update it in place.
inline lv_obj_t *button_content(lv_obj_t *button,const char *text) {
 int width=lv_obj_get_style_width(button,LV_PART_MAIN);
 auto *label=lv_label_create(button);lv_label_set_text(label,text);
 lv_obj_set_width(label,width>76?width-68:8);lv_label_set_long_mode(label,LV_LABEL_LONG_DOT);
 lv_obj_set_style_text_align(label,LV_TEXT_ALIGN_CENTER,0);
 lv_obj_align(label,LV_ALIGN_CENTER,12,0);
 auto *icon=lv_label_create(button);lv_label_set_text(icon,action_icon(text));
 lv_obj_set_style_text_font(icon,&lv_font_montserrat_20,0);
 lv_obj_add_flag(icon,LV_OBJ_FLAG_USER_1);
 lv_obj_align(icon,LV_ALIGN_LEFT_MID,0,0);
 if(width<150){
  int height=lv_obj_get_style_height(button,LV_PART_MAIN);
  lv_obj_set_style_text_font(label,&lv_font_montserrat_14,0);
  lv_obj_add_flag(label,LV_OBJ_FLAG_USER_1);
  if(height>=64){
   lv_obj_set_width(label,width-20);lv_obj_align(label,LV_ALIGN_BOTTOM_MID,0,0);
   lv_obj_align(icon,LV_ALIGN_TOP_MID,0,0);
  }else{
   lv_obj_set_width(label,width-44);lv_obj_align(label,LV_ALIGN_CENTER,10,0);
   lv_obj_set_style_text_font(icon,&lv_font_montserrat_14,0);
  }
 }
 if(lv_obj_get_style_height(button,LV_PART_MAIN)<64)
  lv_obj_set_height(label,lv_obj_get_style_text_font(label,0)->line_height);
 lv_obj_remove_flag(label,LV_OBJ_FLAG_CLICKABLE);lv_obj_remove_flag(icon,LV_OBJ_FLAG_CLICKABLE);
 return label;
}

}
