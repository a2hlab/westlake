.class Lwestlake/WlFix$3;
.super Ljava/lang/Object;
.source "WlFix.java"

# interfaces
.implements Ljava/lang/Runnable;


# annotations
.annotation system Ldalvik/annotation/EnclosingMethod;
    value = Lwestlake/WlFix;->submitAssetProbe(Ljava/lang/Object;)Z
.end annotation

.annotation system Ldalvik/annotation/InnerClass;
    accessFlags = 0x0
    name = null
.end annotation


# instance fields
.field final synthetic val$webView:Ljava/lang/Object;


# direct methods
.method constructor <init>(Ljava/lang/Object;)V
    .registers 2
    .annotation system Ldalvik/annotation/Signature;
        value = {
            "()V"
        }
    .end annotation

    .line 246
    iput-object p1, p0, Lwestlake/WlFix$3;->val$webView:Ljava/lang/Object;

    invoke-direct {p0}, Ljava/lang/Object;-><init>()V

    return-void
.end method


# virtual methods
.method public run()V
    .registers 1

    .line 248
    iget-object p0, p0, Lwestlake/WlFix$3;->val$webView:Ljava/lang/Object;

    # invokes: Lwestlake/WlFix;->evaluateNativeFallbackDiagnostic(Ljava/lang/Object;)V
    invoke-static {p0}, Lwestlake/WlFix;->access$100(Ljava/lang/Object;)V

    return-void
.end method
