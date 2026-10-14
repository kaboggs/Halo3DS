/* New3DS integration additions by Kevin Boggs. */
/* Native controller UI uses a tiny shared font and one white texel, keeping
 * text at the LCD's resolution independently of the world rendering scale. */
static void *controls_white;
static struct native_text_vertex controls_panel_vertices[4*128];
static unsigned int controls_panel_quads;
/* Kevin Boggs: controls_panel_trapezoid - New3DS integration. */
static void controls_panel_trapezoid(float left_top,float right_top,float top,
 float left_bottom,float right_bottom,float bottom,unsigned int color)
{
 if(controls_panel_quads>=128)return;
 struct native_text_vertex *v=controls_panel_vertices+4*controls_panel_quads++;
 static const float pos[4][2]={{0,0},{1,0},{1,1},{0,1}};
 for(int i=0;i<4;++i){
  v[i].position[0]=pos[i][1]?(pos[i][0]?right_bottom:left_bottom):(pos[i][0]?right_top:left_top);
  v[i].position[1]=pos[i][1]?bottom:top;
  v[i].uv[0]=pos[i][0];v[i].uv[1]=pos[i][1];
  for(int c=0;c<4;++c)v[i].color[c]=((color>>(8*c))&255)/255.f;
 }
}
/* Kevin Boggs: controls_panel_rect - New3DS integration. */
static void controls_panel_rect(float x,float y,float w,float h,unsigned int color)
{controls_panel_trapezoid(x,x+w,y,x,x+w,y+h,color);}
/* Kevin Boggs: controls_panel_flush - New3DS integration. */
static void controls_panel_flush(void)
{
 if(!controls_panel_quads)return;
 if(!controls_white){unsigned int pixels[64];for(int i=0;i<64;++i)pixels[i]=~0u;controls_white=n3ds_gpu_texture_create(pixels,8,8);}
 if(controls_white){
  n3ds_gpu_texture_bind(0,controls_white);n3ds_gpu_text_begin(320,240,0xffffffff,1,0);
  C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,GPU_ZERO,GPU_ZERO,GPU_ONE);
  n3ds_gpu_text_draw_many(controls_panel_vertices,controls_panel_quads);
 }
 controls_panel_quads=0;
}
static void controls_quad(float x,float y,float w,float h,unsigned int color,int top)
{
 if(!controls_white){unsigned int pixels[64];for(int i=0;i<64;++i)pixels[i]=~0u;controls_white=n3ds_gpu_texture_create(pixels,8,8);}
 if(!controls_white)return;
 n3ds_gpu_texture_bind(0,controls_white);n3ds_gpu_text_begin(top?400:320,240,color,1,0);
 C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,GPU_ZERO,GPU_ZERO,GPU_ONE);
 struct native_text_vertex q[4]={{{x,y},{1,1,1,1},{0,0}},{{x+w,y},{1,1,1,1},{1,0}},{{x+w,y+h},{1,1,1,1},{1,1}},{{x,y+h},{1,1,1,1},{0,1}}};
 n3ds_gpu_text_draw(q);
}
static void controls_text(float x,float y,const char *text,unsigned int color,int top)
{
 status_text(NULL,0,1);if(!loading_font)return;
 n3ds_gpu_texture_bind(0,loading_font);n3ds_gpu_text_begin(top?400:320,240,color,1,0);
 for(const char *p=text;*p;++p,x+=8){unsigned int c=(unsigned char)*p;float u=(c%16)/16.f,v=(c/16)/8.f;
  struct native_text_vertex q[4]={{{x,y},{1,1,1,1},{u,v}},{{x+8,y},{1,1,1,1},{u+1.f/16,v}},{{x+8,y+8},{1,1,1,1},{u+1.f/16,v+1.f/8}},{{x,y+8},{1,1,1,1},{u,v+1.f/8}}};n3ds_gpu_text_draw(q);
 }
}
/* Kevin Boggs: controls_radar_edge - New3DS integration. */
static float controls_radar_edge(float y)
{float d=y-116.f;return 160.f-sqrtf(80.f*80.f-d*d);}
/* Kevin Boggs: controls_halo_touch_panel - New3DS integration. */
static void controls_halo_touch_panel(int right,int lower,int pressed,int blank)
{
 const unsigned int rim=pressed?0xffd7ec7e:0xffffb971;
 const unsigned int fill=pressed?0xff785038:0xff3e2812;
 const unsigned int ink=pressed?0xffecffd7:0xffe9cf9a;
 float top=lower?122.f:64.f,bottom=lower?164.f:114.f;
 /* Twelve connected trapezoids follow a circle outside the radar rim. The
  * inset layer leaves one continuous three-pixel border at the corners. */
 for(int inset=0;inset<=3;inset+=3){
  float a=top+inset,b=bottom-inset;
  for(int segment=0;segment<12;++segment){
   float ya=a+(b-a)*segment/12.f,yb=a+(b-a)*(segment+1)/12.f;
   float ea=controls_radar_edge(ya),eb=controls_radar_edge(yb);
   if(right)controls_panel_trapezoid(320.f-ea+inset,312.f-inset,ya,
       320.f-eb+inset,312.f-inset,yb,inset?fill:rim);
   else controls_panel_trapezoid(8.f+inset,ea-inset,ya,
       8.f+inset,eb-inset,yb,inset?fill:rim);
  }
 }
 float cy=(top+bottom)*.5f,edge=controls_radar_edge(cy);
 float cx=right?(320.f-edge+312.f)*.5f:(8.f+edge)*.5f;
 controls_panel_rect(right?320.f-controls_radar_edge(top+5)+12.f:20.f,
     top+5.f,right?312.f-(320.f-controls_radar_edge(top+5)+24.f):controls_radar_edge(top+5)-32.f,
     2.f,rim);
 if(blank){
  controls_panel_rect(cx-12,cy-1,8,2,ink);controls_panel_rect(cx+4,cy-1,8,2,ink);
  controls_panel_rect(cx-1,cy-11,2,7,ink);controls_panel_rect(cx-1,cy+4,2,7,ink);
  if(pressed)controls_panel_rect(cx-2,cy-2,4,4,ink);
 }
}
/* Kevin Boggs: n3ds_gpu_match_touch_overlay - New3DS integration. */
void n3ds_gpu_match_touch_overlay(int fps,int show_fps,int show_score,int pressed)
{
 void *saved=n3ds_gpu_texture_bound(0);n3ds_gpu_hud_screen(1);n3ds_gpu_hud_layout(0);
 controls_halo_touch_panel(0,0,pressed==1,1);
 controls_halo_touch_panel(1,0,pressed==2,1);
 controls_halo_touch_panel(0,1,pressed==3,1);
 controls_halo_touch_panel(1,1,pressed==4,show_score?0:1);
 controls_panel_flush();
 if(show_score){controls_text(253,132,"SCORE",0xfffff5e9,0);controls_text(257,146,"HOLD",0xffe9cf9a,0);}
 if(show_fps){char label[20];snprintf(label,sizeof(label),"FPS %02d",fps);
  controls_text(232,47,label,fps>=28?0xff7eecd7:fps>=20?0xffaad6e9:0xff7070ff,0);
 }
 n3ds_gpu_hud_screen(0);n3ds_gpu_texture_bind(0,saved);n3ds_gpu_world_state_restore();
}
/* Kevin Boggs: n3ds_gpu_match_scoreboard - New3DS integration. */
void n3ds_gpu_match_scoreboard(const struct native_score_row *rows,int count)
{
 /* Only the touch button calls this; SELECT uses the original upper-screen
  * scoreboard. Leave the lower touch controls visible during either path. */
 void *saved=n3ds_gpu_texture_bound(0);n3ds_gpu_hud_screen(0);n3ds_gpu_hud_layout(0);
 if(count<0)count=0;if(count>16)count=16;
 int height=62+count*10;
 controls_quad(12,12,376,height,0xffb97535,1);
 controls_quad(14,14,372,height-4,0xff221a12,1);
 controls_text(24,21,"MATCH SCORE",0xfff6dcaa,1);
 controls_quad(18,38,364,17,0xff403024,1);
 controls_text(24,43,"PLAYER",0xffa8d8ec,1);
 controls_text(184,43,"SCORE",0xffa8d8ec,1);
 controls_text(240,43,"K",0xffa8d8ec,1);
 controls_text(264,43,"D",0xffa8d8ec,1);
 controls_text(288,43,"K/D",0xffa8d8ec,1);
 controls_text(344,43,"MED",0xffa8d8ec,1);
 for(int i=0;i<count && i<16;++i){
  int ratio=(int)rows[i].kills*10/(rows[i].deaths>0?rows[i].deaths:1);
  if(ratio>999)ratio=999;
  int y=59+i*10;
  if(rows[i].local)controls_quad(18,y-2,364,10,0xff51462d,1);
  else if(i&1)controls_quad(18,y-2,364,10,0xff2d241b,1);
  unsigned int ink=rows[i].local?0xff6de5ff:0xffe8e4d5;
  char value[24];
  controls_text(24,y,rows[i].name,ink,1);
  snprintf(value,sizeof(value),"%5ld",rows[i].score);controls_text(184,y,value,ink,1);
  snprintf(value,sizeof(value),"%2d",rows[i].kills);controls_text(232,y,value,ink,1);
  snprintf(value,sizeof(value),"%2d",rows[i].deaths);controls_text(256,y,value,ink,1);
  snprintf(value,sizeof(value),"%2d.%1d",ratio/10,ratio%10);controls_text(288,y,value,ink,1);
  snprintf(value,sizeof(value),"%3d",rows[i].medals);controls_text(344,y,value,ink,1);
 }
 n3ds_gpu_hud_screen(0);n3ds_gpu_texture_bind(0,saved);n3ds_gpu_world_state_restore();
}
/* Kevin Boggs: n3ds_gpu_match_postgame_lobby - New3DS integration. */
void n3ds_gpu_match_postgame_lobby(void)
{
 void *saved=n3ds_gpu_texture_bound(0);n3ds_gpu_hud_screen(1);n3ds_gpu_hud_layout(0);
 controls_panel_rect(8,8,304,52,0xffb97535);
 controls_panel_rect(11,11,298,46,0xff221a12);
 controls_panel_flush();
 controls_text(88,17,"MATCH COMPLETE",0xfff6dcaa,0);
 controls_text(80,34,"RETURN TO LOBBY",0xff7eecd7,0);
 controls_text(120,47,"TAP OR B",0xffe9cf9a,0);
 n3ds_gpu_hud_screen(0);n3ds_gpu_texture_bind(0,saved);n3ds_gpu_world_state_restore();
}
void n3ds_gpu_controls_entry(void)
{
 void *saved=n3ds_gpu_texture_bound(0);n3ds_gpu_hud_screen(1);n3ds_gpu_hud_layout(0);
 controls_quad(24,202,272,30,0xff53391a,0);
 controls_text(32,214,"SELECT: Game settings",0xffffffff,0);
 n3ds_gpu_hud_screen(0);n3ds_gpu_texture_bind(0,saved);n3ds_gpu_world_state_restore();
}
void n3ds_gpu_controls_draw(const struct native_control_settings *s,int selected,int original,int save_error)
{
#ifdef HALO_N3DS_EXPERIMENTAL_PC_ARSENAL
 static unsigned int settings_draw_trace;
 if(settings_draw_trace++<3) n3ds_log("ARSENAL SETTINGS: draw called");
#endif
 void *saved=n3ds_gpu_texture_bound(0);n3ds_gpu_hud_layout(0);n3ds_gpu_hud_screen(0);
 controls_quad(0,0,400,240,0xff24150c,1);
 controls_text(128,70,"GAME SETTINGS",0xffffffff,1);
 controls_text(64,104,"Use the touch screen or D-pad.",0xffe9d6aa,1);
 controls_text(96,128,"Press START to save and return.",0xffe9d6aa,1);
 controls_text(48,168,s->xbox_buttons?"Xbox layout selected":"Nintendo layout selected",0xffffffff,1);
 controls_text(64,188,original?"L: fire  R: grenade  Up: zoom":s->swap_shoulders?"R: fire  L: grenade  ZR: zoom":"ZR: fire  ZL: grenade  R: zoom",0xffe9d6aa,1);
 controls_text(32,208,"Quality: richer combat effects and decals.",0xffe9d6aa,1);
 n3ds_gpu_hud_screen(1);controls_quad(0,0,320,240,0xff24150c,0);
 controls_text(108,16,"GAME SETTINGS",0xffffffff,0);
 const char *labels[]={"Right-stick aim","Touch aim","ABXY layout","Swap shoulders","PC textures","Swap X/Y","Swap A/B","Show FPS","Decals","Circle Pad Pro","Effects"};
 for(int row=0;row<n3ds_controls_row_count();++row){int field=n3ds_controls_row_field(row);
  unsigned int color=0xffffffff;float y=24+row*14;
  controls_quad(12,y,296,13,selected==row?0xff674826:0xff35251a,0);
  controls_text(20,y+3,labels[field],color,0);char value[32];
  if(field<2)snprintf(value,sizeof(value),"%u%%",field?s->touch:s->cstick);
  else snprintf(value,sizeof(value),"%s",field==2?(s->xbox_buttons?"Xbox":"Nintendo"):
      field==3?(s->swap_shoulders?"On":"Off"):
      field==4?(s->high_textures?"128+mip":"64"):
      field==5?(s->swap_xy?"On":"Off"):
      field==6?(s->swap_ab?"On":"Off"):
      field==7?(s->show_fps?"On":"Off"):
      field==8?(s->quality?(s->decals?"On":"Off"):"Disabled"):
      field==9?(s->circle_pad_pro?"On":"Off"):(s->quality?"Quality":"Balanced"));
  controls_text(164,y+3,value,color,0);
  controls_quad(242,y+1,28,11,0xff735333,0);controls_quad(274,y+1,28,11,0xff735333,0);
  controls_text(252,y+3,"-",color,0);controls_text(284,y+3,"+",color,0);
 }
 controls_text(16,178,"Performance (not available yet)",0xff807060,0);
 controls_text(16,190,save_error?"Save failed. START retry / B cancel":"Up/down: select  Left/right: change",save_error?0xff70a0ff:0xffe9d6aa,0);
 controls_quad(16,202,134,30,0xff53391a,0);controls_quad(170,202,134,30,0xff674826,0);
 controls_text(35,214,"Reset defaults",0xffffffff,0);controls_text(190,214,"Save + back",0xffffffff,0);
 n3ds_gpu_hud_screen(0);n3ds_gpu_texture_bind(0,saved);n3ds_gpu_world_state_restore();
}

