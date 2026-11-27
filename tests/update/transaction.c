#include "zld_update.c"

static bool authorize(httpd_req_t *r, bool write) { return true; }
static void digest(const void *data, size_t n, char hex[65]) {
    uint8_t raw[32]; SHA256(data,n,raw); hex_digest(raw,hex);
}
static httpd_req_t request(const void *data,size_t n,const char *gen) {
    httpd_req_t r = {.body=data,.content_len=n,.available=n,.target="31416-rev1",.generation=gen};
    strcpy(r.status,"200"); return r;
}
static void response(esp_err_t (*handler)(httpd_req_t*),httpd_req_t *req,const char *status) {
    assert(handler(req)==ESP_OK);
    if (strncmp(req->status,status,3)) fprintf(stderr,"%s / %s: %s\n",req->status,status,req->response);
    assert(!strncmp(req->status,status,3));
}

static void remove_session(void) {
    assert(rename(ROOT "/session.bin", ROOT "/session.old") == 0);
}
static esp_err_t simulate_busy_write(httpd_req_t *req) {
    assert(!zld_card_needs_restart() && !zld_card_needs_restart());
    return httpd_resp_sendstr(req,"OK");
}

/* The sibling Python test lays out the actual release blobs and manifest.
   Only catalog.json is absent. This exercises full-sized digest, parser and
   HTTP missing-index paths without making copies of the checked-in media. */
static void check_shipped_catalog(unsigned missing) {
    assert(missing < ACTIVE_COUNT && !strcmp(active_entries[missing].name, "catalog.json"));
    assert(zld_card_session_start());
    FILE *file = fopen("release.idx", "rb");
    assert(file && !fseek(file, 0, SEEK_END));
    long size = ftell(file);
    assert(size > 0 && size <= MAX_INDEX_SIZE && !fseek(file, 0, SEEK_SET));
    char *manifest = malloc(size);
    assert(manifest && fread(manifest, 1, size, file) == (size_t)size);
    assert(!fclose(file));
    httpd_req_t req = request(manifest, size, ZLD_GENERATION);
    response(prepare, &req, "200");
    char expected[16];
    snprintf(expected, sizeof(expected), "%u\n", missing);
    assert(!strcmp(req.response, expected));
    free(manifest);
    printf("Verified the device index and missing-file response for all %zu release files.\n", ACTIVE_COUNT);
}

