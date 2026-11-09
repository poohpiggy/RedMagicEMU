#define _GNU_SOURCE
#include "../vintf_fragment.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *token = "0123456789abcdef0123456789abcdef";
static const char *stage = ".aemu-radio-slot1-0123456789abcdef0123456789abcdef.data";
static const char *meta = ".aemu-radio-slot1-0123456789abcdef0123456789abcdef.data.meta";
/* Frozen old bytes, independent of the current generator. */
static const char legacy_xml[] =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
    "<!-- RedMagicEMU isolated radio experiment 0123456789abcdef0123456789abcdef -->\n"
    "<manifest version=\"1.0\" type=\"device\">\n"
    " <hal format=\"aidl\"><name>android.hardware.radio.config</name><version>4</version><fqname>IRadioConfig/default</fqname></hal>\n"
    " <hal format=\"aidl\"><name>android.hardware.radio.modem</name><version>4</version><fqname>IRadioModem/slot1</fqname></hal>\n"
    " <hal format=\"aidl\"><name>android.hardware.radio.sim</name><version>4</version><fqname>IRadioSim/slot1</fqname></hal>\n"
    " <hal format=\"aidl\"><name>android.hardware.radio.network</name><version>4</version><fqname>IRadioNetwork/slot1</fqname></hal>\n"
    " <hal format=\"aidl\"><name>android.hardware.radio.messaging</name><version>4</version><fqname>IRadioMessaging/slot1</fqname></hal>\n"
    " <hal format=\"aidl\"><name>android.hardware.radio.voice</name><version>4</version><fqname>IRadioVoice/slot1</fqname></hal>\n"
    "</manifest>\n";
static int stopped(void *context) { return *(int *)context; }
static int stop_then_start(void *context) { int *calls=(int *)context; return (*calls)++==0; }
static int exists(int d, const char *name) { struct stat s; return fstatat(d,name,&s,AT_SYMLINK_NOFOLLOW)==0; }
static void write_file(int d, const char *name, const char *text) {
    int f=openat(d,name,O_CREAT|O_EXCL|O_WRONLY|O_NOFOLLOW,0600); assert(f>=0);
    assert(write(f,text,strlen(text))==(ssize_t)strlen(text)); assert(close(f)==0);
}
static void create_legacy(int d) {
    write_file(d,stage,legacy_xml);
    struct stat st; assert(fstatat(d,stage,&st,AT_SYMLINK_NOFOLLOW)==0);
    char record[160];
    int n=snprintf(record,sizeof record,"RME1 %s %" PRIuMAX " %" PRIuMAX "\n",token,(uintmax_t)st.st_dev,(uintmax_t)st.st_ino);
    assert(n>0 && (size_t)n<sizeof record);
    write_file(d,meta,record);
    assert(symlinkat(stage,d,RME_RADIO_VINTF_NAME)==0);
}

