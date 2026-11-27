/* SPDX-License-Identifier: GPL-3.0-or-later
 * Independent QNX6 Power-Safe read-only filesystem implementation.
 * On-disk references: Linux v6.12.107 qnx6_fs.h and fs/qnx6, original
 * format implementation by Kai Bankett; Filesystems/native/filesystems/qnx6.
 * All storage and allocation use the canonical Common V2 runtime.
 */
#include <infiltratr/filesystem_module_v2.h>
#include <stdint.h>
#include <stddef.h>
#include <stdatomic.h>
enum { SUPER=512,AREA=4096,BOOT=8192,MAX_BLOCK=4096,MAX_LEVEL=5,
       PTRS=16,INODE=128,DIRENT=32,MAX_NAME=510,SHORT_NAME=27,
       SLOTS=8,MAX_DIR_ENTRIES=262144 };
#define MAGIC UINT32_C(0x68191122)
#define NONE UINT32_MAX
typedef struct {uint64_t bytes;uint32_t ptr[PTRS];uint8_t levels;} Tree;
typedef struct {Tree tree;uint32_t id,mode,uid,gid,type;} Node;
typedef struct {
    IfsFsHostRuntimeV2 host;
    uint64_t base,serial;
    uint32_t block,blocks,inodes,free_blocks,variant;
    unsigned be,mmi;
    Tree inode_table,longfile;
    uintptr_t token;
} Volume;
typedef struct {Node node;uint32_t length;char name[MAX_NAME+1];} Entry;
static Volume slots[SLOTS];
static uintptr_t generation;
static atomic_flag gate=ATOMIC_FLAG_INIT;
static int enter(void){return !atomic_flag_test_and_set_explicit(&gate,memory_order_acquire);}
static int leave(int rc){atomic_flag_clear_explicit(&gate,memory_order_release);return rc;}
static void copy(void *d,const void *s,size_t n){uint8_t *o=d;const uint8_t *p=s;while(n--)*o++=*p++;}
static void zero(void *d,size_t n){uint8_t *p=d;while(n--)*p++=0;}
static int same(const void *a,const void *b,size_t n)
{const uint8_t *p=a,*q=b;while(n--)if(*p++!=*q++)return 0;return 1;}
static uint32_t u16(const uint8_t *p,unsigned be){return be?((uint32_t)p[0]<<8|p[1]):((uint32_t)p[1]<<8|p[0]);}
static uint32_t u32(const uint8_t *p,unsigned be){return be?u16(p,be)<<16|u16(p+2,be):u16(p+2,be)<<16|u16(p,be);}
static uint64_t u64(const uint8_t *p,unsigned be){return be?(uint64_t)u32(p,be)<<32|u32(p+4,be):(uint64_t)u32(p+4,be)<<32|u32(p,be);}
static uint32_t crc32(const uint8_t *p,size_t n)
{
    uint32_t crc=0;
    while(n--){crc^=(uint32_t)*p++<<24;for(unsigned i=0;i<8;++i)
        crc=(crc<<1)^((crc&UINT32_C(0x80000000))?UINT32_C(0x04c11db7):0);}
    return crc;
}
static int rd(const Volume *v,uint64_t off,void *p,uint32_t n)
{return ifs_fs_host_read_bytes_v2(&v->host,off,p,n);}
static int cancel(const Volume *v){return v->host.cancelled&&v->host.cancelled(v->host.context);}
static int tree_valid(const Volume *v,const Tree *t)
{
    if(t->levels>MAX_LEVEL||t->bytes>(uint64_t)v->blocks*v->block)return 0;
    uint64_t cap=PTRS;
    for(unsigned i=0;i<t->levels;++i)cap*=v->block/4;
    uint64_t need=(t->bytes+v->block-1)/v->block;
    return need<=cap;
}
static void root_tree(const Volume *v,const uint8_t *p,Tree *t)
{
    t->bytes=u64(p,v->be);t->levels=p[72];
    for(unsigned i=0;i<PTRS;++i)t->ptr[i]=u32(p+8+4*i,v->be);
}
/* Map one logical block through the fixed-depth pointer tree. An unused
 * pointer inside the declared file is not authority to fabricate a hole.
 * Check every physical block and reject loops along the consumed path. */