int main(int argc, char **argv) {
    allow=authorize;
    if (argc == 2) {
        check_shipped_catalog((unsigned)strtoul(argv[1], NULL, 10));
        return 0;
    }
    assert(argc == 1);
    assert(mkdir("card",0777)==0);
    assert(zld_card_session_start() && zld_card_verify() && zld_card_online());
    char ah[65], bh[65], first[256], second[256], gen2[65], image_sha[65], path[PATH_CAP];
    digest("A",1,ah); digest("BBBB",4,bh);
    snprintf(first,sizeof(first),"%s 1 icons/a.png\n",ah);
    snprintf(second,sizeof(second),"%s 1 icons/a.png\n%s 4 icons/b.png\n",ah,bh);
    digest(second,strlen(second),gen2);
    httpd_req_t r=request(first,strlen(first),ZLD_GENERATION);
    r.target="31570-rev1"; response(prepare,&r,"400");
    r=request(first,strlen(first),ZLD_GENERATION); response(prepare,&r,"200");
    assert(!strcmp(r.response,"0\n"));
    r=request("X",1,ZLD_GENERATION); r.index="0"; response(asset,&r,"400");
    assert(!zld_assets_check());
    r=request("A",1,ZLD_GENERATION); r.index="0"; response(asset,&r,"200");
    r=request("",0,ZLD_GENERATION); response(seed,&r,"200"); assert(zld_assets_ready());
    assert(zld_asset_path("icons/a.png",path,sizeof(path)) && access(path,R_OK)==0);
    assert(unlink(path)==0);
    test_bad_remote=true;
    r=request("",0,ZLD_GENERATION); response(fetch_card,&r,"202");
    assert(test_download_count==2 && !zld_assets_ready() && atomic_load(&cloud_state)==CLOUD_ERROR);
    test_bad_remote=false;
    r=request("",0,ZLD_GENERATION); response(fetch_card,&r,"202");
    assert(test_download_count==3 && zld_assets_ready() && atomic_load(&cloud_state)==CLOUD_OK);
    r=request("",0,ZLD_GENERATION); response(fetch_card,&r,"202");
    assert(test_download_count==3);
    r=request(second,strlen(second),gen2); response(prepare,&r,"200"); assert(!strcmp(r.response,"1\n"));
    r=request("BBBB",4,gen2); r.available=2; r.index="1"; response(asset,&r,"503");
    r=request("",0,gen2); response(seed,&r,"409"); assert(zld_assets_ready());
    memset(staged,0,sizeof(staged)); /* process restart */
    assert(zld_assets_check());
    r=request(second,strlen(second),gen2); response(prepare,&r,"200"); assert(!strcmp(r.response,"1\n"));
    unsigned char img[4096]={0xE9};
    snprintf((char*)img+2043,512,MARKER_PREFIX "%s/END",gen2); /* split marker across two reads */
    digest(img,sizeof(img),image_sha);
    r=request(img,sizeof(img),gen2); r.sha=image_sha; response(firmware,&r,"409"); assert(!test_switched);
    r=request("BBBB",4,gen2); r.index="1"; response(asset,&r,"200");
    r=request(second,strlen(second),gen2); response(prepare,&r,"200"); assert(!*r.response);
    FILE *f=fopen(path,"wb"); assert(f); fputs("Z",f); fclose(f); /* same-size SD corruption */
    r=request(img,sizeof(img),gen2); r.sha=image_sha; response(firmware,&r,"409"); assert(!test_switched);
    r=request(second,strlen(second),gen2); response(prepare,&r,"200"); assert(!strcmp(r.response,"0\n"));
    r=request("A",1,gen2); r.index="0"; response(asset,&r,"200"); assert(zld_assets_check());
    r=request(img,sizeof(img),gen2); r.sha=bh; response(firmware,&r,"400"); assert(!test_switched);
    img[2043]='Y'; char wrong_sha[65]; digest(img,sizeof(img),wrong_sha);
    r=request(img,sizeof(img),gen2); r.sha=wrong_sha; response(firmware,&r,"400"); assert(!test_switched);
    img[2043]='Z';
    test_after_write=remove_session;
    r=request(img,sizeof(img),gen2); r.sha=image_sha; response(firmware,&r,"409"); assert(!test_switched);
    test_after_write=NULL;
    assert(rename(ROOT "/session.old", ROOT "/session.bin")==0);
    assert(!zld_card_needs_restart() && zld_card_online());
    r=request(img,sizeof(img),gen2); r.sha=image_sha; response(firmware,&r,"200"); assert(test_switched);
    /* A substituted card invalidates authorization even if it contains the
       same catalog, and an active request defers recovery until fully unwound. */
    assert(rename(ROOT "/session.bin", ROOT "/session.old")==0);
    assert(!zld_card_verify() && !zld_assets_ready() && !zld_card_online());
    assert(!zld_card_needs_restart());
    assert(rename(ROOT "/session.old", ROOT "/session.bin")==0);
    assert(!zld_card_needs_restart() && zld_card_online() && zld_assets_ready());
    uint8_t other[512]={0};
    FILE *swapped=fopen(ROOT "/session.bin","wb"); assert(swapped);
    assert(fwrite(other,1,sizeof(other),swapped)==sizeof(other)); fclose(swapped);
    const httpd_uri_t route={.handler=simulate_busy_write};
    r=request("",0,gen2); r.user_ctx=(void *)&route;
    response(guarded_write,&r,"200"); assert(!zld_card_online());
    assert(zld_card_needs_restart() && !hold_update() && !zld_card_verify());
    puts("Verified delta, interrupted transfer, resume, corruption, board binding and activation.");
}