int main(void) {
#ifdef __ANDROID__
    char path[]="/data/local/tmp/rme-radio-vintf-test-XXXXXX"; assert(mkdtemp(path));
#else
    char path[4096]; const char *tmp=getenv("TMPDIR");
    if (!tmp || !*tmp) tmp="/tmp";
    int path_len=snprintf(path,sizeof path,"%s/rme-radio-vintf-test-XXXXXX",tmp);
    assert(path_len>0 && (size_t)path_len<sizeof path && mkdtemp(path));
#endif
    int d=open(path,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC); assert(d>=0);
    int ok=0;
    assert(rme_radio_vintf_prepare(d,token,stopped,&ok)<0);
    assert(!exists(d,RME_RADIO_VINTF_NAME));
    ok=1;
    assert(rme_radio_vintf_prepare(d,"../../unsafe",stopped,&ok)<0);
    write_file(d,RME_RADIO_VINTF_NAME,"existing");
    assert(rme_radio_vintf_prepare(d,token,stopped,&ok)<0);
    int f=openat(d,RME_RADIO_VINTF_NAME,O_RDONLY|O_NOFOLLOW); char b[16]={0};
    assert(f>=0 && read(f,b,8)==8 && !memcmp(b,"existing",8)); close(f);
    assert(unlinkat(d,RME_RADIO_VINTF_NAME,0)==0);
    assert(symlinkat("../../outside",d,RME_RADIO_VINTF_NAME)==0);
    assert(rme_radio_vintf_prepare(d,token,stopped,&ok)<0);
    char linkbuf[64]={0}; assert(readlinkat(d,RME_RADIO_VINTF_NAME,linkbuf,sizeof linkbuf)==13);
    assert(!memcmp(linkbuf,"../../outside",13));
    assert(unlinkat(d,RME_RADIO_VINTF_NAME,0)==0);
    if (rme_radio_vintf_prepare(d,token,stopped,&ok)) { perror("prepare"); assert(0); }
    assert(rme_radio_vintf_verify(d,token)==0);
    f=openat(d,RME_RADIO_VINTF_NAME,O_RDONLY); assert(f>=0);
    char xml[4096]={0}; ssize_t n=read(f,xml,sizeof(xml)-1); assert(n>0); close(f);
    const char *expected[] = {
        "<name>android.hardware.radio.config</name><version>4</version><fqname>IRadioConfig/default</fqname>",
        "<name>android.hardware.radio.modem</name><version>4</version><fqname>IRadioModem/slot1</fqname>",
        "<name>android.hardware.radio.sim</name><version>4</version><fqname>IRadioSim/slot1</fqname>",
        "<name>android.hardware.radio.network</name><version>4</version><fqname>IRadioNetwork/slot1</fqname>",
        "<name>android.hardware.radio.data</name><version>4</version><fqname>IRadioData/slot1</fqname>",
        "<name>android.hardware.radio.messaging</name><version>4</version><fqname>IRadioMessaging/slot1</fqname>",
        "<name>android.hardware.radio.voice</name><version>4</version><fqname>IRadioVoice/slot1</fqname>"
    };
    for (size_t i=0;i<sizeof expected/sizeof expected[0];i++) {
        const char *first=strstr(xml,expected[i]);
        assert(first && !strstr(first+1,expected[i]));
    }
    int hal_count=0; for (const char *p=xml;(p=strstr(p,"<hal "));p+=5) hal_count++;
    assert(hal_count==7);
    assert(!strstr(xml,"slot2"));
    ok=0; assert(rme_radio_vintf_cleanup(d,token,stopped,&ok)<0); assert(exists(d,RME_RADIO_VINTF_NAME)); ok=1;
    assert(rme_radio_vintf_cleanup(d,"ffffffffffffffffffffffffffffffff",stopped,&ok)<0);
    assert(exists(d,RME_RADIO_VINTF_NAME));
    /* Replacement of the staged file with identical bytes is still refused. */
    assert(renameat(d,stage,d,"original-stage")==0);
    int replacement=openat(d,stage,O_CREAT|O_EXCL|O_WRONLY|O_NOFOLLOW,0600); assert(replacement>=0);
    assert(write(replacement,xml,(size_t)n)==n); assert(close(replacement)==0);
    assert(rme_radio_vintf_cleanup(d,token,stopped,&ok)<0);
    assert(exists(d,RME_RADIO_VINTF_NAME) && exists(d,"original-stage"));
    assert(unlinkat(d,stage,0)==0); assert(renameat(d,"original-stage",d,stage)==0);
    assert(rme_radio_vintf_verify(d,token)==0);
    /* An independently replaced file must not be deleted. */
    assert(unlinkat(d,RME_RADIO_VINTF_NAME,0)==0); write_file(d,RME_RADIO_VINTF_NAME,"replacement");
    assert(rme_radio_vintf_cleanup(d,token,stopped,&ok)<0);
    f=openat(d,RME_RADIO_VINTF_NAME,O_RDONLY|O_NOFOLLOW); memset(b,0,sizeof b);
    assert(f>=0 && read(f,b,11)==11 && !memcmp(b,"replacement",11)); close(f);
    assert(unlinkat(d,RME_RADIO_VINTF_NAME,0)==0);
    /* Target gone while the exact owned staged file remains: interrupted publication recovery. */
    assert(rme_radio_vintf_cleanup(d,token,stopped,&ok)==0);
    assert(!exists(d,RME_RADIO_VINTF_NAME));
    assert(rme_radio_vintf_prepare(d,token,stopped,&ok)==0);
    assert(rme_radio_vintf_cleanup(d,token,stopped,&ok)==0);
    /* Recovery if interruption happened after target and stage were unlinked. */
    assert(rme_radio_vintf_prepare(d,token,stopped,&ok)==0);
    assert(unlinkat(d,RME_RADIO_VINTF_NAME,0)==0);
    assert(unlinkat(d,stage,0)==0);
    assert(rme_radio_vintf_cleanup(d,token,stopped,&ok)==0);
    /* If the guard stops holding, preserve data for later stopped recovery. */
    int calls=0;
    assert(rme_radio_vintf_prepare(d,token,stop_then_start,&calls)<0);
    assert(!exists(d,RME_RADIO_VINTF_NAME));
    assert(exists(d,stage));
    assert(rme_radio_vintf_cleanup(d,token,stopped,&ok)==0);
    /* Old declarations are recoverable but never count as current complete declarations. */
    create_legacy(d);
    assert(rme_radio_vintf_verify(d,token)<0);
    ok=0; assert(rme_radio_vintf_cleanup(d,token,stopped,&ok)<0); ok=1;
    calls=0; assert(rme_radio_vintf_cleanup(d,token,stop_then_start,&calls)<0);
    assert(exists(d,stage) && exists(d,meta) && exists(d,RME_RADIO_VINTF_NAME));
    assert(rme_radio_vintf_cleanup(d,"ffffffffffffffffffffffffffffffff",stopped,&ok)<0);
    assert(linkat(d,stage,d,"legacy-hardlink",0)==0);
    assert(rme_radio_vintf_cleanup(d,token,stopped,&ok)<0);
    assert(exists(d,stage) && exists(d,meta) && exists(d,"legacy-hardlink"));
    assert(unlinkat(d,"legacy-hardlink",0)==0);
    int old=openat(d,stage,O_WRONLY|O_NOFOLLOW); assert(old>=0);
    assert(pwrite(old,"!",1,0)==1 && close(old)==0);
    assert(rme_radio_vintf_cleanup(d,token,stopped,&ok)<0);
    assert(exists(d,stage) && exists(d,meta) && exists(d,RME_RADIO_VINTF_NAME));
    old=openat(d,stage,O_WRONLY|O_NOFOLLOW); assert(old>=0);
    assert(pwrite(old,legacy_xml,1,0)==1 && close(old)==0);
    int journal=openat(d,meta,O_WRONLY|O_NOFOLLOW); assert(journal>=0);
    assert(pwrite(journal,"f",1,5)==1 && close(journal)==0);
    assert(rme_radio_vintf_cleanup(d,token,stopped,&ok)<0);
    assert(exists(d,stage) && exists(d,meta) && exists(d,RME_RADIO_VINTF_NAME));
    journal=openat(d,meta,O_WRONLY|O_NOFOLLOW); assert(journal>=0);
    assert(pwrite(journal,token,1,5)==1 && close(journal)==0);
    assert(renameat(d,stage,d,"legacy-original")==0);
    write_file(d,stage,legacy_xml);
    assert(rme_radio_vintf_cleanup(d,token,stopped,&ok)<0);
    assert(exists(d,stage) && exists(d,meta) && exists(d,"legacy-original"));
    assert(unlinkat(d,stage,0)==0 && renameat(d,"legacy-original",d,stage)==0);
    assert(unlinkat(d,RME_RADIO_VINTF_NAME,0)==0);
    assert(symlinkat("other",d,RME_RADIO_VINTF_NAME)==0);
    assert(rme_radio_vintf_cleanup(d,token,stopped,&ok)<0);
    assert(exists(d,stage) && exists(d,meta) && exists(d,RME_RADIO_VINTF_NAME));
    assert(unlinkat(d,RME_RADIO_VINTF_NAME,0)==0);
    assert(symlinkat(stage,d,RME_RADIO_VINTF_NAME)==0);
    assert(rme_radio_vintf_cleanup(d,token,stopped,&ok)==0);
    assert(!exists(d,stage) && !exists(d,meta) && !exists(d,RME_RADIO_VINTF_NAME));
    create_legacy(d);
    assert(unlinkat(d,RME_RADIO_VINTF_NAME,0)==0);
    assert(rme_radio_vintf_cleanup(d,token,stopped,&ok)==0);
    create_legacy(d);
    assert(unlinkat(d,RME_RADIO_VINTF_NAME,0)==0 && unlinkat(d,stage,0)==0);
    assert(rme_radio_vintf_cleanup(d,token,stopped,&ok)==0 && !exists(d,meta));
    assert(close(d)==0); assert(rmdir(path)==0);
    puts("RADIO_VINTF_FRAGMENT_OK");
}
