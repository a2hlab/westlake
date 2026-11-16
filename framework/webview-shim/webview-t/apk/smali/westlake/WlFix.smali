.class public final Lwestlake/WlFix;
.super Ljava/lang/Object;
.source "WlFix.java"


# annotations
.annotation system Ldalvik/annotation/MemberClasses;
    value = {
        Lwestlake/WlFix$JpegBoundaryNetworkTask;,
        Lwestlake/WlFix$JpegPollTask;,
        Lwestlake/WlFix$BoundarySelfTestTask;,
        Lwestlake/WlFix$JpegPollResult;,
        Lwestlake/WlFix$PngReplaceTask;
    }
.end annotation


# static fields
.field private static final assetProbeScheduled:Ljava/util/IdentityHashMap;
    .annotation system Ldalvik/annotation/Signature;
        value = {
            "Ljava/util/IdentityHashMap<",
            "Ljava/lang/Object;",
            "Ljava/lang/Boolean;",
            ">;"
        }
    .end annotation
.end field

.field private static final assetProbeSubmitted:Ljava/util/IdentityHashMap;
    .annotation system Ldalvik/annotation/Signature;
        value = {
            "Ljava/util/IdentityHashMap<",
            "Ljava/lang/Object;",
            "Ljava/lang/Boolean;",
            ">;"
        }
    .end annotation
.end field

.field private static final drawCounts:Ljava/util/IdentityHashMap;
    .annotation system Ldalvik/annotation/Signature;
        value = {
            "Ljava/util/IdentityHashMap<",
            "Ljava/lang/Object;",
            "Ljava/lang/Integer;",
            ">;"
        }
    .end annotation
.end field

.field private static error:Ljava/io/OutputStream;

.field private static final jpegRepairOwners:Ljava/util/HashMap;
    .annotation system Ldalvik/annotation/Signature;
        value = {
            "Ljava/util/HashMap<",
            "Ljava/lang/String;",
            "Ljava/lang/Object;",
            ">;"
        }
    .end annotation
.end field

.field private static final jpegRepairScheduled:Ljava/util/IdentityHashMap;
    .annotation system Ldalvik/annotation/Signature;
        value = {
            "Ljava/util/IdentityHashMap<",
            "Ljava/lang/Object;",
            "Ljava/lang/Boolean;",
            ">;"
        }
    .end annotation
.end field

.field private static final jpegRepairStarted:Ljava/util/IdentityHashMap;
    .annotation system Ldalvik/annotation/Signature;
        value = {
            "Ljava/util/IdentityHashMap<",
            "Ljava/lang/Object;",
            "Ljava/lang/Boolean;",
            ">;"
        }
    .end annotation
.end field

.field private static scrollSequence:I


# direct methods
.method static constructor <clinit>()V
    .registers 1

    .line 22
    new-instance v0, Ljava/util/IdentityHashMap;

    invoke-direct {v0}, Ljava/util/IdentityHashMap;-><init>()V

    sput-object v0, Lwestlake/WlFix;->assetProbeSubmitted:Ljava/util/IdentityHashMap;

    .line 24
    new-instance v0, Ljava/util/IdentityHashMap;

    invoke-direct {v0}, Ljava/util/IdentityHashMap;-><init>()V

    sput-object v0, Lwestlake/WlFix;->assetProbeScheduled:Ljava/util/IdentityHashMap;

    .line 26
    new-instance v0, Ljava/util/IdentityHashMap;

    invoke-direct {v0}, Ljava/util/IdentityHashMap;-><init>()V

    sput-object v0, Lwestlake/WlFix;->jpegRepairScheduled:Ljava/util/IdentityHashMap;

    .line 28
    new-instance v0, Ljava/util/IdentityHashMap;

    invoke-direct {v0}, Ljava/util/IdentityHashMap;-><init>()V

    sput-object v0, Lwestlake/WlFix;->jpegRepairStarted:Ljava/util/IdentityHashMap;

    .line 30
    new-instance v0, Ljava/util/HashMap;

    invoke-direct {v0}, Ljava/util/HashMap;-><init>()V

    sput-object v0, Lwestlake/WlFix;->jpegRepairOwners:Ljava/util/HashMap;

    .line 31
    new-instance v0, Ljava/util/IdentityHashMap;

    invoke-direct {v0}, Ljava/util/IdentityHashMap;-><init>()V

    sput-object v0, Lwestlake/WlFix;->drawCounts:Ljava/util/IdentityHashMap;

    return-void
.end method

.method private constructor <init>()V
    .registers 1

    .line 33
    invoke-direct {p0}, Ljava/lang/Object;-><init>()V

    return-void
.end method

.method static synthetic access$000(Ljava/lang/String;)V
    .registers 1

    .line 19
    invoke-static {p0}, Lwestlake/WlFix;->emitLine(Ljava/lang/String;)V

    return-void
.end method

.method static synthetic access$100(Ljava/lang/Object;)V
    .registers 1

    .line 19
    invoke-static {p0}, Lwestlake/WlFix;->evaluateNativeFallbackDiagnostic(Ljava/lang/Object;)V

    return-void
.end method

.method static synthetic access$1000()Ljava/util/IdentityHashMap;
    .registers 1

    .line 19
    sget-object v0, Lwestlake/WlFix;->jpegRepairStarted:Ljava/util/IdentityHashMap;

    return-object v0
.end method

.method static synthetic access$1100(Ljava/lang/Object;)V
    .registers 1

    .line 19
    invoke-static {p0}, Lwestlake/WlFix;->startJpegRepairLoop(Ljava/lang/Object;)V

    return-void
.end method

.method static synthetic access$200(Ljava/lang/Object;Ljava/lang/String;)V
    .registers 2

    .line 19
    invoke-static {p0, p1}, Lwestlake/WlFix;->evaluateAssetProbe(Ljava/lang/Object;Ljava/lang/String;)V

    return-void
.end method

.method static synthetic access$300(Ljava/lang/Object;)Z
    .registers 1

    .line 19
    invoke-static {p0}, Lwestlake/WlFix;->submitAssetProbe(Ljava/lang/Object;)Z

    move-result p0

    return p0
.end method

.method static synthetic access$400()Ljava/util/IdentityHashMap;
    .registers 1

    .line 19
    sget-object v0, Lwestlake/WlFix;->assetProbeScheduled:Ljava/util/IdentityHashMap;

    return-object v0
.end method

