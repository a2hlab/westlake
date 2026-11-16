.class final Lwestlake/WlFix$JpegPollResult;
.super Ljava/lang/Object;
.source "WlFix.java"

# interfaces
.implements Ljava/lang/reflect/InvocationHandler;


# annotations
.annotation system Ldalvik/annotation/EnclosingClass;
    value = Lwestlake/WlFix;
.end annotation

.annotation system Ldalvik/annotation/InnerClass;
    accessFlags = 0x1a
    name = "JpegPollResult"
.end annotation


# instance fields
.field private final task:Lwestlake/WlFix$JpegPollTask;


# direct methods
.method constructor <init>(Lwestlake/WlFix$JpegPollTask;)V
    .registers 2

    .line 551
    invoke-direct {p0}, Ljava/lang/Object;-><init>()V

    .line 552
    iput-object p1, p0, Lwestlake/WlFix$JpegPollResult;->task:Lwestlake/WlFix$JpegPollTask;

    return-void
.end method


# virtual methods
.method public invoke(Ljava/lang/Object;Ljava/lang/reflect/Method;[Ljava/lang/Object;)Ljava/lang/Object;
    .registers 4

    const/4 p0, 0x0

    return-object p0
.end method