static int physical(const Volume *v,const Tree *t,uint64_t logical,uint32_t *out)
{
    if(!tree_valid(v,t)||logical>=(t->bytes+v->block-1)/v->block)return IFS_FS_CORRUPT;
    uint32_t fan=v->block/4;uint64_t divisor=1;
    for(unsigned i=0;i<t->levels;++i)divisor*=fan;
    if(logical/divisor>=PTRS)return IFS_FS_CORRUPT;
    uint32_t b=t->ptr[logical/divisor],seen[MAX_LEVEL];
    logical%=divisor;
    uint8_t data[MAX_BLOCK];
    for(unsigned i=0;;++i){
        if(b==NONE||b>=v->blocks)return IFS_FS_CORRUPT;
        for(unsigned j=0;j<i;++j)if(seen[j]==b)return IFS_FS_CORRUPT;
        if(i==t->levels){*out=b;return IFS_FS_OK;}
        seen[i]=b;
        if(cancel(v))return IFS_FS_BUSY;
        int rc=rd(v,v->base+(uint64_t)b*v->block,data,v->block);if(rc)return rc;
        divisor/=fan;uint64_t index=logical/divisor;logical%=divisor;
        if(index>=fan)return IFS_FS_CORRUPT;
        b=u32(data+4*index,v->be);
    }
}
static int tree_read(const Volume *v,const Tree *t,uint64_t at,void *out,uint32_t n)
{
    if((!out&&n)||at>t->bytes||n>t->bytes-at)return IFS_FS_INVALID;
    uint8_t *p=out;
    while(n){
        if(cancel(v))return IFS_FS_BUSY;
        uint32_t block,skip=(uint32_t)(at%v->block),chunk=v->block-skip;
        if(chunk>n)chunk=n;
        int rc=physical(v,t,at/v->block,&block);if(rc)return rc;
        rc=rd(v,v->base+(uint64_t)block*v->block+skip,p,chunk);if(rc)return rc;
        p+=chunk;at+=chunk;n-=chunk;
    }
    return IFS_FS_OK;
}
static int inode(const Volume *v,uint32_t id,Node *out)
{
    if(!id||id>v->inodes)return IFS_FS_CORRUPT;
    uint8_t raw[INODE];int rc=tree_read(v,&v->inode_table,(uint64_t)(id-1)*INODE,raw,INODE);
    if(rc)return rc;
    Node n={.id=id,.mode=u16(raw+32,v->be),.uid=u32(raw+8,v->be),.gid=u32(raw+12,v->be)};
    n.tree.bytes=u64(raw,v->be);n.tree.levels=raw[100];
    for(unsigned i=0;i<PTRS;++i)n.tree.ptr[i]=u32(raw+36+4*i,v->be);
    switch(n.mode&0170000u){
    case 0040000u:n.type=IFS_FS_ENTRY_DIRECTORY;break;
    case 0100000u:n.type=IFS_FS_ENTRY_FILE;break;
    case 0120000u:n.type=IFS_FS_ENTRY_LINK;break;
    default:return IFS_FS_UNSUPPORTED;
    }
    if(raw[101]==2||!tree_valid(v,&n.tree)||
       (n.type==IFS_FS_ENTRY_DIRECTORY&&n.tree.bytes%DIRENT))return IFS_FS_CORRUPT;
    *out=n;return IFS_FS_OK;
}
static int root_check(const Volume *v)
{
    Node root;int rc=inode(v,1,&root);if(rc)return rc;
    if(root.type!=IFS_FS_ENTRY_DIRECTORY||root.tree.bytes<2*DIRENT)return IFS_FS_CORRUPT;
    uint8_t data[2*DIRENT];rc=tree_read(v,&root.tree,0,data,sizeof(data));if(rc)return rc;
    if(u32(data,v->be)!=1||u32(data+DIRENT,v->be)!=1||
       data[4]!=1||data[5]!='.'||data[DIRENT+4]!=2||
       data[DIRENT+5]!='.'||data[DIRENT+6]!='.')return IFS_FS_CORRUPT;
    return IFS_FS_OK;
}
/* Both superblocks must pass the format CRC and agree on immutable
 * geometry. Select the committed serial, not the copy that happens to
 * contain the first readable inode. No fallback to a stale generation. */
