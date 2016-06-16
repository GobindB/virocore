//
//  VROMaterialSubstrateOpenGL.cpp
//  ViroRenderer
//
//  Created by Raj Advani on 5/2/16.
//  Copyright © 2016 Viro Media. All rights reserved.
//

#include "VROMaterialSubstrateOpenGL.h"
#include "VRODriverOpenGL.h"
#include "VROMaterial.h"
#include "VROShaderBuilder.h"
#include "VROShaderProgram.h"
#include "VROAllocationTracker.h"
#include "VROEye.h"
#include "VROLight.h"
#include "VRORenderParameters.h"
#include "VROSortKey.h"
#include <sstream>

static const int kMaxLights = 4;
static std::map<std::string, std::shared_ptr<VROShaderProgram>> _sharedPrograms;

static GLuint _lightingUBO = 0;
static const int _lightingUBOBindingPoint = 0;

// Grouped in 4N slots, matching lighting_general_functions.glsl
typedef struct {
    int type;
    float attenuation_start_distance;
    float attenuation_end_distance;
    float attenuation_falloff_exp;
    
    float position[4];
    float direction[4];
    
    float color[3];
    float spot_inner_angle;
    
    float spot_outer_angle;
    float padding3;
    float padding4;
    float padding5;
} VROLightData;

typedef struct {
    int num_lights;
    float padding0, padding1, padding2;
    
    float ambient_light_color[4];
    VROLightData lights[8];
} VROLightingData;

void VROMaterialSubstrateOpenGL::initLightingUBO() {
    if (_lightingUBO > 0) {
        return;
    }
    
    glGenBuffers(1, &_lightingUBO);
    glBindBuffer(GL_UNIFORM_BUFFER, _lightingUBO);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(VROLightingData), NULL, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
    
    glBindBufferBase(GL_UNIFORM_BUFFER, _lightingUBOBindingPoint, _lightingUBO);
}

void VROMaterialSubstrateOpenGL::hydrateProgram() {
    _program->hydrate();
    
    unsigned int blockIndex = glGetUniformBlockIndex(_program->getProgram(), "lighting");
    glUniformBlockBinding(_program->getProgram(), blockIndex, _lightingUBOBindingPoint);
}

VROMaterialSubstrateOpenGL::VROMaterialSubstrateOpenGL(const VROMaterial &material, const VRODriverOpenGL &driver) :
    _material(material),
    _lightingModel(material.getLightingModel()),
    _program(nullptr),
    _diffuseSurfaceColorUniform(nullptr),
    _diffuseIntensityUniform(nullptr),
    _alphaUniform(nullptr),
    _shininessUniform(nullptr),
    _normalMatrixUniform(nullptr),
    _modelMatrixUniform(nullptr),
    _modelViewMatrixUniform(nullptr),
    _modelViewProjectionMatrixUniform(nullptr),
    _cameraPositionUniform(nullptr) {
        
    initLightingUBO();

    switch (material.getLightingModel()) {
        case VROLightingModel::Constant:
            loadConstantLighting(material, driver);
            break;
                
        case VROLightingModel::Blinn:
            loadBlinnLighting(material, driver);
            break;
                
        case VROLightingModel::Lambert:
            loadLambertLighting(material, driver);
            break;
                
        case VROLightingModel::Phong:
            loadPhongLighting(material, driver);
            break;
                
        default:
            break;
    }
        
    ALLOCATION_TRACKER_ADD(MaterialSubstrates, 1);
}
    
VROMaterialSubstrateOpenGL::~VROMaterialSubstrateOpenGL() {
    ALLOCATION_TRACKER_SUB(MaterialSubstrates, 1);
}

void VROMaterialSubstrateOpenGL::loadConstantLighting(const VROMaterial &material, const VRODriverOpenGL &driver) {
    VROMaterialVisual &diffuse = material.getDiffuse();
    
    std::string vertexShader = "constant_vsh";
    std::string fragmentShader;
    
    std::vector<std::string> samplers;
    
    if (diffuse.getContentsType() == VROContentsType::Fixed) {
        fragmentShader = "constant_c_fsh";
    }
    else if (diffuse.getContentsType() == VROContentsType::Texture2D) {
        _textures.push_back(diffuse.getContentsTexture());
        samplers.push_back("sampler");

        fragmentShader = "constant_t_fsh";
    }
    else {
        _textures.push_back(diffuse.getContentsTexture());
        samplers.push_back("sampler");

        fragmentShader = "constant_q_fsh";
    }
    
    _program = getPooledShader(vertexShader, fragmentShader, samplers);
    if (!_program->isHydrated()) {
        addUniforms();
        hydrateProgram();
    }
    else {
        loadUniforms();
    }
}

