// @shader sceneVS
#version 120
varying vec3 P,N,W,B;varying vec4 C;void main(){vec4 p=gl_ModelViewMatrix*gl_Vertex;P=p.xyz;N=gl_NormalMatrix*gl_Normal;W=gl_Vertex.xyz;B=gl_Normal;C=gl_Color;gl_Position=gl_ProjectionMatrix*p;}

// @shader sceneFS
#version 120
varying vec3 P,N,W,B;
varying vec4 C;
uniform float time,flash,alert;
uniform vec3 L0,L1,L2,L3,L4,L5,L6,T,TD;
uniform vec4 clipPlane;
uniform float mirrored;
uniform mat4 shadowM;
uniform sampler2D shadow;
float hash(vec2 p){vec3 q=fract(vec3(p.xyx)*.1031);q+=dot(q,q.yzx+33.33);return fract((q.x+q.y)*q.z);}
float noise(vec2 p){vec2 i=floor(p),f=fract(p);f=f*f*(3.-2.*f);return mix(mix(hash(i),hash(i+vec2(1,0)),f.x),mix(hash(i+vec2(0,1)),hash(i+1.),f.x),f.y);}
vec3 light(vec3 at,vec3 radiance,vec3 n,float rough,float metal,vec3 base){
    vec3 delta=at-P;float d2=dot(delta,delta);vec3 l=delta*inversesqrt(max(d2,.001)),v=normalize(-P),h=normalize(l+v);
    float nl=clamp(dot(n,l),0.,1.),nv=clamp(dot(n,v),.001,1.),nh=clamp(dot(n,h),0.,1.),vh=clamp(dot(v,h),0.,1.);
    float r=rough,a=r*r,a2=a*a,den=nh*nh*(a2-1.)+1.;
    float D=a2/(3.14159*den*den),k=(r+1.)*(r+1.)*.125;
    float G=nv/(nv*(1.-k)+k)*nl/max(nl*(1.-k)+k,.001);
    vec3 f0=mix(vec3(.04),base,metal),F=f0+(1.-f0)*pow(1.-vh,5.);
    vec3 diffuse=base*(1.-metal)*(1.-F)*.31831,spec=D*G*F/max(4.*nl*nv,.001);
    return (diffuse+spec)*radiance*nl*3.5/(1.+d2*.038);
}
vec3 haze(vec3 at,vec3 tint){vec3 ray=normalize(P);float t=clamp(dot(at,ray),0.,length(P));return tint*exp(-length(at-ray*t)*.75)*min(t*.003,.035);}
void main(){
    if(mirrored>.5&&dot(vec4(P,1),clipPlane)<.03)discard;
    vec3 n=normalize(N),v=normalize(-P),b=C.rgb,an=abs(B);float mat=floor(C.a+.1);
    vec2 uv=an.y>an.x&&an.y>an.z?W.xz:(an.x>an.z?W.zy:W.xy);
    float grain=noise(uv*19.),dirt=noise(uv*.72),aa=max(fwidth(uv.x),fwidth(uv.y));
    float height=0.,rough=38.,metal=.12,wet=0.,mask=0.,close=1.-smoothstep(.015,.15,aa);
    vec2 tile=fract(uv/1.4),edge=min(tile,1.-tile);float e=min(edge.x,edge.y);
    float seam=smoothstep(.012,.029+aa,e),wear=1.-smoothstep(.018,.055+aa,e);
    if(mat<.5){
        float bolt=1.-smoothstep(.012,.019+aa,length(abs(tile-.5)-vec2(.43)));
        float rib=(1.-smoothstep(.08,.16+aa,abs(fract(uv.y*7.)-.5)))*step(.28,tile.x)*step(tile.x,.72);
        b*=mix(.28,1.,seam)*(1.-bolt*.72)*(1.-rib*.2)*(.68+dirt*.4);
        b=mix(b,vec3(.32,.27,.17),wear*(.15+.3*grain));height=seam*.028+grain*.003*close-bolt*.011;rough=35.+grain*35.;
    }else if(mat<1.5){
        float tread=smoothstep(.5,.8,sin(uv.x*32.)*sin(uv.y*32.))*close;
        wet=smoothstep(.43,.77,noise(uv*.38));b*=mix(.27,1.,seam)*(.64+dirt*.5);b=mix(b,b*.5,wet);
        height=seam*.008+tread*.003*(1.-wet);rough=mix(25.,110.,wet);metal=.22;mask=.18+wet*.5;
    }else if(mat<2.5){
        float stripe=smoothstep(.46-aa,.54+aa,fract((uv.x+uv.y)*1.7)),chip=smoothstep(.66,.85,grain);
        b=mix(vec3(.025,.035,.04),b,stripe);b=mix(b,vec3(.26,.24,.2),chip*.75);height=grain*.005*close;rough=28.;
    }else if(mat<3.5){b*=.7+dirt*.25+noise(uv*vec2(2.,90.))*.16*close;height=grain*.002*close;rough=45.;metal=.82;}
    else if(mat<4.5){b*=.7+.3*sin(uv.y*120.)*sin(uv.y*120.);rough=6.;metal=0.;}
    vec3 dx=dFdx(P),dy=dFdy(P),r1=cross(dy,n),r2=cross(n,dx);float det=dot(dx,r1);
    n=normalize(max(abs(det),.000001)*n-sign(det)*(dFdx(height)*r1+dFdy(height)*r2));
    rough=clamp(sqrt(2./(rough+2.))+min(.2,aa*4.),.27,.85);
    vec3 col=b*vec3(.08,.11,.13);
    col+=light(L0,vec3(.35,2.6,3.)*(.94+.06*sin(time*1.7)),n,rough,metal,b);
    col+=light(L1,vec3(3.5,1.6,.6),n,rough,metal,b)+light(L2,vec3(.2,2.,2.9),n,rough,metal,b);
    col+=light(L3,vec3(4.,.85,.22)*(.94+.06*sin(time*7.)),n,rough,metal,b);
    col+=light(L4,mix(vec3(.4,1.8,2.8),vec3(3.8,.45,.12),alert),n,rough,metal,b)+light(L5,vec3(2.5,1.2,.4),n,rough,metal,b);
    vec4 sp=shadowM*vec4(P,1);vec3 sc=sp.xyz/sp.w*.5+.5;float shade=0.;
    float bias=max(.00015,.0008*(1.-abs(dot(n,v))));
    for(int j=0;j<4;j++){vec2 o=vec2(mod(float(j),2.),floor(float(j)/2.))-.5;shade+=step(sc.z-bias,texture2D(shadow,sc.xy+o*.0015).r)*.25;}
    if(sc.x<0.||sc.x>1.||sc.y<0.||sc.y>1.||sp.w<0.)shade=1.;
    float torch=pow(max(dot(normalize(P-T),normalize(TD-T)),0.),19.);
    col+=light(T,vec3(1.5,1.8,2.)*torch*shade,n,rough,metal,b)+light(L6,vec3(5.,1.4,.2)*flash,n,rough,metal,b);
    float fres=pow(1.-clamp(dot(n,v),0.,1.),4.);vec3 env=mix(vec3(.018,.04,.055),vec3(.12,.19,.23),clamp(reflect(-v,n).y*.5+.5,0.,1.));
    col+=env*fres*(.12+metal*.5);if(mat>4.5){col=b*(2.6+.32*sin(time*2.+W.y*3.));mask=0.;}
    float fog=1.-exp(-length(P)*.009);col=mix(col,vec3(.018,.028,.04),fog);
    col+=haze(L0,vec3(.02,.7,1.))+haze(L3,vec3(1.,.25,.035));gl_FragColor=vec4(col,mask);
}

