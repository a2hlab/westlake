.class Lwestlake/WlFix$4;
.super Ljava/lang/Object;
.source "WlFix.java"

# interfaces
.implements Ljava/lang/Runnable;


# annotations
.annotation system Ldalvik/annotation/EnclosingMethod;
    value = Lwestlake/WlFix;->scheduleEvaluation(Ljava/lang/Object;Ljava/lang/String;J)V
.end annotation

.annotation system Ldalvik/annotation/InnerClass;
    accessFlags = 0x0
    name = null
.end annotation


# instance fields
.field final synthetic val$phase:Ljava/lang/String;

.field final synthetic val$webView:Ljava/lang/Object;


# direct methods
.method constructor <init>(Ljava/lang/Object;Ljava/lang/String;)V
    .registers 3
    .annotation system Ldalvik/annotation/Signature;
        value = {
            "()V"
        }
    .end annotation

    .line 264
    iput-object p1, p0, Lwestlake/WlFix$4;->val$webView:Ljava/lang/Object;

    iput-object p2, p0, Lwestlake/WlFix$4;->val$phase:Ljava/lang/String;

    invoke-direct {p0}, Ljava/lang/Object;-><init>()V

    return-void
.end method


# virtual methods
.method public run()V
    .registers 2

    .line 266
    iget-object v0, p0, Lwestlake/WlFix$4;->val$webView:Ljava/lang/Object;

    iget-object p0, p0, Lwestlake/WlFix$4;->val$phase:Ljava/lang/String;

    # invokes: Lwestlake/WlFix;->evaluateAssetProbe(Ljava/lang/Object;Ljava/lang/String;)V
    invoke-static {v0, p0}, Lwestlake/WlFix;->access$200(Ljava/lang/Object;Ljava/lang/String;)V

    return-void
.end method
