/* The current and backup pointers refer only to fully checked immutable generations. */
#include "tone_store.h"
#include "app_ringtone_wav.h"
#include "app_ringtone_zip.h"
#include "zld_update.h"
#include "mbedtls/sha256.h"
#include <errno.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define ACTIVE TONE_ROOT "/active.txt"
#define DEFAULT_TONE "tubular-chime.wav"
static bool hex_sha(const char *s) {
    return s && strlen(s)==64 && strspn(s,"abcdef0123456789")==64;
}
static void as_hex(const uint8_t raw[32], char sha[65]) {
    const char hex[]="0123456789abcdef";
    for (unsigned i=0;i<32;i++) {sha[2*i]=hex[raw[i]>>4]; sha[2*i+1]=hex[raw[i]&15];}
    sha[64]=0;
}
static bool hash_file(const char *path, char sha[65]) {
    FILE *f=fopen(path,"rb");
    if (!f) return false;
    uint8_t buf[2048], digest[32];
    mbedtls_sha256_context hash;
    mbedtls_sha256_init(&hash);
    bool ok=!mbedtls_sha256_starts(&hash,0);
    size_t n;
    while(ok && (n=fread(buf,1,sizeof(buf),f))) ok=!mbedtls_sha256_update(&hash,buf,n);
    ok=ok && !ferror(f) && !mbedtls_sha256_finish(&hash,digest);
    mbedtls_sha256_free(&hash); fclose(f);
    if(ok) as_hex(digest,sha);
    return ok;
}
static bool mkdir_ok(const char *dir) {return !mkdir(dir,0775) || errno==EEXIST;}
bool tone_prepare(void) {return zld_card_verify() && mkdir_ok(TONE_ROOT);}
enum pointer_state { POINTER_MISSING, POINTER_VALID, POINTER_ERROR };
static enum pointer_state read_pointer(const char *p,char dir[65]) {
    FILE *f=fopen(p,"rb");
    if(!f) return errno==ENOENT?POINTER_MISSING:POINTER_ERROR;
    size_t n=fread(dir,1,64,f);
    bool ok=n==64 && fgetc(f)==EOF && !ferror(f);
    if(fclose(f)) ok=false;
    dir[64]=0;
    return ok && hex_sha(dir)?POINTER_VALID:POINTER_ERROR;
}
static bool pointer_at(const char *p,char dir[65]) {
    return read_pointer(p,dir)==POINTER_VALID;
}
static bool put(const char *path, const char *s) {
    FILE *f=fopen(path,"wb"); if(!f) return false;
    size_t n=strlen(s);
    bool ok=fwrite(s,1,n,f)==n && !fflush(f) && !fsync(fileno(f));
    return fclose(f)==0 && ok;
}
static cJSON *read_json(const char *dir,const char *file,size_t max) {
    char path[192]; snprintf(path,sizeof(path),TONE_ROOT "/%s/%s",dir,file);
    struct stat st;
    if(stat(path,&st)) {
        if(strcmp(file,"index.json") || errno!=ENOENT) return NULL;
        snprintf(path,sizeof(path),TONE_ROOT "/%s/index.json.bak",dir);
        if(stat(path,&st)) return NULL;
    }
    if(st.st_size<=0 || (uint64_t)st.st_size>max) return NULL;
    FILE *f=fopen(path,"rb"); if(!f) return NULL;
    size_t n=st.st_size;
    char *s=malloc(n+1);
    cJSON *doc=NULL;
    if(s && fread(s,1,n,f)==n && !memchr(s,0,n)) {
        s[n]=0; doc=cJSON_ParseWithLengthOpts(s,n+1,NULL,true);
    }
    free(s); fclose(f); return doc;
}
static const char *field(const cJSON *o,const char *k) {
    return cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(o,k));
}
static bool generation_at(const char *pointer,char dir[65]) {
    if(!zld_card_online() || !pointer_at(pointer,dir)) return false;
    cJSON *doc=read_json(dir,"index.json",16384);
    cJSON *items=cJSON_GetObjectItemCaseSensitive(doc,"items");
    int count=cJSON_GetArraySize(items);
    const char *tag=field(doc,"version");
    bool ok=hex_sha(field(doc,"archive")) && tag && *tag && strlen(tag)<=31 &&
            cJSON_IsArray(items) && count>0 && count<=32;
    for(int i=0;ok && i<count;i++) {
        const cJSON *row=cJSON_GetArrayItem(items,i);
        ok=app_ringtone_name(field(row,"name")) && field(row,"label") && hex_sha(field(row,"sha256"));
    }
    cJSON_Delete(doc);return ok;
}
static bool current(char dir[65]) {
    return generation_at(ACTIVE,dir) || generation_at(ACTIVE ".bak",dir);
}
/* Only generation directories and their regular flat entries are managed by
 * this installer. Preserve both live pointers while disposing of older versions. */