.method static synthetic access$500([I)[B
    .registers 1
    .annotation system Ldalvik/annotation/Throws;
        value = {
            Ljava/lang/Exception;
        }
    .end annotation

    .line 19
    invoke-static {p0}, Lwestlake/WlFix;->encodeArgbPng([I)[B

    move-result-object p0

    return-object p0
.end method

.method static synthetic access$600(Ljava/lang/Object;Ljava/lang/String;[B)V
    .registers 3

    .line 19
    invoke-static {p0, p1, p2}, Lwestlake/WlFix;->replaceJpegWithPng(Ljava/lang/Object;Ljava/lang/String;[B)V

    return-void
.end method

.method static synthetic access$700(Ljava/lang/String;)Ljava/lang/String;
    .registers 1

    .line 19
    invoke-static {p0}, Lwestlake/WlFix;->buildJpegCandidateScript(Ljava/lang/String;)Ljava/lang/String;

    move-result-object p0

    return-object p0
.end method

.method static synthetic access$800(Ljava/lang/Object;)Ljava/lang/Object;
    .registers 1
    .annotation system Ldalvik/annotation/Throws;
        value = {
            Ljava/lang/Exception;
        }
    .end annotation

    .line 19
    invoke-static {p0}, Lwestlake/WlFix;->findWebViewForAwContents(Ljava/lang/Object;)Ljava/lang/Object;

    move-result-object p0

    return-object p0
.end method

.method static synthetic access$900()Ljava/util/IdentityHashMap;
    .registers 1

    .line 19
    sget-object v0, Lwestlake/WlFix;->jpegRepairScheduled:Ljava/util/IdentityHashMap;

    return-object v0
.end method

.method private static buildAssetProbeScript(Ljava/lang/String;)Ljava/lang/String;
    .registers 3

    .line 82
    new-instance v0, Ljava/lang/StringBuilder;

    const-string v1, "(function(){try{window.__wlBridgeCalls=window.__wlBridgeCalls||[];var names=Object.getOwnPropertyNames(window).filter(function(n){return /bridge/i.test(n)});function val(x){try{if(x===null||x===undefined)return String(x);if(typeof x===\'string\')return x.slice(0,2048);if(typeof x===\'number\'||typeof x===\'boolean\')return x;return JSON.stringify(x).slice(0,2048)}catch(e){return Object.prototype.toString.call(x)}}function wrap(label,obj){var keys=[];try{var cur=obj,depth=0;while(cur&&depth++<3){Object.getOwnPropertyNames(cur).forEach(function(k){if(keys.indexOf(k)<0)keys.push(k)});cur=Object.getPrototypeOf(cur)}}catch(e){}keys.forEach(function(k){try{var f=obj[k];if(typeof f!==\'function\'||f.__wlWrapped||!/call|invoke|request|send|handler/i.test(k))return;var w=function(){try{window.__wlBridgeCalls.push({object:label,method:k,args:[].slice.call(arguments,0,6).map(val),time:Date.now()});if(window.__wlBridgeCalls.length>80)window.__wlBridgeCalls.shift()}catch(e){}return f.apply(this,arguments)};w.__wlWrapped=true;obj[k]=w}catch(e){}});return keys.slice(0,80)}var bridgeKeys={};names.slice(0,32).forEach(function(n){try{bridgeKeys[n]=wrap(n,window[n])}catch(e){bridgeKeys[n]=[\'<error:\'+String(e)+\'>\']}});[\'JSBridge\',\'ToutiaoJSBridge\',\'WebViewJavascriptBridge\',\'Native2JSBridge\',\'bytedance\'].forEach(function(n){try{if(window[n]&&!bridgeKeys[n])bridgeKeys[n]=wrap(n,window[n])}catch(e){}});var o={phase:\'"

    invoke-direct {v0, v1}, Ljava/lang/StringBuilder;-><init>(Ljava/lang/String;)V

    invoke-virtual {v0, p0}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v0

    const-string v1, "\',url:location.href,ready:document.readyState,userAgent:navigator.userAgent,enableBDNative:window.enableBDNative,ttGlobalConfig:val(window.ttGlobalConfig),scrollY:window.scrollY||document.documentElement.scrollTop||0,height:document.documentElement.scrollHeight,bridgeKeys:bridgeKeys,bridgeCalls:window.__wlBridgeCalls.slice(-80),bodyText:(document.body&&document.body.innerText||\'\').slice(0,16000)};o.bodyStyle=(function(){if(!document.body)return null;var s=getComputedStyle(document.body),r=document.body.getBoundingClientRect();return {rect:[r.left,r.top,r.width,r.height],display:s.display,visibility:s.visibility,opacity:s.opacity,color:s.color,background:s.backgroundColor,overflow:s.overflow}})();o.text=[];var all=[].slice.call(document.querySelectorAll(\'body *\'));for(var i=0;i<all.length&&o.text.length<80;i++){var x=all[i];if(x.children.length)continue;var t=(x.innerText||x.textContent||\'\').trim();if(!t)continue;var r=x.getBoundingClientRect(),s=getComputedStyle(x);o.text.push({tag:x.tagName,text:t.slice(0,300),className:String(x.className||\'\').slice(0,200),rect:[r.left,r.top,r.width,r.height],display:s.display,visibility:s.visibility,opacity:s.opacity,color:s.color,fontSize:s.fontSize})}o.links=[].slice.call(document.querySelectorAll(\'link[rel=stylesheet]\')).map(function(x){return {href:x.href,sheet:!!x.sheet,disabled:x.disabled}});o.styles=[].slice.call(document.styleSheets).map(function(x){var n=-1,e=\'\';try{n=x.cssRules.length}catch(z){e=String(z)}return {href:x.href||\'\',rules:n,error:e}});o.images=[].slice.call(document.images,0,40).map(function(x){return {complete:x.complete,naturalWidth:x.naturalWidth,naturalHeight:x.naturalHeight,src:x.currentSrc||x.src}});o.nativeImages=[].slice.call(document.querySelectorAll(\'native-image\'),0,40).map(function(x){var r=x.getBoundingClientRect(),s=getComputedStyle(x);return {id:x._id,state:x.state,counter:x.counter,isInserted:x.isInserted,naturalWidth:x.naturalWidth,naturalHeight:x.naturalHeight,rect:[r.left,r.top,r.width,r.height],display:s.display,visibility:s.visibility,opacity:s.opacity,src:x.getAttribute(\'src\'),dataExtra:(x.getAttribute(\'data-extra\')||\'\').slice(0,4096),html:x.outerHTML.slice(0,8192)}});o.nativeObjects=[].slice.call(document.querySelectorAll(\'object[type=\"native/image\"]\'),0,40).map(function(x){var r=x.getBoundingClientRect();return {id:x.id,data:x.data,type:x.type,rect:[r.left,r.top,r.width,r.height],html:x.outerHTML.slice(0,8192)}});o.frames=[].slice.call(document.querySelectorAll(\'iframe\')).map(function(x){var r=x.getBoundingClientRect();var child=null,error=\'\';try{child={url:x.contentWindow.location.href,ready:x.contentDocument.readyState,images:[].slice.call(x.contentDocument.images,0,16).map(function(i){return {complete:i.complete,naturalWidth:i.naturalWidth,naturalHeight:i.naturalHeight,src:i.currentSrc||i.src}})}}catch(e){error=String(e)}return {src:x.src,rect:[r.left,r.top,r.width,r.height],child:child,error:error}});o.resources=(performance.getEntriesByType(\'resource\')||[]).slice(-80).map(function(x){return {name:x.name,initiator:x.initiatorType,duration:x.duration,transfer:x.transferSize||0,encoded:x.encodedBodySize||0,decoded:x.decodedBodySize||0}});return JSON.stringify(o)}catch(e){return JSON.stringify({phase:\'"

    invoke-virtual {v0, v1}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v0

    invoke-virtual {v0, p0}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object p0

    const-string v0, "\',error:String(e),stack:String(e&&e.stack)})}})()"

    invoke-virtual {p0, v0}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object p0

    invoke-virtual {p0}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;

    move-result-object p0

    return-object p0
.end method

.method private static buildJpegCandidateScript(Ljava/lang/String;)Ljava/lang/String;
    .registers 3

    .line 344
    new-instance v0, Ljava/lang/StringBuilder;

    const-string v1, "(function(){try{window.__wlJpegQueue=window.__wlJpegQueue||[];window.__wlJpegNodes=window.__wlJpegNodes||{};window.__wlJpegSeq=window.__wlJpegSeq||0;var ready=window.__wlJpegQueue.shift();if(ready)return ready;var n=0;[].slice.call(document.images).forEach(function(x){if(n>=3||!x.complete||x.naturalWidth!==0||x.dataset.wlJpegState)return;var r=x.getBoundingClientRect();if(r.width<=0||r.height<=0)return;var u=x.currentSrc||x.src||\'\';if(!/^https?:/i.test(u))return;var id=\'j\'+(++window.__wlJpegSeq);x.dataset.wlJpegState=\'pending\';window.__wlJpegNodes[id]=x;n++;fetch(u,{credentials:\'include\'}).then(function(q){if(!q.ok)throw Error(\'http \'+q.status);return q.arrayBuffer()}).then(function(b){var a=new Uint8Array(b);if(a.length<3||a[0]!==255||a[1]!==216||a[2]!==255){x.dataset.wlJpegState=\'nonjpeg\';return}if(a.length>12582912)throw Error(\'jpeg too large \'+a.length);var s=\'\',step=32768;for(var i=0;i<a.length;i+=step){s+=String.fromCharCode.apply(null,a.subarray(i,Math.min(i+step,a.length)))}window.__wlJpegQueue.push(\'__WLJPEG797__"

    invoke-direct {v0, v1}, Ljava/lang/StringBuilder;-><init>(Ljava/lang/String;)V

    invoke-virtual {v0, p0}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v0

    const-string v1, "|\'+id+\'|\'+btoa(s));x.dataset.wlJpegState=\'fetched\'}).catch(function(e){x.dataset.wlJpegState=\'fetcherror\';window.__wlJpegQueue.push(\'__WLJPEG797__"

    invoke-virtual {v0, v1}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v0

    invoke-virtual {v0, p0}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object p0

    const-string v0, "|\'+id+\'|!boundary|\'+encodeURIComponent(u)+\'|\'+String(e))})});return \'\'}catch(e){return \'!script \'+String(e)}})()"

    invoke-virtual {p0, v0}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object p0

    invoke-virtual {p0}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;

    move-result-object p0

    return-object p0
.end method

.method private static emit(Ljava/lang/String;)V
    .registers 3

    .line 52
    new-instance v0, Ljava/lang/StringBuilder;

    const-string v1, "[WESTLAKE-WEBCALLBACK-726] "

    invoke-direct {v0, v1}, Ljava/lang/StringBuilder;-><init>(Ljava/lang/String;)V

    invoke-virtual {v0, p0}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object p0

    invoke-virtual {p0}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;

    move-result-object p0

    invoke-static {p0}, Lwestlake/WlFix;->emitLine(Ljava/lang/String;)V

    return-void
.end method

.method private static emitLine(Ljava/lang/String;)V
    .registers 4

    .line 37
    :try_start_0
    sget-object v0, Lwestlake/WlFix;->error:Ljava/io/OutputStream;

    if-nez v0, :cond_d

    .line 38
    new-instance v0, Ljava/io/FileOutputStream;

    sget-object v1, Ljava/io/FileDescriptor;->err:Ljava/io/FileDescriptor;

    invoke-direct {v0, v1}, Ljava/io/FileOutputStream;-><init>(Ljava/io/FileDescriptor;)V

    sput-object v0, Lwestlake/WlFix;->error:Ljava/io/OutputStream;

    .line 40
    :cond_d
    sget-object v0, Lwestlake/WlFix;->error:Ljava/io/OutputStream;

    new-instance v1, Ljava/lang/StringBuilder;

    invoke-direct {v1}, Ljava/lang/StringBuilder;-><init>()V

    invoke-virtual {v1, p0}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v1

    const-string v2, "\n"

    invoke-virtual {v1, v2}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v1

    invoke-virtual {v1}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;

    move-result-object v1

    const-string v2, "UTF-8"

    invoke-virtual {v1, v2}, Ljava/lang/String;->getBytes(Ljava/lang/String;)[B

    move-result-object v1

    invoke-virtual {v0, v1}, Ljava/io/OutputStream;->write([B)V

    .line 41
    sget-object v0, Lwestlake/WlFix;->error:Ljava/io/OutputStream;

    invoke-virtual {v0}, Ljava/io/OutputStream;->flush()V
    :try_end_30
    .catchall {:try_start_0 .. :try_end_30} :catchall_31

    goto :goto_3b

    .line 44
    :catchall_31
    :try_start_31
    sget-object v0, Ljava/lang/System;->err:Ljava/io/PrintStream;

    invoke-virtual {v0, p0}, Ljava/io/PrintStream;->println(Ljava/lang/String;)V

    .line 45
    sget-object p0, Ljava/lang/System;->err:Ljava/io/PrintStream;

    invoke-virtual {p0}, Ljava/io/PrintStream;->flush()V
    :try_end_3b
    .catchall {:try_start_31 .. :try_end_3b} :catchall_3b

    :catchall_3b
    :goto_3b
    return-void
.end method

.method private static encodeArgbPng([I)[B
    .registers 12
    .annotation system Ldalvik/annotation/Throws;
        value = {
            Ljava/lang/Exception;
        }
    .end annotation

    if-eqz p0, :cond_e5

    .line 449
    array-length v0, p0

    const/4 v1, 0x3

    if-lt v0, v1, :cond_e5

    const/4 v0, 0x0

    .line 450
    aget v1, p0, v0

    const/4 v2, 0x1

    .line 451
    aget v2, p0, v2

    int-to-long v3, v1

    int-to-long v5, v2

    mul-long/2addr v3, v5

    if-lez v1, :cond_c6

    if-lez v2, :cond_c6

    const-wide/32 v7, 0x1000000

    cmp-long v7, v3, v7

    if-gtz v7, :cond_c6

    .line 453
    array-length v7, p0

    int-to-long v7, v7

    const-wide/16 v9, 0x2

    add-long/2addr v9, v3

    cmp-long v7, v7, v9

    if-ltz v7, :cond_c6

    .line 457
    new-instance v7, Ljava/io/ByteArrayOutputStream;

    const-wide/16 v8, 0x4

    mul-long/2addr v3, v8

    add-long/2addr v3, v5

    const-wide/32 v5, 0x7fffffff

    .line 458
    invoke-static {v5, v6, v3, v4}, Ljava/lang/Math;->min(JJ)J

    move-result-wide v3

    long-to-int v3, v3

    invoke-direct {v7, v3}, Ljava/io/ByteArrayOutputStream;-><init>(I)V

    const/4 v3, 0x2

    move v4, v0

    :goto_36
    if-ge v4, v2, :cond_63

    .line 461
    invoke-virtual {v7, v0}, Ljava/io/ByteArrayOutputStream;->write(I)V

    move v5, v0

    :goto_3c
    if-ge v5, v1, :cond_60

    add-int/lit8 v6, v3, 0x1

    .line 463
    aget v3, p0, v3

    ushr-int/lit8 v8, v3, 0x10

    and-int/lit16 v8, v8, 0xff

    .line 464
    invoke-virtual {v7, v8}, Ljava/io/ByteArrayOutputStream;->write(I)V

    ushr-int/lit8 v8, v3, 0x8

    and-int/lit16 v8, v8, 0xff

    .line 465
    invoke-virtual {v7, v8}, Ljava/io/ByteArrayOutputStream;->write(I)V

    and-int/lit16 v8, v3, 0xff

    .line 466
    invoke-virtual {v7, v8}, Ljava/io/ByteArrayOutputStream;->write(I)V

    ushr-int/lit8 v3, v3, 0x18

    and-int/lit16 v3, v3, 0xff

    .line 467
    invoke-virtual {v7, v3}, Ljava/io/ByteArrayOutputStream;->write(I)V

    add-int/lit8 v5, v5, 0x1

    move v3, v6

    goto :goto_3c

    :cond_60
    add-int/lit8 v4, v4, 0x1

    goto :goto_36

    .line 470
    :cond_63
    new-instance p0, Ljava/io/ByteArrayOutputStream;

    invoke-direct {p0}, Ljava/io/ByteArrayOutputStream;-><init>()V

    .line 471
    new-instance v3, Ljava/util/zip/DeflaterOutputStream;

    invoke-direct {v3, p0}, Ljava/util/zip/DeflaterOutputStream;-><init>(Ljava/io/OutputStream;)V

    .line 472
    invoke-virtual {v7, v3}, Ljava/io/ByteArrayOutputStream;->writeTo(Ljava/io/OutputStream;)V

    .line 473
    invoke-virtual {v3}, Ljava/util/zip/DeflaterOutputStream;->finish()V

    .line 474
    invoke-virtual {v3}, Ljava/util/zip/DeflaterOutputStream;->close()V

    .line 476
    new-instance v3, Ljava/io/ByteArrayOutputStream;

    invoke-virtual {p0}, Ljava/io/ByteArrayOutputStream;->size()I

    move-result v4

    add-int/lit16 v4, v4, 0x80

    invoke-direct {v3, v4}, Ljava/io/ByteArrayOutputStream;-><init>(I)V

    const/16 v4, 0x8

    new-array v5, v4, [B

    .line 477
    fill-array-data v5, :array_ee

    invoke-virtual {v3, v5}, Ljava/io/ByteArrayOutputStream;->write([B)V

    .line 478
    new-instance v5, Ljava/io/ByteArrayOutputStream;

    const/16 v6, 0xd

    invoke-direct {v5, v6}, Ljava/io/ByteArrayOutputStream;-><init>(I)V

    .line 479
    invoke-static {v5, v1}, Lwestlake/WlFix;->writeInt32(Ljava/io/ByteArrayOutputStream;I)V

    .line 480
    invoke-static {v5, v2}, Lwestlake/WlFix;->writeInt32(Ljava/io/ByteArrayOutputStream;I)V

    .line 481
    invoke-virtual {v5, v4}, Ljava/io/ByteArrayOutputStream;->write(I)V

    const/4 v1, 0x6

    .line 482
    invoke-virtual {v5, v1}, Ljava/io/ByteArrayOutputStream;->write(I)V

    .line 483
    invoke-virtual {v5, v0}, Ljava/io/ByteArrayOutputStream;->write(I)V

    .line 484
    invoke-virtual {v5, v0}, Ljava/io/ByteArrayOutputStream;->write(I)V

    .line 485
    invoke-virtual {v5, v0}, Ljava/io/ByteArrayOutputStream;->write(I)V

    const-string v1, "IHDR"

    .line 486
    invoke-virtual {v5}, Ljava/io/ByteArrayOutputStream;->toByteArray()[B

    move-result-object v2

    invoke-static {v3, v1, v2}, Lwestlake/WlFix;->writePngChunk(Ljava/io/ByteArrayOutputStream;Ljava/lang/String;[B)V

    const-string v1, "IDAT"

    .line 487
    invoke-virtual {p0}, Ljava/io/ByteArrayOutputStream;->toByteArray()[B

    move-result-object p0

    invoke-static {v3, v1, p0}, Lwestlake/WlFix;->writePngChunk(Ljava/io/ByteArrayOutputStream;Ljava/lang/String;[B)V

    const-string p0, "IEND"

    new-array v0, v0, [B

    .line 488
    invoke-static {v3, p0, v0}, Lwestlake/WlFix;->writePngChunk(Ljava/io/ByteArrayOutputStream;Ljava/lang/String;[B)V

    .line 489
    invoke-virtual {v3}, Ljava/io/ByteArrayOutputStream;->toByteArray()[B

    move-result-object p0

    return-object p0

    .line 455
    :cond_c6
    new-instance p0, Ljava/lang/Exception;

    new-instance v0, Ljava/lang/StringBuilder;

    const-string v3, "invalid dimensions "

    invoke-direct {v0, v3}, Ljava/lang/StringBuilder;-><init>(Ljava/lang/String;)V

    invoke-virtual {v0, v1}, Ljava/lang/StringBuilder;->append(I)Ljava/lang/StringBuilder;

    move-result-object v0

    const-string v1, "x"

    invoke-virtual {v0, v1}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v0

    invoke-virtual {v0, v2}, Ljava/lang/StringBuilder;->append(I)Ljava/lang/StringBuilder;

    move-result-object v0

    invoke-virtual {v0}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;

    move-result-object v0

    invoke-direct {p0, v0}, Ljava/lang/Exception;-><init>(Ljava/lang/String;)V

    throw p0

    .line 449
    :cond_e5
    new-instance p0, Ljava/lang/Exception;

    const-string v0, "decoder returned null"

    invoke-direct {p0, v0}, Ljava/lang/Exception;-><init>(Ljava/lang/String;)V

    throw p0

    nop

    :array_ee
    .array-data 1
        -0x77t
        0x50t
        0x4et
        0x47t
        0xdt
        0xat
        0x1at
        0xat
    .end array-data
.end method

.method private static evaluateAssetProbe(Ljava/lang/Object;Ljava/lang/String;)V
    .registers 11

    const-string v0, "[WESTLAKE-WEBASSET-793] phase="

    :try_start_2
    const-string v1, "android.webkit.ValueCallback"

    .line 147
    invoke-static {v1}, Ljava/lang/Class;->forName(Ljava/lang/String;)Ljava/lang/Class;

    move-result-object v1

    .line 149
    invoke-virtual {v1}, Ljava/lang/Class;->getClassLoader()Ljava/lang/ClassLoader;

    move-result-object v2

    filled-new-array {v1}, [Ljava/lang/Class;

    move-result-object v3

    new-instance v4, Lwestlake/WlFix$1;

    invoke-direct {v4, p1}, Lwestlake/WlFix$1;-><init>(Ljava/lang/String;)V

    .line 148
    invoke-static {v2, v3, v4}, Ljava/lang/reflect/Proxy;->newProxyInstance(Ljava/lang/ClassLoader;[Ljava/lang/Class;Ljava/lang/reflect/InvocationHandler;)Ljava/lang/Object;

    move-result-object v2

    .line 161
    invoke-virtual {p0}, Ljava/lang/Object;->getClass()Ljava/lang/Class;

    move-result-object v3

    const-string v4, "evaluateJavascript"

    const/4 v5, 0x2

    new-array v6, v5, [Ljava/lang/Class;

    const-class v7, Ljava/lang/String;

    const/4 v8, 0x0

    aput-object v7, v6, v8

    const/4 v7, 0x1

    aput-object v1, v6, v7

    invoke-virtual {v3, v4, v6}, Ljava/lang/Class;->getMethod(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;

    move-result-object v1

    new-array v3, v5, [Ljava/lang/Object;

    .line 163
    invoke-static {p1}, Lwestlake/WlFix;->buildAssetProbeScript(Ljava/lang/String;)Ljava/lang/String;

    move-result-object v4

    aput-object v4, v3, v8

    aput-object v2, v3, v7

    .line 162
    invoke-virtual {v1, p0, v3}, Ljava/lang/reflect/Method;->invoke(Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/Object;

    .line 164
    new-instance v1, Ljava/lang/StringBuilder;

    invoke-direct {v1, v0}, Ljava/lang/StringBuilder;-><init>(Ljava/lang/String;)V

    invoke-virtual {v1, p1}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v1

    const-string v2, " submitted view="

    invoke-virtual {v1, v2}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v1

    .line 165
    invoke-virtual {p0}, Ljava/lang/Object;->getClass()Ljava/lang/Class;

    move-result-object p0

    invoke-virtual {p0}, Ljava/lang/Class;->getName()Ljava/lang/String;

    move-result-object p0

    invoke-virtual {v1, p0}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object p0

    invoke-virtual {p0}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;

    move-result-object p0

    .line 164
    invoke-static {p0}, Lwestlake/WlFix;->emitLine(Ljava/lang/String;)V
    :try_end_5d
    .catchall {:try_start_2 .. :try_end_5d} :catchall_5e

    goto :goto_93

    :catchall_5e
    move-exception p0

    .line 167
    new-instance v1, Ljava/lang/StringBuilder;

    invoke-direct {v1, v0}, Ljava/lang/StringBuilder;-><init>(Ljava/lang/String;)V

    invoke-virtual {v1, p1}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object p1

    const-string v0, " error="

    invoke-virtual {p1, v0}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object p1

    .line 168
    invoke-virtual {p0}, Ljava/lang/Object;->getClass()Ljava/lang/Class;

    move-result-object v0

    invoke-virtual {v0}, Ljava/lang/Class;->getName()Ljava/lang/String;

    move-result-object v0

    invoke-virtual {p1, v0}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object p1

    const-string v0, ":"

    invoke-virtual {p1, v0}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object p1

    invoke-virtual {p0}, Ljava/lang/Throwable;->getMessage()Ljava/lang/String;

    move-result-object p0

    invoke-static {p0}, Ljava/lang/String;->valueOf(Ljava/lang/Object;)Ljava/lang/String;

    move-result-object p0

    invoke-virtual {p1, p0}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object p0

    invoke-virtual {p0}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;

    move-result-object p0

    .line 167
    invoke-static {p0}, Lwestlake/WlFix;->emitLine(Ljava/lang/String;)V

    :goto_93
    return-void
.end method

.method private static evaluateNativeFallbackDiagnostic(Ljava/lang/Object;)V
    .registers 9

    :try_start_0
    const-string v0, "android.webkit.ValueCallback"

    .line 187
    invoke-static {v0}, Ljava/lang/Class;->forName(Ljava/lang/String;)Ljava/lang/Class;

    move-result-object v0

    .line 189
    invoke-virtual {v0}, Ljava/lang/Class;->getClassLoader()Ljava/lang/ClassLoader;

    move-result-object v1

    filled-new-array {v0}, [Ljava/lang/Class;

    move-result-object v2

    new-instance v3, Lwestlake/WlFix$2;

    invoke-direct {v3}, Lwestlake/WlFix$2;-><init>()V

    .line 188
    invoke-static {v1, v2, v3}, Ljava/lang/reflect/Proxy;->newProxyInstance(Ljava/lang/ClassLoader;[Ljava/lang/Class;Ljava/lang/reflect/InvocationHandler;)Ljava/lang/Object;

    move-result-object v1

    .line 200
    invoke-virtual {p0}, Ljava/lang/Object;->getClass()Ljava/lang/Class;

    move-result-object v2

    const-string v3, "evaluateJavascript"

    const/4 v4, 0x2

    new-array v5, v4, [Ljava/lang/Class;

    const-class v6, Ljava/lang/String;

    const/4 v7, 0x0

    aput-object v6, v5, v7

    const/4 v6, 0x1

    aput-object v0, v5, v6

    invoke-virtual {v2, v3, v5}, Ljava/lang/Class;->getMethod(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;

    move-result-object v0

    new-array v2, v4, [Ljava/lang/Object;

    const-string v3, "(function(){try{var a=[].slice.call(document.querySelectorAll(\'native-image\'));var before=a.map(function(x){return {id:x._id,state:x.state,counter:x.counter,isInserted:x.isInserted,src:x.getAttribute(\'src\')}});a.forEach(function(x){x.dispatchEvent(new CustomEvent(\'inserterror\',{detail:{result:-1,westlakeDiagnostic:true}}))});return JSON.stringify({dispatched:a.length,before:before})}catch(e){return JSON.stringify({error:String(e),stack:String(e&&e.stack)})}})()"

    aput-object v3, v2, v7

    aput-object v1, v2, v6

    .line 201
    invoke-virtual {v0, p0, v2}, Ljava/lang/reflect/Method;->invoke(Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/Object;

    const-string p0, "[WESTLAKE-WEBNATIVE-794] submitted"

    .line 203
    invoke-static {p0}, Lwestlake/WlFix;->emitLine(Ljava/lang/String;)V
    :try_end_3c
    .catchall {:try_start_0 .. :try_end_3c} :catchall_3d

    goto :goto_6a

    :catchall_3d
    move-exception p0

    .line 205
    new-instance v0, Ljava/lang/StringBuilder;

    const-string v1, "[WESTLAKE-WEBNATIVE-794] error="

    invoke-direct {v0, v1}, Ljava/lang/StringBuilder;-><init>(Ljava/lang/String;)V

    invoke-virtual {p0}, Ljava/lang/Object;->getClass()Ljava/lang/Class;

    move-result-object v1

    invoke-virtual {v1}, Ljava/lang/Class;->getName()Ljava/lang/String;

    move-result-object v1

    invoke-virtual {v0, v1}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v0

    const-string v1, ":"

    invoke-virtual {v0, v1}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v0

    .line 206
    invoke-virtual {p0}, Ljava/lang/Throwable;->getMessage()Ljava/lang/String;

    move-result-object p0

    invoke-static {p0}, Ljava/lang/String;->valueOf(Ljava/lang/Object;)Ljava/lang/String;

    move-result-object p0

    invoke-virtual {v0, p0}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object p0

    invoke-virtual {p0}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;

    move-result-object p0

    .line 205
    invoke-static {p0}, Lwestlake/WlFix;->emitLine(Ljava/lang/String;)V

    :goto_6a
    return-void
.end method

.method private static findWebViewForAwContents(Ljava/lang/Object;)Ljava/lang/Object;
    .registers 9
    .annotation system Ldalvik/annotation/Throws;
        value = {
            Ljava/lang/Exception;
        }
    .end annotation

    const-string v0, "android.view.WindowManagerGlobal"

    .line 770
    invoke-static {v0}, Ljava/lang/Class;->forName(Ljava/lang/String;)Ljava/lang/Class;

    move-result-object v0

    const/4 v1, 0x0

    new-array v2, v1, [Ljava/lang/Class;

    const-string v3, "getInstance"

    .line 771
    invoke-virtual {v0, v3, v2}, Ljava/lang/Class;->getMethod(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;

    move-result-object v2

    new-array v3, v1, [Ljava/lang/Object;

    const/4 v4, 0x0

    invoke-virtual {v2, v4, v3}, Ljava/lang/reflect/Method;->invoke(Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/Object;

    move-result-object v2

    const-string v3, "mRoots"

    .line 772
    invoke-virtual {v0, v3}, Ljava/lang/Class;->getDeclaredField(Ljava/lang/String;)Ljava/lang/reflect/Field;

    move-result-object v0

    const/4 v3, 0x1

    .line 773
    invoke-virtual {v0, v3}, Ljava/lang/reflect/Field;->setAccessible(Z)V

    .line 774
    invoke-virtual {v0, v2}, Ljava/lang/reflect/Field;->get(Ljava/lang/Object;)Ljava/lang/Object;

    move-result-object v0

    check-cast v0, Ljava/util/ArrayList;

    .line 775
    invoke-virtual {v0}, Ljava/util/ArrayList;->size()I

    move-result v2

    sub-int/2addr v2, v3

    :goto_2b
    if-ltz v2, :cond_4c

    .line 776
    invoke-virtual {v0, v2}, Ljava/util/ArrayList;->get(I)Ljava/lang/Object;

    move-result-object v5

    .line 777
    invoke-virtual {v5}, Ljava/lang/Object;->getClass()Ljava/lang/Class;

    move-result-object v6

    const-string v7, "mView"

    invoke-virtual {v6, v7}, Ljava/lang/Class;->getDeclaredField(Ljava/lang/String;)Ljava/lang/reflect/Field;

    move-result-object v6

    .line 778
    invoke-virtual {v6, v3}, Ljava/lang/reflect/Field;->setAccessible(Z)V

    .line 780
    invoke-virtual {v6, v5}, Ljava/lang/reflect/Field;->get(Ljava/lang/Object;)Ljava/lang/Object;

    move-result-object v5

    .line 779
    invoke-static {v5, p0, v1}, Lwestlake/WlFix;->findWebViewForAwContentsInTree(Ljava/lang/Object;Ljava/lang/Object;I)Ljava/lang/Object;

    move-result-object v5

    if-eqz v5, :cond_49

    return-object v5

    :cond_49
    add-int/lit8 v2, v2, -0x1

    goto :goto_2b

    :cond_4c
    return-object v4
.end method

.method private static findWebViewForAwContentsInTree(Ljava/lang/Object;Ljava/lang/Object;I)Ljava/lang/Object;
    .registers 11
    .annotation system Ldalvik/annotation/Throws;
        value = {
            Ljava/lang/Exception;
        }
    .end annotation

    const/4 v0, 0x0

    if-eqz p0, :cond_af

    const/16 v1, 0x40

    if-le p2, v1, :cond_9

    goto/16 :goto_af

    :cond_9
    const-string v1, "android.webkit.WebView"

    .line 789
    invoke-static {v1}, Ljava/lang/Class;->forName(Ljava/lang/String;)Ljava/lang/Class;

    move-result-object v1

    .line 790
    invoke-virtual {v1, p0}, Ljava/lang/Class;->isInstance(Ljava/lang/Object;)Z

    move-result v2

    const/4 v3, 0x1

    const/4 v4, 0x0

    if-eqz v2, :cond_67

    sget-object v2, Ljava/lang/Boolean;->TRUE:Ljava/lang/Boolean;

    .line 791
    invoke-virtual {p0}, Ljava/lang/Object;->getClass()Ljava/lang/Class;

    move-result-object v5

    const-string v6, "isShown"

    new-array v7, v4, [Ljava/lang/Class;

    invoke-virtual {v5, v6, v7}, Ljava/lang/Class;->getMethod(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;

    move-result-object v5

    new-array v6, v4, [Ljava/lang/Object;

    invoke-virtual {v5, p0, v6}, Ljava/lang/reflect/Method;->invoke(Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/Object;

    move-result-object v5

    invoke-virtual {v2, v5}, Ljava/lang/Boolean;->equals(Ljava/lang/Object;)Z

    move-result v2

    if-eqz v2, :cond_67

    :try_start_31
    const-string v2, "mProvider"

    .line 793
    invoke-virtual {v1, v2}, Ljava/lang/Class;->getDeclaredField(Ljava/lang/String;)Ljava/lang/reflect/Field;

    move-result-object v1

    .line 794
    invoke-virtual {v1, v3}, Ljava/lang/reflect/Field;->setAccessible(Z)V

    .line 795
    invoke-virtual {v1, p0}, Ljava/lang/reflect/Field;->get(Ljava/lang/Object;)Ljava/lang/Object;

    move-result-object v1
    :try_end_3e
    .catchall {:try_start_31 .. :try_end_3e} :catchall_67

    .line 798
    :try_start_3e
    invoke-virtual {v1}, Ljava/lang/Object;->getClass()Ljava/lang/Class;

    move-result-object v2

    const-string v5, "getRealWebViewProvider"

    new-array v6, v4, [Ljava/lang/Class;

    invoke-virtual {v2, v5, v6}, Ljava/lang/Class;->getDeclaredMethod(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;

    move-result-object v2

    .line 800
    invoke-virtual {v2, v3}, Ljava/lang/reflect/Method;->setAccessible(Z)V

    new-array v5, v4, [Ljava/lang/Object;

    .line 801
    invoke-virtual {v2, v1, v5}, Ljava/lang/reflect/Method;->invoke(Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/Object;

    move-result-object v1
    :try_end_53
    .catchall {:try_start_3e .. :try_end_53} :catchall_53

    .line 804
    :catchall_53
    :try_start_53
    invoke-virtual {v1}, Ljava/lang/Object;->getClass()Ljava/lang/Class;

    move-result-object v2

    const-string v5, "f"

    invoke-virtual {v2, v5}, Ljava/lang/Class;->getDeclaredField(Ljava/lang/String;)Ljava/lang/reflect/Field;

    move-result-object v2

    .line 805
    invoke-virtual {v2, v3}, Ljava/lang/reflect/Field;->setAccessible(Z)V

    .line 806
    invoke-virtual {v2, v1}, Ljava/lang/reflect/Field;->get(Ljava/lang/Object;)Ljava/lang/Object;

    move-result-object v1
    :try_end_64
    .catchall {:try_start_53 .. :try_end_64} :catchall_67

    if-ne v1, p1, :cond_67

    return-object p0

    :catchall_67
    :cond_67
    const-string v1, "android.view.ViewGroup"

    .line 810
    invoke-static {v1}, Ljava/lang/Class;->forName(Ljava/lang/String;)Ljava/lang/Class;

    move-result-object v1

    .line 811
    invoke-virtual {v1, p0}, Ljava/lang/Class;->isInstance(Ljava/lang/Object;)Z

    move-result v2

    if-nez v2, :cond_74

    return-object v0

    :cond_74
    const-string v2, "getChildCount"

    new-array v5, v4, [Ljava/lang/Class;

    .line 812
    invoke-virtual {v1, v2, v5}, Ljava/lang/Class;->getMethod(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;

    move-result-object v2

    new-array v4, v4, [Ljava/lang/Object;

    invoke-virtual {v2, p0, v4}, Ljava/lang/reflect/Method;->invoke(Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/Object;

    move-result-object v2

    check-cast v2, Ljava/lang/Integer;

    invoke-virtual {v2}, Ljava/lang/Integer;->intValue()I

    move-result v2

    .line 813
    sget-object v4, Ljava/lang/Integer;->TYPE:Ljava/lang/Class;

    filled-new-array {v4}, [Ljava/lang/Class;

    move-result-object v4

    const-string v5, "getChildAt"

    invoke-virtual {v1, v5, v4}, Ljava/lang/Class;->getMethod(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;

    move-result-object v1

    sub-int/2addr v2, v3

    :goto_95
    if-ltz v2, :cond_af

    .line 816
    invoke-static {v2}, Ljava/lang/Integer;->valueOf(I)Ljava/lang/Integer;

    move-result-object v4

    filled-new-array {v4}, [Ljava/lang/Object;

    move-result-object v4

    invoke-virtual {v1, p0, v4}, Ljava/lang/reflect/Method;->invoke(Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/Object;

    move-result-object v4

    add-int/lit8 v5, p2, 0x1

    .line 815
    invoke-static {v4, p1, v5}, Lwestlake/WlFix;->findWebViewForAwContentsInTree(Ljava/lang/Object;Ljava/lang/Object;I)Ljava/lang/Object;

    move-result-object v4

    if-eqz v4, :cond_ac

    return-object v4

    :cond_ac
    add-int/lit8 v2, v2, -0x1

    goto :goto_95

    :cond_af
    :goto_af
    return-object v0
.end method

.method private static handleJpegResultMessage(Ljava/lang/String;)V
    .registers 11

    const-string v0, "[WESTLAKE-WEBJPEG-797] renderer-fetch-error id="

    const-string v1, "[WESTLAKE-WEBJPEG-797] fetch-error id="

    const-string v2, "[WESTLAKE-WEBJPEG-797] fetched id="

    const-string v3, "[WESTLAKE-WEBJPEG-797] owner-missing token="

    const/16 v4, 0x7c

    .line 373
    :try_start_a
    invoke-virtual {p0, v4}, Ljava/lang/String;->indexOf(I)I

    move-result v5

    if-gez v5, :cond_12

    const/4 v6, -0x1

    goto :goto_18

    :cond_12
    add-int/lit8 v6, v5, 0x1

    .line 374
    invoke-virtual {p0, v4, v6}, Ljava/lang/String;->indexOf(II)I

    move-result v6

    :goto_18
    if-ltz v5, :cond_f5

    if-gez v6, :cond_1e

    goto/16 :goto_f5

    :cond_1e
    const/16 v7, 0xd

    .line 376
    invoke-virtual {p0, v7, v5}, Ljava/lang/String;->substring(II)Ljava/lang/String;

    move-result-object v7

    const/4 v8, 0x1

    add-int/2addr v5, v8

    .line 377
    invoke-virtual {p0, v5, v6}, Ljava/lang/String;->substring(II)Ljava/lang/String;

    move-result-object v5

    add-int/2addr v6, v8

    .line 378
    invoke-virtual {p0, v6}, Ljava/lang/String;->substring(I)Ljava/lang/String;

    move-result-object p0

    .line 380
    const-class v6, Lwestlake/WlFix;

    monitor-enter v6
    :try_end_32
    .catchall {:try_start_a .. :try_end_32} :catchall_f6

    .line 381
    :try_start_32
    sget-object v9, Lwestlake/WlFix;->jpegRepairOwners:Ljava/util/HashMap;

    invoke-virtual {v9, v7}, Ljava/util/HashMap;->get(Ljava/lang/Object;)Ljava/lang/Object;

    move-result-object v9

    .line 382
    monitor-exit v6
    :try_end_39
    .catchall {:try_start_32 .. :try_end_39} :catchall_f2

    if-nez v9, :cond_4c

    .line 384
    :try_start_3b
    new-instance p0, Ljava/lang/StringBuilder;

    invoke-direct {p0, v3}, Ljava/lang/StringBuilder;-><init>(Ljava/lang/String;)V

    invoke-virtual {p0, v7}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object p0

    invoke-virtual {p0}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;

    move-result-object p0

    invoke-static {p0}, Lwestlake/WlFix;->emitLine(Ljava/lang/String;)V

    return-void

    :cond_4c
    const-string v3, "!"

    .line 387
    invoke-virtual {p0, v3}, Ljava/lang/String;->startsWith(Ljava/lang/String;)Z

    move-result v3

    if-eqz v3, :cond_cb

    const-string v2, "!boundary|"

    .line 388
    invoke-virtual {p0, v2}, Ljava/lang/String;->startsWith(Ljava/lang/String;)Z

    move-result v2

    if-eqz v2, :cond_ac

    const/16 v1, 0xa

    .line 389
    invoke-virtual {p0, v4, v1}, Ljava/lang/String;->indexOf(II)I

    move-result v2

    if-gez v2, :cond_69

    .line 391
    invoke-virtual {p0, v1}, Ljava/lang/String;->substring(I)Ljava/lang/String;

    move-result-object p0

    goto :goto_6d

    .line 392
    :cond_69
    invoke-virtual {p0, v1, v2}, Ljava/lang/String;->substring(II)Ljava/lang/String;

    move-result-object p0

    :goto_6d
    const-string v1, "UTF-8"

    .line 393
    invoke-static {p0, v1}, Ljava/net/URLDecoder;->decode(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;

    move-result-object p0

    .line 394
    new-instance v1, Ljava/lang/StringBuilder;

    invoke-direct {v1, v0}, Ljava/lang/StringBuilder;-><init>(Ljava/lang/String;)V

    invoke-virtual {v1, v5}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v0

    const-string v1, " boundary-retry url="

    invoke-virtual {v0, v1}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v0

    .line 396
    invoke-virtual {p0}, Ljava/lang/String;->length()I

    move-result v1

    const/16 v2, 0xb4

    invoke-static {v2, v1}, Ljava/lang/Math;->min(II)I

    move-result v1

    const/4 v2, 0x0

    invoke-virtual {p0, v2, v1}, Ljava/lang/String;->substring(II)Ljava/lang/String;

    move-result-object v1

    invoke-virtual {v0, v1}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v0

    invoke-virtual {v0}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;

    move-result-object v0

    .line 394
    invoke-static {v0}, Lwestlake/WlFix;->emitLine(Ljava/lang/String;)V

    .line 397
    new-instance v0, Ljava/lang/Thread;

    new-instance v1, Lwestlake/WlFix$JpegBoundaryNetworkTask;

    invoke-direct {v1, v9, v5, p0}, Lwestlake/WlFix$JpegBoundaryNetworkTask;-><init>(Ljava/lang/Object;Ljava/lang/String;Ljava/lang/String;)V

    const-string p0, "wl-webjpeg-net"

    invoke-direct {v0, v1, p0}, Ljava/lang/Thread;-><init>(Ljava/lang/Runnable;Ljava/lang/String;)V

    .line 398
    invoke-virtual {v0}, Ljava/lang/Thread;->start()V

    return-void

    .line 401
    :cond_ac
    new-instance v0, Ljava/lang/StringBuilder;

    invoke-direct {v0, v1}, Ljava/lang/StringBuilder;-><init>(Ljava/lang/String;)V

    invoke-virtual {v0, v5}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v0

    const-string v1, " "

    invoke-virtual {v0, v1}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v0

    .line 402
    invoke-virtual {p0, v8}, Ljava/lang/String;->substring(I)Ljava/lang/String;

    move-result-object p0

    invoke-virtual {v0, p0}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object p0

    invoke-virtual {p0}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;

    move-result-object p0

    .line 401
    invoke-static {p0}, Lwestlake/WlFix;->emitLine(Ljava/lang/String;)V

    return-void

    .line 405
    :cond_cb
    invoke-static {}, Ljava/util/Base64;->getDecoder()Ljava/util/Base64$Decoder;

    move-result-object v0

    invoke-virtual {v0, p0}, Ljava/util/Base64$Decoder;->decode(Ljava/lang/String;)[B

    move-result-object p0

    .line 406
    new-instance v0, Ljava/lang/StringBuilder;

    invoke-direct {v0, v2}, Ljava/lang/StringBuilder;-><init>(Ljava/lang/String;)V

    invoke-virtual {v0, v5}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v0

    const-string v1, " bytes="

    invoke-virtual {v0, v1}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v0

    array-length v1, p0

    invoke-virtual {v0, v1}, Ljava/lang/StringBuilder;->append(I)Ljava/lang/StringBuilder;

    move-result-object v0

    invoke-virtual {v0}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;

    move-result-object v0

    invoke-static {v0}, Lwestlake/WlFix;->emitLine(Ljava/lang/String;)V

    .line 408
    invoke-static {v9, v5, p0}, Lwestlake/WlFix;->replaceJpegWithPng(Ljava/lang/Object;Ljava/lang/String;[B)V
    :try_end_f1
    .catchall {:try_start_3b .. :try_end_f1} :catchall_f6

    goto :goto_123

    :catchall_f2
    move-exception p0

    .line 382
    :try_start_f3
    monitor-exit v6
    :try_end_f4
    .catchall {:try_start_f3 .. :try_end_f4} :catchall_f2

    :try_start_f4
    throw p0
    :try_end_f5
    .catchall {:try_start_f4 .. :try_end_f5} :catchall_f6

    :cond_f5
    :goto_f5
    return-void

    :catchall_f6
    move-exception p0

    .line 410
    new-instance v0, Ljava/lang/StringBuilder;

    const-string v1, "[WESTLAKE-WEBJPEG-797] message-error "

    invoke-direct {v0, v1}, Ljava/lang/StringBuilder;-><init>(Ljava/lang/String;)V

    .line 411
    invoke-virtual {p0}, Ljava/lang/Object;->getClass()Ljava/lang/Class;

    move-result-object v1

    invoke-virtual {v1}, Ljava/lang/Class;->getName()Ljava/lang/String;

    move-result-object v1

    invoke-virtual {v0, v1}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v0

    const-string v1, ":"

    invoke-virtual {v0, v1}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v0

    .line 412
    invoke-virtual {p0}, Ljava/lang/Throwable;->getMessage()Ljava/lang/String;

    move-result-object p0

    invoke-static {p0}, Ljava/lang/String;->valueOf(Ljava/lang/Object;)Ljava/lang/String;

    move-result-object p0

    invoke-virtual {v0, p0}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object p0

    invoke-virtual {p0}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;

    move-result-object p0

    .line 410
    invoke-static {p0}, Lwestlake/WlFix;->emitLine(Ljava/lang/String;)V

    :goto_123
    return-void
.end method

.method private static replaceJpegWithPng(Ljava/lang/Object;Ljava/lang/String;[B)V
    .registers 5

    .line 530
    new-instance v0, Ljava/lang/Thread;

    new-instance v1, Lwestlake/WlFix$6;

    invoke-direct {v1, p2, p0, p1}, Lwestlake/WlFix$6;-><init>([BLjava/lang/Object;Ljava/lang/String;)V

    const-string p0, "wl-webjpeg"

    invoke-direct {v0, v1, p0}, Ljava/lang/Thread;-><init>(Ljava/lang/Runnable;Ljava/lang/String;)V

    .line 545
    invoke-virtual {v0}, Ljava/lang/Thread;->start()V

    return-void
.end method

.method private static scheduleAssetProbe(Ljava/lang/Object;)V
    .registers 10

    const-string v0, "[WESTLAKE-WEBASSET-793] ui-probe-scheduled thread="

    .line 279
    const-class v1, Lwestlake/WlFix;

    monitor-enter v1

    .line 280
    :try_start_5
    sget-object v2, Lwestlake/WlFix;->assetProbeSubmitted:Ljava/util/IdentityHashMap;

    invoke-virtual {v2, p0}, Ljava/util/IdentityHashMap;->containsKey(Ljava/lang/Object;)Z

    move-result v2

    if-nez v2, :cond_c8

    sget-object v2, Lwestlake/WlFix;->assetProbeScheduled:Ljava/util/IdentityHashMap;

    .line 281
    invoke-virtual {v2, p0}, Ljava/util/IdentityHashMap;->containsKey(Ljava/lang/Object;)Z

    move-result v3

    if-eqz v3, :cond_17

    goto/16 :goto_c8

    .line 282
    :cond_17
    sget-object v3, Ljava/lang/Boolean;->TRUE:Ljava/lang/Boolean;

    invoke-virtual {v2, p0, v3}, Ljava/util/IdentityHashMap;->put(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;

    .line 283
    monitor-exit v1
    :try_end_1d
    .catchall {:try_start_5 .. :try_end_1d} :catchall_ca

    :try_start_1d
    const-string v1, "android.os.Looper"

    .line 285
    invoke-static {v1}, Ljava/lang/Class;->forName(Ljava/lang/String;)Ljava/lang/Class;

    move-result-object v1

    const-string v2, "getMainLooper"

    const/4 v3, 0x0

    new-array v4, v3, [Ljava/lang/Class;

    .line 286
    invoke-virtual {v1, v2, v4}, Ljava/lang/Class;->getMethod(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;

    move-result-object v2

    new-array v4, v3, [Ljava/lang/Object;

    const/4 v5, 0x0

    invoke-virtual {v2, v5, v4}, Ljava/lang/reflect/Method;->invoke(Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/Object;

    move-result-object v2

    const-string v4, "android.os.Handler"

    .line 287
    invoke-static {v4}, Ljava/lang/Class;->forName(Ljava/lang/String;)Ljava/lang/Class;

    move-result-object v4

    .line 288
    filled-new-array {v1}, [Ljava/lang/Class;

    move-result-object v1

    invoke-virtual {v4, v1}, Ljava/lang/Class;->getConstructor([Ljava/lang/Class;)Ljava/lang/reflect/Constructor;

    move-result-object v1

    filled-new-array {v2}, [Ljava/lang/Object;

    move-result-object v2

    invoke-virtual {v1, v2}, Ljava/lang/reflect/Constructor;->newInstance([Ljava/lang/Object;)Ljava/lang/Object;

    move-result-object v1

    const-string v2, "postDelayed"

    const/4 v5, 0x2

    new-array v6, v5, [Ljava/lang/Class;

    .line 289
    const-class v7, Ljava/lang/Runnable;

    aput-object v7, v6, v3

    sget-object v7, Ljava/lang/Long;->TYPE:Ljava/lang/Class;

    const/4 v8, 0x1

    aput-object v7, v6, v8

    invoke-virtual {v4, v2, v6}, Ljava/lang/Class;->getMethod(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;

    move-result-object v2

    new-array v4, v8, [Ljava/lang/Runnable;

    .line 292
    new-instance v6, Lwestlake/WlFix$5;

    invoke-direct {v6, p0, v2, v1, v4}, Lwestlake/WlFix$5;-><init>(Ljava/lang/Object;Ljava/lang/reflect/Method;Ljava/lang/Object;[Ljava/lang/Runnable;)V

    aput-object v6, v4, v3

    new-array v5, v5, [Ljava/lang/Object;

    aget-object v4, v4, v3

    aput-object v4, v5, v3

    const-wide/16 v3, 0x3e8

    .line 318
    invoke-static {v3, v4}, Ljava/lang/Long;->valueOf(J)Ljava/lang/Long;

    move-result-object v3

    aput-object v3, v5, v8

    invoke-virtual {v2, v1, v5}, Ljava/lang/reflect/Method;->invoke(Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/Object;

    .line 319
    new-instance v1, Ljava/lang/StringBuilder;

    invoke-direct {v1, v0}, Ljava/lang/StringBuilder;-><init>(Ljava/lang/String;)V

    .line 320
    invoke-static {}, Ljava/lang/Thread;->currentThread()Ljava/lang/Thread;

    move-result-object v0

    invoke-virtual {v0}, Ljava/lang/Thread;->getName()Ljava/lang/String;

    move-result-object v0

    invoke-virtual {v1, v0}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v0

    invoke-virtual {v0}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;

    move-result-object v0

    .line 319
    invoke-static {v0}, Lwestlake/WlFix;->emitLine(Ljava/lang/String;)V
    :try_end_8d
    .catchall {:try_start_1d .. :try_end_8d} :catchall_8e

    goto :goto_c4

    :catchall_8e
    move-exception v0

    .line 322
    const-class v2, Lwestlake/WlFix;

    monitor-enter v2

    .line 323
    :try_start_92
    sget-object v1, Lwestlake/WlFix;->assetProbeScheduled:Ljava/util/IdentityHashMap;

    invoke-virtual {v1, p0}, Ljava/util/IdentityHashMap;->remove(Ljava/lang/Object;)Ljava/lang/Object;

    .line 324
    monitor-exit v2
    :try_end_98
    .catchall {:try_start_92 .. :try_end_98} :catchall_c5

    .line 325
    new-instance p0, Ljava/lang/StringBuilder;

    const-string v1, "[WESTLAKE-WEBASSET-793] schedule-error="

    invoke-direct {p0, v1}, Ljava/lang/StringBuilder;-><init>(Ljava/lang/String;)V

    .line 326
    invoke-virtual {v0}, Ljava/lang/Object;->getClass()Ljava/lang/Class;

    move-result-object v1

    invoke-virtual {v1}, Ljava/lang/Class;->getName()Ljava/lang/String;

    move-result-object v1

    invoke-virtual {p0, v1}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object p0

    const-string v1, ":"

    invoke-virtual {p0, v1}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object p0

    .line 327
    invoke-virtual {v0}, Ljava/lang/Throwable;->getMessage()Ljava/lang/String;

    move-result-object v0

    invoke-static {v0}, Ljava/lang/String;->valueOf(Ljava/lang/Object;)Ljava/lang/String;

    move-result-object v0

    invoke-virtual {p0, v0}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object p0

    invoke-virtual {p0}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;

    move-result-object p0

    .line 325
    invoke-static {p0}, Lwestlake/WlFix;->emitLine(Ljava/lang/String;)V

    :goto_c4
    return-void

    :catchall_c5
    move-exception p0

    .line 324
    :try_start_c6
    monitor-exit v2
    :try_end_c7
    .catchall {:try_start_c6 .. :try_end_c7} :catchall_c5

    throw p0

    .line 281
    :cond_c8
    :goto_c8
    :try_start_c8
    monitor-exit v1

    return-void

    :catchall_ca
    move-exception p0

    .line 283
    monitor-exit v1
    :try_end_cc
    .catchall {:try_start_c8 .. :try_end_cc} :catchall_ca

    throw p0
.end method

.method private static scheduleEvaluation(Ljava/lang/Object;Ljava/lang/String;J)V
    .registers 7
    .annotation system Ldalvik/annotation/Throws;
        value = {
            Ljava/lang/Exception;
        }
    .end annotation

    .line 263
    invoke-virtual {p0}, Ljava/lang/Object;->getClass()Ljava/lang/Class;

    move-result-object v0

    const-class v1, Ljava/lang/Runnable;

    sget-object v2, Ljava/lang/Long;->TYPE:Ljava/lang/Class;

    filled-new-array {v1, v2}, [Ljava/lang/Class;

    move-result-object v1

    const-string v2, "postDelayed"

    invoke-virtual {v0, v2, v1}, Ljava/lang/Class;->getMethod(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;

    move-result-object v0

    new-instance v1, Lwestlake/WlFix$4;

    invoke-direct {v1, p0, p1}, Lwestlake/WlFix$4;-><init>(Ljava/lang/Object;Ljava/lang/String;)V

    .line 268
    invoke-static {p2, p3}, Ljava/lang/Long;->valueOf(J)Ljava/lang/Long;

    move-result-object v2

    filled-new-array {v1, v2}, [Ljava/lang/Object;

    move-result-object v1

    .line 263
    invoke-virtual {v0, p0, v1}, Ljava/lang/reflect/Method;->invoke(Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/Object;

    .line 269
    new-instance p0, Ljava/lang/StringBuilder;

    const-string v0, "[WESTLAKE-WEBASSET-793] phase="

    invoke-direct {p0, v0}, Ljava/lang/StringBuilder;-><init>(Ljava/lang/String;)V

    invoke-virtual {p0, p1}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object p0

    const-string p1, " scheduled delayMs="

    invoke-virtual {p0, p1}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object p0

    invoke-virtual {p0, p2, p3}, Ljava/lang/StringBuilder;->append(J)Ljava/lang/StringBuilder;

    move-result-object p0

    invoke-virtual {p0}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;

    move-result-object p0

    invoke-static {p0}, Lwestlake/WlFix;->emitLine(Ljava/lang/String;)V

    return-void
.end method

.method private static scheduleJpegRepair(Ljava/lang/Object;)V
    .registers 9

    .line 713
    const-class v0, Lwestlake/WlFix;

    monitor-enter v0

    .line 714
    :try_start_3
    sget-object v1, Lwestlake/WlFix;->jpegRepairStarted:Ljava/util/IdentityHashMap;

    invoke-virtual {v1, p0}, Ljava/util/IdentityHashMap;->containsKey(Ljava/lang/Object;)Z

    move-result v1

    if-nez v1, :cond_ae

    sget-object v1, Lwestlake/WlFix;->jpegRepairScheduled:Ljava/util/IdentityHashMap;

    .line 715
    invoke-virtual {v1, p0}, Ljava/util/IdentityHashMap;->containsKey(Ljava/lang/Object;)Z

    move-result v2

    if-eqz v2, :cond_15

    goto/16 :goto_ae

    .line 716
    :cond_15
    sget-object v2, Ljava/lang/Boolean;->TRUE:Ljava/lang/Boolean;

    invoke-virtual {v1, p0, v2}, Ljava/util/IdentityHashMap;->put(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;

    .line 717
    monitor-exit v0
    :try_end_1b
    .catchall {:try_start_3 .. :try_end_1b} :catchall_b0

    :try_start_1b
    const-string v0, "android.os.Looper"

    .line 719
    invoke-static {v0}, Ljava/lang/Class;->forName(Ljava/lang/String;)Ljava/lang/Class;

    move-result-object v0

    const-string v1, "getMainLooper"

    const/4 v2, 0x0

    new-array v3, v2, [Ljava/lang/Class;

    .line 720
    invoke-virtual {v0, v1, v3}, Ljava/lang/Class;->getMethod(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;

    move-result-object v1

    new-array v3, v2, [Ljava/lang/Object;

    const/4 v4, 0x0

    invoke-virtual {v1, v4, v3}, Ljava/lang/reflect/Method;->invoke(Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/Object;

    move-result-object v1

    const-string v3, "android.os.Handler"

    .line 721
    invoke-static {v3}, Ljava/lang/Class;->forName(Ljava/lang/String;)Ljava/lang/Class;

    move-result-object v3

    .line 722
    filled-new-array {v0}, [Ljava/lang/Class;

    move-result-object v0

    invoke-virtual {v3, v0}, Ljava/lang/Class;->getConstructor([Ljava/lang/Class;)Ljava/lang/reflect/Constructor;

    move-result-object v0

    filled-new-array {v1}, [Ljava/lang/Object;

    move-result-object v1

    invoke-virtual {v0, v1}, Ljava/lang/reflect/Constructor;->newInstance([Ljava/lang/Object;)Ljava/lang/Object;

    move-result-object v0

    const-string v1, "postDelayed"

    const/4 v4, 0x2

    new-array v5, v4, [Ljava/lang/Class;

    .line 723
    const-class v6, Ljava/lang/Runnable;

    aput-object v6, v5, v2

    sget-object v6, Ljava/lang/Long;->TYPE:Ljava/lang/Class;

    const/4 v7, 0x1

    aput-object v6, v5, v7

    invoke-virtual {v3, v1, v5}, Ljava/lang/Class;->getMethod(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;

    move-result-object v1

    new-array v3, v7, [Ljava/lang/Runnable;

    .line 726
    new-instance v5, Lwestlake/WlFix$7;

    invoke-direct {v5, p0, v1, v0, v3}, Lwestlake/WlFix$7;-><init>(Ljava/lang/Object;Ljava/lang/reflect/Method;Ljava/lang/Object;[Ljava/lang/Runnable;)V

    aput-object v5, v3, v2

    new-array v4, v4, [Ljava/lang/Object;

    aget-object v3, v3, v2

    aput-object v3, v4, v2

    const-wide/16 v2, 0x3e8

    .line 758
    invoke-static {v2, v3}, Ljava/lang/Long;->valueOf(J)Ljava/lang/Long;

    move-result-object v2

    aput-object v2, v4, v7

    invoke-virtual {v1, v0, v4}, Ljava/lang/reflect/Method;->invoke(Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/Object;
    :try_end_73
    .catchall {:try_start_1b .. :try_end_73} :catchall_74

    goto :goto_aa

    :catchall_74
    move-exception v0

    .line 760
    const-class v1, Lwestlake/WlFix;

    monitor-enter v1

    .line 761
    :try_start_78
    sget-object v2, Lwestlake/WlFix;->jpegRepairScheduled:Ljava/util/IdentityHashMap;

    invoke-virtual {v2, p0}, Ljava/util/IdentityHashMap;->remove(Ljava/lang/Object;)Ljava/lang/Object;

    .line 762
    monitor-exit v1
    :try_end_7e
    .catchall {:try_start_78 .. :try_end_7e} :catchall_ab

    .line 763
    new-instance p0, Ljava/lang/StringBuilder;

    const-string v1, "[WESTLAKE-WEBJPEG-797] schedule-error "

    invoke-direct {p0, v1}, Ljava/lang/StringBuilder;-><init>(Ljava/lang/String;)V

    .line 764
    invoke-virtual {v0}, Ljava/lang/Object;->getClass()Ljava/lang/Class;

    move-result-object v1

    invoke-virtual {v1}, Ljava/lang/Class;->getName()Ljava/lang/String;

    move-result-object v1

    invoke-virtual {p0, v1}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object p0

    const-string v1, ":"

    invoke-virtual {p0, v1}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object p0

    .line 765
    invoke-virtual {v0}, Ljava/lang/Throwable;->getMessage()Ljava/lang/String;

    move-result-object v0

    invoke-static {v0}, Ljava/lang/String;->valueOf(Ljava/lang/Object;)Ljava/lang/String;

    move-result-object v0

    invoke-virtual {p0, v0}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object p0

    invoke-virtual {p0}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;

    move-result-object p0

    .line 763
    invoke-static {p0}, Lwestlake/WlFix;->emitLine(Ljava/lang/String;)V

    :goto_aa
    return-void

    :catchall_ab
    move-exception p0

    .line 762
    :try_start_ac
    monitor-exit v1
    :try_end_ad
    .catchall {:try_start_ac .. :try_end_ad} :catchall_ab

    throw p0

    .line 715
    :cond_ae
    :goto_ae
    :try_start_ae
    monitor-exit v0

    return-void

    :catchall_b0
    move-exception p0

    .line 717
    monitor-exit v0
    :try_end_b2
    .catchall {:try_start_ae .. :try_end_b2} :catchall_b0

    throw p0
.end method

.method private static startJpegRepairLoop(Ljava/lang/Object;)V
    .registers 10

    const-string v0, "[WESTLAKE-WEBJPEG-797] repair-loop-started view="

    :try_start_2
    const-string v1, "android.webkit.ValueCallback"

    .line 686
    invoke-static {v1}, Ljava/lang/Class;->forName(Ljava/lang/String;)Ljava/lang/Class;

    move-result-object v1
    :try_end_8
    .catchall {:try_start_2 .. :try_end_8} :catchall_b7

    .line 691
    new-instance v2, Ljava/lang/StringBuilder;

    invoke-direct {v2}, Ljava/lang/StringBuilder;-><init>()V

    invoke-static {p0}, Ljava/lang/System;->identityHashCode(Ljava/lang/Object;)I

    move-result v3

    invoke-static {v3}, Ljava/lang/Integer;->toHexString(I)Ljava/lang/String;

    move-result-object v3

    invoke-virtual {v2, v3}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v2

    new-instance v3, Ljava/lang/Object;

    invoke-direct {v3}, Ljava/lang/Object;-><init>()V

    .line 692
    invoke-static {v3}, Ljava/lang/System;->identityHashCode(Ljava/lang/Object;)I

    move-result v3

    invoke-static {v3}, Ljava/lang/Integer;->toHexString(I)Ljava/lang/String;

    move-result-object v3

    invoke-virtual {v2, v3}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v2

    invoke-virtual {v2}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;

    move-result-object v2

    .line 693
    const-class v3, Lwestlake/WlFix;

    monitor-enter v3

    .line 694
    :try_start_31
    sget-object v4, Lwestlake/WlFix;->jpegRepairOwners:Ljava/util/HashMap;

    invoke-virtual {v4, v2, p0}, Ljava/util/HashMap;->put(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;

    .line 695
    monitor-exit v3
    :try_end_37
    .catchall {:try_start_31 .. :try_end_37} :catchall_b4

    .line 696
    new-instance v3, Lwestlake/WlFix$JpegPollTask;

    invoke-direct {v3, p0, v1, v2}, Lwestlake/WlFix$JpegPollTask;-><init>(Ljava/lang/Object;Ljava/lang/Class;Ljava/lang/String;)V

    .line 698
    :try_start_3c
    invoke-virtual {p0}, Ljava/lang/Object;->getClass()Ljava/lang/Class;

    move-result-object v1

    const-string v2, "postDelayed"

    const/4 v4, 0x2

    new-array v5, v4, [Ljava/lang/Class;

    const-class v6, Ljava/lang/Runnable;

    const/4 v7, 0x0

    aput-object v6, v5, v7

    sget-object v6, Ljava/lang/Long;->TYPE:Ljava/lang/Class;

    const/4 v8, 0x1

    aput-object v6, v5, v8

    invoke-virtual {v1, v2, v5}, Ljava/lang/Class;->getMethod(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;

    move-result-object v1

    new-array v2, v4, [Ljava/lang/Object;

    aput-object v3, v2, v7

    const-wide/16 v3, 0x3e8

    .line 699
    invoke-static {v3, v4}, Ljava/lang/Long;->valueOf(J)Ljava/lang/Long;

    move-result-object v3

    aput-object v3, v2, v8

    .line 698
    invoke-virtual {v1, p0, v2}, Ljava/lang/reflect/Method;->invoke(Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/Object;

    const-string v1, "1"

    const-string v2, "WL_WEB_DIAG_ASSETS"

    .line 700
    invoke-static {v2}, Ljava/lang/System;->getenv(Ljava/lang/String;)Ljava/lang/String;

    move-result-object v2

    invoke-virtual {v1, v2}, Ljava/lang/String;->equals(Ljava/lang/Object;)Z

    move-result v1

    if-eqz v1, :cond_7f

    .line 701
    new-instance v1, Ljava/lang/Thread;

    new-instance v2, Lwestlake/WlFix$BoundarySelfTestTask;

    invoke-direct {v2, p0}, Lwestlake/WlFix$BoundarySelfTestTask;-><init>(Ljava/lang/Object;)V

    const-string v3, "wl-webjpeg-selftest"

    invoke-direct {v1, v2, v3}, Ljava/lang/Thread;-><init>(Ljava/lang/Runnable;Ljava/lang/String;)V

    .line 702
    invoke-virtual {v1}, Ljava/lang/Thread;->start()V

    .line 704
    :cond_7f
    new-instance v1, Ljava/lang/StringBuilder;

    invoke-direct {v1, v0}, Ljava/lang/StringBuilder;-><init>(Ljava/lang/String;)V

    .line 705
    invoke-virtual {p0}, Ljava/lang/Object;->getClass()Ljava/lang/Class;

    move-result-object p0

    invoke-virtual {p0}, Ljava/lang/Class;->getName()Ljava/lang/String;

    move-result-object p0

    invoke-virtual {v1, p0}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object p0

    invoke-virtual {p0}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;

    move-result-object p0

    .line 704
    invoke-static {p0}, Lwestlake/WlFix;->emitLine(Ljava/lang/String;)V
    :try_end_97
    .catchall {:try_start_3c .. :try_end_97} :catchall_98

    goto :goto_b3

    :catchall_98
    move-exception p0

    .line 707
    new-instance v0, Ljava/lang/StringBuilder;

    const-string v1, "[WESTLAKE-WEBJPEG-797] loop-start-error "

    invoke-direct {v0, v1}, Ljava/lang/StringBuilder;-><init>(Ljava/lang/String;)V

    .line 708
    invoke-virtual {p0}, Ljava/lang/Object;->getClass()Ljava/lang/Class;

    move-result-object p0

    invoke-virtual {p0}, Ljava/lang/Class;->getName()Ljava/lang/String;

    move-result-object p0

    invoke-virtual {v0, p0}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object p0

    invoke-virtual {p0}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;

    move-result-object p0

    .line 707
    invoke-static {p0}, Lwestlake/WlFix;->emitLine(Ljava/lang/String;)V

    :goto_b3
    return-void

    :catchall_b4
    move-exception p0

    .line 695
    :try_start_b5
    monitor-exit v3
    :try_end_b6
    .catchall {:try_start_b5 .. :try_end_b6} :catchall_b4

    throw p0

    :catchall_b7
    const-string p0, "[WESTLAKE-WEBJPEG-797] no ValueCallback"

    .line 688
    invoke-static {p0}, Lwestlake/WlFix;->emitLine(Ljava/lang/String;)V

    return-void
.end method

.method private static submitAssetProbe(Ljava/lang/Object;)Z
    .registers 12

    const-string v0, "[WESTLAKE-WEBASSET-793] devtools-enabled url="

    const-string v1, "[WESTLAKE-WEBASSET-793] java-user-agent="

    const-string v2, "[WESTLAKE-WEBASSET-793] devtools-error="

    const-string v3, "[WESTLAKE-WEBASSET-793] java-user-agent-error="

    const/4 v4, 0x0

    .line 212
    :try_start_9
    invoke-static {p0}, Lwestlake/WlFix;->findWebViewForAwContents(Ljava/lang/Object;)Ljava/lang/Object;

    move-result-object v5

    if-nez v5, :cond_10

    return v4

    .line 217
    :cond_10
    invoke-virtual {v5}, Ljava/lang/Object;->getClass()Ljava/lang/Class;

    move-result-object v6

    const-string v7, "getUrl"

    new-array v8, v4, [Ljava/lang/Class;

    invoke-virtual {v6, v7, v8}, Ljava/lang/Class;->getMethod(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;

    move-result-object v6

    new-array v7, v4, [Ljava/lang/Object;

    invoke-virtual {v6, v5, v7}, Ljava/lang/reflect/Method;->invoke(Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/Object;

    move-result-object v6

    .line 216
    invoke-static {v6}, Ljava/lang/String;->valueOf(Ljava/lang/Object;)Ljava/lang/String;

    move-result-object v6
    :try_end_26
    .catchall {:try_start_9 .. :try_end_26} :catchall_13a

    .line 219
    :try_start_26
    invoke-virtual {v5}, Ljava/lang/Object;->getClass()Ljava/lang/Class;

    move-result-object v7

    const-string v8, "getSettings"

    new-array v9, v4, [Ljava/lang/Class;

    invoke-virtual {v7, v8, v9}, Ljava/lang/Class;->getMethod(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;

    move-result-object v7

    new-array v8, v4, [Ljava/lang/Object;

    invoke-virtual {v7, v5, v8}, Ljava/lang/reflect/Method;->invoke(Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/Object;

    move-result-object v7

    .line 221
    invoke-virtual {v7}, Ljava/lang/Object;->getClass()Ljava/lang/Class;

    move-result-object v8

    const-string v9, "getUserAgentString"

    new-array v10, v4, [Ljava/lang/Class;

    invoke-virtual {v8, v9, v10}, Ljava/lang/Class;->getMethod(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;

    move-result-object v8

    new-array v9, v4, [Ljava/lang/Object;

    invoke-virtual {v8, v7, v9}, Ljava/lang/reflect/Method;->invoke(Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/Object;

    move-result-object v7

    .line 220
    invoke-static {v7}, Ljava/lang/String;->valueOf(Ljava/lang/Object;)Ljava/lang/String;

    move-result-object v7

    .line 222
    new-instance v8, Ljava/lang/StringBuilder;

    invoke-direct {v8, v1}, Ljava/lang/StringBuilder;-><init>(Ljava/lang/String;)V

    invoke-virtual {v8, v7}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v1

    invoke-virtual {v1}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;

    move-result-object v1

    invoke-static {v1}, Lwestlake/WlFix;->emitLine(Ljava/lang/String;)V
    :try_end_5e
    .catchall {:try_start_26 .. :try_end_5e} :catchall_5f

    goto :goto_78

    :catchall_5f
    move-exception v1

    .line 224
    :try_start_60
    new-instance v7, Ljava/lang/StringBuilder;

    invoke-direct {v7, v3}, Ljava/lang/StringBuilder;-><init>(Ljava/lang/String;)V

    .line 225
    invoke-virtual {v1}, Ljava/lang/Object;->getClass()Ljava/lang/Class;

    move-result-object v1

    invoke-virtual {v1}, Ljava/lang/Class;->getName()Ljava/lang/String;

    move-result-object v1

    invoke-virtual {v7, v1}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v1

    invoke-virtual {v1}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;

    move-result-object v1

    .line 224
    invoke-static {v1}, Lwestlake/WlFix;->emitLine(Ljava/lang/String;)V
    :try_end_78
    .catchall {:try_start_60 .. :try_end_78} :catchall_13a

    :goto_78
    const/4 v1, 0x1

    :try_start_79
    const-string v3, "android.webkit.WebView"

    .line 228
    invoke-static {v3}, Ljava/lang/Class;->forName(Ljava/lang/String;)Ljava/lang/Class;

    move-result-object v3

    const-string v7, "setWebContentsDebuggingEnabled"

    new-array v8, v1, [Ljava/lang/Class;

    .line 229
    sget-object v9, Ljava/lang/Boolean;->TYPE:Ljava/lang/Class;

    aput-object v9, v8, v4

    invoke-virtual {v3, v7, v8}, Ljava/lang/Class;->getMethod(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;

    move-result-object v3

    new-array v7, v1, [Ljava/lang/Object;

    sget-object v8, Ljava/lang/Boolean;->TRUE:Ljava/lang/Boolean;

    aput-object v8, v7, v4

    const/4 v8, 0x0

    .line 230
    invoke-virtual {v3, v8, v7}, Ljava/lang/reflect/Method;->invoke(Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/Object;

    .line 231
    new-instance v3, Ljava/lang/StringBuilder;

    invoke-direct {v3, v0}, Ljava/lang/StringBuilder;-><init>(Ljava/lang/String;)V

    invoke-virtual {v3, v6}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v0

    invoke-virtual {v0}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;

    move-result-object v0

    invoke-static {v0}, Lwestlake/WlFix;->emitLine(Ljava/lang/String;)V
    :try_end_a5
    .catchall {:try_start_79 .. :try_end_a5} :catchall_a6

    goto :goto_d1

    :catchall_a6
    move-exception v0

    .line 233
    :try_start_a7
    new-instance v3, Ljava/lang/StringBuilder;

    invoke-direct {v3, v2}, Ljava/lang/StringBuilder;-><init>(Ljava/lang/String;)V

    .line 234
    invoke-virtual {v0}, Ljava/lang/Object;->getClass()Ljava/lang/Class;

    move-result-object v2

    invoke-virtual {v2}, Ljava/lang/Class;->getName()Ljava/lang/String;

    move-result-object v2

    invoke-virtual {v3, v2}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v2

    const-string v3, ":"

    invoke-virtual {v2, v3}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v2

    .line 235
    invoke-virtual {v0}, Ljava/lang/Throwable;->getMessage()Ljava/lang/String;

    move-result-object v0

    invoke-static {v0}, Ljava/lang/String;->valueOf(Ljava/lang/Object;)Ljava/lang/String;

    move-result-object v0

    invoke-virtual {v2, v0}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v0

    invoke-virtual {v0}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;

    move-result-object v0

    .line 233
    invoke-static {v0}, Lwestlake/WlFix;->emitLine(Ljava/lang/String;)V

    .line 237
    :goto_d1
    const-class v0, Lwestlake/WlFix;

    monitor-enter v0
    :try_end_d4
    .catchall {:try_start_a7 .. :try_end_d4} :catchall_13a

    .line 238
    :try_start_d4
    sget-object v2, Lwestlake/WlFix;->assetProbeSubmitted:Ljava/util/IdentityHashMap;

    sget-object v3, Ljava/lang/Boolean;->TRUE:Ljava/lang/Boolean;

    invoke-virtual {v2, p0, v3}, Ljava/util/IdentityHashMap;->put(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;

    .line 239
    monitor-exit v0
    :try_end_dc
    .catchall {:try_start_d4 .. :try_end_dc} :catchall_137

    :try_start_dc
    const-string p0, "initial"

    .line 240
    invoke-static {v5, p0}, Lwestlake/WlFix;->evaluateAssetProbe(Ljava/lang/Object;Ljava/lang/String;)V

    const-string p0, "late20"

    const-wide/16 v2, 0x4e20

    .line 241
    invoke-static {v5, p0, v2, v3}, Lwestlake/WlFix;->scheduleEvaluation(Ljava/lang/Object;Ljava/lang/String;J)V

    const-string p0, "late60"

    const-wide/32 v2, 0xea60

    .line 242
    invoke-static {v5, p0, v2, v3}, Lwestlake/WlFix;->scheduleEvaluation(Ljava/lang/Object;Ljava/lang/String;J)V

    const-string p0, "late120"

    const-wide/32 v2, 0x1d4c0

    .line 243
    invoke-static {v5, p0, v2, v3}, Lwestlake/WlFix;->scheduleEvaluation(Ljava/lang/Object;Ljava/lang/String;J)V

    const-string p0, "1"

    const-string v0, "WL_WEB_NATIVE_FALLBACK_DIAG"

    .line 244
    invoke-static {v0}, Ljava/lang/System;->getenv(Ljava/lang/String;)Ljava/lang/String;

    move-result-object v0

    invoke-virtual {p0, v0}, Ljava/lang/String;->equals(Ljava/lang/Object;)Z

    move-result p0

    if-eqz p0, :cond_136

    .line 245
    invoke-virtual {v5}, Ljava/lang/Object;->getClass()Ljava/lang/Class;

    move-result-object p0

    const-string v0, "postDelayed"

    const/4 v2, 0x2

    new-array v3, v2, [Ljava/lang/Class;

    const-class v6, Ljava/lang/Runnable;

    aput-object v6, v3, v4

    sget-object v6, Ljava/lang/Long;->TYPE:Ljava/lang/Class;

    aput-object v6, v3, v1

    invoke-virtual {p0, v0, v3}, Ljava/lang/Class;->getMethod(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;

    move-result-object p0

    new-array v0, v2, [Ljava/lang/Object;

    new-instance v2, Lwestlake/WlFix$3;

    invoke-direct {v2, v5}, Lwestlake/WlFix$3;-><init>(Ljava/lang/Object;)V

    aput-object v2, v0, v4

    const-wide/16 v2, 0x1f40

    .line 250
    invoke-static {v2, v3}, Ljava/lang/Long;->valueOf(J)Ljava/lang/Long;

    move-result-object v2

    aput-object v2, v0, v1

    .line 245
    invoke-virtual {p0, v5, v0}, Ljava/lang/reflect/Method;->invoke(Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/Object;

    const-string p0, "post-fallback-diag"

    const-wide/16 v2, 0x3e80

    .line 251
    invoke-static {v5, p0, v2, v3}, Lwestlake/WlFix;->scheduleEvaluation(Ljava/lang/Object;Ljava/lang/String;J)V
    :try_end_136
    .catchall {:try_start_dc .. :try_end_136} :catchall_13a

    :cond_136
    return v1

    :catchall_137
    move-exception p0

    .line 239
    :try_start_138
    monitor-exit v0
    :try_end_139
    .catchall {:try_start_138 .. :try_end_139} :catchall_137

    :try_start_139
    throw p0
    :try_end_13a
    .catchall {:try_start_139 .. :try_end_13a} :catchall_13a

    :catchall_13a
    move-exception p0

    .line 255
    new-instance v0, Ljava/lang/StringBuilder;

    const-string v1, "[WESTLAKE-WEBASSET-793] error="

    invoke-direct {v0, v1}, Ljava/lang/StringBuilder;-><init>(Ljava/lang/String;)V

    invoke-virtual {p0}, Ljava/lang/Object;->getClass()Ljava/lang/Class;

    move-result-object v1

    invoke-virtual {v1}, Ljava/lang/Class;->getName()Ljava/lang/String;

    move-result-object v1

    invoke-virtual {v0, v1}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v0

    const-string v1, ":"

    invoke-virtual {v0, v1}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v0

    .line 256
    invoke-virtual {p0}, Ljava/lang/Throwable;->getMessage()Ljava/lang/String;

    move-result-object p0

    invoke-static {p0}, Ljava/lang/String;->valueOf(Ljava/lang/Object;)Ljava/lang/String;

    move-result-object p0

    invoke-virtual {v0, p0}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object p0

    invoke-virtual {p0}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;

    move-result-object p0

    .line 255
    invoke-static {p0}, Lwestlake/WlFix;->emitLine(Ljava/lang/String;)V

    return v4
.end method

.method private static unquoteJavascriptString(Ljava/lang/String;)Ljava/lang/String;
    .registers 4

    if-eqz p0, :cond_51

    const-string v0, "null"

    .line 417
    invoke-virtual {v0, p0}, Ljava/lang/String;->equals(Ljava/lang/Object;)Z

    move-result v0

    if-eqz v0, :cond_b

    goto :goto_51

    .line 418
    :cond_b
    invoke-virtual {p0}, Ljava/lang/String;->length()I

    move-result v0

    const/4 v1, 0x2

    if-lt v0, v1, :cond_30

    const/4 v0, 0x0

    invoke-virtual {p0, v0}, Ljava/lang/String;->charAt(I)C

    move-result v0

    const/16 v1, 0x22

    if-ne v0, v1, :cond_30

    .line 419
    invoke-virtual {p0}, Ljava/lang/String;->length()I

    move-result v0

    const/4 v2, 0x1

    sub-int/2addr v0, v2

    invoke-virtual {p0, v0}, Ljava/lang/String;->charAt(I)C

    move-result v0

    if-ne v0, v1, :cond_30

    .line 420
    invoke-virtual {p0}, Ljava/lang/String;->length()I

    move-result v0

    sub-int/2addr v0, v2

    invoke-virtual {p0, v2, v0}, Ljava/lang/String;->substring(II)Ljava/lang/String;

    move-result-object p0

    :cond_30
    const-string v0, "\\n"

    const-string v1, "\n"

    .line 424
    invoke-virtual {p0, v0, v1}, Ljava/lang/String;->replace(Ljava/lang/CharSequence;Ljava/lang/CharSequence;)Ljava/lang/String;

    move-result-object p0

    const-string v0, "\\r"

    const-string v1, "\r"

    invoke-virtual {p0, v0, v1}, Ljava/lang/String;->replace(Ljava/lang/CharSequence;Ljava/lang/CharSequence;)Ljava/lang/String;

    move-result-object p0

    const-string v0, "\\\""

    const-string v1, "\""

    .line 425
    invoke-virtual {p0, v0, v1}, Ljava/lang/String;->replace(Ljava/lang/CharSequence;Ljava/lang/CharSequence;)Ljava/lang/String;

    move-result-object p0

    const-string v0, "\\\\"

    const-string v1, "\\"

    invoke-virtual {p0, v0, v1}, Ljava/lang/String;->replace(Ljava/lang/CharSequence;Ljava/lang/CharSequence;)Ljava/lang/String;

    move-result-object p0

    return-object p0

    :cond_51
    :goto_51
    const-string p0, ""

    return-object p0
.end method

.method public static web_callback(Ljava/lang/Object;)V
    .registers 4

    const-string v0, "result="

    if-nez p0, :cond_a

    const-string p0, "result=NULL"

    .line 57
    invoke-static {p0}, Lwestlake/WlFix;->emit(Ljava/lang/String;)V

    return-void

    .line 61
    :cond_a
    :try_start_a
    instance-of v1, p0, Ljava/lang/String;

    if-eqz v1, :cond_11

    check-cast p0, Ljava/lang/String;

    goto :goto_19

    :cond_11
    invoke-virtual {p0}, Ljava/lang/Object;->getClass()Ljava/lang/Class;

    move-result-object p0

    invoke-virtual {p0}, Ljava/lang/Class;->getName()Ljava/lang/String;

    move-result-object p0

    .line 62
    :goto_19
    invoke-static {p0}, Lwestlake/WlFix;->unquoteJavascriptString(Ljava/lang/String;)Ljava/lang/String;

    move-result-object v1

    const-string v2, "__WLJPEG797__"

    .line 63
    invoke-virtual {v1, v2}, Ljava/lang/String;->startsWith(Ljava/lang/String;)Z

    move-result v2

    if-eqz v2, :cond_28

    .line 64
    invoke-static {v1}, Lwestlake/WlFix;->handleJpegResultMessage(Ljava/lang/String;)V

    .line 66
    :cond_28
    invoke-virtual {p0}, Ljava/lang/String;->length()I

    move-result v1

    const/16 v2, 0xf0

    if-le v1, v2, :cond_35

    const/4 v1, 0x0

    .line 67
    invoke-virtual {p0, v1, v2}, Ljava/lang/String;->substring(II)Ljava/lang/String;

    move-result-object p0

    .line 69
    :cond_35
    new-instance v1, Ljava/lang/StringBuilder;

    invoke-direct {v1, v0}, Ljava/lang/StringBuilder;-><init>(Ljava/lang/String;)V

    invoke-virtual {v1, p0}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object p0

    invoke-virtual {p0}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;

    move-result-object p0

    invoke-static {p0}, Lwestlake/WlFix;->emit(Ljava/lang/String;)V
    :try_end_45
    .catchall {:try_start_a .. :try_end_45} :catchall_46

    goto :goto_4b

    :catchall_46
    const-string p0, "result=<unprintable>"

    .line 71
    invoke-static {p0}, Lwestlake/WlFix;->emit(Ljava/lang/String;)V

    :goto_4b
    return-void
.end method

.method public static web_draw(Ljava/lang/Object;)V
    .registers 5

    const-string v0, "[WESTLAKE-WEBDRAW-730] aw="

    .line 848
    :try_start_2
    invoke-virtual {p0}, Ljava/lang/Object;->getClass()Ljava/lang/Class;

    move-result-object v1

    const-string v2, "f"

    invoke-virtual {v1, v2}, Ljava/lang/Class;->getDeclaredField(Ljava/lang/String;)Ljava/lang/reflect/Field;

    move-result-object v1

    const/4 v2, 0x1

    .line 849
    invoke-virtual {v1, v2}, Ljava/lang/reflect/Field;->setAccessible(Z)V

    .line 850
    invoke-virtual {v1, p0}, Ljava/lang/reflect/Field;->get(Ljava/lang/Object;)Ljava/lang/Object;

    move-result-object p0

    .line 851
    sget-object v1, Lwestlake/WlFix;->drawCounts:Ljava/util/IdentityHashMap;

    invoke-virtual {v1, p0}, Ljava/util/IdentityHashMap;->get(Ljava/lang/Object;)Ljava/lang/Object;

    move-result-object v3

    check-cast v3, Ljava/lang/Integer;

    if-nez v3, :cond_1f

    goto :goto_24

    .line 852
    :cond_1f
    invoke-virtual {v3}, Ljava/lang/Integer;->intValue()I

    move-result v3

    add-int/2addr v2, v3

    .line 853
    :goto_24
    invoke-static {v2}, Ljava/lang/Integer;->valueOf(I)Ljava/lang/Integer;

    move-result-object v3

    invoke-virtual {v1, p0, v3}, Ljava/util/IdentityHashMap;->put(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;

    const/16 v1, 0x8

    if-le v2, v1, :cond_33

    .line 854
    rem-int/lit8 v1, v2, 0x19

    if-nez v1, :cond_83

    .line 855
    :cond_33
    new-instance v1, Ljava/lang/StringBuilder;

    invoke-direct {v1, v0}, Ljava/lang/StringBuilder;-><init>(Ljava/lang/String;)V

    .line 856
    invoke-static {p0}, Ljava/lang/System;->identityHashCode(Ljava/lang/Object;)I

    move-result p0

    invoke-static {p0}, Ljava/lang/Integer;->toHexString(I)Ljava/lang/String;

    move-result-object p0

    invoke-virtual {v1, p0}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object p0

    const-string v0, " count="

    invoke-virtual {p0, v0}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object p0

    invoke-virtual {p0, v2}, Ljava/lang/StringBuilder;->append(I)Ljava/lang/StringBuilder;

    move-result-object p0

    const-string v0, " thread="

    invoke-virtual {p0, v0}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object p0

    .line 857
    invoke-static {}, Ljava/lang/Thread;->currentThread()Ljava/lang/Thread;

    move-result-object v0

    invoke-virtual {v0}, Ljava/lang/Thread;->getName()Ljava/lang/String;

    move-result-object v0

    invoke-virtual {p0, v0}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object p0

    invoke-virtual {p0}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;

    move-result-object p0

    .line 855
    invoke-static {p0}, Lwestlake/WlFix;->emitLine(Ljava/lang/String;)V
    :try_end_67
    .catchall {:try_start_2 .. :try_end_67} :catchall_68

    goto :goto_83

    :catchall_68
    move-exception p0

    .line 860
    new-instance v0, Ljava/lang/StringBuilder;

    const-string v1, "[WESTLAKE-WEBDRAW-730] error="

    invoke-direct {v0, v1}, Ljava/lang/StringBuilder;-><init>(Ljava/lang/String;)V

    invoke-virtual {p0}, Ljava/lang/Object;->getClass()Ljava/lang/Class;

    move-result-object p0

    invoke-virtual {p0}, Ljava/lang/Class;->getName()Ljava/lang/String;

    move-result-object p0

    invoke-virtual {v0, p0}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object p0

    invoke-virtual {p0}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;

    move-result-object p0

    invoke-static {p0}, Lwestlake/WlFix;->emitLine(Ljava/lang/String;)V

    :cond_83
    :goto_83
    return-void
.end method

.method public static web_scroll(Ljava/lang/Object;)V
    .registers 6

    const-string v0, "[WESTLAKE-WEBSCROLL-729] seq="

    .line 825
    :try_start_2
    invoke-virtual {p0}, Ljava/lang/Object;->getClass()Ljava/lang/Class;

    move-result-object v1

    const-string v2, "Z"

    invoke-virtual {v1, v2}, Ljava/lang/Class;->getDeclaredField(Ljava/lang/String;)Ljava/lang/reflect/Field;

    move-result-object v1

    .line 826
    invoke-virtual {p0}, Ljava/lang/Object;->getClass()Ljava/lang/Class;

    move-result-object v2

    const-string v3, "a0"

    invoke-virtual {v2, v3}, Ljava/lang/Class;->getDeclaredField(Ljava/lang/String;)Ljava/lang/reflect/Field;

    move-result-object v2

    const/4 v3, 0x1

    .line 827
    invoke-virtual {v1, v3}, Ljava/lang/reflect/Field;->setAccessible(Z)V

    .line 828
    invoke-virtual {v2, v3}, Ljava/lang/reflect/Field;->setAccessible(Z)V

    .line 829
    new-instance v4, Ljava/lang/StringBuilder;

    invoke-direct {v4, v0}, Ljava/lang/StringBuilder;-><init>(Ljava/lang/String;)V

    sget v0, Lwestlake/WlFix;->scrollSequence:I

    add-int/2addr v0, v3

    sput v0, Lwestlake/WlFix;->scrollSequence:I

    invoke-virtual {v4, v0}, Ljava/lang/StringBuilder;->append(I)Ljava/lang/StringBuilder;

    move-result-object v0

    const-string v3, " aw="

    invoke-virtual {v0, v3}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v0

    .line 830
    invoke-static {p0}, Ljava/lang/System;->identityHashCode(Ljava/lang/Object;)I

    move-result v3

    invoke-static {v3}, Ljava/lang/Integer;->toHexString(I)Ljava/lang/String;

    move-result-object v3

    invoke-virtual {v0, v3}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v0

    const-string v3, " widthDip="

    invoke-virtual {v0, v3}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v0

    .line 831
    invoke-virtual {v1, p0}, Ljava/lang/reflect/Field;->getFloat(Ljava/lang/Object;)F

    move-result v1

    invoke-virtual {v0, v1}, Ljava/lang/StringBuilder;->append(F)Ljava/lang/StringBuilder;

    move-result-object v0

    const-string v1, " heightDip="

    invoke-virtual {v0, v1}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v0

    .line 832
    invoke-virtual {v2, p0}, Ljava/lang/reflect/Field;->getFloat(Ljava/lang/Object;)F

    move-result v1

    invoke-virtual {v0, v1}, Ljava/lang/StringBuilder;->append(F)Ljava/lang/StringBuilder;

    move-result-object v0

    const-string v1, " thread="

    invoke-virtual {v0, v1}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v0

    .line 833
    invoke-static {}, Ljava/lang/Thread;->currentThread()Ljava/lang/Thread;

    move-result-object v1

    invoke-virtual {v1}, Ljava/lang/Thread;->getName()Ljava/lang/String;

    move-result-object v1

    invoke-virtual {v0, v1}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v0

    invoke-virtual {v0}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;

    move-result-object v0

    .line 829
    invoke-static {v0}, Lwestlake/WlFix;->emitLine(Ljava/lang/String;)V

    .line 834
    invoke-virtual {v2, p0}, Ljava/lang/reflect/Field;->getFloat(Ljava/lang/Object;)F

    move-result v0

    const/high16 v1, 0x3f800000    # 1.0f

    cmpl-float v0, v0, v1

    if-lez v0, :cond_ac

    .line 835
    invoke-static {p0}, Lwestlake/WlFix;->scheduleJpegRepair(Ljava/lang/Object;)V

    const-string v0, "1"

    const-string v1, "WL_WEB_DIAG_ASSETS"

    .line 836
    invoke-static {v1}, Ljava/lang/System;->getenv(Ljava/lang/String;)Ljava/lang/String;

    move-result-object v1

    invoke-virtual {v0, v1}, Ljava/lang/String;->equals(Ljava/lang/Object;)Z

    move-result v0

    if-eqz v0, :cond_ac

    .line 837
    invoke-static {p0}, Lwestlake/WlFix;->scheduleAssetProbe(Ljava/lang/Object;)V
    :try_end_90
    .catchall {:try_start_2 .. :try_end_90} :catchall_91

    goto :goto_ac

    :catchall_91
    move-exception p0

    .line 841
    new-instance v0, Ljava/lang/StringBuilder;

    const-string v1, "[WESTLAKE-WEBSCROLL-729] error="

    invoke-direct {v0, v1}, Ljava/lang/StringBuilder;-><init>(Ljava/lang/String;)V

    invoke-virtual {p0}, Ljava/lang/Object;->getClass()Ljava/lang/Class;

    move-result-object p0

    invoke-virtual {p0}, Ljava/lang/Class;->getName()Ljava/lang/String;

    move-result-object p0

    invoke-virtual {v0, p0}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object p0

    invoke-virtual {p0}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;

    move-result-object p0

    invoke-static {p0}, Lwestlake/WlFix;->emitLine(Ljava/lang/String;)V

    :cond_ac
    :goto_ac
    return-void
.end method

.method private static writeInt32(Ljava/io/ByteArrayOutputStream;I)V
    .registers 3

    ushr-int/lit8 v0, p1, 0x18

    and-int/lit16 v0, v0, 0xff

    .line 429
    invoke-virtual {p0, v0}, Ljava/io/ByteArrayOutputStream;->write(I)V

    ushr-int/lit8 v0, p1, 0x10

    and-int/lit16 v0, v0, 0xff

    .line 430
    invoke-virtual {p0, v0}, Ljava/io/ByteArrayOutputStream;->write(I)V

    ushr-int/lit8 v0, p1, 0x8

    and-int/lit16 v0, v0, 0xff

    .line 431
    invoke-virtual {p0, v0}, Ljava/io/ByteArrayOutputStream;->write(I)V

    and-int/lit16 p1, p1, 0xff

    .line 432
    invoke-virtual {p0, p1}, Ljava/io/ByteArrayOutputStream;->write(I)V

    return-void
.end method

.method private static writePngChunk(Ljava/io/ByteArrayOutputStream;Ljava/lang/String;[B)V
    .registers 4
    .annotation system Ldalvik/annotation/Throws;
        value = {
            Ljava/lang/Exception;
        }
    .end annotation

    const-string v0, "US-ASCII"

    .line 437
    invoke-virtual {p1, v0}, Ljava/lang/String;->getBytes(Ljava/lang/String;)[B

    move-result-object p1

    .line 438
    array-length v0, p2

    invoke-static {p0, v0}, Lwestlake/WlFix;->writeInt32(Ljava/io/ByteArrayOutputStream;I)V

    .line 439
    invoke-virtual {p0, p1}, Ljava/io/ByteArrayOutputStream;->write([B)V

    .line 440
    invoke-virtual {p0, p2}, Ljava/io/ByteArrayOutputStream;->write([B)V

    .line 441
    new-instance v0, Ljava/util/zip/CRC32;

    invoke-direct {v0}, Ljava/util/zip/CRC32;-><init>()V

    .line 442
    invoke-virtual {v0, p1}, Ljava/util/zip/CRC32;->update([B)V

    .line 443
    invoke-virtual {v0, p2}, Ljava/util/zip/CRC32;->update([B)V

    .line 444
    invoke-virtual {v0}, Ljava/util/zip/CRC32;->getValue()J

    move-result-wide p1

    long-to-int p1, p1

    invoke-static {p0, p1}, Lwestlake/WlFix;->writeInt32(Ljava/io/ByteArrayOutputStream;I)V

    return-void
.end method
