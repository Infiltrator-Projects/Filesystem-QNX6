/* SPDX-License-Identifier: GPL-3.0-or-later
 * Independently byte-authored structural fixture. Not third-party media.
 */
#include <infiltratr/filesystem_module_v2.h>
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum {B=512,N=128,MAX=81920};
static uint8_t disk[MAX],saved[MAX];static unsigned big,mmi,base,first,second,sector,allocs,fail;
static const char longname[]="a-long-filename-which-requires-the-longfile-tree.bin";
static void p16(uint8_t *p,uint32_t n){p[big?0:1]=n>>8;p[big?1:0]=n;}
static void p32(uint8_t *p,uint32_t n){p16(p+(big?0:2),n>>16);p16(p+(big?2:0),n);}
static void p64(uint8_t *p,uint64_t n){p32(p+(big?0:4),n>>32);p32(p+(big?4:0),n);}
static uint8_t *blk(unsigned n){return disk+base+n*B;}
static uint32_t crc(const uint8_t *p,unsigned n)
{uint32_t c=0;while(n--){c^=(uint32_t)*p++<<24;for(unsigned i=0;i<8;++i)c=(c<<1)^((c&0x80000000u)?0x04c11db7u:0);}return c;}
static uint32_t lnamecrc(const char *p)
{uint32_t c=0;while(*p)c=((c>>1)+(unsigned char)*p++)^((c&1)?0x80000000u:0);return c;}
static void fix(uint8_t *p){p32(p+4,crc(p+8,504));}
static void root(uint8_t *p,uint64_t size,unsigned a,unsigned b)
{p64(p,size);for(unsigned i=0;i<16;++i)p32(p+8+4*i,UINT32_MAX);p32(p+8,a);p32(p+12,b);}
static void super(uint8_t *p,unsigned serial,unsigned old)
{
    p32(p,0x68191122);p64(p+8,serial);
    unsigned off=mmi?40:48,t=mmi?64:72;
    p32(p+off,B);p32(p+off+4,6);p32(p+off+8,0);p32(p+off+12,N);p32(p+off+16,100);
    root(p+t,6*128,old?127:0,1);root(p+t+80,16,13,UINT32_MAX);
    root(p+t+160,B,6,UINT32_MAX);fix(p);
}
static void ino(unsigned id,uint16_t mode,unsigned bytes,unsigned ptr,unsigned depth)
{
    uint8_t *p=blk(0)+(id-1)*128;p64(p,bytes);p32(p+8,123);p32(p+12,456);
    p16(p+32,mode);for(unsigned j=0;j<16;++j)p32(p+36+4*j,UINT32_MAX);
    p32(p+36,ptr);p[100]=depth;p[101]=(mode&0170000)==0040000?1:3;
}
static void dent(uint8_t *p,unsigned id,const char *name)
{p32(p,id);p[4]=strlen(name);memcpy(p+5,name,p[4]);}
static void fixture(unsigned variant,unsigned boot)
{
    memset(disk,0,sizeof disk);big=variant==2;mmi=variant==3;first=boot?8192:0;base=first+4096;second=base+N*B;
    super(disk+first,2,1);super(disk+second,3,0);
    ino(1,0040755,6*32,2,0);ino(2,0100644,700,3,1);ino(3,0100644,1,11,0);
    ino(4,0120777,9,12,0);ino(5,0040755,3*32,4,0);ino(6,0100644,3,5,0);
    uint8_t *d=blk(2);dent(d,1,".");dent(d+32,1,"..");dent(d+64,2,"short.bin");
    p32(d+96,3);d[100]=255;p32(d+104,0);p32(d+108,lnamecrc(longname));
    dent(d+128,4,"link");dent(d+160,5,"dir");
    p16(blk(6),strlen(longname));memcpy(blk(6)+2,longname,strlen(longname));
    memset(blk(3),255,B);p32(blk(3),10);p32(blk(3)+4,20);
    for(unsigned i=0;i<B;++i){blk(10)[i]=(uint8_t)(i*7);blk(20)[i]=(uint8_t)(i*13+1);}
    blk(11)[0]='X';memcpy(blk(12),"short.bin",9);
    dent(blk(4),5,".");dent(blk(4)+32,1,"..");dent(blk(4)+64,6,"child");
    memcpy(blk(5),"yes",3);memcpy(saved,disk,sizeof disk);
}
static int rd(void *c,uint64_t off,void *out,uint32_t n)
{(void)c;if(fail||off>sizeof disk||n>sizeof disk-off||off%sector||n%sector)return -1;memcpy(out,disk+off,n);return 0;}
static void *allocate(void *c,size_t n){(void)c;void *p=malloc(n);if(p)++allocs;return p;}
static void release(void *c,void *p){(void)c;if(p){assert(allocs);--allocs;free(p);}}
int main(int argc,char **argv)
{
    assert(argc==2);void *lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);if(!lib){puts(dlerror());return 1;}
    IfsFsModuleApiEntryV2 entry;*(void **)(&entry)=dlsym(lib,IFS_FS_COMMON_EXPORT_V2);assert(entry);
    const IfsFsModuleApiV2 *api=entry();assert(ifs_fs_api_v2_valid(api,"qnx6"));
    const IfsFsCoreOpsV2 *core=(const void *)ifs_fs_group_v2(api,IFS_FS_GROUP_CORE);
    const IfsFsMetadataOpsV2 *meta=(const void *)ifs_fs_group_v2(api,IFS_FS_GROUP_METADATA);
    assert(core&&meta&&meta->readlink&&!core->write);
    for(unsigned variant=1;variant<=3;++variant)for(unsigned boot=0;boot<=1;++boot){
        if(variant==3&&boot)continue;
        fixture(variant,boot);
        for(sector=512;sector<=4096;sector*=8){
            IfsFsHostRuntimeV2 h={.abi_version=2,.structure_bytes=sizeof(h),.capabilities=IFS_FS_HOST_READ_ONLY,
              .volume_bytes=sizeof disk,.logical_sector_bytes=sector,.physical_sector_bytes=sector,.maximum_transfer_bytes=4096,
              .read_at=rd,.allocate=allocate,.release=release};
            uint32_t got=0;assert(core->probe(&h,&got)==0&&got==variant&&!allocs);
            void *v=NULL,*other=NULL;IfsFsVolumeInfoV2 i={.structure_bytes=sizeof i};
            assert(core->mount(&h,&v,&i)==0&&v&&i.variant==variant&&i.total_bytes==N*B&&i.free_bytes==100*B);
            assert(core->mount(&h,&other,&i)==0&&other!=v);
            char name[511],path[520];IfsFsEntryV2 e={.structure_bytes=sizeof e,.name=name,.name_capacity=sizeof name};
            assert(core->stat(v,"/",&e)==0&&e.type==IFS_FS_ENTRY_DIRECTORY);
            assert(core->directory_entry(v,"/",0,&e)==0&&!strcmp(name,"short.bin"));
            assert(core->directory_entry(v,"/",1,&e)==0&&!strcmp(name,longname));
            assert(core->directory_entry(v,"/",4,&e)==IFS_FS_END);
            assert(core->stat(v,"/short.bin",&e)==0&&e.size_bytes==700);
            unsigned char buf[700];assert(core->read(v,"/short.bin",0,buf,700)==0&&!memcmp(buf,blk(10),512)&&!memcmp(buf+512,blk(20),188));
            snprintf(path,sizeof path,"/%s",longname);
            assert(core->read(v,path,0,buf,1)==0&&buf[0]=='X');
            assert(core->read(v,"/dir/child",0,buf,3)==0&&!memcmp(buf,"yes",3));
            assert(core->stat(v,"/link",&e)==0&&e.type==IFS_FS_ENTRY_LINK);
            uint32_t n=0;assert(meta->readlink(v,"/link",name,sizeof name,&n)==0&&n==9&&!strcmp(name,"short.bin"));
            IfsFsMetadataV2 m={.structure_bytes=sizeof m};assert(meta->get(v,"/short.bin",&m)==0&&m.mode==0100644&&m.owner==123&&m.group==456);
            p32(blk(3),3);assert(core->read(v,"/short.bin",0,buf,1)==IFS_FS_CORRUPT);memcpy(disk,saved,sizeof disk);
            p32(blk(3),N);assert(core->read(v,"/short.bin",0,buf,1)==IFS_FS_CORRUPT);memcpy(disk,saved,sizeof disk);
            if(!mmi){blk(2)[108]^=1;assert(core->stat(v,path,&e)==IFS_FS_CORRUPT);memcpy(disk,saved,sizeof disk);}
            fail=1;assert(core->stat(v,"/short.bin",&e)!=0);fail=0;
            assert(core->unmount(v)==0&&core->unmount(v)==IFS_FS_INVALID&&core->unmount(other)==0&&!allocs);
            disk[second+40]^=1;assert(core->probe(&h,&got)!=0);memcpy(disk,saved,sizeof disk);
            p64(disk+first+8,3);fix(disk+first);assert(core->probe(&h,&got)!=0);memcpy(disk,saved,sizeof disk);
            assert(!memcmp(disk,saved,sizeof disk));
        }
    }
    puts("QNX6 byte fixture: both byte orders, MMI, dual commits, indirect reads, long names, symlinks, corruption PASS");
    dlclose(lib);return 0;
}
