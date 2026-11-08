/*
 * libOpenMAXAL.so: the NDK's OpenMAX AL library, with no engine behind it.
 *
 * Android's OpenMAX AL (deprecated in the NDK) plays MPEG-2 transport streams from an Android buffer
 * queue, and nothing carries it to OH's media services. Corpus apps name the library in DT_NEEDED
 * without importing a single symbol from it: Roblox's libroblox.so, Insta360's video player, Amazon
 * Shopping's video codec and Slotomania's SDK. Without a library of that name their library did not
 * load at all, and Roblox's engine never started.
 *
 * This library exports what AOSP's does (libOpenMAXAL.map.txt, version LIBOPENMAXAL): the interface
 * IDs, with AOSP's values (frameworks/wilhelm, OpenSLES_IID.cpp: XA_IID_NULL and
 * XA_IID_ANDROIDBUFFERQUEUESOURCE share OpenSL ES's), and an engine that cannot be created, which the
 * API reports as XA_RESULT_FEATURE_UNSUPPORTED.
 */
#include <stddef.h>
#include <stdint.h>

#define EXPORT __attribute__((visibility("default")))

/* OpenMAXAL.h */
typedef uint8_t XAuint8;
typedef uint16_t XAuint16;
typedef uint32_t XAuint32;
typedef XAuint32 XAresult;
typedef XAuint32 XAboolean;
typedef const struct XAInterfaceID_ {
    XAuint32 time_low;
    XAuint16 time_mid;
    XAuint16 time_hi_and_version;
    XAuint16 clock_seq;
    XAuint8 node[6];
} *XAInterfaceID;
typedef const struct XAObjectItf_* const* XAObjectItf;
typedef struct XAEngineOption_ XAEngineOption;

#define XA_RESULT_SUCCESS ((XAuint32)0x00000000)
#define XA_RESULT_PARAMETER_INVALID ((XAuint32)0x00000002)
#define XA_RESULT_FEATURE_UNSUPPORTED ((XAuint32)0x0000000C)

static const struct XAInterfaceID_ iid_androidbufferqueuesource =
    {0x7fc1a460, 0xeec1, 0x11df, 0xa306, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}};
EXPORT const XAInterfaceID XA_IID_ANDROIDBUFFERQUEUESOURCE = &iid_androidbufferqueuesource;
static const struct XAInterfaceID_ iid_audiodecodercapabilities =
    {0xdeac0cc0, 0x3995, 0x11dc, 0x8872, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}};
EXPORT const XAInterfaceID XA_IID_AUDIODECODERCAPABILITIES = &iid_audiodecodercapabilities;
static const struct XAInterfaceID_ iid_audioencoder =
    {0xebbab900, 0x3997, 0x11dc, 0x891f, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}};
EXPORT const XAInterfaceID XA_IID_AUDIOENCODER = &iid_audioencoder;
static const struct XAInterfaceID_ iid_audioencodercapabilities =
    {0x83fbc600, 0x3998, 0x11dc, 0x8f6d, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}};
EXPORT const XAInterfaceID XA_IID_AUDIOENCODERCAPABILITIES = &iid_audioencodercapabilities;
static const struct XAInterfaceID_ iid_audioiodevicecapabilities =
    {0x2b276d00, 0xf775, 0x11db, 0xa963, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}};
EXPORT const XAInterfaceID XA_IID_AUDIOIODEVICECAPABILITIES = &iid_audioiodevicecapabilities;
static const struct XAInterfaceID_ iid_camera =
    {0xc7b84d20, 0xdf00, 0x11db, 0xba87, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}};
EXPORT const XAInterfaceID XA_IID_CAMERA = &iid_camera;
static const struct XAInterfaceID_ iid_cameracapabilities =
    {0x01cab1c0, 0xe86a, 0x11db, 0xa5b9, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}};
EXPORT const XAInterfaceID XA_IID_CAMERACAPABILITIES = &iid_cameracapabilities;
static const struct XAInterfaceID_ iid_configextension =
    {0x6dc22ea0, 0xdf03, 0x11db, 0xbed7, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}};
EXPORT const XAInterfaceID XA_IID_CONFIGEXTENSION = &iid_configextension;
static const struct XAInterfaceID_ iid_devicevolume =
    {0x4bb44020, 0xf775, 0x11db, 0xad03, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}};
EXPORT const XAInterfaceID XA_IID_DEVICEVOLUME = &iid_devicevolume;
static const struct XAInterfaceID_ iid_dynamicinterfacemanagement =
    {0x6e2340c0, 0xf775, 0x11db, 0x85da, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}};
