.class Lwestlake/WlFix$1;
.super Ljava/lang/Object;
.source "WlFix.java"

# interfaces
.implements Ljava/lang/reflect/InvocationHandler;


# annotations
.annotation system Ldalvik/annotation/EnclosingMethod;
    value = Lwestlake/WlFix;->evaluateAssetProbe(Ljava/lang/Object;Ljava/lang/String;)V
.end annotation

.annotation system Ldalvik/annotation/InnerClass;
    accessFlags = 0x0
    name = null
.end annotation


# instance fields
.field final synthetic val$phase:Ljava/lang/String;


# direct methods
.method constructor <init>(Ljava/lang/String;)V
    .registers 2
    .annotation system Ldalvik/annotation/Signature;
        value = {
            "()V"
        }
    .end annotation

    .line 150
    iput-object p1, p0, Lwestlake/WlFix$1;->val$phase:Ljava/lang/String;

    invoke-direct {p0}, Ljava/lang/Object;-><init>()V

    return-void
.end method


# virtual methods
.method public invoke(Ljava/lang/Object;Ljava/lang/reflect/Method;[Ljava/lang/Object;)Ljava/lang/Object;
    .registers 5

    const-string p1, "onReceiveValue"

    .line 152
    invoke-virtual {p2}, Ljava/lang/reflect/Method;->getName()Ljava/lang/String;

    move-result-object p2

    invoke-virtual {p1, p2}, Ljava/lang/String;->equals(Ljava/lang/Object;)Z

    move-result p1

    if-eqz p1, :cond_46

    const/4 p1, 0x0

    if-eqz p3, :cond_19

    .line 153
    array-length p2, p3

    if-eqz p2, :cond_19

    .line 154
    aget-object p2, p3, p1

    invoke-static {p2}, Ljava/lang/String;->valueOf(Ljava/lang/Object;)Ljava/lang/String;

    move-result-object p2

    goto :goto_1b

    :cond_19
    const-string p2, "NULL"

    .line 155
    :goto_1b
    invoke-virtual {p2}, Ljava/lang/String;->length()I

    move-result p3

    const v0, 0xea60

    if-le p3, v0, :cond_28

    invoke-virtual {p2, p1, v0}, Ljava/lang/String;->substring(II)Ljava/lang/String;

    move-result-object p2

    .line 156
    :cond_28
    new-instance p1, Ljava/lang/StringBuilder;

    const-string p3, "[WESTLAKE-WEBASSET-793] phase="

    invoke-direct {p1, p3}, Ljava/lang/StringBuilder;-><init>(Ljava/lang/String;)V

    iget-object p0, p0, Lwestlake/WlFix$1;->val$phase:Ljava/lang/String;

    invoke-virtual {p1, p0}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object p0

    const-string p1, " result="

    invoke-virtual {p0, p1}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object p0

    invoke-virtual {p0, p2}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object p0

    invoke-virtual {p0}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;

    move-result-object p0

    # invokes: Lwestlake/WlFix;->emitLine(Ljava/lang/String;)V
    invoke-static {p0}, Lwestlake/WlFix;->access$000(Ljava/lang/String;)V

    :cond_46
    const/4 p0, 0x0

    return-object p0
.end method
