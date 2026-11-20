#include "sector_rebase.h"
#include "psx.h"
#include <stdlib.h>
#include <limits.h>

static bool range(uint32_t a,unsigned bytes) {
    return a>=4 && a<sizeof g_ram && bytes<=sizeof g_ram-a;
}
typedef struct { uint32_t nodes,planes,room;unsigned count; } Hull;
static bool hull(uint32_t map,int room,Hull *h) {
    map&=0x1fffffff;
    if(!range(map,52) || room<0 || (unsigned)room>=mem_r16(map+50))return false;
    uint32_t records=mem_r32(map+32)&0x1fffffff;
    if(!range(records,40*mem_r16(map+50)))return false;
    h->room=records+40*(unsigned)room;
    unsigned first=mem_r16(h->room+16);h->count=mem_r16(h->room+18);
    h->nodes=mem_r32(map+24)&0x1fffffff;h->planes=mem_r32(map+16)&0x1fffffff;
    if(!h->count || h->count>4096 || !range(h->nodes,8*(first+h->count)))return false;
    h->nodes+=8*first;return range(h->planes,12);
}
static uint32_t plane(const Hull *h,unsigned i) {
    uint32_t word=mem_r32(h->nodes+8*i);
    if((word&3)!=3)return 0;
    unsigned index=word>>2;
    if(index>(sizeof g_ram-12)/12 || !range(h->planes,12*(index+1)))return 0;
    return h->planes+12*index;
}
typedef struct { uint32_t address;int16_t target[3];bool conflict; } VertexPatch;
static unsigned mesh_align(uint32_t old,uint32_t next,const Hull *a,const Hull *b,const int32_t delta[3]) {
    VertexPatch *patch=calloc(8192,sizeof *patch);if(!patch)return 0;
    unsigned used=0,changed=0;bool valid=true;
    uint32_t av=mem_r32(old)&0x1fffffff,bv=mem_r32(next)&0x1fffffff;
    for(unsigned type=0;type<2 && valid;type++) {
        unsigned corners=type?4:3,stride=type?12:20,offset=type?8:4;
        unsigned ac=mem_r16(a->room+offset+2),bc=mem_r16(b->room+offset+2);
        uint32_t af=mem_r32(old+offset)&0x1fffffff,bf=mem_r32(next+offset)&0x1fffffff;
        af+=stride*mem_r16(a->room+offset);bf+=stride*mem_r16(b->room+offset);
        if(ac!=bc || ac>1024 || !range(af,stride*ac) || !range(bf,stride*bc)){valid=false;break;}
        for(unsigned j=0;j<bc && valid;j++) {
            uint32_t dest[4];int32_t point[4][3],best_target[4][3];int best=49;bool ambiguous=false;
            for(unsigned k=0;k<corners;k++) {
                dest[k]=bv+4*mem_r16(bf+j*stride+2*k);
                if(!range(dest[k],12)){valid=false;break;}
                for(unsigned axis=0;axis<3;axis++)point[k][axis]=(int16_t)mem_r16(dest[k]+2*axis);
            }
            if(!valid)break;
            for(unsigned i=0;i<ac;i++) {
                int32_t target[4][3];bool fits=true;
                for(unsigned k=0;k<corners;k++) {
                    uint32_t p=av+4*mem_r16(af+i*stride+2*k);
                    if(!range(p,12)){valid=false;fits=false;break;}
                    for(unsigned axis=0;axis<3;axis++) {
                        target[k][axis]=(int16_t)mem_r16(p+2*axis)+delta[axis]/4096;
                        if(target[k][axis]<INT16_MIN || target[k][axis]>INT16_MAX)fits=false;
                    }
                }
                if(!fits)continue;
                for(unsigned rotation=0;rotation<corners;rotation++) {
                    int score=0;
                    for(unsigned k=0;k<corners;k++)for(unsigned axis=0;axis<3;axis++) {
                        int d=target[(k+rotation)%corners][axis]-point[k][axis];
                        if(abs(d)>2)score=1000;else score+=d*d;
                    }
                    if(score>best)continue;
                    if(score==best) {
                        for(unsigned k=0;k<corners;k++)for(unsigned axis=0;axis<3;axis++)
                            if(best_target[k][axis]!=target[(k+rotation)%corners][axis])ambiguous=true;
                    } else {
                        best=score;ambiguous=false;
                        for(unsigned k=0;k<corners;k++)for(unsigned axis=0;axis<3;axis++)
                            best_target[k][axis]=target[(k+rotation)%corners][axis];
                    }
                }
            }
            if(best>48 || ambiguous)continue;
            for(unsigned k=0;k<corners;k++) {
                unsigned i=0;for(;i<used && patch[i].address!=dest[k];i++);
                if(i==used) {
                    if(used==8192){valid=false;break;}
                    patch[used++].address=dest[k];
                    for(unsigned axis=0;axis<3;axis++)patch[i].target[axis]=(int16_t)best_target[k][axis];
                } else for(unsigned axis=0;axis<3;axis++)
                    if(patch[i].target[axis]!=best_target[k][axis])patch[i].conflict=true;
            }
        }
    }
    if(valid)for(unsigned i=0;i<used;i++)if(!patch[i].conflict) {
        bool differs=false;
        for(unsigned k=0;k<3;k++) {
            uint32_t address=patch[i].address+2*k;
            if((int16_t)mem_r16(address)!=patch[i].target[k])differs=true;
            mem_w16(address,(uint16_t)patch[i].target[k]);
        }
        changed+=differs;
    }
    free(patch);return changed;
}