/* Kevin Boggs: n3ds_gpu_local_match_menu - bottom-screen local match options. */
void n3ds_gpu_local_match_menu(const char *title,int bots,int difficulty,int weapons,int vehicles,int selected,int error)
{
 static const char *const labels[]={"Bots","Difficulty","Weapons","Vehicles"};
 static const char *const difficulties[]={"Easy","Normal","Hard"};
 static const char *const weapon_sets[]={"Normal","Pistols","Rifles","Plasma","Snipers","No sniping","Rockets","Shotguns","Short range","Human","None","Flamethrowers","Fuel Rods"};
 static const char *const vehicle_sets[]={"All","None","Warthogs","Ghosts","Scorpions"};
 void *saved=n3ds_gpu_texture_bound(0);n3ds_gpu_hud_layout(0);n3ds_gpu_hud_screen(1);
 controls_quad(0,0,320,240,0xff24150c,0);
 controls_text(64,12,"LOCAL MATCH SETTINGS",0xffffffff,0);
 controls_text(16,30,error?"Could not start. Retry or go back.":title && *title?title:"Selected gametype",error?0xff70a0ff:0xffe9d6aa,0);
 for(int row=0;row<4;++row){float y=48+row*28;char value[24];
  controls_quad(12,y,296,25,row==selected?0xff674826:0xff35251a,0);
  controls_text(20,y+9,labels[row],0xffffffff,0);
  if(row==0)snprintf(value,sizeof(value),"%d",bots);
  else snprintf(value,sizeof(value),"%s",row==1?difficulties[difficulty]:row==2?weapon_sets[weapons]:vehicle_sets[vehicles]);
  controls_text(116,y+9,value,0xffe9d6aa,0);
  controls_quad(240,y+2,30,21,0xff735333,0);controls_quad(274,y+2,30,21,0xff735333,0);
  controls_text(251,y+9,"-",0xffffffff,0);controls_text(285,y+9,"+",0xffffffff,0);
 }
 controls_quad(16,166,288,30,0xff53391a,0);
 controls_text(68,177,"X: Gear previews",0xffffffff,0);
 controls_quad(16,202,136,30,0xff53391a,0);controls_quad(168,202,136,30,0xff674826,0);
 controls_text(48,214,"B: Back",0xffffffff,0);controls_text(204,214,"A: Start",0xffffffff,0);
 n3ds_gpu_hud_screen(0);n3ds_gpu_texture_bind(0,saved);n3ds_gpu_world_state_restore();
}

