/* NULLPOINT - an original, entirely procedural native 96K FPS.
   Build with build.ps1. Windows/OpenGL system DLLs only. */
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0602
#define CORE static __attribute__((sysv_abi))
#include <windows.h>
#include <mmsystem.h>
#include <compressapi.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PI 3.14159265358979323846
#define MW 35
#define MH 43
#define CELL 2.8f
#define MAXE 64
#define MAXP 384
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
#define CLAMP(a,b,c) (MIN(MAX(a,b),c))
typedef struct {float x,y,z;} V3;
typedef struct {V3 p; float yaw,hp,cd,phase,hit,gait,speed,fire,dead; int type,active;} Enemy;
typedef struct {V3 p,v; float life,maxlife,size; int type;} Particle;
typedef struct {V3 p,v; float life; int owner;} Bolt;
typedef struct {V3 p; float sx,sy,sz,hp; int kind,active;} Prop;
typedef struct {V3 p; int type,active;} Pickup;
typedef struct {V3 p,v;float fuse;} Grenade;
typedef struct {V3 p; int on;} Relay;
static HWND window;
static HDC dc;
static HGLRC rc;
static int width=1280,height=720,active=1,running=1,mode=0,fullscreen=0,mute=0,quality=1,testMode=0;
static int keys[256],pressed[256],mouseDown,mouseRight,focused=1;
static int map[MH][MW],seen[MH][MW],distmap[MH][MW];
static Enemy enemies[MAXE]; static int enemyCount;
static Particle parts[MAXP]; static int partIndex;
static Bolt bolts[96]; static int boltIndex;
static Prop props[256]; static int propCount;
static Pickup pickups[128]; static int pickupCount;
static Relay relays[3];
static Grenade grenadesLive[8];
static V3 velocity;static float eyeHeight=1.65f,grenadeCD,simCarry,jumpBuffer,coyote,swayX,swayY;
static int grenades=4,upgrades,evacSpawned;
static V3 player; static float yaw,pitch,vy,health,armor,gameTime,totalTime,damageFlash,hitFlash,gunKick,shootCD,reloadTime,stepPhase,shake,toastTime,extraction,gateLift;
static int weapon,ammo[3],reserve[3],kills,relayCount,bossAwake,bossDead,grounded,shotsFired,hitsMade,deaths,menuIndex=0,difficulty=1;
static const int magazine[3]={30,8,6};
static const char *weaponName[3]={"VX-30 / PULSE CARBINE","BR-8 / BREACH SHOTGUN","ARC-6 / ION LANCE"};
static char toast[128],lastError[4096];
static float mouseSensitivity=.0021f;
static unsigned rng=0x496f6e;
static GLuint worldList,worldProgram,postProgram,depthProgram,fbo,colorTex,depthTex,fontList,shadowFBO,shadowTex,sphereList,bloomFBO,bloomTex[2],bloomProgram;
static GLuint mirrorFBO,mirrorTex,mirrorDepth;
static int rw,rh;
static GLint worldU[16],postU[8],bloomU[3];
static float lastFrameMS=16;
static LARGE_INTEGER frequency,lastCounter;
static FILE *logFile;

#define GLFN(T,N,S) static T N;
#include "gl_api.h"
#undef GLFN
typedef BOOL (WINAPI *SwapIntervalFn)(int);
static SwapIntervalFn swapInterval;