unsigned sector_rebase_align(uint32_t transfer) {
    transfer&=0x1fffffff;
    uint32_t old=mem_r32(0xa475c),next=mem_r32(0xa4758);
    uint32_t player=mem_r32(0xa49e8)&0x1fffffff;
    Hull a,b;
    if(old==next || !range(transfer,16) || !range(player,388) ||
       !hull(old,(int16_t)mem_r16(player+326),&a) ||
       !hull(next,(int16_t)mem_r16(transfer+12),&b))return 0;
    int32_t delta[3];
    for(unsigned k=0;k<3;k++) {
        delta[k]=(int32_t)mem_r32(transfer+4*k);
        /* The loader identifies the duplicate room by its minimum corner.
         * Require that identity, rather than matching arbitrary nearby rooms. */
        int difference=(int16_t)mem_r16(b.room+28+2*k)-(int16_t)mem_r16(a.room+28+2*k);
        if((int64_t)difference*4096!=delta[k])return 0;
    }
    unsigned changed=mesh_align(old&0x1fffffff,next&0x1fffffff,&a,&b,delta);
    for(unsigned j=0;j<b.count;j++) {
        uint32_t q=plane(&b,j);if(!q)continue;
        int n[3];int64_t norm=0,shift=0;
        for(unsigned k=0;k<3;k++) {
            n[k]=(int16_t)mem_r16(q+2*k);norm+=(int64_t)n[k]*n[k];
            shift+=((int64_t)n[k]*delta[k])>>12;
        }
        /* Exactly axial planes have no normal quantization seam. */
        unsigned components=(n[0]!=0)+(n[1]!=0)+(n[2]!=0);
        if(components<2 || norm<4093LL*4093 || norm>4098LL*4098)continue;
        int32_t d=(int32_t)mem_r32(q+8);int64_t best=8193,target=d;bool ambiguous=false;
        for(unsigned i=0;i<a.count;i++) {
            uint32_t p=plane(&a,i);if(!p)continue;
            if((int16_t)mem_r16(p)!=n[0] || (int16_t)mem_r16(p+2)!=n[1] ||
               (int16_t)mem_r16(p+4)!=n[2])continue;
            int64_t expected=(int32_t)mem_r32(p+8)-shift;
            int64_t error=llabs(expected-d);
            if(error<best){best=error;target=expected;ambiguous=false;}
            else if(error==best && expected!=target)ambiguous=true;
        }
        /* Separate sector exports have slightly different hull constants.
         * Preserve the existing physical plane only for an unambiguous close
         * copy (<= two world units); genuine shape changes keep native data.
         * Updating guest RAM also keeps subsequent collisions and saves in
         * the same coordinate system, without persistent host-side offsets. */
        if(best && best<=8192 && !ambiguous && target>=INT32_MIN && target<=INT32_MAX) {
            mem_w32(q+8,(uint32_t)(int32_t)target);changed++;
        }
    }
    return changed;
}