static void clean_dir(const char *dir) {
    if(!hex_sha(dir)) return;
    char base[160], path[320];
    snprintf(base,sizeof(base),TONE_ROOT "/%s",dir);
    DIR *d=opendir(base); if(!d) return;
    struct dirent *entry;
    while((entry=readdir(d))) {
        const char *name=entry->d_name;
        if(!app_ringtone_name(name) && strcmp(name,"ringtone.json") && strcmp(name,"index.json") &&
           strcmp(name,"index.json.bak") && strcmp(name,"index.json.new")) continue;
        snprintf(path,sizeof(path),TONE_ROOT "/%s/%.64s",dir,name);unlink(path);
    }
    closedir(d);rmdir(base);
}
static void prune(void) {
    char active[65]="",backup[65]="",obsolete[65];
    if(!zld_card_verify() || read_pointer(ACTIVE,active)==POINTER_ERROR ||
       read_pointer(ACTIVE ".bak",backup)==POINTER_ERROR) return;
    /* Reopen the directory after a removal to avoid invalid FAT iterators. */
    for(unsigned i=0;i<64;i++) {
        DIR *d=opendir(TONE_ROOT); if(!d) return;
        struct dirent *e; obsolete[0]=0;
        while((e=readdir(d))) {
            if(hex_sha(e->d_name) && strcmp(e->d_name,active) && strcmp(e->d_name,backup)) {
                memcpy(obsolete,e->d_name,sizeof(obsolete));break;
            }
        }
        closedir(d);
        if(!*obsolete || !zld_card_verify()) break;
        clean_dir(obsolete);
        char path[160];snprintf(path,sizeof(path),TONE_ROOT "/%s",obsolete);
        if(!access(path,F_OK)) break;
    }
}
static bool wav_path(const char *dir,const char *name,char *path,size_t cap) {
    int n=snprintf(path,cap,TONE_ROOT "/%s/%s",dir,name);
    return n>0 && (size_t)n<cap && !access(path,R_OK);
}
static cJSON *metadata(const char *dir,const char *tag,const cJSON *report) {
    cJSON *source=read_json(dir,"ringtone.json",65536);
    cJSON *rows=cJSON_GetObjectItemCaseSensitive(source,"sounds");
    const cJSON *schema=cJSON_GetObjectItemCaseSensitive(source,"schema");
    const char *kind=field(source,"kind");
    int count=cJSON_GetArraySize(rows);
    cJSON *out=NULL,*items=NULL;
    if(!kind || strcmp(kind,"intercom-ringtones") || !cJSON_IsNumber(schema) || schema->valuedouble!=1 ||
       !cJSON_IsArray(rows) || count<1 || count>32 ||
       count!=cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(report,"installed"))) goto done;
    out=cJSON_CreateObject();
    if(!out || !cJSON_AddStringToObject(out,"version",tag) || !(items=cJSON_AddArrayToObject(out,"items"))) goto bad;
    for(int i=0;i<count;i++) {
        const cJSON *row=cJSON_GetArrayItem(rows,i);
        const char *name=field(row,"file"),*label=field(row,"title");
        if(!app_ringtone_name(name) || !label || !*label || strlen(label)>160) goto bad;
        for(int j=0;j<i;j++) if(!strcmp(name,field(cJSON_GetArrayItem(rows,j),"file"))) goto bad;
        char path[192],sha[65]; snprintf(path,sizeof(path),TONE_ROOT "/%s/%s",dir,name);
        cJSON *item=cJSON_CreateObject();
        if(!item || !cJSON_AddItemToArray(items,item)) {cJSON_Delete(item); goto bad;}
        if(!hash_file(path,sha) || !cJSON_AddStringToObject(item,"name",name) ||
           !cJSON_AddStringToObject(item,"label",label) || !cJSON_AddStringToObject(item,"sha256",sha)) goto bad;
    }
    goto done;