CORE V3 v3(float x,float y,float z){V3 a={x,y,z};return a;}
CORE V3 add(V3 a,V3 b){return v3(a.x+b.x,a.y+b.y,a.z+b.z);}
CORE V3 sub(V3 a,V3 b){return v3(a.x-b.x,a.y-b.y,a.z-b.z);}
CORE V3 mul(V3 a,float t){return v3(a.x*t,a.y*t,a.z*t);}
CORE float dot(V3 a,V3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
CORE float length(V3 a){return (float)sqrtf(dot(a,a));}
CORE V3 norm(V3 a){float l=length(a);return mul(a,1/MAX(l,.0001f));}
CORE float rnd(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return (rng&65535)/65535.f;}
CORE float dsq(float x,float z){return x*x+z*z;}
CORE V3 cellpos(int x,int z){return v3((x+.5f)*CELL,0,(z+.5f)*CELL);}
CORE float decay(float t,float d){return MAX(0,t-d);}
CORE void note(const char *s){strncpy(toast,s,127);toast[127]=0;toastTime=4;}
CORE void logline(const char *s){if(logFile){fprintf(logFile,"%s\n",s);fflush(logFile);}}
CORE int solid(int x,int z){return x<0||x>=MW||z<0||z>=MH||map[z][x]==1||(map[z][x]==2&&relayCount<3);}
CORE int wallAt(float x,float z){return solid((int)floor(x/CELL),(int)floor(z/CELL));}
CORE int bodyCollision(float x,float z,float r,float feet,float h){
    if(wallAt(x-r,z-r)||wallAt(x+r,z-r)||wallAt(x-r,z+r)||wallAt(x+r,z+r))return 1;
    for(int i=0;i<propCount;i++){Prop *p=props+i;if(p->active&&feet<p->sy-.025f&&feet+h>0&&fabs(x-p->p.x)<p->sx*.5f+r&&fabs(z-p->p.z)<p->sz*.5f+r)return 1;}
    return 0;
}
CORE int collides(float x,float z,float r){return bodyCollision(x,z,r,0,1.9f);}
CORE void moveBody(V3 *p,float dx,float dz,float radius){
    int n=1+(int)(MAX(fabs(dx),fabs(dz))/.12f);dx/=n;dz/=n;
    float feet=p==&player?p->y-eyeHeight:p->y,h=p==&player?eyeHeight+.18f:1.9f;
    for(int i=0;i<n;i++){if(!bodyCollision(p->x+dx,p->z,radius,feet,h))p->x+=dx;if(!bodyCollision(p->x,p->z+dz,radius,feet,h))p->z+=dz;}
}
CORE int clearSight(V3 a,V3 b){V3 d=sub(b,a);float l=length(d);int n=(int)(l/.32f)+1;d=mul(d,1.f/n);for(int i=1;i<n;i++){a=add(a,d);if(wallAt(a.x,a.z))return 0;}return 1;}
CORE void carve(int x,int z,int w,int h){for(int j=z;j<z+h;j++)for(int i=x;i<x+w;i++)map[j][i]=0;}
CORE void prop(int x,int z,float sx,float sy,float sz,int kind){Prop *p=props+propCount++;p->p=cellpos(x,z);p->sx=sx;p->sy=sy;p->sz=sz;p->kind=kind;p->active=1;p->hp=50;}
CORE void pickup(float x,float z,int type){if(pickupCount<128){Pickup *p=pickups+pickupCount++;p->p=v3(x,.5,z);p->type=type;p->active=1;}}
CORE void spawn(int x,int z,int type){if(enemyCount>=MAXE)return;Enemy *e=enemies+enemyCount++;memset(e,0,sizeof(*e));e->p=cellpos(x,z);e->type=type;e->hp=type==3?1400:type==2?180:type==1?65:90;e->phase=rnd()*6.28f;e->cd=1+rnd()*2;}
CORE void buildMap(void){
    static const unsigned char rooms[][4]={{13,2,9,7},{16,9,3,4},{10,12,15,13},{8,17,3,3},{1,13,8,11},{24,17,3,3},{26,13,8,11},{16,25,3,5},{9,29,17,11},{16,40,3,2},{2,3,8,7},{9,5,5,3},{3,9,3,5},{25,3,8,7},{21,5,5,3},{29,9,3,5}};
    static const unsigned char fixtures[][6]={
      {14,5,18,14,18,0},{20,7,15,26,15,1},{13,15,18,40,18,1},{21,15,18,40,18,1},
      {13,21,18,40,18,1},{21,21,18,40,18,1},{17,18,36,55,36,2},{11,23,16,14,16,0},
      {23,13,16,14,16,0},{3,15,15,38,16,1},{6,15,15,38,16,1},{3,18,15,38,16,1},
      {28,15,18,16,18,0},{31,18,21,22,21,0},{28,22,13,28,13,1},{12,32,18,50,18,1},
      {22,32,18,50,18,1},{12,37,18,50,18,1},{22,37,18,50,18,1},
      {4,5,16,9,16,0},{7,7,16,28,16,1},{27,5,16,9,16,0},{31,7,16,28,16,1},
      {16,14,8,12,8,3},{19,22,8,12,8,3},{6,18,8,12,8,3},{29,19,8,12,8,3},
      {7,5,8,12,8,3},{6,5,8,12,8,3},{28,7,8,12,8,3},{29,7,8,12,8,3},{15,33,8,12,8,3},{20,37,8,12,8,3}};
    static const unsigned char guards[][3]={
      {17,11,0},{12,14,0},{22,14,1},{12,20,0},{22,21,2},{19,23,1},
      {5,14,1},{7,19,0},{2,21,0},{6,21,2},{29,14,0},{32,16,1},{27,20,0},{30,21,2},
      {15,31,0},{20,31,0},{17,35,3},{11,36,1},{23,36,1},
      {4,4,0},{8,4,1},{6,8,2},{29,4,1},{32,8,0},{26,8,2},{4,11,0},{30,11,0}};
    static const unsigned char supplies[][3]={{20,4,0},{14,8,1},{11,16,2},{23,22,1},{2,16,0},{7,23,1},{32,14,2},{27,23,0},{16,27,1},{10,30,0},{24,38,0},{3,8,0},{26,4,1},{8,8,3},{31,4,3},{10,38,3}};
    for(int j=0;j<MH;j++)for(int i=0;i<MW;i++)map[j][i]=1;
    for(unsigned i=0;i<sizeof(rooms)/4;i++)carve(rooms[i][0],rooms[i][1],rooms[i][2],rooms[i][3]);
    for(int i=16;i<=18;i++)map[28][i]=2;propCount=pickupCount=enemyCount=0;
    for(unsigned i=0;i<sizeof(fixtures)/6;i++){const unsigned char *p=fixtures[i];prop(p[0],p[1],p[2]*.1f,p[3]*.1f,p[4]*.1f,p[5]);}
    relays[0].p=cellpos(5,22);relays[1].p=cellpos(30,22);relays[2].p=cellpos(17,24);for(int i=0;i<3;i++)relays[i].on=0;
    for(unsigned i=0;i<sizeof(guards)/3;i++)spawn(guards[i][0],guards[i][1],guards[i][2]);
    for(unsigned i=0;i<sizeof(supplies)/3;i++){V3 p=cellpos(supplies[i][0],supplies[i][1]);pickup(p.x,p.z,supplies[i][2]);}
}
CORE void resetGame(void){
    velocity=v3(0,0,0);eyeHeight=1.65f;simCarry=grenadeCD=jumpBuffer=coyote=swayX=swayY=0;grenades=4;upgrades=evacSpawned=0;memset(grenadesLive,0,sizeof(grenadesLive));
    rng=0x496f6e;relayCount=bossAwake=bossDead=kills=shotsFired=hitsMade=0;
    memset(parts,0,sizeof(parts));memset(bolts,0,sizeof(bolts));memset(seen,0,sizeof(seen));
    buildMap();player=cellpos(17,4);player.y=1.65f;yaw=0;pitch=0;vy=0;health=100;armor=50;
    for(int i=0;i<3;i++){ammo[i]=magazine[i];reserve[i]=i==0?180:i==1?40:24;}
    weapon=0;grounded=1;gameTime=damageFlash=hitFlash=gunKick=shootCD=reloadTime=extraction=gateLift=0;
    note("REACTOR OFFLINE. SEVER ALL THREE RELAYS.");
}

#include "build/shaders.h"
/* UVs travel from the vertex shader; no inverse matrices per fragment. */
CORE void color(float r,float g,float b,int mat){glColor4f(r,g,b,(float)mat);}
/* Indexed cube corners keep the generator small without per-box matrix calls. */
CORE void box(float x,float y,float z,float sx,float sy,float sz){
    static const unsigned char faces[24]={0,2,3,1,5,7,6,4,4,6,2,0,1,3,7,5,2,6,7,3,4,0,1,5};
    static const signed char normals[18]={0,0,-1,0,0,1,-1,0,0,1,0,0,0,1,0,0,-1,0};
    glBegin(GL_QUADS);for(int f=0;f<6;f++){glNormal3f(normals[f*3],normals[f*3+1],normals[f*3+2]);for(int j=0;j<4;j++){int k=faces[f*4+j];glVertex3f(x+sx*((k&1)?.5f:-.5f),y+sy*((k&2)?.5f:-.5f),z+sz*((k&4)?.5f:-.5f));}}glEnd();
}
/* Chamfered boxes: six inset faces, twelve edge strips, eight corner triangles. */
CORE void bevelBox(float x,float y,float z,float sx,float sy,float sz){
    float half[3]={sx*.5f,sy*.5f,sz*.5f},center[3]={x,y,z};float b=MIN(.06f,MIN(sx,MIN(sy,sz))*.15f);static const int order[4]={0,1,3,2};
    glBegin(GL_QUADS);
    for(int axis=0;axis<3;axis++){int u=(axis+1)%3,v=(axis+2)%3;for(int sign=-1;sign<=1;sign+=2){float n[3]={0,0,0};n[axis]=sign;glNormal3f(n[0],n[1],n[2]);for(int j=0;j<4;j++){float p[3]={0};p[axis]=sign*half[axis];p[u]=(order[sign>0?j:3-j]&1?1:-1)*(half[u]-b);p[v]=(order[sign>0?j:3-j]&2?1:-1)*(half[v]-b);glVertex3f(center[0]+p[0],center[1]+p[1],center[2]+p[2]);}}
        for(int a=-1;a<=1;a+=2)for(int c=-1;c<=1;c+=2){float n[3]={0};n[u]=a*.7071f;n[v]=c*.7071f;glNormal3f(n[0],n[1],n[2]);for(int j=0;j<4;j++){float p[3];int q=order[a*c>0?3-j:j];p[axis]=(q&1?1:-1)*(half[axis]-b);p[u]=a*(half[u]-(q&2?b:0));p[v]=c*(half[v]-(q&2?0:b));glVertex3f(center[0]+p[0],center[1]+p[1],center[2]+p[2]);}}
    }glEnd();glBegin(GL_TRIANGLES);
    for(int k=0;k<8;k++){float n[3]={(k&1)?1:-1,(k&2)?1:-1,(k&4)?1:-1};glNormal3f(n[0]*.57735f,n[1]*.57735f,n[2]*.57735f);for(int j=0;j<3;j++){int t=n[0]*n[1]*n[2]<0?2-j:j;glVertex3f(x+n[0]*(half[0]-(t==0?0:b)),y+n[1]*(half[1]-(t==1?0:b)),z+n[2]*(half[2]-(t==2?0:b)));}}glEnd();
}
CORE void tube(float x,float y,float z,float radius,float h,int sides){
    glBegin(GL_QUAD_STRIP);for(int i=0;i<=sides;i++){float a=i*2*PI/sides,c=cosf(a),s=sinf(a);glNormal3f(c,0,s);glVertex3f(x+c*radius,y-h*.5f,z+s*radius);glVertex3f(x+c*radius,y+h*.5f,z+s*radius);}glEnd();
    for(int k=-1;k<=1;k+=2){glBegin(GL_TRIANGLE_FAN);glNormal3f(0,k,0);glVertex3f(x,y+k*h*.5f,z);for(int i=0;i<=sides;i++){float a=-k*i*2*PI/sides;glVertex3f(x+cosf(a)*radius,y+k*h*.5f,z+sinf(a)*radius);}glEnd();}
}
CORE void ring(float x,float y,float z,float radius,float thick,int sides){
    glBegin(GL_QUADS);for(int i=0;i<sides;i++){float a=i*2*PI/sides,b=(i+1)*2*PI/sides;glNormal3f(0,1,0);glVertex3f(x+cosf(a)*(radius-thick),y,z+sinf(a)*(radius-thick));glVertex3f(x+cosf(b)*(radius-thick),y,z+sinf(b)*(radius-thick));glVertex3f(x+cosf(b)*radius,y,z+sinf(b)*radius);glVertex3f(x+cosf(a)*radius,y,z+sinf(a)*radius);}glEnd();
}
CORE void beam(V3 a,V3 b,float radius){V3 d=sub(b,a);float l=length(d);if(l<.001f){box(a.x,a.y,a.z,radius*2,radius*2,radius*2);return;}glPushMatrix();glTranslatef(a.x,a.y,a.z);glRotatef(acosf(CLAMP(d.y/l,-1,1))*180/PI,d.z,0,-d.x);tube(0,l*.5f,0,radius,l,8);glPopMatrix();}
CORE float ceiling(int x,int z){if(z>=29)return 10; if(x>=10&&x<=24&&z>=12&&z<=24)return 8.4;return 5.6;}
CORE void sign3d(float x,float y,float z,float angle,float scale,const char *s){
    glPushMatrix();glTranslatef(x,y,z);glRotatef(angle,0,1,0);glScalef(scale,scale,scale);glListBase(fontList-32);glCallLists(strlen(s),GL_UNSIGNED_BYTE,s);glPopMatrix();
}
CORE void makeWorld(void){
    worldList=glGenLists(1);glNewList(worldList,GL_COMPILE);
    for(int z=0;z<MH;z++)for(int x=0;x<MW;x++)if(map[z][x]!=1){
        float xx=(x+.5f)*CELL,zz=(z+.5f)*CELL,h=ceiling(x,z);
        color(.26,.31,.33,1);box(xx,-.16,zz,CELL,.3,CELL);
        color(.12,.155,.17,0);box(xx,h+.16,zz,CELL,.3,CELL);
        for(int d=0;d<4;d++){
            int nx=x+(d==0)-(d==1),nz=z+(d==2)-(d==3);
            if(nx<0||nz<0||nx>=MW||nz>=MH||map[nz][nx]!=1)continue;
            glPushMatrix();glTranslatef(xx,0,zz);glRotatef(d==0?90:d==1?-90:d==2?0:180,0,1,0);
            color(.3,.34,.35,0);box(0,h/2,CELL/2+.12f,CELL,h,.26);
            color(.09,.12,.135,3);box(-1.3,h/2,1.24,.17,h,.31);box(1.3,h/2,1.24,.17,h,.31);
            color(.21,.24,.25,3);box(0,.35,1.23,CELL,.5,.26);box(0,h-.38,1.15,CELL,.4,.45);
            color(.7,.36,.07,2);box(0,.67,1.18,2.5,.12,.08);
            if((x+z)%3==0){
                color(.052,.09,.105,3);box(0,2.15,1.14,1.9,1.5,.2);
                color(.32,.4,.42,3);for(int k=0;k<7;k++)box(0,1.56+k*.19,1.01,1.7,.055,.13);
                color(.02,.7,.82,5);box(-1.13,2.5,1.05,.035,1.8,.045);
            }else{
                color(.15,.19,.2,3);tube(.96,h*.53,1.03,.09,h*.85,8);
                color(.58,.27,.05,3);for(int k=1;k<h;k+=2)tube(.96,k,1.03,.13,.09,8);
                color(.32,.4,.41,0);box(-.19,h*.45,1.16,1.52,h*.46,.07);
            }
            color(.7,.85,.8,5);box(0,h-.55,.97,.95,.065,.14);
            color(.015,.38,.46,5);box(0,.12,1.04,1.95,.035,.07);
            glPopMatrix();
        }
        if((x%3==1)&&(z%3==0)){
            color(.11,.15,.17,3);box(xx,h-.13,zz,CELL,.22,.25);
            color(.6,.76,.74,5);box(xx,h-.28,zz,1.1,.04,.15);
        }
        if(x==16||x==18){color(.64,.32,.05,2);box(xx,.009,zz,.1,.012,CELL);}
    }
    for(int i=0;i<propCount;i++){
        Prop *p=props+i;if(p->kind==3)continue;float x=p->p.x,z=p->p.z,h=p->sy;
        if(p->kind==0){
            color(.3,.33,.28,0);bevelBox(x,h/2,z,p->sx,h,p->sz);
            color(.63,.32,.055,2);box(x,h*.7,z-p->sz*.5f-.01,p->sx*.96,.28,.03);
            color(.1,.14,.15,3);for(int k=-1;k<=1;k+=2){box(x+k*p->sx*.35,h/2,z,.09,h+.06,p->sz+.09);box(x,h/2,z+k*p->sz*.37,p->sx+.09,h+.06,.09);}
        }else if(p->kind==1){
            color(.11,.16,.18,3);bevelBox(x,h/2,z,p->sx,h,p->sz);
            color(.3,.35,.36,0);box(x,h/2,z,p->sx+.18,h*.86,p->sz*.65);
            for(int side=-1;side<=1;side+=2){
                color(.05,.07,.08,3);box(x,h*.58,z+side*(p->sz*.5f+.015),p->sx*.76,h*.65,.05);
                for(int k=0;k<9;k++){color(.17,.23,.25,3);box(x,.45+k*(h-.8)/9,z+side*(p->sz*.5f+.05),p->sx*.72,.07,.06);
                    color(.05,.65,(k%3)*.25+.15,5);box(x-p->sx*.24,.5+k*(h-.8)/9,z+side*(p->sz*.5f+.09),.06,.035,.02);}
            }
            color(.03,.6,.78,5);box(x-p->sx*.51,h/2,z,.05,h*.73,.09);
            color(.5,.25,.03,2);box(x,.15,z,p->sx+.3,.28,p->sz+.3);
        }else{
            color(.13,.18,.2,3);tube(x,.4,z,2.5,.8,16);tube(x,5.2,z,2.2,.65,16);
            color(.24,.3,.31,0);tube(x,2.8,z,1.25,4.4,16);
            for(int j=0;j<8;j++){float a=j*PI/4;float px=x+sinf(a)*1.85,pz=z+cosf(a)*1.85;
                color(.21,.27,.28,3);tube(px,2.8,pz,.13,4.4,8);color(.07,.8,.95,5);tube(px,2.8,pz,.066,3.4,8);
                color(.55,.3,.06,2);beam(v3(px,4.8,pz),v3(x+sinf(a)*.9,5.8,z+cosf(a)*.9),.18);
            }
            color(.055,.9,1,5);for(int k=0;k<4;k++)tube(x,1.3+k,z,1.3,.08,24);
        }
    }
    /* Load-bearing ceiling arches, conduits, and repeated gantries. */
    for(int z=13;z<25;z+=4){
        float zz=(z+.5f)*CELL;color(.115,.155,.17,3);box(17.5*CELL,7.9,zz,15*CELL,.48,.45);
        for(int side=-1;side<=1;side+=2){float xx=(17.5+side*6.5)*CELL;
            box(xx,3.9,zz,.36,7.8,.55);beam(v3(xx,5.6,zz),v3(xx-side*3.1,7.9,zz),.24);
            color(.75,.3,.055,2);box(xx,1.1,zz,.4,.7,.59);color(.115,.155,.17,3);
        }
    }
    for(int x=15;x<=19;x+=2){color(.19,.23,.24,3);beam(v3(x*CELL,7.6,12*CELL),v3(x*CELL,7.6,25*CELL),.13);}
    for(int z=30;z<40;z+=3){color(.2,.23,.25,3);box(17.5*CELL,9.5,z*CELL,17*CELL,.5,.6);
        for(int x=10;x<26;x+=3){color(.1,.5,.7,5);box(x*CELL,9.17,z*CELL,1.4,.05,.19);}}
    for(int side=-1;side<=1;side+=2){float x=(17.5f+side*5.8f)*CELL;
        color(.07,.12,.14,3);box(x,6.45,18.5f*CELL,.8,.18,34);
        for(int z=13;z<25;z++){float zz=(z+.5f)*CELL;color(.21,.26,.27,3);box(x,6.8,zz,.065,.6,.065);box(x,6.45,zz,1.04,.07,.08);}
        for(int k=0;k<3;k++){color(.16,.22,.24,3);beam(v3(x+(k-1)*.22f,6.62,13*CELL),v3(x+(k-1)*.22f,6.62,24*CELL),.064);}
    }
    V3 e=cellpos(17,41);color(.025,.8,.6,5);box(e.x,.015,e.z,7.8,.02,3.8);
    color(.52,.67,.62,5);sign3d(18.85*CELL,3.2,8.98*CELL,180,.62,"REACTOR 09");
    color(.03,.48,.7,5);sign3d(9.98*CELL,4.1,16.98*CELL,90,.62,"01 / COOLANT");
    color(.8,.32,.05,5);sign3d(25.02*CELL,4.1,20.02*CELL,-90,.62,"02 / FOUNDRY");
    color(.7,.36,.065,5);sign3d(18.9*CELL,4.8,27.99*CELL,180,.6,"CONTAINMENT");
    color(.03,.75,.45,5);sign3d(18.7*CELL,3.4,41.97*CELL,180,.62,"EXTRACTION");
    color(.03,.65,.75,5);sign3d(13.03f*CELL,3.1f,8.2f*CELL,90,.48f,"SERVICE / OVERCLOCK");
    color(.7,.3,.08,5);sign3d(21.97f*CELL,3.1f,4.6f*CELL,-90,.48f,"ARMORY / OVERCLOCK");
    glEndList();
}

/* Small streaming synthesizer. All buffers persist until waveOut returns them. */
typedef struct {int kind;float t,duration,volume,pan;} Voice;
static Voice voices[24];static int voiceIndex;
static HWAVEOUT audioOut;static WAVEHDR audioHeaders[4];static short audioData[4][1024*2];
static double audioTime;static float bassPhase;static unsigned soundRng=173;
CORE void sound(int kind,float vol,float pan){if(!audioOut)return;Voice *v=voices+(voiceIndex++%24);v->kind=kind;v->t=0;v->duration=kind==0?.17f:kind==1?.37f:kind==2?.6f:kind==3?.8f:kind==4?.08f:kind==5?.5f:1.2f;v->volume=vol;v->pan=CLAMP(pan,-1,1);}
CORE void initAudio(void){
    WAVEFORMATEX f={0};f.wFormatTag=WAVE_FORMAT_PCM;f.nChannels=2;f.nSamplesPerSec=22050;f.wBitsPerSample=16;f.nBlockAlign=4;f.nAvgBytesPerSec=f.nSamplesPerSec*4;
    if(waveOutOpen(&audioOut,WAVE_MAPPER,&f,0,0,CALLBACK_NULL)!=MMSYSERR_NOERROR){audioOut=0;return;}
    for(int i=0;i<4;i++){audioHeaders[i].lpData=(char*)audioData[i];audioHeaders[i].dwBufferLength=sizeof(audioData[i]);waveOutPrepareHeader(audioOut,audioHeaders+i,sizeof(WAVEHDR));audioHeaders[i].dwFlags|=WHDR_DONE;}
}
CORE void pumpAudio(void){
    if(!audioOut)return;
    for(int b=0;b<4;b++)if(audioHeaders[b].dwFlags&WHDR_DONE){
        for(int i=0;i<1024;i++){
            float l=0,r=0;double t=audioTime;audioTime+=1./22050;
            if(mode==1||mode==0){
                float beat=fmod(t,60./94), bar=fmod(t,60./94*16);int step=(int)(t/(60./94))%16;
                const float notes[8]={55,55,65.406f,55,49,49,41.203f,49};float f=notes[((int)(t/(60./94*2)))%8];
                bassPhase+=f/22050;if(bassPhase>1)bassPhase-=1;
                float env=exp(-beat*6),kick=sin(2*PI*(48*beat+1.5*(1-exp(-beat*35))))*exp(-beat*23);
                soundRng=soundRng*1664525u+1013904223u;float noise=((soundRng>>16)&65535)/32768.f-1;
                float snare=(step%4==2?noise*exp(-beat*23)*.035f:0);
                float hat=noise*exp(-fmod(t,60./188)*65)*.009f;
                float drone=(sin(t*2*PI*55)+sin(t*2*PI*82.41)+sin(t*2*PI*110.15))*.006f;
                float music=((bassPhase*2-1)*env*.024f+kick*.07f+snare+hat+drone)*(mode==0?.7f:1.f);
                l=r=music;(void)bar;
            }
            for(int k=0;k<24;k++)if(voices[k].t<voices[k].duration){
                Voice *v=voices+k;float u=v->t,env=1-u/v->duration,s=0;v->t+=1.f/22050;
                soundRng=soundRng*1664525u+1013904223u;float n=((soundRng>>16)&65535)/32768.f-1;
                if(v->kind==0)s=(n*.6f+sin(u*(1800-u*2200))*.4f)*env*env;
                if(v->kind==1)s=(n*.7f+sin(u*(600-u*600))*.5f)*env*env;
                if(v->kind==2)s=(sin(u*(4500-u*2800))+sin(u*8900)*.3f)*env*.65f;
                if(v->kind==3)s=(n*.5f+sin(u*(240-u*90))*.8f)*env*env;
                if(v->kind==4)s=n*env*env*.45f;
                if(v->kind==5)s=sin(u*2*PI*(u<.15?660:u<.3?880:1320))*env*.45f;
                if(v->kind==6)s=(sin(u*510)+sin(u*607))*env*.4f;
                s*=v->volume*.24f;l+=s*(1-v->pan*.6f);r+=s*(1+v->pan*.6f);
            }
            audioData[b][i*2]=(short)(CLAMP(l,-.9,.9)*32767*(mute?0:1));audioData[b][i*2+1]=(short)(CLAMP(r,-.9,.9)*32767*(mute?0:1));
        }
        waveOutWrite(audioOut,audioHeaders+b,sizeof(WAVEHDR));
    }
}
CORE void stopAudio(void){if(audioOut){waveOutReset(audioOut);for(int i=0;i<4;i++)waveOutUnprepareHeader(audioOut,audioHeaders+i,sizeof(WAVEHDR));waveOutClose(audioOut);audioOut=0;}}
CORE void particles(V3 p,int count,int type,float strength){
    for(int i=0;i<count;i++){Particle *s=parts+(partIndex++%MAXP);s->p=p;s->v=v3((rnd()-.5f)*strength,rnd()*strength,(rnd()-.5f)*strength);s->life=s->maxlife=.18f+rnd()*.8f;s->size=.018+rnd()*.04;s->type=type;}
}
CORE void hurtPlayer(float amount){
    if(mode!=1)return;amount*=difficulty==0?.55f:difficulty==2?1.4f:1;
    float absorb=MIN(armor,amount*.6f);armor-=absorb;health-=amount-absorb;damageFlash=.7f;shake=.13f;sound(4,.5,0);
    if(health<=0){health=0;mode=3;deaths++;mouseDown=0;ClipCursor(NULL);while(ShowCursor(TRUE)<0);sound(6,.8,0);}
}
CORE float rayBox(V3 o,V3 d,V3 lo,V3 hi){
    float tmin=0,tmax=200;float aa[3]={o.x,o.y,o.z},dd[3]={d.x,d.y,d.z},ll[3]={lo.x,lo.y,lo.z},hh[3]={hi.x,hi.y,hi.z};
    for(int i=0;i<3;i++){if(fabs(dd[i])<1e-6){if(aa[i]<ll[i]||aa[i]>hh[i])return 200;}else{float a=(ll[i]-aa[i])/dd[i],b=(hh[i]-aa[i])/dd[i];if(a>b){float t=a;a=b;b=t;}tmin=MAX(tmin,a);tmax=MIN(tmax,b);if(tmin>tmax)return 200;}}
    return tmin;
}
CORE float traceWorld(V3 o,V3 d,float maxdist){
    float dist=maxdist;
    for(float t=.1;t<maxdist;t+=.14f){V3 p=add(o,mul(d,t));if(wallAt(p.x,p.z)||p.y<.03||p.y>ceiling((int)(p.x/CELL),(int)(p.z/CELL))){dist=t;break;}}
    for(int i=0;i<propCount;i++){Prop *p=props+i;if(!p->active)continue;float t=rayBox(o,d,v3(p->p.x-p->sx/2,0,p->p.z-p->sz/2),v3(p->p.x+p->sx/2,p->sy,p->p.z+p->sz/2));dist=MIN(dist,t);}
    return dist;
}
CORE V3 forward(void){return v3(sinf(yaw)*cosf(pitch),sinf(pitch),cosf(yaw)*cosf(pitch));}
CORE void look(float dx,float dy){swayX=CLAMP(swayX+dx*.00025f,-.045f,.045f);swayY=CLAMP(swayY+dy*.00025f,-.04f,.04f);yaw-=dx*mouseSensitivity;pitch=CLAMP(pitch-dy*mouseSensitivity,-1.4f,1.4f);}
CORE void damageEnemy(int index,float damage,V3 impact){
    Enemy *e=enemies+index;e->hp-=damage;e->hit=.16;e->active=1;hitFlash=.15f;hitsMade++;particles(impact,7,0,2.5);
    if(e->hp<=0){e->dead=gameTime;kills++;particles(add(e->p,v3(0,1,0)),35,1,7);sound(3,.75f,CLAMP((e->p.x-player.x)*.02,-.8,.8));
        if(e->type==3){bossDead=1;note("WARDEN DOWN. REACH EXTRACTION AND HOLD THE UPLINK.");sound(5,.9,0);}
        else if(rnd()<.5f)pickup(e->p.x,e->p.z,rnd()<.3f?0:1);
    }
}
CORE void blast(V3 p,float radius,float power){
    particles(p,55,0,8);sound(3,.9f,CLAMP((p.x-player.x)*.03f,-1,1));
    for(int i=0;i<enemyCount;i++){Enemy *e=enemies+i;V3 center=add(e->p,v3(0,e->type==3?1.6f:1,0));float d=length(sub(center,p));if(e->hp>0&&d<radius&&clearSight(p,center))damageEnemy(i,power*(1-d/radius),center);}
    V3 away=sub(player,p);float d=length(away);if(d<radius&&clearSight(p,player)){hurtPlayer(power*.24f*(1-d/radius));away=mul(norm(away),5*(1-d/radius));velocity=add(velocity,away);vy+=MAX(0,away.y);}
    for(int i=0;i<propCount;i++){Prop *q=props+i;if(q->kind==3&&q->active&&length(sub(add(q->p,v3(0,.6f,0)),p))<radius&&clearSight(p,add(q->p,v3(0,.6f,0)))){q->active=0;blast(add(q->p,v3(0,.65f,0)),5.5f,170);}}
}
CORE void throwGrenade(void){
    if(grenadeCD>0||grenades<=0)return;
    for(int i=0;i<8;i++)if(grenadesLive[i].fuse<=0){Grenade *g=grenadesLive+i;V3 f=forward();g->p=add(player,mul(f,MAX(.05f,MIN(.65f,traceWorld(player,f,.8f)-.1f))));g->v=add(velocity,add(mul(f,10),v3(0,2.5,0)));g->fuse=2.1f;grenades--;grenadeCD=.5f;sound(4,.3,0);return;}
}
CORE void stepGrenades(float dt){
    grenadeCD=decay(grenadeCD,dt);
    for(int i=0;i<8;i++){Grenade *g=grenadesLive+i;if(g->fuse<=0)continue;g->fuse-=dt;
        if(g->fuse<=0){blast(g->p,5.5f,180*(1+upgrades*.15f));continue;}
        int n=1+(int)(length(g->v)*dt/.1f);float h=dt/n;
        for(int k=0;k<n;k++){
            float nx=g->p.x+g->v.x*h,nz=g->p.z+g->v.z*h;
            if(bodyCollision(nx,g->p.z,.11f,g->p.y-.11f,.22f))g->v.x*=-.6f;else g->p.x=nx;
            if(bodyCollision(g->p.x,nz,.11f,g->p.y-.11f,.22f))g->v.z*=-.6f;else g->p.z=nz;
            g->v.y-=18*h;float ny=g->p.y+g->v.y*h,base=.12f;
            for(int j=0;j<propCount;j++){Prop *q=props+j;if(q->active&&g->p.y>=q->sy+.08f&&fabs(g->p.x-q->p.x)<q->sx/2+.1f&&fabs(g->p.z-q->p.z)<q->sz/2+.1f)base=MAX(base,q->sy+.12f);}
            if(ny<base&&g->v.y<0){g->p.y=base;g->v.y=fabs(g->v.y)<1.2f?0:-g->v.y*.5f;g->v.x*=.78f;g->v.z*=.78f;}else g->p.y=ny;
            float roof=ceiling((int)(g->p.x/CELL),(int)(g->p.z/CELL))-.12f;if(g->p.y>roof){g->p.y=roof;g->v.y=-fabs(g->v.y)*.5f;}
        }
    }
}
CORE void reloadWeapon(void){if(!reloadTime&&ammo[weapon]<magazine[weapon]&&reserve[weapon]>0){reloadTime=weapon==1?1.6f:1.35f;sound(4,.5,0);note("RELOADING");}}
CORE void fireWeapon(void){
    if(shootCD>0||reloadTime>0)return;if(ammo[weapon]<=0){reloadWeapon();return;}
    ammo[weapon]--;shotsFired++;shootCD=weapon==0?.115f:weapon==1?.72f:.6f;gunKick=1;shake=weapon==1?.07f:.025f;
    sound(weapon,weapon==1?1.f:.72f,0);
    int count=weapon==1?9:1;
    for(int pellet=0;pellet<count;pellet++){
        V3 d=forward();float spread=weapon==1?.055f:weapon==0?(mouseRight?.003:.012):.001f;
        d=norm(add(d,v3((rnd()-.5f)*spread,(rnd()-.5f)*spread,(rnd()-.5f)*spread)));
        float best=traceWorld(player,d,110);int hit=-1;
        for(int i=0;i<enemyCount;i++){Enemy *e=enemies+i;if(e->hp<=0)continue;float h=e->type==3?4:e->type==1?1.0f:1.9f,r=e->type==3?1.65f:e->type==2?.8f:.5f;float y=e->type==1?1.4f+sinf(totalTime*2+e->phase)*.2f:0;
            float t=rayBox(player,d,v3(e->p.x-r,y,e->p.z-r),v3(e->p.x+r,y+h,e->p.z+r));if(t<best){best=t;hit=i;}}
        V3 hitPos=add(player,mul(d,best));
        float damage=(weapon==0?22:weapon==1?14:130)*(1+upgrades*.15f);
        if(hit>=0)damageEnemy(hit,damage,hitPos);else{particles(hitPos,weapon==2?14:3,0,2.4);for(int j=0;j<propCount;j++){Prop *q=props+j;if(q->kind==3&&q->active&&fabs(hitPos.x-q->p.x)<q->sx/2+.2f&&fabs(hitPos.z-q->p.z)<q->sz/2+.2f&&hitPos.y<q->sy+.2f){q->hp-=damage;if(q->hp<=0){q->active=0;blast(add(q->p,v3(0,.65f,0)),5.5f,170);}break;}}}
        if(weapon==2){for(float t=.7f;t<best;t+=.45f){Particle *p=parts+(partIndex++%MAXP);p->p=add(player,mul(d,t));p->v=v3(0,0,0);p->life=p->maxlife=.17;p->size=.035;p->type=2;}}
    }
}
CORE void makeBolt(V3 origin,V3 target,float speed,int owner){Bolt *b=bolts+(boltIndex++%96);b->p=origin;b->v=mul(norm(sub(target,origin)),speed);b->life=6;b->owner=owner;}
CORE void distanceField(void){
    int qx[MW*MH],qz[MW*MH],head=0,tail=0;for(int z=0;z<MH;z++)for(int x=0;x<MW;x++)distmap[z][x]=9999;
    int px=CLAMP((int)(player.x/CELL),0,MW-1),pz=CLAMP((int)(player.z/CELL),0,MH-1);distmap[pz][px]=0;qx[tail]=px;qz[tail++]=pz;
    while(head<tail){int x=qx[head],z=qz[head++];for(int d=0;d<4;d++){int nx=x+(d==0)-(d==1),nz=z+(d==2)-(d==3);if(!solid(nx,nz)&&distmap[nz][nx]==9999){distmap[nz][nx]=distmap[z][x]+1;qx[tail]=nx;qz[tail++]=nz;}}}
}
CORE void interact(void){
    for(int i=0;i<3;i++)if(!relays[i].on&&dsq(relays[i].p.x-player.x,relays[i].p.z-player.z)<9){
        relays[i].on=1;relayCount++;armor=MIN(100,armor+25);reserve[0]+=30;reserve[1]+=4;reserve[2]+=3;health=MIN(100,health+20);
        particles(add(relays[i].p,v3(0,1.5,0)),50,2,5);sound(5,1,0);
        note(relayCount==3?"ALL RELAYS SEVERED. CONTAINMENT GATE OPEN.":"RELAY SEVERED. SECURITY REINFORCEMENTS INBOUND.");
        if(enemyCount+2<MAXE){spawn(16,13,relayCount%2);spawn(19,22,0);enemies[enemyCount-1].active=enemies[enemyCount-2].active=1;}
        return;
    }
}
CORE void simulate(float dt){
    swayX*=MAX(0,1-dt*12);swayY*=MAX(0,1-dt*12);
    gateLift=MIN(6,gateLift+(relayCount==3?dt*3:0));
    gameTime+=dt;shootCD=decay(shootCD,dt);damageFlash=decay(damageFlash,dt*1.5f);hitFlash=decay(hitFlash,dt);gunKick=decay(gunKick,dt*8);shake=decay(shake,dt);toastTime=decay(toastTime,dt);
    if(reloadTime>0){reloadTime-=dt;if(reloadTime<=0){reloadTime=0;int n=MIN(reserve[weapon],magazine[weapon]-ammo[weapon]);ammo[weapon]+=n;reserve[weapon]-=n;sound(4,.35,0);}}
    for(int i=0;i<3;i++)if(pressed['1'+i]){weapon=i;reloadTime=0;shootCD=.18;}
    if(pressed['R'])reloadWeapon();if(pressed['E'])interact();if(pressed['G'])throwGrenade();stepGrenades(dt);
    if(keys[VK_LEFT])yaw+=dt*1.8;if(keys[VK_RIGHT])yaw-=dt*1.8;if(keys[VK_UP])pitch=MIN(1.35,pitch+dt);if(keys[VK_DOWN])pitch=MAX(-1.35,pitch-dt);
    float feet=player.y-eyeHeight;eyeHeight+=( (keys[VK_CONTROL]?1.05f:1.65f)-eyeHeight)*MIN(1,dt*14);player.y=feet+eyeHeight;
    float dx=(keys['D']?1.f:0)-(keys['A']?1.f:0),dz=(keys['W']?1.f:0)-(keys['S']?1.f:0),m=sqrtf(dx*dx+dz*dz);
    float speed=keys[VK_SHIFT]?8.4f:5.4f;if(keys[VK_CONTROL])speed=2.8f;
    V3 wish=v3(0,0,0);if(m>0){dx/=m;dz/=m;wish=v3((-dx*cosf(yaw)+dz*sinf(yaw))*speed,0,(dz*cosf(yaw)+dx*sinf(yaw))*speed);}
    float control=MIN(1,dt*(grounded?18:4));velocity.x+=(wish.x-velocity.x)*control;velocity.z+=(wish.z-velocity.z)*control;
    moveBody(&player,velocity.x*dt,velocity.z*dt,.33f);
    float motion=sqrtf(velocity.x*velocity.x+velocity.z*velocity.z),old=stepPhase;stepPhase+=dt*motion*1.7f;
    if((int)(old/PI)!=(int)(stepPhase/PI)&&grounded&&motion>.5f)sound(4,.22,0);
    jumpBuffer=pressed[VK_SPACE]?.12f:decay(jumpBuffer,dt);coyote=grounded?.1f:decay(coyote,dt);
    if(jumpBuffer>0&&coyote>0){vy=7.5f;grounded=0;coyote=jumpBuffer=0;sound(4,.15,0);}
    vy-=dt*18;float next=feet+vy*dt,support=0;
    for(int i=0;i<propCount;i++){Prop *p=props+i;if(p->active&&feet>=p->sy-.03f&&fabs(player.x-p->p.x)<p->sx/2+.25f&&fabs(player.z-p->p.z)<p->sz/2+.25f)support=MAX(support,p->sy);}
    grounded=0;if(next<=support&&vy<=0){next=support;vy=0;grounded=1;}player.y=next+eyeHeight;
    float roof=ceiling((int)(player.x/CELL),(int)(player.z/CELL));if(player.y>roof-.2f){player.y=roof-.2f;vy=MIN(0,vy);}
    if(mouseDown||keys['F'])fireWeapon();
    if((int)(gameTime*3)!=(int)((gameTime-dt)*3))distanceField();
    int px=(int)(player.x/CELL),pz=(int)(player.z/CELL);for(int z=MAX(0,pz-4);z<MIN(MH,pz+5);z++)for(int x=MAX(0,px-4);x<MIN(MW,px+5);x++)seen[z][x]=1;
    if(pz>=29&&!bossAwake){bossAwake=1;note("CONTAINMENT FAILURE. ELIMINATE THE WARDEN.");sound(6,1,0);}
    for(int i=0;i<enemyCount;i++){
        Enemy *e=enemies+i;e->fire=decay(e->fire,dt*8);if(e->hp<=0)continue;e->hit=decay(e->hit,dt);e->cd-=dt;V3 oldPos=e->p;
        V3 dif=sub(player,e->p);dif.y=0;float dist=length(dif);int sight=dist<45&&clearSight(add(e->p,v3(0,1.5,0)),player);
        if(e->type==3){if(!bossAwake)continue;e->active=1;}
        else if(sight&&dist<27)e->active=1;
        if(!e->active)continue;float turn=atan2f(dif.x,dif.z)*180/PI-e->yaw;e->yaw+=(fmod(turn+540,360)-180)*MIN(1,dt*7);
        if(dist>(e->type==3?14:e->type==2?5:9)||!sight){
            V3 target=player;int x=(int)(e->p.x/CELL),z=(int)(e->p.z/CELL);
            if(!sight&&x>=0&&x<MW&&z>=0&&z<MH){int best=distmap[z][x];for(int d=0;d<4;d++){int nx=x+(d==0)-(d==1),nz=z+(d==2)-(d==3);if(!solid(nx,nz)&&distmap[nz][nx]<best){best=distmap[nz][nx];target=cellpos(nx,nz);}}}
            V3 dir=sub(target,e->p);dir.y=0;dir=norm(dir);float es=e->type==1?3.8f:e->type==2?2.1f:e->type==3?1.5f:2.7f;
            V3 before=e->p;moveBody(&e->p,dir.x*dt*es,dir.z*dt*es,e->type==3?1.2f:.38f);
            if(length(sub(before,e->p))<dt*.2f)moveBody(&e->p,dir.z*dt*es,-dir.x*dt*es,.38f);
        }else if(e->type==1){moveBody(&e->p,cosf(totalTime+e->phase)*dt*1.3,sinf(totalTime+e->phase)*dt*1.3,.38);}
        float stride=length(sub(e->p,oldPos));e->gait+=stride*4;e->speed+=(MIN(1,stride/MAX(dt,.001)) -e->speed)*MIN(1,dt*10);
        if(sight&&e->cd<=0&&dist<38){e->fire=1;
            V3 from=add(e->p,v3(0,e->type==3?2.6f:e->type==1?1.9f:1.3f,0));V3 target=player;
            if(e->type==3){for(int j=-2;j<=2;j++)makeBolt(from,add(target,v3(j*1.8f,0,-j*.3f)),13,i);e->cd=e->hp<600?.9f:1.5f;}
            else{target.x+=(rnd()-.5f)*1.2f;target.z+=(rnd()-.5f)*1.2f;makeBolt(from,target,e->type==1?13:11,i);e->cd=e->type==2?.9f:1.7f+rnd()*.4f;}
            sound(2,MIN(.5,3/MAX(dist,1)),CLAMP((from.x-player.x)*.02,-1,1));
        }
        if(dist<1.1&&e->cd<.3f){hurtPlayer(10);e->cd=1;}
    }
    for(int i=0;i<96;i++)if(bolts[i].life>0){Bolt *b=bolts+i;b->life-=dt;V3 delta=mul(b->v,dt);int n=1+(int)(length(delta)/.2);delta=mul(delta,1.f/n);
        for(int j=0;j<n&&b->life>0;j++){b->p=add(b->p,delta);int blocked=wallAt(b->p.x,b->p.z)||b->p.y<0;
            for(int k=0;k<propCount;k++){Prop *p=props+k;if(p->active&&b->p.y<p->sy&&fabs(b->p.x-p->p.x)<p->sx/2&&fabs(b->p.z-p->p.z)<p->sz/2)blocked=1;}
            if(blocked){particles(b->p,4,0,2);b->life=0;}
            else if(dsq(b->p.x-player.x,b->p.z-player.z)<.26&&b->p.y>player.y-1.6f&&b->p.y<player.y+.2f){hurtPlayer(enemies[b->owner].type==3?18:12);b->life=0;}}
    }
    for(int i=0;i<MAXP;i++)if(parts[i].life>0){Particle *p=parts+i;p->life-=dt;p->p=add(p->p,mul(p->v,dt));if(p->type!=2)p->v.y-=dt*8;if(p->p.y<.04){p->p.y=.04;p->v.y=fabs(p->v.y)*.3f;}}
    for(int i=0;i<pickupCount;i++){Pickup *p=pickups+i;if(!p->active||dsq(p->p.x-player.x,p->p.z-player.z)>1.6f)continue;
        if(p->type==0){if(health>=100)continue;health=MIN(100,health+35);note("MEDICAL CELL +35");}
        if(p->type==1){grenades=MIN(8,grenades+1);reserve[0]+=45;reserve[1]+=6;reserve[2]+=3;note("MUNITIONS RECOVERED");}
        if(p->type==3){upgrades++;grenades=MIN(8,grenades+2);health=MIN(100,health+25);note("OVERCLOCK CELL: DAMAGE +15% / GRENADES +2");}
        if(p->type==2){if(armor>=100)continue;armor=MIN(100,armor+40);note("ARMOR CAPACITOR +40");}
        p->active=0;sound(5,.5,0);
    }
    if(bossDead&&!evacSpawned){evacSpawned=1;static const unsigned char wave[][2]={{13,35},{21,35},{11,38},{23,38},{16,31},{18,31}};for(int i=0;i<6;i++){spawn(wave[i][0],wave[i][1],i%3);enemies[enemyCount-1].active=1;}note("EXTRACTION UPLINK: HOLD THE GREEN BAY FOR 6 SECONDS.");sound(6,.8,0);}
    if(mode==1&&pz>=40&&bossDead){extraction+=dt;if(extraction>6){mode=4;mouseDown=0;ClipCursor(NULL);while(ShowCursor(TRUE)<0);sound(5,1,0);}}else extraction=decay(extraction,dt*2);

    memset(pressed,0,sizeof(pressed));
}

CORE void advanceFrame(float dt){
    if(mode==1){simCarry+=CLAMP(dt,0,.1f);while(simCarry>=1.f/120&&mode==1){simulate(1.f/120);simCarry-=1.f/120;}}
    else{simCarry=0;memset(pressed,0,sizeof(pressed));}
}
/* A generated sphere mesh is reused by every joint, eye and projectile. */
CORE void makeMeshes(void){
    sphereList=glGenLists(1);glNewList(sphereList,GL_COMPILE);
    for(int j=0;j<12;j++){float a=-PI/2+j*PI/12,b=a+PI/12;glBegin(GL_QUAD_STRIP);for(int i=0;i<=24;i++){float t=i*PI/12;float x=cosf(a)*cosf(t),y=sinf(a),z=cosf(a)*sinf(t);glNormal3f(x,y,z);glVertex3f(x,y,z);x=cosf(b)*cosf(t);y=sinf(b);z=cosf(b)*sinf(t);glNormal3f(x,y,z);glVertex3f(x,y,z);}glEnd();}glEndList();
}
CORE void orb(float x,float y,float z,float r){glPushMatrix();glTranslatef(x,y,z);glScalef(r,r,r);glCallList(sphereList);glPopMatrix();}
/* Small part tables replace repeated code and preserve all model detail. */
CORE void panels(const signed char *p,int count,float scale){
    static const unsigned char palette[][4]={{16,24,29,3},{78,94,99,0},{105,124,128,3},{156,81,16,2},{12,180,210,5},{230,115,18,5},{215,25,5,5},{46,60,62,4}};
    static const signed char *sources[8];static GLuint lists[8];static int cached;
    int slot=0;while(slot<cached&&sources[slot]!=p)slot++;if(slot<cached){glCallList(lists[slot]);return;}
    if(cached<8){sources[cached]=p;lists[cached]=glGenLists(1);glNewList(lists[cached++],GL_COMPILE);}
    for(int i=0;i<count;i++,p+=7){const unsigned char *c=palette[(int)p[6]];color(c[0]/255.f,c[1]/255.f,c[2]/255.f,c[3]);bevelBox(p[0]*scale,p[1]*scale,p[2]*scale,p[3]*scale,p[4]*scale,p[5]*scale);}
    if(slot<8){glEndList();glCallList(lists[slot]);}
}
CORE int visibleEnemy(const Enemy *e){
    float x=e->p.x-player.x,z=e->p.z-player.z,d=x*x+z*z;
    return d<85*85&&(fabs(pitch)>.6f||(x*sinf(yaw)+z*cosf(yaw)+4)>MIN(.3f,height/(float)width)*sqrtf(d));
}

CORE void drawEnemy(Enemy *e){
    glPushMatrix();glTranslatef(e->p.x,e->p.y,e->p.z);glRotatef(e->yaw,0,1,0);
    float anim=sinf(e->gait+e->phase)*e->speed*.25f;
    if(e->hp<=0){float fall=CLAMP((gameTime-e->dead)*2.8f,0,1);glTranslatef(0,-fall*.2f,0);glRotatef(fall*82,1,0,0);}
    else{glTranslatef(0,sinf(e->gait*2)*e->speed*.028f,0);glRotatef(sinf(e->hit*PI/.16f)*-7,1,0,0);}
    if(e->type==3){
        color(.1,.15,.17,3);tube(0,1.45,0,1.35,1.35,12);color(.42,.45,.38,0);tube(0,2.05,0,1.15,1.05,8);
        color(.09,.13,.15,3);tube(0,2.85,0,.8,.45,8);color(.9,.13,.025,5);orb(0,2.66,.84,.44);
        color(.18,.26,.29,3);for(int j=0;j<3;j++){tube(0,1.05+j*.37,0,1.43,.065,24);}
        for(int j=0;j<10;j++){float a=j*PI/5;color(.22,.3,.32,3);box(sinf(a)*.96,2.62,cosf(a)*.96,.12,.6,.12);}
        glPushMatrix();glTranslatef(0,2.66,.86);glRotatef(90,1,0,0);color(.055,.1,.12,3);ring(0,0,0,.54,.12,32);glPopMatrix();
        color(.6,.3,.05,2);for(int k=0;k<4;k++){float a=k*PI/2+.4f;beam(v3(sinf(a)*.8,2.5,cosf(a)*.8),v3(sinf(a)*1.35,1,cosf(a)*1.35),.25);}
        for(int k=0;k<6;k++){float a=k*PI/3,step=sinf(e->gait+k*PI)*e->speed;V3 p=v3(sinf(a)*1.1,1.6,cosf(a)*1.1),q=v3(sinf(a)*2.1,1.35+step*.22,cosf(a)*2.1),r=v3(sinf(a)*2.5,.15+MAX(0,step)*.45,cosf(a)*2.5+step*.35);
            color(.22,.27,.28,3);beam(p,q,.17);beam(q,r,.11);color(.58,.64,.64,3);beam(add(p,v3(0,.17,0)),add(q,v3(0,.17,0)),.055);orb(q.x,q.y,q.z,.23);color(.8,.22,.025,5);orb(p.x,p.y,p.z,.1);color(.4,.42,.37,0);box(r.x,r.y,r.z,.46,.22,.65);}
        color(.09,.11,.13,3);for(int k=-1;k<=1;k+=2){box(k*1.15,2.2,.4-e->fire*.22,.44,.5,1.65);color(.9,.12,.01,5);box(k*1.15,2.2,1.27-e->fire*.22,.22,.22,.07);if(e->fire>.45f)orb(k*1.15,2.2,1.45,.23*e->fire);color(.09,.11,.13,3);}
    }else if(e->type==1){
        glTranslatef(0,1.8+sinf(totalTime*2+e->phase)*.2f,0);color(.12,.19,.23,3);orb(0,0,0,.42);color(.4,.5,.5,0);box(0,.05,-.1,.7,.25,.6);
        color(.85,.17,.03,5);box(0,.04,.36,.36,.09,.04);
        for(int k=-1;k<=1;k+=2){color(.16,.2,.23,3);box(k*.6,0,0,.6,.13,.4);tube(k*.72,-.04,0,.22,.18,12);color(.02,.75,1,5);tube(k*.72,-.14,0,.13,.035,12);glPushMatrix();glTranslatef(k*.72,.07,0);glRotatef(totalTime*900*k,0,1,0);color(.36,.43,.44,3);box(0,0,0,.52,.025,.05);box(0,0,0,.05,.025,.52);glPopMatrix();}
        color(.07,.09,.11,3);box(0,-.2,.4,.15,.17,.4);
    }else{
        if(e->type==2)glScalef(1.35,1.1,1.2);
        for(int k=-1;k<=1;k+=2){float swing=anim*k;
            color(.13,.17,.19,3);beam(v3(k*.23,.85,0),v3(k*.27,.42,swing),.13);beam(v3(k*.27,.42,swing),v3(k*.28,.13,-swing),.1);
            color(.4,.42,.36,0);box(k*.28,.14,.08-swing,.28,.22,.46);box(k*.24,.61,swing*.7,.25,.36,.28);
            color(.15,.2,.21,3);orb(k*.27,.42,swing,.14);
        }
        static const signed char armorParts[][7]={
            {0,47,0,29,26,18,1},{0,46,11,22,14,5,0},{0,54,-13,21,26,9,1},
            {0,57,13,27,6,3,3},{0,72,1,17,14,16,1},{0,72,10,13,2,1,6},
            {0,78,1,19,2,17,0},{-10,70,1,4,9,11,2},{10,70,1,4,9,11,2},
            {-7,52,13,6,8,2,2},{7,52,13,6,8,2,2},{0,37,1,17,5,16,0},
            {0,63,-2,9,6,9,0},{0,43,13,12,3,2,3},{0,49,-18,10,14,2,0},
            {-5,52,-19,2,9,1,4},{5,52,-19,2,9,1,4}};
        panels(&armorParts[0][0],sizeof(armorParts)/7,.025);
        if(e->hit>0){color(1,.32,.02,5);box(0,1.3,.342,.15,.12,.012);}
        for(int k=-1;k<=1;k+=2){color(.35,.38,.32,0);orb(k*.47,1.42,0,.22);color(.12,.16,.17,3);beam(v3(k*.46,1.35,0),v3(k*.5,1,.23),.13);beam(v3(k*.5,1,.23),v3(k*.3,1.15,.64),.12);}
        color(.06,.08,.1,3);box(.3,1.14,.7-e->fire*.09,.25,.25,.7);
        color(.9,.15,.025,5);box(.3,1.14,1.06-e->fire*.09,.13,.13,.02);
        if(e->fire>.45f){color(1,.4,.05,5);orb(.3,1.14,1.2,.16*e->fire);}
    }
    glPopMatrix();
}
CORE void drawMachinery(void){
    for(int i=0;i<6;i++){float x=(i%2?24.8f:10.2f)*CELL,z=(14.5f+(i/2)*4)*CELL;
        glPushMatrix();glTranslatef(x,4.7,z);glRotatef(i%2?-90:90,0,1,0);glRotatef(90,1,0,0);
        color(.11,.16,.18,3);ring(0,0,0,.95,.15,24);ring(0,.13,0,.84,.045,24);
        glRotatef(totalTime*(i%2?150:-150),0,1,0);color(.3,.37,.38,3);
        for(int k=0;k<5;k++){glRotatef(72,0,1,0);box(.4,0,0,.67,.065,.18);}color(.55,.3,.08,3);tube(0,0,0,.18,.2,12);glPopMatrix();
    }
    V3 p=cellpos(17,18);
    for(int j=0;j<3;j++){glPushMatrix();glTranslatef(p.x,1.2f+j*1.45f,p.z);glRotatef(totalTime*(j%2?22:-22),0,1,0);
        for(int k=0;k<6;k++){glRotatef(60,0,1,0);color(.12,.23,.26,3);box(2.1,0,0,.15,.3,.64);color(.02,.8,1,5);box(2.19,0,0,.015,.12,.42);}
        glPopMatrix();}
}
CORE void drawOrdnance(void){
    for(int i=0;i<propCount;i++){Prop *p=props+i;if(p->kind==3&&p->active){float x=p->p.x,z=p->p.z;color(.46,.18,.045,2);tube(x,.6,z,.38,1.15,16);color(.11,.17,.19,3);for(int j=0;j<3;j++)tube(x,.1+j*.5f,z,.41,.07,16);color(1,.2,.015,5);tube(x,.61,z,.385,.08,16);}}
    for(int i=0;i<8;i++){Grenade *g=grenadesLive+i;if(g->fuse>0){color(.15,.2,.18,3);orb(g->p.x,g->p.y,g->p.z,.12);color(.95,.3,.02,5);orb(g->p.x,g->p.y+.075f,g->p.z,.045f);}}

}
CORE void drawDynamic(void){
    drawMachinery();
    drawOrdnance();
    for(int i=0;i<enemyCount;i++)if((enemies[i].hp>0||gameTime-enemies[i].dead<8)&&visibleEnemy(enemies+i))drawEnemy(enemies+i);
    for(int i=0;i<3;i++){
        Relay *r=relays+i;float x=r->p.x,z=r->p.z;
        color(.13,.18,.19,3);box(x,.58,z,1.25,1.16,1.0);color(.34,.39,.39,0);box(x,1.27,z,1.5,.3,1.2);
        color(.025,r->on?.8:.38,r->on?.35:.95,5);box(x,1.44,z,1.25,.022,.82);
        for(int k=0;k<3;k++){color(.6,.3,.05,2);tube(x+(k-1)*.37,1.82,z,.13,.7,8);color(.02,r->on?.4:.85,r->on?.15:1,5);tube(x+(k-1)*.37,1.86,z,.08,.45,8);}
        if(!r->on){glPushMatrix();glTranslatef(x,2.7+sinf(totalTime*2)*.12,z);glRotatef(totalTime*35,0,1,0);color(.04,.9,1,5);box(0,0,0,.22,.22,.22);glPopMatrix();}
    }
    float gateX=17.5*CELL,gateZ=28.5*CELL,raise=gateLift;
    color(.22,.29,.3,0);box(gateX,2.8+raise,gateZ,8.4,5.6,.55);color(.7,.32,.045,2);box(gateX,1.1+raise,gateZ-.3,8.1,.55,.07);
    color(relayCount==3?.05:.85,relayCount==3?.8:.1,.04,5);box(gateX,3.7+raise,gateZ-.31,4,.065,.06);
    for(int i=0;i<pickupCount;i++)if(pickups[i].active){
        Pickup *p=pickups+i;glPushMatrix();glTranslatef(p->p.x,.55+sinf(totalTime*2+i)*.12,p->p.z);glRotatef(totalTime*35+i*41,0,1,0);
        color(.07,.13,.15,3);box(0,0,0,.55,.45,.5);
        if(p->type==0){color(.12,.9,.5,5);box(0,0,-.258,.32,.09,.02);box(0,0,-.27,.1,.3,.02);}
        else if(p->type==1){color(.95,.48,.045,5);for(int j=0;j<3;j++)box((j-1)*.14,0,-.265,.055,.28,.03);}
        else if(p->type==2){color(.05,.55,1,5);box(0,0,-.26,.25,.25,.03);}else{color(.8,.15,1,5);orb(0,0,0,.3);}
        glPopMatrix();color(p->type==1?.7:.02,p->type==0?.65:.35,p->type==2?1:.13,5);ring(p->p.x,.02,p->p.z,.6,.028,24);
    }
    /* Reactor orbitals and the Warden containment field. */
    V3 p=cellpos(17,18);glPushMatrix();glTranslatef(p.x,3.2,p.z);glRotatef(totalTime*17,0,1,0);glRotatef(24,1,0,0);color(.1,.6,.72,5);ring(0,0,0,2.4,.025,48);glPopMatrix();
    for(int i=0;i<96;i++)if(bolts[i].life>0){Bolt *b=bolts+i;color(1,.17,.025,5);orb(b->p.x,b->p.y,b->p.z,.11);color(.8,.3,.02,5);beam(b->p,sub(b->p,mul(norm(b->v),.6)),.034);}
    glEnable(GL_BLEND);glBlendFunc(GL_ONE,GL_ONE);glDepthMask(GL_FALSE);
    for(int i=0;i<MAXP;i++)if(parts[i].life>0){Particle *p=parts+i;float f=p->life/p->maxlife;if(p->type==2)color(.05*f,.55*f,.8*f,5);else color(.9*f,.38*f,.065*f,5);beam(p->p,sub(p->p,mul(p->v,.026f)),p->size*.55f);}
    for(int i=0;i<85;i++){float x=player.x+sinf(i*47.31)*13,z=player.z+cosf(i*17.37)*13,y=fmod(i*.33+totalTime*.14,5.5);if(!wallAt(x,z)){color(.085,.13,.14,5);box(x,y,z,.012,.018,.012);}}
    glDepthMask(GL_TRUE);glDisable(GL_BLEND);
}
CORE void drawWeapon(void){
    static const signed char receiver[][7]={
        {4,-13,17,19,20,22,7},{-17,-7,-36,21,16,24,7},
        {0,0,0,27,23,72,0},{0,4,40,20,21,38,0},{0,-18,12,13,31,18,7},
        {0,10,-9,31,11,62,1},{0,-7,-45,29,19,32,1},{16,4,-12,2,11,39,3},
        {0,19,4,6,8,47,0},{-16,3,-10,2,12,43,1},{0,-9,55,22,9,8,7},
        {0,7,55,22,10,6,1},{0,-9,-24,20,9,18,0},{0,5,-45,32,11,6,2},
        {8,-20,15,2,26,9,2},{-8,-20,15,2,26,9,2},{0,-33,16,18,4,15,0}};
    static const signed char carbine[][7]={
        {0,-27,-15,15,36,25,0},{0,-30,-15,16,13,26,1},
        {0,24,-14,18,4,14,0},{-8,31,-14,3,12,14,0},{8,31,-14,3,12,14,0},
        {0,38,-14,18,3,14,0},{0,31,-8,1,5,1,4},{0,31,-8,5,1,1,4},
        {0,4,-68,14,12,22,1},{0,12,-78,3,13,4,0}};
    static const signed char shotgun[][7]={{0,-5,-62,26,17,36,7},{0,-18,-18,16,20,24,1},{0,16,-85,3,6,3,5}};
    static const signed char ion[][7]={{0,3,-59,35,28,70,1},{0,18,-59,18,2,60,4},{0,3,-96,18,16,2,4},{0,-19,-30,23,22,30,0}};
    glMatrixMode(GL_MODELVIEW);glLoadIdentity();glTranslatef((mouseRight?.02f:.39f)-swayX,(mouseRight?(weapon==0?-.248f:-.14f):-.41f)+sinf(stepPhase)*.012f-gunKick*.027f+swayY-sinf(grenadeCD*2*PI)*.12f,-.86f+gunKick*.075f);
    glRotatef(sinf(stepPhase*.5f)*.55f+swayX*70,0,0,1);if(keys[VK_SHIFT]&&!mouseRight)glRotatef(-11,1,0,1);
    if(reloadTime>0){float t=sinf(CLAMP(reloadTime/(weapon==1?1.6f:1.35f),0,1)*PI);glTranslatef(0,-.22f*t,.07f*t);glRotatef(32*t,0,0,1);}
    glRotatef(gunKick*6,1,0,0);glScalef(.8,.8,.8);
    color(.14,.18,.19,4);beam(v3(.09,-.26,.58),v3(.03,-.14,.18),.115);beam(v3(-.52,-.33,.51),v3(-.18,-.1,-.35),.095);
    panels(&receiver[0][0],sizeof(receiver)/7,.01);
    color(.085,.12,.14,3);for(int i=0;i<7;i++){box(0,.234,.25-i*.074,.12,.015,.034);box(.169,.04,-.3+i*.06,.025,.08,.022);}
    color(.44,.48,.48,3);for(int i=0;i<4;i++)orb(-.178,.095,.15-i*.13,.022);
    color(.15,.21,.23,3);box(-.175,.04,.06+gunKick*.16,.04,.045,.23);
    if(weapon==0){
        panels(&carbine[0][0],sizeof(carbine)/7,.01);
        color(.23,.28,.29,3);beam(v3(0,0,-.58),v3(0,0,-1.15),.059);color(.06,.085,.1,3);beam(v3(0,0,-1.02),v3(0,0,-1.22),.092);
        if(reloadTime>0){color(.14,.2,.22,3);box(0,-.27-sinf(reloadTime*2.2)*.22,-.15,.155,.3,.25);}
    }else if(weapon==1){
        panels(&shotgun[0][0],sizeof(shotgun)/7,.01);
        for(int k=-1;k<=1;k+=2){color(.23,.27,.29,3);beam(v3(k*.064,.025,-.49),v3(k*.064,.025,-1.02),.067);color(.03,.045,.05,3);beam(v3(k*.064,.025,-1.021),v3(k*.064,.025,-1.027),.047);}
        for(int j=0;j<6;j++){color(.075,.1,.11,3);box(0,-.058,-.49-j*.05+gunKick*.1,.275,.176,.014);}
        color(.66,.26,.04,3);for(int j=0;j<4;j++)beam(v3(.21,.02,-.26+j*.11),v3(.21,.12,-.26+j*.11),.035);
    }else{
        panels(&ion[0][0],sizeof(ion)/7,.01);
        for(int j=0;j<5;j++){color(.25,.32,.34,3);box(0,.025,-.31-j*.145,.39,.34,.045);}
        glPushMatrix();glTranslatef(0,.03,-.78);glRotatef(90,1,0,0);glRotatef(totalTime*110,0,1,0);color(.01,.55+gunKick*.4,.8,5);ring(0,0,0,.22,.025,24);glPopMatrix();
    }
    if(gunKick>.5f){glEnable(GL_BLEND);glBlendFunc(GL_ONE,GL_ONE);glDepthMask(GL_FALSE);color(weapon==2?.05:1,weapon==2?.8:.6,weapon==2?1:.12,5);orb(0,.01,weapon==0?-1.32:-1.12,.09+gunKick*.07);glDepthMask(GL_TRUE);glDisable(GL_BLEND);}
}

static GLuint fonts[4];static int fontSizes[4]={15,22,62,12};
CORE void makeFonts(void){
    HFONT vectorFont=CreateFontA(-32,0,0,0,FW_BOLD,0,0,0,ANSI_CHARSET,OUT_TT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,"Consolas");HGDIOBJ prev=SelectObject(dc,vectorFont);fontList=glGenLists(96);GLYPHMETRICSFLOAT metrics[96];wglUseFontOutlinesA(dc,32,96,fontList,.003f,0,WGL_FONT_POLYGONS,metrics);SelectObject(dc,prev);DeleteObject(vectorFont);
    for(int i=0;i<4;i++){HFONT font=CreateFontA(fontSizes[i],0,0,0,i==2?FW_BOLD:FW_NORMAL,0,0,0,ANSI_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,i==2?"Bahnschrift":"Consolas");HGDIOBJ old=SelectObject(dc,font);fonts[i]=glGenLists(96);wglUseFontBitmapsA(dc,32,96,fonts[i]);SelectObject(dc,old);DeleteObject(font);}
}
CORE void textAt(float x,float y,int size,const char *s,float r,float g,float b){glColor4f(r,g,b,1);glRasterPos2f(x,y+fontSizes[size]);glListBase(fonts[size]-32);glCallLists((int)strlen(s),GL_UNSIGNED_BYTE,s);}
CORE void rect(float x,float y,float w,float h,float r,float g,float b,float a){glColor4f(r,g,b,a);glBegin(GL_QUADS);glVertex2f(x,y);glVertex2f(x+w,y);glVertex2f(x+w,y+h);glVertex2f(x,y+h);glEnd();}
CORE void line2(float x,float y,float xx,float yy,float r,float g,float b,float a){glColor4f(r,g,b,a);glBegin(GL_LINES);glVertex2f(x,y);glVertex2f(xx,yy);glEnd();}
CORE void uiStart(void){UseProgram(0);glDisable(GL_DEPTH_TEST);glDisable(GL_TEXTURE_2D);glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glMatrixMode(GL_PROJECTION);glLoadIdentity();glOrtho(0,width,height,0,-1,1);glMatrixMode(GL_MODELVIEW);glLoadIdentity();}
CORE void drawMap(float x,float y,float scale,int full){
    rect(x-10,y-10,MW*scale+20,MH*scale+20,.015,.035,.04,.83);
    for(int z=0;z<MH;z++)for(int xx=0;xx<MW;xx++)if((seen[z][xx]||full)&&map[z][xx]!=1)rect(x+(MW-xx-1)*scale,y+(MH-z-1)*scale,scale-.5f,scale-.5f,.15,.25,.27,.8);
    for(int i=0;i<3;i++)rect(x+(MW-relays[i].p.x/CELL)*scale-2,y+(MH-relays[i].p.z/CELL)*scale-2,4,4,relays[i].on?.15:1,relays[i].on?.8:.54,.12,1);
    float a=x+(MW-player.x/CELL)*scale,b=y+(MH-player.z/CELL)*scale;rect(a-2,b-2,4,4,.8,1,1,1);line2(a,b,a-sinf(yaw)*9,b-cosf(yaw)*9,.6,1,1,1);
    if(full){textAt(x,y+MH*scale+15,0,"01 COOLANT / 02 FOUNDRY / 03 CORE",.55,.7,.7);textAt(x,y+MH*scale+36,0,"GOLD: RELAY   GREEN: OFFLINE",.6,.75,.75);}
}
CORE void drawUI(void){
    char s[256];uiStart();float cx=width*.5f,cy=height*.5f;
    if(mode==1||mode==2||mode==3){
        rect(25,24,320,67,.018,.035,.041,.82);rect(25,24,3,67,.02,.7,.77,1);
        textAt(42,34,3,"N U L L P O I N T  /  OPERATION 09",.54,.7,.72);
        snprintf(s,sizeof(s),bossDead?"DEFEND EXTRACTION UPLINK":relayCount==3?"ELIMINATE THE WARDEN":"SEVER RELAYS  /  %d OF 3",relayCount);textAt(42,56,0,s,.93,.95,.88);
        rect(25,height-105,238,78,.018,.035,.041,.85);textAt(41,height-98,3,"VITALS",.43,.65,.68);
        snprintf(s,sizeof(s),"%03d",(int)ceil(health));textAt(39,height-80,1,s,.84,.95,.88);textAt(87,height-75,3,"HP",.48,.64,.65);
        snprintf(s,sizeof(s),"%03d",(int)armor);textAt(142,height-80,1,s,.2,.76,.87);textAt(190,height-75,3,"AR",.48,.64,.65);
        rect(40,height-43,96,3,.1,.2,.22,1);rect(40,height-43,.96*health,3,.34,.85,.65,1);rect(147,height-43,98,3,.1,.2,.22,1);rect(147,height-43,.98*armor,3,.1,.65,.85,1);
        rect(width-320,height-105,295,78,.018,.035,.041,.85);textAt(width-303,height-98,3,weaponName[weapon],.45,.65,.68);
        snprintf(s,sizeof(s),reloadTime>0?"RELOADING":"%02d  /  %03d",ammo[weapon],reserve[weapon]);textAt(width-303,height-79,1,s,.92,.93,.83);textAt(width-303,height-49,3,"1 CARBINE    2 BREACH    3 ION",.47,.63,.65);
        float gap=5+gunKick*10;float cr=hitFlash>0?1:.68,cg=hitFlash>0?.32:.88;
        line2(cx-gap-7,cy,cx-gap,cy,cr,cg,.8,.9);line2(cx+gap,cy,cx+gap+7,cy,cr,cg,.8,.9);line2(cx,cy-gap-7,cx,cy-gap,cr,cg,.8,.9);line2(cx,cy+gap,cx,cy+gap+7,cr,cg,.8,.9);rect(cx-1,cy-1,2,2,.9,1,1,.9);
        if(hitFlash>0){line2(cx-12,cy-12,cx-7,cy-7,1,.4,.1,1);line2(cx+12,cy-12,cx+7,cy-7,1,.4,.1,1);line2(cx-12,cy+12,cx-7,cy+7,1,.4,.1,1);line2(cx+12,cy+12,cx+7,cy+7,1,.4,.1,1);}
        if(toastTime>0){float tw=strlen(toast)*7.4f;rect(cx-tw/2-18,height-159,tw+36,34,.025,.065,.075,MIN(1,toastTime)*.88);textAt(cx-tw/2,height-153,3,toast,.85,.9,.79);}
        for(int i=0;i<3;i++)if(!relays[i].on&&dsq(player.x-relays[i].p.x,player.z-relays[i].p.z)<9){rect(cx-129,cy+52,258,35,.01,.1,.12,.92);textAt(cx-110,cy+58,0,"[ E ]  SEVER POWER RELAY",.22,.96,1);}
        if(bossAwake&&!bossDead){float hp=0;for(int i=0;i<enemyCount;i++)if(enemies[i].type==3)hp=enemies[i].hp;rect(cx-200,29,400,5,.11,.15,.16,1);rect(cx-200,29,400*hp/1400,5,.93,.25,.075,1);textAt(cx-60,40,3,"T H E  W A R D E N",.85,.55,.35);}
        if(keys[VK_TAB])drawMap(cx-MW*7/2,105,7,1);else drawMap(width-141,31,2.55f,0);
        snprintf(s,sizeof(s),"G GRENADES %d  /  OVERCLOCK %d OF 3",grenades,upgrades);textAt(cx-155,height-46,3,s,.62,.73,.7);
        textAt(cx-143,height-23,3,"TAB MAP  /  E INTERACT  /  ESC PAUSE",.3,.48,.52);
        if(bossDead){rect(cx-140,99,280,5,.1,.2,.22,1);rect(cx-140,99,280*MIN(1,extraction/6),5,.1,.85,.5,1);}
    }
    if(mode!=1){
        rect(0,0,width,height,.005,.018,.025,mode==0?.40f:.8f);
        float x=width*.105f,y=height*.2f;
        textAt(x,y-43,3,"PROCEDURAL SYSTEMS  /  NATIVE 96K EDITION",.32,.75,.8);
        textAt(x-4,y,2,mode==0?"NULLPOINT":mode==2?"SUSPENDED":mode==3?"SIGNAL LOST":"CORE SILENCED",.91,.94,.88);
        rect(x,y+82,48,3,.95,.49,.13,1);
        if(mode==0){
            textAt(x,y+113,1,"THE REACTOR IS STILL DREAMING.",.88,.65,.36);
            textAt(x,y+153,0,"Its security network has turned against the crew.",.64,.74,.75);
            textAt(x,y+180,0,"Sever three power relays. Destroy the Warden.",.64,.74,.75);
            textAt(x,y+207,0,"Explore for upgrades. Fight your way out.",.64,.74,.75);
            rect(x-1,y+260,316,49,.08,.22,.24,.93);rect(x-1,y+260,3,49,.05,.83,.9,1);
            textAt(x+19,y+273,0,"[ ENTER ]  INITIALIZE MISSION",.88,.95,.91);
            snprintf(s,sizeof(s),"[ <  > ]  THREAT: %s",difficulty==0?"RECRUIT":difficulty==1?"OPERATIVE":"VETERAN");textAt(x,y+332,0,s,.62,.73,.74);
        }else if(mode==2){
            textAt(x,y+118,1,"[ ESC / ENTER ]  RESUME",.5,.89,.9);textAt(x,y+164,0,"[ N ]  RESTART MISSION",.65,.74,.75);
            snprintf(s,sizeof(s),"[ M ]  AUDIO: %s",mute?"MUTED":"ON");textAt(x,y+203,0,s,.65,.74,.75);
            snprintf(s,sizeof(s),"[ Q ]  RENDER: %s",quality?"FULL":"PERFORMANCE");textAt(x,y+238,0,s,.65,.74,.75);
            snprintf(s,sizeof(s),"[ - / + ]  MOUSE SENSITIVITY: %.1f",mouseSensitivity*1000);textAt(x,y+273,0,s,.65,.74,.75);
            textAt(x,y+308,0,"[ F11 ]  BORDERLESS   [ END ]  QUIT",.65,.74,.75);
        }else{
            snprintf(s,sizeof(s),"%02d:%02d  ELAPSED  /  %d HOSTILES NEUTRALIZED",(int)gameTime/60,(int)gameTime%60,kills);textAt(x,y+118,0,s,.7,.82,.79);
            textAt(x,y+160,0,mode==3?"Your signal faded. The facility remains awake.":"The network is dark. Your transmission gets through.",.64,.74,.75);
            textAt(x,y+218,1,"[ ENTER ]  RUN IT AGAIN",.25,.86,.9);textAt(x,y+265,0,"[ END ]  QUIT",.54,.68,.7);
        }
        textAt(34,height-54,3,"WASD MOVE   MOUSE AIM   CLICK FIRE   R RELOAD   1/2/3 WEAPONS",.44,.63,.66);
        textAt(34,height-32,3,"SHIFT SPRINT   SPACE JUMP   CTRL CROUCH   G GRENADE   E USE   F11 FULLSCREEN",.44,.63,.66);
        textAt(width-226,27,3,"NO ASSETS. ONLY ALGORITHMS.",.35,.65,.69);
    }
    glDisable(GL_BLEND);
}

CORE int fail(const char *s){if(s!=lastError){strncpy(lastError,s,sizeof(lastError)-1);lastError[sizeof(lastError)-1]=0;}logline(s);if(!testMode)MessageBoxA(NULL,s,"NULLPOINT",MB_OK|MB_ICONERROR);return 0;}
CORE GLuint shaderProgram(const char *vs,const char *fs){
    GLuint p=CreateProgram();const char *sources[2]={vs,fs};
    for(int i=0;i<2;i++){GLuint s=CreateShader(i?GL_FRAGMENT_SHADER:GL_VERTEX_SHADER);ShaderSource(s,1,sources+i,NULL);CompileShader(s);GLint ok;GetShaderiv(s,GL_COMPILE_STATUS,&ok);if(!ok){GetShaderInfoLog(s,sizeof(lastError),NULL,lastError);fail(lastError);return 0;}AttachShader(p,s);}
    LinkProgram(p);GLint ok;GetProgramiv(p,GL_LINK_STATUS,&ok);if(!ok){GetProgramInfoLog(p,sizeof(lastError),NULL,lastError);fail(lastError);return 0;}return p;
}
CORE void uniformSlots(GLuint program,GLint *out,const char *names,int count){for(int i=0;i<count;i++){out[i]=GetUniformLocation(program,names);names+=strlen(names)+1;}}
CORE void targetTexture(GLuint t,int format,int w,int h){
    glBindTexture(GL_TEXTURE_2D,t);glTexImage2D(GL_TEXTURE_2D,0,format,w,h,0,GL_RGBA,GL_FLOAT,NULL);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
}
CORE void screenQuad(void){
    glBegin(GL_QUADS);glTexCoord2f(0,0);glVertex2f(-1,-1);glTexCoord2f(1,0);glVertex2f(1,-1);glTexCoord2f(1,1);glVertex2f(1,1);glTexCoord2f(0,1);glVertex2f(-1,1);glEnd();
}
CORE void depthTarget(GLuint texture,int w,int h){glBindTexture(GL_TEXTURE_2D,texture);glTexImage2D(GL_TEXTURE_2D,0,GL_DEPTH_COMPONENT24,w,h,0,GL_DEPTH_COMPONENT,GL_UNSIGNED_INT,NULL);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);}
CORE int resizeTargets(void){
    rw=MAX(320,width*(quality?1:.67f));rh=MAX(240,height*(quality?1:.67f));
    targetTexture(colorTex,GL_RGBA16F_ARB,rw,rh);
    depthTarget(depthTex,rw,rh);
    BindFramebuffer(GL_FRAMEBUFFER_EXT,fbo);FramebufferTexture2D(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,colorTex,0);FramebufferTexture2D(GL_FRAMEBUFFER_EXT,GL_DEPTH_ATTACHMENT_EXT,GL_TEXTURE_2D,depthTex,0);
    GLenum status=CheckFramebufferStatus(GL_FRAMEBUFFER_EXT);int format=GL_RGBA16F_ARB;
    if(status!=GL_FRAMEBUFFER_COMPLETE_EXT){format=GL_RGBA8;targetTexture(colorTex,format,rw,rh);status=CheckFramebufferStatus(GL_FRAMEBUFFER_EXT);}
    if(status!=GL_FRAMEBUFFER_COMPLETE_EXT)return 0;
    BindFramebuffer(GL_FRAMEBUFFER_EXT,mirrorFBO);targetTexture(mirrorTex,format,rw/2,rh/2);depthTarget(mirrorDepth,rw/2,rh/2);
    FramebufferTexture2D(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,mirrorTex,0);FramebufferTexture2D(GL_FRAMEBUFFER_EXT,GL_DEPTH_ATTACHMENT_EXT,GL_TEXTURE_2D,mirrorDepth,0);if(CheckFramebufferStatus(GL_FRAMEBUFFER_EXT)!=GL_FRAMEBUFFER_COMPLETE_EXT)return 0;
    BindFramebuffer(GL_FRAMEBUFFER_EXT,bloomFBO);
    for(int i=0;i<2;i++){targetTexture(bloomTex[i],format,rw/4,rh/4);FramebufferTexture2D(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,bloomTex[i],0);if(CheckFramebufferStatus(GL_FRAMEBUFFER_EXT)!=GL_FRAMEBUFFER_COMPLETE_EXT)return 0;}
    BindFramebuffer(GL_FRAMEBUFFER_EXT,0);return 1;
}
CORE int initGL(void){
    PIXELFORMATDESCRIPTOR pfd={0};pfd.nSize=sizeof(pfd);pfd.nVersion=1;pfd.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER;pfd.iPixelType=PFD_TYPE_RGBA;pfd.cColorBits=32;pfd.cDepthBits=24;pfd.iLayerType=PFD_MAIN_PLANE;
    dc=GetDC(window);int fmt=ChoosePixelFormat(dc,&pfd);if(!fmt||!SetPixelFormat(dc,fmt,&pfd))return fail("Could not initialize the Windows OpenGL surface.");
    rc=wglCreateContext(dc);if(!rc||!wglMakeCurrent(dc,rc))return fail("OpenGL initialization failed. A working graphics driver is required.");
    static const struct { void *slot; const char *name; } api[]={
#define GLFN(T,N,S) { &N,S },
#include "gl_api.h"
#undef GLFN
    };
    for(unsigned i=0;i<sizeof(api)/sizeof(api[0]);i++){PROC fn=wglGetProcAddress(api[i].name);if(!fn){snprintf(lastError,sizeof(lastError),"Missing OpenGL function: %s",api[i].name);return fail(lastError);}memcpy(api[i].slot,&fn,sizeof(fn));}

    logline((const char*)glGetString(GL_VENDOR));logline((const char*)glGetString(GL_RENDERER));logline((const char*)glGetString(GL_VERSION));
    swapInterval=(SwapIntervalFn)wglGetProcAddress("wglSwapIntervalEXT");if(swapInterval)swapInterval(testMode?0:1);
    DECOMPRESSOR_HANDLE decomp;SIZE_T decoded=0;
    if(!CreateDecompressor(COMPRESS_ALGORITHM_MSZIP,NULL,&decomp))return fail("Cannot initialize embedded shader data.");
    BOOL expanded=Decompress(decomp,(void*)shaderPack,sizeof(shaderPack),shaderText,sizeof(shaderText),&decoded);CloseDecompressor(decomp);
    if(!expanded||decoded!=sizeof(shaderText))return fail("Embedded shader data is damaged.");
    worldProgram=shaderProgram(sceneVS,sceneFS);postProgram=shaderProgram(postVS,postFS);bloomProgram=shaderProgram(postVS,bloomFS);depthProgram=shaderProgram("#version 120\nvoid main(){gl_Position=gl_ModelViewProjectionMatrix*gl_Vertex;}","#version 120\nvoid main(){gl_FragColor=vec4(1);}");if(!worldProgram||!postProgram||!depthProgram||!bloomProgram)return 0;
    uniformSlots(worldProgram,worldU,"time\0flash\0alert\0shadowM\0shadow\0L0\0L1\0L2\0L3\0L4\0L5\0L6\0T\0TD\0clipPlane\0mirrored",16);
    uniformSlots(postProgram,postU,"scene\0depth\0bloom\0pixel\0lens\0reflection\0time\0hurt",8);uniformSlots(bloomProgram,bloomU,"source\0step\0cutoff",3);
    UseProgram(worldProgram);Uniform1i(worldU[4],2);UseProgram(postProgram);for(int i=0;i<3;i++)Uniform1i(postU[i],i);Uniform1i(postU[5],3);UseProgram(bloomProgram);Uniform1i(bloomU[0],0);
    GenFramebuffers(1,&mirrorFBO);glGenTextures(1,&mirrorTex);glGenTextures(1,&mirrorDepth);GenFramebuffers(1,&bloomFBO);glGenTextures(2,bloomTex);GenFramebuffers(1,&fbo);glGenTextures(1,&colorTex);glGenTextures(1,&depthTex);if(!resizeTargets())return fail("Framebuffer initialization failed.");
    GenFramebuffers(1,&shadowFBO);glGenTextures(1,&shadowTex);glBindTexture(GL_TEXTURE_2D,shadowTex);glTexImage2D(GL_TEXTURE_2D,0,GL_DEPTH_COMPONENT24,1024,1024,0,GL_DEPTH_COMPONENT,GL_UNSIGNED_INT,NULL);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    BindFramebuffer(GL_FRAMEBUFFER_EXT,shadowFBO);FramebufferTexture2D(GL_FRAMEBUFFER_EXT,GL_DEPTH_ATTACHMENT_EXT,GL_TEXTURE_2D,shadowTex,0);glDrawBuffer(GL_NONE);glReadBuffer(GL_NONE);if(CheckFramebufferStatus(GL_FRAMEBUFFER_EXT)!=GL_FRAMEBUFFER_COMPLETE_EXT)return fail("Shadow framebuffer initialization failed.");BindFramebuffer(GL_FRAMEBUFFER_EXT,0);
    makeMeshes();makeFonts();makeWorld();glEnable(GL_NORMALIZE);return 1;
}
CORE void matmul(float *out,const float *a,const float *b){for(int j=0;j<4;j++)for(int i=0;i<4;i++){float s=0;for(int k=0;k<4;k++)s+=a[i+k*4]*b[k+j*4];out[i+j*4]=s;}}
CORE void camera(float offset){glMatrixMode(GL_MODELVIEW);glLoadIdentity();glTranslatef(-offset,offset*.45f,0);glRotatef(-pitch*180/PI,1,0,0);glRotatef(180-yaw*180/PI,0,1,0);glTranslatef(-player.x,-player.y,-player.z);}
CORE void lightPosition(int index,V3 p,const float *m){Uniform3f(worldU[5+index],m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12],m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13],m[2]*p.x+m[6]*p.y+m[10]*p.z+m[14]);}
CORE void render(void){
    if((rw!=(int)MAX(320,width*(quality?1:.67f))||rh!=(int)MAX(240,height*(quality?1:.67f)))&&!resizeTargets()){fail("Render target resize failed.");running=0;return;}
    float sp[16],sv[16],scombined[16],inverse[16]={0},shadowMatrix[16];
    glEnable(GL_CULL_FACE);glFrontFace(GL_CCW);
    BindFramebuffer(GL_FRAMEBUFFER_EXT,shadowFBO);glViewport(0,0,1024,1024);glClear(GL_DEPTH_BUFFER_BIT);UseProgram(depthProgram);glEnable(GL_DEPTH_TEST);glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE);glDisable(GL_TEXTURE_2D);glDisable(GL_BLEND);
    glMatrixMode(GL_PROJECTION);glLoadIdentity();glFrustum(-.11,.11,-.11,.11,.08,180);glGetFloatv(GL_PROJECTION_MATRIX,sp);camera(.35);glGetFloatv(GL_MODELVIEW_MATRIX,sv);
    glEnable(GL_POLYGON_OFFSET_FILL);glPolygonOffset(2,2);glCallList(worldList);drawMachinery();drawOrdnance();for(int i=0;i<enemyCount;i++)if((enemies[i].hp>0||gameTime-enemies[i].dead<8)&&visibleEnemy(enemies+i))drawEnemy(enemies+i);glDisable(GL_POLYGON_OFFSET_FILL);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);matmul(scombined,sp,sv);
    float m[16],n=.08f,fov=mouseRight?56:78,top=n*tan(fov*PI/360);
    for(int pass=0;pass<2;pass++){glFrontFace(pass?GL_CCW:GL_CW);
    BindFramebuffer(GL_FRAMEBUFFER_EXT,pass?fbo:mirrorFBO);glViewport(0,0,pass?rw:rw/2,pass?rh:rh/2);glClearColor(.025,.04,.05,0);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);glDisable(GL_BLEND);
    glMatrixMode(GL_PROJECTION);glLoadIdentity();glFrustum(-top*width/height,top*width/height,-top,top,n,180);
    glMatrixMode(GL_MODELVIEW);glLoadIdentity();glRotatef(-pitch*180/PI+(shake>0?sinf(totalTime*71)*shake*5:0),1,0,0);glRotatef(180-yaw*180/PI,0,1,0);
    glTranslatef(-player.x,-player.y-(mode==1&&grounded?sinf(stepPhase)*.018f:0),-player.z);
    if(!pass){glTranslatef(0,-.02f,0);glScalef(1,-1,1);}
    UseProgram(worldProgram);Uniform1f(worldU[15],pass?0:1);Uniform1f(worldU[0],totalTime);Uniform1f(worldU[2],bossAwake);
    glGetFloatv(GL_MODELVIEW_MATRIX,m);
    for(int i=0;i<3;i++){for(int j=0;j<3;j++)inverse[i+j*4]=m[j+i*4];inverse[12+i]=-(m[i*4]*m[12]+m[i*4+1]*m[13]+m[i*4+2]*m[14]);}inverse[15]=1;matmul(shadowMatrix,scombined,inverse);UniformMatrix4fv(worldU[3],1,GL_FALSE,shadowMatrix);
    Uniform4f(worldU[14],m[4],m[5],m[6],inverse[13]);
    ActiveTexture(GL_TEXTURE2);glBindTexture(GL_TEXTURE_2D,shadowTex);ActiveTexture(GL_TEXTURE0);
    lightPosition(0,v3(17.5*CELL,6,18.5*CELL),m);lightPosition(1,v3(17.5*CELL,5,6.5*CELL),m);
    lightPosition(2,v3(4.5*CELL,4.5,18.5*CELL),m);lightPosition(3,v3(29.5*CELL,5,18.5*CELL),m);
    lightPosition(4,v3(17.5*CELL,7,34.5*CELL),m);lightPosition(5,v3(17.5*CELL,5,25*CELL),m);
    V3 glowPos=add(player,mul(forward(),1));float glowStrength=gunKick,bestGlow=100;
    if(gunKick<.1f)for(int i=0;i<96;i++)if(bolts[i].life>0){float d=length(sub(bolts[i].p,player));if(d<bestGlow){bestGlow=d;glowPos=bolts[i].p;glowStrength=.7f;}}
    lightPosition(6,glowPos,m);Uniform1f(worldU[1],glowStrength);
    lightPosition(7,player,m);lightPosition(8,add(player,forward()),m);
    glCallList(worldList);drawDynamic();
    if(pass&&mode!=0&&mode!=4){glDepthRange(0,.03);drawWeapon();glDepthRange(0,1);}
    }
    glDisable(GL_CULL_FACE);glDisable(GL_DEPTH_TEST);UseProgram(bloomProgram);BindFramebuffer(GL_FRAMEBUFFER_EXT,bloomFBO);glViewport(0,0,rw/4,rh/4);ActiveTexture(GL_TEXTURE0);
    for(int i=0;i<2;i++){FramebufferTexture2D(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,bloomTex[i],0);glBindTexture(GL_TEXTURE_2D,i?bloomTex[0]:colorTex);Uniform2f(bloomU[1],i?0:4.f/rw,i?4.f/rh:0);Uniform1f(bloomU[2],i?0:.85f);screenQuad();}
    BindFramebuffer(GL_FRAMEBUFFER_EXT,0);glViewport(0,0,width,height);UseProgram(postProgram);ActiveTexture(GL_TEXTURE2);glBindTexture(GL_TEXTURE_2D,bloomTex[1]);ActiveTexture(GL_TEXTURE0);
    ActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,colorTex);
    ActiveTexture(GL_TEXTURE3);glBindTexture(GL_TEXTURE_2D,mirrorTex);ActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,depthTex);
    Uniform2f(postU[3],1.f/rw,1.f/rh);Uniform2f(postU[4],top/n*width/height,top/n);Uniform1f(postU[6],totalTime);Uniform1f(postU[7],damageFlash);
    screenQuad();
    ActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,0);ActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,0);drawUI();
}
CORE void capture(const char *path){
    int stride=(width*3+3)&~3;DWORD size=stride*height;unsigned char *pixels=malloc(size);if(!pixels)return;
    glPixelStorei(GL_PACK_ALIGNMENT,4);glReadBuffer(GL_BACK);glReadPixels(0,0,width,height,GL_BGR,GL_UNSIGNED_BYTE,pixels);
    BITMAPFILEHEADER fh={0};BITMAPINFOHEADER ih={0};fh.bfType=0x4d42;fh.bfSize=sizeof(fh)+sizeof(ih)+size;fh.bfOffBits=sizeof(fh)+sizeof(ih);ih.biSize=sizeof(ih);ih.biWidth=width;ih.biHeight=height;ih.biPlanes=1;ih.biBitCount=24;ih.biSizeImage=size;
    FILE *f=fopen(path,"wb");if(f){fwrite(&fh,sizeof(fh),1,f);fwrite(&ih,sizeof(ih),1,f);fwrite(pixels,size,1,f);fclose(f);}free(pixels);
}
CORE void cursorLock(int lock){
    if(lock&&focused){RECT r;GetClientRect(window,&r);POINT a={r.left,r.top},b={r.right,r.bottom};ClientToScreen(window,&a);ClientToScreen(window,&b);r.left=a.x;r.top=a.y;r.right=b.x;r.bottom=b.y;ClipCursor(&r);while(ShowCursor(FALSE)>=0);}
    else{ClipCursor(NULL);while(ShowCursor(TRUE)<0);}
}
CORE void startGame(void){resetGame();mode=1;memset(keys,0,sizeof(keys));memset(pressed,0,sizeof(pressed));mouseDown=mouseRight=0;cursorLock(1);}
CORE void toggleFullscreen(void){
    static RECT old;fullscreen=!fullscreen;
    if(fullscreen){GetWindowRect(window,&old);SetWindowLongPtr(window,GWL_STYLE,WS_POPUP|WS_VISIBLE);MONITORINFO mi={sizeof(mi)};GetMonitorInfo(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&mi);SetWindowPos(window,HWND_TOP,mi.rcMonitor.left,mi.rcMonitor.top,mi.rcMonitor.right-mi.rcMonitor.left,mi.rcMonitor.bottom-mi.rcMonitor.top,SWP_FRAMECHANGED);}
    else{SetWindowLongPtr(window,GWL_STYLE,WS_OVERLAPPEDWINDOW|WS_VISIBLE);SetWindowPos(window,HWND_NOTOPMOST,old.left,old.top,old.right-old.left,old.bottom-old.top,SWP_FRAMECHANGED);}
    cursorLock(mode==1);
}
static LRESULT CALLBACK wndProc(HWND hwnd,UINT msg,WPARAM w,LPARAM l){
    switch(msg){
    case WM_CLOSE:running=0;return 0;
    case WM_DESTROY:PostQuitMessage(0);return 0;
    case WM_SIZE:width=MAX(1,LOWORD(l));height=MAX(1,HIWORD(l));return 0;
    case WM_ACTIVATEAPP:focused=(int)w;if(!w){if(mode==1)mode=2;memset(keys,0,sizeof(keys));mouseDown=mouseRight=0;cursorLock(0);}return 0;
    case WM_INPUT:if(mode==1&&focused){RAWINPUT input;UINT size=sizeof(input);if(GetRawInputData((HRAWINPUT)l,RID_INPUT,&input,&size,sizeof(RAWINPUTHEADER))==sizeof(input)&&input.header.dwType==RIM_TYPEMOUSE)look(input.data.mouse.lLastX,input.data.mouse.lLastY);}return DefWindowProc(hwnd,msg,w,l);
    case WM_LBUTTONDOWN:if(mode==1)mouseDown=1;else if(mode==0){POINT p={LOWORD(l),HIWORD(l)};if(p.x>=width*.105f&&p.x<width*.105f+316&&p.y>=height*.2f+260&&p.y<height*.2f+309)startGame();}return 0;
    case WM_LBUTTONUP:mouseDown=0;return 0;
    case WM_RBUTTONDOWN:mouseRight=1;return 0;
    case WM_RBUTTONUP:mouseRight=0;return 0;
    case WM_MOUSEWHEEL:if(mode==1){weapon=(weapon+(GET_WHEEL_DELTA_WPARAM(w)>0?1:2))%3;reloadTime=0;}return 0;
    case WM_KEYDOWN:case WM_SYSKEYDOWN:
        if(w<256){if(!keys[w])pressed[w]=1;keys[w]=1;}
        if(l&(1<<30))return 0;
        if(w==VK_F4&&(l&(1<<29))){running=0;return 0;}
        if(w==VK_END){running=0;return 0;}
        if(w==VK_F11){toggleFullscreen();return 0;}
        if(w=='M'){mute=!mute;return 0;}
        if(w=='Q'&&mode!=1){quality=!quality;return 0;}
        if(w==VK_OEM_MINUS)mouseSensitivity=MAX(.0004,mouseSensitivity-.0002);
        if(w==VK_OEM_PLUS)mouseSensitivity=MIN(.008,mouseSensitivity+.0002);
        if(mode==0&&(w==VK_LEFT||w==VK_RIGHT))difficulty=(difficulty+(w==VK_LEFT?2:1))%3;
        if(w==VK_RETURN){if(mode==0||mode==3||mode==4)startGame();else if(mode==2){mode=1;cursorLock(1);memset(keys,0,sizeof(keys));}return 0;}
        if(w==VK_ESCAPE){if(mode==1){mode=2;cursorLock(0);mouseDown=0;}else if(mode==2){mode=1;cursorLock(1);memset(keys,0,sizeof(keys));}else if(mode==0)running=0;return 0;}
        if(w=='N'&&mode==2){startGame();return 0;}return 0;
    case WM_KEYUP:case WM_SYSKEYUP:if(w<256)keys[w]=0;return 0;
    }
    return DefWindowProc(hwnd,msg,w,l);
}