EXPORT const XAInterfaceID XA_IID_DYNAMICINTERFACEMANAGEMENT = &iid_dynamicinterfacemanagement;
static const struct XAInterfaceID_ iid_dynamicsource =
    {0xc88d5480, 0x3a12, 0x11dc, 0x80a2, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}};
EXPORT const XAInterfaceID XA_IID_DYNAMICSOURCE = &iid_dynamicsource;
static const struct XAInterfaceID_ iid_engine =
    {0x45c58f40, 0xdf04, 0x11db, 0x9e76, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}};
EXPORT const XAInterfaceID XA_IID_ENGINE = &iid_engine;
static const struct XAInterfaceID_ iid_equalizer =
    {0x7ad86d40, 0xf775, 0x11db, 0xbc77, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}};
EXPORT const XAInterfaceID XA_IID_EQUALIZER = &iid_equalizer;
static const struct XAInterfaceID_ iid_imagecontrols =
    {0xf46de3e0, 0xdf03, 0x11db, 0x92f1, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}};
EXPORT const XAInterfaceID XA_IID_IMAGECONTROLS = &iid_imagecontrols;
static const struct XAInterfaceID_ iid_imagedecodercapabilities =
    {0xc333e7a0, 0xe616, 0x11dc, 0xa93e, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}};
EXPORT const XAInterfaceID XA_IID_IMAGEDECODERCAPABILITIES = &iid_imagedecodercapabilities;
static const struct XAInterfaceID_ iid_imageeffects =
    {0xb865bca0, 0xdf04, 0x11db, 0xbab9, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}};
EXPORT const XAInterfaceID XA_IID_IMAGEEFFECTS = &iid_imageeffects;
static const struct XAInterfaceID_ iid_imageencoder =
    {0xcd49f140, 0xdf04, 0x11db, 0x8888, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}};
EXPORT const XAInterfaceID XA_IID_IMAGEENCODER = &iid_imageencoder;
static const struct XAInterfaceID_ iid_imageencodercapabilities =
    {0xc19f0640, 0xe86f, 0x11db, 0xb2d2, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}};
EXPORT const XAInterfaceID XA_IID_IMAGEENCODERCAPABILITIES = &iid_imageencodercapabilities;
static const struct XAInterfaceID_ iid_led =
    {0xa534d920, 0xf775, 0x11db, 0x8b70, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}};
EXPORT const XAInterfaceID XA_IID_LED = &iid_led;
static const struct XAInterfaceID_ iid_metadataextraction =
    {0x5df4fda0, 0xf776, 0x11db, 0xabc5, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}};
EXPORT const XAInterfaceID XA_IID_METADATAEXTRACTION = &iid_metadataextraction;
static const struct XAInterfaceID_ iid_metadatainsertion =
    {0x49a14d60, 0xdf05, 0x11db, 0x9191, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}};
EXPORT const XAInterfaceID XA_IID_METADATAINSERTION = &iid_metadatainsertion;
static const struct XAInterfaceID_ iid_metadatatraversal =
    {0x73ffb0e0, 0xf776, 0x11db, 0xa00e, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}};
EXPORT const XAInterfaceID XA_IID_METADATATRAVERSAL = &iid_metadatatraversal;
static const struct XAInterfaceID_ iid_null =
    {0xec7178ec, 0xe5e1, 0x4432, 0xa3f4, {0x46, 0x57, 0xe6, 0x79, 0x52, 0x10}};
EXPORT const XAInterfaceID XA_IID_NULL = &iid_null;
static const struct XAInterfaceID_ iid_object =
    {0x82f5a5a0, 0xf776, 0x11db, 0x9700, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}};
EXPORT const XAInterfaceID XA_IID_OBJECT = &iid_object;
static const struct XAInterfaceID_ iid_outputmix =
    {0xb25b6fa0, 0xf776, 0x11db, 0xb86b, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}};
EXPORT const XAInterfaceID XA_IID_OUTPUTMIX = &iid_outputmix;
static const struct XAInterfaceID_ iid_play =
    {0xb9c293e0, 0xf776, 0x11db, 0x80df, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}};
EXPORT const XAInterfaceID XA_IID_PLAY = &iid_play;
static const struct XAInterfaceID_ iid_playbackrate =
    {0xc36f1440, 0xf776, 0x11db, 0xac48, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}};
EXPORT const XAInterfaceID XA_IID_PLAYBACKRATE = &iid_playbackrate;
static const struct XAInterfaceID_ iid_prefetchstatus =
    {0xcceac0a0, 0xf776, 0x11db, 0xbb9c, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}};
