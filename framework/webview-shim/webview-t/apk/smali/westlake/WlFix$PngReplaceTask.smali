.class final Lwestlake/WlFix$PngReplaceTask;
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
    name = "PngReplaceTask"
.end annotation


# instance fields
.field private final id:Ljava/lang/String;

.field private final jpegBytes:I

.field private final png:[B

.field private final png64:Ljava/lang/String;

.field private final webView:Ljava/lang/Object;


# direct methods
.method constructor <init>(Ljava/lang/Object;Ljava/lang/String;I[B)V
    .registers 5

    .line 499
    invoke-direct {p0}, Ljava/lang/Object;-><init>()V

    .line 500
    iput-object p1, p0, Lwestlake/WlFix$PngReplaceTask;->webView:Ljava/lang/Object;

    .line 501
    iput-object p2, p0, Lwestlake/WlFix$PngReplaceTask;->id:Ljava/lang/String;

    .line 502
    iput p3, p0, Lwestlake/WlFix$PngReplaceTask;->jpegBytes:I

    .line 503
    iput-object p4, p0, Lwestlake/WlFix$PngReplaceTask;->png:[B

    .line 504
    invoke-static {}, Ljava/util/Base64;->getEncoder()Ljava/util/Base64$Encoder;

    move-result-object p1

    invoke-virtual {p1, p4}, Ljava/util/Base64$Encoder;->encodeToString([B)Ljava/lang/String;

    move-result-object p1

    iput-object p1, p0, Lwestlake/WlFix$PngReplaceTask;->png64:Ljava/lang/String;

    return-void
.end method


# virtual methods
.method public run()V
    .registers 8

    const-string v0, "[WESTLAKE-WEBJPEG-797] repaired id="

    const-string v1, "(function(){var x=window.__wlJpegNodes&&window.__wlJpegNodes[\'"

    .line 509
    :try_start_4
    new-instance v2, Ljava/lang/StringBuilder;

    invoke-direct {v2, v1}, Ljava/lang/StringBuilder;-><init>(Ljava/lang/String;)V

    iget-object v1, p0, Lwestlake/WlFix$PngReplaceTask;->id:Ljava/lang/String;

    invoke-virtual {v2, v1}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v1

    const-string v2, "\'];if(!x)return \'missing\';x.onload=function(){x.dataset.wlJpegState=\'repaired\'};x.onerror=function(){x.dataset.wlJpegState=\'pngerror\'};x.src=\'data:image/png;base64,"

    invoke-virtual {v1, v2}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v1

    iget-object v2, p0, Lwestlake/WlFix$PngReplaceTask;->png64:Ljava/lang/String;

    invoke-virtual {v1, v2}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v1

    const-string v2, "\';return \'set\'})()"

    invoke-virtual {v1, v2}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v1

    invoke-virtual {v1}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;

    move-result-object v1

    .line 514
    iget-object v2, p0, Lwestlake/WlFix$PngReplaceTask;->webView:Ljava/lang/Object;

    invoke-virtual {v2}, Ljava/lang/Object;->getClass()Ljava/lang/Class;

    move-result-object v2

    const-string v3, "evaluateJavascript"

    const/4 v4, 0x2

    new-array v4, v4, [Ljava/lang/Class;

    const-class v5, Ljava/lang/String;

    const/4 v6, 0x0

    aput-object v5, v4, v6

    const-string v5, "android.webkit.ValueCallback"

    .line 516
    invoke-static {v5}, Ljava/lang/Class;->forName(Ljava/lang/String;)Ljava/lang/Class;

    move-result-object v5

    const/4 v6, 0x1

    aput-object v5, v4, v6

    .line 514
    invoke-virtual {v2, v3, v4}, Ljava/lang/Class;->getMethod(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;

    move-result-object v2

    iget-object v3, p0, Lwestlake/WlFix$PngReplaceTask;->webView:Ljava/lang/Object;

    const/4 v4, 0x0

    filled-new-array {v1, v4}, [Ljava/lang/Object;

    move-result-object v1

    .line 517
    invoke-virtual {v2, v3, v1}, Ljava/lang/reflect/Method;->invoke(Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/Object;

    .line 518
    new-instance v1, Ljava/lang/StringBuilder;

    invoke-direct {v1, v0}, Ljava/lang/StringBuilder;-><init>(Ljava/lang/String;)V

    iget-object v0, p0, Lwestlake/WlFix$PngReplaceTask;->id:Ljava/lang/String;

    invoke-virtual {v1, v0}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v0

    const-string v1, " jpegBytes="

    invoke-virtual {v0, v1}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v0

    iget v1, p0, Lwestlake/WlFix$PngReplaceTask;->jpegBytes:I

    invoke-virtual {v0, v1}, Ljava/lang/StringBuilder;->append(I)Ljava/lang/StringBuilder;

    move-result-object v0

    const-string v1, " pngBytes="

    invoke-virtual {v0, v1}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v0

    iget-object v1, p0, Lwestlake/WlFix$PngReplaceTask;->png:[B

    array-length v1, v1

    invoke-virtual {v0, v1}, Ljava/lang/StringBuilder;->append(I)Ljava/lang/StringBuilder;

    move-result-object v0

    invoke-virtual {v0}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;

    move-result-object v0

    # invokes: Lwestlake/WlFix;->emitLine(Ljava/lang/String;)V
    invoke-static {v0}, Lwestlake/WlFix;->access$000(Ljava/lang/String;)V
    :try_end_77
    .catchall {:try_start_4 .. :try_end_77} :catchall_78

    goto :goto_b1

    :catchall_78
    move-exception v0

    .line 521
    new-instance v1, Ljava/lang/StringBuilder;

    const-string v2, "[WESTLAKE-WEBJPEG-797] replace-error id="

    invoke-direct {v1, v2}, Ljava/lang/StringBuilder;-><init>(Ljava/lang/String;)V

    iget-object p0, p0, Lwestlake/WlFix$PngReplaceTask;->id:Ljava/lang/String;

    invoke-virtual {v1, p0}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object p0

    const-string v1, " "

    invoke-virtual {p0, v1}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object p0

    .line 522
    invoke-virtual {v0}, Ljava/lang/Object;->getClass()Ljava/lang/Class;

    move-result-object v1

    invoke-virtual {v1}, Ljava/lang/Class;->getName()Ljava/lang/String;

    move-result-object v1

    invoke-virtual {p0, v1}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object p0

    const-string v1, ":"

    invoke-virtual {p0, v1}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object p0

    .line 523
    invoke-virtual {v0}, Ljava/lang/Throwable;->getMessage()Ljava/lang/String;

    move-result-object v0

    invoke-static {v0}, Ljava/lang/String;->valueOf(Ljava/lang/Object;)Ljava/lang/String;

    move-result-object v0

    invoke-virtual {p0, v0}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object p0

    invoke-virtual {p0}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;

    move-result-object p0

    .line 521
    # invokes: Lwestlake/WlFix;->emitLine(Ljava/lang/String;)V
    invoke-static {p0}, Lwestlake/WlFix;->access$000(Ljava/lang/String;)V

    :goto_b1
    return-void
.end method
