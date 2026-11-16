.class final Lwestlake/WlFix$JpegBoundaryNetworkTask;
.super Ljava/lang/Object;
.source "WlFix.java"

# interfaces
.implements Ljava/lang/Runnable;


# annotations
.annotation system Ldalvik/annotation/EnclosingClass;
    value = Lwestlake/WlFix;
.end annotation

.annotation system Ldalvik/annotation/InnerClass;
    accessFlags = 0x1a
    name = "JpegBoundaryNetworkTask"
.end annotation


# instance fields
.field private final id:Ljava/lang/String;

.field private final url:Ljava/lang/String;

.field private final webView:Ljava/lang/Object;


# direct methods
.method constructor <init>(Ljava/lang/Object;Ljava/lang/String;Ljava/lang/String;)V
    .registers 4

    .line 567
    invoke-direct {p0}, Ljava/lang/Object;-><init>()V

    .line 568
    iput-object p1, p0, Lwestlake/WlFix$JpegBoundaryNetworkTask;->webView:Ljava/lang/Object;

    .line 569
    iput-object p2, p0, Lwestlake/WlFix$JpegBoundaryNetworkTask;->id:Ljava/lang/String;

    .line 570
    iput-object p3, p0, Lwestlake/WlFix$JpegBoundaryNetworkTask;->url:Ljava/lang/String;

    return-void
.end method


