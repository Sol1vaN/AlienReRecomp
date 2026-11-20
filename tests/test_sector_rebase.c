#include "psx.h"
#include "sector_rebase.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdlib.h>
void bus_write16(uint32_t address,uint32_t value) { abort(); }
#include <stdio.h>

static void fixture(void) {
    memset(g_ram,0,sizeof g_ram);
    mem_w32(0xa475c,0x1000);mem_w32(0xa4758,0x2000);mem_w32(0xa49e8,0x3000);
    mem_w16(0x3000+326,0);
    for(unsigned m=0;m<2;m++) {
        uint32_t map=0x1000+m*0x1000;
        mem_w16(map+50,1);mem_w32(map+32,map+0x100);
        mem_w32(map+16,map+0x200);mem_w32(map+24,map+0x300);
        mem_w16(map+0x100+18,1);mem_w32(map+0x300,3);
        mem_w16(map+0x304,-1);mem_w16(map+0x306,-2);
        mem_w16(map+0x202,-1641);mem_w16(map+0x204,3752);
    }
    mem_w16(0x111c,10);mem_w16(0x111e,10);mem_w16(0x1120,10);
    mem_w16(0x211c,10);mem_w16(0x211e,860);mem_w16(0x2120,4538);
    mem_w32(0x1208,-1470520);mem_w32(0x2208,-17059020);
    mem_w32(0x4000,0);mem_w32(0x4004,850*4096);mem_w32(0x4008,4528*4096);mem_w16(0x400c,0);
}
int main(void) {
    fixture();
    int64_t before=((int64_t)-1641*393344>>12)+((int64_t)3752*1775917>>12)-1470520;
    assert(before<0);
    assert(sector_rebase_align(0x4000)==1);
    int32_t d=(int32_t)mem_r32(0x2208);
    int64_t after=((int64_t)-1641*3874944>>12)+((int64_t)3752*20322605>>12)+d;
    assert(before==after);assert(sector_rebase_align(0x4000)==0);
    /* Do not turn a different room, wall orientation or real shape change
     * into an origin correction. Exact axis planes remain authored. */
    fixture();mem_w16(0x211c,11);assert(sector_rebase_align(0x4000)==0);
    fixture();mem_w16(0x2202,-1640);assert(sector_rebase_align(0x4000)==0);
    fixture();mem_w32(0x2208,-17059020+8*4096);assert(sector_rebase_align(0x4000)==0);
    fixture();mem_w16(0x1202,0);mem_w16(0x1204,4096);mem_w16(0x2202,0);mem_w16(0x2204,4096);
    assert(sector_rebase_align(0x4000)==0);
    /* Ambiguous equidistant copies must not pick an arbitrary wall. */
    fixture();mem_w16(0x1112,2);mem_w32(0x1308,7);
    mem_w16(0x120c+2,-1641);mem_w16(0x120c+4,3752);
    mem_w32(0x120c+8,-1470520+11412);assert(sector_rebase_align(0x4000)==0);
    fixture();mem_w32(0x2010,0x1ffffc);assert(sector_rebase_align(0x4000)==0);
    fixture();mem_w16(0x2112,65535);assert(sector_rebase_align(0x4000)==0);
    fixture();assert(sector_rebase_align(0x1ffffc)==0);
    /* The shared mesh may be reordered between exports. Match complete
     * faces, preserve non-position vertex data, and reject distant copies. */
    fixture();
    for(unsigned m=0;m<2;m++) {
        uint32_t map=0x1000+m*0x1000,face=0x1600+m*0x1000,verts=0x1700+m*0x1000;
        mem_w32(map,verts);mem_w32(map+4,face);mem_w32(map+8,face);
        mem_w16(map+0x100+10,1);
        for(unsigned k=0;k<4;k++) {
            mem_w16(face+2*k,3*((k+m)%4));
            mem_w16(verts+12*k,10+20*(k==1 || k==2));
            mem_w16(verts+12*k+2,10+10*(k>=2)+m*852);
            mem_w16(verts+12*k+4,10+m*4528);mem_w16(verts+12*k+6,1234+k);
            mem_w32(verts+12*k+8,0x00556677);
        }
    }
    assert(sector_rebase_align(0x4000)==5);
    for(unsigned k=0;k<4;k++) {
        assert(mem_r16(0x2700+12*k+2)==860+10*(k>=2));
        assert(mem_r16(0x2700+12*k+6)==1234+k);
        assert(mem_r32(0x2700+12*k+8)==0x00556677);
    }
    assert(sector_rebase_align(0x4000)==0);
    /* Recreated closed doors retain their displayed pose. Opening state,
     * orientation, identity and portal links remain those of the new door. */
    fixture();uint32_t old=0x10d1d0,next=old+388;
    mem_w8(old+384,1);mem_w16(old+322,245);mem_w16(old+326,0);
    mem_w32(old+100,0x5000);mem_w32(old+44,0x123456);mem_w8(0x500a,3);
    SectorDoors doors;sector_rebase_doors_capture(0x4000,&doors);assert(doors.count==1);
    mem_w8(old+384,0);mem_w8(next+384,1);mem_w16(next+322,245);mem_w16(next+326,0);
    mem_w32(next+100,0x5100);mem_w32(next+44,0x123456);mem_w8(0x510a,3);
    mem_w32(next+188,0x87654321);
    for(unsigned k=0;k<3;k++)mem_w32(next+48+4*k,(uint32_t)doors.door[0].p[k]);
    mem_w32(next+56,(uint32_t)doors.door[0].p[2]+6*4096);
    assert(sector_rebase_doors_restore(&doors)==1);
    assert((int32_t)mem_r32(next+56)==doors.door[0].p[2]);assert(mem_r32(next+188)==0x87654321);
    mem_w32(next+56,(uint32_t)doors.door[0].p[2]+17*4096);assert(!sector_rebase_doors_restore(&doors));
    mem_w32(next+56,(uint32_t)doors.door[0].p[2]+6*4096);mem_w16(0x5106,1);
    assert(!sector_rebase_doors_restore(&doors));mem_w16(0x5106,0);
    /* The same closed upright door is exported facing the other portal side.
     * Preserve its center, not its old orientation or the old room's links. */
    mem_w16(next+62,2048);assert(sector_rebase_doors_restore(&doors)==1);
    assert((int32_t)mem_r32(next+56)==doors.door[0].p[2]);
    assert(mem_r16(next+62)==2048 && mem_r32(next+188)==0x87654321);
    mem_w32(next+56,(uint32_t)doors.door[0].p[2]+6*4096);
    mem_w16(next+62,1024);assert(!sector_rebase_doors_restore(&doors));
    mem_w16(next+62,2048);mem_w16(next+60,1);assert(!sector_rebase_doors_restore(&doors));
    mem_w16(next+60,0);doors.door[0].angle[1]=2048;mem_w16(next+62,0);
    assert(sector_rebase_doors_restore(&doors)==1);assert(mem_r16(next+62)==0);
    puts("sector origin continuity: ok");return 0;
}
