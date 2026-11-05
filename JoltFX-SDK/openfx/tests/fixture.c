#include <ofxImageEffect.h>
#include <ofxProperty.h>
#include <ofxMemory.h>
#include <ofxMessage.h>
#include <string.h>

static OfxHost *host;
static const OfxPropertySuiteV1 *props;
static int loaded;
static void set_host(OfxHost *h) { host=h; }
static OfxStatus entry(const char *action, const void *handle,
                      OfxPropertySetHandle in, OfxPropertySetHandle out) {
    (void)in; (void)out;
    if (!strcmp(action,kOfxActionLoad)) {
        props=host->fetchSuite(host->host,kOfxPropertySuite,1);
        if (!props || host->fetchSuite(host->host,kOfxPropertySuite,99)) return kOfxStatFailed;
        loaded=1;
        return kOfxStatOK;
    }
    if (!strcmp(action,kOfxActionUnload)) { loaded=0; return kOfxStatOK; }
    if (!strcmp(action,kOfxActionDescribe)) {
        const OfxImageEffectSuiteV1 *images=host->fetchSuite(host->host,kOfxImageEffectSuite,1);
        OfxPropertySetHandle p;
        if (!loaded || !images || images->getPropertySet((OfxImageEffectHandle)handle,&p)) return kOfxStatFailed;
        const char *contexts[]={kOfxImageEffectContextGeneral,kOfxImageEffectContextFilter};
        const char *depths[]={kOfxBitDepthFloat,kOfxBitDepthByte};
        if (props->propSetString(p,kOfxPropLabel,0,"SDK inspection fixture") ||
            props->propSetString(p,kOfxImageEffectPluginPropGrouping,0,"JoltFX/Tests") ||
            props->propSetStringN(p,kOfxImageEffectPropSupportedContexts,2,contexts) ||
            props->propSetStringN(p,kOfxImageEffectPropSupportedPixelDepths,2,depths)) return kOfxStatErrMemory;
        int count=0;
        char *label=NULL;
        if (props->propGetDimension(p,kOfxImageEffectPropSupportedContexts,&count) || count!=2 ||
            props->propGetString(p,kOfxPropLabel,0,&label) || strcmp(label,"SDK inspection fixture")) return kOfxStatFailed;
        if (props->propGetString(p,kOfxPropLabel,-1,&label)!=kOfxStatErrBadIndex) return kOfxStatFailed;
        int ints[]={7,8},got[2]={0}; double number=0;
        void *pointer=NULL;
        if (props->propSetIntN(p,"test.ints",2,ints) || props->propGetIntN(p,"test.ints",2,got) ||
            got[0]!=7 || got[1]!=8 || props->propSetDouble(p,"test.double",0,2.5) ||
            props->propGetDouble(p,"test.double",0,&number) || number!=2.5 ||
            props->propSetPointer(p,"test.pointer",0,host) || props->propGetPointer(p,"test.pointer",0,&pointer) ||
            pointer!=host) return kOfxStatErrMemory;
        if (props->propSetInt(p,"test.double",0,1)!=kOfxStatErrValue ||
            props->propSetInt(p,"test.ints",16,1)!=kOfxStatErrBadIndex ||
            props->propGetInt(p,"test.missing",0,got)!=kOfxStatErrUnknown) return kOfxStatFailed;
        if (props->propSetIntN(p,"test.ints",1,ints) || props->propGetDimension(p,"test.ints",&count) || count!=1 ||
            props->propReset(p,"test.ints") || props->propGetDimension(p,"test.ints",&count) || count!=0 ||
            props->propGetIntN(p,"test.ints",0,NULL)) return kOfxStatFailed;
        const OfxMemorySuiteV1 *memory=host->fetchSuite(host->host,kOfxMemorySuite,1);
        if (!memory || memory->memoryAlloc(NULL,97,&pointer)) return kOfxStatErrMemory;
        memset(pointer,23,97);
        if (memory->memoryFree(pointer)) return kOfxStatFailed;
        const OfxMessageSuiteV1 *messages=host->fetchSuite(host->host,kOfxMessageSuite,1);
        if (!messages || messages->message(handle? (void *)handle:NULL,kOfxMessageLog,"test","fixture %d",42)) return kOfxStatFailed;
        return kOfxStatOK;
    }
    return kOfxStatReplyDefault;
}
static OfxPlugin plugin={kOfxImageEffectPluginApi,1,"org.joltfx.inspection",2,3,set_host,entry};
static OfxStatus fail_entry(const char *action,const void *handle,OfxPropertySetHandle in,OfxPropertySetHandle out) {
    (void)handle; (void)in; (void)out;
    if (!strcmp(action,kOfxActionDescribe)) return kOfxStatErrMissingHostFeature;
    return kOfxStatOK;
}
static OfxPlugin unsupported={kOfxImageEffectPluginApi,1,"org.joltfx.unsupported",1,0,set_host,fail_entry};
OfxExport int OfxGetNumberOfPlugins(void) { return 2; }
OfxExport OfxPlugin *OfxGetPlugin(int i) { return i==0?&plugin:i==1?&unsupported:NULL; }
/* OfxSetHost is deliberately absent: it is optional, unlike plugin.setHost. */
