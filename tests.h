/* Developer-only checks, compiled against the same game implementation. */
CORE void advanceTest(float seconds){for(int i=0;i<(int)(seconds*120+.5f);i++){totalTime+=1.f/120;simulate(1.f/120);}}
CORE int selftest(void){
    int fails=0;logFile=fopen("selftest.txt","w");resetGame();mode=1;
    fails+=check(!collides(player.x,player.z,.33),"Start is collision free");
    distanceField();for(int i=0;i<3;i++)fails+=check(distmap[(int)(relays[i].p.z/CELL)][(int)(relays[i].p.x/CELL)]<9999,"Relay reachable before opening gate");
    fails+=check(distmap[35][17]==9999,"Containment gate blocks Warden arena");
    V3 p=cellpos(16,27);moveBody(&p,0,20,.33);fails+=check(p.z<28*CELL,"Swept movement cannot tunnel through locked gate");
    p=player;moveBody(&p,-200,0,.33);fails+=check(p.x>=13*CELL,"Swept movement cannot tunnel through outer wall");
    float hp=health;hurtPlayer(20);fails+=check(health<hp&&armor<50,"Armor absorbs part of damage");
    mode=1;ammo[0]=0;reserve[0]=31;reloadWeapon();advanceTest(1.4f);fails+=check(ammo[0]==30&&reserve[0]==1,"Reload conserves ammunition");
    resetGame();mode=1;Enemy *e=enemies;player=cellpos(17,10);player.y=1.65;yaw=0;pitch=-.14;float ehp=e->hp;fireWeapon();fails+=check(e->hp<ehp&&ammo[0]==29,"Hitscan damages target and spends one cartridge");
    shootCD=0;ammo[0]=0;int priorShots=shotsFired;fireWeapon();fails+=check(shotsFired==priorShots,"Empty weapon cannot fire");
    resetGame();mode=1;for(int i=0;i<3;i++){player=add(relays[i].p,v3(0,1.65,-1));interact();interact();}fails+=check(relayCount==3,"Relay interactions are idempotent");
    player=cellpos(17,26);player.y=1.65;distanceField();fails+=check(distmap[41][17]<9999,"All relays open route to extraction");
    for(int i=0;i<enemyCount;i++)if(enemies[i].type==3)damageEnemy(i,1500,enemies[i].p);
    fails+=check(bossDead==1,"Warden death unlocks extraction");
    player=cellpos(17,41);player.y=1.65;evacSpawned=1;for(int i=0;i<enemyCount;i++)enemies[i].hp=0;advanceTest(6.2f);fails+=check(mode==4,"Extraction completes mission");
    resetGame();mode=1;armor=0;hurtPlayer(999);fails+=check(mode==3&&health==0,"Lethal damage enters loss state");
    resetGame();fails+=check(health==100&&relayCount==0&&!bossDead&&kills==0,"Restart resets mission state");
    resetGame();mode=1;player=cellpos(4,5);player.y=3.2f;vy=-1;grounded=0;advanceTest(.5f);fails+=check(fabs(player.y-eyeHeight-.9f)<.03f,"Player lands on a crate support surface");
    V3 finalPos[3];const int rates[3]={30,60,144};for(int r=0;r<3;r++){resetGame();mode=1;memset(keys,0,sizeof(keys));keys['W']=1;for(int f=0;f<rates[r]*2;f++)advanceFrame(1.f/rates[r]);finalPos[r]=player;}
    fails+=check(length(sub(finalPos[0],finalPos[1]))<.06f&&length(sub(finalPos[1],finalPos[2]))<.06f,"Movement consistent at 30, 60 and 144 render FPS");
    resetGame();mode=1;memset(keys,0,sizeof(keys));pressed[VK_SPACE]=1;advanceFrame(1.f/240);advanceFrame(1.f/240);fails+=check(vy>0&&!grounded,"Jump edge survives a frame without a simulation tick");
    resetGame();mode=1;throwGrenade();fails+=check(grenades==3&&grenadesLive[0].fuse>0,"Grenade consumes inventory and spawns a live projectile");
    grenadesLive[0].p=cellpos(17,4);grenadesLive[0].p.y=.2f;grenadesLive[0].v=v3(1,-3,0);stepGrenades(.05f);fails+=check(grenadesLive[0].p.y>=.12f&&grenadesLive[0].v.y>0,"Grenade bounces above the floor");
    resetGame();mode=1;int barrel=-1;for(int i=0;i<propCount;i++)if(props[i].kind==3){barrel=i;break;}V3 blastAt=add(props[barrel].p,v3(0,.6f,0));blast(blastAt,6,200);fails+=check(!props[barrel].active,"Explosion destroys a nearby barrel");
    fails+=check(!bodyCollision(blastAt.x,blastAt.z,.1f,0,1),"Destroyed barrel releases its collision volume");
    resetGame();mode=1;propCount=0;prop(16,15,.8f,1.2f,.8f,3);prop(17,15,.8f,1.2f,.8f,3);blast(add(props[0].p,v3(0,.6f,0)),5.5f,200);fails+=check(!props[0].active&&!props[1].active,"Adjacent barrels chain-react");
    resetGame();mode=1;propCount=0;prop(16,15,.8f,1.2f,.8f,3);prop(18,15,.8f,1.2f,.8f,3);map[15][17]=1;blast(add(props[0].p,v3(0,.6f,0)),7,200);fails+=check(props[1].active,"Walls block blast propagation to other barrels");
    if(logFile){fprintf(logFile,"RESULT: %d failures\n",fails);fclose(logFile);logFile=NULL;}return fails;
}
CORE int campaignTest(int level){
    logFile=fopen("campaign.txt","w");resetGame();mode=1;difficulty=level;
    int route[MH][MW],qx[MW*MH],qz[MW*MH],previous=-1,usedGrenades=0;
    for(int frame=0;frame<60*600&&mode==1;frame++){
        V3 goal=relayCount<3?relays[relayCount].p:cellpos(17,41);if(upgrades<2||(relayCount==3&&upgrades<3))for(int i=0;i<pickupCount;i++)if(pickups[i].active&&pickups[i].type==3){goal=pickups[i].p;break;}int gx=(int)(goal.x/CELL),gz=(int)(goal.z/CELL);
        for(int z=0;z<MH;z++)for(int x=0;x<MW;x++)route[z][x]=9999;int head=0,tail=1;qx[0]=gx;qz[0]=gz;route[gz][gx]=0;
        while(head<tail){int x=qx[head],z=qz[head++];for(int d=0;d<4;d++){int nx=x+(d==0)-(d==1),nz=z+(d==2)-(d==3);V3 p=cellpos(nx,nz);if(!solid(nx,nz)&&!collides(p.x,p.z,.4)&&route[nz][nx]==9999){route[nz][nx]=route[z][x]+1;qx[tail]=nx;qz[tail++]=nz;}}}
        int x=(int)(player.x/CELL),z=(int)(player.z/CELL),best=route[z][x];V3 target=cellpos(x,z);
        for(int d=0;d<4;d++){int nx=x+(d==0)-(d==1),nz=z+(d==2)-(d==3);if(!solid(nx,nz)&&route[nz][nx]<best){best=route[nz][nx];target=cellpos(nx,nz);}}
        V3 dir=sub(target,player);dir.y=0;float walkAngle=atan2f(dir.x,dir.z);int hit=-1;float nearest=42;
        for(int i=0;i<enemyCount;i++){Enemy *e=enemies+i;if(e->hp<=0)continue;V3 point=add(e->p,v3(0,e->type==3?2.3f:e->type==1?1.85f:1.35f,0)),d=sub(point,player);float len=length(d);if(len<nearest&&traceWorld(player,norm(d),len+.3)>len-.1){nearest=len;hit=i;}}
        memset(keys,0,sizeof(keys));mouseDown=0;
        if(hit>=0){Enemy *e=enemies+hit;V3 d=sub(add(e->p,v3(0,e->type==3?2.3f:e->type==1?1.85f:1.35f,0)),player);yaw=atan2f(d.x,d.z);pitch=atan2f(d.y,sqrtf(d.x*d.x+d.z*d.z));
            int choice=(e->type>=2&&ammo[2]+reserve[2]>0)?2:nearest<6&&ammo[1]+reserve[1]>0?1:0;if(ammo[choice]+reserve[choice]==0)for(int k=0;k<3;k++)if(ammo[k]+reserve[k]>0)choice=k;
            if(choice!=weapon){weapon=choice;reloadTime=0;}mouseDown=1;if(frame%150==0&&nearest>7&&nearest<16&&grenades>0&&grenadeCD<=0){pressed['G']=1;usedGrenades++;}
        }else{yaw=walkAngle;pitch=0;}
        if((hit<0||nearest>13||relayCount<3)&&length(dir)>.15){float rel=walkAngle-yaw;float f=cosf(rel),s=sinf(rel);if(f>.3)keys['W']=1;if(f<-.3)keys['S']=1;if(s>.3)keys['A']=1;if(s<-.3)keys['D']=1;}
        if(relayCount<3&&dsq(player.x-goal.x,player.z-goal.z)<9)pressed['E']=1;
        totalTime+=1.f/60;advanceFrame(1.f/60);
        if(previous!=relayCount){fprintf(logFile,"At %.1fs: relays=%d health=%.1f kills=%d\n",gameTime,relayCount,health,kills);previous=relayCount;}
    }
    int ok=mode==4&&upgrades==3&&usedGrenades>0;fprintf(logFile,"Upgrades %d/3, grenades thrown %d\n",upgrades,usedGrenades);fprintf(logFile,"%s Campaign with ordinary movement, firing, damage, pickups and interactions. Time %.1f, kills %d, relays %d, Warden %d, health %.1f, mode %d\n",ok?"PASS":"FAIL",gameTime,kills,relayCount,bossDead,health,mode);fclose(logFile);logFile=NULL;return !ok;
}