static int candidate(const Volume *host,uint64_t first,unsigned mmi,Volume *out)
{
    if(first>host->host.volume_bytes||SUPER>host->host.volume_bytes-first)return IFS_FS_NOT_FOUND;
    uint8_t a[SUPER],b[SUPER];int rc=rd(host,first,a,SUPER);if(rc)return rc;
    unsigned be=0;
    if(u32(a,0)!=MAGIC){if(u32(a,1)!=MAGIC)return IFS_FS_NOT_FOUND;be=1;}
    if(mmi&&be)return IFS_FS_UNSUPPORTED;
    if(u32(a+4,be)!=crc32(a+8,SUPER-8))return IFS_FS_CORRUPT;
    uint32_t off=mmi?40:48,tree=mmi?64:72;
    uint32_t block=u32(a+off,be),blocks=u32(a+off+12,be),inodes=u32(a+off+4,be);
    if(block<512||block>MAX_BLOCK||(block&(block-1))||first%block||
       !blocks||!inodes||inodes>(uint64_t)blocks*block/INODE)
        return IFS_FS_CORRUPT;
    uint64_t base=first+AREA;
    if(base>host->host.volume_bytes||blocks>(host->host.volume_bytes-base)/block)
        return IFS_FS_CORRUPT;
    uint64_t second=base+(uint64_t)blocks*block;
    if(second>host->host.volume_bytes||SUPER>host->host.volume_bytes-second)return IFS_FS_CORRUPT;
    rc=rd(host,second,b,SUPER);if(rc)return rc;
    if(u32(b,be)!=MAGIC||u32(b+4,be)!=crc32(b+8,SUPER-8)||
       u32(b+off,be)!=block||u32(b+off+4,be)!=inodes||u32(b+off+12,be)!=blocks||
       !same(a+(mmi?28:32),b+(mmi?28:32),mmi?12:16))return IFS_FS_CORRUPT;
    uint64_t x=u64(a+8,be),y=u64(b+8,be);
    /* Equal serials with divergent tree roots have no unique authority. */
    if(x==y&&(!same(a+tree,b+tree,4*80)||u32(a+off+16,be)!=u32(b+off+16,be)))
        return IFS_FS_CORRUPT;
    const uint8_t *s=x>=y?a:b;
    Volume v=*host;v.be=be;v.mmi=mmi;v.variant=mmi?3:be?2:1;v.base=base;
    v.serial=x>=y?x:y;v.block=block;v.blocks=blocks;v.inodes=inodes;
    v.free_blocks=u32(s+off+16,be);
    if(v.free_blocks>blocks||u32(s+off+8,be)>inodes)return IFS_FS_CORRUPT;
    root_tree(&v,s+tree,&v.inode_table);root_tree(&v,s+tree+160,&v.longfile);
    if(!tree_valid(&v,&v.inode_table)||!tree_valid(&v,&v.longfile)||
       v.inode_table.bytes<(uint64_t)inodes*INODE)return IFS_FS_CORRUPT;
    rc=root_check(&v);if(rc)return rc;
    *out=v;return IFS_FS_OK;
}
static int discover(const IfsFsHostRuntimeV2 *h,Volume *out)
{
    if(!ifs_fs_host_v2_valid(h))return IFS_FS_INVALID;
    Volume host={0};size_t n=h->structure_bytes;
    if(n>sizeof(host.host))n=sizeof(host.host);
    copy(&host.host,h,n);host.host.structure_bytes=(uint32_t)n;
    Volume found;int have=0,failure=IFS_FS_NOT_FOUND;
    for(unsigned i=0;i<3;++i){
        Volume v;int rc=candidate(&host,i==0?BOOT:0,i==2,&v);
        if(rc==IFS_FS_OK){if(have)return IFS_FS_CORRUPT;found=v;have=1;}
        else if(rc!=IFS_FS_NOT_FOUND){
            if(rc==IFS_FS_IO_ERROR||rc==IFS_FS_NO_MEMORY||rc==IFS_FS_BUSY)return rc;
            failure=rc;
        }
    }
    if(!have)return failure;
    *out=found;return IFS_FS_OK;
}
static Volume *mounted(void *p)
{if(p)for(unsigned i=0;i<SLOTS;++i)if(slots[i].token==(uintptr_t)p)return &slots[i];return NULL;}
static void info(const Volume *v,IfsFsVolumeInfoV2 *o)
{*o=(IfsFsVolumeInfoV2){.structure_bytes=sizeof(*o),.variant=v->variant,.max_name_bytes=MAX_NAME,
.block_bytes=v->block,.total_bytes=(uint64_t)v->blocks*v->block,.free_bytes=(uint64_t)v->free_blocks*v->block};}
static int probe_impl(const IfsFsHostRuntimeV2 *h,uint32_t *variant)
{if(!variant)return IFS_FS_INVALID;Volume v;int rc=discover(h,&v);if(!rc)*variant=v.variant;return rc;}
static int mount_impl(const IfsFsHostRuntimeV2 *h,void **out,IfsFsVolumeInfoV2 *o)
{
    if(!out||!o||o->structure_bytes<sizeof(*o))return IFS_FS_INVALID;
    *out=NULL;if(!ifs_fs_host_v2_valid(h))return IFS_FS_INVALID;
    if(!(h->capabilities&IFS_FS_HOST_READ_ONLY))return IFS_FS_ACCESS;
    unsigned i=0;while(i<SLOTS&&slots[i].token)++i;
    if(i==SLOTS||generation==UINTPTR_MAX)return IFS_FS_BUSY;
    int rc=discover(h,&slots[i]);if(rc)return rc;
    slots[i].token=++generation;*out=(void *)slots[i].token;info(&slots[i],o);return IFS_FS_OK;
}
static int unmount_impl(void *p)
{Volume *v=mounted(p);if(!v)return IFS_FS_INVALID;zero(v,sizeof(*v));return IFS_FS_OK;}
static uint32_t long_checksum(const char *s,uint32_t n)
{
    uint32_t sum=0;
    while(n--){uint32_t byte=(uint32_t)(int32_t)(int8_t)*s++;sum=((sum>>1)+byte)^((sum&1)?UINT32_C(0x80000000):0);}
    return sum;
}
static int name_decode(const Volume *v,const uint8_t raw[DIRENT],Entry *e)
{
    uint32_t n=raw[4];
    if(n==255){
        uint64_t at=(uint64_t)u32(raw+8,v->be)*v->block;
        uint8_t len[2];int rc=tree_read(v,&v->longfile,at,len,2);if(rc)return rc;
        n=u16(len,v->be);
        if(!n||n>MAX_NAME||n>v->block-2)return IFS_FS_CORRUPT;
        rc=tree_read(v,&v->longfile,at+2,e->name,n);if(rc)return rc;
        if(!v->mmi&&long_checksum(e->name,n)!=u32(raw+12,v->be))return IFS_FS_CORRUPT;
    }else{
        if(!n||n>SHORT_NAME)return IFS_FS_CORRUPT;
        copy(e->name,raw+5,n);
    }
    for(uint32_t i=0;i<n;++i)if(!e->name[i]||e->name[i]=='/'||e->name[i]=='\\')return IFS_FS_CORRUPT;
    e->name[n]=0;e->length=n;return IFS_FS_OK;
}
static int child(const Volume *v,const Node *dir,const char *name,uint32_t length,uint32_t index,Entry *out)
{
    if(dir->type!=IFS_FS_ENTRY_DIRECTORY)return IFS_FS_NOT_FOUND;
    if(dir->tree.bytes/DIRENT>MAX_DIR_ENTRIES)return IFS_FS_UNSUPPORTED;
    int found=0;Entry result;
    for(uint64_t at=0;at<dir->tree.bytes;at+=DIRENT){
        uint8_t raw[DIRENT];int rc=tree_read(v,&dir->tree,at,raw,DIRENT);if(rc)return rc;
        uint32_t id=u32(raw,v->be);if(!id||!raw[4])continue;
        if(id>v->inodes)return IFS_FS_CORRUPT;
        Entry e;rc=name_decode(v,raw,&e);if(rc)return rc;
        if((e.length==1&&e.name[0]=='.')||(e.length==2&&same(e.name,"..",2)))continue;
        if(name?(length==e.length&&same(name,e.name,length)):index==0){
            if(found&&name)return IFS_FS_CORRUPT;
            if(!found){
                rc=inode(v,id,&e.node);if(rc)return rc;
                result=e;found=1;
                if(!name){*out=result;return IFS_FS_OK;}
            }
        }
        if(!name&&index)--index;
    }
    if(!found)return name?IFS_FS_NOT_FOUND:IFS_FS_END;
    *out=result;return IFS_FS_OK;
}
static int resolve(const Volume *v,const char *path,Entry *out)
{
    if(!path||*path!='/')return IFS_FS_INVALID;
    Entry e={.length=1,.name="/"};int rc=inode(v,1,&e.node);if(rc)return rc;
    ++path;
    for(unsigned depth=0;*path;++depth){
        if(depth>=256)return IFS_FS_INVALID;
        uint32_t n=0;while(n<=MAX_NAME&&path[n]&&path[n]!='/')++n;
        if(!n||n>MAX_NAME||(n==1&&path[0]=='.')||(n==2&&same(path,"..",2)))return IFS_FS_INVALID;
        Node dir=e.node;rc=child(v,&dir,path,n,0,&e);if(rc)return rc;
        path+=n;if(*path=='/'){++path;if(!*path)return IFS_FS_INVALID;}
    }
    *out=e;return IFS_FS_OK;
}
static int emit(const Entry *e,IfsFsEntryV2 *o)
{
    if(!o||o->structure_bytes<sizeof(*o)||!o->name)return IFS_FS_INVALID;
    if(o->name_capacity<=e->length)return IFS_FS_NO_SPACE;
    char *name=o->name;uint32_t cap=o->name_capacity;
    *o=(IfsFsEntryV2){.structure_bytes=sizeof(*o),.type=e->node.type,.size_bytes=e->node.tree.bytes,
      .name=name,.name_capacity=cap,.name_bytes=e->length};
    copy(name,e->name,e->length+1);return IFS_FS_OK;
}
static int stat_impl(void *p,const char *path,IfsFsEntryV2 *o)
{Volume *v=mounted(p);if(!v)return IFS_FS_INVALID;Entry e;int rc=resolve(v,path,&e);return rc?rc:emit(&e,o);}
static int list_impl(void *p,const char *path,uint32_t i,IfsFsEntryV2 *o)
{Volume *v=mounted(p);if(!v)return IFS_FS_INVALID;Entry dir,e;int rc=resolve(v,path,&dir);if(!rc)rc=child(v,&dir.node,NULL,0,i,&e);return rc?rc:emit(&e,o);}
static int read_impl(void *p,const char *path,uint64_t at,void *out,uint32_t n)
{Volume *v=mounted(p);if(!v)return IFS_FS_INVALID;Entry e;int rc=resolve(v,path,&e);if(rc)return rc;return e.node.type==IFS_FS_ENTRY_FILE?tree_read(v,&e.node.tree,at,out,n):IFS_FS_UNSUPPORTED;}
static int readlink_impl(void *p,const char *path,char *out,uint32_t cap,uint32_t *n)
{
    if(!n)return IFS_FS_INVALID;
    *n=0;Volume *v=mounted(p);if(!v||!out)return IFS_FS_INVALID;
    Entry e;int rc=resolve(v,path,&e);if(rc)return rc;
    if(e.node.type!=IFS_FS_ENTRY_LINK)return IFS_FS_INVALID;
    if(!e.node.tree.bytes||e.node.tree.bytes>=cap||e.node.tree.bytes>4095)return IFS_FS_NO_SPACE;
    uint32_t len=(uint32_t)e.node.tree.bytes;
    char target[4096];rc=tree_read(v,&e.node.tree,0,target,len);if(rc)return rc;
    for(uint32_t i=0;i<len;++i)if(!target[i])return IFS_FS_CORRUPT;
    copy(out,target,len);out[len]=0;*n=len;return IFS_FS_OK;
}
static int query_impl(void *p,IfsFsVolumeInfoV2 *o)
{Volume *v=mounted(p);if(!v||!o||o->structure_bytes<sizeof(*o))return IFS_FS_INVALID;info(v,o);return IFS_FS_OK;}
static int metadata_impl(void *p,const char *path,IfsFsMetadataV2 *o)
{
    Volume *v=mounted(p);if(!v||!o||o->structure_bytes<sizeof(*o))return IFS_FS_INVALID;
    Entry e;int rc=resolve(v,path,&e);if(rc)return rc;
    *o=(IfsFsMetadataV2){.structure_bytes=sizeof(*o),.mask=IFS_FS_META_GET_MODE,
       .mode=e.node.mode,.owner=e.node.uid,.group=e.node.gid};return IFS_FS_OK;
}
#define WRAP(name,args,call) static int name args{if(!enter())return IFS_FS_BUSY;return leave(call);}
WRAP(probe,(const IfsFsHostRuntimeV2 *h,uint32_t *v),probe_impl(h,v))
WRAP(mount,(const IfsFsHostRuntimeV2 *h,void **p,IfsFsVolumeInfoV2 *o),mount_impl(h,p,o))
WRAP(unmount,(void *p),unmount_impl(p))
WRAP(stat_path,(void *p,const char *s,IfsFsEntryV2 *o),stat_impl(p,s,o))
WRAP(list_path,(void *p,const char *s,uint32_t i,IfsFsEntryV2 *o),list_impl(p,s,i,o))
WRAP(read_path,(void *p,const char *s,uint64_t a,void *b,uint32_t n),read_impl(p,s,a,b,n))
WRAP(readlink_path,(void *p,const char *s,char *b,uint32_t c,uint32_t *n),readlink_impl(p,s,b,c,n))
WRAP(query,(void *p,IfsFsVolumeInfoV2 *o),query_impl(p,o))
WRAP(metadata,(void *p,const char *s,IfsFsMetadataV2 *o),metadata_impl(p,s,o))
static const IfsFsCoreOpsV2 core={
 .header={2,sizeof(core),IFS_FS_GROUP_CORE,0,IFS_FS_CORE_PROBE|IFS_FS_CORE_MOUNT|IFS_FS_CORE_STAT|IFS_FS_CORE_LIST|IFS_FS_CORE_READ},
 .probe=probe,.mount=mount,.unmount=unmount,.stat=stat_path,.directory_entry=list_path,.read=read_path};
