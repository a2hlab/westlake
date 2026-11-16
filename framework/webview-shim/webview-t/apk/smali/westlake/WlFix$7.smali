.class Lwestlake/WlFix$7;
.super Ljava/lang/Object;
.source "WlFix.java"

# interfaces
.implements Ljava/lang/Runnable;


# annotations
.annotation system Ldalvik/annotation/EnclosingMethod;
    value = Lwestlake/WlFix;->scheduleJpegRepair(Ljava/lang/Object;)V
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

    .line 726
    iput-object p1, p0, Lwestlake/WlFix$7;->val$targetAwContents:Ljava/lang/Object;

    iput-object p2, p0, Lwestlake/WlFix$7;->val$postDelayed:Ljava/lang/reflect/Method;

    iput-object p3, p0, Lwestlake/WlFix$7;->val$handler:Ljava/lang/Object;

    iput-object p4, p0, Lwestlake/WlFix$7;->val$task:[Ljava/lang/Runnable;

    invoke-direct {p0}, Ljava/lang/Object;-><init>()V

    return-void
.end method


# virtual methods
.method public run()V
    .registers 7

    .line 730
    iget v0, p0, Lwestlake/WlFix$7;->attempts:I

    const/4 v1, 0x1

    add-int/2addr v0, v1

    iput v0, p0, Lwestlake/WlFix$7;->attempts:I

    .line 732
    :try_start_6
    iget-object v0, p0, Lwestlake/WlFix$7;->val$targetAwContents:Ljava/lang/Object;

    # invokes: Lwestlake/WlFix;->findWebViewForAwContents(Ljava/lang/Object;)Ljava/lang/Object;
    invoke-static {v0}, Lwestlake/WlFix;->access$800(Ljava/lang/Object;)Ljava/lang/Object;

    move-result-object v0

    if-eqz v0, :cond_48

    .line 734
    const-class v2, Lwestlake/WlFix;

    monitor-enter v2
    :try_end_11
    .catchall {:try_start_6 .. :try_end_11} :catchall_2d

    .line 735
    :try_start_11
    # getter for: Lwestlake/WlFix;->jpegRepairScheduled:Ljava/util/IdentityHashMap;
    invoke-static {}, Lwestlake/WlFix;->access$900()Ljava/util/IdentityHashMap;

    move-result-object v3

    iget-object v4, p0, Lwestlake/WlFix$7;->val$targetAwContents:Ljava/lang/Object;

    invoke-virtual {v3, v4}, Ljava/util/IdentityHashMap;->remove(Ljava/lang/Object;)Ljava/lang/Object;

    .line 736
    # getter for: Lwestlake/WlFix;->jpegRepairStarted:Ljava/util/IdentityHashMap;
    invoke-static {}, Lwestlake/WlFix;->access$1000()Ljava/util/IdentityHashMap;

    move-result-object v3

    iget-object v4, p0, Lwestlake/WlFix$7;->val$targetAwContents:Ljava/lang/Object;

    sget-object v5, Ljava/lang/Boolean;->TRUE:Ljava/lang/Boolean;

    invoke-virtual {v3, v4, v5}, Ljava/util/IdentityHashMap;->put(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;

    .line 737
    monitor-exit v2
    :try_end_26
    .catchall {:try_start_11 .. :try_end_26} :catchall_2a

    .line 738
    :try_start_26
    # invokes: Lwestlake/WlFix;->startJpegRepairLoop(Ljava/lang/Object;)V
    invoke-static {v0}, Lwestlake/WlFix;->access$1100(Ljava/lang/Object;)V
    :try_end_29
    .catchall {:try_start_26 .. :try_end_29} :catchall_2d

    return-void

    :catchall_2a
    move-exception v0

    .line 737
    :try_start_2b
    monitor-exit v2
    :try_end_2c
    .catchall {:try_start_2b .. :try_end_2c} :catchall_2a

    :try_start_2c
    throw v0
    :try_end_2d
    .catchall {:try_start_2c .. :try_end_2d} :catchall_2d

    :catchall_2d
    move-exception v0

    .line 742
    new-instance v2, Ljava/lang/StringBuilder;

    const-string v3, "[WESTLAKE-WEBJPEG-797] find-error "

    invoke-direct {v2, v3}, Ljava/lang/StringBuilder;-><init>(Ljava/lang/String;)V

    .line 743
    invoke-virtual {v0}, Ljava/lang/Object;->getClass()Ljava/lang/Class;

    move-result-object v0

    invoke-virtual {v0}, Ljava/lang/Class;->getName()Ljava/lang/String;

    move-result-object v0

    invoke-virtual {v2, v0}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v0

    invoke-virtual {v0}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;

    move-result-object v0

    .line 742
    # invokes: Lwestlake/WlFix;->emitLine(Ljava/lang/String;)V
    invoke-static {v0}, Lwestlake/WlFix;->access$000(Ljava/lang/String;)V

    .line 745
    :cond_48
    iget v0, p0, Lwestlake/WlFix$7;->attempts:I

    const/16 v2, 0x28

    if-ge v0, v2, :cond_68

    .line 747
    :try_start_4e
    iget-object v0, p0, Lwestlake/WlFix$7;->val$postDelayed:Ljava/lang/reflect/Method;

    iget-object v2, p0, Lwestlake/WlFix$7;->val$handler:Ljava/lang/Object;

    const/4 v3, 0x2

    new-array v3, v3, [Ljava/lang/Object;

    iget-object v4, p0, Lwestlake/WlFix$7;->val$task:[Ljava/lang/Runnable;

    const/4 v5, 0x0

    aget-object v4, v4, v5

    aput-object v4, v3, v5

    const-wide/16 v4, 0xbb8

    invoke-static {v4, v5}, Ljava/lang/Long;->valueOf(J)Ljava/lang/Long;

    move-result-object v4

    aput-object v4, v3, v1

    invoke-virtual {v0, v2, v3}, Ljava/lang/reflect/Method;->invoke(Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/Object;
    :try_end_67
    .catchall {:try_start_4e .. :try_end_67} :catchall_68

    return-void

    .line 752
    :catchall_68
    :cond_68
    const-class v0, Lwestlake/WlFix;

    monitor-enter v0

    .line 753
    :try_start_6b
    # getter for: Lwestlake/WlFix;->jpegRepairScheduled:Ljava/util/IdentityHashMap;
    invoke-static {}, Lwestlake/WlFix;->access$900()Ljava/util/IdentityHashMap;

    move-result-object v1

    iget-object v2, p0, Lwestlake/WlFix$7;->val$targetAwContents:Ljava/lang/Object;

    invoke-virtual {v1, v2}, Ljava/util/IdentityHashMap;->remove(Ljava/lang/Object;)Ljava/lang/Object;

    .line 754
    monitor-exit v0
    :try_end_75
    .catchall {:try_start_6b .. :try_end_75} :catchall_8a

    .line 755
    new-instance v0, Ljava/lang/StringBuilder;

    const-string v1, "[WESTLAKE-WEBJPEG-797] find-gave-up attempts="

    invoke-direct {v0, v1}, Ljava/lang/StringBuilder;-><init>(Ljava/lang/String;)V

    iget p0, p0, Lwestlake/WlFix$7;->attempts:I

    invoke-virtual {v0, p0}, Ljava/lang/StringBuilder;->append(I)Ljava/lang/StringBuilder;

    move-result-object p0

    invoke-virtual {p0}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;

    move-result-object p0

    # invokes: Lwestlake/WlFix;->emitLine(Ljava/lang/String;)V
    invoke-static {p0}, Lwestlake/WlFix;->access$000(Ljava/lang/String;)V

    return-void

    :catchall_8a
    move-exception p0

    .line 754
    :try_start_8b
    monitor-exit v0
    :try_end_8c
    .catchall {:try_start_8b .. :try_end_8c} :catchall_8a

    throw p0
.end method