// @shader postVS
#version 120
varying vec2 uv;void main(){uv=gl_MultiTexCoord0.xy;gl_Position=gl_Vertex;}

// @shader postFS
#version 120
varying vec2 uv;
uniform sampler2D scene,depth,bloom,reflection;
uniform vec2 pixel,lens;
uniform float time,hurt;
float z(vec2 p){return .08/(1.-texture2D(depth,p).r*.9995556);}
vec3 position(vec2 p){return vec3((p*2.-1.)*lens,-1.)*z(p);}
void main(){
    vec4 material=texture2D(scene,uv);vec3 c=material.rgb,p=position(uv),n=normalize(cross(dFdx(p),dFdy(p)));
    if(dot(n,-p)<0.)n=-n;float a=-p.z,occ=0.;
    for(int i=0;i<8;i++){float t=float(i)*.785398;vec2 dir=vec2(cos(t),sin(t));vec3 d=position(uv+dir*pixel*clamp(80./max(a,1.),2.,28.))-p;float distance=length(d);occ+=max(0.,dot(n,d/max(distance,.001))-.06)*max(0.,1.-distance/1.6);}
    c*=1.-occ*.065;
    // Flat floors use a clipped, half-resolution mirrored scene, avoiding missing screen-space data.
    if(material.a>.02&&a>.15){float fres=.06+.65*pow(1.-clamp(dot(n,normalize(-p)),0.,1.),4.);vec3 mirror=texture2D(reflection,uv).rgb;c+=mirror*material.a*fres;}
    c+=texture2D(bloom,uv).rgb*.5;vec2 q=uv*2.-1.;c*=1.-dot(q,q)*.13;
    c=(c*(2.51*c+.03))/(c*(2.43*c+.59)+.14);c=pow(clamp(c,0.,1.),vec3(.4545));
    c=mix(c,vec3(.7,.035,.008),hurt*dot(q,q)*.4);float grain=fract(sin(dot(uv+time*.0001,vec2(12.9898,78.233)))*43758.5453);c+=(grain-.5)*.006;gl_FragColor=vec4(c,1);
}

// @shader bloomFS
#version 120
varying vec2 uv;uniform sampler2D source;uniform vec2 step;uniform float cutoff;void main(){vec3 c=vec3(0);float sum=0.;for(int i=-6;i<=6;i++){float x=float(i),w=exp(-x*x*.1);c+=max(texture2D(source,uv+step*x).rgb-cutoff,0.)*w;sum+=w;}gl_FragColor=vec4(c/sum,1);}