CORE int check(int condition,const char *name){if(logFile)fprintf(logFile,"%s %s\n",condition?"PASS":"FAIL",name);return !condition;}
#ifdef NULLPOINT_TESTS
#include "tests.h"
#endif
CORE int verifyGraphics(void){
    logline("Graphics verification started");int failures=0;
    for(int angle=0;angle<4;angle++)for(int k=0;k<4;k++){
        resetGame();mode=1;yaw=angle*PI/2;pitch=0;camera(0);float view[16];glGetFloatv(GL_MODELVIEW_MATRIX,view);V3 before=player;memset(keys,0,sizeof(keys));keys[(unsigned char)"WASD"[k]]=1;for(int tick=0;tick<12;tick++)simulate(1.f/120);V3 delta=sub(player,before);float vx=view[0]*delta.x+view[8]*delta.z,vz=view[2]*delta.x+view[10]*delta.z;
        failures+=check(k==0?vz<-.2f:k==1?vx<-.2f:k==2?vz>.2f:vx>.2f,"WASD movement follows rendered camera basis");
    }
    memset(keys,0,sizeof(keys));for(int axis=0;axis<4;axis++){resetGame();V3 original=add(player,mul(forward(),10));look(axis==0?100:axis==1?-100:0,axis==2?-100:axis==3?100:0);camera(0);float view[16];glGetFloatv(GL_MODELVIEW_MATRIX,view);float xx=view[0]*original.x+view[4]*original.y+view[8]*original.z+view[12],yy=view[1]*original.x+view[5]*original.y+view[9]*original.z+view[13];failures+=check(axis==0?xx<0:axis==1?xx>0:axis==2?yy<0:yy>0,"Mouse look direction follows rendered camera");}
    resetGame();mode=0;player=cellpos(14,13);player.y=2.4;yaw=.85;pitch=-.03;totalTime=7;render();glFinish();capture("01-title.bmp");
    mode=1;player=cellpos(17,4);player.y=1.65;yaw=0;pitch=0;render();glFinish();capture("02-arrival.bmp");
    player=cellpos(14,14);player.y=1.65;yaw=.62;pitch=.02;render();glFinish();capture("03-reactor.bmp");
    player=cellpos(4,19);player.y=1.65;yaw=.28;pitch=-.05;render();glFinish();capture("04-relay.bmp");
    player=cellpos(17,32);player.y=1.65;yaw=0;pitch=.04;bossAwake=1;relayCount=3;gateLift=6;weapon=2;for(int i=0;i<enemyCount;i++)enemies[i].yaw=atan2f(player.x-enemies[i].p.x,player.z-enemies[i].p.z)*180/PI;render();glFinish();capture("05-warden.bmp");
    int ok=glGetError()==GL_NO_ERROR;logline(ok?"PASS OpenGL error check":"FAIL OpenGL error check");
    LARGE_INTEGER a,b;QueryPerformanceCounter(&a);for(int i=0;i<90;i++){totalTime+=1.f/60;render();}glFinish();QueryPerformanceCounter(&b);
    char s[256];snprintf(s,sizeof(s),"90 full-resolution frames: %.2f ms average",(b.QuadPart-a.QuadPart)*1000./frequency.QuadPart/90);logline(s);
    for(int i=0;i<enemyCount;i++)enemies[i].cd=0;
    mouseDown=1;for(int i=0;i<75;i++){totalTime+=1.f/60;advanceFrame(1.f/60);render();}mouseDown=0;glFinish();capture("06-combat.bmp");
    failures+=check(glGetError()==GL_NO_ERROR,"Animated combat and particle rendering");
    resetGame();mode=1;player=cellpos(4,7);player.y=1.65f;yaw=.95f;pitch=0;render();glFinish();capture("07-service.bmp");
    yaw=2.35f;pitch=.1f;throwGrenade();for(int i=0;i<255;i++){totalTime+=1.f/120;simulate(1.f/120);}render();glFinish();capture("08-grenade.bmp");
    failures+=check(glGetError()==GL_NO_ERROR,"Expanded wing and grenade rendering");
    return !ok||failures;
}
int WINAPI WinMain(HINSTANCE instance,HINSTANCE previous,LPSTR cmd,int show){
    (void)previous;(void)show;quality=strstr(cmd,"--performance")?0:1;testMode=strstr(cmd,"--verify")!=NULL;
#ifdef NULLPOINT_TESTS
    if(strstr(cmd,"--selftest"))return selftest();if(strstr(cmd,"--playtest"))return campaignTest(strstr(cmd,"--veteran")?2:strstr(cmd,"--operative")?1:0);
#endif
    if(testMode)logFile=fopen("graphics.txt","w");
    SetProcessDPIAware();WNDCLASSA wc={0};wc.style=CS_OWNDC;wc.lpfnWndProc=wndProc;wc.hInstance=instance;wc.hCursor=LoadCursor(NULL,IDC_ARROW);wc.lpszClassName="NULLPOINT96";RegisterClassA(&wc);
    RECT wr={0,0,width,height};AdjustWindowRect(&wr,WS_OVERLAPPEDWINDOW,FALSE);
    window=CreateWindowA(wc.lpszClassName,"NULLPOINT // NATIVE 96K",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,wr.right-wr.left,wr.bottom-wr.top,NULL,NULL,instance,NULL);
    if(!window)return 1;resetGame();if(!initGL()){if(logFile)fclose(logFile);DestroyWindow(window);return 2;}
    QueryPerformanceFrequency(&frequency);QueryPerformanceCounter(&lastCounter);
    if(testMode){int result=verifyGraphics();wglMakeCurrent(NULL,NULL);wglDeleteContext(rc);ReleaseDC(window,dc);DestroyWindow(window);fclose(logFile);return result;}
    RAWINPUTDEVICE raw={1,2,0,window};RegisterRawInputDevices(&raw,1,sizeof(raw));initAudio();timeBeginPeriod(1);
    ShowWindow(window,SW_SHOW);UpdateWindow(window);
    player=cellpos(14,13);player.y=2.4;yaw=.85;pitch=-.03;
    while(running){
        MSG msg;while(PeekMessage(&msg,NULL,0,0,PM_REMOVE)){if(msg.message==WM_QUIT)running=0;TranslateMessage(&msg);DispatchMessage(&msg);}
        LARGE_INTEGER now;QueryPerformanceCounter(&now);float dt=(now.QuadPart-lastCounter.QuadPart)/(double)frequency.QuadPart;lastCounter=now;dt=CLAMP(dt,0,.1f);totalTime+=dt;
        if(mode==0)yaw=.85f+sinf(totalTime*.09f)*.13f;
        advanceFrame(dt);
        pumpAudio();if(!IsIconic(window)){render();SwapBuffers(dc);}else Sleep(30);
        QueryPerformanceCounter(&now);lastFrameMS=(now.QuadPart-lastCounter.QuadPart)*1000./frequency.QuadPart;
        if(lastFrameMS<7)Sleep((DWORD)(7-lastFrameMS));
    }
    cursorLock(0);stopAudio();timeEndPeriod(1);wglMakeCurrent(NULL,NULL);wglDeleteContext(rc);ReleaseDC(window,dc);DestroyWindow(window);return 0;
}
