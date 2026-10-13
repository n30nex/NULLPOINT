/* NULLPOINT - an original, entirely procedural native 96K FPS.
   Build with build.ps1. Windows/OpenGL system DLLs only. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
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
typedef struct {V3 p; float yaw,hp,cd,phase,hit; int type,active;} Enemy;
typedef struct {V3 p,v; float life,maxlife,size; int type;} Particle;
typedef struct {V3 p,v; float life; int owner;} Bolt;
typedef struct {V3 p; float sx,sy,sz; int kind,active;} Prop;
typedef struct {V3 p; int type,active;} Pickup;
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
static V3 player; static float yaw,pitch,vy,health,armor,gameTime,totalTime,damageFlash,hitFlash,gunKick,shootCD,reloadTime,stepPhase,shake,toastTime,extraction;
static int weapon,ammo[3],reserve[3],kills,relayCount,bossAwake,bossDead,grounded,shotsFired,hitsMade,deaths,menuIndex=0,difficulty=1;
static const int magazine[3]={30,8,6};
static const char *weaponName[3]={"VX-30 / PULSE CARBINE","BR-8 / BREACH SHOTGUN","ARC-6 / ION LANCE"};
static char toast[128],lastError[4096];
static float mouseSensitivity=.0021f;
static unsigned rng=0x496f6e;
static GLuint worldList,worldProgram,postProgram,depthProgram,fbo,colorTex,depthTex,fontList,shadowFBO,shadowTex;
static int rw,rh;
static float lastFrameMS=16;
static LARGE_INTEGER frequency,lastCounter;
static FILE *logFile;

#define GLFN(T,N) static T N
GLFN(PFNGLCREATESHADERPROC,CreateShader); GLFN(PFNGLSHADERSOURCEPROC,ShaderSource);
GLFN(PFNGLCOMPILESHADERPROC,CompileShader); GLFN(PFNGLGETSHADERIVPROC,GetShaderiv);
GLFN(PFNGLGETSHADERINFOLOGPROC,GetShaderInfoLog); GLFN(PFNGLCREATEPROGRAMPROC,CreateProgram);
GLFN(PFNGLATTACHSHADERPROC,AttachShader); GLFN(PFNGLLINKPROGRAMPROC,LinkProgram);
GLFN(PFNGLGETPROGRAMIVPROC,GetProgramiv); GLFN(PFNGLGETPROGRAMINFOLOGPROC,GetProgramInfoLog);
GLFN(PFNGLUSEPROGRAMPROC,UseProgram); GLFN(PFNGLGETUNIFORMLOCATIONPROC,GetUniformLocation);
GLFN(PFNGLUNIFORM1FPROC,Uniform1f); GLFN(PFNGLUNIFORM1IPROC,Uniform1i);
GLFN(PFNGLUNIFORM2FPROC,Uniform2f); GLFN(PFNGLUNIFORM3FPROC,Uniform3f);
GLFN(PFNGLUNIFORMMATRIX4FVPROC,UniformMatrix4fv);
GLFN(PFNGLACTIVETEXTUREPROC,ActiveTexture);
GLFN(PFNGLGENFRAMEBUFFERSEXTPROC,GenFramebuffers); GLFN(PFNGLBINDFRAMEBUFFEREXTPROC,BindFramebuffer);
GLFN(PFNGLFRAMEBUFFERTEXTURE2DEXTPROC,FramebufferTexture2D);
GLFN(PFNGLCHECKFRAMEBUFFERSTATUSEXTPROC,CheckFramebufferStatus);
typedef BOOL (WINAPI *SwapIntervalFn)(int);
static SwapIntervalFn swapInterval;

static V3 v3(float x,float y,float z){V3 a={x,y,z};return a;}
static V3 add(V3 a,V3 b){return v3(a.x+b.x,a.y+b.y,a.z+b.z);}
static V3 sub(V3 a,V3 b){return v3(a.x-b.x,a.y-b.y,a.z-b.z);}
static V3 mul(V3 a,float t){return v3(a.x*t,a.y*t,a.z*t);}
static float dot(V3 a,V3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
static float length(V3 a){return (float)sqrt(dot(a,a));}
static V3 norm(V3 a){float l=length(a);return mul(a,1/MAX(l,.0001f));}
static float rnd(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return (rng&65535)/65535.f;}
static float dsq(float x,float z){return x*x+z*z;}
static V3 cellpos(int x,int z){return v3((x+.5f)*CELL,0,(z+.5f)*CELL);}
static float decay(float t,float d){return MAX(0,t-d);}
static void note(const char *s){strncpy(toast,s,127);toast[127]=0;toastTime=4;}
static void logline(const char *s){if(logFile){fprintf(logFile,"%s\n",s);fflush(logFile);}}
static int solid(int x,int z){return x<0||x>=MW||z<0||z>=MH||map[z][x]==1||(map[z][x]==2&&relayCount<3);}
static int wallAt(float x,float z){return solid((int)floor(x/CELL),(int)floor(z/CELL));}
static int collides(float x,float z,float r){
    if(wallAt(x-r,z-r)||wallAt(x+r,z-r)||wallAt(x-r,z+r)||wallAt(x+r,z+r))return 1;
    for(int i=0;i<propCount;i++){Prop *p=props+i;if(p->kind<3&&fabs(x-p->p.x)<p->sx*.5f+r&&fabs(z-p->p.z)<p->sz*.5f+r)return 1;}
    return 0;
}
static void moveBody(V3 *p,float dx,float dz,float radius){
    int n=1+(int)(MAX(fabs(dx),fabs(dz))/.16f);dx/=n;dz/=n;
    for(int i=0;i<n;i++){if(!collides(p->x+dx,p->z,radius))p->x+=dx;if(!collides(p->x,p->z+dz,radius))p->z+=dz;}
}
static int clearSight(V3 a,V3 b){V3 d=sub(b,a);float l=length(d);int n=(int)(l/.32f)+1;d=mul(d,1.f/n);for(int i=1;i<n;i++){a=add(a,d);if(wallAt(a.x,a.z))return 0;}return 1;}
static void carve(int x,int z,int w,int h){for(int j=z;j<z+h;j++)for(int i=x;i<x+w;i++)map[j][i]=0;}
static void prop(int x,int z,float sx,float sy,float sz,int kind){Prop *p=props+propCount++;p->p=cellpos(x,z);p->sx=sx;p->sy=sy;p->sz=sz;p->kind=kind;p->active=1;}
static void pickup(float x,float z,int type){if(pickupCount<128){Pickup *p=pickups+pickupCount++;p->p=v3(x,.5,z);p->type=type;p->active=1;}}
static void spawn(int x,int z,int type){Enemy *e=enemies+enemyCount++;memset(e,0,sizeof(*e));e->p=cellpos(x,z);e->type=type;e->hp=type==3?1400:type==2?180:type==1?65:90;e->phase=rnd()*6.28f;e->cd=1+rnd()*2;}
static void buildMap(void){
    memset(map,1,sizeof(map));for(int j=0;j<MH;j++)for(int i=0;i<MW;i++)map[j][i]=1;
    carve(13,2,9,7);carve(16,9,3,4);carve(10,12,15,13);
    carve(8,17,3,3);carve(1,13,8,11);carve(24,17,3,3);carve(26,13,8,11);
    carve(16,25,3,5);carve(9,29,17,11);carve(16,40,3,2);
    for(int i=16;i<=18;i++)map[28][i]=2;
    propCount=pickupCount=enemyCount=0;
    prop(14,5,1.8,1.4,1.8,0);prop(20,7,1.5,2.6,1.5,1);
    prop(13,15,1.8,4,1.8,1);prop(21,15,1.8,4,1.8,1);
    prop(13,21,1.8,4,1.8,1);prop(21,21,1.8,4,1.8,1);
    prop(17,18,3.6,5.5,3.6,2);
    prop(11,23,1.6,1.4,1.6,0);prop(23,13,1.6,1.4,1.6,0);
    prop(3,15,1.5,3.8,1.6,1);prop(6,15,1.5,3.8,1.6,1);prop(3,18,1.5,3.8,1.6,1);
    prop(28,15,1.8,1.6,1.8,0);prop(31,18,2.1,2.2,2.1,0);prop(28,22,1.3,2.8,1.3,1);
    prop(12,32,1.8,5,1.8,1);prop(22,32,1.8,5,1.8,1);prop(12,37,1.8,5,1.8,1);prop(22,37,1.8,5,1.8,1);
    relays[0].p=cellpos(5,22);relays[1].p=cellpos(30,22);relays[2].p=cellpos(17,24);
    for(int i=0;i<3;i++)relays[i].on=0;
    spawn(17,11,0);spawn(12,14,0);spawn(22,14,1);spawn(12,20,0);spawn(22,21,2);spawn(19,23,1);
    spawn(5,14,1);spawn(7,19,0);spawn(2,21,0);spawn(6,21,2);
    spawn(29,14,0);spawn(32,16,1);spawn(27,20,0);spawn(30,21,2);
    spawn(15,31,0);spawn(20,31,0);spawn(17,35,3);spawn(11,36,1);spawn(23,36,1);
    for(int i=0;i<enemyCount;i++)enemies[i].active=0;
    V3 q=cellpos(20,4);pickup(q.x,q.z,0);q=cellpos(14,8);pickup(q.x,q.z,1);
    q=cellpos(11,16);pickup(q.x,q.z,2);q=cellpos(23,22);pickup(q.x,q.z,1);
    q=cellpos(2,16);pickup(q.x,q.z,0);q=cellpos(7,23);pickup(q.x,q.z,1);
    q=cellpos(32,14);pickup(q.x,q.z,2);q=cellpos(27,23);pickup(q.x,q.z,0);
    q=cellpos(15,27);pickup(q.x+CELL,q.z,1);q=cellpos(10,30);pickup(q.x,q.z,0);q=cellpos(24,38);pickup(q.x,q.z,0);
}
static void resetGame(void){
    rng=0x496f6e;relayCount=bossAwake=bossDead=kills=shotsFired=hitsMade=0;
    memset(parts,0,sizeof(parts));memset(bolts,0,sizeof(bolts));memset(seen,0,sizeof(seen));
    buildMap();player=cellpos(17,4);player.y=1.65f;yaw=0;pitch=0;vy=0;health=100;armor=50;
    for(int i=0;i<3;i++){ammo[i]=magazine[i];reserve[i]=i==0?180:i==1?40:24;}
    weapon=0;grounded=1;gameTime=damageFlash=hitFlash=gunKick=shootCD=reloadTime=extraction=0;
    note("REACTOR OFFLINE. SEVER ALL THREE RELAYS.");
}

static const char *sceneVS=
"#version 120\n"
"varying vec3 P,N;varying vec4 C;void main(){vec4 p=gl_ModelViewMatrix*gl_Vertex;P=p.xyz;N=normalize(gl_NormalMatrix*gl_Normal);C=gl_Color;gl_Position=gl_ProjectionMatrix*p;}";
static const char *sceneFS=
"#version 120\n"
"varying vec3 P,N;varying vec4 C;uniform float time,flash,alert;uniform vec3 L0,L1,L2,L3,L4,L5,L6;uniform mat4 shadowM;uniform sampler2D shadow;"
"float hash(vec3 p){return fract(sin(dot(p,vec3(127.1,311.7,74.7)))*43758.5453);}"
"float noise(vec3 p){vec3 i=floor(p),f=fract(p);f=f*f*(3.-2.*f);return mix(mix(mix(hash(i),hash(i+vec3(1,0,0)),f.x),mix(hash(i+vec3(0,1,0)),hash(i+vec3(1,1,0)),f.x),f.y),mix(mix(hash(i+vec3(0,0,1)),hash(i+vec3(1,0,1)),f.x),mix(hash(i+vec3(0,1,1)),hash(i+vec3(1,1,1)),f.x),f.y),f.z);}"
"vec3 light(vec3 p,vec3 c,vec3 n,float rough){vec3 d=p-P;float l=length(d);vec3 v=normalize(-P);vec3 h=normalize(normalize(d)+v);return c*(max(dot(n,normalize(d)),0.)+pow(max(dot(n,h),0.),rough)*.65)/(1.+l*l*.045);}"
"void main(){vec3 n=normalize(N);float mat=floor(C.a+.1);vec3 b=C.rgb;"
"vec3 wp=(gl_ModelViewMatrixInverse*vec4(P,1)).xyz;vec3 wn=normalize((gl_ModelViewMatrixTranspose*vec4(n,0)).xyz);"
"vec3 an=abs(wn);vec2 uv=an.y>an.x&&an.y>an.z?wp.xz:(an.x>an.z?wp.zy:wp.xy);"
"float grit=noise(wp*3.)*.55+noise(wp*34.)*.25+hash(floor(wp*100.))*.2;float seam=1.;float rough=28.;"
"if(mat<.5){vec2 u=fract(uv/1.4);float edge=min(min(u.x,1.-u.x),min(u.y,1.-u.y));seam=mix(.27,1.,smoothstep(.012,.034,edge));vec2 r=abs(u-.5);float bolt=length(r-vec2(.43));seam*=mix(.36,1.,smoothstep(.012,.023,bolt));b*=seam*(.68+grit*.6);float rib=step(.91,fract(uv.y*7.))*step(.36,u.x)*step(u.x,.64);b*=1.-rib*.35;}"
"else if(mat<1.5){vec2 u=fract(uv*.714);float edge=min(min(u.x,1.-u.x),min(u.y,1.-u.y));float aa=fwidth(edge);b*=mix(.35,1.,smoothstep(.018-aa,.035+aa,edge))*(.67+grit*.55);rough=50.;}"
"else if(mat<2.5){float stripe=step(.5,fract((uv.x+uv.y)*1.7));b=mix(vec3(.045,.06,.065),b,stripe)*(.6+grit*.6);}"
"else if(mat<3.5){b*=.65+grit*.55;rough=70.;}"
"else if(mat<4.5){b*=.58+.42*sin(uv.y*120.)*sin(uv.y*120.);}"
"float bump=(noise(wp*42.)-.5)*.13;n=normalize(n+vec3(bump,bump*.3,-bump));"
"vec3 lum=vec3(.19,.25,.29)+light(L0,vec3(.45,1.65,1.8),n,rough)+light(L1,vec3(2.5,1.25,.42),n,rough)+light(L2,vec3(.25,1.35,2.),n,rough)+light(L3,vec3(2.8,.45,.16),n,rough)+light(L4,vec3(.3,1.1,1.7),n,rough)+light(L5,vec3(1.9,.95,.36),n,rough);"
"vec4 sp=shadowM*vec4(P,1);vec3 sc=sp.xyz/sp.w*.5+.5;float shade=0.;for(int j=0;j<4;j++){vec2 o=vec2(mod(float(j),2.),floor(float(j)/2.))-.5;shade+=step(sc.z-.0012,texture2D(shadow,sc.xy+o/1024.).r)*.25;}if(sc.x<0.||sc.x>1.||sc.y<0.||sc.y>1.||sp.w<0.)shade=1.;"
"float torch=pow(max(dot(normalize(P),vec3(0,0,-1)),0.),16.);lum+=light(vec3(.35,.2,0),vec3(1.5,1.65,1.75)*torch*shade,n,rough);lum*=.65+.35*shade;lum+=light(L6,vec3(3.2,1.6,.3)*flash,n,rough);"
"vec3 color=b*lum;if(mat>4.5){color=b*(1.7+.22*sin(time*2.+wp.y*3.));}"
"float fog=1.-exp(-length(P)*.016);color=mix(color,vec3(.026,.043,.052),fog);gl_FragColor=vec4(color,1.);}";
static const char *postVS="#version 120\nvarying vec2 uv;void main(){uv=gl_MultiTexCoord0.xy;gl_Position=gl_Vertex;}";
static const char *postFS=
"#version 120\n"
"varying vec2 uv;uniform sampler2D scene,depth;uniform vec2 pixel;uniform float time,hurt;"
"float z(vec2 p){float d=texture2D(depth,p).r;return .16/(1.000889-d*.999111);}"
"void main(){vec3 c=texture2D(scene,uv).rgb;float a=z(uv),occ=0.;vec3 glow=vec3(0);"
"for(int i=0;i<8;i++){float t=float(i)*.785398;vec2 v=vec2(cos(t),sin(t));float d=z(uv+v*pixel*7.);occ+=step(.13,a-d)*exp(-abs(a-d)*.55);vec3 s=texture2D(scene,uv+v*pixel*5.).rgb;glow+=max(s-.65,0.);}"
"c=c*(1.-occ*.032)+glow*.08;vec2 q=uv*2.-1.;c*=1.-dot(q,q)*.17;c=c/(c+vec3(.68));c=pow(c,vec3(.85));"
"c=mix(c,vec3(.65,.045,.008),hurt*dot(q,q)*.45);float grain=fract(sin(dot(uv+time*.0001,vec2(12.9898,78.233)))*43758.5453);c+=(grain-.5)*.012;gl_FragColor=vec4(c,1);}";

static void color(float r,float g,float b,int mat){glColor4f(r,g,b,(float)mat);}
static void box(float x,float y,float z,float sx,float sy,float sz){
    float a=x-sx*.5f,b=x+sx*.5f,c=y-sy*.5f,d=y+sy*.5f,e=z-sz*.5f,f=z+sz*.5f;
    glBegin(GL_QUADS);
    glNormal3f(0,0,-1);glVertex3f(a,c,e);glVertex3f(a,d,e);glVertex3f(b,d,e);glVertex3f(b,c,e);
    glNormal3f(0,0,1);glVertex3f(b,c,f);glVertex3f(b,d,f);glVertex3f(a,d,f);glVertex3f(a,c,f);
    glNormal3f(-1,0,0);glVertex3f(a,c,f);glVertex3f(a,d,f);glVertex3f(a,d,e);glVertex3f(a,c,e);
    glNormal3f(1,0,0);glVertex3f(b,c,e);glVertex3f(b,d,e);glVertex3f(b,d,f);glVertex3f(b,c,f);
    glNormal3f(0,1,0);glVertex3f(a,d,e);glVertex3f(a,d,f);glVertex3f(b,d,f);glVertex3f(b,d,e);
    glNormal3f(0,-1,0);glVertex3f(a,c,f);glVertex3f(a,c,e);glVertex3f(b,c,e);glVertex3f(b,c,f);glEnd();
}
static void tube(float x,float y,float z,float radius,float h,int sides){
    glBegin(GL_QUAD_STRIP);for(int i=0;i<=sides;i++){float a=i*2*PI/sides,c=cos(a),s=sin(a);glNormal3f(c,0,s);glVertex3f(x+c*radius,y-h*.5f,z+s*radius);glVertex3f(x+c*radius,y+h*.5f,z+s*radius);}glEnd();
    for(int k=-1;k<=1;k+=2){glBegin(GL_TRIANGLE_FAN);glNormal3f(0,k,0);glVertex3f(x,y+k*h*.5f,z);for(int i=0;i<=sides;i++){float a=k*i*2*PI/sides;glVertex3f(x+cos(a)*radius,y+k*h*.5f,z+sin(a)*radius);}glEnd();}
}
static void ring(float x,float y,float z,float radius,float thick,int sides){
    glBegin(GL_QUADS);for(int i=0;i<sides;i++){float a=i*2*PI/sides,b=(i+1)*2*PI/sides;glNormal3f(0,1,0);glVertex3f(x+cos(a)*(radius-thick),y,z+sin(a)*(radius-thick));glVertex3f(x+cos(a)*radius,y,z+sin(a)*radius);glVertex3f(x+cos(b)*radius,y,z+sin(b)*radius);glVertex3f(x+cos(b)*(radius-thick),y,z+sin(b)*(radius-thick));}glEnd();
}
static void beam(V3 a,V3 b,float radius){V3 d=sub(b,a);float l=length(d);glPushMatrix();glTranslatef(a.x,a.y,a.z);glRotatef(acos(CLAMP(d.y/MAX(l,.001),-1,1))*180/PI,d.z,0,-d.x);tube(0,l*.5f,0,radius,l,8);glPopMatrix();}
static float ceiling(int x,int z){if(z>=29)return 10; if(x>=10&&x<=24&&z>=12&&z<=24)return 8.4;return 5.6;}
static void sign3d(float x,float y,float z,float angle,float scale,const char *s){
    glPushMatrix();glTranslatef(x,y,z);glRotatef(angle,0,1,0);glScalef(scale,scale,scale);glListBase(fontList-32);glCallLists(strlen(s),GL_UNSIGNED_BYTE,s);glPopMatrix();
}
static void makeWorld(void){
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
        Prop *p=props+i;float x=p->p.x,z=p->p.z,h=p->sy;
        if(p->kind==0){
            color(.3,.33,.28,0);box(x,h/2,z,p->sx,h,p->sz);
            color(.63,.32,.055,2);box(x,h*.7,z-p->sz*.5f-.01,p->sx*.96,.28,.03);
            color(.1,.14,.15,3);for(int k=-1;k<=1;k+=2){box(x+k*p->sx*.35,h/2,z,.09,h+.06,p->sz+.09);box(x,h/2,z+k*p->sz*.37,p->sx+.09,h+.06,.09);}
        }else if(p->kind==1){
            color(.11,.16,.18,3);box(x,h/2,z,p->sx,h,p->sz);
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
            for(int j=0;j<8;j++){float a=j*PI/4;float px=x+sin(a)*1.85,pz=z+cos(a)*1.85;
                color(.21,.27,.28,3);tube(px,2.8,pz,.13,4.4,8);color(.07,.8,.95,5);tube(px,2.8,pz,.066,3.4,8);
                color(.55,.3,.06,2);beam(v3(px,4.8,pz),v3(x+sin(a)*.9,5.8,z+cos(a)*.9),.18);
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
    V3 e=cellpos(17,41);color(.025,.8,.6,5);box(e.x,.015,e.z,7.8,.02,3.8);
    color(.52,.67,.62,5);sign3d(18.85*CELL,3.2,8.98*CELL,180,.62,"REACTOR 09");
    color(.03,.48,.7,5);sign3d(9.98*CELL,4.1,16.98*CELL,90,.62,"01 / COOLANT");
    color(.8,.32,.05,5);sign3d(25.02*CELL,4.1,20.02*CELL,-90,.62,"02 / FOUNDRY");
    color(.7,.36,.065,5);sign3d(18.9*CELL,4.8,27.99*CELL,180,.6,"CONTAINMENT");
    color(.03,.75,.45,5);sign3d(18.7*CELL,3.4,41.97*CELL,180,.62,"EXTRACTION");
    glEndList();
}

/* Small streaming synthesizer. All buffers persist until waveOut returns them. */
typedef struct {int kind;float t,duration,volume,pan;} Voice;
static Voice voices[24];static int voiceIndex;
static HWAVEOUT audioOut;static WAVEHDR audioHeaders[4];static short audioData[4][1024*2];
static double audioTime;static float bassPhase;static unsigned soundRng=173;
static void sound(int kind,float vol,float pan){if(!audioOut)return;Voice *v=voices+(voiceIndex++%24);v->kind=kind;v->t=0;v->duration=kind==0?.17f:kind==1?.37f:kind==2?.6f:kind==3?.8f:kind==4?.08f:kind==5?.5f:1.2f;v->volume=vol;v->pan=CLAMP(pan,-1,1);}
static void initAudio(void){
    WAVEFORMATEX f={0};f.wFormatTag=WAVE_FORMAT_PCM;f.nChannels=2;f.nSamplesPerSec=22050;f.wBitsPerSample=16;f.nBlockAlign=4;f.nAvgBytesPerSec=f.nSamplesPerSec*4;
    if(waveOutOpen(&audioOut,WAVE_MAPPER,&f,0,0,CALLBACK_NULL)!=MMSYSERR_NOERROR){audioOut=0;return;}
    for(int i=0;i<4;i++){audioHeaders[i].lpData=(char*)audioData[i];audioHeaders[i].dwBufferLength=sizeof(audioData[i]);waveOutPrepareHeader(audioOut,audioHeaders+i,sizeof(WAVEHDR));audioHeaders[i].dwFlags|=WHDR_DONE;}
}
static void pumpAudio(void){
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
static void stopAudio(void){if(audioOut){waveOutReset(audioOut);for(int i=0;i<4;i++)waveOutUnprepareHeader(audioOut,audioHeaders+i,sizeof(WAVEHDR));waveOutClose(audioOut);audioOut=0;}}
static void particles(V3 p,int count,int type,float strength){
    for(int i=0;i<count;i++){Particle *s=parts+(partIndex++%MAXP);s->p=p;s->v=v3((rnd()-.5f)*strength,rnd()*strength,(rnd()-.5f)*strength);s->life=s->maxlife=.18f+rnd()*.8f;s->size=.018+rnd()*.04;s->type=type;}
}
static void hurtPlayer(float amount){
    if(mode!=1)return;amount*=difficulty==0?.55f:difficulty==2?1.4f:1;
    float absorb=MIN(armor,amount*.6f);armor-=absorb;health-=amount-absorb;damageFlash=.7f;shake=.13f;sound(4,.5,0);
    if(health<=0){health=0;mode=3;deaths++;mouseDown=0;ClipCursor(NULL);while(ShowCursor(TRUE)<0);sound(6,.8,0);}
}
static float rayBox(V3 o,V3 d,V3 lo,V3 hi){
    float tmin=0,tmax=200;float aa[3]={o.x,o.y,o.z},dd[3]={d.x,d.y,d.z},ll[3]={lo.x,lo.y,lo.z},hh[3]={hi.x,hi.y,hi.z};
    for(int i=0;i<3;i++){if(fabs(dd[i])<1e-6){if(aa[i]<ll[i]||aa[i]>hh[i])return 200;}else{float a=(ll[i]-aa[i])/dd[i],b=(hh[i]-aa[i])/dd[i];if(a>b){float t=a;a=b;b=t;}tmin=MAX(tmin,a);tmax=MIN(tmax,b);if(tmin>tmax)return 200;}}
    return tmin;
}
static float traceWorld(V3 o,V3 d,float maxdist){
    float dist=maxdist;
    for(float t=.1;t<maxdist;t+=.14f){V3 p=add(o,mul(d,t));if(wallAt(p.x,p.z)||p.y<.03||p.y>ceiling((int)(p.x/CELL),(int)(p.z/CELL))){dist=t;break;}}
    for(int i=0;i<propCount;i++){Prop *p=props+i;float t=rayBox(o,d,v3(p->p.x-p->sx/2,0,p->p.z-p->sz/2),v3(p->p.x+p->sx/2,p->sy,p->p.z+p->sz/2));dist=MIN(dist,t);}
    return dist;
}
static V3 forward(void){return v3(sin(yaw)*cos(pitch),sin(pitch),cos(yaw)*cos(pitch));}
static void look(float dx,float dy){yaw-=dx*mouseSensitivity;pitch=CLAMP(pitch-dy*mouseSensitivity,-1.4f,1.4f);}
static void damageEnemy(int index,float damage,V3 impact){
    Enemy *e=enemies+index;e->hp-=damage;e->hit=.16;e->active=1;hitFlash=.15f;hitsMade++;particles(impact,7,0,2.5);
    if(e->hp<=0){kills++;particles(add(e->p,v3(0,1,0)),35,1,7);sound(3,.75f,CLAMP((e->p.x-player.x)*.02,-.8,.8));
        if(e->type==3){bossDead=1;note("WARDEN DESTROYED. REACH THE GREEN EXTRACTION BAY.");sound(5,.9,0);}
        else if(rnd()<.5f)pickup(e->p.x,e->p.z,rnd()<.3f?0:1);
    }
}
static void reloadWeapon(void){if(!reloadTime&&ammo[weapon]<magazine[weapon]&&reserve[weapon]>0){reloadTime=weapon==1?1.6f:1.35f;sound(4,.5,0);note("RELOADING");}}
static void fireWeapon(void){
    if(shootCD>0||reloadTime>0)return;if(ammo[weapon]<=0){reloadWeapon();return;}
    ammo[weapon]--;shotsFired++;shootCD=weapon==0?.115f:weapon==1?.72f:.6f;gunKick=1;shake=weapon==1?.07f:.025f;
    sound(weapon,weapon==1?1.f:.72f,0);
    int count=weapon==1?9:1;
    for(int pellet=0;pellet<count;pellet++){
        V3 d=forward();float spread=weapon==1?.055f:weapon==0?(mouseRight?.003:.012):.001f;
        d=norm(add(d,v3((rnd()-.5f)*spread,(rnd()-.5f)*spread,(rnd()-.5f)*spread)));
        float best=traceWorld(player,d,110);int hit=-1;
        for(int i=0;i<enemyCount;i++){Enemy *e=enemies+i;if(e->hp<=0)continue;float h=e->type==3?4:e->type==1?1.0f:1.9f,r=e->type==3?1.65f:e->type==2?.8f:.5f;float y=e->type==1?1.4f+sin(totalTime*2+e->phase)*.2f:0;
            float t=rayBox(player,d,v3(e->p.x-r,y,e->p.z-r),v3(e->p.x+r,y+h,e->p.z+r));if(t<best){best=t;hit=i;}}
        V3 hitPos=add(player,mul(d,best));
        if(hit>=0)damageEnemy(hit,weapon==0?22:weapon==1?14:130,hitPos);else particles(hitPos,weapon==2?14:3,0,2.4);
        if(weapon==2){for(float t=.7f;t<best;t+=.45f){Particle *p=parts+(partIndex++%MAXP);p->p=add(player,mul(d,t));p->v=v3(0,0,0);p->life=p->maxlife=.17;p->size=.035;p->type=2;}}
    }
}
static void makeBolt(V3 origin,V3 target,float speed,int owner){Bolt *b=bolts+(boltIndex++%96);b->p=origin;b->v=mul(norm(sub(target,origin)),speed);b->life=6;b->owner=owner;}
static void distanceField(void){
    int qx[MW*MH],qz[MW*MH],head=0,tail=0;for(int z=0;z<MH;z++)for(int x=0;x<MW;x++)distmap[z][x]=9999;
    int px=CLAMP((int)(player.x/CELL),0,MW-1),pz=CLAMP((int)(player.z/CELL),0,MH-1);distmap[pz][px]=0;qx[tail]=px;qz[tail++]=pz;
    while(head<tail){int x=qx[head],z=qz[head++];for(int d=0;d<4;d++){int nx=x+(d==0)-(d==1),nz=z+(d==2)-(d==3);if(!solid(nx,nz)&&distmap[nz][nx]==9999){distmap[nz][nx]=distmap[z][x]+1;qx[tail]=nx;qz[tail++]=nz;}}}
}
static void interact(void){
    for(int i=0;i<3;i++)if(!relays[i].on&&dsq(relays[i].p.x-player.x,relays[i].p.z-player.z)<9){
        relays[i].on=1;relayCount++;armor=MIN(100,armor+25);reserve[0]+=30;reserve[1]+=4;reserve[2]+=3;health=MIN(100,health+20);
        particles(add(relays[i].p,v3(0,1.5,0)),50,2,5);sound(5,1,0);
        note(relayCount==3?"ALL RELAYS SEVERED. CONTAINMENT GATE OPEN.":"RELAY SEVERED. SECURITY REINFORCEMENTS INBOUND.");
        if(enemyCount+2<MAXE){spawn(16,13,relayCount%2);spawn(19,22,0);enemies[enemyCount-1].active=enemies[enemyCount-2].active=1;}
        return;
    }
}
static void simulate(float dt){
    gameTime+=dt;shootCD=decay(shootCD,dt);damageFlash=decay(damageFlash,dt*1.5f);hitFlash=decay(hitFlash,dt);gunKick=decay(gunKick,dt*8);shake=decay(shake,dt);toastTime=decay(toastTime,dt);
    if(reloadTime>0){reloadTime-=dt;if(reloadTime<=0){reloadTime=0;int n=MIN(reserve[weapon],magazine[weapon]-ammo[weapon]);ammo[weapon]+=n;reserve[weapon]-=n;sound(4,.35,0);}}
    for(int i=0;i<3;i++)if(pressed['1'+i]){weapon=i;reloadTime=0;shootCD=.18;}
    if(pressed['R'])reloadWeapon();if(pressed['E'])interact();
    if(keys[VK_LEFT])yaw+=dt*1.8;if(keys[VK_RIGHT])yaw-=dt*1.8;if(keys[VK_UP])pitch=MIN(1.35,pitch+dt);if(keys[VK_DOWN])pitch=MAX(-1.35,pitch-dt);
    float dx=(keys['D']?1.f:0)-(keys['A']?1.f:0),dz=(keys['W']?1.f:0)-(keys['S']?1.f:0),m=sqrt(dx*dx+dz*dz);
    float speed=keys[VK_SHIFT]?8.4f:5.4f;if(keys[VK_CONTROL])speed=2.8f;
    if(m>0){dx=dx/m*speed*dt;dz=dz/m*speed*dt;moveBody(&player,-dx*cos(yaw)+dz*sin(yaw),dz*cos(yaw)+dx*sin(yaw),.33f);
        float old=stepPhase;stepPhase+=dt*(keys[VK_SHIFT]?14:9);if((int)(old/PI)!=(int)(stepPhase/PI)&&grounded)sound(4,.22,0);}
    if(pressed[VK_SPACE]&&grounded){vy=5.2;grounded=0;sound(4,.15,0);}
    vy-=dt*16;player.y+=vy*dt;float eye=keys[VK_CONTROL]?1.05f:1.65f;if(player.y<eye){player.y=eye;vy=0;grounded=1;}
    if(mouseDown||keys['F'])fireWeapon();
    if((int)(gameTime*3)!=(int)((gameTime-dt)*3))distanceField();
    int px=(int)(player.x/CELL),pz=(int)(player.z/CELL);for(int z=MAX(0,pz-4);z<MIN(MH,pz+5);z++)for(int x=MAX(0,px-4);x<MIN(MW,px+5);x++)seen[z][x]=1;
    if(pz>=29&&!bossAwake){bossAwake=1;note("CONTAINMENT FAILURE. ELIMINATE THE WARDEN.");sound(6,1,0);}
    for(int i=0;i<enemyCount;i++){
        Enemy *e=enemies+i;if(e->hp<=0)continue;e->hit=decay(e->hit,dt);e->cd-=dt;
        V3 dif=sub(player,e->p);dif.y=0;float dist=length(dif);int sight=dist<45&&clearSight(add(e->p,v3(0,1.5,0)),player);
        if(e->type==3){if(!bossAwake)continue;e->active=1;}
        else if(sight&&dist<27)e->active=1;
        if(!e->active)continue;e->yaw=atan2(dif.x,dif.z)*180/PI;
        if(dist>(e->type==3?14:e->type==2?5:9)||!sight){
            V3 target=player;int x=(int)(e->p.x/CELL),z=(int)(e->p.z/CELL);
            if(!sight&&x>=0&&x<MW&&z>=0&&z<MH){int best=distmap[z][x];for(int d=0;d<4;d++){int nx=x+(d==0)-(d==1),nz=z+(d==2)-(d==3);if(!solid(nx,nz)&&distmap[nz][nx]<best){best=distmap[nz][nx];target=cellpos(nx,nz);}}}
            V3 dir=sub(target,e->p);dir.y=0;dir=norm(dir);float es=e->type==1?3.8f:e->type==2?2.1f:e->type==3?1.5f:2.7f;
            V3 before=e->p;moveBody(&e->p,dir.x*dt*es,dir.z*dt*es,e->type==3?1.2f:.38f);
            if(length(sub(before,e->p))<dt*.2f)moveBody(&e->p,dir.z*dt*es,-dir.x*dt*es,.38f);
        }else if(e->type==1){moveBody(&e->p,cos(totalTime+e->phase)*dt*1.3,sin(totalTime+e->phase)*dt*1.3,.38);}
        if(sight&&e->cd<=0&&dist<38){
            V3 from=add(e->p,v3(0,e->type==3?2.6f:e->type==1?1.9f:1.3f,0));V3 target=player;
            if(e->type==3){for(int j=-2;j<=2;j++)makeBolt(from,add(target,v3(j*1.8f,0,-j*.3f)),13,i);e->cd=e->hp<600?.9f:1.5f;}
            else{target.x+=(rnd()-.5f)*1.2f;target.z+=(rnd()-.5f)*1.2f;makeBolt(from,target,e->type==1?13:11,i);e->cd=e->type==2?.9f:1.7f+rnd()*.4f;}
            sound(2,MIN(.5,3/MAX(dist,1)),CLAMP((from.x-player.x)*.02,-1,1));
        }
        if(dist<1.1&&e->cd<.3f){hurtPlayer(10);e->cd=1;}
    }
    for(int i=0;i<96;i++)if(bolts[i].life>0){Bolt *b=bolts+i;b->life-=dt;V3 delta=mul(b->v,dt);int n=1+(int)(length(delta)/.2);delta=mul(delta,1.f/n);
        for(int j=0;j<n&&b->life>0;j++){b->p=add(b->p,delta);int blocked=wallAt(b->p.x,b->p.z)||b->p.y<0;
            for(int k=0;k<propCount;k++){Prop *p=props+k;if(b->p.y<p->sy&&fabs(b->p.x-p->p.x)<p->sx/2&&fabs(b->p.z-p->p.z)<p->sz/2)blocked=1;}
            if(blocked){particles(b->p,4,0,2);b->life=0;}
            else if(dsq(b->p.x-player.x,b->p.z-player.z)<.26&&b->p.y>player.y-1.6f&&b->p.y<player.y+.2f){hurtPlayer(enemies[b->owner].type==3?18:12);b->life=0;}}
    }
    for(int i=0;i<MAXP;i++)if(parts[i].life>0){Particle *p=parts+i;p->life-=dt;p->p=add(p->p,mul(p->v,dt));if(p->type!=2)p->v.y-=dt*8;if(p->p.y<.04){p->p.y=.04;p->v.y=fabs(p->v.y)*.3f;}}
    for(int i=0;i<pickupCount;i++){Pickup *p=pickups+i;if(!p->active||dsq(p->p.x-player.x,p->p.z-player.z)>1.6f)continue;
        if(p->type==0){if(health>=100)continue;health=MIN(100,health+35);note("MEDICAL CELL +35");}
        if(p->type==1){reserve[0]+=45;reserve[1]+=6;reserve[2]+=3;note("MUNITIONS RECOVERED");}
        if(p->type==2){if(armor>=100)continue;armor=MIN(100,armor+40);note("ARMOR CAPACITOR +40");}
        p->active=0;sound(5,.5,0);
    }
    if(pz>=40&&bossDead){extraction+=dt;if(extraction>1.5f){mode=4;mouseDown=0;ClipCursor(NULL);while(ShowCursor(TRUE)<0);sound(5,1,0);}}else extraction=0;
    memset(pressed,0,sizeof(pressed));
}

static void orb(float x,float y,float z,float r){
    for(int j=0;j<6;j++){float a=-PI/2+j*PI/6,b=a+PI/6;glBegin(GL_QUAD_STRIP);for(int i=0;i<=12;i++){float t=i*PI/6;
        glNormal3f(cos(a)*cos(t),sin(a),cos(a)*sin(t));glVertex3f(x+r*cos(a)*cos(t),y+r*sin(a),z+r*cos(a)*sin(t));
        glNormal3f(cos(b)*cos(t),sin(b),cos(b)*sin(t));glVertex3f(x+r*cos(b)*cos(t),y+r*sin(b),z+r*cos(b)*sin(t));}glEnd();}
}
static void drawEnemy(Enemy *e){
    glPushMatrix();glTranslatef(e->p.x,e->p.y,e->p.z);glRotatef(e->yaw,0,1,0);
    float anim=sin(totalTime*7+e->phase)*(e->active?.25f:.04f);
    if(e->type==3){
        color(.1,.15,.17,3);tube(0,1.45,0,1.35,1.35,12);color(.42,.45,.38,0);tube(0,2.05,0,1.15,1.05,8);
        color(.09,.13,.15,3);tube(0,2.85,0,.8,.45,8);color(.9,.13,.025,5);orb(0,2.66,.84,.44);
        color(.6,.3,.05,2);for(int k=0;k<4;k++){float a=k*PI/2+.4f;beam(v3(sin(a)*.8,2.5,cos(a)*.8),v3(sin(a)*1.35,1,cos(a)*1.35),.25);}
        for(int k=0;k<6;k++){float a=k*PI/3;V3 p=v3(sin(a)*1.1,1.6,cos(a)*1.1),q=v3(sin(a)*2.1,1.35+anim*(k%2?1:-1),cos(a)*2.1),r=v3(sin(a)*2.5,.15,cos(a)*2.5);
            color(.22,.27,.28,3);beam(p,q,.17);beam(q,r,.11);orb(q.x,q.y,q.z,.23);color(.8,.22,.025,5);orb(p.x,p.y,p.z,.1);color(.4,.42,.37,0);box(r.x,.13,r.z,.46,.22,.65);}
        color(.09,.11,.13,3);for(int k=-1;k<=1;k+=2){box(k*1.15,2.2,.4,.44,.5,1.65);color(.9,.12,.01,5);box(k*1.15,2.2,1.27,.22,.22,.07);color(.09,.11,.13,3);}
    }else if(e->type==1){
        glTranslatef(0,1.8+sin(totalTime*2+e->phase)*.2f,0);color(.12,.19,.23,3);orb(0,0,0,.42);color(.4,.5,.5,0);box(0,.05,-.1,.7,.25,.6);
        color(.85,.17,.03,5);box(0,.04,.36,.36,.09,.04);
        for(int k=-1;k<=1;k+=2){color(.16,.2,.23,3);box(k*.6,0,0,.6,.13,.4);tube(k*.72,-.04,0,.22,.18,12);color(.02,.75,1,5);tube(k*.72,-.14,0,.13,.035,12);}
        color(.07,.09,.11,3);box(0,-.2,.4,.15,.17,.4);
    }else{
        if(e->type==2)glScalef(1.35,1.1,1.2);
        for(int k=-1;k<=1;k+=2){float swing=anim*k;
            color(.13,.17,.19,3);beam(v3(k*.23,.85,0),v3(k*.27,.42,swing),.13);beam(v3(k*.27,.42,swing),v3(k*.28,.13,-swing),.1);
            color(.4,.42,.36,0);box(k*.28,.14,.08-swing,.28,.22,.46);box(k*.24,.61,swing*.7,.25,.36,.28);
            color(.15,.2,.21,3);orb(k*.27,.42,swing,.14);
        }
        color(e->hit>0?.8:.28,e->hit>0?.6:.33,e->hit>0?.35:.32,0);box(0,1.18,0,.72,.66,.46);
        color(.08,.115,.13,3);box(0,1.15,.26,.54,.35,.12);box(0,1.35,-.32,.52,.64,.22);
        color(.7,.34,.05,2);box(0,1.43,.33,.66,.16,.08);
        color(.14,.19,.2,3);tube(0,1.59,0,.11,.13,8);box(0,1.79,.02,.43,.36,.4);
        color(.85,.08,.02,5);box(0,1.8,.231,.32,.061,.03);color(.07,.11,.13,3);box(0,1.93,.04,.47,.06,.42);
        for(int k=-1;k<=1;k+=2){color(.35,.38,.32,0);orb(k*.47,1.42,0,.22);color(.12,.16,.17,3);beam(v3(k*.46,1.35,0),v3(k*.5,1,.23),.13);beam(v3(k*.5,1,.23),v3(k*.3,1.15,.64),.12);}
        color(.06,.08,.1,3);box(.3,1.14,.7,.25,.25,.7);color(.9,.15,.025,5);box(.3,1.14,1.06,.13,.13,.02);
    }
    glPopMatrix();
}
static void drawDynamic(void){
    for(int i=0;i<enemyCount;i++)if(enemies[i].hp>0&&dsq(enemies[i].p.x-player.x,enemies[i].p.z-player.z)<85*85)drawEnemy(enemies+i);
    for(int i=0;i<3;i++){
        Relay *r=relays+i;float x=r->p.x,z=r->p.z;
        color(.13,.18,.19,3);box(x,.58,z,1.25,1.16,1.0);color(.34,.39,.39,0);box(x,1.27,z,1.5,.3,1.2);
        color(.025,r->on?.8:.38,r->on?.35:.95,5);box(x,1.44,z,1.25,.022,.82);
        for(int k=0;k<3;k++){color(.6,.3,.05,2);tube(x+(k-1)*.37,1.82,z,.13,.7,8);color(.02,r->on?.4:.85,r->on?.15:1,5);tube(x+(k-1)*.37,1.86,z,.08,.45,8);}
        if(!r->on){glPushMatrix();glTranslatef(x,2.7+sin(totalTime*2)*.12,z);glRotatef(totalTime*35,0,1,0);color(.04,.9,1,5);box(0,0,0,.22,.22,.22);glPopMatrix();}
    }
    float gateX=17.5*CELL,gateZ=28.5*CELL,raise=relayCount==3?6:0;
    color(.22,.29,.3,0);box(gateX,2.8+raise,gateZ,8.4,5.6,.55);color(.7,.32,.045,2);box(gateX,1.1+raise,gateZ-.3,8.1,.55,.07);
    color(relayCount==3?.05:.85,relayCount==3?.8:.1,.04,5);box(gateX,3.7+raise,gateZ-.31,4,.065,.06);
    for(int i=0;i<pickupCount;i++)if(pickups[i].active){
        Pickup *p=pickups+i;glPushMatrix();glTranslatef(p->p.x,.55+sin(totalTime*2+i)*.12,p->p.z);glRotatef(totalTime*35+i*41,0,1,0);
        color(.07,.13,.15,3);box(0,0,0,.55,.45,.5);
        if(p->type==0){color(.12,.9,.5,5);box(0,0,-.258,.32,.09,.02);box(0,0,-.27,.1,.3,.02);}
        else if(p->type==1){color(.95,.48,.045,5);for(int j=0;j<3;j++)box((j-1)*.14,0,-.265,.055,.28,.03);}
        else{color(.05,.55,1,5);box(0,0,-.26,.25,.25,.03);}
        glPopMatrix();color(p->type==1?.7:.02,p->type==0?.65:.35,p->type==2?1:.13,5);ring(p->p.x,.02,p->p.z,.6,.028,24);
    }
    /* Reactor orbitals and the Warden containment field. */
    V3 p=cellpos(17,18);glPushMatrix();glTranslatef(p.x,3.2,p.z);glRotatef(totalTime*17,0,1,0);glRotatef(24,1,0,0);color(.1,.6,.72,5);ring(0,0,0,2.4,.025,48);glPopMatrix();
    for(int i=0;i<96;i++)if(bolts[i].life>0){Bolt *b=bolts+i;color(1,.17,.025,5);orb(b->p.x,b->p.y,b->p.z,.11);color(.8,.3,.02,5);beam(b->p,sub(b->p,mul(norm(b->v),.6)),.034);}
    glEnable(GL_BLEND);glBlendFunc(GL_ONE,GL_ONE);glDepthMask(GL_FALSE);
    for(int i=0;i<MAXP;i++)if(parts[i].life>0){Particle *p=parts+i;float f=p->life/p->maxlife;if(p->type==2)color(.05*f,.55*f,.8*f,5);else color(.9*f,.38*f,.065*f,5);box(p->p.x,p->p.y,p->p.z,p->size,p->size*2,p->size);}
    for(int i=0;i<85;i++){float x=player.x+sin(i*47.31)*13,z=player.z+cos(i*17.37)*13,y=fmod(i*.33+totalTime*.14,5.5);if(!wallAt(x,z)){color(.085,.13,.14,5);box(x,y,z,.012,.018,.012);}}
    glDepthMask(GL_TRUE);glDisable(GL_BLEND);
}
static void drawWeapon(void){
    glMatrixMode(GL_MODELVIEW);glLoadIdentity();glTranslatef(mouseRight?.08:.42,-.39f+sin(stepPhase)*.008f-gunKick*.027f,-.83+gunKick*.07f);
    if(reloadTime>0){glTranslatef(0,-.24f*sin(reloadTime*2.2),.05);glRotatef(25*sin(reloadTime*2.2),0,0,1);}
    glRotatef(gunKick*5,1,0,0);glScalef(.8,.8,.8);
    /* Gloves and forearms. */
    color(.14,.18,.19,3);beam(v3(.09,-.26,.58),v3(.03,-.14,.18),.115);beam(v3(-.52,-.33,.51),v3(-.18,-.1,-.35),.095);
    color(.22,.27,.28,0);box(.035,-.13,.17,.19,.2,.22);box(-.17,-.065,-.36,.21,.16,.24);
    color(.045,.075,.09,3);box(0,0,0,.27,.23,.72);box(0,.04,.4,.2,.21,.38);box(0,-.18,.12,.13,.31,.18);
    color(.31,.37,.38,0);box(0,.1,-.09,.31,.11,.62);color(.14,.18,.19,3);box(0,-.07,-.45,.29,.19,.32);
    color(.58,.31,.055,2);box(.157,.04,-.12,.013,.11,.39);
    color(.09,.13,.15,3);box(0,.192,.04,.06,.075,.47);
    for(int i=0;i<7;i++){box(0,.234,.25-i*.074,.12,.015,.034);box(.155,.04,-.3+i*.06,.025,.085,.018);}
    if(weapon==0){
        color(.23,.28,.29,3);beam(v3(0,0,-.58),v3(0,0,-1.15),.059);color(.06,.085,.1,3);beam(v3(0,0,-1.02),v3(0,0,-1.22),.092);
        color(.08,.12,.14,3);box(0,-.27,-.15,.155,.36,.25);color(.25,.31,.33,0);box(0,-.3,-.15,.159,.13,.255);
        color(.045,.085,.1,3);box(0,.25,-.14,.16,.16,.13);color(.055,.7,.75,5);box(0,.253,-.071,.092,.071,.006);
    }else if(weapon==1){
        for(int k=-1;k<=1;k+=2){color(.23,.27,.29,3);beam(v3(k*.064,.025,-.49),v3(k*.064,.025,-1.02),.067);color(.03,.045,.05,3);beam(v3(k*.064,.025,-1.021),v3(k*.064,.025,-1.027),.047);}
        color(.4,.23,.095,0);box(0,-.055,-.62,.26,.17,.36);for(int j=0;j<6;j++){color(.075,.1,.11,3);box(0,-.058,-.49-j*.05,.275,.176,.014);}
        color(.66,.26,.04,3);for(int j=0;j<4;j++)beam(v3(.21,.02,-.26+j*.11),v3(.21,.12,-.26+j*.11),.035);
    }else{
        color(.17,.24,.28,3);box(0,.03,-.59,.35,.28,.7);color(.045,.7,1,5);box(0,.18,-.59,.18,.013,.6);
        for(int j=0;j<5;j++){color(.25,.32,.34,3);box(0,.025,-.31-j*.145,.39,.34,.045);}
        color(.07,.8,1,5);box(0,.03,-.956,.18,.16,.017);color(.06,.1,.12,3);box(0,-.19,-.3,.23,.22,.3);
    }
    if(gunKick>.5f){glEnable(GL_BLEND);glBlendFunc(GL_ONE,GL_ONE);glDepthMask(GL_FALSE);color(weapon==2?.05:1,weapon==2?.8:.6,weapon==2?1:.12,5);orb(0,.01,weapon==0?-1.32:-1.12,.09+gunKick*.07);glDepthMask(GL_TRUE);glDisable(GL_BLEND);}
}

static GLuint fonts[4];static int fontSizes[4]={15,22,62,12};
static void makeFonts(void){
    HFONT vectorFont=CreateFontA(-32,0,0,0,FW_BOLD,0,0,0,ANSI_CHARSET,OUT_TT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,"Consolas");HGDIOBJ prev=SelectObject(dc,vectorFont);fontList=glGenLists(96);GLYPHMETRICSFLOAT metrics[96];wglUseFontOutlinesA(dc,32,96,fontList,.003f,0,WGL_FONT_POLYGONS,metrics);SelectObject(dc,prev);DeleteObject(vectorFont);
    for(int i=0;i<4;i++){HFONT font=CreateFontA(fontSizes[i],0,0,0,i==2?FW_BOLD:FW_NORMAL,0,0,0,ANSI_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,i==2?"Bahnschrift":"Consolas");HGDIOBJ old=SelectObject(dc,font);fonts[i]=glGenLists(96);wglUseFontBitmapsA(dc,32,96,fonts[i]);SelectObject(dc,old);DeleteObject(font);}
}
static void textAt(float x,float y,int size,const char *s,float r,float g,float b){glColor4f(r,g,b,1);glRasterPos2f(x,y+fontSizes[size]);glListBase(fonts[size]-32);glCallLists((int)strlen(s),GL_UNSIGNED_BYTE,s);}
static void rect(float x,float y,float w,float h,float r,float g,float b,float a){glColor4f(r,g,b,a);glBegin(GL_QUADS);glVertex2f(x,y);glVertex2f(x+w,y);glVertex2f(x+w,y+h);glVertex2f(x,y+h);glEnd();}
static void line2(float x,float y,float xx,float yy,float r,float g,float b,float a){glColor4f(r,g,b,a);glBegin(GL_LINES);glVertex2f(x,y);glVertex2f(xx,yy);glEnd();}
static void uiStart(void){UseProgram(0);glDisable(GL_DEPTH_TEST);glDisable(GL_TEXTURE_2D);glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glMatrixMode(GL_PROJECTION);glLoadIdentity();glOrtho(0,width,height,0,-1,1);glMatrixMode(GL_MODELVIEW);glLoadIdentity();}
static void drawMap(float x,float y,float scale,int full){
    rect(x-10,y-10,MW*scale+20,MH*scale+20,.015,.035,.04,.83);
    for(int z=0;z<MH;z++)for(int xx=0;xx<MW;xx++)if((seen[z][xx]||full)&&map[z][xx]!=1)rect(x+xx*scale,y+(MH-z-1)*scale,scale-.5f,scale-.5f,.15,.25,.27,.8);
    for(int i=0;i<3;i++)rect(x+relays[i].p.x/CELL*scale-2,y+(MH-relays[i].p.z/CELL)*scale-2,4,4,relays[i].on?.15:1,relays[i].on?.8:.54,.12,1);
    float a=x+player.x/CELL*scale,b=y+(MH-player.z/CELL)*scale;rect(a-2,b-2,4,4,.8,1,1,1);line2(a,b,a+sin(yaw)*9,b-cos(yaw)*9,.6,1,1,1);
    if(full){textAt(x,y+MH*scale+15,0,"01 COOLANT / 02 FOUNDRY / 03 CORE",.55,.7,.7);textAt(x,y+MH*scale+36,0,"GOLD: RELAY   GREEN: OFFLINE",.6,.75,.75);}
}
static void drawUI(void){
    char s[256];uiStart();float cx=width*.5f,cy=height*.5f;
    if(mode==1||mode==2||mode==3){
        rect(25,24,320,67,.018,.035,.041,.82);rect(25,24,3,67,.02,.7,.77,1);
        textAt(42,34,3,"N U L L P O I N T  /  OPERATION 09",.54,.7,.72);
        snprintf(s,sizeof(s),bossDead?"REACH EXTRACTION":relayCount==3?"ELIMINATE THE WARDEN":"SEVER RELAYS  /  %d OF 3",relayCount);textAt(42,56,0,s,.93,.95,.88);
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
        textAt(cx-143,height-23,3,"TAB MAP  /  E INTERACT  /  ESC PAUSE",.3,.48,.52);
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
            textAt(x,y+207,0,"Find your way out.",.64,.74,.75);
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
        textAt(34,height-32,3,"SHIFT SPRINT   SPACE JUMP   CTRL CROUCH   E USE   F11 FULLSCREEN",.44,.63,.66);
        textAt(width-226,27,3,"NO ASSETS. ONLY ALGORITHMS.",.35,.65,.69);
    }
    glDisable(GL_BLEND);
}

static int fail(const char *s){strncpy(lastError,s,sizeof(lastError)-1);logline(s);if(!testMode)MessageBoxA(NULL,s,"NULLPOINT",MB_OK|MB_ICONERROR);return 0;}
static GLuint shaderProgram(const char *vs,const char *fs){
    GLuint p=CreateProgram();const char *sources[2]={vs,fs};
    for(int i=0;i<2;i++){GLuint s=CreateShader(i?GL_FRAGMENT_SHADER:GL_VERTEX_SHADER);ShaderSource(s,1,sources+i,NULL);CompileShader(s);GLint ok;GetShaderiv(s,GL_COMPILE_STATUS,&ok);if(!ok){GetShaderInfoLog(s,sizeof(lastError),NULL,lastError);fail(lastError);return 0;}AttachShader(p,s);}
    LinkProgram(p);GLint ok;GetProgramiv(p,GL_LINK_STATUS,&ok);if(!ok){GetProgramInfoLog(p,sizeof(lastError),NULL,lastError);fail(lastError);return 0;}return p;
}
static void uniform1(GLuint prog,const char *n,float f){Uniform1f(GetUniformLocation(prog,n),f);}
static int resizeTargets(void){
    rw=MAX(320,width*(quality?1:.67f));rh=MAX(240,height*(quality?1:.67f));
    glBindTexture(GL_TEXTURE_2D,colorTex);glTexImage2D(GL_TEXTURE_2D,0,GL_RGB8,rw,rh,0,GL_RGB,GL_UNSIGNED_BYTE,NULL);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D,depthTex);glTexImage2D(GL_TEXTURE_2D,0,GL_DEPTH_COMPONENT24,rw,rh,0,GL_DEPTH_COMPONENT,GL_UNSIGNED_INT,NULL);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    BindFramebuffer(GL_FRAMEBUFFER_EXT,fbo);FramebufferTexture2D(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,colorTex,0);FramebufferTexture2D(GL_FRAMEBUFFER_EXT,GL_DEPTH_ATTACHMENT_EXT,GL_TEXTURE_2D,depthTex,0);
    GLenum status=CheckFramebufferStatus(GL_FRAMEBUFFER_EXT);BindFramebuffer(GL_FRAMEBUFFER_EXT,0);return status==GL_FRAMEBUFFER_COMPLETE_EXT;
}
static int initGL(void){
    PIXELFORMATDESCRIPTOR pfd={0};pfd.nSize=sizeof(pfd);pfd.nVersion=1;pfd.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER;pfd.iPixelType=PFD_TYPE_RGBA;pfd.cColorBits=32;pfd.cDepthBits=24;pfd.iLayerType=PFD_MAIN_PLANE;
    dc=GetDC(window);int fmt=ChoosePixelFormat(dc,&pfd);if(!fmt||!SetPixelFormat(dc,fmt,&pfd))return fail("Could not initialize the Windows OpenGL surface.");
    rc=wglCreateContext(dc);if(!rc||!wglMakeCurrent(dc,rc))return fail("OpenGL initialization failed. A working graphics driver is required.");
#define LOAD(N,T,S) N=(T)wglGetProcAddress(S);if(!N)return fail("Missing graphics capability: " S);
    LOAD(CreateShader,PFNGLCREATESHADERPROC,"glCreateShader");LOAD(ShaderSource,PFNGLSHADERSOURCEPROC,"glShaderSource");LOAD(CompileShader,PFNGLCOMPILESHADERPROC,"glCompileShader");LOAD(GetShaderiv,PFNGLGETSHADERIVPROC,"glGetShaderiv");LOAD(GetShaderInfoLog,PFNGLGETSHADERINFOLOGPROC,"glGetShaderInfoLog");
    LOAD(CreateProgram,PFNGLCREATEPROGRAMPROC,"glCreateProgram");LOAD(AttachShader,PFNGLATTACHSHADERPROC,"glAttachShader");LOAD(LinkProgram,PFNGLLINKPROGRAMPROC,"glLinkProgram");LOAD(GetProgramiv,PFNGLGETPROGRAMIVPROC,"glGetProgramiv");LOAD(GetProgramInfoLog,PFNGLGETPROGRAMINFOLOGPROC,"glGetProgramInfoLog");LOAD(UseProgram,PFNGLUSEPROGRAMPROC,"glUseProgram");LOAD(GetUniformLocation,PFNGLGETUNIFORMLOCATIONPROC,"glGetUniformLocation");LOAD(Uniform1f,PFNGLUNIFORM1FPROC,"glUniform1f");LOAD(Uniform1i,PFNGLUNIFORM1IPROC,"glUniform1i");LOAD(Uniform2f,PFNGLUNIFORM2FPROC,"glUniform2f");LOAD(Uniform3f,PFNGLUNIFORM3FPROC,"glUniform3f");LOAD(ActiveTexture,PFNGLACTIVETEXTUREPROC,"glActiveTexture");
    LOAD(UniformMatrix4fv,PFNGLUNIFORMMATRIX4FVPROC,"glUniformMatrix4fv");
    LOAD(GenFramebuffers,PFNGLGENFRAMEBUFFERSEXTPROC,"glGenFramebuffersEXT");LOAD(BindFramebuffer,PFNGLBINDFRAMEBUFFEREXTPROC,"glBindFramebufferEXT");LOAD(FramebufferTexture2D,PFNGLFRAMEBUFFERTEXTURE2DEXTPROC,"glFramebufferTexture2DEXT");LOAD(CheckFramebufferStatus,PFNGLCHECKFRAMEBUFFERSTATUSEXTPROC,"glCheckFramebufferStatusEXT");
#undef LOAD
    logline((const char*)glGetString(GL_VENDOR));logline((const char*)glGetString(GL_RENDERER));logline((const char*)glGetString(GL_VERSION));
    swapInterval=(SwapIntervalFn)wglGetProcAddress("wglSwapIntervalEXT");if(swapInterval)swapInterval(testMode?0:1);
    worldProgram=shaderProgram(sceneVS,sceneFS);postProgram=shaderProgram(postVS,postFS);depthProgram=shaderProgram("#version 120\nvoid main(){gl_Position=gl_ModelViewProjectionMatrix*gl_Vertex;}","#version 120\nvoid main(){gl_FragColor=vec4(1);}");if(!worldProgram||!postProgram||!depthProgram)return 0;
    GenFramebuffers(1,&fbo);glGenTextures(1,&colorTex);glGenTextures(1,&depthTex);if(!resizeTargets())return fail("Framebuffer initialization failed.");
    GenFramebuffers(1,&shadowFBO);glGenTextures(1,&shadowTex);glBindTexture(GL_TEXTURE_2D,shadowTex);glTexImage2D(GL_TEXTURE_2D,0,GL_DEPTH_COMPONENT24,1024,1024,0,GL_DEPTH_COMPONENT,GL_UNSIGNED_INT,NULL);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    BindFramebuffer(GL_FRAMEBUFFER_EXT,shadowFBO);FramebufferTexture2D(GL_FRAMEBUFFER_EXT,GL_DEPTH_ATTACHMENT_EXT,GL_TEXTURE_2D,shadowTex,0);glDrawBuffer(GL_NONE);glReadBuffer(GL_NONE);if(CheckFramebufferStatus(GL_FRAMEBUFFER_EXT)!=GL_FRAMEBUFFER_COMPLETE_EXT)return fail("Shadow framebuffer initialization failed.");BindFramebuffer(GL_FRAMEBUFFER_EXT,0);
    makeFonts();makeWorld();glEnable(GL_NORMALIZE);return 1;
}
static void matmul(float *out,const float *a,const float *b){for(int j=0;j<4;j++)for(int i=0;i<4;i++){float s=0;for(int k=0;k<4;k++)s+=a[i+k*4]*b[k+j*4];out[i+j*4]=s;}}
static void camera(float offset){glMatrixMode(GL_MODELVIEW);glLoadIdentity();glTranslatef(-offset,offset*.45f,0);glRotatef(-pitch*180/PI,1,0,0);glRotatef(180-yaw*180/PI,0,1,0);glTranslatef(-player.x,-player.y,-player.z);}
static void lightPosition(const char *name,V3 p,const float *m){Uniform3f(GetUniformLocation(worldProgram,name),m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12],m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13],m[2]*p.x+m[6]*p.y+m[10]*p.z+m[14]);}
static void render(void){
    if(rw!=(int)MAX(320,width*(quality?1:.67f))||rh!=(int)MAX(240,height*(quality?1:.67f)))resizeTargets();
    float sp[16],sv[16],scombined[16],inverse[16]={0},shadowMatrix[16];
    BindFramebuffer(GL_FRAMEBUFFER_EXT,shadowFBO);glViewport(0,0,1024,1024);glClear(GL_DEPTH_BUFFER_BIT);UseProgram(depthProgram);glEnable(GL_DEPTH_TEST);glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE);glDisable(GL_TEXTURE_2D);glDisable(GL_BLEND);
    glMatrixMode(GL_PROJECTION);glLoadIdentity();glFrustum(-.11,.11,-.11,.11,.08,180);glGetFloatv(GL_PROJECTION_MATRIX,sp);camera(.35);glGetFloatv(GL_MODELVIEW_MATRIX,sv);
    glEnable(GL_POLYGON_OFFSET_FILL);glPolygonOffset(2,2);glCallList(worldList);for(int i=0;i<enemyCount;i++)if(enemies[i].hp>0)drawEnemy(enemies+i);glDisable(GL_POLYGON_OFFSET_FILL);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);matmul(scombined,sp,sv);
    BindFramebuffer(GL_FRAMEBUFFER_EXT,fbo);glViewport(0,0,rw,rh);glClearColor(.025,.04,.05,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);glDisable(GL_BLEND);
    glMatrixMode(GL_PROJECTION);glLoadIdentity();float n=.08f,fov=mouseRight?56:78,top=n*tan(fov*PI/360);glFrustum(-top*width/height,top*width/height,-top,top,n,180);
    glMatrixMode(GL_MODELVIEW);glLoadIdentity();glRotatef(-pitch*180/PI+(shake>0?sin(totalTime*71)*shake*5:0),1,0,0);glRotatef(180-yaw*180/PI,0,1,0);
    glTranslatef(-player.x,-player.y-(mode==1&&grounded?sin(stepPhase)*.018f:0),-player.z);
    UseProgram(worldProgram);uniform1(worldProgram,"time",totalTime);uniform1(worldProgram,"flash",gunKick);uniform1(worldProgram,"alert",bossAwake);
    float m[16];glGetFloatv(GL_MODELVIEW_MATRIX,m);
    for(int i=0;i<3;i++){for(int j=0;j<3;j++)inverse[i+j*4]=m[j+i*4];inverse[12+i]=-(m[i*4]*m[12]+m[i*4+1]*m[13]+m[i*4+2]*m[14]);}inverse[15]=1;matmul(shadowMatrix,scombined,inverse);UniformMatrix4fv(GetUniformLocation(worldProgram,"shadowM"),1,GL_FALSE,shadowMatrix);
    ActiveTexture(GL_TEXTURE2);glBindTexture(GL_TEXTURE_2D,shadowTex);Uniform1i(GetUniformLocation(worldProgram,"shadow"),2);ActiveTexture(GL_TEXTURE0);
    lightPosition("L0",v3(17.5*CELL,6,18.5*CELL),m);lightPosition("L1",v3(17.5*CELL,5,6.5*CELL),m);
    lightPosition("L2",v3(4.5*CELL,4.5,18.5*CELL),m);lightPosition("L3",v3(29.5*CELL,5,18.5*CELL),m);
    lightPosition("L4",v3(17.5*CELL,7,34.5*CELL),m);lightPosition("L5",v3(17.5*CELL,5,25*CELL),m);
    lightPosition("L6",add(player,mul(forward(),1)),m);
    glCallList(worldList);drawDynamic();
    if(mode!=0&&mode!=4){glDepthRange(0,.03);drawWeapon();glDepthRange(0,1);}
    BindFramebuffer(GL_FRAMEBUFFER_EXT,0);glViewport(0,0,width,height);glDisable(GL_DEPTH_TEST);UseProgram(postProgram);
    ActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,colorTex);Uniform1i(GetUniformLocation(postProgram,"scene"),0);
    ActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,depthTex);Uniform1i(GetUniformLocation(postProgram,"depth"),1);
    Uniform2f(GetUniformLocation(postProgram,"pixel"),1.f/rw,1.f/rh);uniform1(postProgram,"time",totalTime);uniform1(postProgram,"hurt",damageFlash);
    glBegin(GL_QUADS);glTexCoord2f(0,0);glVertex2f(-1,-1);glTexCoord2f(1,0);glVertex2f(1,-1);glTexCoord2f(1,1);glVertex2f(1,1);glTexCoord2f(0,1);glVertex2f(-1,1);glEnd();
    ActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,0);ActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,0);drawUI();
}
static void capture(const char *path){
    int stride=(width*3+3)&~3;DWORD size=stride*height;unsigned char *pixels=malloc(size);if(!pixels)return;
    glPixelStorei(GL_PACK_ALIGNMENT,4);glReadBuffer(GL_BACK);glReadPixels(0,0,width,height,GL_BGR,GL_UNSIGNED_BYTE,pixels);
    BITMAPFILEHEADER fh={0};BITMAPINFOHEADER ih={0};fh.bfType=0x4d42;fh.bfSize=sizeof(fh)+sizeof(ih)+size;fh.bfOffBits=sizeof(fh)+sizeof(ih);ih.biSize=sizeof(ih);ih.biWidth=width;ih.biHeight=height;ih.biPlanes=1;ih.biBitCount=24;ih.biSizeImage=size;
    FILE *f=fopen(path,"wb");if(f){fwrite(&fh,sizeof(fh),1,f);fwrite(&ih,sizeof(ih),1,f);fwrite(pixels,size,1,f);fclose(f);}free(pixels);
}
static void cursorLock(int lock){
    if(lock&&focused){RECT r;GetClientRect(window,&r);POINT a={r.left,r.top},b={r.right,r.bottom};ClientToScreen(window,&a);ClientToScreen(window,&b);r.left=a.x;r.top=a.y;r.right=b.x;r.bottom=b.y;ClipCursor(&r);while(ShowCursor(FALSE)>=0);}
    else{ClipCursor(NULL);while(ShowCursor(TRUE)<0);}
}
static void startGame(void){resetGame();mode=1;memset(keys,0,sizeof(keys));memset(pressed,0,sizeof(pressed));mouseDown=mouseRight=0;cursorLock(1);}
static void toggleFullscreen(void){
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

static int check(int condition,const char *name){if(logFile)fprintf(logFile,"%s %s\n",condition?"PASS":"FAIL",name);return !condition;}
static int selftest(void){
    int fails=0;logFile=fopen("selftest.txt","w");resetGame();mode=1;
    fails+=check(!collides(player.x,player.z,.33),"Start is collision free");
    distanceField();for(int i=0;i<3;i++)fails+=check(distmap[(int)(relays[i].p.z/CELL)][(int)(relays[i].p.x/CELL)]<9999,"Relay reachable before opening gate");
    fails+=check(distmap[35][17]==9999,"Containment gate blocks Warden arena");
    V3 p=cellpos(16,27);moveBody(&p,0,20,.33);fails+=check(p.z<28*CELL,"Swept movement cannot tunnel through locked gate");
    p=player;moveBody(&p,-200,0,.33);fails+=check(p.x>=13*CELL,"Swept movement cannot tunnel through outer wall");
    float hp=health;hurtPlayer(20);fails+=check(health<hp&&armor<50,"Armor absorbs part of damage");
    mode=1;ammo[0]=0;reserve[0]=31;reloadWeapon();simulate(1.4f);fails+=check(ammo[0]==30&&reserve[0]==1,"Reload conserves ammunition");
    resetGame();mode=1;Enemy *e=enemies;player=cellpos(17,10);player.y=1.65;yaw=0;pitch=-.14;float ehp=e->hp;fireWeapon();fails+=check(e->hp<ehp&&ammo[0]==29,"Hitscan damages target and spends one cartridge");
    shootCD=0;ammo[0]=0;int priorShots=shotsFired;fireWeapon();fails+=check(shotsFired==priorShots,"Empty weapon cannot fire");
    resetGame();mode=1;for(int i=0;i<3;i++){player=add(relays[i].p,v3(0,1.65,-1));interact();interact();}fails+=check(relayCount==3,"Relay interactions are idempotent");
    player=cellpos(17,26);player.y=1.65;distanceField();fails+=check(distmap[41][17]<9999,"All relays open route to extraction");
    for(int i=0;i<enemyCount;i++)if(enemies[i].type==3)damageEnemy(i,1500,enemies[i].p);
    fails+=check(bossDead==1,"Warden death unlocks extraction");
    player=cellpos(17,41);player.y=1.65;simulate(1.6);fails+=check(mode==4,"Extraction completes mission");
    resetGame();mode=1;armor=0;hurtPlayer(999);fails+=check(mode==3&&health==0,"Lethal damage enters loss state");
    resetGame();fails+=check(health==100&&relayCount==0&&!bossDead&&kills==0,"Restart resets mission state");
    if(logFile){fprintf(logFile,"RESULT: %d failures\n",fails);fclose(logFile);logFile=NULL;}return fails;
}
static int campaignTest(void){
    logFile=fopen("campaign.txt","w");resetGame();mode=1;difficulty=0;
    int route[MH][MW],qx[MW*MH],qz[MW*MH],previous=-1;
    for(int frame=0;frame<60*600&&mode==1;frame++){
        V3 goal=relayCount<3?relays[relayCount].p:cellpos(17,41);int gx=(int)(goal.x/CELL),gz=(int)(goal.z/CELL);
        for(int z=0;z<MH;z++)for(int x=0;x<MW;x++)route[z][x]=9999;int head=0,tail=1;qx[0]=gx;qz[0]=gz;route[gz][gx]=0;
        while(head<tail){int x=qx[head],z=qz[head++];for(int d=0;d<4;d++){int nx=x+(d==0)-(d==1),nz=z+(d==2)-(d==3);V3 p=cellpos(nx,nz);if(!solid(nx,nz)&&!collides(p.x,p.z,.4)&&route[nz][nx]==9999){route[nz][nx]=route[z][x]+1;qx[tail]=nx;qz[tail++]=nz;}}}
        int x=(int)(player.x/CELL),z=(int)(player.z/CELL),best=route[z][x];V3 target=cellpos(x,z);
        for(int d=0;d<4;d++){int nx=x+(d==0)-(d==1),nz=z+(d==2)-(d==3);if(!solid(nx,nz)&&route[nz][nx]<best){best=route[nz][nx];target=cellpos(nx,nz);}}
        V3 dir=sub(target,player);dir.y=0;float walkAngle=atan2(dir.x,dir.z);int hit=-1;float nearest=42;
        for(int i=0;i<enemyCount;i++){Enemy *e=enemies+i;if(e->hp<=0)continue;V3 point=add(e->p,v3(0,e->type==3?2.3f:e->type==1?1.85f:1.35f,0)),d=sub(point,player);float len=length(d);if(len<nearest&&traceWorld(player,norm(d),len+.3)>len-.1){nearest=len;hit=i;}}
        memset(keys,0,sizeof(keys));mouseDown=0;
        if(hit>=0){Enemy *e=enemies+hit;V3 d=sub(add(e->p,v3(0,e->type==3?2.3f:e->type==1?1.85f:1.35f,0)),player);yaw=atan2(d.x,d.z);pitch=atan2(d.y,sqrt(d.x*d.x+d.z*d.z));
            int choice=(e->type>=2&&ammo[2]+reserve[2]>0)?2:nearest<6&&ammo[1]+reserve[1]>0?1:0;if(ammo[choice]+reserve[choice]==0)for(int k=0;k<3;k++)if(ammo[k]+reserve[k]>0)choice=k;
            if(choice!=weapon){weapon=choice;reloadTime=0;}mouseDown=1;
        }else{yaw=walkAngle;pitch=0;}
        if((hit<0||nearest>13||relayCount<3)&&length(dir)>.15){float rel=walkAngle-yaw;float f=cos(rel),s=sin(rel);if(f>.3)keys['W']=1;if(f<-.3)keys['S']=1;if(s>.3)keys['A']=1;if(s<-.3)keys['D']=1;}
        if(relayCount<3&&dsq(player.x-goal.x,player.z-goal.z)<9)pressed['E']=1;
        totalTime+=1.f/60;simulate(1.f/60);
        if(previous!=relayCount){fprintf(logFile,"At %.1fs: relays=%d health=%.1f kills=%d\n",gameTime,relayCount,health,kills);previous=relayCount;}
        if(frame%600==0)fprintf(logFile,"t=%.1f pos=(%.1f,%.1f) HP=%.1f kills=%d ammo=%d/%d %d/%d %d/%d\n",gameTime,player.x,player.z,health,kills,ammo[0],reserve[0],ammo[1],reserve[1],ammo[2],reserve[2]);
    }
    int ok=mode==4;fprintf(logFile,"%s Campaign with ordinary movement, firing, damage, pickups and interactions. Time %.1f, kills %d, relays %d, Warden %d, health %.1f, mode %d\n",ok?"PASS":"FAIL",gameTime,kills,relayCount,bossDead,health,mode);fclose(logFile);logFile=NULL;return !ok;
}
static int verifyGraphics(void){
    logline("Graphics verification started");int failures=0;
    for(int angle=0;angle<4;angle++)for(int k=0;k<4;k++){
        resetGame();mode=1;yaw=angle*PI/2;pitch=0;camera(0);float view[16];glGetFloatv(GL_MODELVIEW_MATRIX,view);V3 before=player;memset(keys,0,sizeof(keys));keys["WASD"[k]]=1;simulate(.05f);V3 delta=sub(player,before);float vx=view[0]*delta.x+view[8]*delta.z,vz=view[2]*delta.x+view[10]*delta.z;
        failures+=check(k==0?vz<-.2f:k==1?vx<-.2f:k==2?vz>.2f:vx>.2f,"WASD movement follows rendered camera basis");
    }
    memset(keys,0,sizeof(keys));for(int axis=0;axis<4;axis++){resetGame();V3 original=add(player,mul(forward(),10));look(axis==0?100:axis==1?-100:0,axis==2?-100:axis==3?100:0);camera(0);float view[16];glGetFloatv(GL_MODELVIEW_MATRIX,view);float xx=view[0]*original.x+view[4]*original.y+view[8]*original.z+view[12],yy=view[1]*original.x+view[5]*original.y+view[9]*original.z+view[13];failures+=check(axis==0?xx<0:axis==1?xx>0:axis==2?yy<0:yy>0,"Mouse look direction follows rendered camera");}
    resetGame();mode=0;player=cellpos(14,13);player.y=2.4;yaw=.85;pitch=-.03;totalTime=7;render();glFinish();capture("01-title.bmp");
    mode=1;player=cellpos(17,4);player.y=1.65;yaw=0;pitch=0;render();glFinish();capture("02-arrival.bmp");
    player=cellpos(14,14);player.y=1.65;yaw=.62;pitch=.02;render();glFinish();capture("03-reactor.bmp");
    player=cellpos(4,19);player.y=1.65;yaw=.28;pitch=-.05;render();glFinish();capture("04-relay.bmp");
    player=cellpos(17,32);player.y=1.65;yaw=0;pitch=.04;bossAwake=1;relayCount=3;weapon=2;for(int i=0;i<enemyCount;i++)enemies[i].yaw=atan2(player.x-enemies[i].p.x,player.z-enemies[i].p.z)*180/PI;render();glFinish();capture("05-warden.bmp");
    int ok=glGetError()==GL_NO_ERROR;logline(ok?"PASS OpenGL error check":"FAIL OpenGL error check");
    LARGE_INTEGER a,b;QueryPerformanceCounter(&a);for(int i=0;i<90;i++){totalTime+=1.f/60;render();}glFinish();QueryPerformanceCounter(&b);
    char s[256];snprintf(s,sizeof(s),"90 full-resolution frames: %.2f ms average",(b.QuadPart-a.QuadPart)*1000./frequency.QuadPart/90);logline(s);
    return !ok||failures;
}
int WINAPI WinMain(HINSTANCE instance,HINSTANCE previous,LPSTR cmd,int show){
    (void)previous;(void)show;testMode=strstr(cmd,"--selftest")||strstr(cmd,"--verify");
    if(strstr(cmd,"--selftest"))return selftest();if(strstr(cmd,"--playtest"))return campaignTest();
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
        LARGE_INTEGER now;QueryPerformanceCounter(&now);float dt=(now.QuadPart-lastCounter.QuadPart)/(double)frequency.QuadPart;lastCounter=now;dt=CLAMP(dt,.001f,.05f);totalTime+=dt;
        if(mode==0)yaw=.85f+sin(totalTime*.09f)*.13f;
        if(mode==1)simulate(dt);else memset(pressed,0,sizeof(pressed));
        pumpAudio();if(!IsIconic(window)){render();SwapBuffers(dc);}else Sleep(30);
        QueryPerformanceCounter(&now);lastFrameMS=(now.QuadPart-lastCounter.QuadPart)*1000./frequency.QuadPart;
        if(lastFrameMS<7)Sleep((DWORD)(7-lastFrameMS));
    }
    cursorLock(0);stopAudio();timeEndPeriod(1);wglMakeCurrent(NULL,NULL);wglDeleteContext(rc);ReleaseDC(window,dc);DestroyWindow(window);return 0;
}
