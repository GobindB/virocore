/*
 * Copyright © 2017 Viro Media. All rights reserved.
 */
package com.viro.renderer.jni;

public class ARPlaneJni extends ARNodeJni {

    public ARPlaneJni(float minWidth, float minHeight) {
        super();
        setNativeRef(nativeCreateARPlane(minWidth, minHeight));
    }

    public void destroy() {
        super.destroy();
        nativeDestroyARPlane(mNativeRef);
    }

    @Override
    protected long createNativeARNodeDelegate(long nativeRef) {
        return nativeCreateARPlaneDelegate(nativeRef);
    }

    @Override
    void destroyNativeARNodeDelegate(long delegateRef) {
        nativeDestroyARPlaneDelegate(delegateRef);
    }

    public void setMinWidth(float minWidth) {
        nativeSetMinWidth(mNativeRef, minWidth);
    }

    public void setMinHeight(float minHeight) {
        nativeSetMinHeight(mNativeRef, minHeight);
    }

    private native long nativeCreateARPlane(float minWidth, float minHeight);

    private native void nativeDestroyARPlane(long nativeRef);

    private native void nativeSetMinWidth(long nativeRef, float minWidth);

    private native void nativeSetMinHeight(long nativeRef, float minHeight);

    private native long nativeCreateARPlaneDelegate(long nativeRef);

    private native void nativeDestroyARPlaneDelegate(long delegateRef);
}