static const IfsFsVolumeOpsV2 volume={.header={2,sizeof(volume),IFS_FS_GROUP_VOLUME,0,IFS_FS_VOLUME_QUERY},.query=query};
static const IfsFsMetadataOpsV2 meta={.header={2,sizeof(meta),IFS_FS_GROUP_METADATA,0,IFS_FS_META_GET_MODE|IFS_FS_META_READLINK},.get=metadata,.readlink=readlink_path};
static const IfsFsGroupHeaderV2 *group(uint32_t id,uint32_t lo,uint32_t hi)
{if(lo>2||hi<2)return NULL;if(id==IFS_FS_GROUP_CORE)return &core.header;if(id==IFS_FS_GROUP_VOLUME)return &volume.header;if(id==IFS_FS_GROUP_METADATA)return &meta.header;return NULL;}
static const IfsFsModuleApiV2 api={.abi_version=2,.structure_bytes=sizeof(api),.format_id="qnx6",
#ifdef IFS_BOOT_FIRMWARE
 .runtime_profiles=IFS_FS_PROFILE_FREESTANDING|IFS_FS_PROFILE_FIRMWARE,
#else
 .runtime_profiles=IFS_FS_PROFILE_HOSTED,
#endif
 .group_mask=IFS_FS_GROUP_BIT(IFS_FS_GROUP_CORE)|IFS_FS_GROUP_BIT(IFS_FS_GROUP_VOLUME)|IFS_FS_GROUP_BIT(IFS_FS_GROUP_METADATA),.query_group=group};
__attribute__((visibility("default")))const IfsFsModuleApiV2 *infiltrator_filesystem_api_v2(void){return &api;}