/* The guest deletes and recreates type-245 doors when replacing the shared
 * room. Their separately exported spawn positions differ by several units.
 * Carry the already displayed closed pose across that replacement; keep the
 * new door's identity, links, opening direction and animation state. */
void sector_rebase_doors_capture(uint32_t transfer,SectorDoors *state) {
    memset(state,0,sizeof *state);state->room=-1;
    transfer&=0x1fffffff;uint32_t player=mem_r32(0xa49e8)&0x1fffffff;
    if(!range(transfer,16) || !range(player,388))return;
    int room=(int16_t)mem_r16(player+326);state->room=(int16_t)mem_r16(transfer+12);
    for(unsigned i=0;i<96;i++) {
        uint32_t o=0x10d1d0+388*i,data=mem_r32(o+100)&0x1fffffff;
        if(mem_r8(o+384)!=1 || mem_r16(o+322)!=245 ||
           (int16_t)mem_r16(o+326)!=room || !range(data,28) || mem_r16(data+6))continue;
        SectorDoor *d=&state->door[state->count++];
        for(unsigned k=0;k<3;k++) {
            d->p[k]=(int32_t)((uint32_t)mem_r32(o+48+4*k)+mem_r32(transfer+4*k));
            d->angle[k]=mem_r16(o+60+2*k)&4095;
        }
        d->model=mem_r32(o+44);d->kind=mem_r8(data+10);
    }
}
unsigned sector_rebase_doors_restore(const SectorDoors *state) {
    unsigned changed=0;
    if(state->room<0 || state->count>96)return 0;
    for(unsigned i=0;i<96;i++) {
        uint32_t o=0x10d1d0+388*i,data=mem_r32(o+100)&0x1fffffff;
        if(mem_r8(o+384)!=1 || mem_r16(o+322)!=245 ||
           (int16_t)mem_r16(o+326)!=state->room || !range(data,28) || mem_r16(data+6))continue;
        int found=-1;int64_t best=16LL*4096*16*4096;bool ambiguous=false;
        for(unsigned j=0;j<state->count;j++) {
            const SectorDoor *d=&state->door[j];bool same=d->model==mem_r32(o+44) && d->kind==mem_r8(data+10);
            int64_t distance=0;
            for(unsigned k=0;k<3;k++) {
                unsigned angle=mem_r16(o+60+2*k)&4095;
                bool equivalent=d->angle[k]==angle;
                /* A closed upright type-245 door is symmetric under a half
                 * turn. Adjacent sector exports face it toward opposite
                 * portal sides. Match that same physical closed door while
                 * retaining the new orientation for opening and room links. */
                if(k==1 && d->angle[0]==0 && d->angle[2]==0 &&
                   !(mem_r16(o+60)&4095) && !(mem_r16(o+64)&4095) &&
                   ((d->angle[1]-angle)&4095)==2048)equivalent=true;
                if(!equivalent)same=false;
                int64_t diff=(int32_t)mem_r32(o+48+4*k)-(int64_t)d->p[k];
                if(llabs(diff)>16*4096){same=false;break;}
                distance+=diff*diff;
            }
            if(!same)continue;
            if(distance<best){best=distance;found=(int)j;ambiguous=false;}
            else if(distance==best)ambiguous=true;
        }
        if(found<0 || ambiguous)continue;
        for(unsigned k=0;k<3;k++)mem_w32(o+48+4*k,(uint32_t)state->door[found].p[k]);
        changed++;
    }
    return changed;
}
