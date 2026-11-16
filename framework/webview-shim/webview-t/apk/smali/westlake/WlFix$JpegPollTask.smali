.class final Lwestlake/WlFix$JpegPollTask;
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
    name = "JpegPollTask"
.end annotation


# instance fields
.field private final owner:Ljava/lang/String;

.field private polls:I

.field private final valueCallback:Ljava/lang/Class;
    .annotation system Ldalvik/annotation/Signature;
        value = {
            "Ljava/lang/Class<",
            "*>;"
        }
    .end annotation
.end field

.field private final webView:Ljava/lang/Object;


# direct methods
.method constructor <init>(Ljava/lang/Object;Ljava/lang/Class;Ljava/lang/String;)V
    .registers 4
    .annotation system Ldalvik/annotation/Signature;
        value = {
            "(",
            "Ljava/lang/Object;",
            "Ljava/lang/Class<",
            "*>;",
            "Ljava/lang/String;",
            ")V"
        }
    .end annotation

    .line 650
    invoke-direct {p0}, Ljava/lang/Object;-><init>()V

    .line 651
    iput-object p1, p0, Lwestlake/WlFix$JpegPollTask;->webView:Ljava/lang/Object;

    .line 652
    iput-object p2, p0, Lwestlake/WlFix$JpegPollTask;->valueCallback:Ljava/lang/Class;

    .line 653
    iput-object p3, p0, Lwestlake/WlFix$JpegPollTask;->owner:Ljava/lang/String;

    return-void
.end method


