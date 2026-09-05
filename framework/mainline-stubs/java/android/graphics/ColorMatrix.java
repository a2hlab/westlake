package android.graphics;

import java.util.Arrays;

/**
 * WESTLAKE §675 (2026-08-18) — real android.graphics.ColorMatrix.
 *
 * The shipped adapter-mainline-stubs.jar carried a stub whose entire body was `<init>()V`.
 * Toutiao's AsyncImageView and UiUtils both call ColorMatrix.set(float[]) from their class
 * initializers. This is pure Java in AOSP, so keep its real matrix behavior here.
 */
public class ColorMatrix {

    private final float[] mArray = new float[20];

    public ColorMatrix() {
        reset();
    }

    public ColorMatrix(float[] src) {
        System.arraycopy(src, 0, mArray, 0, 20);
    }

    public ColorMatrix(ColorMatrix src) {
        System.arraycopy(src.mArray, 0, mArray, 0, 20);
    }

    public final float[] getArray() {
        return mArray;
    }

    public void reset() {
        final float[] a = mArray;
        Arrays.fill(a, 0);
        a[0] = a[6] = a[12] = a[18] = 1;
    }

    public void set(ColorMatrix src) {
        System.arraycopy(src.mArray, 0, mArray, 0, 20);
    }

    public void set(float[] src) {
        System.arraycopy(src, 0, mArray, 0, 20);
    }

    public void setScale(float rScale, float gScale, float bScale, float aScale) {
        final float[] a = mArray;
        for (int i = 19; i > 0; --i) {
            a[i] = 0;
        }
        a[0] = rScale;
        a[6] = gScale;
        a[12] = bScale;
        a[18] = aScale;
    }

    public void setRotate(int axis, float degrees) {
        reset();
        double radians = degrees * Math.PI / 180d;
        float cosine = (float) Math.cos(radians);
        float sine = (float) Math.sin(radians);
        switch (axis) {
            case 0:
                mArray[6] = mArray[12] = cosine;
                mArray[7] = sine;
                mArray[11] = -sine;
                break;
            case 1:
                mArray[0] = mArray[12] = cosine;
                mArray[2] = -sine;
                mArray[10] = sine;
                break;
            case 2:
                mArray[0] = mArray[6] = cosine;
                mArray[1] = sine;
                mArray[5] = -sine;
                break;
            default:
                throw new RuntimeException();
        }
    }

    public void setConcat(ColorMatrix matA, ColorMatrix matB) {
        float[] tmp = new float[20];
        int index = 0;
        for (int j = 0; j < 20; j += 5) {
            for (int i = 0; i < 4; i++) {
                tmp[index++] = matA.mArray[j] * matB.mArray[i]
                        + matA.mArray[j + 1] * matB.mArray[i + 5]
                        + matA.mArray[j + 2] * matB.mArray[i + 10]
                        + matA.mArray[j + 3] * matB.mArray[i + 15];
            }
            tmp[index++] = matA.mArray[j] * matB.mArray[4]
                    + matA.mArray[j + 1] * matB.mArray[9]
                    + matA.mArray[j + 2] * matB.mArray[14]
                    + matA.mArray[j + 3] * matB.mArray[19]
                    + matA.mArray[j + 4];
        }
        System.arraycopy(tmp, 0, mArray, 0, 20);
    }

    public void preConcat(ColorMatrix prematrix) {
        setConcat(this, prematrix);
    }

    public void postConcat(ColorMatrix postmatrix) {
        setConcat(postmatrix, this);
    }

    public void setSaturation(float sat) {
        reset();
        float[] m = mArray;
        final float invSat = 1 - sat;
        final float r = 0.213f * invSat;
        final float g = 0.715f * invSat;
        final float b = 0.072f * invSat;
        m[0] = r + sat;
        m[1] = g;
        m[2] = b;
        m[5] = r;
        m[6] = g + sat;
        m[7] = b;
        m[10] = r;
        m[11] = g;
        m[12] = b + sat;
    }

    public void setRGB2YUV() {
        reset();
        float[] m = mArray;
        m[0] = 0.299f;
        m[1] = 0.587f;
        m[2] = 0.114f;
        m[5] = -0.16874f;
        m[6] = -0.33126f;
        m[7] = 0.5f;
        m[10] = 0.5f;
        m[11] = -0.41869f;
        m[12] = -0.08131f;
    }

    public void setYUV2RGB() {
        reset();
        float[] m = mArray;
        m[2] = 1.402f;
        m[5] = 1;
        m[6] = -0.34414f;
        m[7] = -0.71414f;
        m[10] = 1;
        m[11] = 1.772f;
        m[12] = 0;
    }

    @Override
    public boolean equals(Object obj) {
        if (this == obj) return true;
        if (!(obj instanceof ColorMatrix)) return false;
        return Arrays.equals(mArray, ((ColorMatrix) obj).mArray);
    }

    @Override
    public int hashCode() {
        return Arrays.hashCode(mArray);
    }

    @Override
    public String toString() {
        return "ColorMatrix" + Arrays.toString(mArray);
    }
}