#include "engine_pc_preview.h"
/* Kevin Boggs: n3ds_gpu_local_match_gear - browse equipment without replacing the top menu. */
void n3ds_gpu_local_match_gear(int map,int pc,int weapons,int vehicles,int selected)
{
 static const char *const weapon_sets[]={"Normal","Pistols","Rifles","Plasma","Snipers","No sniping","Rockets","Shotguns","Short range","Human","None","Flamethrowers","Fuel Rods"};
 static const char *const vehicle_sets[]={"All","None","Warthogs","Ghosts","Scorpions"};
 void *saved=n3ds_gpu_texture_bound(0);n3ds_gpu_hud_layout(0);n3ds_gpu_hud_screen(1);
 controls_quad(0,0,320,240,0xff24150c,0);
 if(map>=0)n3ds_gear_preview_draw(map,pc,selected,weapons,vehicles);
 n3ds_gpu_hud_screen(1);
 controls_quad(0,0,320,24,0xff24150c,0);controls_text(32,8,"GEAR: DRAG / L+R ZOOM",0xffffffff,0);
 controls_quad(0,144,320,96,0xff24150c,0);
 int empty=selected?vehicles==1:weapons==10;
 if(empty)controls_text(72,86,selected?"No vehicles":"No weapons",0xffffffff,0);
 else if(map<0)controls_text(64,86,"Preview unavailable",0xffffffff,0);
 else if(n3ds_preview_loading())controls_text(72,86,"Loading preview...",0xffffffff,0);
 else if(!n3ds_preview_available())controls_text(64,86,"Preview unavailable",0xffffffff,0);
 controls_text(48,134,"Example from selected set",0xffe9d6aa,0);
 for(int row=0;row<2;++row){float y=148+row*24;
  controls_quad(12,y,296,22,row==selected?0xff674826:0xff35251a,0);
  controls_text(20,y+7,row?"Vehicles":"Weapons",0xffffffff,0);
  controls_text(116,y+7,row?vehicle_sets[vehicles]:weapon_sets[weapons],0xffe9d6aa,0);
  controls_quad(240,y+1,30,20,0xff735333,0);controls_quad(274,y+1,30,20,0xff735333,0);
  controls_text(251,y+7,"-",0xffffffff,0);controls_text(285,y+7,"+",0xffffffff,0);
 }
 controls_quad(16,202,288,30,0xff53391a,0);controls_text(88,214,"A/B/X: Done",0xffffffff,0);
 n3ds_gpu_hud_screen(0);n3ds_gpu_texture_bind(0,saved);n3ds_gpu_world_state_restore();
}