EXPORT const XAInterfaceID XA_IID_PREFETCHSTATUS = &iid_prefetchstatus;
static const struct XAInterfaceID_ iid_radio =
    {0xb316ad80, 0xdf05, 0x11db, 0xb5b6, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}};
EXPORT const XAInterfaceID XA_IID_RADIO = &iid_radio;
static const struct XAInterfaceID_ iid_rds =
    {0xb80f42c0, 0xdf05, 0x11db, 0x92a5, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}};
EXPORT const XAInterfaceID XA_IID_RDS = &iid_rds;
static const struct XAInterfaceID_ iid_record =
    {0xd7948cc0, 0xf776, 0x11db, 0x8a3b, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}};
EXPORT const XAInterfaceID XA_IID_RECORD = &iid_record;
static const struct XAInterfaceID_ iid_seek =
    {0xee6a3120, 0xf776, 0x11db, 0xb518, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}};
EXPORT const XAInterfaceID XA_IID_SEEK = &iid_seek;
static const struct XAInterfaceID_ iid_snapshot =
    {0xdb1b6dc0, 0xdf05, 0x11db, 0x8c01, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}};
EXPORT const XAInterfaceID XA_IID_SNAPSHOT = &iid_snapshot;
static const struct XAInterfaceID_ iid_streaminformation =
    {0x3a628fe0, 0x1238, 0x11de, 0xad9f, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}};
EXPORT const XAInterfaceID XA_IID_STREAMINFORMATION = &iid_streaminformation;
static const struct XAInterfaceID_ iid_threadsync =
    {0xf3599ea0, 0xf776, 0x11db, 0xb3ea, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}};
EXPORT const XAInterfaceID XA_IID_THREADSYNC = &iid_threadsync;
static const struct XAInterfaceID_ iid_vibra =
    {0xfe374c00, 0xf776, 0x11db, 0xa8f0, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}};
EXPORT const XAInterfaceID XA_IID_VIBRA = &iid_vibra;
static const struct XAInterfaceID_ iid_videodecodercapabilities =
    {0xd18cb200, 0xe616, 0x11dc, 0xab01, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}};
EXPORT const XAInterfaceID XA_IID_VIDEODECODERCAPABILITIES = &iid_videodecodercapabilities;
static const struct XAInterfaceID_ iid_videoencoder =
    {0x9444db60, 0xdf06, 0x11db, 0xb311, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}};
EXPORT const XAInterfaceID XA_IID_VIDEOENCODER = &iid_videoencoder;
static const struct XAInterfaceID_ iid_videoencodercapabilities =
    {0x5aef2760, 0xe872, 0x11db, 0x849f, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}};
EXPORT const XAInterfaceID XA_IID_VIDEOENCODERCAPABILITIES = &iid_videoencodercapabilities;
static const struct XAInterfaceID_ iid_videopostprocessing =
    {0x898b6820, 0x7e6e, 0x11dd, 0x8caf, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}};
EXPORT const XAInterfaceID XA_IID_VIDEOPOSTPROCESSING = &iid_videopostprocessing;
static const struct XAInterfaceID_ iid_volume =
    {0x088ba520, 0xf777, 0x11db, 0xa5e3, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}};
EXPORT const XAInterfaceID XA_IID_VOLUME = &iid_volume;

/* No engine can be created, so none of its interfaces is supported either. */
EXPORT XAresult xaCreateEngine(XAObjectItf* pEngine, XAuint32 numOptions, const XAEngineOption* pEngineOptions,
                               XAuint32 numInterfaces, const XAInterfaceID* pInterfaceIds,
                               const XAboolean* pInterfaceRequired) {
    (void)numOptions; (void)pEngineOptions; (void)numInterfaces; (void)pInterfaceIds; (void)pInterfaceRequired;
    if (pEngine == NULL) return XA_RESULT_PARAMETER_INVALID;
    *pEngine = NULL;
    return XA_RESULT_FEATURE_UNSUPPORTED;
}

EXPORT XAresult xaQueryNumSupportedEngineInterfaces(XAuint32* pNumSupportedInterfaces) {
    if (pNumSupportedInterfaces == NULL) return XA_RESULT_PARAMETER_INVALID;
    *pNumSupportedInterfaces = 0;
    return XA_RESULT_SUCCESS;
}

/* Every index is past the end of an empty list. */
EXPORT XAresult xaQuerySupportedEngineInterfaces(XAuint32 index, XAInterfaceID* pInterfaceId) {
    (void)index;
    if (pInterfaceId != NULL) *pInterfaceId = NULL;
    return XA_RESULT_PARAMETER_INVALID;
}