void VROMaterialSubstrateOpenGL::loadLambertLighting(const VROMaterial &material, const VRODriverOpenGL &driver) {
    std::string vertexShader = "lambert_vsh";
    std::string fragmentShader;
    
    std::vector<std::string> samplers;
    
    VROMaterialVisual &diffuse = material.getDiffuse();
    VROMaterialVisual &reflective = material.getReflective();
    
    if (diffuse.getContentsType() == VROContentsType::Fixed) {
        if (reflective.getContentsType() == VROContentsType::TextureCube) {
            _textures.push_back(reflective.getContentsTexture());
            samplers.push_back("reflect_texture");

            fragmentShader = "lambert_c_reflect_fsh";
        }
        else {
            fragmentShader = "lambert_c_fsh";
        }
    }
    else {
        _textures.push_back(diffuse.getContentsTexture());
        samplers.push_back("texture");
        
        if (reflective.getContentsType() == VROContentsType::TextureCube) {
            _textures.push_back(reflective.getContentsTexture());
            samplers.push_back("reflect_texture");
            
            fragmentShader = "lambert_t_reflect_fsh";
        }
        else {
            fragmentShader = "lambert_t_fsh";
        }
    }
    
    _program = getPooledShader(vertexShader, fragmentShader, samplers);
    if (!_program->isHydrated()) {
        addUniforms();
        hydrateProgram();
    }
    else {
        loadUniforms();
    }
}

void VROMaterialSubstrateOpenGL::loadPhongLighting(const VROMaterial &material, const VRODriverOpenGL &driver) {
    std::string vertexShader = "phong_vsh";
    std::string fragmentShader;
    
    std::vector<std::string> samplers;
    
    /*
     If there's no specular map, then we fall back to Lambert lighting.
     */
    VROMaterialVisual &specular = material.getSpecular();
    if (specular.getContentsType() != VROContentsType::Texture2D) {
        loadLambertLighting(material, driver);
        return;
    }
    
    VROMaterialVisual &diffuse = material.getDiffuse();
    VROMaterialVisual &reflective = material.getReflective();
    
    if (diffuse.getContentsType() == VROContentsType::Fixed) {
        _textures.push_back(specular.getContentsTexture());
        samplers.push_back("specular_texture");
        
        if (reflective.getContentsType() == VROContentsType::TextureCube) {
            _textures.push_back(reflective.getContentsTexture());
            samplers.push_back("reflect_texture");
            
            fragmentShader = "phong_c_reflect_fsh";
        }
        else {
            fragmentShader = "phong_c_fsh";
        }
    }
    else {
        _textures.push_back(diffuse.getContentsTexture());
        _textures.push_back(specular.getContentsTexture());
        
        samplers.push_back("diffuse_texture");
        samplers.push_back("specular_texture");
        
        if (reflective.getContentsType() == VROContentsType::TextureCube) {
            _textures.push_back(reflective.getContentsTexture());
            samplers.push_back("reflect_texture");

            fragmentShader = "phong_t_reflect_fsh";
        }
        else {
            fragmentShader = "phong_t_fsh";
        }
    }
    
    _program = getPooledShader(vertexShader, fragmentShader, samplers);
    if (!_program->isHydrated()) {
        addUniforms();
        _shininessUniform = _program->addUniform(VROShaderProperty::Float, 1, "material_shininess");
        hydrateProgram();
    }
    else {
        _shininessUniform = _program->getUniform("material_shininess");
        loadUniforms();
    }
}

