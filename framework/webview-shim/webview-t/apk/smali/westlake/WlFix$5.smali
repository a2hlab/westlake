.class Lwestlake/WlFix$5;
.super Ljava/lang/Object;
.source "WlFix.java"

# interfaces
.implements Ljava/lang/Runnable;


# annotations
.annotation system Ldalvik/annotation/EnclosingMethod;
    value = Lwestlake/WlFix;->scheduleAssetProbe(Ljava/lang/Object;)V
.end annotation

.annotation system Ldalvik/annotation/InnerClass;
    accessFlags = 0x0
    name = null
.end annotation


# instance fields
.field private attempts:I

.field final synthetic val$handler:Ljava/lang/Object;

.field final synthetic val$postDelayed:Ljava/lang/reflect/Method;

.field final synthetic val$targetAwContents:Ljava/lang/Object;

.field final synthetic val$task:[Ljava/lang/Runnable;


# direct methods
.method constructor <init>(Ljava/lang/Object;Ljava/lang/reflect/Method;Ljava/lang/Object;[Ljava/lang/Runnable;)V
    .registers 5
    .annotation system Ldalvik/annotation/Signature;
        value = {
            "()V"
        }
    .end annotation

    .line 292
    iput-object p1, p0, Lwestlake/WlFix$5;->val$targetAwContents:Ljava/lang/Object;

    iput-object p2, p0, Lwestlake/WlFix$5;->val$postDelayed:Ljava/lang/reflect/Method;

    iput-object p3, p0, Lwestlake/WlFix$5;->val$handler:Ljava/lang/Object;

    iput-object p4, p0, Lwestlake/WlFix$5;->val$task:[Ljava/lang/Runnable;

    invoke-direct {p0}, Ljava/lang/Object;-><init>()V

    return-void
.end method


# virtual methods
.method public run()V
    .registers 7

    .line 296
    iget v0, p0, Lwestlake/WlFix$5;->attempts:I

    const/4 v1, 0x1

    add-int/2addr v0, v1

    iput v0, p0, Lwestlake/WlFix$5;->attempts:I

    .line 297
    iget-object v0, p0, Lwestlake/WlFix$5;->val$targetAwContents:Ljava/lang/Object;

    # invokes: Lwestlake/WlFix;->submitAssetProbe(Ljava/lang/Object;)Z
    invoke-static {v0}, Lwestlake/WlFix;->access$300(Ljava/lang/Object;)Z

    move-result v0

    if-nez v0, :cond_6c

    .line 298
    iget v2, p0, Lwestlake/WlFix$5;->attempts:I

    const/16 v3, 0x28

    if-ge v2, v3, :cond_6c

    .line 300
    :try_start_14
    iget-object v0, p0, Lwestlake/WlFix$5;->val$postDelayed:Ljava/lang/reflect/Method;

    iget-object v2, p0, Lwestlake/WlFix$5;->val$handler:Ljava/lang/Object;

    const/4 v3, 0x2

    new-array v3, v3, [Ljava/lang/Object;

    iget-object v4, p0, Lwestlake/WlFix$5;->val$task:[Ljava/lang/Runnable;

    const/4 v5, 0x0

    aget-object v4, v4, v5

    aput-object v4, v3, v5

    const-wide/16 v4, 0xbb8

    invoke-static {v4, v5}, Ljava/lang/Long;->valueOf(J)Ljava/lang/Long;

    move-result-object v4

    aput-object v4, v3, v1

    invoke-virtual {v0, v2, v3}, Ljava/lang/reflect/Method;->invoke(Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/Object;
    :try_end_2d
    .catchall {:try_start_14 .. :try_end_2d} :catchall_2e

    goto :goto_68

    :catchall_2e
    move-exception v0

    .line 302
    const-class v1, Lwestlake/WlFix;

    monitor-enter v1

    .line 303
    :try_start_32
    # getter for: Lwestlake/WlFix;->assetProbeScheduled:Ljava/util/IdentityHashMap;
    invoke-static {}, Lwestlake/WlFix;->access$400()Ljava/util/IdentityHashMap;

    move-result-object v2

    iget-object p0, p0, Lwestlake/WlFix$5;->val$targetAwContents:Ljava/lang/Object;

    invoke-virtual {v2, p0}, Ljava/util/IdentityHashMap;->remove(Ljava/lang/Object;)Ljava/lang/Object;

    .line 304
    monitor-exit v1
    :try_end_3c
    .catchall {:try_start_32 .. :try_end_3c} :catchall_69

    .line 305
    new-instance p0, Ljava/lang/StringBuilder;

    const-string v1, "[WESTLAKE-WEBASSET-793] retry-error="

    invoke-direct {p0, v1}, Ljava/lang/StringBuilder;-><init>(Ljava/lang/String;)V

    .line 306
    invoke-virtual {v0}, Ljava/lang/Object;->getClass()Ljava/lang/Class;

    move-result-object v1

    invoke-virtual {v1}, Ljava/lang/Class;->getName()Ljava/lang/String;

    move-result-object v1

    invoke-virtual {p0, v1}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object p0

    const-string v1, ":"

    invoke-virtual {p0, v1}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object p0

    .line 307
    invoke-virtual {v0}, Ljava/lang/Throwable;->getMessage()Ljava/lang/String;

    move-result-object v0

    invoke-static {v0}, Ljava/lang/String;->valueOf(Ljava/lang/Object;)Ljava/lang/String;

    move-result-object v0

    invoke-virtual {p0, v0}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object p0

    invoke-virtual {p0}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;

    move-result-object p0

    .line 305
    # invokes: Lwestlake/WlFix;->emitLine(Ljava/lang/String;)V
    invoke-static {p0}, Lwestlake/WlFix;->access$000(Ljava/lang/String;)V

    :goto_68
    return-void

    :catchall_69
    move-exception p0

    .line 304
    :try_start_6a
    monitor-exit v1
    :try_end_6b
    .catchall {:try_start_6a .. :try_end_6b} :catchall_69

    throw p0

    .line 311
    :cond_6c
    const-class v1, Lwestlake/WlFix;

    monitor-enter v1

    .line 312
    :try_start_6f
    # getter for: Lwestlake/WlFix;->assetProbeScheduled:Ljava/util/IdentityHashMap;
    invoke-static {}, Lwestlake/WlFix;->access$400()Ljava/util/IdentityHashMap;

    move-result-object v2

    iget-object v3, p0, Lwestlake/WlFix$5;->val$targetAwContents:Ljava/lang/Object;

    invoke-virtual {v2, v3}, Ljava/util/IdentityHashMap;->remove(Ljava/lang/Object;)Ljava/lang/Object;

    .line 313
    monitor-exit v1
    :try_end_79
    .catchall {:try_start_6f .. :try_end_79} :catchall_98

    .line 314
    new-instance v1, Ljava/lang/StringBuilder;

    const-string v2, "[WESTLAKE-WEBASSET-793] probe-finished submitted="

    invoke-direct {v1, v2}, Ljava/lang/StringBuilder;-><init>(Ljava/lang/String;)V

    invoke-virtual {v1, v0}, Ljava/lang/StringBuilder;->append(Z)Ljava/lang/StringBuilder;

    move-result-object v0

    const-string v1, " attempts="

    invoke-virtual {v0, v1}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v0

    iget p0, p0, Lwestlake/WlFix$5;->attempts:I

    invoke-virtual {v0, p0}, Ljava/lang/StringBuilder;->append(I)Ljava/lang/StringBuilder;

    move-result-object p0

    invoke-virtual {p0}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;

    move-result-object p0

    # invokes: Lwestlake/WlFix;->emitLine(Ljava/lang/String;)V
    invoke-static {p0}, Lwestlake/WlFix;->access$000(Ljava/lang/String;)V

    return-void

    :catchall_98
    move-exception p0

    .line 313
    :try_start_99
    monitor-exit v1
    :try_end_9a
    .catchall {:try_start_99 .. :try_end_9a} :catchall_98

    throw p0
.end method
