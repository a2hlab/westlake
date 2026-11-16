.class Lwestlake/WlFix$2;
.super Ljava/lang/Object;
.source "WlFix.java"

# interfaces
.implements Ljava/lang/reflect/InvocationHandler;


# annotations
.annotation system Ldalvik/annotation/EnclosingMethod;
    value = Lwestlake/WlFix;->evaluateNativeFallbackDiagnostic(Ljava/lang/Object;)V
.end annotation

.annotation system Ldalvik/annotation/InnerClass;
    accessFlags = 0x0
    name = null
.end annotation


# direct methods
.method constructor <init>()V
    .registers 1

    .line 190
    invoke-direct {p0}, Ljava/lang/Object;-><init>()V

    return-void
.end method


# virtual methods
.method public invoke(Ljava/lang/Object;Ljava/lang/reflect/Method;[Ljava/lang/Object;)Ljava/lang/Object;
    .registers 4

    const-string p0, "onReceiveValue"

    .line 192
    invoke-virtual {p2}, Ljava/lang/reflect/Method;->getName()Ljava/lang/String;

    move-result-object p1

    invoke-virtual {p0, p1}, Ljava/lang/String;->equals(Ljava/lang/Object;)Z

    move-result p0

    if-eqz p0, :cond_2d

    .line 193
    new-instance p0, Ljava/lang/StringBuilder;

    const-string p1, "[WESTLAKE-WEBNATIVE-794] result="

    invoke-direct {p0, p1}, Ljava/lang/StringBuilder;-><init>(Ljava/lang/String;)V

    if-eqz p3, :cond_20

    .line 194
    array-length p1, p3

    if-eqz p1, :cond_20

    const/4 p1, 0x0

    .line 195
    aget-object p1, p3, p1

    invoke-static {p1}, Ljava/lang/String;->valueOf(Ljava/lang/Object;)Ljava/lang/String;

    move-result-object p1

    goto :goto_22

    :cond_20
    const-string p1, "NULL"

    :goto_22
    invoke-virtual {p0, p1}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object p0

    invoke-virtual {p0}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;

    move-result-object p0

    .line 193
    # invokes: Lwestlake/WlFix;->emitLine(Ljava/lang/String;)V
    invoke-static {p0}, Lwestlake/WlFix;->access$000(Ljava/lang/String;)V

    :cond_2d
    const/4 p0, 0x0

    return-object p0
.end method