void VROMaterialSubstrateOpenGL::loadBlinnLighting(const VROMaterial &material, const VRODriverOpenGL &driver) {
    std::string vertexShader = "blinn_vsh";
    std::string fragmentShader;
    
    std::vector<std::string> samplers;
    
    /*
     If there's no specular map, then we fall back to Lambert lighting.
     */
    VROMaterialVisual &specular = material.getSpecular();
    if (specular.getContentsType() != VROContentsType::Texture2D) {
        loadLambertLighting(material, driver);
        return;
    }
    
    VROMaterialVisual &diffuse = material.getDiffuse();
    VROMaterialVisual &reflective = material.getReflective();
    
    if (diffuse.getContentsType() == VROContentsType::Fixed) {
        _textures.push_back(specular.getContentsTexture());
        samplers.push_back("specular_texture");

        if (reflective.getContentsType() == VROContentsType::TextureCube) {
            _textures.push_back(reflective.getContentsTexture());
            samplers.push_back("reflect_texture");
            
            fragmentShader = "blinn_c_reflect_fsh";
        }
        else {
            fragmentShader = "blinn_c_fsh";
        }
    }
    else {
        _textures.push_back(diffuse.getContentsTexture());
        _textures.push_back(specular.getContentsTexture());
        
        samplers.push_back("diffuse_texture");
        samplers.push_back("specular_texture");
        
        if (reflective.getContentsType() == VROContentsType::TextureCube) {
            _textures.push_back(reflective.getContentsTexture());
            samplers.push_back("reflect_texture");

            fragmentShader = "blinn_t_reflect_fsh";
        }
        else {
            fragmentShader = "blinn_t_fsh";
        }
    }
    
    _program = getPooledShader(vertexShader, fragmentShader, samplers);
    if (!_program->isHydrated()) {
        addUniforms();
        _shininessUniform = _program->addUniform(VROShaderProperty::Float, 1, "material_shininess");
        hydrateProgram();
    }
    else {
        _shininessUniform = _program->getUniform("material_shininess");
        loadUniforms();
    }
}

void VROMaterialSubstrateOpenGL::addUniforms() {
    _program->addUniform(VROShaderProperty::Int, 1, "lighting.num_lights");
    _program->addUniform(VROShaderProperty::Vec3, 1, "lighting.ambient_light_color");
    
    for (int i = 0; i < kMaxLights; i++) {
        std::stringstream ss;
        ss << "lighting.lights[" << i << "].";
        
        std::string prefix = ss.str();
        _program->addUniform(VROShaderProperty::Int, 1, prefix + "type");
        
        _program->addUniform(VROShaderProperty::Vec3, 1, prefix + "position");
        _program->addUniform(VROShaderProperty::Vec3, 1, prefix + "direction");
        _program->addUniform(VROShaderProperty::Vec3, 1, prefix + "color");
        
        _program->addUniform(VROShaderProperty::Float, 1, prefix + "attenuation_start_distance");
        _program->addUniform(VROShaderProperty::Float, 1, prefix + "attenuation_end_distance");
        _program->addUniform(VROShaderProperty::Float, 1, prefix + "attenuation_falloff_exp");
        _program->addUniform(VROShaderProperty::Float, 1, prefix + "spot_inner_angle");
        _program->addUniform(VROShaderProperty::Float, 1, prefix + "spot_outer_angle");
    }
    
    _program->addUniform(VROShaderProperty::Vec3, 1, "ambient_light_color");

    _normalMatrixUniform = _program->addUniform(VROShaderProperty::Mat4, 1, "normal_matrix");
    _modelMatrixUniform = _program->addUniform(VROShaderProperty::Mat4, 1, "model_matrix");
    _modelViewMatrixUniform = _program->addUniform(VROShaderProperty::Mat4, 1, "modelview_matrix");
    _modelViewProjectionMatrixUniform = _program->addUniform(VROShaderProperty::Mat4, 1, "modelview_projection_matrix");
    _cameraPositionUniform = _program->addUniform(VROShaderProperty::Vec3, 1, "camera_position");
    
    _diffuseSurfaceColorUniform = _program->addUniform(VROShaderProperty::Vec4, 1, "material_diffuse_surface_color");
    _diffuseIntensityUniform = _program->addUniform(VROShaderProperty::Float, 1, "material_diffuse_intensity");
    _alphaUniform = _program->addUniform(VROShaderProperty::Float, 1, "material_alpha");
}

void VROMaterialSubstrateOpenGL::loadUniforms() {
    _diffuseSurfaceColorUniform = _program->getUniform("material_diffuse_surface_color");
    _diffuseIntensityUniform = _program->getUniform("material_diffuse_intensity");
    _alphaUniform = _program->getUniform("material_alpha");
    
    _normalMatrixUniform = _program->getUniform("normal_matrix");
    _modelMatrixUniform = _program->getUniform("model_matrix");
    _modelViewMatrixUniform = _program->getUniform("modelview_matrix");
    _modelViewProjectionMatrixUniform = _program->getUniform("modelview_projection_matrix");
    _cameraPositionUniform = _program->getUniform("camera_position");
}

void VROMaterialSubstrateOpenGL::bindShader() {
    _program->bind();
}

