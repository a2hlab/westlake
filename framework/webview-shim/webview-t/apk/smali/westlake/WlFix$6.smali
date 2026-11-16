.class Lwestlake/WlFix$6;
.super Ljava/lang/Object;
.source "WlFix.java"

# interfaces
.implements Ljava/lang/Runnable;


# annotations
.annotation system Ldalvik/annotation/EnclosingMethod;
    value = Lwestlake/WlFix;->replaceJpegWithPng(Ljava/lang/Object;Ljava/lang/String;[B)V
.end annotation

.annotation system Ldalvik/annotation/InnerClass;
    accessFlags = 0x0
    name = null
.end annotation


# instance fields
.field final synthetic val$id:Ljava/lang/String;

.field final synthetic val$jpeg:[B

.field final synthetic val$webView:Ljava/lang/Object;


# direct methods
.method constructor <init>([BLjava/lang/Object;Ljava/lang/String;)V
    .registers 4
    .annotation system Ldalvik/annotation/Signature;
        value = {
            "()V"
        }
    .end annotation

    .line 530
    iput-object p1, p0, Lwestlake/WlFix$6;->val$jpeg:[B

    iput-object p2, p0, Lwestlake/WlFix$6;->val$webView:Ljava/lang/Object;

    iput-object p3, p0, Lwestlake/WlFix$6;->val$id:Ljava/lang/String;

    invoke-direct {p0}, Ljava/lang/Object;-><init>()V

    return-void
.end method


# virtual methods
.method public run()V
    .registers 10

    :try_start_0
    const-string v0, "com.ohos.shim.bridge.OHBridge"

    .line 533
    invoke-static {v0}, Ljava/lang/Class;->forName(Ljava/lang/String;)Ljava/lang/Class;

    move-result-object v0

    const-string v1, "imageDecodeToPixels"

    const/4 v2, 0x1

    new-array v3, v2, [Ljava/lang/Class;

    .line 534
    const-class v4, [B

    const/4 v5, 0x0

    aput-object v4, v3, v5

    invoke-virtual {v0, v1, v3}, Ljava/lang/Class;->getMethod(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;

    move-result-object v0

    new-array v1, v2, [Ljava/lang/Object;

    iget-object v3, p0, Lwestlake/WlFix$6;->val$jpeg:[B

    aput-object v3, v1, v5

    const/4 v3, 0x0

    .line 535
    invoke-virtual {v0, v3, v1}, Ljava/lang/reflect/Method;->invoke(Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/Object;

    move-result-object v0

    check-cast v0, [I

    .line 536
    # invokes: Lwestlake/WlFix;->encodeArgbPng([I)[B
    invoke-static {v0}, Lwestlake/WlFix;->access$500([I)[B

    move-result-object v0

    .line 537
    iget-object v1, p0, Lwestlake/WlFix$6;->val$webView:Ljava/lang/Object;

    invoke-virtual {v1}, Ljava/lang/Object;->getClass()Ljava/lang/Class;

    move-result-object v1

    const-string v3, "post"

    new-array v4, v2, [Ljava/lang/Class;

    const-class v6, Ljava/lang/Runnable;

    aput-object v6, v4, v5

    invoke-virtual {v1, v3, v4}, Ljava/lang/Class;->getMethod(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;

    move-result-object v1

    iget-object v3, p0, Lwestlake/WlFix$6;->val$webView:Ljava/lang/Object;

    new-array v2, v2, [Ljava/lang/Object;

    new-instance v4, Lwestlake/WlFix$PngReplaceTask;

    iget-object v6, p0, Lwestlake/WlFix$6;->val$webView:Ljava/lang/Object;

    iget-object v7, p0, Lwestlake/WlFix$6;->val$id:Ljava/lang/String;

    iget-object v8, p0, Lwestlake/WlFix$6;->val$jpeg:[B

    array-length v8, v8

    invoke-direct {v4, v6, v7, v8, v0}, Lwestlake/WlFix$PngReplaceTask;-><init>(Ljava/lang/Object;Ljava/lang/String;I[B)V

    aput-object v4, v2, v5

    invoke-virtual {v1, v3, v2}, Ljava/lang/reflect/Method;->invoke(Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/Object;
    :try_end_4c
    .catchall {:try_start_0 .. :try_end_4c} :catchall_4d

    goto :goto_86

    :catchall_4d
    move-exception v0

    .line 540
    new-instance v1, Ljava/lang/StringBuilder;

    const-string v2, "[WESTLAKE-WEBJPEG-797] decode-error id="

    invoke-direct {v1, v2}, Ljava/lang/StringBuilder;-><init>(Ljava/lang/String;)V

    iget-object p0, p0, Lwestlake/WlFix$6;->val$id:Ljava/lang/String;

    invoke-virtual {v1, p0}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object p0

    const-string v1, " "

    invoke-virtual {p0, v1}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object p0

    .line 541
    invoke-virtual {v0}, Ljava/lang/Object;->getClass()Ljava/lang/Class;

    move-result-object v1

    invoke-virtual {v1}, Ljava/lang/Class;->getName()Ljava/lang/String;

    move-result-object v1

    invoke-virtual {p0, v1}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object p0

    const-string v1, ":"

    invoke-virtual {p0, v1}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object p0

    .line 542
    invoke-virtual {v0}, Ljava/lang/Throwable;->getMessage()Ljava/lang/String;

    move-result-object v0

    invoke-static {v0}, Ljava/lang/String;->valueOf(Ljava/lang/Object;)Ljava/lang/String;

    move-result-object v0

    invoke-virtual {p0, v0}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object p0

    invoke-virtual {p0}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;

    move-result-object p0

    .line 540
    # invokes: Lwestlake/WlFix;->emitLine(Ljava/lang/String;)V
    invoke-static {p0}, Lwestlake/WlFix;->access$000(Ljava/lang/String;)V

    :goto_86
    return-void
.end method
