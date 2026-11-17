/* Host test for the BIOS (VBE) mode path against a scripted INT10 service. */
#include "nvx_vbe.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static UCHAR low[0x100000];
static ULONG current, scenario, controllerCalls, biosModeSets;
static int frees, derefs;

VOID LogStr(const char *s) { (void)s; }
VOID LogHex(unsigned long long x) { (void)x; }
VOID VideoPortZeroMemory(PVOID p,ULONG n) { memset(p,0,n); }
VOID VideoPortMoveMemory(PVOID d,PVOID s,ULONG n) { memmove(d,s,n); }
static VOID deref(PVOID p) { (void)p;derefs++; }
static VP_STATUS allocate(PVOID p,PUSHORT s,PUSHORT o,PULONG n)
{ (void)p;*s=0x1200;*o=0;*n=512;return 0; }
static VP_STATUS release(PVOID p,USHORT s,USHORT o)
{ (void)p;(void)s;(void)o;frees++;return 0; }
static VP_STATUS readmem(PVOID p,USHORT s,USHORT o,PVOID dst,ULONG n)
{ (void)p; assert((ULONG)s*16+o+n<=sizeof(low)); memcpy(dst,low+(ULONG)s*16+o,n);return 0; }
static VP_STATUS writemem(PVOID p,USHORT s,USHORT o,PVOID src,ULONG n)
{ (void)p;assert((ULONG)s*16+o+n<=sizeof(low));memcpy(low+(ULONG)s*16+o,src,n);return 0; }

static void make_edid(UCHAR *e)
{
    static const UCHAR header[8]={0,0xff,0xff,0xff,0xff,0xff,0xff,0};
    UCHAR sum=0;
    memset(e,0,128);memcpy(e,header,8);
    e[8]=0x10;e[9]=0xac;e[10]=0x34;e[11]=0x12;
    for(int i=0;i<127;i++) sum=(UCHAR)(sum+e[i]);
    e[127]=(UCHAR)(0-sum);
}

/* Scenarios:
 *  0 healthy BIOS: 6 linear 32bpp modes + EDID
 *  1 INT10 service unavailable
 *  2 every mode has an impossible stride
 *  3 SET_MODE "succeeds" but CURRENT_MODE reports something else
 *  4 short list (3 modes) that grows to 6 after the first mode set
 *  5 healthy modes, DDC unsupported
 *  6 healthy modes, EDID with a bad checksum
 *  7 a mode whose framebuffer lies outside BAR1 is dropped
 *  8 16/24bpp modes are dropped, 32bpp kept
 */
static const USHORT ids[6]={0x112,0x115,0x118,0x11b,0x145,0x14a};
static const USHORT widths[6]={640,800,1024,1280,1920,2560};
static const USHORT heights[6]={480,600,768,1024,1080,1440};

static VP_STATUS call(PVOID p,NVX_INT10_ARGS *a)
{
    (void)p;
    void *buffer=low+(ULONG)a->SegEs*16+a->Edi;
    if((a->Eax&0xffff)==0x4f00) {
        VBE_INFO_BLOCK *info=buffer;
        USHORT *list=(void *)(low+0x12080);
        ULONG n=(scenario==4 && !biosModeSets)?3:6;
        controllerCalls++;
        memcpy(info->VbeSignature,"VESA",4);info->VbeVersion=0x300;
        info->VideoModePtr=0x12000080; /* list inside the reused BIOS buffer */
        for(ULONG i=0;i<n;i++) list[i]=ids[i];
        list[n]=0xffff;
    } else if((a->Eax&0xffff)==0x4f01) {
        VBE_MODE_INFO *v=buffer;
        int i;
        for(i=0;i<6;i++) if(ids[i]==a->Ecx) break;
        assert(i<6);
        v->ModeAttributes=0x9b;v->NumberOfPlanes=1;v->MemoryModel=6;
        v->BitsPerPixel=32;
        if(scenario==8 && i==0) v->BitsPerPixel=16;
        if(scenario==8 && i==1) v->BitsPerPixel=24;
        v->XResolution=widths[i];v->YResolution=heights[i];
        v->BytesPerScanLine=(USHORT)(widths[i]*4);
        v->LinBytesPerScanLine=(USHORT)(widths[i]*4+(i==2?64:0)); /* one padded stride */
        v->PhysBasePtr=0xe0000000;
        if(scenario==7 && i==5) v->PhysBasePtr=0xeff00000; /* 2560x1440x4 overruns BAR1 */
        v->LinRedMaskSize=v->LinGreenMaskSize=v->LinBlueMaskSize=8;
        v->LinRedFieldPosition=16;v->LinGreenFieldPosition=8;
        if(scenario==2) v->LinBytesPerScanLine=1;
    } else {
        assert((a->Eax&0xffff)==0x4f15 && (a->Ebx&0xff)==1);
        if(scenario==5) {a->Eax=0x014f;return 0;}
        make_edid(buffer);
        if(scenario==6) ((UCHAR *)buffer)[127]^=1;
    }
    a->Eax=0xffff004f;return 0;
}
VP_STATUS VideoPortQueryServices(PVOID e,ULONG type,PVOID ptr)
{
    NVX_INT10_INTERFACE *api=ptr;(void)e;
    assert(type==4 && api->Size==sizeof(NVX_INT10_INTERFACE) && api->Version==1);
    if(scenario==1) return 87;
    api->Dereference=deref;api->Allocate=allocate;api->Free=release;
    api->Read=readmem;api->Write=writemem;api->Call=call;return 0;
}
VP_STATUS VideoPortInt10(PVOID e,VIDEO_X86_BIOS_ARGUMENTS *a)
{
    (void)e;
    if(a->Eax==0x4f02) {
        assert(a->Ebx&0x4000); /* linear framebuffer always requested */
        biosModeSets++;
        if(scenario!=3) current=a->Ebx&0x3fff;
    }
    else { assert(a->Eax==0x4f03);a->Ebx=current; }
    a->Eax=0xffff004f;return 0;
}