void VROMaterialSubstrateOpenGL::bindLights(const std::vector<std::shared_ptr<VROLight>> &lights) {
    pglpush("Lights");
    VROVector3f ambientLight;
    
    VROLightingData data;
    data.num_lights = (int) lights.size();
    
    for (int i = 0; i < lights.size(); i++) {
        const std::shared_ptr<VROLight> &light = lights[i];
        
        data.lights[i].type = (int) light->getType();
        light->getTransformedPosition().toArray(data.lights[i].position);
        light->getDirection().toArray(data.lights[i].direction);
        light->getColor().toArray(data.lights[i].color);
        data.lights[i].attenuation_start_distance = light->getAttenuationStartDistance();
        data.lights[i].attenuation_end_distance = light->getAttenuationEndDistance();
        data.lights[i].attenuation_falloff_exp = light->getAttenuationFalloffExponent();
        data.lights[i].spot_inner_angle = light->getSpotInnerAngle();
        data.lights[i].spot_outer_angle = light->getSpotOuterAngle();
        
        if (light->getType() == VROLightType::Ambient) {
            ambientLight += light->getColor();
        }
    }
    
    ambientLight.toArray(data.ambient_light_color);
    
    glBindBuffer(GL_UNIFORM_BUFFER, _lightingUBO);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(VROLightingData), &data);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
    
    pglpop();
}

void VROMaterialSubstrateOpenGL::bindDepthSettings() {
    if (_material.getWritesToDepthBuffer()) {
        glDepthMask(GL_TRUE);
    }
    else {
        glDepthMask(GL_FALSE);
    }
    
    if (_material.getReadsFromDepthBuffer()) {
        glDepthFunc(GL_LEQUAL);
    }
    else {
        glDepthFunc(GL_ALWAYS);
    }
}

void VROMaterialSubstrateOpenGL::bindViewUniforms(VROMatrix4f transform, VROMatrix4f modelview,
                                                  VROMatrix4f projectionMatrix, VROVector3f cameraPosition) {
    
    if (_normalMatrixUniform != nullptr) {
        _normalMatrixUniform->setMat4(transform.invert().transpose());
    }
    if (_modelMatrixUniform != nullptr) {
        _modelMatrixUniform->setMat4(transform);
    }
    if (_modelViewMatrixUniform != nullptr) {
        _modelViewMatrixUniform->setMat4(modelview);
    }
    if (_modelViewProjectionMatrixUniform != nullptr) {
        _modelViewProjectionMatrixUniform->setMat4(projectionMatrix.multiply(modelview));
    }
    if (_cameraPositionUniform != nullptr) {
        _cameraPositionUniform->setVec3(cameraPosition);
    }
}

void VROMaterialSubstrateOpenGL::bindMaterialUniforms(float opacity) {
    if (_diffuseSurfaceColorUniform != nullptr) {
        _diffuseSurfaceColorUniform->setVec4(_material.getDiffuse().getContentsColor());
    }
    if (_diffuseIntensityUniform != nullptr) {
        _diffuseIntensityUniform->setFloat(_material.getDiffuse().getIntensity());
    }
    if (_alphaUniform != nullptr) {
        _alphaUniform->setFloat(_material.getTransparency() * opacity);
    }
    if (_shininessUniform != nullptr) {
        _shininessUniform->setFloat(_material.getShininess());
    }
}

std::shared_ptr<VROShaderProgram> VROMaterialSubstrateOpenGL::getPooledShader(std::string vertexShader,
                                                                              std::string fragmentShader,
                                                                              const std::vector<std::string> &samplers) {
    std::string name = vertexShader + "_" + fragmentShader;
    
    std::map<std::string, std::shared_ptr<VROShaderProgram>>::iterator it = _sharedPrograms.find(name);
    if (it == _sharedPrograms.end()) {
        std::shared_ptr<VROShaderProgram> program = std::make_shared<VROShaderProgram>(vertexShader, fragmentShader,
                                                                                       ((int)VROShaderMask::Tex | (int)VROShaderMask::Norm));
        for (const std::string &sampler : samplers) {
            program->addSampler(sampler);
        }
        _sharedPrograms[name] = program;
        return program;
    }
    else {
        return it->second;
    }
}

void VROMaterialSubstrateOpenGL::updateSortKey(VROSortKey &key) const {
    key.shader = _program->getShaderId();
    key.textures = hashTextures(_textures);
}

uint32_t VROMaterialSubstrateOpenGL::hashTextures(const std::vector<std::shared_ptr<VROTexture>> &textures) const {
    uint32_t h = 0;
    for (const std::shared_ptr<VROTexture> &texture : textures) {
        h = 31 * h + texture->getTextureId();
    }
    return h;
}