/* Kevin Boggs: n3ds_gpu_map_browser - keep preview controls on the lower display. */
void n3ds_gpu_map_browser(int map,int pc,int ready)
{
    static const char *const names[]={"Battle Creek","Blood Gulch","Boarding Action","Derelict","Chill Out",
        "Damnation","Danger Canyon","Death Island","Gephyrophobia","Hang 'Em High","Ice Fields",
        "Infinity","Longest","Prisoner","Chiron TL-34","Rat Race","Sidewinder","Timberland","Wizard"};
    static const char *const views[]={"TEXTURED LIT","X-RAY","WIREFRAME","X-RAY + WIRE"};
    void *saved=n3ds_gpu_texture_bound(0);n3ds_gpu_hud_layout(0);n3ds_gpu_hud_screen(1);
    controls_quad(0,0,320,240,0xff24150c,0);
    if(ready){if(pc)n3ds_pc_preview_draw(map,0,0,0,0);else n3ds_xbox_preview_draw(map);}
    n3ds_gpu_hud_screen(1);
    controls_quad(0,0,320,24,0xff24150c,0);controls_text(16,8,names[map],0xffffffff,0);
    controls_quad(12,26,133,25,0xff674826,0);controls_text(20,35,views[n3ds_pc_preview_view_mode()],0xffffffff,0);
    controls_quad(152,26,89,25,0xff674826,0);controls_text(160,35,n3ds_pc_preview_backfaces_enabled()?"BACKS ON":"BACKS OFF",0xffffffff,0);
    controls_quad(248,26,63,25,0xff674826,0);controls_text(255,35,n3ds_pc_preview_nav_enabled()?"NAV ON":"NAV OFF",0xffffffff,0);
    controls_quad(0,180,320,60,0xff24150c,0);
    if(!ready || n3ds_preview_loading())controls_text(72,112,"Loading preview...",0xffffffff,0);
    else if(!n3ds_preview_available())controls_text(64,112,"Preview unavailable",0xffffffff,0);
    controls_text(24,189,"Drag: rotate    L/R: zoom",0xffe9d6aa,0);
    controls_text(24,210,"D-pad: map   A: select   B: back",0xffffffff,0);
    n3ds_gpu_hud_screen(0);n3ds_gpu_texture_bind(0,saved);n3ds_gpu_world_state_restore();
}