# virtual methods
.method public run()V
    .registers 9

    .line 666
    iget v0, p0, Lwestlake/WlFix$JpegPollTask;->polls:I

    const/4 v1, 0x1

    add-int/2addr v0, v1

    iput v0, p0, Lwestlake/WlFix$JpegPollTask;->polls:I

    const/16 v2, 0x12c

    if-le v0, v2, :cond_b

    return-void

    .line 668
    :cond_b
    :try_start_b
    iget-object v0, p0, Lwestlake/WlFix$JpegPollTask;->valueCallback:Ljava/lang/Class;

    .line 669
    invoke-virtual {v0}, Ljava/lang/Class;->getClassLoader()Ljava/lang/ClassLoader;

    move-result-object v0

    new-array v2, v1, [Ljava/lang/Class;

    iget-object v3, p0, Lwestlake/WlFix$JpegPollTask;->valueCallback:Ljava/lang/Class;

    const/4 v4, 0x0

    aput-object v3, v2, v4

    new-instance v3, Lwestlake/WlFix$JpegPollResult;

    invoke-direct {v3, p0}, Lwestlake/WlFix$JpegPollResult;-><init>(Lwestlake/WlFix$JpegPollTask;)V

    .line 668
    invoke-static {v0, v2, v3}, Ljava/lang/reflect/Proxy;->newProxyInstance(Ljava/lang/ClassLoader;[Ljava/lang/Class;Ljava/lang/reflect/InvocationHandler;)Ljava/lang/Object;

    move-result-object v0

    .line 671
    iget-object v2, p0, Lwestlake/WlFix$JpegPollTask;->webView:Ljava/lang/Object;

    invoke-virtual {v2}, Ljava/lang/Object;->getClass()Ljava/lang/Class;

    move-result-object v2

    const-string v3, "evaluateJavascript"

    const/4 v5, 0x2

    new-array v6, v5, [Ljava/lang/Class;

    const-class v7, Ljava/lang/String;

    aput-object v7, v6, v4

    iget-object v7, p0, Lwestlake/WlFix$JpegPollTask;->valueCallback:Ljava/lang/Class;

    aput-object v7, v6, v1

    invoke-virtual {v2, v3, v6}, Ljava/lang/Class;->getMethod(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;

    move-result-object v2

    iget-object v3, p0, Lwestlake/WlFix$JpegPollTask;->webView:Ljava/lang/Object;

    new-array v5, v5, [Ljava/lang/Object;

    iget-object v6, p0, Lwestlake/WlFix$JpegPollTask;->owner:Ljava/lang/String;

    .line 673
    # invokes: Lwestlake/WlFix;->buildJpegCandidateScript(Ljava/lang/String;)Ljava/lang/String;
    invoke-static {v6}, Lwestlake/WlFix;->access$700(Ljava/lang/String;)Ljava/lang/String;

    move-result-object v6

    aput-object v6, v5, v4

    aput-object v0, v5, v1

    .line 672
    invoke-virtual {v2, v3, v5}, Ljava/lang/reflect/Method;->invoke(Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/Object;

    .line 674
    invoke-virtual {p0}, Lwestlake/WlFix$JpegPollTask;->scheduleNext()V
    :try_end_4c
    .catchall {:try_start_b .. :try_end_4c} :catchall_4d

    goto :goto_7a

    :catchall_4d
    move-exception p0

    .line 676
    new-instance v0, Ljava/lang/StringBuilder;

    const-string v1, "[WESTLAKE-WEBJPEG-797] poll-error "

    invoke-direct {v0, v1}, Ljava/lang/StringBuilder;-><init>(Ljava/lang/String;)V

    .line 677
    invoke-virtual {p0}, Ljava/lang/Object;->getClass()Ljava/lang/Class;

    move-result-object v1

    invoke-virtual {v1}, Ljava/lang/Class;->getName()Ljava/lang/String;

    move-result-object v1

    invoke-virtual {v0, v1}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v0

    const-string v1, ":"

    invoke-virtual {v0, v1}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v0

    .line 678
    invoke-virtual {p0}, Ljava/lang/Throwable;->getMessage()Ljava/lang/String;

    move-result-object p0

    invoke-static {p0}, Ljava/lang/String;->valueOf(Ljava/lang/Object;)Ljava/lang/String;

    move-result-object p0

    invoke-virtual {v0, p0}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object p0

    invoke-virtual {p0}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;

    move-result-object p0

    .line 676
    # invokes: Lwestlake/WlFix;->emitLine(Ljava/lang/String;)V
    invoke-static {p0}, Lwestlake/WlFix;->access$000(Ljava/lang/String;)V

    :goto_7a
    return-void
.end method

.method scheduleNext()V
    .registers 8

    .line 658
    :try_start_0
    iget-object v0, p0, Lwestlake/WlFix$JpegPollTask;->webView:Ljava/lang/Object;

    invoke-virtual {v0}, Ljava/lang/Object;->getClass()Ljava/lang/Class;

    move-result-object v0

    const-string v1, "postDelayed"

    const/4 v2, 0x2

    new-array v3, v2, [Ljava/lang/Class;

    const-class v4, Ljava/lang/Runnable;

    const/4 v5, 0x0

    aput-object v4, v3, v5

    sget-object v4, Ljava/lang/Long;->TYPE:Ljava/lang/Class;

    const/4 v6, 0x1

    aput-object v4, v3, v6

    invoke-virtual {v0, v1, v3}, Ljava/lang/Class;->getMethod(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;

    move-result-object v0

    iget-object v1, p0, Lwestlake/WlFix$JpegPollTask;->webView:Ljava/lang/Object;

    new-array v2, v2, [Ljava/lang/Object;

    aput-object p0, v2, v5

    const-wide/16 v3, 0x7d0

    .line 660
    invoke-static {v3, v4}, Ljava/lang/Long;->valueOf(J)Ljava/lang/Long;

    move-result-object p0

    aput-object p0, v2, v6

    .line 659
    invoke-virtual {v0, v1, v2}, Ljava/lang/reflect/Method;->invoke(Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/Object;
    :try_end_2a
    .catchall {:try_start_0 .. :try_end_2a} :catchall_2a

    :catchall_2a
    return-void
.end method