static void fresh(NVX_DEVICE_EXTENSION *dev)
{
    memset(dev,0,sizeof(*dev));memset(low,0,sizeof(low));
    current=3;frees=derefs=0;controllerCalls=0;biosModeSets=0;
    dev->PhysicalBar1.QuadPart=0xe0000000;dev->Bar1Length=0x10000000;
}

int main(void)
{
    NVX_DEVICE_EXTENSION dev;
    UCHAR edid[128];
    assert(NvxVbeSuccess(0,0xffff004f));
    assert(!NvxVbeSuccess(0,0xffff014f));
    assert(!NvxVbeSuccess(87,0x004f));
    make_edid(edid);assert(NvxEdidBlockValid(edid));
    edid[20]^=0x40;assert(!NvxEdidBlockValid(edid));
    make_edid(edid);edid[1]=0;assert(!NvxEdidBlockValid(edid));

    scenario=0;fresh(&dev);
    assert(NvxVbeInitialize(&dev));
    assert(dev.NumModes==6 && controllerCalls==1 && frees==1 && derefs==1);
    assert(!biosModeSets && !dev.ModeIsSet); /* enumeration alone never changes the mode */
    assert(dev.EdidValid && dev.Edid[8]==0x10 && dev.Edid[9]==0xac);
    assert(dev.Modes[4].VisScreenWidth==1920 && dev.Modes[4].ScreenStride==7680);
    assert(dev.Modes[2].ScreenStride==1024*4+64);      /* BIOS stride, not width*4 */
    assert(dev.Modes[2].VideoMemoryBitmapWidth==1024+16);
    assert(dev.Modes[0].RedMask==0x00ff0000 && dev.Modes[0].GreenMask==0x0000ff00 &&
           dev.Modes[0].BlueMask==0x000000ff && dev.Modes[0].BitsPerPlane==32);
    assert(dev.ModeFramebuffer[5].QuadPart==0xe0000000);
    assert(NvxVbeSetMode(&dev,4) && dev.ModeIsSet && current==0x145);
    assert(dev.CurrentModeIndex==4 && dev.ActiveWidth==1920 && dev.ActiveHeight==1080 && dev.ActiveBpp==32);
    assert(!NvxVbeSetMode(&dev,6) && dev.CurrentModeIndex==4);  /* out of range */

    scenario=1;fresh(&dev);
    assert(!NvxVbeInitialize(&dev) && !dev.NumModes && !frees && !derefs);

    scenario=2;fresh(&dev);
    assert(!NvxVbeInitialize(&dev) && !dev.NumModes && frees==1 && derefs==1);

    /* Mode set not confirmed by read-back: reported as failure, state kept. */
    scenario=0;fresh(&dev);assert(NvxVbeInitialize(&dev));
    assert(NvxVbeSetMode(&dev,1));
    scenario=3;
    assert(!NvxVbeSetMode(&dev,4));
    assert(dev.CurrentModeIndex==1 && dev.ActiveWidth==800);

    /* Short list: one verified mode set, then the fuller list is adopted and
       the active mode is re-anchored by VBE id. */
    scenario=4;fresh(&dev);
    assert(NvxVbeInitialize(&dev));
    assert(controllerCalls==2 && biosModeSets==1 && dev.NumModes==6);
    assert(frees==2 && derefs==2);
    assert(dev.ModeIsSet && dev.ModeVbeIds[dev.CurrentModeIndex]==current && current==0x112);
    assert(dev.EdidValid);

    scenario=5;fresh(&dev);
    assert(NvxVbeInitialize(&dev) && dev.NumModes==6 && !dev.EdidValid);
    scenario=6;fresh(&dev);
    assert(NvxVbeInitialize(&dev) && dev.NumModes==6 && !dev.EdidValid);

    scenario=7;fresh(&dev);
    assert(NvxVbeInitialize(&dev) && dev.NumModes==5);
    for(ULONG i=0;i<dev.NumModes;i++) assert(dev.ModeVbeIds[i]!=0x14a);

    scenario=8;fresh(&dev);
    assert(NvxVbeInitialize(&dev) && dev.NumModes==4);
    for(ULONG i=0;i<dev.NumModes;i++) assert(dev.Modes[i].BitsPerPlane==32);

    puts("vbe_mock_test: ok");
    return 0;
}