#ifdef HALO_N3DS_REMOTE_PILOT_DEV_MENU
#include "engine_link_font_data.inl"
static void *link_font_texture;
/* Kevin Boggs: link_font_prepare - New3DS integration. */
static int link_font_prepare(void)
{
 if(link_font_texture)return 1;
 unsigned int *pixels=malloc(128u*128u*sizeof(*pixels));
 if(!pixels)return 0;
 static const unsigned int spread[8]={0,1,4,5,16,17,20,21};
 for(unsigned int y=0;y<128;++y)for(unsigned int x=0;x<128;++x){
  unsigned int pixel=y*128+x,packed=link_font_alpha4[pixel/2];
  unsigned int alpha=((pixel&1)?packed&15:packed>>4)*17;
  unsigned int py=127-y;
  unsigned int at=((py/8)*16+x/8)*64+spread[x&7]+2*spread[py&7];
  /* GPU_RGBA8 stores alpha in the low byte. */
  pixels[at]=0xffffff00u|alpha;
 }
 link_font_texture=n3ds_gpu_texture_create(pixels,128,128);
 free(pixels);
 return link_font_texture!=NULL;
}
static unsigned int link_tint(unsigned int rgb,unsigned int alpha)
{return ((alpha&255)<<24)|((rgb&255)<<16)|(rgb&0xff00)|((rgb>>16)&255);}
/* Kevin Boggs: link_quad - New3DS integration. */
static void link_quad(float x,float y,float w,float h,unsigned int rgb,unsigned int alpha,int top)
{
 if(!controls_white){unsigned int pixels[64];for(int i=0;i<64;++i)pixels[i]=~0u;controls_white=n3ds_gpu_texture_create(pixels,8,8);}
 if(!controls_white || !alpha)return;
 n3ds_gpu_texture_bind(0,controls_white);
 n3ds_gpu_text_begin(top?400:320,240,link_tint(rgb,alpha),1,0);
 C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_SRC_ALPHA,GPU_ONE_MINUS_SRC_ALPHA,GPU_ONE,GPU_ONE_MINUS_SRC_ALPHA);
 struct native_text_vertex q[4]={{{x,y},{1,1,1,1},{0,0}},{{x+w,y},{1,1,1,1},{1,0}},{{x+w,y+h},{1,1,1,1},{1,1}},{{x,y+h},{1,1,1,1},{0,1}}};
 n3ds_gpu_text_draw(q);
}
/* Kevin Boggs: link_text - New3DS integration. */
static void link_text(float x,float y,const char *message,float size,unsigned int rgb,unsigned int alpha,int top)
{
 if(!alpha || !link_font_prepare())return;
 n3ds_gpu_texture_bind(0,link_font_texture);
 n3ds_gpu_text_begin(top?400:320,240,link_tint(rgb,alpha),1,0);
 C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_SRC_ALPHA,GPU_ONE_MINUS_SRC_ALPHA,GPU_ONE,GPU_ONE_MINUS_SRC_ALPHA);
 for(const char *p=message;*p;++p,x+=size*.82f){
  const char *found=strchr(link_font_glyphs,*p);
  unsigned int index=found?(unsigned int)(found-link_font_glyphs):0;
  float u=(index%8)/8.f,v=(index/8)/8.f;
  struct native_text_vertex q[4]={{{x,y},{1,1,1,1},{u,v}},{{x+size,y},{1,1,1,1},{u+.125f,v}},{{x+size,y+size},{1,1,1,1},{u+.125f,v+.125f}},{{x,y+size},{1,1,1,1},{u,v+.125f}}};
  n3ds_gpu_text_draw(q);
 }
}
/* Kevin Boggs: link_text_center - New3DS integration. */
static void link_text_center(float center,float y,const char *message,float size,unsigned int rgb,unsigned int alpha,int top)
{link_text(center-(float)strlen(message)*size*.82f*.5f,y,message,size,rgb,alpha,top);}
/* Kevin Boggs: n3ds_gpu_remote_pilot_dev_menu - New3DS integration. */
void n3ds_gpu_remote_pilot_dev_menu(int open,int status,const char *address,int action,int action_frames,int hud_visible)
{
 static const char *const labels[]={"NOT ARMED","WAITING FOR PC","PC AUTHENTICATED","RETRYING","LISTENER ERROR"};
 static const char *const hints[]={"PC LINK  OFFLINE","PC LINK  WAITING","PC LINK  ONLINE","PC LINK  RETRY","PC LINK  ERROR"};
 static const char *const badge[]={"","LINK","LIVE","RETRY","ERR"};
 if(status<0 || status>4)status=4;
 if(!address)address="offline";
 const unsigned int navy=0x07192d,panel=0x123958,card=0x0b2945;
 const unsigned int rim=0x4faad5,cyan=0x76dcfa,ink=0xd7f2ff;
 const unsigned int signal=status==2?0x8cf5e4:status==4?0xb8d8e8:cyan;
 void *saved=n3ds_gpu_texture_bound(0);
 n3ds_gpu_hud_layout(0);n3ds_gpu_hud_screen(1);
 if(!open){
  if(hud_visible && status!=0){
   n3ds_gpu_hud_screen(0);
   link_quad(332,4,64,20,navy,205,1);
   link_quad(332,4,2,20,rim,245,1);
   link_quad(338,11,6,6,signal,255,1);
   link_text_center(369,8,badge[status],11,signal,255,1);
   n3ds_gpu_hud_screen(1);
  }
  static unsigned int hint_start_ms;
  unsigned int now=(unsigned int)osGetTime();
  if(!hint_start_ms)hint_start_ms=now?now:1;
  unsigned int elapsed=now-hint_start_ms;
  float hint_alpha=elapsed<3800?1.f:elapsed<5200?(5200-elapsed)/1400.f:0.f;
  unsigned int opacity=(unsigned int)(fmaxf(0,fminf(1,hint_alpha))*255.f);
  if(opacity){
   link_quad(132,5,184,23,navy,opacity*3/4,0);
   link_quad(132,5,2,23,rim,opacity,0);
   link_text_center(224,9,hints[status],13,signal,opacity,0);
  }
 }else{
  n3ds_gpu_hud_screen(0);
  link_quad(0,0,400,240,navy,255,1);
  link_quad(18,17,364,206,rim,255,1);
  link_quad(20,19,360,202,panel,255,1);
  link_quad(20,19,360,3,cyan,255,1);
  link_quad(28,65,344,2,0x2c719b,255,1);
  link_text_center(200,36,"HALO3DS / PC LINK",16,ink,255,1);
  link_quad(34,74,332,66,card,255,1);
  link_quad(34,74,3,66,signal,255,1);
  link_text(48,81,labels[status],16,signal,255,1);
  char ip_line[32];snprintf(ip_line,sizeof(ip_line),"3DS IP: %s",address);
  for(char *p=ip_line;*p;++p)if(*p>='a' && *p<='z')*p-=32;
  link_text(48,106,ip_line,14,ink,255,1);
  if(status==0){
   link_text_center(200,151,"ARM LINK FILE ON SD BEFORE BOOT",13,ink,255,1);
   link_text_center(200,168,"RETRY CANNOT ARM THIS SESSION",13,cyan,255,1);
  }else if(status==2){
   link_text_center(200,151,"PAIRED PC AUTHENTICATED",13,ink,255,1);
   link_text_center(200,168,"OPEN REMOTE PILOT ON PC",13,cyan,255,1);
  }else{
   link_text_center(200,151,"CHECK 3DS WI-FI AND PC VIEWER",13,ink,255,1);
   link_text_center(200,168,"A REOPENS LOCAL LISTENER",13,cyan,255,1);
  }
  const char *feedback=action_frames>0?(action==1?"RETRYING LINK":action==2?"BUG IT QUEUED":action==3?"CLOSING":action==4?"ARM LINK FILE ON SD":action==5?"BUG IT NEEDS ARMED LINK":hud_visible?"TOP BADGE ON":"TOP BADGE OFF"):"A RETRY  Y BUG IT  X HUD  B BACK";
  link_text_center(200,195,feedback,12,action_frames>0?signal:ink,255,1);
  n3ds_gpu_hud_screen(1);
  link_quad(0,0,320,240,navy,255,0);
  link_quad(10,10,300,220,rim,255,0);
  link_quad(12,12,296,216,panel,255,0);
  link_quad(12,12,296,3,cyan,255,0);
  link_text_center(160,27,"REMOTE PILOT",16,ink,255,0);
  link_quad(22,61,276,78,card,255,0);
  link_quad(22,61,3,78,signal,255,0);
  link_text(30,67,"LINK STATUS",14,cyan,255,0);
  link_text(30,86,labels[status],15,signal,255,0);
  snprintf(ip_line,sizeof(ip_line),"IP: %s",address);
  for(char *p=ip_line;*p;++p)if(*p>='a' && *p<='z')*p-=32;
  link_text(30,108,ip_line,14,ink,255,0);
  const char *bottom_feedback=action==1?"RETRYING LINK":action==2?"BUG IT QUEUED":action==3?"CLOSING":action==4?"ARM LINK ON SD":action==5?"NEEDS ARMED LINK":hud_visible?"TOP BADGE ON":"TOP BADGE OFF";
  link_text_center(118,150,action_frames>0?bottom_feedback:"PC VIEWER SCANS",11,action_frames>0?signal:ink,255,0);
  link_quad(226,143,74,26,rim,255,0);
  link_quad(228,145,70,22,card,255,0);
  link_text_center(263,150,hud_visible?"HUD ON":"HUD OFF",11,cyan,255,0);
  int flash=(action_frames>0 && (action_frames/5)%2==0);
  link_quad(18,174,88,48,action==1||action==4?signal:cyan,255,0);
  link_quad(21,177,82,42,(action==1||action==4)&&flash?signal:0x205b82,255,0);
  link_quad(116,174,88,48,action==2||action==5?signal:cyan,255,0);
  link_quad(119,177,82,42,(action==2||action==5)&&flash?signal:0x205b82,255,0);
  link_quad(214,174,88,48,action==3?signal:rim,255,0);
  link_quad(217,177,82,42,action==3&&flash?signal:card,255,0);
  link_text_center(62,191,"A RETRY",13,ink,255,0);
  link_text_center(160,191,"Y BUGIT",13,ink,255,0);
  link_text_center(258,191,"B BACK",13,ink,255,0);
 }
 n3ds_gpu_hud_screen(0);n3ds_gpu_texture_bind(0,saved);n3ds_gpu_world_state_restore();
}
#endif
#ifdef HALO_N3DS_PC_LOCAL_MENU
#include "engine_pc_preview.h"
static void controls_text_center(float width,float y,const char *message,unsigned int color,int top)
{controls_text((width-(float)strlen(message)*8.f)*.5f,y,message,color,top);}
/* Kevin Boggs: n3ds_gpu_pc_local_menu - New3DS integration. */
void n3ds_gpu_pc_local_menu(int map,int mode,int bots,int difficulty,int weapon_set,int vehicle_choice,int page,int field,int error,int high_textures,int original)
{
 static const char *const maps[]={"Beaver Creek","Blood Gulch","Boarding Action","Derelict","Chill Out","Damnation","Danger Canyon","Death Island","Gephyrophobia","Hang 'Em High","Ice Fields","Infinity","Longest","Prisoner","Chiron TL-34","Rat Race","Sidewinder","Timberland","Wizard"};
 static const char *const modes[]={"Slayer","CTF","Oddball","King","Race"};
 static const char *const difficulties[]={"Easy","Normal","Hard"};
 static const char *const weapons[]={"Normal (PC map)","Pistols","Rifles","Plasma","Snipers","No sniping","Rockets","Shotguns","Short range","Human","None","Flamethrowers","Fuel Rods"};
 static const char *const vehicles[]={"Mode default","All PC vehicles","None","Warthog + Rocket","Ghost","Scorpion"};
 static const char *const views[]={"VIEW: TEX+LIT","VIEW: X-RAY","VIEW: WIRE","VIEW: X+WIRE"};
 char line[48];void *saved=n3ds_gpu_texture_bound(0);
 n3ds_pc_preview_draw(map,page,field,weapon_set,vehicle_choice);
 n3ds_gpu_hud_layout(0);n3ds_gpu_hud_screen(0);
 controls_quad(0,0,400,240,0xff24150c,1);
 if(page){
  controls_text_center(400,45,"MATCH EQUIPMENT",0xffffffff,1);
  controls_text_center(400,84,"Pick map weapon and vehicle rules.",0xffe9d6aa,1);
  controls_text_center(400,116,"Normal keeps PC map weapon pickups.",0xffe9d6aa,1);
  controls_text_center(400,157,"Drag to turn  L/R: zoom",0xffffffff,1);
  controls_text_center(400,179,"X: match setup  B: back",0xffffffff,1);
 }else{
  controls_text_center(400,23,"LOCAL MULTIPLAYER",0xffffffff,1);
  controls_text_center(400,52,"D-pad up/down: option   left/right: change",0xffe9d6aa,1);
  for(int row=1;row<(original?4:5);++row){
   float y=78+(row-1)*31;
   controls_quad(24,y,352,27,field==row?0xff805527:0xff35251a,1);
   if(row==1)snprintf(line,sizeof(line),"Mode: %s",modes[mode]);
   else if(row==2)snprintf(line,sizeof(line),"Bots: %d",bots);
   else if(row==3)snprintf(line,sizeof(line),"Difficulty: %s",difficulties[difficulty]);
   else snprintf(line,sizeof(line),"PC textures: %s",high_textures?"128 + mip":"64 standard");
   controls_text_center(400,y+8,line,0xffffffff,1);
  }
  controls_text_center(400,218,"SELECT: close   X: equipment   A: start",0xffe9d6aa,1);
 }
 n3ds_gpu_hud_screen(1);
 controls_quad(0,0,320,23,0xff24150c,0);
 controls_quad(0,22,320,1,0xffbd8142,0);
 controls_text_center(320,8,page?"MATCH EQUIPMENT":"LOCAL MULTIPLAYER",0xffffffff,0);
 if(!page){
  controls_quad(12,26,133,25,0xffbd8142,0);
  controls_quad(14,28,129,21,0xff694420,0);
  controls_text(21,35,views[n3ds_pc_preview_view_mode()],0xfffbd29b,0);
  controls_quad(152,26,89,25,0xffbd8142,0);
  controls_quad(154,28,85,21,n3ds_pc_preview_backfaces_enabled()?0xff694420:0xff302016,0);
  controls_text(161,35,n3ds_pc_preview_backfaces_enabled()?"BACKS ON":"BACKS OFF",0xfffbd29b,0);
  controls_quad(248,26,63,25,0xffbd8142,0);
  controls_quad(250,28,59,21,n3ds_pc_preview_nav_enabled()?0xff694420:0xff302016,0);
  controls_text(255,35,n3ds_pc_preview_nav_enabled()?"NAV ON":"NAV OFF",0xfffbd29b,0);
 }else if(field==0 && weapon_set==10)controls_text_center(320,81,"NO WEAPON",0xfffbd29b,0);
 else if(field==1 && vehicle_choice==2)controls_text_center(320,81,"NO VEHICLES",0xfffbd29b,0);
 if(page){
  controls_quad(0,144,320,96,0xff24150c,0);
  for(int row=0;row<2;++row){
   float y=148+row*24;controls_quad(12,y,296,22,field==row?0xff805527:0xff35251a,0);
   snprintf(line,sizeof(line),"%s: %s",row?"Vehicles":"Weapons",row?vehicles[vehicle_choice]:weapons[weapon_set]);
   controls_text_center(320,y+7,line,0xffffffff,0);
  }
  controls_text_center(320,193,"All adds Banshees to 3 maps",0xffe9d6aa,0);
 }else{
  controls_quad(0,180,320,60,0xff24150c,0);
  controls_quad(12,182,296,22,field==0?0xff805527:0xff35251a,0);
  snprintf(line,sizeof(line),"MAP %02d/19: %s  < >",map+1,maps[map]);
  controls_text_center(320,189,line,0xffffffff,0);
 }
 if(error)controls_text_center(320,page?193:171,"Save or launch failed",0xff70a0ff,0);
 controls_quad(16,205,136,30,0xffbd8142,0);controls_quad(18,207,132,26,0xff654321,0);
 controls_quad(168,205,136,30,0xffbd8142,0);controls_quad(170,207,132,26,0xff3c291a,0);
 controls_text(55,216,page?"A: DONE":"A: START",0xffffffff,0);controls_text(206,216,page?"X: BACK":"X: GEAR",0xffffffff,0);
 n3ds_gpu_hud_screen(0);n3ds_gpu_texture_bind(0,saved);n3ds_gpu_world_state_restore();
}
#endif
