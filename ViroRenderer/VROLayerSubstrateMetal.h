//
//  VROLayerSubstrateMetal.h
//  ViroRenderer
//
//  Created by Raj Advani on 10/20/15.
//  Copyright © 2015 Viro Media. All rights reserved.
//

#ifndef VROLayerSubstrateMetal_h
#define VROLayerSubstrateMetal_h

#include "VROLayerSubstrate.h"
#include <Metal/Metal.h>
#include <MetalKit/MetalKit.h>

class VROLayerSubstrateMetal : public VROLayerSubstrate {
    
public:
    
    VROLayerSubstrateMetal(const VRORenderContext &context);
    virtual ~VROLayerSubstrateMetal() {}
    
    void render(const VRORenderContext &context,
                VROMatrix4f mv,
                vector_float4 bgColor);
    void setContents(const void *data, size_t dataLength, size_t width, size_t height);
    
private:
    
    id <MTLDevice> _device;
    
    id <MTLRenderPipelineState> _pipelineState;
    id <MTLDepthStencilState> _depthState;
    
    id <MTLBuffer> _vertexBuffer;
    id <MTLBuffer> _uniformsBuffer;
    id <MTLTexture> _texture;
    
    void hydrate(const VRORenderContext &context);
};

#endif /* VROLayerSubstrateMetal_h */