bad:
    cJSON_Delete(out); out=NULL;
done:
    cJSON_Delete(source); return out;
}
bool tone_current_verified(const char *digest) {
    char dir[65]; if(!current(dir)) return false;
    cJSON *doc=read_json(dir,"index.json",16384);
    cJSON *items=cJSON_GetObjectItemCaseSensitive(doc,"items");
    const char *sha=field(doc,"archive");
    int n=cJSON_GetArraySize(items);
    bool ok=hex_sha(sha) && !strcmp(sha,digest) && n>=1 && n<=32;
    for(int i=0;ok && i<n;i++) {
        const cJSON *item=cJSON_GetArrayItem(items,i);
        const char *name=field(item,"name"),*sum=field(item,"sha256");
        char path[192],actual[65];
        if(!app_ringtone_name(name) || !hex_sha(sum)) {ok=false;break;}
        snprintf(path,sizeof(path),TONE_ROOT "/%s/%s",dir,name);
        ok=hash_file(path,actual) && !strcmp(actual,sum);
    }
    cJSON_Delete(doc); return ok;
}
bool tone_set_version(const char *tag,const char *digest) {
    char dir[65],base[192],tmp[200],bak[200];
    if(!tag || !*tag || strlen(tag)>31 || !hex_sha(digest) || !current(dir)) return false;
    cJSON *doc=read_json(dir,"index.json",16384);
    const char *old=field(doc,"version"),*sha=field(doc,"archive");
    bool ok=old && hex_sha(sha) && !strcmp(sha,digest);
    if(ok && !strcmp(old,tag)) { cJSON_Delete(doc);return true; }
    cJSON *changed=ok?cJSON_GetObjectItemCaseSensitive(doc,"version"):NULL;
    ok=changed && cJSON_SetValuestring(changed,tag);
    char *body=ok?cJSON_PrintUnformatted(doc):NULL;
    snprintf(base,sizeof(base),TONE_ROOT "/%s/index.json",dir);
    snprintf(tmp,sizeof(tmp),"%s.new",base);snprintf(bak,sizeof(bak),"%s.bak",base);
    ok=body && put(tmp,body) && zld_card_verify();
    if(ok && !access(base,F_OK)) {
        if(unlink(bak) && errno!=ENOENT) ok=false;
        if(ok && rename(base,bak)) ok=false;
    }
    if(ok) ok=!rename(tmp,base);
    cJSON_free(body);cJSON_Delete(doc);return ok;
}
bool tone_resolve(const char *name,char *path,size_t cap) {
    if(!app_ringtone_name(name) || !zld_card_online()) return false;
    char dir[65];
    if(current(dir)) {
        if(wav_path(dir,name,path,cap)) return true;
        /* A later release may intentionally remove a tone. Use the backup
         * only when this name genuinely belongs to the current index. */
        cJSON *doc=read_json(dir,"index.json",16384);
        const cJSON *rows=cJSON_GetObjectItemCaseSensitive(doc,"items"),*row;
        bool missing=false;
        cJSON_ArrayForEach(row,rows) {
            const char *id=field(row,"name");
            if(id && !strcmp(id,name)) {missing=true;break;}
        }
        cJSON_Delete(doc);
        if(missing && generation_at(ACTIVE ".bak",dir) && wav_path(dir,name,path,cap)) return true;
    }
    if(strcmp(name,DEFAULT_TONE)) return false;
    return zld_asset_path("ringtones/" DEFAULT_TONE,path,cap) && !access(path,R_OK);
}
cJSON *tone_list(char version[32]) {
    char dir[65], def_path[192]; version[0]=0;
    cJSON *doc=current(dir)?read_json(dir,"index.json",16384):NULL;
    const char *tag=field(doc,"version"); if(tag) snprintf(version,32,"%.31s",tag);
    const cJSON *rows=cJSON_GetObjectItemCaseSensitive(doc,"items"),*row;
    cJSON *out=cJSON_CreateArray(); bool has_default=false;
    cJSON_ArrayForEach(row,rows) {
        const char *name=field(row,"name"),*label=field(row,"label");
        if(!app_ringtone_name(name) || !label) continue;
        char path[192]; snprintf(path,sizeof(path),TONE_ROOT "/%s/%s",dir,name);
        if(access(path,R_OK) && !tone_resolve(name,path,sizeof(path))) continue;
        cJSON *item=cJSON_CreateObject();
        if(!item || !cJSON_AddItemToArray(out,item)) {cJSON_Delete(item);break;}
        cJSON_AddStringToObject(item,"name",name); cJSON_AddStringToObject(item,"label",label);
        if(!strcmp(name,DEFAULT_TONE)) has_default=true;
    }
    if(!has_default && tone_resolve(DEFAULT_TONE,def_path,sizeof(def_path))) {
        cJSON *item=cJSON_CreateObject();
        if(item && cJSON_AddItemToArray(out,item)) {
            cJSON_AddStringToObject(item,"name",DEFAULT_TONE);cJSON_AddStringToObject(item,"label","Csőharang");
        } else cJSON_Delete(item);
    }
    cJSON_Delete(doc);return out;
}
bool tone_install(const char *archive,const char *tag,const char *digest,char error[160]) {
    char source_sha[65],old[65],dir[65],path[192],folder[160];
    if(!hex_sha(digest) || !tag || !*tag || strlen(tag)>31 || !tone_prepare() ||
       !hash_file(archive,source_sha) || strcmp(source_sha,digest)) return false;
    prune();
    /* A repaired copy is extracted separately, even when reinstalling the same ZIP. */
    char basis[70]; snprintf(basis,sizeof(basis),"%s:%d",digest,0);
    uint8_t raw[32];
    if(mbedtls_sha256((const uint8_t*)basis,strlen(basis),raw,0)) return false;
    as_hex(raw,dir);
    enum pointer_state active_state=read_pointer(ACTIVE,old);
    if(active_state==POINTER_ERROR) return false;
    char backup[65]="";
    enum pointer_state backup_state=read_pointer(ACTIVE ".bak",backup);
    if(backup_state==POINTER_ERROR) return false;
    /* Protect the usable old pointer even if its index is temporarily unreadable. */
    const char *protected_dir=active_state==POINTER_VALID?old:backup;
    if(!strcmp(dir,protected_dir)) {
        basis[65]='1';
        if(mbedtls_sha256((const uint8_t*)basis,strlen(basis),raw,0)) return false;
        as_hex(raw,dir);
    }
    if(backup_state==POINTER_VALID && !strcmp(backup,dir) &&
       !generation_at(ACTIVE,old) && generation_at(ACTIVE ".bak",backup)) return false;
    snprintf(folder,sizeof(folder),TONE_ROOT "/%s",dir);
    if(!mkdir_ok(folder)) return false;
    cJSON *report=cJSON_CreateObject(),*doc=NULL;
    bool ok=report && cJSON_AddArrayToObject(report,"failed") &&
        app_ringtone_unpack(archive,folder,report,error) && (doc=metadata(dir,tag,report)) &&
        cJSON_AddStringToObject(doc,"archive",digest);
    char *body=ok?cJSON_PrintUnformatted(doc):NULL;
    snprintf(path,sizeof(path),TONE_ROOT "/%s/index.json",dir);
    ok=body && put(path,body) && zld_card_verify() && put(ACTIVE ".tmp",dir);
    if(ok) {
        enum pointer_state previous=read_pointer(ACTIVE,old);
        if(previous==POINTER_ERROR) ok=false;
        else if(previous==POINTER_VALID) {
            if(generation_at(ACTIVE,old)) {
                if(unlink(ACTIVE ".bak") && errno!=ENOENT) ok=false;
                if(ok && rename(ACTIVE,ACTIVE ".bak")) ok=false;
            } else if(generation_at(ACTIVE ".bak",old)) {
                /* Preserve a known readable backup during recovery. */
                ok=!unlink(ACTIVE);
            } else ok=false;
        }
    }
    if(ok) ok=rename(ACTIVE ".tmp",ACTIVE)==0;
    cJSON_free(body);cJSON_Delete(doc);cJSON_Delete(report);
    prune();
    return ok;
}

bool tone_install_local(const char *archive,char error[160]) {
    char digest[65];
    if(!hash_file(archive,digest)) return false;
    /* An upload identical to an installed official release retains its verified origin. */
    return tone_current_verified(digest) || tone_install(archive,"Helyi csomag",digest,error);
}