# virtual methods
.method public run()V
    .registers 13

    const-string v0, "[WESTLAKE-WEBJPEG-797] boundary-fetched id="

    const-string v1, "[WESTLAKE-WEBJPEG-797] non-jpeg id="

    const-string v2, "boundary response "

    :try_start_6
    const-string v3, "com.ohos.shim.bridge.OHBridge"

    .line 579
    const-class v4, Lwestlake/WlFix;

    .line 580
    invoke-virtual {v4}, Ljava/lang/Class;->getClassLoader()Ljava/lang/ClassLoader;

    move-result-object v4

    const/4 v5, 0x1

    .line 579
    invoke-static {v3, v5, v4}, Ljava/lang/Class;->forName(Ljava/lang/String;ZLjava/lang/ClassLoader;)Ljava/lang/Class;

    move-result-object v3

    const-string v4, "httpRequest"

    const/4 v6, 0x4

    new-array v7, v6, [Ljava/lang/Class;

    .line 581
    const-class v8, Ljava/lang/String;

    const/4 v9, 0x0

    aput-object v8, v7, v9

    const-class v8, Ljava/lang/String;

    aput-object v8, v7, v5

    const-class v8, Ljava/lang/String;

    const/4 v10, 0x2

    aput-object v8, v7, v10

    const-class v8, Ljava/lang/String;

    const/4 v11, 0x3

    aput-object v8, v7, v11

    invoke-virtual {v3, v4, v7}, Ljava/lang/Class;->getMethod(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;

    move-result-object v3

    new-array v4, v6, [Ljava/lang/Object;

    iget-object v6, p0, Lwestlake/WlFix$JpegBoundaryNetworkTask;->url:Ljava/lang/String;

    aput-object v6, v4, v9

    const-string v6, "GET"

    aput-object v6, v4, v5

    const-string v6, "{\"responseBase64\":true}"

    aput-object v6, v4, v10

    const/4 v6, 0x0

    aput-object v6, v4, v11

    .line 583
    invoke-virtual {v3, v6, v4}, Ljava/lang/reflect/Method;->invoke(Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/Object;

    move-result-object v3

    check-cast v3, Ljava/lang/String;

    const/16 v4, 0xb4

    if-eqz v3, :cond_cf

    const-string v6, "base64:"

    .line 585
    invoke-virtual {v3, v6}, Ljava/lang/String;->startsWith(Ljava/lang/String;)Z

    move-result v6

    if-eqz v6, :cond_cf

    .line 590
    invoke-static {}, Ljava/util/Base64;->getDecoder()Ljava/util/Base64$Decoder;

    move-result-object v2

    const/4 v6, 0x7

    invoke-virtual {v3, v6}, Ljava/lang/String;->substring(I)Ljava/lang/String;

    move-result-object v3

    invoke-virtual {v2, v3}, Ljava/util/Base64$Decoder;->decode(Ljava/lang/String;)[B

    move-result-object v2

    .line 591
    array-length v3, v2
    :try_end_60
    .catchall {:try_start_6 .. :try_end_60} :catchall_f2

    const-string v6, " bytes="

    if-lt v3, v11, :cond_b3

    :try_start_64
    aget-byte v3, v2, v9

    const/16 v7, 0xff

    and-int/2addr v3, v7

    if-ne v3, v7, :cond_b3

    aget-byte v3, v2, v5

    and-int/2addr v3, v7

    const/16 v5, 0xd8

    if-ne v3, v5, :cond_b3

    aget-byte v3, v2, v10

    and-int/2addr v3, v7

    if-eq v3, v7, :cond_78

    goto :goto_b3

    .line 597
    :cond_78
    new-instance v1, Ljava/lang/StringBuilder;

    invoke-direct {v1, v0}, Ljava/lang/StringBuilder;-><init>(Ljava/lang/String;)V

    iget-object v0, p0, Lwestlake/WlFix$JpegBoundaryNetworkTask;->id:Ljava/lang/String;

    invoke-virtual {v1, v0}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v0

    invoke-virtual {v0, v6}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v0

    array-length v1, v2

    invoke-virtual {v0, v1}, Ljava/lang/StringBuilder;->append(I)Ljava/lang/StringBuilder;

    move-result-object v0

    const-string v1, " url="

    invoke-virtual {v0, v1}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v0

    iget-object v1, p0, Lwestlake/WlFix$JpegBoundaryNetworkTask;->url:Ljava/lang/String;

    .line 599
    invoke-virtual {v1}, Ljava/lang/String;->length()I

    move-result v3

    invoke-static {v4, v3}, Ljava/lang/Math;->min(II)I

    move-result v3

    invoke-virtual {v1, v9, v3}, Ljava/lang/String;->substring(II)Ljava/lang/String;

    move-result-object v1

    invoke-virtual {v0, v1}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v0

    invoke-virtual {v0}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;

    move-result-object v0

    .line 597
    # invokes: Lwestlake/WlFix;->emitLine(Ljava/lang/String;)V
    invoke-static {v0}, Lwestlake/WlFix;->access$000(Ljava/lang/String;)V

    .line 600
    iget-object v0, p0, Lwestlake/WlFix$JpegBoundaryNetworkTask;->webView:Ljava/lang/Object;

    iget-object v1, p0, Lwestlake/WlFix$JpegBoundaryNetworkTask;->id:Ljava/lang/String;

    # invokes: Lwestlake/WlFix;->replaceJpegWithPng(Ljava/lang/Object;Ljava/lang/String;[B)V
    invoke-static {v0, v1, v2}, Lwestlake/WlFix;->access$600(Ljava/lang/Object;Ljava/lang/String;[B)V

    goto :goto_12b

    .line 593
    :cond_b3
    :goto_b3
    new-instance v0, Ljava/lang/StringBuilder;

    invoke-direct {v0, v1}, Ljava/lang/StringBuilder;-><init>(Ljava/lang/String;)V

    iget-object v1, p0, Lwestlake/WlFix$JpegBoundaryNetworkTask;->id:Ljava/lang/String;

    invoke-virtual {v0, v1}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v0

    invoke-virtual {v0, v6}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v0

    array-length v1, v2

    invoke-virtual {v0, v1}, Ljava/lang/StringBuilder;->append(I)Ljava/lang/StringBuilder;

    move-result-object v0

    invoke-virtual {v0}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;

    move-result-object v0

    # invokes: Lwestlake/WlFix;->emitLine(Ljava/lang/String;)V
    invoke-static {v0}, Lwestlake/WlFix;->access$000(Ljava/lang/String;)V

    return-void

    .line 586
    :cond_cf
    invoke-static {v3}, Ljava/lang/String;->valueOf(Ljava/lang/Object;)Ljava/lang/String;

    move-result-object v0

    .line 587
    new-instance v1, Ljava/lang/Exception;

    new-instance v3, Ljava/lang/StringBuilder;

    invoke-direct {v3, v2}, Ljava/lang/StringBuilder;-><init>(Ljava/lang/String;)V

    .line 588
    invoke-virtual {v0}, Ljava/lang/String;->length()I

    move-result v2

    invoke-static {v4, v2}, Ljava/lang/Math;->min(II)I

    move-result v2

    .line 587
    invoke-virtual {v0, v9, v2}, Ljava/lang/String;->substring(II)Ljava/lang/String;

    move-result-object v0

    invoke-virtual {v3, v0}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v0

    invoke-virtual {v0}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;

    move-result-object v0

    invoke-direct {v1, v0}, Ljava/lang/Exception;-><init>(Ljava/lang/String;)V

    throw v1
    :try_end_f2
    .catchall {:try_start_64 .. :try_end_f2} :catchall_f2

    :catchall_f2
    move-exception v0

    .line 602
    new-instance v1, Ljava/lang/StringBuilder;

    const-string v2, "[WESTLAKE-WEBJPEG-797] boundary-network-error id="

    invoke-direct {v1, v2}, Ljava/lang/StringBuilder;-><init>(Ljava/lang/String;)V

    iget-object p0, p0, Lwestlake/WlFix$JpegBoundaryNetworkTask;->id:Ljava/lang/String;

    invoke-virtual {v1, p0}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object p0

    const-string v1, " "

    invoke-virtual {p0, v1}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object p0

    .line 603
    invoke-virtual {v0}, Ljava/lang/Object;->getClass()Ljava/lang/Class;

    move-result-object v1

    invoke-virtual {v1}, Ljava/lang/Class;->getName()Ljava/lang/String;

    move-result-object v1

    invoke-virtual {p0, v1}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object p0

    const-string v1, ":"

    invoke-virtual {p0, v1}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object p0

    .line 604
    invoke-virtual {v0}, Ljava/lang/Throwable;->getMessage()Ljava/lang/String;

    move-result-object v0

    invoke-static {v0}, Ljava/lang/String;->valueOf(Ljava/lang/Object;)Ljava/lang/String;

    move-result-object v0

    invoke-virtual {p0, v0}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object p0

    invoke-virtual {p0}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;

    move-result-object p0

    .line 602
    # invokes: Lwestlake/WlFix;->emitLine(Ljava/lang/String;)V
    invoke-static {p0}, Lwestlake/WlFix;->access$000(Ljava/lang/String;)V

    :goto_12b
    return-void
.end method
